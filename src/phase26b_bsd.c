#include "ios27/phase26b_bsd.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NFD 32u
#define NFILE 16u
#define NOPEN 32u
#define FILE_CAP 65536u
#define PATH_CAP 96u
#define EPERM_D 1u
#define ENOENT_D 2u
#define EBADF_D 9u
#define EACCES_D 13u
#define EFAULT_D 14u
#define EINVAL_D 22u
#define EMFILE_D 24u
#define ENOSPC_D 28u
#define ENOTSUP_D 45u
#define ENAMETOOLONG_D 63u
#define ENOSYS_D 78u
#define EOVERFLOW_D 84u
#define ECANCELED_D 89u
#define O_CREAT_D 0x200u
#define O_TRUNC_D 0x400u
#define O_EXCL_D 0x800u
#define MAP_PRIVATE_D 2u
#define MAP_ANON_D 0x1000u

typedef struct {
    char path[PATH_CAP];
    uint8_t data[FILE_CAP];
    size_t length;
    uint8_t present, writable;
} vfile;
typedef struct {
    size_t file_index, offset;
    uint32_t flags;
    unsigned refs;
} open_description;
typedef struct {
    uint32_t generation;
    int description; /* -1 = closed */
    uint8_t retired;
} descriptor;
struct ios27_phase26b_process {
    pthread_mutex_t mu;
    uint32_t pid;
    uint64_t epoch;
    int stopped;
    uint8_t arena[IOS27_26B_PAGE * IOS27_26B_PAGES];
    uint8_t permissions[IOS27_26B_PAGES];
    uint8_t mapped[IOS27_26B_PAGES];
    vfile files[NFILE];
    open_description opens[NOPEN];
    descriptor fds[NFD];
};

static void ret(ios27_phase26b_regs *r, uint64_t value, unsigned error) {
    r->x[0] = value;
    r->carry = error;
}
static void err(ios27_phase26b_regs *r, unsigned code) { ret(r, code, 1u); }
static int address_range(ios27_phase26b_process *p, uint64_t address,
                         size_t length, unsigned rights, size_t *offset) {
    uint64_t offset64;
    size_t first, last;
    if (!p || address < IOS27_26B_BASE) return -1;
    offset64 = address - IOS27_26B_BASE;
    if (offset64 > sizeof(p->arena) ||
        length > sizeof(p->arena) - (size_t)offset64) return -1;
    *offset = (size_t)offset64;
    if (!length) return 0;
    first = *offset / IOS27_26B_PAGE;
    last = (*offset + length - 1u) / IOS27_26B_PAGE;
    for (size_t i = first; i <= last; ++i)
        if ((p->permissions[i] & rights) != rights) return -1;
    return 0;
}
static int copy_mem(ios27_phase26b_process *p, uint64_t address, void *buffer,
                    size_t length, int to_guest) {
    size_t offset;
    if ((!buffer && length) ||
        address_range(p, address, length,
                      to_guest ? IOS27_26B_PROT_WRITE : IOS27_26B_PROT_READ,
                      &offset)) return -1;
    if (length) {
        if (to_guest) memcpy(p->arena + offset, buffer, length);
        else memcpy(buffer, p->arena + offset, length);
    }
    return 0;
}
int ios27_phase26b_copyin(ios27_phase26b_process *p, uint64_t a,
                          void *dst, size_t n) {
    int rc;
    if (!p) return -1;
    pthread_mutex_lock(&p->mu);
    rc = p->stopped ? -1 : copy_mem(p, a, dst, n, 0);
    pthread_mutex_unlock(&p->mu);
    return rc;
}
int ios27_phase26b_copyout(ios27_phase26b_process *p, uint64_t a,
                           const void *src, size_t n) {
    int rc;
    if (!p) return -1;
    pthread_mutex_lock(&p->mu);
    rc = p->stopped ? -1 : copy_mem(p, a, (void *)src, n, 1);
    pthread_mutex_unlock(&p->mu);
    return rc;
}
ios27_phase26b_process *ios27_phase26b_create(uint32_t pid, uint64_t epoch) {
    ios27_phase26b_process *p;
    static const char msg[] = "ios27 isolated guest\n";
    if (!pid) return NULL;
    p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    if (pthread_mutex_init(&p->mu, NULL)) { free(p); return NULL; }
    p->pid = pid;
    p->epoch = epoch;
    for (size_t i = 0; i < 16u; ++i)
        { p->permissions[i] = IOS27_26B_PROT_READ | IOS27_26B_PROT_WRITE;
          p->mapped[i] = 1u; }
    for (size_t i = 0; i < NFD; ++i) p->fds[i].description = -1;
    strcpy(p->files[0].path, "/app/readme.txt");
    p->files[0].present = 1u;
    memcpy(p->files[0].data, msg, sizeof(msg) - 1u);
    p->files[0].length = sizeof(msg) - 1u;
    strcpy(p->files[1].path, "/tmp/data");
    p->files[1].present = p->files[1].writable = 1u;
    return p;
}
void ios27_phase26b_stop(ios27_phase26b_process *p) {
    if (!p) return;
    pthread_mutex_lock(&p->mu);
    p->stopped = 1;
    pthread_mutex_unlock(&p->mu);
}
void ios27_phase26b_destroy(ios27_phase26b_process *p) {
    if (!p) return;
    ios27_phase26b_stop(p);
    pthread_mutex_destroy(&p->mu);
    free(p);
}
static int path_from_guest(ios27_phase26b_process *p, uint64_t a,
                           char path[PATH_CAP]) {
    for (size_t i = 0; i < PATH_CAP; ++i) {
        if (copy_mem(p, a + i, path + i, 1u, 0)) return EFAULT_D;
        if (!path[i]) {
            /* Deliberately reject all relative, duplicate-slash, dot,
             * backslash and traversal components, even in /tmp. */
            if (i < 2u || path[0] != '/' || strstr(path, "//") ||
                strstr(path, "/.") || strchr(path, '\\')) return EACCES_D;
            return 0;
        }
    }
    return ENAMETOOLONG_D;
}
static int lookup_fd(ios27_phase26b_process *p, uint64_t handle) {
    uint64_t raw, slot, generation;
    if (handle < 3u || handle > INT_MAX) return -1;
    raw = handle - 3u;
    slot = raw % NFD;
    generation = raw / NFD;
    if (generation != p->fds[slot].generation ||
        p->fds[slot].description < 0) return -1;
    return (int)slot;
}
static int alloc_fd(ios27_phase26b_process *p, size_t minimum, int description) {
    for (size_t i = 0; i < NFD; ++i) {
        uint64_t handle = 3u + i + (uint64_t)p->fds[i].generation * NFD;
        if (handle >= minimum && !p->fds[i].retired &&
            p->fds[i].description < 0 && handle <= INT_MAX) {
            p->fds[i].description = description;
            ++p->opens[description].refs;
            return (int)handle;
        }
    }
    return -1;
}
static void close_slot(ios27_phase26b_process *p, size_t slot) {
    descriptor *fd = &p->fds[slot];
    open_description *d = &p->opens[fd->description];
    if (--d->refs == 0u) memset(d, 0, sizeof(*d));
    fd->description = -1;
    if (fd->generation >= (UINT32_MAX / NFD) - 1u)
        fd->retired = 1u;
    else ++fd->generation;
}
static void open_file(ios27_phase26b_process *p, ios27_phase26b_regs *r,
                      uint64_t pathname, uint64_t raw_flags) {
    char path[PATH_CAP];
    unsigned flags;
    int index = -1, desc = -1, handle;
    int e;
    if (raw_flags > UINT32_MAX) { err(r, EINVAL_D); return; }
    flags = (unsigned)raw_flags;
    if (flags & ~(3u | O_CREAT_D | O_TRUNC_D | O_EXCL_D) ||
        (flags & 3u) == 3u) { err(r, EINVAL_D); return; }
    e = path_from_guest(p, pathname, path);
    if (e) { err(r, (unsigned)e); return; }
    for (size_t i = 0; i < NFILE; ++i)
        if (p->files[i].present && strcmp(path, p->files[i].path) == 0)
            { index = (int)i; break; }
    if (index >= 0 && (flags & O_CREAT_D) && (flags & O_EXCL_D))
        { err(r, 17u); return; }
    if (index < 0) {
        if (!(flags & O_CREAT_D)) { err(r, ENOENT_D); return; }
        if (strncmp(path, "/tmp/", 5u) != 0 ||
            !path[5] || strchr(path + 5, '/')) { err(r, EACCES_D); return; }
        for (size_t i = 2; i < NFILE; ++i)
            if (!p->files[i].present) { index = (int)i; break; }
        if (index < 0) { err(r, ENOSPC_D); return; }
    } else if (!p->files[index].writable &&
               ((flags & 3u) || (flags & O_TRUNC_D))) {
        err(r, EACCES_D); return;
    }
    if ((flags & O_TRUNC_D) && !(flags & 3u)) { err(r, EINVAL_D); return; }
    for (size_t i = 0; i < NOPEN; ++i)
        if (!p->opens[i].refs) { desc = (int)i; break; }
    if (desc < 0) { err(r, EMFILE_D); return; }
    handle = alloc_fd(p, 3u, desc);
    if (handle < 0) { err(r, EMFILE_D); return; }
    if (!p->files[index].present) {
        strcpy(p->files[index].path, path);
        p->files[index].present = p->files[index].writable = 1u;
    }
    if (flags & O_TRUNC_D) p->files[index].length = 0u;
    p->opens[desc].file_index = (size_t)index;
    p->opens[desc].flags = flags & 3u;
    ret(r, (uint64_t)handle, 0u);
}
static void transfer(ios27_phase26b_process *p, ios27_phase26b_regs *r,
                     int write, int positioned) {
    int slot = lookup_fd(p, r->x[0]);
    open_description *d;
    vfile *f;
    size_t offset, n;
    uint64_t address = r->x[1], count = r->x[2];
    if (slot < 0) { err(r, EBADF_D); return; }
    d = &p->opens[p->fds[slot].description];
    if ((write && !(d->flags & 3u)) ||
        (!write && (d->flags & 3u) == 1u)) { err(r, EBADF_D); return; }
    if (count > IOS27_26B_MAX_IO) { err(r, EINVAL_D); return; }
    if (positioned && r->x[3] > INT64_MAX) { err(r, EINVAL_D); return; }
    f = &p->files[d->file_index];
    offset = positioned ? (size_t)r->x[3] : d->offset;
    if (offset > FILE_CAP) { err(r, EINVAL_D); return; }
    n = (size_t)count;
    if (!write && n > f->length - (offset < f->length ? offset : f->length))
        n = offset < f->length ? f->length - offset : 0u;
    if (write && n > FILE_CAP - offset) n = FILE_CAP - offset;
    /* Validate the entire requested writable buffer before any IO state
     * changes; short IO is permitted only for EOF and bounded file size. */
    if (n) {
        size_t guest_offset;
        if (address_range(p, address, n,
                          write ? IOS27_26B_PROT_READ : IOS27_26B_PROT_WRITE,
                          &guest_offset)) { err(r, EFAULT_D); return; }
        if (write) {
            if (offset > f->length)
                memset(f->data + f->length, 0, offset - f->length);
            memcpy(f->data + offset, p->arena + guest_offset, n);
            if (f->length < offset + n) f->length = offset + n;
        } else memcpy(p->arena + guest_offset, f->data + offset, n);
    }
    if (!positioned) d->offset = offset + n;
    ret(r, n, 0u);
}
static void stat_fd(ios27_phase26b_process *p, ios27_phase26b_regs *r) {
    /* Darwin LP64 inode64 stat: 144 bytes, zero-initialized, with
     * st_mode at 4, st_nlink at 6, st_ino at 8, st_uid at 16,
     * st_gid at 20, st_size at 96, st_blocks at 104, st_blksize at 112.
     * Explicit byte writes avoid host struct layout dependencies. */
    uint8_t b[144] = {0};
    int slot = lookup_fd(p, r->x[0]);
    size_t idx, len;
    if (slot < 0) { err(r, EBADF_D); return; }
    idx = p->opens[p->fds[slot].description].file_index;
    len = p->files[idx].length;
    b[4] = 0xa4u; b[5] = 0x81u; /* S_IFREG | 0644 */
    b[6] = 1u;
    b[8] = (uint8_t)(idx + 1u);
    b[16] = b[20] = 0xf5u; b[17] = b[21] = 1u; /* uid/gid 501 */
    for (size_t i = 0; i < 8u; ++i) b[96u + i] = (uint8_t)((uint64_t)len >> (8u*i));
    b[104] = (uint8_t)((len + 511u) / 512u);
    b[112] = 0x00u; b[113] = 0x10u; /* 4096 */
    if (copy_mem(p, r->x[1], b, sizeof(b), 1)) { err(r, EFAULT_D); return; }
    ret(r, 0u, 0u);
}
static void vm(ios27_phase26b_process *p, ios27_phase26b_regs *r,
               unsigned op) {
    uint64_t a = r->x[0], length = r->x[1];
    size_t first, pages;
    unsigned prot = (unsigned)r->x[2];
    if (!length || length > sizeof(p->arena) ||
        length > UINT64_MAX - (IOS27_26B_PAGE - 1u)) {
        err(r, EINVAL_D); return;
    }
    pages = (size_t)((length + IOS27_26B_PAGE - 1u) / IOS27_26B_PAGE);
    if (op == 197u) {
        if (a || r->x[2] > 3u || r->x[3] != (MAP_PRIVATE_D | MAP_ANON_D) ||
            r->x[4] != UINT64_MAX || r->x[5]) { err(r, ENOTSUP_D); return; }
        for (first = 16u; first + pages <= IOS27_26B_PAGES; ++first) {
            size_t k;
            for (k = 0; k < pages; ++k)
                if (p->mapped[first + k]) break;
            if (k == pages) {
                memset(p->arena + first * IOS27_26B_PAGE, 0,
                       pages * IOS27_26B_PAGE);
                for (k = 0; k < pages; ++k)
                    { p->permissions[first + k] = (uint8_t)prot;
                      p->mapped[first + k] = 1u; }
                ret(r, IOS27_26B_BASE + first * IOS27_26B_PAGE, 0u);
                return;
            }
        }
        err(r, ENOSPC_D); return;
    }
    if (a < IOS27_26B_BASE || (a - IOS27_26B_BASE) % IOS27_26B_PAGE ||
        a - IOS27_26B_BASE > sizeof(p->arena)) { err(r, EINVAL_D); return; }
    first = (size_t)((a - IOS27_26B_BASE) / IOS27_26B_PAGE);
    if (first < 16u || pages > IOS27_26B_PAGES - first) {
        err(r, EINVAL_D); return;
    }
    if (op == 74u && r->x[2] > 3u) { err(r, ENOTSUP_D); return; }
    for (size_t i = first; i < first + pages; ++i)
        if (!p->mapped[i]) { err(r, EINVAL_D); return; }
    for (size_t i = first; i < first + pages; ++i) {
        p->permissions[i] = op == 73u ? 0u : (uint8_t)prot;
        if (op == 73u) { p->mapped[i] = 0u;
            memset(p->arena + i * IOS27_26B_PAGE, 0, IOS27_26B_PAGE); }
    }
    ret(r, 0u, 0u);
}
static void dispatch(ios27_phase26b_process *p, ios27_phase26b_regs *r,
                     unsigned number) {
    int slot;
    switch (number) {
    case 5u: open_file(p, r, r->x[0], r->x[1]); break;
    case 463u:
        if (r->x[0] != UINT64_MAX - 1u) { err(r, ENOTSUP_D); break; } /* AT_FDCWD = -2 */
        open_file(p, r, r->x[1], r->x[2]); break;
    case 6u:
        slot = lookup_fd(p, r->x[0]);
        if (slot < 0) err(r, EBADF_D);
        else { close_slot(p, (size_t)slot); ret(r, 0u, 0u); }
        break;
    case 3u: transfer(p, r, 0, 0); break;
    case 4u: transfer(p, r, 1, 0); break;
    case 153u: transfer(p, r, 0, 1); break;
    case 154u: transfer(p, r, 1, 1); break;
    case 199u: {
        int64_t off = (int64_t)r->x[1];
        int64_t base = 0;
        open_description *d;
        slot = lookup_fd(p, r->x[0]);
        if (slot < 0) { err(r, EBADF_D); break; }
        d = &p->opens[p->fds[slot].description];
        if (r->x[2] == 1u) base = (int64_t)d->offset;
        else if (r->x[2] == 2u)
            base = (int64_t)p->files[d->file_index].length;
        else if (r->x[2]) { err(r, EINVAL_D); break; }
        if (off < -base || off > FILE_CAP - base) { err(r, EINVAL_D); break; }
        d->offset = (size_t)(base + off);
        ret(r, d->offset, 0u); break;
    }
    case 92u: {
        int desc, handle;
        slot = lookup_fd(p, r->x[0]);
        if (slot < 0) { err(r, EBADF_D); break; }
        desc = p->fds[slot].description;
        if (r->x[1] == 3u) ret(r, p->opens[desc].flags, 0u); /* F_GETFL */
        else if (r->x[1] == 0u) { /* F_DUPFD */
            if (r->x[2] > INT_MAX) { err(r, EINVAL_D); break; }
            handle = alloc_fd(p, (size_t)r->x[2], desc);
            if (handle < 0) err(r, EMFILE_D);
            else ret(r, (uint64_t)handle, 0u);
        } else err(r, ENOTSUP_D);
        break;
    }
    case 189u: stat_fd(p, r); break;
    case 197u: case 73u: case 74u: vm(p, r, number); break;
    case 116u: {
        uint8_t tv[16] = {0};
        if (r->x[1]) { err(r, ENOTSUP_D); break; }
        for (size_t i = 0; i < 8u; ++i)
            tv[i] = (uint8_t)(p->epoch >> (i * 8u));
        if (copy_mem(p, r->x[0], tv, sizeof(tv), 1))
            err(r, EFAULT_D);
        else ret(r, 0u, 0u);
        break;
    }
    case 20u: ret(r, p->pid, 0u); break;
    case 39u: ret(r, 1u, 0u); break; /* virtual supervisor */
    case 24u: case 25u: case 43u: case 47u: ret(r, 501u, 0u); break;
    default: err(r, ENOSYS_D); break;
    }
}
void ios27_phase26b_trap(ios27_phase26b_process *p, ios27_phase26b_regs *r) {
    uint64_t sel;
    if (!r) return;
    if (!p || r->instruction != IOS27_26B_SVC) { err(r, EINVAL_D); return; }
    pthread_mutex_lock(&p->mu);
    if (p->stopped) { err(r, ECANCELED_D); goto out; }
    sel = r->x16;
    if (sel > UINT32_MAX ||
        ((sel & 0xff000000u) != 0u &&
         (sel & 0xff000000u) != IOS27_26B_BSD_CLASS) ||
        (sel & 0x00ff0000u)) err(r, ENOSYS_D);
    else dispatch(p, r, (unsigned)(sel & 0xffffu));
out:
    pthread_mutex_unlock(&p->mu);
}
