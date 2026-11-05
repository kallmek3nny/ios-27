# phase27d: independent animated scene and bounded view adapter

This is **not** Apple's CoreAnimation, UIKit, UIWindow, or Liquid Glass.
It is an original, portable C11 scene/view compatibility subset layered over the
tested `phase26d_compositor` CPU rasterizer from [PR #108](https://github.com/kallmek3nny/ios-27/pull/108).
No proprietary binaries, SDK, Android APIs, or GPU acceleration are loaded.

## Architecture and integration boundary

```text
logical view tree (model) + transaction savepoints
       | atomic outermost commit
       v
evaluate timed presentation geometry (scale, affine, x/y, alpha)
       | private rebuild (reject invalid geometry without mutation)
       v
phase26d CPU rgba8 rasterizer (private presentation cache)
       | bounded 512x512 synchronous render
       v
p27d_surface_sink (borrowed immutable premultiplied RGBA8 frame)
       | copy/import into validated host graphics surface, before callback returns
       v
agent 3 surface contract / agent 5 Android host
```

`p27d_submit(scene, width, height, sink, user)` is a portable fallback
contract for **agent 3**: pixels are row-major premultiplied RGBA8, with
stride `width * 4`, bounded capacity `width * height * 4`, caller-supplied
frame dimensions, a monotonic revision counter, timestamp, and FNV-1a64.
The callback must synchronously copy/import the data before it returns; the
buffer is retired immediately afterward. It is not a GPU fence or an
IOSurface handle. A future bridge can translate this format into the validated
phase27c surface descriptor without either module importing the other.
Callbacks execute **outside the scene lock** and may re-enter scene APIs.

For **agent 5**, `p27d_pointer_event` carries action, timestamp, pointer ID,
and floating-point scene coordinates. `p27d_route_event` returns a fixed-size
leaf-to-root responder path (up to 64 views), with captured pointer semantics.
The host owns callback dispatch, focus, gesture recognition, keyboard/text
editing, and accessibility translation; those are not invented here.
All scene mutation and animation are separate from input callbacks.
Only ten simultaneously captured pointer IDs are supported.

## Scene semantics

- Max 64 original views, each with a versioned stale-handle-rejecting identity.
  Parents are explicit; cycles and orphaned parents are rejected.
- At most eight nested transactions. The transaction owner is thread-bound.
  Nested rollback restores its savepoint. No intermediate state can reach
  presentation; the outermost commit publishes the new model and frame together.
- `p27d_get(..., presentation=false)` is logical, stable model geometry.
  `presentation=true` is the last atomically published evaluated geometry.
  The private phase26d model is **a transient presentation cache** and is not
  exposed as the logical model.
- Animated X/Y, opacity, uniform scale, and the six affine coefficients.
  Curves: linear, quadratic ease in/out, cubic smoothstep. Times are explicit
  unsigned nanoseconds (including a caller-supplied zero epoch).
  No frame clock or OS timing dependency. `p27d_next_frame` suggests a
  16.67 ms upper-bound step until the next animation boundary.
- Unsupported/singular intermediate transforms fail closed; the previous
  rendered snapshot remains valid. Canceled animations revert to the model
  description. Hidden parents and a non-visible window suppress rendering
  and hit testing. View layout invalidation uses conservative whole-frame
  damage revisions, not partial tiles.
- Hit tests use inverse affine mapping, pixel bounds, rounded corners, clip
  ancestors, inherited visibility, and the same child z-order ordering as
  phase26d, with stable slot tie-breaking. The world/local conversion API is
  explicit. This is a deliberately narrow UIView-style coordinate model:
  each view has local bounds [0,width) x [0,height), and position + affine
  translation are relative to its parent.
- Minimal UIWindow detached/attached/visible and independent controller-root
  unloaded/loaded/visible lifecycles provide explicit transactional transitions.
  Controller appearance requires a visible window with the same root and a
  previously loaded controller; hiding the window downgrades its controller,
  and subtree teardown invalidates controller identity. This does not implement `UIApplication`, UINavigationController,
  UIKit appearance callbacks, first responder or platform window services.
- No external node/backing pointers escape synchronization. Read/render/step,
  transaction mutation, and retirement are mutex-serialized. Callers must
  quiesce all users before freeing a scene.

## Reproducible visual verification

Build on the phase26d PR branch:

```sh
timeout 120s clang -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
  -pthread -Iinclude src/phase26d_compositor.c src/phase27d_scene.c \
  tests/phase27d-scene.c -lm -o /tmp/phase27d-test
mkdir -p /tmp/phase27d-frames
timeout 180s /tmp/phase27d-test /tmp/phase27d-frames
python3 tools/phase27d-reference.py /tmp/phase27d-frames
sha256sum /tmp/phase27d-frames/frame-*.png
```

The test checks a pinned red 2x2 RGBA FNV-1a64 golden hash, exact pixel
colors for a root, transparent layered panel and moving control, three distinct
animated frame hashes, repeated identical-timestamp frame equality, and
synchronous sink delivery. It also exercises transaction savepoint rollback,
stale handles, invalid buffer rejection, singular transform rejection, event
routing and pointer capture, hidden-window lifecycle, and simultaneous
read/render versus frame rebuild.

CI executes the same fixture on linux x86_64 and arm64, both normal and
ASan/UBSan, all under explicit timeouts with `-Werror`. The workflow uploads
three lossless reference PNGs (start, mid, end) as artifacts. The images are
generated by tested RGBA data via a Python stdlib-only PNG encoder, not by
stock artwork or an Apple rendering implementation.

## Deliberate limits and next-stage handoffs

The CPU backend's box blur, translucent tint, shadow approximation, and
premultiplied-alpha math are reused as validated in PR #108. This phase
does not claim physically correct frosted-glass backdrop sampling, Gaussian
blur, gamma-correct compositing, true Metal/Vulkan presentation, a real
UIKit responder runtime, asynchronous surface fences, or Apple-compatible
native framework binaries. Future agents must independently validate these.
No global progress counters were modified.

**Dependency:** PR #108 remains open. This PR targets its source branch
to isolate only phase27d changes; retarget to `main` once PR #108 merges.
