# phase26d: independent cpu compositor reference

this module is **not apple coreanimation**. it is a bounded, independently implemented
reference scene graph that supplies an eventual adapter boundary for phase26c.
there is no claim that ios ui frameworks or objective-c CALayer execute.

## contracts and milestones
1. versioned handles, bounded 256-node forest, cycle rejection, recursive retirement,
   affine transforms and stable sibling z-order. mutations require a transaction.
2. at most 8 nested savepoints, nested rollback, outermost atomic publication,
   read-only model and presentation snapshots, monotonically counted commits.
3. immutable, copied premultiplied rgba backing stores (256x256 maximum, 16 mib
   aggregate live image budget), retained across model/working/savepoint/presentation
   snapshots. zero external pointers escape the lock. invalid upload is atomic.
4. timestamp-driven opacity/position linear animations; deterministic cpu rgba8
   source-over compositing with conservative rounded bounds, clipping, simple
   local box-blur textures, tinted material rectangle, and black shadow edges.

## explicitly excluded
apple ca transactions, native apple metadata execution, implicit animations,
cubic bezier animation, gpu submission, iosurface, ahardwarebuffer, color
management beyond rgba8 srgb-assumed, isolation/group opacity, backdrop gaussian
blur, display refresh timing and presentation fences. the simple material/shadow
primitives are **prototypes** and not apple-compatible visual effects.
the cpu step timestamp is caller-provided. no android dependencies.

## safety, ownership, limitations
every api is internally protected by one mutex; render serializes with mutation.
transaction ownership is pinned to the initiating pthread; a foreign thread
cannot mutate, nest, commit, or rollback that thread's open transaction.
the caller keeps frame pixels alive through the synchronous render call.
free must not race a concurrent api call. image input must be premultiplied
rgba8, color fill uses straight rgba8. descriptor geometry is integer pixels;
transform is finite affine with nonzero determinant; sampling uses pixel centers.
committing an outer transaction replaces the presentation snapshot immediately;
step(timestamp) subsequently evaluates animations without changing model data.
rollback restores one nested savepoint (not the entire outer transaction).
the intentional full-frame reference rasterizer is not optimized for production.
a caller needing fast frames should connect a verified surface backend later.

## native objective-c evidence
the sdk-free phase26d fixture is cross-compiled by clang into genuine arm64
darwin objc2 mh_object metadata. this is a **compile/metadata check only**.
it is not executed through objc_msgsend or apple coreanimation. actual
runtime execution requires separately verified dispatch and bootstrap integration.

## reproducible verification
run `timeout 120s clang -std=c11 -Wall -Wextra -Werror -Wpedantic -pthread -Iinclude src/phase26d_compositor.c tests/phase26d-core.c -lm -o /tmp/phase26d-core`
then `timeout 180s /tmp/phase26d-core`.
the dedicated pr workflow runs on linux x86_64 and arm64, with asan/ubsan.
no docs/progress.json or progress counters are modified by this branch.
