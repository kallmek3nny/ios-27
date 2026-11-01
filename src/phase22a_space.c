#define _GNU_SOURCE
#include "ios27/phase22a_space.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
    uint64_t guest;
    uint64_t length;
    uint64_t reserved_start;
    uint64_t reserved_length;
    uint8_t *host;
    unsigned permissions;
    uint32_t generation;
    uint32_t pins;
    int live;
} region;

typedef struct {
    uint32_t generation;
    uint32_t region_index;
    uint32_t region_generation;
    int live;
} borrow;

struct ios27_22a_space {
    pthread_mutex_t mutex;
    ios27_22a_config config;
    region regions[IOS27_22A_MAX_REGIONS];
    borrow borrows[IOS27_22A_MAX_PINS];
    uint64_t committed;
    uint32_t active;
    uint32_t pins;
    uint32_t failure_countdown;
    int inject_failure;
    unsigned failure_stage;
    int closed;
    int quiescing;
};

static int valid_permissions(unsigned perms) {
    return perms != 0u && !(perms & ~(IOS27_22A_READ | IOS27_22A_WRITE |
                                      IOS27_22A_EXEC)) &&
           !(perms & IOS27_22A_WRITE && perms & IOS27_22A_EXEC) &&
           !(perms & IOS27_22A_WRITE && !(perms & IOS27_22A_READ)) &&
           !(perms & IOS27_22A_EXEC && !(perms & IOS27_22A_READ));
}

static int host_protection(unsigned perms) {
    return ((perms & IOS27_22A_READ) ? PROT_READ : 0) |
           ((perms & IOS27_22A_WRITE) ? PROT_WRITE : 0) |
           ((perms & IOS27_22A_EXEC) ? PROT_EXEC : 0);
}

static int valid_span(const ios27_22a_space *s, uint64_t addr, uint64_t len) {
    return len > 0u && addr >= IOS27_22A_PAGE &&
           addr < s->config.address_limit &&
           len <= s->config.address_limit - addr;
}

static uint64_t make_handle(uint32_t generation, unsigned index) {
    return ((uint64_t)generation << 32u) | (uint64_t)(index + 1u);
}

static int handle_index(uint64_t id, unsigned capacity, unsigned *index,
                        uint32_t *generation) {
    uint32_t slot = (uint32_t)id;
    *generation = (uint32_t)(id >> 32u);
    if (!*generation || slot == 0u || slot > capacity) return 0;
    *index = slot - 1u;
    return 1;
}

static int resolve_region(ios27_22a_space *s, uint64_t handle, region **out) {
    unsigned index;
    uint32_t generation;
    region *r;
    if (!handle_index(handle, IOS27_22A_MAX_REGIONS, &index, &generation))
        return IOS27_22A_INVALID;
    r = &s->regions[index];
    if (!r->live || r->generation != generation) return IOS27_22A_STALE;
    *out = r;
    return IOS27_22A_OK;
}

static region *find_at(ios27_22a_space *s, uint64_t address) {
    for (unsigned i = 0u; i < s->config.region_limit; ++i) {
        region *r = &s->regions[i];
        if (r->live && address >= r->guest && address - r->guest < r->length)
            return r;
    }
    return NULL;
}

/* Preflight EVERY map seam before copying. No partially written guest range. */
static int check_range(ios27_22a_space *s, uint64_t address, uint64_t bytes,
                       uint64_t alignment, unsigned permissions) {
    uint64_t cursor, remaining;
    if (!valid_span(s, address, bytes) || !alignment ||
        (alignment & (alignment - 1u)) || address % alignment ||
        !permissions || (permissions & ~(IOS27_22A_READ | IOS27_22A_WRITE |
                                         IOS27_22A_EXEC)))
        return IOS27_22A_INVALID;
    cursor = address;
    remaining = bytes;
    while (remaining) {
        region *r = find_at(s, cursor);
        uint64_t available;
        if (!r) return IOS27_22A_FAULT;
        if ((r->permissions & permissions) != permissions)
            return IOS27_22A_PERM;
        available = r->length - (cursor - r->guest);
        if (available > remaining) available = remaining;
        remaining -= available;
        cursor += available;
    }
    return IOS27_22A_OK;
}

int ios27_22a_create(const ios27_22a_config *config, ios27_22a_space **out) {
    ios27_22a_space *s;
    long page;
    if (!out) return IOS27_22A_INVALID;
    *out = NULL;
    page = sysconf(_SC_PAGESIZE);
    if (!config || page != (long)IOS27_22A_PAGE ||
        config->address_limit < 3u * IOS27_22A_PAGE ||
        config->address_limit % IOS27_22A_PAGE ||
        config->committed_limit < IOS27_22A_PAGE ||
        config->committed_limit > config->address_limit ||
        !config->region_limit || config->region_limit > IOS27_22A_MAX_REGIONS)
        return IOS27_22A_INVALID;
    s = calloc(1u, sizeof(*s));
    if (!s) return IOS27_22A_LIMIT;
    s->config = *config;
    if (pthread_mutex_init(&s->mutex, NULL) != 0) {
        free(s);
        return IOS27_22A_LIMIT;
    }
    *out = s;
    return IOS27_22A_OK;
}

int ios27_22a_map(ios27_22a_space *s, uint64_t address, uint64_t bytes,
                  unsigned permissions, unsigned guards, uint64_t *handle) {
    uint64_t before, after, start, reserved;
    unsigned slot;
    void *p;
    region *r;
    int result = IOS27_22A_INVALID;
    if (!s || !handle) return result;
    *handle = 0u;
    pthread_mutex_lock(&s->mutex);
    if (s->closed || s->quiescing) { result = IOS27_22A_CLOSED; goto done; }
    if (!valid_permissions(permissions) ||
        (guards & ~(IOS27_22A_GUARD_BEFORE | IOS27_22A_GUARD_AFTER)) ||
        address % IOS27_22A_PAGE || bytes % IOS27_22A_PAGE ||
        !valid_span(s, address, bytes)) goto done;
    before = (guards & IOS27_22A_GUARD_BEFORE) ? IOS27_22A_PAGE : 0u;
    after = (guards & IOS27_22A_GUARD_AFTER) ? IOS27_22A_PAGE : 0u;
    if (address < IOS27_22A_PAGE + before ||
        after > s->config.address_limit - address ||
        bytes > s->config.address_limit - address - after ||
        s->committed > s->config.committed_limit ||
        bytes > s->config.committed_limit - s->committed) {
        result = IOS27_22A_LIMIT;
        goto done;
    }
    start = address - before;
    reserved = bytes + before + after;
    for (unsigned i = 0u; i < s->config.region_limit; ++i) {
        region *other = &s->regions[i];
        if (other->live && start < other->reserved_start + other->reserved_length &&
            other->reserved_start < start + reserved) {
            result = IOS27_22A_BUSY;
            goto done;
        }
    }
    for (slot = 0u; slot < s->config.region_limit; ++slot)
        if (!s->regions[slot].live && s->regions[slot].generation != UINT32_MAX)
            break;
    if (slot == s->config.region_limit) { result = IOS27_22A_LIMIT; goto done; }
    if (reserved > SIZE_MAX) { result = IOS27_22A_LIMIT; goto done; }
    if (s->inject_failure && !s->failure_countdown) {
        s->inject_failure = 0;
        result = IOS27_22A_LIMIT;
        goto done;
    }
    p = mmap(NULL, (size_t)reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
             -1, 0);
    if (p == MAP_FAILED) { result = IOS27_22A_LIMIT; goto done; }
    if (s->failure_stage == 1u) {
        s->failure_stage = 0u;
        (void)munmap(p, (size_t)reserved);
        result = IOS27_22A_LIMIT;
        goto done;
    }
    if (mprotect((uint8_t *)p + before, (size_t)bytes,
                 host_protection(permissions)) != 0) {
        (void)munmap(p, (size_t)reserved);
        result = IOS27_22A_LIMIT;
        goto done;
    }
    if (s->failure_stage == 2u) {
        s->failure_stage = 0u;
        (void)munmap(p, (size_t)reserved);
        result = IOS27_22A_LIMIT;
        goto done;
    }
    if (s->inject_failure && s->failure_countdown) --s->failure_countdown;
    r = &s->regions[slot];
    ++r->generation;
    r->guest = address;
    r->length = bytes;
    r->reserved_start = start;
    r->reserved_length = reserved;
    r->host = p;
    r->permissions = permissions;
    r->pins = 0u;
    r->live = 1;
    s->committed += bytes;
    ++s->active;
    *handle = make_handle(r->generation, slot);
    result = IOS27_22A_OK;
done:
    pthread_mutex_unlock(&s->mutex);
    return result;
}

int ios27_22a_map_stack(ios27_22a_space *s, uint64_t address,
                        uint64_t bytes, uint64_t *handle) {
    return ios27_22a_map(s, address, bytes, IOS27_22A_READ | IOS27_22A_WRITE,
                         IOS27_22A_GUARD_BEFORE | IOS27_22A_GUARD_AFTER,
                         handle);
}

int ios27_22a_protect(ios27_22a_space *s, uint64_t handle,
                      unsigned permissions) {
    region *r = NULL;
    int result;
    if (!s) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    result = (s->closed || s->quiescing) ? IOS27_22A_CLOSED : resolve_region(s, handle, &r);
    if (!result) {
        if (!valid_permissions(permissions)) result = IOS27_22A_INVALID;
        else if (r->pins) result = IOS27_22A_BUSY;
        else if (mprotect(r->host + (r->guest - r->reserved_start),
                          (size_t)r->length, host_protection(permissions)) != 0)
            result = IOS27_22A_LIMIT;
        else r->permissions = permissions;
    }
    pthread_mutex_unlock(&s->mutex);
    return result;
}

int ios27_22a_unmap(ios27_22a_space *s, uint64_t handle) {
    region *r = NULL;
    int result;
    if (!s) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    result = (s->closed || s->quiescing) ? IOS27_22A_CLOSED : resolve_region(s, handle, &r);
    if (!result) {
        if (r->pins) result = IOS27_22A_BUSY;
        else if (munmap(r->host, (size_t)r->reserved_length) != 0)
            result = IOS27_22A_LIMIT;
        else {
            s->committed -= r->length;
            --s->active;
            r->host = NULL;
            r->live = 0;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return result;
}

int ios27_22a_probe(ios27_22a_space *s, uint64_t address,
                    uint64_t bytes, uint64_t alignment, unsigned permissions) {
    int result;
    if (!s) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    result = (s->closed || s->quiescing) ? IOS27_22A_CLOSED :
             check_range(s, address, bytes, alignment, permissions);
    pthread_mutex_unlock(&s->mutex);
    return result;
}

static int copy_bytes(ios27_22a_space *s, uint64_t address, void *host,
                      size_t bytes, int to_guest) {
    uint8_t *h = host;
    uint64_t cursor = address;
    size_t remaining = bytes;
    int result;
    if (!s || !host) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    result = (s->closed || s->quiescing) ? IOS27_22A_CLOSED : check_range(s, address,
                    (uint64_t)bytes, 1u, to_guest ?
                    IOS27_22A_WRITE : IOS27_22A_READ);
    if (result) goto done;
    while (remaining) {
        region *r = find_at(s, cursor);
        uint64_t offset = cursor - r->guest;
        size_t n = (size_t)(r->length - offset);
        uint8_t *guest = r->host + (r->guest - r->reserved_start) + offset;
        if (n > remaining) n = remaining;
        if (to_guest) memmove(guest, h, n);
        else memmove(h, guest, n);
        cursor += n;
        h += n;
        remaining -= n;
    }
done:
    pthread_mutex_unlock(&s->mutex);
    return result;
}

int ios27_22a_copyin(ios27_22a_space *s, void *destination,
                     uint64_t address, size_t bytes) {
    return copy_bytes(s, address, destination, bytes, 0);
}
int ios27_22a_copyout(ios27_22a_space *s, uint64_t address,
                      const void *source, size_t bytes) {
    return copy_bytes(s, address, (void *)source, bytes, 1);
}

int ios27_22a_pin(ios27_22a_space *s, uint64_t address,
                  size_t bytes, uint64_t alignment, unsigned permissions,
                  ios27_22a_view *view) {
    region *r;
    unsigned slot, region_index;
    int result;
    if (!s || !view) return IOS27_22A_INVALID;
    memset(view, 0, sizeof(*view));
    pthread_mutex_lock(&s->mutex);
    result = (s->closed || s->quiescing) ? IOS27_22A_CLOSED :
             check_range(s, address, (uint64_t)bytes, alignment, permissions);
    if (result) goto done;
    r = find_at(s, address);
    if ((uint64_t)bytes > r->length - (address - r->guest)) {
        result = IOS27_22A_INVALID;
        goto done;
    }
    for (slot = 0u; slot < IOS27_22A_MAX_PINS; ++slot)
        if (!s->borrows[slot].live && s->borrows[slot].generation != UINT32_MAX)
            break;
    if (slot == IOS27_22A_MAX_PINS) { result = IOS27_22A_LIMIT; goto done; }
    region_index = (unsigned)(r - s->regions);
    borrow *b = &s->borrows[slot];
    ++b->generation;
    b->region_index = region_index;
    b->region_generation = r->generation;
    b->live = 1;
    ++r->pins;
    ++s->pins;
    view->host = r->host + (r->guest - r->reserved_start) + (address - r->guest);
    view->length = bytes;
    view->token = make_handle(b->generation, slot);
done:
    pthread_mutex_unlock(&s->mutex);
    return result;
}

int ios27_22a_unpin(ios27_22a_space *s, ios27_22a_view *view) {
    unsigned slot;
    uint32_t generation;
    int result = IOS27_22A_STALE;
    if (!s || !view || !handle_index(view->token, IOS27_22A_MAX_PINS,
                                     &slot, &generation)) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    borrow *b = &s->borrows[slot];
    if (b->live && b->generation == generation) {
        region *r = &s->regions[b->region_index];
        if (r->live && r->generation == b->region_generation && r->pins) {
            --r->pins;
            --s->pins;
            b->live = 0;
            memset(view, 0, sizeof(*view));
            result = IOS27_22A_OK;
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return result;
}

int ios27_22a_snapshot(ios27_22a_space *s, ios27_22a_stats *stats) {
    if (!s || !stats) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    stats->committed = s->committed;
    stats->regions = s->active;
    stats->pins = s->pins;
    stats->closed = (uint32_t)s->closed;
    stats->quiescing = (uint32_t)s->quiescing;
    pthread_mutex_unlock(&s->mutex);
    return IOS27_22A_OK;
}

int ios27_22a_fail_map_after(ios27_22a_space *s, uint32_t count) {
    if (!s) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    if (s->closed || s->quiescing) { pthread_mutex_unlock(&s->mutex); return IOS27_22A_CLOSED; }
    s->failure_countdown = count;
    s->inject_failure = 1;
    pthread_mutex_unlock(&s->mutex);
    return IOS27_22A_OK;
}

int ios27_22a_fail_stage(ios27_22a_space *s, unsigned stage) {
    if (!s || stage > 2u) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    if (s->closed || s->quiescing) { pthread_mutex_unlock(&s->mutex); return IOS27_22A_CLOSED; }
    s->failure_stage = stage;
    pthread_mutex_unlock(&s->mutex);
    return IOS27_22A_OK;
}

int ios27_22a_quiesce(ios27_22a_space *s) {
    int result;
    if (!s) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    s->quiescing = 1;
    result = s->pins ? IOS27_22A_BUSY : IOS27_22A_OK;
    pthread_mutex_unlock(&s->mutex);
    return result;
}

int ios27_22a_close(ios27_22a_space *s) {
    if (!s) return IOS27_22A_INVALID;
    pthread_mutex_lock(&s->mutex);
    if (s->pins) { pthread_mutex_unlock(&s->mutex); return IOS27_22A_BUSY; }
    if (!s->closed) {
        for (unsigned i = 0u; i < s->config.region_limit; ++i) {
            region *r = &s->regions[i];
            if (!r->live) continue;
            if (munmap(r->host, (size_t)r->reserved_length) != 0) {
                pthread_mutex_unlock(&s->mutex);
                return IOS27_22A_LIMIT;
            }
            s->committed -= r->length;
            --s->active;
            r->host = NULL;
            r->live = 0;
        }
        s->closed = 1;
    }
    pthread_mutex_unlock(&s->mutex);
    return IOS27_22A_OK;
}

int ios27_22a_destroy(ios27_22a_space **s) {
    ios27_22a_space *space;
    int result;
    if (!s || !*s) return IOS27_22A_INVALID;
    space = *s;
    result = ios27_22a_close(space);
    if (result) return result;
    (void)pthread_mutex_destroy(&space->mutex);
    free(space);
    *s = NULL;
    return IOS27_22A_OK;
}
