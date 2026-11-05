# phase26b: capability-backed bsd guest syscall subset

## execution boundary

this independent module executes host-native C dispatcher logic against a bounded guest memory model. it validates a saved arm64 x0-x7/x16 register frame and an svc #0x80 instruction encoding, returning a positive darwin errno in x0 with carry set on failure. it does **not** intercept an actually executing arm64 guest svc, run a darwin mh_execute on linux, or implement dyld bindings. the standalone clang/lld mach-o fixture is genuinely compiler-linked and contains svc, but is **only inspected**.

every backing file is in process-owned ram; host paths are never opened. no network, host syscalls, external children, devices, privilege changes or host kernel resources are exposed.

## intentionally narrow contracts

- open(5), openat(463, only absolute paths with at_fdcwd=-2), close(6), read(3), write(4), pread(153), pwrite(154)
- lseek(199), fstat(189, 144-byte darwin lp64 inode64 layout), fcntl(92: f_dupfd and f_getfl only)
- mmap(197, anonymous private guest pages), munmap(73), mprotect(74, no execute), gettimeofday(116, injected epoch)
- getpid(20), getppid(39), getuid(24), geteuid(25), getgid(47), getegid(43) return guest-only identities

all unsupported numbers, classes, flags and forms fail closed. each operation holds a per-process mutex. guest memory is 256 kib in 4 kib pages; the initial 64 kib is mapped rw, and mmap allocates from the rest. guest addresses never become arbitrary host pointers. file count is at most 16, file length at most 64 kib, fd slots at most 32 and individual io copies at most 8 kib.

the virtual filesystem has an immutable /app/readme.txt, mutable /tmp/data, and allows creation of single-component names in /tmp. traversal, dot components, backslashes and paths outside the virtual namespace never reach the host. descriptor numbers include a generation count; duplicate descriptors share an open description and file offset. pread and pwrite do not advance that offset. stop prevents new operations; callers must join before destroy.

## validation

independent entrypoint: timeout 300s python3 tools/phase26b-verify.py

the runner compiles host-native -o0 and -o2 integration stress with -werror, and executes 320,000 thread-contended dispatcher calls per build. phase26b_sanitize=1 additionally enables asan/ubsan. it separately compiles and links a genuine arm64 darwin mh_execute, inspects its architecture/filetype and checks for the svc opcode. these are not native darwin guest trap executions. ci covers x86_64 and linux arm64 where runners support them.

## tracked libsystem_kernel/bsd targets with modeled service evidence

open, openat (restricted), close, read, write, pread, pwrite, lseek, fstat, fcntl (two commands), mmap, munmap, mprotect, gettimeofday, getpid, getppid, getuid, geteuid, getgid and getegid.

these may be described as **host-native guest service models** after their checks pass. they must not be counted as native-fixture darwin syscall execution until a real guest code-to-trap path is implemented and verified. no shared tracker files were edited.

## unsupported

native running darwin binaries, full kernel bsd, host filesystem delegation, fcntl flags beyond getfl/dupfd, file-backed mmap, signals, cancellation points, guest process creation, native guest thread scheduling, dynamic virtual directory traversal, sockets, and device or privilege services.
