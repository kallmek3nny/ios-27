#include "ios27/phase27a_startup.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    char argv_data[IOS27_PHASE27A_MAX_ARGV][IOS27_PHASE27A_MAX_STRING];
    char env_data[IOS27_PHASE27A_MAX_ENV][IOS27_PHASE27A_MAX_STRING];
    char apple_data[IOS27_PHASE27A_MAX_APPLE][IOS27_PHASE27A_MAX_STRING];
    char *argv[IOS27_PHASE27A_MAX_ARGV + 1u];
    char *env[IOS27_PHASE27A_MAX_ENV + 1u];
    char *apple[IOS27_PHASE27A_MAX_APPLE + 1u];
} startup_strings;

typedef struct {
    const ios27_phase27a_request *req;
    unsigned char color[IOS27_PHASE27A_MAX_IMAGES];
    size_t ordered[IOS27_PHASE27A_MAX_IMAGES];
    size_t used;
} graph;

static int bounded_copy(const char *src, char *dest, size_t *remaining) {
    if (!src || !dest || !remaining) return -1;
    size_t n = 0;
    while (n < IOS27_PHASE27A_MAX_STRING && src[n]) ++n;
    if (!n || n == IOS27_PHASE27A_MAX_STRING || n + 1u > *remaining)
        return -1;
    memcpy(dest, src, n + 1u);
    *remaining -= n + 1u;
    return 0;
}

static int copy_inputs(const ios27_phase27a_inputs *in, startup_strings *s) {
    if (!in || !s || in->argc < 1 ||
        in->argc > (int)IOS27_PHASE27A_MAX_ARGV ||
        in->envc > IOS27_PHASE27A_MAX_ENV ||
        in->applec > IOS27_PHASE27A_MAX_APPLE ||
        !in->argv || (in->envc && !in->env) ||
        (in->applec && !in->apple)) return -1;
    size_t budget = IOS27_PHASE27A_MAX_INPUT_BYTES;
    for (int i = 0; i < in->argc; ++i) {
        if (bounded_copy(in->argv[i], s->argv_data[i], &budget)) return -1;
        s->argv[i] = s->argv_data[i];
    }
    for (size_t i = 0; i < in->envc; ++i) {
        if (bounded_copy(in->env[i], s->env_data[i], &budget)) return -1;
        char *equal = strchr(s->env_data[i], '=');
        if (!equal || equal == s->env_data[i]) return -1;
        /* Two definitions of the same environment key are ambiguous. */
        for (size_t j = 0; j < i; ++j) {
            size_t old_len = (size_t)(strchr(s->env_data[j], '=') - s->env_data[j]);
            if (old_len == (size_t)(equal - s->env_data[i]) &&
                !memcmp(s->env_data[j], s->env_data[i], old_len)) return -1;
        }
        s->env[i] = s->env_data[i];
    }
    for (size_t i = 0; i < in->applec; ++i) {
        if (bounded_copy(in->apple[i], s->apple_data[i], &budget)) return -1;
        s->apple[i] = s->apple_data[i];
    }
    return 0;
}

static int bounded_name(const char *text) {
    if (!text) return 0;
    for (size_t i = 0; i < IOS27_PHASE27A_MAX_STRING; ++i) {
        if (!text[i]) return i != 0u;
    }
    return 0;
}

static int find_image(const graph *g, uint32_t id) {
    for (size_t i = 0; i < g->req->image_count; ++i)
        if (g->req->images[i].id == id) return (int)i;
    return -1;
}

static ios27_phase27a_status visit(graph *g, size_t i) {
    if (g->color[i] == 1u) return IOS27_PHASE27A_CYCLE;
    if (g->color[i] == 2u) return IOS27_PHASE27A_OK;
    g->color[i] = 1u;
    const ios27_phase27a_image *im = &g->req->images[i];
    for (size_t j = 0; j < im->dependency_count; ++j) {
        int parent = find_image(g, im->dependencies[j]);
        if (parent < 0) return IOS27_PHASE27A_INVALID;
        ios27_phase27a_status rc = visit(g, (size_t)parent);
        if (rc != IOS27_PHASE27A_OK) return rc;
    }
    g->color[i] = 2u;
    g->ordered[g->used++] = i;
    return IOS27_PHASE27A_OK;
}

static ios27_phase27a_status preflight(graph *g, size_t *main_index) {
    const ios27_phase27a_request *r = g->req;
    if (!r || !r->images || !r->image_count ||
        r->image_count > IOS27_PHASE27A_MAX_IMAGES ||
        !r->hooks.activate || !r->hooks.rollback || !r->hooks.enter_main)
        return IOS27_PHASE27A_INVALID;
    size_t mains = 0;
    for (size_t i = 0; i < r->image_count; ++i) {
        const ios27_phase27a_image *im = &r->images[i];
        if (!im->id || !bounded_name(im->name) ||
            im->dependency_count > IOS27_PHASE27A_MAX_DEPS ||
            (im->dependency_count && !im->dependencies) ||
            !im->text_executable || im->text_writable ||
            im->text_filesize == 0 ||
            UINT64_MAX - im->text_fileoff < im->text_filesize)
            return IOS27_PHASE27A_UNSUPPORTED;
        for (size_t j = 0; j < i; ++j)
            if (r->images[j].id == im->id ||
                !strcmp(r->images[j].name, im->name))
                return IOS27_PHASE27A_INVALID;
        for (size_t j = 0; j < im->dependency_count; ++j)
            for (size_t k = 0; k < j; ++k)
                if (im->dependencies[j] == im->dependencies[k])
                    return IOS27_PHASE27A_INVALID;
        if (im->has_main) {
            ++mains;
            *main_index = i;
            if (im->entry_fileoff < im->text_fileoff ||
                im->entry_fileoff >= im->text_fileoff + im->text_filesize ||
                (im->entry_fileoff & 3u))
                return IOS27_PHASE27A_UNSUPPORTED;
        } else if (im->entry_fileoff != 0)
            return IOS27_PHASE27A_UNSUPPORTED;
    }
    if (mains != 1) return IOS27_PHASE27A_INVALID;
    for (size_t i = 0; i < r->image_count; ++i) {
        ios27_phase27a_status rc = visit(g, i);
        if (rc != IOS27_PHASE27A_OK) return rc;
    }
    return IOS27_PHASE27A_OK;
}

static ios27_phase27a_status rollback(const ios27_phase27a_request *r,
                                      ios27_phase27a_result *out,
                                      const size_t *ordered, size_t n,
                                      ios27_phase27a_status cause) {
    int rollback_failed = 0;
    for (size_t i = n; i > 0; --i) {
        if (r->hooks.rollback(r->context, &r->images[ordered[i - 1u]]))
            rollback_failed = 1;
        ++out->rolled_back;
    }
    out->cause = cause;
    out->status = rollback_failed ? IOS27_PHASE27A_ROLLBACK_FAILED : cause;
    return out->status;
}

ios27_phase27a_status ios27_phase27a_launch(const ios27_phase27a_request *r,
                                            ios27_phase27a_result *out) {
    if (!out) return IOS27_PHASE27A_INVALID;
    memset(out, 0, sizeof(*out));
    if (!r) return (out->status = IOS27_PHASE27A_INVALID);
    graph g = {0};
    g.req = r;
    size_t main_index = 0;
    ios27_phase27a_status rc = preflight(&g, &main_index);
    if (rc != IOS27_PHASE27A_OK) return (out->status = rc);
    startup_strings s = {0};
    if (copy_inputs(&r->inputs, &s))
        return (out->status = IOS27_PHASE27A_INVALID);
    for (size_t i = 0; i < g.used; ++i) {
        const ios27_phase27a_image *im = &r->images[g.ordered[i]];
        /* Even a failing activate() must be rolled back: it can partially
         * allocate resources before reporting failure. */
        ++out->activated;
        if (r->hooks.activate(r->context, im)) {
            out->failed_image = im->id;
            return rollback(r, out, g.ordered, out->activated,
                            IOS27_PHASE27A_ACTIVATION_FAILED);
        }
    }
    ios27_phase27a_frame frame = {r->inputs.argc, s.argv, s.env, s.apple};
    int guest_exit = INT_MIN;
    ++out->main_invocations;
    if (r->hooks.enter_main(r->context, &r->images[main_index],
                            &frame, &guest_exit)) {
        out->failed_image = r->images[main_index].id;
        rc = IOS27_PHASE27A_ENTRY_FAILED;
    } else if (guest_exit != 0) {
        out->failed_image = r->images[main_index].id;
        out->guest_exit_code = guest_exit;
        rc = IOS27_PHASE27A_GUEST_FAILED;
    } else rc = IOS27_PHASE27A_OK;
    return rollback(r, out, g.ordered, out->activated, rc);
}