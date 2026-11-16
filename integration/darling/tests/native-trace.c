/* Headless Linux AArch64 fault diagnostics for our private runtime children.
 * Tracees execute normally on the CPU; ptrace records native fault registers.
 * The external timeout bounds the run. EXITKILL ties tracees to this process. */
#define _GNU_SOURCE
#include <asm/ptrace.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t snapshot_due;
static struct { pid_t pid; int snapshot; } tasks[2048];
static void snapshot_alarm(int signal) { (void)signal; snapshot_due = 1; }

static int task_index(pid_t pid, int create) {
    int empty = -1;
    for (unsigned i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i) {
        if (tasks[i].pid == pid) return (int)i;
        if (!tasks[i].pid && empty < 0) empty = (int)i;
    }
    if (create && empty >= 0) { tasks[empty].pid = pid; tasks[empty].snapshot = 0; return empty; }
    return -1;
}

static int word(pid_t pid, uintptr_t address, unsigned long *value) {
    errno = 0;
    long result = ptrace(PTRACE_PEEKDATA, pid, (void *)address, NULL);
    if (result == -1 && errno) return -1;
    *value = (unsigned long)result;
    return 0;
}

static void dump(pid_t pid, int signal, const char *kind) {
    struct user_pt_regs regs = {0};
    struct iovec io = {&regs, sizeof(regs)};
    siginfo_t info = {0};
    ptrace(PTRACE_GETSIGINFO, pid, NULL, &info);
    dprintf(2, "%s pid=%d signal=%d code=%d address=%p\n", kind, pid, signal, info.si_code, info.si_addr);
    if (ptrace(PTRACE_GETREGSET, pid, (void *)(uintptr_t)NT_PRSTATUS, &io) == 0) {
        dprintf(2, "pc=%016" PRIx64 " sp=%016" PRIx64 " pstate=%016" PRIx64 "\n",
                (uint64_t)regs.pc, (uint64_t)regs.sp, (uint64_t)regs.pstate);
        for (unsigned i = 0; i < 31; ++i)
            dprintf(2, "x%u=%016" PRIx64 "%s", i, (uint64_t)regs.regs[i], i % 4 == 3 || i == 30 ? "\n" : " ");
        uintptr_t frame = regs.regs[29];
        for (unsigned i = 0; i < 20 && frame && !(frame & 7); ++i) {
            unsigned long next, link;
            if (word(pid, frame, &next) || word(pid, frame + 8, &link)) break;
            dprintf(2, "frame[%u] fp=%016" PRIxPTR " lr=%016lx\n", i, frame, link);
            if (next <= frame || next - frame > 1024 * 1024) break;
            frame = next;
        }
        for (unsigned i = 0; i < 8; ++i) {
            unsigned long value;
            uintptr_t address = regs.sp + i * sizeof(value);
            if (word(pid, address, &value)) break;
            dprintf(2, "stack[%u]=%016lx\n", i, value);
        }
    } else dprintf(2, "register read failed: %d\n", errno);
    char path[64], buffer[2048];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t total = 0;
        ssize_t size;
        while (total < 65536 && (size = read(fd, buffer, sizeof(buffer))) > 0) {
            ssize_t used = 0;
            while (used < size) {
                ssize_t written = write(2, buffer + used, (size_t)(size - used));
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) break;
                used += written;
            }
            total += (size_t)size;
        }
        close(fd);
    }
    dprintf(2, "%s_END pid=%d\n", kind, pid);
}

int main(int argc, char **argv) {
    if (argc < 2) { fputs("usage: native-trace <program> [args...]\n", stderr); return 2; }
    unsigned snapshot_seconds = 0;
    const char *snapshot = getenv("IOS27_TRACE_SNAPSHOT_SECONDS");
    if (snapshot) {
        char *end;
        unsigned long value = strtoul(snapshot, &end, 10);
        if (snapshot == end || *end || value < 1 || value > 600) return 2;
        snapshot_seconds = (unsigned)value;
    }
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 2; }
    if (!child) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL)) { perror("ptrace TRACEME"); _exit(125); }
        raise(SIGSTOP);
        execvp(argv[1], argv + 1);
        perror("execvp"); _exit(127);
    }
    int status, result = 1;
    if (waitpid(child, &status, 0) != child || !WIFSTOPPED(status)) {
        fputs("trace child did not stop\n", stderr); return 2;
    }
    unsigned long options = PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE |
                            PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL;
    if (ptrace(PTRACE_SETOPTIONS, child, NULL, (void *)options)) {
        perror("ptrace SETOPTIONS"); kill(child, SIGKILL); return 2;
    }
    if (ptrace(PTRACE_CONT, child, NULL, NULL)) { perror("ptrace CONT"); kill(child, SIGKILL); return 2; }
    task_index(child, 1);
    if (snapshot_seconds) {
        struct sigaction action = {0};
        action.sa_handler = snapshot_alarm;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGALRM, &action, NULL)) { perror("snapshot sigaction"); kill(child, SIGKILL); return 2; }
        alarm(snapshot_seconds);
    }
    for (;;) {
        if (snapshot_due) {
            snapshot_due = 0;
            for (unsigned i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i) {
                if (tasks[i].pid) {
                    tasks[i].snapshot = 1;
                    syscall(SYS_tkill, tasks[i].pid, SIGSTOP);
                }
            }
        }
        pid_t pid = waitpid(-1, &status, __WALL);
        if (pid < 0 && errno == EINTR) continue;
        if (pid < 0) { if (errno != ECHILD) perror("waitpid"); break; }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (pid == child) result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            int index = task_index(pid, 0);
            if (index >= 0) tasks[index].pid = 0;
            continue;
        }
        if (!WIFSTOPPED(status)) continue;
        int signal = WSTOPSIG(status);
        unsigned event = (unsigned)status >> 16;
        int index = task_index(pid, 1);
        if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_VFORK || event == PTRACE_EVENT_CLONE) {
            unsigned long created;
            if (ptrace(PTRACE_GETEVENTMSG, pid, NULL, &created) == 0) task_index((pid_t)created, 1);
        }
        if (!event && (signal == SIGSEGV || signal == SIGBUS || signal == SIGILL || signal == SIGABRT || signal == SIGTRAP))
            dump(pid, signal, "NATIVE_FAULT");
        if (!event && signal == SIGSTOP && index >= 0 && tasks[index].snapshot) {
            tasks[index].snapshot = 0;
            dump(pid, signal, "NATIVE_SNAPSHOT");
        }
        if (event || signal == SIGSTOP) signal = 0;
        if (ptrace(PTRACE_CONT, pid, NULL, (void *)(uintptr_t)signal) && errno != ESRCH)
            dprintf(2, "continue pid=%d errno=%d\n", pid, errno);
    }
    return result;
}
