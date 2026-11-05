#include "ios27/phase23d_bridge.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(ios27_23d_wire) == 160, "phase23d wire layout must be 160 bytes");
#define P IOS27_23D_TASKS
#define R IOS27_23D_PORTS
#define T IOS27_23D_THREADS
#define F IOS27_23D_FDS
#define H 32u

typedef struct { uint8_t live, cancelled; uint32_t generation; uint64_t last_correlation; } thread_state;
typedef struct { uint8_t used, file, mode; size_t offset; } fd_state;
typedef struct {
    uint8_t live, permissions[IOS27_23D_MEMORY / IOS27_23D_PAGE];
    uint8_t memory[IOS27_23D_MEMORY];
    uint8_t input[IOS27_23D_FILE_MAX], output[IOS27_23D_FILE_MAX];
    size_t input_size, output_size;
    uint32_t generation;
    thread_state threads[T];
    fd_state fds[F];
    uint8_t send[R], once[R];
} task_state;
typedef struct { uint8_t live; uint32_t generation, name, kind; uint64_t owner; } port_state;
struct ios27_23d_runtime {
    pthread_mutex_t mu;
    task_state tasks[P];
    port_state ports[R];
    ios27_23d_stats stats;
};
static uint64_t make_id(uint32_t index, uint32_t generation) {
    return ((uint64_t)generation << 32) | (uint64_t)(index + 1u);
}
static int index_of(uint64_t id, unsigned limit) {
    uint32_t low = (uint32_t)id;
    if (low == 0 || low > limit || (id >> 32) == 0) return -1;
    return (int)(low - 1u);
}
static uint32_t next_generation(uint32_t g) { return g == UINT32_MAX ? 1u : g + 1u; }
static task_state *task_get(ios27_23d_runtime *rt, uint64_t id) {
    int i = index_of(id, P);
    if (i < 0) return NULL;
    task_state *t = &rt->tasks[i];
    return t->live && t->generation == (uint32_t)(id >> 32) ? t : NULL;
}
static thread_state *thread_get(task_state *task, uint64_t tid) {
    int i = index_of(tid, T);
    if (i < 0) return NULL;
    thread_state *t = &task->threads[i];
    return t->live && t->generation == (uint32_t)(tid >> 32) ? t : NULL;
}
static port_state *port_get(ios27_23d_runtime *rt, uint64_t handle) {
    int i = index_of(handle, R);
    if (i < 0) return NULL;
    port_state *p = &rt->ports[i];
    return p->live && p->generation == (uint32_t)(handle >> 32) ? p : NULL;
}
static int region(task_state *task, uint64_t off, size_t bytes, uint8_t access) {
    if (off > IOS27_23D_MEMORY || bytes > IOS27_23D_MEMORY - (size_t)off) return 0;
    if (bytes == 0) return 1;
    size_t first = (size_t)off / IOS27_23D_PAGE;
    size_t last = ((size_t)off + bytes - 1u) / IOS27_23D_PAGE;
    for (size_t i = first; i <= last; ++i)
        if ((task->permissions[i] & access) != access) return 0;
    return 1;
}
static int separated(uint64_t a, size_t as, uint64_t b, size_t bs) {
    if (a > UINT64_MAX - as || b > UINT64_MAX - bs) return 0;
    return a + as <= b || b + bs <= a;
}
static void revoke_port(ios27_23d_runtime *rt, unsigned i) {
    port_state *p = &rt->ports[i];
    p->live = 0;
    p->generation = next_generation(p->generation);
    for (unsigned t = 0; t < P; ++t) {
        rt->tasks[t].send[i] = 0;
        rt->tasks[t].once[i] = 0;
    }
    rt->stats.port_deaths++;
}
int ios27_23d_new(ios27_23d_runtime **out) {
    if (!out) return IOS27_23D_INVALID;
    *out = NULL;
    ios27_23d_runtime *rt = calloc(1, sizeof(*rt));
    if (!rt) return IOS27_23D_NOMEM;
    if (pthread_mutex_init(&rt->mu, NULL) != 0) { free(rt); return IOS27_23D_NOMEM; }
    for (unsigned i = 0; i < P; ++i) {
        rt->tasks[i].generation = 1;
        for (unsigned j = 0; j < T; ++j) rt->tasks[i].threads[j].generation = 1;
    }
    for (unsigned i = 0; i < R; ++i) rt->ports[i].generation = 1;
    *out = rt;
    return IOS27_23D_OK;
}
void ios27_23d_free(ios27_23d_runtime *rt) {
    if (!rt) return;
    pthread_mutex_destroy(&rt->mu);
    free(rt);
}
int ios27_23d_process_create(ios27_23d_runtime *rt, uint64_t *pid) {
    if (!rt || !pid) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    int status = IOS27_23D_LIMIT;
    for (unsigned i = 0; i < P; ++i) {
        task_state *t = &rt->tasks[i];
        if (t->live) continue;
        uint32_t gen = t->generation;
        uint32_t thread_gen[T];
        for (unsigned j = 0; j < T; ++j) thread_gen[j] = t->threads[j].generation;
        memset(t, 0, sizeof(*t));
        t->live = 1; t->generation = gen;
        for (unsigned j = 0; j < T; ++j) t->threads[j].generation = thread_gen[j];
        for (unsigned j = 0; j < IOS27_23D_MEMORY / IOS27_23D_PAGE; ++j)
            t->permissions[j] = IOS27_23D_READ | IOS27_23D_WRITE;
        *pid = make_id(i, gen);
        status = IOS27_23D_OK;
        break;
    }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_process_destroy(ios27_23d_runtime *rt, uint64_t pid) {
    if (!rt) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, pid);
    int status = IOS27_23D_DEAD;
    if (t) {
        for (unsigned i = 0; i < R; ++i)
            if (rt->ports[i].live && rt->ports[i].owner == pid) revoke_port(rt, i);
        t->live = 0;
        t->generation = next_generation(t->generation);
        for (unsigned i = 0; i < T; ++i) {
            t->threads[i].live = 0;
            t->threads[i].generation = next_generation(t->threads[i].generation);
        }
        memset(t->memory, 0, sizeof(t->memory));
        memset(t->input, 0, sizeof(t->input));
        memset(t->output, 0, sizeof(t->output));
        status = IOS27_23D_OK;
    }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_thread_create(ios27_23d_runtime *rt, uint64_t pid, uint64_t *tid) {
    if (!rt || !tid) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, pid);
    int status = t ? IOS27_23D_LIMIT : IOS27_23D_DEAD;
    if (t) for (unsigned i = 0; i < T; ++i) if (!t->threads[i].live) {
        thread_state *th = &t->threads[i];
        th->live = 1; th->cancelled = 0; th->last_correlation = 0;
        *tid = make_id(i, th->generation); status = IOS27_23D_OK; break;
    }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_thread_cancel(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid) {
    if (!rt) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, pid);
    thread_state *th = t ? thread_get(t, tid) : NULL;
    int status = th ? IOS27_23D_OK : IOS27_23D_DEAD;
    if (th) { th->cancelled = 1; rt->stats.cancelled++; }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_thread_destroy(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid) {
    if (!rt) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, pid);
    thread_state *th = t ? thread_get(t, tid) : NULL;
    int status = th ? IOS27_23D_OK : IOS27_23D_DEAD;
    if (th) { th->live = 0; th->generation = next_generation(th->generation); }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_memory_protect(ios27_23d_runtime *rt, uint64_t pid, uint32_t page, uint32_t permissions) {
    if (!rt || page >= IOS27_23D_MEMORY / IOS27_23D_PAGE ||
        (permissions & ~(IOS27_23D_READ | IOS27_23D_WRITE)) != 0) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, pid);
    if (t) t->permissions[page] = (uint8_t)permissions;
    pthread_mutex_unlock(&rt->mu);
    return t ? IOS27_23D_OK : IOS27_23D_DEAD;
}
static int memory_copy(ios27_23d_runtime *rt, uint64_t pid, uint64_t off,
                       void *buf, size_t size, int into_guest) {
    if (!rt || (!buf && size)) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, pid);
    int status = !t ? IOS27_23D_DEAD : !region(t, off, size, into_guest ? IOS27_23D_WRITE : IOS27_23D_READ)
                 ? IOS27_23D_FAULT : IOS27_23D_OK;
    if (!status && size) {
        if (into_guest) memcpy(t->memory + (size_t)off, buf, size);
        else memcpy(buf, t->memory + (size_t)off, size);
        rt->stats.copies++;
    }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_memory_read(ios27_23d_runtime *rt, uint64_t pid, uint64_t offset, void *destination, size_t size) {
    return memory_copy(rt, pid, offset, destination, size, 0);
}
int ios27_23d_memory_write(ios27_23d_runtime *rt, uint64_t pid, uint64_t offset, const void *source, size_t size) {
    return memory_copy(rt, pid, offset, (void *)source, size, 1);
}
int ios27_23d_seed(ios27_23d_runtime *rt, uint64_t pid, const uint8_t *data, size_t size) {
    if (!rt || size > IOS27_23D_FILE_MAX || (!data && size)) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, pid);
    if (t) { if (size) memcpy(t->input, data, size); t->input_size = size; }
    pthread_mutex_unlock(&rt->mu);
    return t ? IOS27_23D_OK : IOS27_23D_DEAD;
}
int ios27_23d_service_register(ios27_23d_runtime *rt, uint64_t owner, uint32_t name,
                               uint32_t kind, uint64_t *port) {
    if (!rt || !port || name == 0 || kind < IOS27_23D_SERVICE_ECHO || kind > IOS27_23D_SERVICE_RELAY)
        return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    task_state *t = task_get(rt, owner);
    int status = t ? IOS27_23D_LIMIT : IOS27_23D_DEAD;
    if (t) {
        for (unsigned i = 0; i < R; ++i)
            if (rt->ports[i].live && rt->ports[i].owner == owner && rt->ports[i].name == name)
                { status = IOS27_23D_BUSY; break; }
        if (status == IOS27_23D_LIMIT) for (unsigned i = 0; i < R; ++i) if (!rt->ports[i].live) {
            port_state *p = &rt->ports[i];
            p->live = 1; p->name = name; p->kind = kind; p->owner = owner;
            t->send[i] = 1;
            *port = make_id(i, p->generation); status = IOS27_23D_OK; break;
        }
    }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_service_grant(ios27_23d_runtime *rt, uint64_t owner, uint64_t port,
                            uint64_t recipient, int send_once) {
    if (!rt || (send_once != 0 && send_once != 1)) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    port_state *p = port_get(rt, port);
    task_state *t = task_get(rt, recipient);
    int status = !p || !t ? IOS27_23D_DEAD : p->owner != owner ? IOS27_23D_DENIED : IOS27_23D_OK;
    if (!status) {
        int i = index_of(port, R);
        if (send_once) t->once[i] = 1;
        else t->send[i] = 1;
    }
    pthread_mutex_unlock(&rt->mu);
    return status;
}
int ios27_23d_service_destroy(ios27_23d_runtime *rt, uint64_t owner, uint64_t port) {
    if (!rt) return IOS27_23D_INVALID;
    pthread_mutex_lock(&rt->mu);
    port_state *p = port_get(rt, port);
    int status = !p ? IOS27_23D_DEAD : p->owner != owner ? IOS27_23D_DENIED : IOS27_23D_OK;
    if (!status) revoke_port(rt, (unsigned)index_of(port, R));
    pthread_mutex_unlock(&rt->mu);
    return status;
}
static int invoke(ios27_23d_runtime *rt, task_state *caller, uint64_t port,
                  const ios27_23d_wire *req, ios27_23d_wire *reply,
                  unsigned depth, uint32_t budget) {
    if (budget == 0) return IOS27_23D_TIMEOUT;
    if (depth >= IOS27_23D_MAX_DEPTH) return IOS27_23D_LIMIT;
    port_state *p = port_get(rt, port);
    if (!p) return IOS27_23D_DEAD;
    task_state *owner = task_get(rt, p->owner);
    int index = index_of(port, R);
    if (!owner) return IOS27_23D_DEAD;
    if (!caller->send[index] && !caller->once[index]) return IOS27_23D_DENIED;
    memset(reply, 0, sizeof(*reply));
    reply->opcode = req->opcode;
    reply->correlation = req->correlation;
    if (p->kind == IOS27_23D_SERVICE_ECHO && req->opcode == IOS27_23D_RPC_ECHO) {
        reply->size = req->size;
        memcpy(reply->data, req->data, req->size);
    } else if (p->kind == IOS27_23D_SERVICE_SUM && req->opcode == IOS27_23D_RPC_SUM) {
        if (req->size % 4u || req->size == 0) return IOS27_23D_INVALID;
        uint32_t total = 0;
        for (unsigned i = 0; i < req->size; i += 4) {
            uint32_t part;
            memcpy(&part, req->data + i, sizeof(part));
            total += part;
        }
        memcpy(reply->data, &total, sizeof(total)); reply->size = 4;
    } else if (p->kind == IOS27_23D_SERVICE_RELAY && req->opcode == IOS27_23D_RPC_RELAY) {
        if (req->flags != 0) return IOS27_23D_UNSUPPORTED;
        ios27_23d_wire forwarded = *req;
        forwarded.opcode = IOS27_23D_RPC_ECHO;
        forwarded.transfer = 0;
        rt->stats.nested_calls++;
        return invoke(rt, caller, req->transfer, &forwarded, reply, depth + 1u, budget - 1u);
    } else return IOS27_23D_UNSUPPORTED;
    return IOS27_23D_OK;
}
static int rpc(ios27_23d_runtime *rt, task_state *caller, thread_state *th,
               const uint64_t *x, ios27_23d_result *out) {
    uint64_t port = x[0], reqoff = x[1], reqlen = x[2], replyoff = x[3], capacity = x[4];
    if (reqlen < H || reqlen > sizeof(ios27_23d_wire) || capacity < H ||
        capacity > sizeof(ios27_23d_wire) || !region(caller, reqoff, (size_t)reqlen, IOS27_23D_READ) ||
        !region(caller, replyoff, (size_t)capacity, IOS27_23D_WRITE) ||
        !separated(reqoff, (size_t)reqlen, replyoff, (size_t)capacity)) return IOS27_23D_FAULT;
    ios27_23d_wire req;
    memset(&req, 0, sizeof(req));
    memcpy(&req, caller->memory + (size_t)reqoff, (size_t)reqlen);
    if (req.reserved || (req.flags & ~IOS27_23D_RPC_TRANSFER_SEND) ||
        req.size != reqlen - H || req.correlation == 0 || req.correlation <= th->last_correlation ||
        req.opcode < IOS27_23D_RPC_ECHO || req.opcode > IOS27_23D_RPC_RELAY) return IOS27_23D_INVALID;
    if (req.flags && req.opcode == IOS27_23D_RPC_RELAY) return IOS27_23D_UNSUPPORTED;
    port_state *destination = port_get(rt, port);
    if (!destination) return IOS27_23D_DEAD;
    task_state *recipient = task_get(rt, destination->owner);
    if (!recipient) return IOS27_23D_DEAD;
    int transfer_i = -1;
    if (req.flags & IOS27_23D_RPC_TRANSFER_SEND) {
        port_state *transferred = port_get(rt, req.transfer);
        transfer_i = index_of(req.transfer, R);
        if (!transferred) return IOS27_23D_DEAD;
        if (!caller->send[transfer_i]) return IOS27_23D_DENIED;
    } else if (req.opcode != IOS27_23D_RPC_RELAY && req.transfer != 0) return IOS27_23D_INVALID;
    ios27_23d_wire reply;
    int status = invoke(rt, caller, port, &req, &reply, 0, (uint32_t)(x[5] > UINT32_MAX ? UINT32_MAX : x[5]));
    if (status) return status;
    size_t bytes = H + reply.size;
    if (bytes > capacity) return IOS27_23D_LIMIT;
    /* Commit: all checks are finished before guest bytes, ownership or
     * monotonic correlation are changed. The lock protects this transition. */
    memcpy(caller->memory + (size_t)replyoff, &reply, bytes);
    if (transfer_i >= 0) recipient->send[transfer_i] = 1;
    int port_i = index_of(port, R);
    if (!caller->send[port_i]) caller->once[port_i] = 0;
    th->last_correlation = req.correlation;
    out->correlation = reply.correlation;
    out->value = (int64_t)bytes;
    rt->stats.rpc_calls++;
    rt->stats.copies += 2;
    return IOS27_23D_OK;
}
static int open_virtual(task_state *t, uint64_t path, uint64_t flags, int64_t *out) {
    if (flags > 2 || !region(t, path, 32, IOS27_23D_READ)) return IOS27_23D_FAULT;
    const char *text = (const char *)(t->memory + (size_t)path);
    size_t n = 0;
    while (n < 32 && text[n] != 0) ++n;
    if (n == 32) return IOS27_23D_INVALID;
    int file = strcmp(text, "/fixture/input") == 0 ? 0 :
               strcmp(text, "/fixture/output") == 0 ? 1 : -1;
    if (file < 0 || (file == 0 && flags != 0)) return IOS27_23D_DENIED;
    for (unsigned i = 3; i < F; ++i) if (!t->fds[i].used) {
        t->fds[i] = (fd_state){ 1, (uint8_t)file, (uint8_t)flags, 0 };
        *out = (int64_t)i;
        return IOS27_23D_OK;
    }
    return IOS27_23D_LIMIT;
}
static int fd_operation(task_state *t, uint32_t number, const uint64_t *x, int64_t *out) {
    if (number == IOS27_23D_SYS_OPEN) return open_virtual(t, x[0], x[1], out);
    if (x[0] < 3 || x[0] >= F || !t->fds[x[0]].used) return IOS27_23D_DENIED;
    fd_state *fd = &t->fds[x[0]];
    if (number == IOS27_23D_SYS_CLOSE) { fd->used = 0; *out = 0; return IOS27_23D_OK; }
    uint8_t *data = fd->file == 0 ? t->input : t->output;
    size_t *len = fd->file == 0 ? &t->input_size : &t->output_size;
    if (number == IOS27_23D_SYS_LSEEK) {
        if (x[2] > 2) return IOS27_23D_INVALID;
        size_t base = x[2] == 0 ? 0 : x[2] == 1 ? fd->offset : *len;
        int64_t delta = (int64_t)x[1];
        uint64_t pos;
        if (delta < 0) {
            uint64_t magnitude = (uint64_t)(-(delta + 1)) + 1u;
            if (magnitude > base) return IOS27_23D_INVALID;
            pos = base - magnitude;
        } else {
            if ((uint64_t)delta > IOS27_23D_FILE_MAX - base) return IOS27_23D_LIMIT;
            pos = base + (uint64_t)delta;
        }
        fd->offset = (size_t)pos; *out = (int64_t)pos; return IOS27_23D_OK;
    }
    if (number != IOS27_23D_SYS_READ && number != IOS27_23D_SYS_WRITE) return IOS27_23D_UNSUPPORTED;
    if ((number == IOS27_23D_SYS_READ && fd->mode == 1) ||
        (number == IOS27_23D_SYS_WRITE && fd->mode == 0)) return IOS27_23D_DENIED;
    if (x[2] > IOS27_23D_FILE_MAX) return IOS27_23D_LIMIT;
    size_t count = (size_t)x[2];
    if (!region(t, x[1], count, number == IOS27_23D_SYS_READ ? IOS27_23D_WRITE : IOS27_23D_READ))
        return IOS27_23D_FAULT;
    if (number == IOS27_23D_SYS_READ) {
        if (fd->offset > *len) return IOS27_23D_INVALID;
        if (count > *len - fd->offset) count = *len - fd->offset;
        if (count) memcpy(t->memory + (size_t)x[1], data + fd->offset, count);
    } else {
        if (fd->file == 0) return IOS27_23D_DENIED;
        if (count > IOS27_23D_FILE_MAX - fd->offset) return IOS27_23D_LIMIT;
        if (count) memcpy(data + fd->offset, t->memory + (size_t)x[1], count);
        if (fd->offset + count > *len) *len = fd->offset + count;
    }
    fd->offset += count; *out = (int64_t)count;
    return IOS27_23D_OK;
}
int ios27_23d_trap(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid,
                   const ios27_23d_frame *frame, ios27_23d_result *out) {
    if (!rt || !frame || !out) return IOS27_23D_INVALID;
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&rt->mu);
    rt->stats.calls++;
    task_state *t = task_get(rt, pid);
    thread_state *th = t ? thread_get(t, tid) : NULL;
    int status;
    if (frame->abi != IOS27_23D_ABI) status = IOS27_23D_BAD_ABI;
    else if (!th) status = IOS27_23D_DEAD;
    else if (th->cancelled) status = IOS27_23D_INTERRUPTED;
    else switch (frame->number) {
        case IOS27_23D_SYS_GETPID:
            if (frame->x[0] || frame->x[1] || frame->x[2] || frame->x[3] || frame->x[4] || frame->x[5]) status = IOS27_23D_INVALID;
            else { out->value = (int64_t)pid; status = IOS27_23D_OK; }
            break;
        case IOS27_23D_SYS_OPEN:
        case IOS27_23D_SYS_READ:
        case IOS27_23D_SYS_WRITE:
        case IOS27_23D_SYS_CLOSE:
        case IOS27_23D_SYS_LSEEK:
            status = IOS27_23D_OK;
            for (unsigned i = frame->number == IOS27_23D_SYS_LSEEK ? 3u :
                              frame->number == IOS27_23D_SYS_READ || frame->number == IOS27_23D_SYS_WRITE ? 3u :
                              frame->number == IOS27_23D_SYS_OPEN ? 2u : 1u; i < 6u; ++i)
                if (frame->x[i]) { status = IOS27_23D_INVALID; break; }
            if (!status) {
                status = fd_operation(t, frame->number, frame->x, &out->value);
                if (!status) rt->stats.fd_operations++;
            }
            break;
        case IOS27_23D_SYS_RPC: status = rpc(rt, t, th, frame->x, out); break;
        default: status = IOS27_23D_UNSUPPORTED; break;
    }
    if (status) rt->stats.rejects++;
    out->status = status;
    pthread_mutex_unlock(&rt->mu);
    return status;
}
void ios27_23d_get_stats(ios27_23d_runtime *rt, ios27_23d_stats *out) {
    if (!rt || !out) return;
    pthread_mutex_lock(&rt->mu);
    *out = rt->stats;
    pthread_mutex_unlock(&rt->mu);
}
