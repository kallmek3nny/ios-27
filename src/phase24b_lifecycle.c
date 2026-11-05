#define _POSIX_C_SOURCE 200809L
#include "ios27/phase24b_lifecycle.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#define P24B_MAGIC 0xfeedfacfu
#define P24B_CPU_ARM64 0x0100000cu
#define P24B_MH_EXECUTE 2u
#define P24B_MH_DYLIB 6u
#define P24B_LC_SEGMENT64 0x19u
#define P24B_PROT_W 2u
#define P24B_PROT_X 4u
#define P24B_INIT_PTRS 0x9u
#define P24B_TERM_PTRS 0xau
#define P24B_INIT_OFFSETS 0x16u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static int fits(size_t off, uint64_t n, size_t len) {
    return off <= len && n <= (uint64_t)(len - off);
}
static int sum64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) return EOVERFLOW;
    *out = a + b;
    return 0;
}
static int name_eq(const uint8_t *p, const char *wanted) {
    size_t n = strlen(wanted);
    return n <= 16 && !memcmp(p, wanted, n) && (n == 16 || !p[n]);
}

int ios27_24b_parse(const uint8_t *bytes, size_t size, ios27_24b_image *out) {
    ios27_24b_image tmp = {0};
    uint32_t count, cmdbytes;
    unsigned saw_init = 0, saw_term = 0, saw_offsets = 0, saw_text = 0;
    size_t cursor, end;
    if (!bytes || !out || size < 32 || rd32(bytes) != P24B_MAGIC ||
        rd32(bytes + 4) != P24B_CPU_ARM64 ||
        (rd32(bytes + 12) != P24B_MH_DYLIB &&
         rd32(bytes + 12) != P24B_MH_EXECUTE)) return ENOEXEC;
    count = rd32(bytes + 16);
    cmdbytes = rd32(bytes + 20);
    if (count > 128 || cmdbytes > 1048576 || !fits(32, cmdbytes, size))
        return ENOEXEC;
    cursor = 32;
    end = cursor + cmdbytes;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t cmd, cmdsize;
        if (!fits(cursor, 8, end)) return ENOEXEC;
        cmd = rd32(bytes + cursor);
        cmdsize = rd32(bytes + cursor + 4);
        if (cmdsize < 8 || (cmdsize & 7u) || !fits(cursor, cmdsize, end))
            return ENOEXEC;
        if (cmd == P24B_LC_SEGMENT64) {
            const uint8_t *seg = bytes + cursor;
            uint64_t vmaddr, vmsize, fileoff, filesize, segend;
            uint32_t sections, prot;
            if (cmdsize < 72) return ENOEXEC;
            vmaddr = rd64(seg + 24);
            vmsize = rd64(seg + 32);
            fileoff = rd64(seg + 40);
            filesize = rd64(seg + 48);
            prot = rd32(seg + 60);
            sections = rd32(seg + 64);
            if (sections > 128 || (uint64_t)sections * 80u + 72u > cmdsize ||
                sum64(vmaddr, vmsize, &segend) ||
                fileoff > SIZE_MAX || !fits((size_t)fileoff, filesize, size) ||
                filesize > vmsize) return ENOEXEC;
            if (name_eq(seg + 8, "__TEXT")) {
                if (++saw_text != 1 || !(prot & P24B_PROT_X)) return ENOEXEC;
                tmp.text_vmaddr = vmaddr;
            }
            if (prot & P24B_PROT_X) {
                if ((prot & P24B_PROT_W) || !vmsize ||
                    tmp.executable_count == IOS27_24B_RANGES) return ENOEXEC;
                for (size_t x = 0; x < tmp.executable_count; ++x)
                    if (vmaddr < tmp.executable[x].end &&
                        tmp.executable[x].start < segend) return ENOEXEC;
                tmp.executable[tmp.executable_count++] =
                    (ios27_24b_range){vmaddr, segend};
            }
            for (uint32_t s = 0; s < sections; ++s) {
                const uint8_t *sec = seg + 72u + s * 80u;
                int init = name_eq(sec, "__mod_init_func");
                int term = name_eq(sec, "__mod_term_func");
                int offs = name_eq(sec, "__init_offsets");
                uint64_t addr, length, fileoffset;
                uint32_t type;
                unsigned width;
                if (!init && !term && !offs) continue;
                addr = rd64(sec + 32);
                length = rd64(sec + 40);
                fileoffset = rd32(sec + 48);
                type = rd32(sec + 64) & 0xffu;
                width = offs ? 4u : 8u;
                if ((init && type != P24B_INIT_PTRS) ||
                    (term && type != P24B_TERM_PTRS) ||
                    (offs && type != P24B_INIT_OFFSETS) ||
                    (addr % width) || (fileoffset % width) ||
                    (length % width) ||
                    length > IOS27_24B_CALLBACKS * (uint64_t)width ||
                    addr < vmaddr || addr - vmaddr > vmsize ||
                    length > vmsize - (addr - vmaddr) ||
                    fileoffset < fileoff || fileoffset - fileoff > filesize ||
                    length > filesize - (fileoffset - fileoff) ||
                    !fits((size_t)fileoffset, length, size)) return ENOEXEC;
                if (init) {
                    if (saw_init++) return ENOEXEC;
                    tmp.init_table = addr;
                    tmp.init_count = (size_t)(length / width);
                } else if (term) {
                    if (saw_term++) return ENOEXEC;
                    tmp.term_table = addr;
                    tmp.term_count = (size_t)(length / width);
                } else {
                    if (saw_offsets++) return ENOEXEC;
                    tmp.offset_table = addr;
                    tmp.offset_count = (size_t)(length / width);
                }
            }
        }
        cursor += cmdsize;
    }
    /* Mixing two initializer formats has no established ordering contract. */
    if (cursor != end || !saw_text || !tmp.executable_count ||
        (saw_init && saw_offsets) ||
        tmp.init_count + tmp.offset_count > IOS27_24B_CALLBACKS)
        return ENOEXEC;
    tmp.parsed = 1;
    *out = tmp;
    return 0;
}

static int executable(const ios27_24b_image *im, uint64_t pc) {
    if (!pc || (pc & 3u)) return 0;
    for (size_t i = 0; i < im->executable_count; ++i)
        if (pc >= im->executable[i].start &&
            pc < im->executable[i].end &&
            im->executable[i].end - pc >= 4u) return 1;
    return 0;
}

int ios27_24b_bind(ios27_24b_image *image, uint64_t slide,
                   ios27_24b_read reader, void *context) {
    ios27_24b_image tmp;
    uint64_t text;
    if (!image || !reader || !image->parsed || image->bound ||
        image->init_count + image->offset_count > IOS27_24B_CALLBACKS ||
        image->term_count > IOS27_24B_CALLBACKS ||
        !image->executable_count ||
        image->executable_count > IOS27_24B_RANGES) return EINVAL;
    tmp = *image;
    if (sum64(tmp.text_vmaddr, slide, &text)) return EOVERFLOW;
    for (size_t i = 0; i < tmp.executable_count; ++i) {
        if (sum64(tmp.executable[i].start, slide,
                  &tmp.executable[i].start) ||
            sum64(tmp.executable[i].end, slide,
                  &tmp.executable[i].end)) return EOVERFLOW;
    }
    for (unsigned which = 0; which < 3u; ++which) {
        uint64_t table = which == 0 ? tmp.init_table :
                         which == 1 ? tmp.offset_table : tmp.term_table;
        size_t n = which == 0 ? tmp.init_count :
                   which == 1 ? tmp.offset_count : tmp.term_count;
        unsigned width = which == 1 ? 4u : 8u;
        uint64_t start;
        if (!n) continue;
        if (sum64(table, slide, &start) ||
            (n - 1) > (UINT64_MAX - start) / width)
            return EOVERFLOW;
        for (size_t i = 0; i < n; ++i) {
            uint64_t value = 0, pc = 0;
            int err = reader(context, start + i * width, width, &value);
            if (err) return ENOEXEC;
            if (which == 1) {
                if (value > UINT32_MAX || sum64(text, value, &pc))
                    return ENOEXEC;
            } else pc = value;
            if (!executable(&tmp, pc)) return ENOEXEC;
            if (which == 2) tmp.term[i] = pc;
            else tmp.init[which == 1 ? tmp.init_count + i : i] = pc;
        }
    }
    tmp.init_count += tmp.offset_count;
    tmp.offset_count = 0;
    tmp.bound = 1;
    *image = tmp;                 /* no partial table writes on error */
    return 0;
}

static void diagnostic(ios27_24b_runtime *r, unsigned image,
                       ios27_24b_diagnostic why, int err) {
    r->last_image = image;
    r->last_diagnostic = why;
    r->last_error = err;
}
int ios27_24b_runtime_init(ios27_24b_runtime *r,
                           ios27_24b_execution_kind kind) {
    int err;
    if (!r || (kind != IOS27_24B_MODELED &&
               kind != IOS27_24B_NATIVE_VERIFIED)) return EINVAL;
    memset(r, 0, sizeof(*r));
    err = pthread_mutex_init(&r->mutex, NULL);
    if (err) return err;
    err = pthread_cond_init(&r->changed, NULL);
    if (err) { (void)pthread_mutex_destroy(&r->mutex); return err; }
    r->initialized = 1;
    r->kind = kind;
    return 0;
}
int ios27_24b_register(ios27_24b_runtime *r, unsigned index,
                       const ios27_24b_image *im, const unsigned *deps,
                       size_t dep_count) {
    if (!r || !r->initialized || !im || !im->bound ||
        index >= IOS27_24B_IMAGES || dep_count > IOS27_24B_DEPS ||
        (dep_count && !deps) || im->init_count > IOS27_24B_CALLBACKS ||
        im->term_count > IOS27_24B_CALLBACKS ||
        !im->executable_count || im->executable_count > IOS27_24B_RANGES)
        return EINVAL;
    for (size_t i = 0; i < im->init_count; ++i)
        if (!executable(im, im->init[i])) return ENOEXEC;
    for (size_t i = 0; i < im->term_count; ++i)
        if (!executable(im, im->term[i])) return ENOEXEC;
    for (size_t i = 0; i < dep_count; ++i)
        if (deps[i] == index || deps[i] >= IOS27_24B_IMAGES) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    ios27_24b_slot *slot = &r->slots[index];
    int err = r->gate ? EBUSY : slot->registered ? EEXIST : 0;
    if (!err) {
        slot->image = *im;
        if (dep_count) memcpy(slot->deps, deps, dep_count * sizeof(*deps));
        slot->dep_count = dep_count;
        slot->registered = 1;
        slot->generation = 1;
        if (r->count <= index) r->count = (size_t)index + 1;
    }
    pthread_mutex_unlock(&r->mutex);
    return err;
}
static int dfs(ios27_24b_runtime *r, unsigned id, unsigned char *mark,
               unsigned *order, size_t *used) {
    if (id >= r->count || !r->slots[id].registered) return ENOENT;
    if (mark[id] == 1) return ELOOP;
    if (mark[id] == 2) return 0;
    mark[id] = 1;
    const ios27_24b_slot *s = &r->slots[id];
    for (size_t i = 0; i < s->dep_count; ++i) {
        int err = dfs(r, s->deps[i], mark, order, used);
        if (err) return err;
    }
    mark[id] = 2;
    order[(*used)++] = id;
    return 0;
}
static int graph(ios27_24b_runtime *r, unsigned id,
                 unsigned *order, size_t *n) {
    unsigned char mark[IOS27_24B_IMAGES] = {0};
    *n = 0;
    return dfs(r, id, mark, order, n);
}
static int locate(ios27_24b_runtime *r, ios27_24b_handle h) {
    if (!h.ticket || h.index >= r->count ||
        !r->slots[h.index].registered ||
        r->slots[h.index].generation != h.generation) return -1;
    for (size_t i = 0; i < IOS27_24B_HANDLES; ++i)
        if (r->leases[i].ticket == h.ticket &&
            r->leases[i].index == h.index) return (int)i;
    return -1;
}
static int gating(ios27_24b_runtime *r, int wait, unsigned index) {
    if (!wait && r->gate) {
        int e = pthread_equal(r->gate_owner, pthread_self()) ? EDEADLK : EBUSY;
        diagnostic(r, index, IOS27_24B_DIAG_REENTRANT, e);
        return e;
    }
    if (wait && r->gate) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts)) return errno;
        ts.tv_sec += 3;
        while (r->gate) {
            if (pthread_equal(r->gate_owner, pthread_self())) {
                diagnostic(r, index, IOS27_24B_DIAG_REENTRANT, EDEADLK);
                return EDEADLK;
            }
            int e = pthread_cond_timedwait(&r->changed, &r->mutex, &ts);
            if (e) {
                diagnostic(r, index, IOS27_24B_DIAG_TIMEOUT, e);
                return e;
            }
        }
    }
    r->gate = 1;
    r->gate_owner = pthread_self();
    return 0;
}
static void ungate(ios27_24b_runtime *r) {
    r->gate = 0;
    pthread_cond_broadcast(&r->changed);
}
int ios27_24b_open(ios27_24b_runtime *r, unsigned index,
                   ios27_24b_handle *out) {
    unsigned order[IOS27_24B_IMAGES];
    size_t n = 0, free_lease = IOS27_24B_HANDLES;
    int err;
    if (!r || !r->initialized || !out) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    if (r->gate) {
        err = pthread_equal(r->gate_owner, pthread_self()) ? EDEADLK : EBUSY;
        diagnostic(r, index, IOS27_24B_DIAG_REENTRANT, err);
        pthread_mutex_unlock(&r->mutex);
        return err;
    }
    err = graph(r, index, order, &n);
    if (err) diagnostic(r, index,
                         err == ELOOP ? IOS27_24B_DIAG_CYCLE :
                         IOS27_24B_DIAG_MALFORMED, err);
    if (!err) for (size_t i = 0; i < n; ++i) {
        ios27_24b_slot *s = &r->slots[order[i]];
        if (s->refs == IOS27_24B_REF_LIMIT) err = EOVERFLOW;
        if (s->state == IOS27_24B_QUARANTINED) err = ENOTRECOVERABLE;
    }
    if (!err) for (size_t i = 0; i < IOS27_24B_HANDLES; ++i)
        if (!r->leases[i].ticket) { free_lease = i; break; }
    if (!err && (free_lease == IOS27_24B_HANDLES ||
                 r->next_ticket == UINT64_MAX))
        err = EMFILE;
    if (!err) {
        for (size_t i = 0; i < n; ++i) ++r->slots[order[i]].refs;
        uint64_t ticket = ++r->next_ticket;
        r->leases[free_lease] = (ios27_24b_lease){ticket, index, 0};
        *out = (ios27_24b_handle){index, r->slots[index].generation, ticket};
        ++r->transitions;
    }
    pthread_mutex_unlock(&r->mutex);
    return err;
}
static int invoke(ios27_24b_runtime *r, unsigned id,
                  ios27_24b_action action, uint64_t pc, size_t ordinal,
                  ios27_24b_dispatch bridge, void *context) {
    if (!bridge) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    ++r->slots[id].active;
    pthread_mutex_unlock(&r->mutex);
    int err = bridge(context, id, action, pc, ordinal);
    pthread_mutex_lock(&r->mutex);
    --r->slots[id].active;
    ++r->callbacks;
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->mutex);
    return err;
}
int ios27_24b_ensure(ios27_24b_runtime *r, ios27_24b_handle h,
                     ios27_24b_dispatch execute,
                     ios27_24b_dispatch compensate, void *context) {
    unsigned order[IOS27_24B_IMAGES];
    size_t n = 0;
    int err, lease;
    if (!r || !r->initialized || !execute) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    lease = locate(r, h);
    err = lease < 0 ? ESTALE : graph(r, h.index, order, &n);
    if (!err) err = gating(r, 1, h.index);
    if (!err) ++r->leases[lease].active;
    pthread_mutex_unlock(&r->mutex);
    if (err) return err;
    for (size_t j = 0; j < n; ++j) {
        unsigned id = order[j];
        ios27_24b_slot *s = &r->slots[id];
        if (s->state == IOS27_24B_READY) continue;
        if (s->state == IOS27_24B_FAILED ||
            s->state == IOS27_24B_QUARANTINED) {
            err = s->failure ? s->failure : ENOTRECOVERABLE;
            break;
        }
        if (s->state != IOS27_24B_NEW) { err = EBUSY; break; }
        s->state = IOS27_24B_STARTING;
        ++r->transitions;
        size_t done = 0;
        for (; done < s->image.init_count; ++done) {
            err = invoke(r, id, IOS27_24B_INIT, s->image.init[done],
                         done, execute, context);
            if (err) break;
            s->completed = (unsigned)(done + 1);
        }
        if (err) {
            int rollback_ok = 1;
            for (size_t k = done; k > 0; --k) {
                if (!compensate || invoke(r, id, IOS27_24B_COMPENSATE,
                     s->image.init[k - 1], k - 1, compensate, context))
                    rollback_ok = 0;
            }
            s->failure = err;
            s->poisoned = !rollback_ok;
            s->state = rollback_ok ? IOS27_24B_FAILED :
                                      IOS27_24B_QUARANTINED;
            if (rollback_ok) s->completed = 0;
            pthread_mutex_lock(&r->mutex);
            diagnostic(r, id, rollback_ok ? IOS27_24B_DIAG_CALLBACK_FAILED :
                       IOS27_24B_DIAG_COMPENSATION_FAILED, err);
            pthread_mutex_unlock(&r->mutex);
            break;
        }
        s->state = IOS27_24B_READY;
        ++r->startups;
    }
    pthread_mutex_lock(&r->mutex);
    --r->leases[lease].active;
    ungate(r);
    pthread_mutex_unlock(&r->mutex);
    return err;
}
int ios27_24b_close(ios27_24b_runtime *r, ios27_24b_handle h,
                    ios27_24b_dispatch execute, void *context) {
    unsigned order[IOS27_24B_IMAGES];
    size_t n = 0;
    int err, lease, first = 0, acquired = 0;
    if (!r || !r->initialized || !execute) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    lease = locate(r, h);
    err = lease < 0 ? ESTALE : graph(r, h.index, order, &n);
    if (!err && r->leases[lease].active) err = EBUSY;
    if (!err) {
        err = gating(r, 0, h.index);
        acquired = !err;
    }
    if (!err) for (size_t j = 0; j < n; ++j) {
        ios27_24b_slot *s = &r->slots[order[j]];
        if (!s->refs) err = ESTALE;
        if (s->refs == 1 && s->state == IOS27_24B_QUARANTINED)
            err = ENOTRECOVERABLE;
    }
    if (err) {
        if (acquired) ungate(r);
        pthread_mutex_unlock(&r->mutex);
        return err;
    }
    r->leases[lease].ticket = 0;
    for (size_t j = 0; j < n; ++j) --r->slots[order[j]].refs;
    ++r->transitions;
    pthread_mutex_unlock(&r->mutex);
    for (size_t j = n; j > 0; --j) {
        unsigned id = order[j - 1];
        ios27_24b_slot *s = &r->slots[id];
        if (s->refs) continue;
        if (first) {
            s->state = IOS27_24B_QUARANTINED;
            s->poisoned = 1;
            continue; /* dependencies stay mapped after failed destructor */
        }
        if (s->state == IOS27_24B_READY) {
            s->state = IOS27_24B_FINALIZING;
            for (size_t k = s->image.term_count; k > 0; --k) {
                int result = invoke(r, id, IOS27_24B_TERM,
                                    s->image.term[k - 1], k - 1,
                                    execute, context);
                if (result && !first) {
                    first = result;
                    s->failure = result;
                    s->poisoned = 1;
                    s->state = IOS27_24B_QUARANTINED;
                    pthread_mutex_lock(&r->mutex);
                    diagnostic(r, id, IOS27_24B_DIAG_TERMINATOR_FAILED,
                               result);
                    pthread_mutex_unlock(&r->mutex);
                    break; /* unknown side effects: stop finalizing this image */
                }
            }
            if (!first) ++r->shutdowns;
        }
        if (!first) {
            s->state = IOS27_24B_NEW;
            s->completed = 0;
            s->failure = 0;
            s->poisoned = 0;
            ++s->generation;
            if (!s->generation) ++s->generation;
        }
        ++r->transitions;
    }
    pthread_mutex_lock(&r->mutex);
    ungate(r);
    pthread_mutex_unlock(&r->mutex);
    return first;
}
int ios27_24b_inspect(ios27_24b_runtime *r, unsigned id,
                      ios27_24b_snapshot *out) {
    if (!r || !r->initialized || !out || id >= IOS27_24B_IMAGES)
        return EINVAL;
    pthread_mutex_lock(&r->mutex);
    const ios27_24b_slot *s = &r->slots[id];
    int err = r->gate ? EBUSY : !s->registered ? ENOENT : 0;
    if (!err) *out = (ios27_24b_snapshot){
        s->state, s->refs, s->active, s->completed, s->poisoned,
        s->generation, s->failure};
    pthread_mutex_unlock(&r->mutex);
    return err;
}
int ios27_24b_runtime_destroy(ios27_24b_runtime *r) {
    if (!r || !r->initialized) return EINVAL;
    pthread_mutex_lock(&r->mutex);
    int err = r->gate ? EBUSY : 0;
    for (size_t i = 0; !err && i < r->count; ++i)
        if (r->slots[i].refs || r->slots[i].active ||
            r->slots[i].state != IOS27_24B_NEW) err = EBUSY;
    for (size_t i = 0; !err && i < IOS27_24B_HANDLES; ++i)
        if (r->leases[i].ticket) err = EBUSY;
    if (err) diagnostic(r, 0, IOS27_24B_DIAG_PINNED, err);
    pthread_mutex_unlock(&r->mutex);
    if (err) return err;
    err = pthread_cond_destroy(&r->changed);
    if (err) return err;
    err = pthread_mutex_destroy(&r->mutex);
    if (!err) r->initialized = 0;
    return err;
}
