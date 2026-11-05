# Phase 24b: independently bounded Darwin image lifecycle

Branch: \`agent/phase24b-native-lifecycle-20261029\`  
Starting \`main\`: \`527adf78a63890e219812bed8ff773af2f5cd70f\`  
Read-only references: phase23a PR #92, phase23b PR #96. No references imported.

## Four implementation milestones

1. **Validated linked image metadata:** \`src/phase24b_lifecycle.c\` parses 64-bit little-endian arm64 MH_DYLIB and MH_EXECUTE segment/section tables with bounded counts, file ranges, W^X executable ranges, duplicate section checks, and strict section flags. Supports \`__mod_init_func\`, \`__mod_term_func\`, and the restricted \`__init_offsets\` encoding (\`S_INIT_FUNC_OFFSETS\`, 32-bit \`__TEXT\` relative offsets). Mixing init pointer tables with init offset tables is rejected because an ordering policy is not verified.
2. **Post-fixup callback binding:** \`ios27_24b_bind\` accepts a caller-supplied *already relocated* guest-memory reader and slide, resolves pointers and offset entries, validates alignment and *image-owned* executable ranges, and commits all resolved callbacks atomically. No guest instructions are executed while parsing or binding. The API intentionally refuses unsigned slide overflows and unsupported image layouts.
3. **Dependency-aware lifecycle:** bounded DFS rejects cycles and missing dependencies. Leases pin transitive dependencies; a global callback gate establishes deterministic dependency-first initialization and reverse-topological teardown while rejecting reentrant loading. Exactly-once image startup is shared among independent handles. Cross-thread ensure waits with a three-second deadline; concurrent close is refused while startup/shutdown callbacks are pinned. Generation plus unique lease tickets reject stale/double close. A constructor failure becomes sticky; previously completed callbacks are compensated *only* through an explicitly supplied compensation bridge. Failure to compensate quarantines the image and its transitive graph, disallowing unsafe retirement. Destructor failure quarantines remaining graph dependencies. The engine does not unmap memory: a separately verified loader must honor all references, pins, and quarantine states before releasing executable pages.
4. **Authentic fixtures and executable host stress:** \`tools/phase24b-check.py\` invokes Clang for six original ARM64 Darwin MH_OBJECTs and LLVM \`ld64.lld\` for six genuine interdependent MH_DYLIBs, verifying constructor/destructor sections and dependency commands. \`tests/phase24b-native.c\` exercises actual linked Mach-O parsing, transactional simulated mapped-memory binding, 10,000 full six-image startup/shutdown cycles (120,000+ modeled constructor/destructor dispatches), two independent roots with overlapping transitive dependencies, eight concurrent startup threads, failure/compensation, stale handles, and reentrant loading. \`.github/workflows/phase24b-lifecycle.yml\` runs this on x86_64 and aarch64 Ubuntu, with x86_64 ASan/UBSan. All subprocess calls are bounded.

## Execution truth table

| Operation | Status |
| --- | --- |
| Compiler and LLVM link real Darwin ARM64 dylibs | Implemented, CI validation pending |
| Inspect real linked Mach-O constructor metadata | Implemented, CI validation pending |
| Bind from caller-supplied post-fixup VM reads | Implemented, exercised by modeled mapped-memory reads |
| Host-native C state machine and callback model | Implemented, CI validation pending |
| Execute an actual *compiled Darwin* constructor in guest mappings | **Not demonstrated; 0 callbacks claimed** |
| Verify relocator, executable mappings, Darwin ABI bridge and foreign Mach/libSystem calls | **Not implemented here** |

An Ubuntu ARM64 runner executes the **host test harness** natively; this does *not* execute the fixture's Mach-O callbacks. The test executor's \`IOS27_24B_MODELED\` marker is deliberate. The API also exposes \`IOS27_24B_NATIVE_VERIFIED\` as a caller-side claim for a **future independently validated guest bridge**; setting that marker alone is not evidence of native Darwin execution.

## Safety, limitations, and test commands

No shared source, root CMakeLists, HANDOFF, or global progress files changed. Standalone build uses:

\`\`\`sh
timeout 300s python3 tools/phase24b-check.py
timeout 300s env PHASE24B_SANITIZE=1 python3 tools/phase24b-check.py
\`\`\`

The executable loader, dyld relocation setup and native callback entrypoint needed for actual guest execution are not present within this standalone phase24b implementation. Destructor failures cannot be undone automatically; they quarantine images and require higher-level recovery. Native ABI restrictions include untrusted callback pointers, arm64e pointer authentication, unsupported fixup formats, and external Darwin services, all outside the runtime bridge's attested scope.

The **3+ active engineering hour requirement has not been established** in this session, and must not be inferred from wall-clock PR timestamps. Source/test evidence and CI status should be checked independently before merge. Never treat green modeled tests as permission to unmap executing images or as proof of real iOS framework compatibility.
