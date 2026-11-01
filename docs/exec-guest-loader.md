# Exec guest loader

`ios27_run_executable_exec` runs the same reviewed, freestanding ARM64 Mach-O
fixtures as `ios27_run_executable_contained`, but the guest starts in a fresh
address space instead of a fork of the launcher. The supervised child
`execveat`s the project's own `ios27-loader` helper, which re-parses a sealed
copy of the image and enters `LC_MAIN` under the same mandatory syscall
filter. This is still trusted-fixture failure containment. It does not
provide Darwin/XNU services, run Apple frameworks or iOS applications, or
sandbox untrusted Mach-O code.

## API

The declaration is in `include/ios27/process.h`:

```c
ios27_process_options options = {.timeout_ms = 1000};
ios27_process_result result;
int status = ios27_run_executable_exec(
    "./build/ios27-loader", bytes, size, &info, &startup, &options,
    &result, error, sizeof(error));
```

The startup context, outcomes, status codes, full-width `exit_code` and the
1 through 60000 ms deadline are the same as in
[contained native process execution](contained-native-process.md). The
deadline also covers `execveat` and the loader's re-parse and mapping work.

`loader_path` must name the trusted `ios27-loader` build output. The launcher
opens it once with `O_RDONLY | O_CLOEXEC` and requires a regular file. The
child then executes that descriptor with `AT_EMPTY_PATH`, so the path is not
resolved a second time. A NULL or empty path, a path that cannot be opened,
or a non-regular file fails before a child is created. Nothing authenticates
the helper beyond these checks, so callers must not pass any other
executable.

The `run-contained` command still uses the fork runner. No command-line
option selects exec mode.

## Launch payload

After repeating the Mach-O layout preflight with the host page size, the
launcher copies its inputs into a memfd named `ios27-launch`:

1. a fixed header with magic, version 1, `argc`, `envc`, `applec`, a reserved
   word, the image size and the string-table size;
2. the image bytes;
3. `argc + envc + applec` NUL-terminated strings in `argv`, `envp`, `apple`
   order.

Limits match the guarded guest stack: 128 entries per vector, 4096 bytes per
string including the terminator and 64 KiB of strings in total. Images are
limited to 64 MiB.

The memfd is created with `MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL`.
The payload is only read and copied, never executed in place, so it does not
need to be executable. Passing the flag also satisfies `vm.memfd_noexec=2`,
which the
[kernel documentation](https://docs.kernel.org/6.12/userspace-api/mfd_noexec.html)
describes as rejecting memfds created without it. Kernels that do not
recognize the flag reject it with `EINVAL`, and the launcher then retries
with only `MFD_CLOEXEC | MFD_ALLOW_SEALING`. If libc headers lack the
constant, the uapi value `0x0008` is used.

After the writes, the launcher checks the file size and adds
`F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE`. Seals belong to
the inode, not to a single descriptor
([`F_ADD_SEALS(2const)`](https://man7.org/linux/man-pages/man2/F_ADD_SEALS.2const.html)).
After sealing, no descriptor can modify, grow or truncate the payload, and
the seal set is final.

## Child and loader sequence

1. The supervisor forks a child that resets signals, disables core dumps,
   keeps only the report pipe (descriptor 3) and the payload (descriptor 4),
   sets `PR_SET_NO_NEW_PRIVS` and executes the loader descriptor with a
   single `argv` entry and an empty environment. The signal mask stays fully
   blocked across the exec.
2. The loader refuses to start unless it has exactly one argument and
   descriptor 3 is a FIFO. If you run it by hand, it prints a note and exits
   with 2 rather than adopting descriptors 3 and 4.
3. It sets `RLIMIT_CORE` to zero and enters a deny-all Landlock domain, then
   opens the payload. All four launch seals must be present (`F_GET_SEALS`)
   and the size must match. The file is mapped `PROT_READ` and `MAP_PRIVATE`,
   and the NULL-terminated vectors are rebuilt with bounds checks on every
   count, size and string. Descriptor 4 is then closed; the private mapping
   outlives it.

   The Landlock ruleset has no rules. Depending on the ABI version the kernel
   reports, it handles every filesystem right through
   `LANDLOCK_ACCESS_FS_IOCTL_DEV` (ABI 5), TCP bind and connect (ABI 4), and
   the abstract UNIX socket and signal scopes (ABI 6). New file and directory
   opens, executions and filesystem changes are therefore refused for the
   loader and for the guest that later runs in the same process. Rights added
   by later ABI versions are not requested. Descriptors opened before
   enforcement, here the report pipe and the sealed payload, stay usable, as
   the [kernel documentation](https://docs.kernel.org/userspace-api/landlock.html)
   describes. After enforcement the loader confirms that opening `/` for
   reading fails with `EACCES`. Following the documented best-effort pattern,
   `ENOSYS` or `EOPNOTSUPP` from the version query skips the domain, and the
   mandatory seccomp filter still applies before guest entry. Any other
   Landlock failure is a setup error. Android kernel support, and whether
   Android's app seccomp policy permits these syscalls, are not verified.
4. Launcher metadata is never trusted across exec. The loader re-runs
   `ios27_macho_parse` and `ios27_macho_executable_layout` on its sealed copy
   with its own page size.
5. `ios27_native_handoff` applies the in-process runner's preflight, guarded
   guest stack and final W^X mappings. The guest stack holds private copies
   of `argv`, `envp` and `apple`.
6. Before `LC_MAIN`, the loader unmaps the payload, closes every descriptor
   except the report pipe, sets `PR_SET_NO_NEW_PRIVS`, clears its signal
   mask, installs the mandatory seccomp filter and sends the readiness
   packet.

## Reports and setup errors

The loader uses the fork child's 12-byte packets (magic, kind, value): either
one SETUP packet, or READY followed by RETURN with the guest's C `int`. The
loader exits with 0 after a RETURN packet is delivered and with 125
otherwise. A SETUP value is either a positive `errno` or one of these loader
codes:

| Value | Meaning |
| ---: | --- |
| -1 | sealed payload rejected |
| -2 | sealed image failed re-parse or layout checks |
| -3 | loader built for a non-ARM64 host |
| -4 | native preparation failed or the handoff returned |

As with the fork runner, a setup report or a signal before readiness fails
the API, and a raw exit without a RETURN report is a report failure.

## What changes and what does not

- The guest no longer inherits the launcher's heap, stack, mappings or
  descriptors. Its address space holds the loader executable and its
  libraries, the guest stack and the guest image mappings.
- The report pipe is still writable from that address space, so its packets
  are not authenticated evidence about an untrusted guest.
- The syscall filter is the same one the fork runner uses. It checks native
  Linux syscall numbers and does not translate Darwin syscalls.
- Native entry requires ARM64 Linux with memfd sealing, `execveat`,
  `close_range`, seccomp filters and `PR_SET_NO_NEW_PRIVS`, and setup fails
  closed without them. On other architectures the API rejects execution
  before opening the loader. The single-threaded launcher and default
  `SIGCHLD` requirements still apply.
- `ios27-loader` is also built for Android. Android kernels and libc, and
  Android's limits on executing app-writable files, have not been checked
  against it. No Galaxy S25+ device test is claimed.

## Verification scope

`contained-native-macho` passes `$<TARGET_FILE:ios27-loader>` as its third
argument and repeats its scenarios in both fork and exec mode:
`argv`/`envp`/`apple` copies and fresh globals, invalid and stale inputs, the
labeled synthetic trap, loop and forbidden-syscall injections followed by
recovery, unsupported-host rejection and option validation. Exec mode also
rejects NULL, empty, nonexistent and non-loader paths. Its loop case uses a
250 ms deadline instead of 40 ms because the deadline includes the exec and
the re-parse. The native cases need an ARM64 runner; on x86_64, only the
validation and rejection cases run.

`contained-process-supervisor` (`tests/test_process.c`) also checks the
launch payload directly. A created payload must be close-on-exec and a
regular file, carry all four launch seals, have no execute bits when
`F_SEAL_EXEC` is reported, and refuse writes, growth, truncation, further
sealing and shared writable mappings with `EPERM`. The reader must reject a
memfd missing any launch seal, an invalid descriptor, corrupted headers or
string tables and oversized files, while accepting a resealed faithful copy,
strings exactly at the length limit, a full 64 KiB string table and an empty
startup context. Creation must enforce the vector, string, string-table and
image limits without leaking a descriptor. Passing these tests does not
satisfy the general Darwin process-startup or untrusted-guest-isolation
capability gates.
