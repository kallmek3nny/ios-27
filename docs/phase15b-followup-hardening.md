# phase 15b follow-up: nonzero TEXT offsets and fail-closed metadata

this incremental change follows phase 15b prs #31 and #33. it retains both
the freestanding runner and the multi-image native entrypoint implementation.

- account for the `__TEXT.fileoff` when computing the `LC_MAIN` virtual entry;
  reject offset underflow/overflow, and check the offset is file-backed executable
  code before any address becomes callable.
- flag previously unknown `LC_REQ_DYLD` load commands during parsing, and refuse
  native execution. unknown required commands may carry unimplemented semantics.
- reject malformed, duplicate, or virtual-address-overlapping `__PAGEZERO`
  segments so reserved no-access ranges cannot silently mask mapped regions.
- require the explicit multi-image loader to reject unknown required commands
  and overlapping PAGEZERO reservations before staging memory.
- deterministic tests cover a nonzero `__TEXT` file offset, unknown required
  load commands, overlapping `__PAGEZERO`, and invalid dylib max protections.

all execution remains restricted to trusted freestanding test fixtures; no
kernel/bootloader/root changes, emulation, Apple binaries, or Darwin ABI claims.
