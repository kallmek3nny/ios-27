#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_lockorder.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct ios27_21j_lockorder {
    pthread_mutex_t mutex;
    size_t nlocks, nactors;
    uint64_t edges[IOS27_21J_GRAPH_MAX_LOCKS];
    uint64_t held[IOS27_21J_GRAPH_MAX_ACTORS];
    uint32_t owners[IOS27_21J_GRAPH_MAX_LOCKS];
    uint32_t waiting_on[IOS27_21J_GRAPH_MAX_ACTORS];
    ios27_21j_lockorder_stats statistics;
};
static int reaches(const ios27_21j_lockorder *g, uint32_t from, uint32_t to) {
    uint64_t frontier = UINT64_C(1) << from, seen = 0;
    while (frontier) {
        for (uint32_t node = 0; node < g->nlocks; ++node) {
            uint64_t bit = UINT64_C(1) << node;
            if (!(frontier & bit)) continue;
            frontier &= ~bit;
            if (node == to) return 1;
            if (seen & bit) break;
            seen |= bit;
            frontier |= g->edges[node] & ~seen;
            break;
        }
    }
    return 0;
}
int ios27_21j_lockorder_create(ios27_21j_lockorder **out,
                               size_t nlocks, size_t nactors) {
    ios27_21j_lockorder *g;
    int error;
    if (!out || !nlocks || nlocks > IOS27_21J_GRAPH_MAX_LOCKS ||
        !nactors || nactors > IOS27_21J_GRAPH_MAX_ACTORS) return EINVAL;
    *out = NULL;
    g = calloc(1, sizeof(*g));
    if (!g) return ENOMEM;
    error = pthread_mutex_init(&g->mutex, NULL);
    if (error) { free(g); return error; }
    g->nlocks = nlocks;
    g->nactors = nactors;
    g->statistics.locks = nlocks;
    g->statistics.actors = nactors;
    *out = g;
    return 0;
}
int ios27_21j_lockorder_destroy(ios27_21j_lockorder *g) {
    if (!g) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    if (g->statistics.active || g->statistics.waiting) {
        (void)pthread_mutex_unlock(&g->mutex);
        return EBUSY;
    }
    (void)pthread_mutex_unlock(&g->mutex);
    int error = pthread_mutex_destroy(&g->mutex);
    if (error) return error;
    free(g);
    return 0;
}
int ios27_21j_lockorder_acquire(ios27_21j_lockorder *g,
                                uint32_t actor, uint32_t lock_id) {
    uint64_t bit, held;
    int rc = 0;
    if (!g || actor >= g->nactors || lock_id >= g->nlocks) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    bit = UINT64_C(1) << lock_id;
    held = g->held[actor];
    if (g->waiting_on[actor]) {
        rc = EBUSY;
    } else if (held & bit) {
        ++g->statistics.cycles_rejected;
        rc = EDEADLK;
    } else if (g->owners[lock_id]) {
        ++g->statistics.contention;
        rc = EBUSY;
    } else {
        for (uint32_t id = 0; id < g->nlocks; ++id) {
            if (!(held & (UINT64_C(1) << id))) continue;
            if (reaches(g, lock_id, id)) {
                ++g->statistics.cycles_rejected;
                rc = EDEADLK;
                break;
            }
        }
    }
    if (!rc) {
        for (uint32_t id = 0; id < g->nlocks; ++id) {
            uint64_t new_edge = UINT64_C(1) << lock_id;
            if (held & (UINT64_C(1) << id) && !(g->edges[id] & new_edge)) {
                g->edges[id] |= new_edge;
                ++g->statistics.edges;
            }
        }
        g->owners[lock_id] = actor + 1u;
        g->held[actor] |= bit;
        ++g->statistics.acquisitions;
        ++g->statistics.active;
        if (g->statistics.active > g->statistics.peak)
            g->statistics.peak = g->statistics.active;
    }
    (void)pthread_mutex_unlock(&g->mutex);
    return rc;
}
int ios27_21j_lockorder_release(ios27_21j_lockorder *g,
                                uint32_t actor, uint32_t lock_id) {
    int rc = 0;
    if (!g || actor >= g->nactors || lock_id >= g->nlocks) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    if (g->owners[lock_id] != actor + 1u) rc = EPERM;
    else {
        g->owners[lock_id] = 0;
        g->held[actor] &= ~(UINT64_C(1) << lock_id);
        ++g->statistics.releases;
        --g->statistics.active;
    }
    (void)pthread_mutex_unlock(&g->mutex);
    return rc;
}
/* Wait-for edges follow the actual logical lock owner, not historical
   acquisition order. The owner and wait tables are protected by one guard. */
int ios27_21j_lockorder_wait(ios27_21j_lockorder *g,
                             uint32_t actor, uint32_t lock_id) {
    uint32_t owner;
    int cycle = 0, rc = 0;
    if (!g || actor >= g->nactors || lock_id >= g->nlocks) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    if (g->waiting_on[actor]) {
        rc = EALREADY;
    } else {
        owner = g->owners[lock_id];
        if (!owner) rc = EAGAIN;
        else {
            uint32_t current = owner - 1u;
            for (size_t hops = 0; hops < g->nactors; ++hops) {
                if (current == actor) { cycle = 1; break; }
                uint32_t awaited = g->waiting_on[current];
                if (!awaited) break;
                owner = g->owners[awaited - 1u];
                if (!owner) break;
                current = owner - 1u;
            }
            if (cycle) {
                ++g->statistics.cycles_rejected;
                ++g->statistics.wait_cycles_rejected;
                rc = EDEADLK;
            } else {
                g->waiting_on[actor] = lock_id + 1u;
                ++g->statistics.waiting;
            }
        }
    }
    (void)pthread_mutex_unlock(&g->mutex);
    return rc;
}
int ios27_21j_lockorder_cancel_wait(ios27_21j_lockorder *g,
                                    uint32_t actor, uint32_t lock_id) {
    int rc = 0;
    if (!g || actor >= g->nactors || lock_id >= g->nlocks) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    if (g->waiting_on[actor] != lock_id + 1u) rc = EINVAL;
    else {
        g->waiting_on[actor] = 0;
        --g->statistics.waiting;
    }
    (void)pthread_mutex_unlock(&g->mutex);
    return rc;
}
int ios27_21j_lockorder_reset(ios27_21j_lockorder *g) {
    if (!g) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    if (g->statistics.active || g->statistics.waiting) {
        (void)pthread_mutex_unlock(&g->mutex);
        return EBUSY;
    }
    memset(g->edges, 0, sizeof(g->edges));
    g->statistics.edges = 0;
    ++g->statistics.resets;
    (void)pthread_mutex_unlock(&g->mutex);
    return 0;
}
int ios27_21j_lockorder_snapshot(ios27_21j_lockorder *g,
                                 ios27_21j_lockorder_stats *out) {
    if (!g || !out) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    *out = g->statistics;
    (void)pthread_mutex_unlock(&g->mutex);
    return 0;
}
int ios27_21j_lockorder_edge(ios27_21j_lockorder *g,
                             uint32_t from, uint32_t to, int *out) {
    if (!g || !out || from >= g->nlocks || to >= g->nlocks) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    *out = (g->edges[from] & (UINT64_C(1) << to)) != 0;
    (void)pthread_mutex_unlock(&g->mutex);
    return 0;
}

int ios27_21j_lockorder_audit(ios27_21j_lockorder *g,
                              ios27_21j_graph_audit *out) {
    ios27_21j_graph_audit result = {0};
    uint32_t indegree[IOS27_21J_GRAPH_MAX_LOCKS] = {0};
    unsigned char removed[IOS27_21J_GRAPH_MAX_LOCKS] = {0};
    if (!g || !out) return EINVAL;
    (void)pthread_mutex_lock(&g->mutex);
    result.examined_actors = g->nactors;
    for (uint32_t actor = 0; actor < g->nactors; ++actor) {
        uint32_t waiting = g->waiting_on[actor];
        if (waiting) {
            ++result.waiting;
            if (waiting > g->nlocks)
                result.violations |= IOS27_21J_GRAPH_AUDIT_WAITS;
        }
        for (uint32_t lock = 0; lock < g->nlocks; ++lock) {
            uint64_t bit = UINT64_C(1) << lock;
            int held = (g->held[actor] & bit) != 0;
            int owner = g->owners[lock] == actor + 1u;
            if (held != owner)
                result.violations |= IOS27_21J_GRAPH_AUDIT_OWNERS;
        }
    }
    for (uint32_t lock = 0; lock < g->nlocks; ++lock) {
        if (g->owners[lock]) {
            ++result.owned;
            if (g->owners[lock] > g->nactors)
                result.violations |= IOS27_21J_GRAPH_AUDIT_OWNERS;
        }
        uint64_t neighbors = g->edges[lock];
        if (g->nlocks < 64u && (neighbors >> g->nlocks))
            result.violations |= IOS27_21J_GRAPH_AUDIT_EDGES;
        for (uint32_t to = 0; to < g->nlocks; ++to) {
            if (neighbors & (UINT64_C(1) << to)) {
                ++result.edges;
                ++indegree[to];
            }
        }
    }
    if (result.owned != g->statistics.active)
        result.violations |= IOS27_21J_GRAPH_AUDIT_OWNERS;
    if (result.waiting != g->statistics.waiting)
        result.violations |= IOS27_21J_GRAPH_AUDIT_WAITS;
    if (result.edges != g->statistics.edges)
        result.violations |= IOS27_21J_GRAPH_AUDIT_EDGES;
    for (uint32_t actor = 0; actor < g->nactors; ++actor) {
        uint32_t current = actor;
        for (size_t hops = 0; hops < g->nactors; ++hops) {
            uint32_t target = g->waiting_on[current];
            if (!target || target > g->nlocks) break;
            uint32_t owner = g->owners[target - 1u];
            if (!owner || owner > g->nactors) break;
            current = owner - 1u;
            if (current == actor) {
                result.violations |= IOS27_21J_GRAPH_AUDIT_WAITS;
                break;
            }
        }
    }
    /* Bounded Kahn topological sort independently checks that all
       accepted historical lock order edges still form a DAG. */
    for (uint32_t removed_count = 0; removed_count < g->nlocks;
         ++removed_count) {
        uint32_t node = g->nlocks;
        for (uint32_t i = 0; i < g->nlocks; ++i) {
            if (!removed[i] && indegree[i] == 0) {
                node = i;
                break;
            }
        }
        if (node == g->nlocks) {
            result.violations |= IOS27_21J_GRAPH_AUDIT_EDGES;
            break;
        }
        removed[node] = 1;
        for (uint32_t to = 0; to < g->nlocks; ++to)
            if (g->edges[node] & (UINT64_C(1) << to))
                --indegree[to];
    }
    *out = result;
    (void)pthread_mutex_unlock(&g->mutex);
    return result.violations ? EPROTO : 0;
}
