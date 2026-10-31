#ifndef IOS27_PROCESS_INTERNAL_H
#define IOS27_PROCESS_INTERNAL_H

#include "ios27/process.h"

/* Private supervisor boundary. entry must use only prepared private memory
 * and return normally; the child cannot allocate or call general host libc.
 * Exposed to native supervisor tests, not to guest import resolution. */
int ios27_process_supervise(int (*entry)(void *), void *context,
                            const ios27_process_options *options,
                            ios27_process_result *result,
                            char *error, size_t error_size);

/* Fixed-size child report protocol shared by the fork child and the exec'd
 * guest loader: one SETUP failure packet, or READY followed by RETURN. */
#define IOS27_REPORT_MAGIC UINT32_C(0x49323750)
#define IOS27_REPORT_RETURN 0u
#define IOS27_REPORT_SETUP 1u
#define IOS27_REPORT_READY 2u

/* Negative SETUP values sent by the guest loader; positive values are errno. */
#define IOS27_LOADER_E_PAYLOAD (-1)
#define IOS27_LOADER_E_IMAGE (-2)
#define IOS27_LOADER_E_ARCH (-3)
#define IOS27_LOADER_E_PREPARE (-4)

/* Descriptor layout inherited across execveat by the guest loader. */
#define IOS27_LOADER_REPORT_FD 3
#define IOS27_LOADER_PAYLOAD_FD 4

/* Child-side helpers. Only async-signal-safe system calls are used. */
int ios27_process_report(int fd, uint32_t kind, int value);
/* Close every descriptor outside [first, last]; requires close_range. */
int ios27_process_close_outside(int first, int last);
/* Mandatory guest filter: native exit, exit_group, rt_sigreturn and a
 * report-sized write to report_fd only. Requires no_new_privs. */
int ios27_process_install_filter(int report_fd);

/* Sealed memfd launch payload: header, image bytes, then argc + envc +
 * applec NUL-terminated startup strings in argv, envp, apple order. */
#define IOS27_LAUNCH_MAGIC UINT32_C(0x4c373249)
#define IOS27_LAUNCH_VERSION 1u
#define IOS27_LAUNCH_MAX_VECTOR 128u
#define IOS27_LAUNCH_MAX_STRING 4096u /* including the terminator */
#define IOS27_LAUNCH_MAX_STRINGS (64u * 1024u)
#define IOS27_LAUNCH_MAX_IMAGE (64u * 1024u * 1024u)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t argc;
    uint32_t envc;
    uint32_t applec;
    uint32_t reserved;
    uint64_t image_size;
    uint64_t strings_size;
} ios27_launch_header;

typedef struct {
    uint8_t *mapping;
    size_t mapping_size;
    const uint8_t *image;
    size_t image_size;
    ios27_startup_context context;
    const char *vectors[3u * (IOS27_LAUNCH_MAX_VECTOR + 1u)];
} ios27_launch_payload;

/* Copy the image and startup strings into a new close-on-exec memfd sealed
 * against write, grow, shrink and further sealing. Startup limits match the
 * guest stack: 128 entries per vector, 4096 bytes per string including the
 * terminator and 64 KiB of strings. On failure *fd is -1. */
int ios27_launch_payload_create(const uint8_t *data, size_t size,
                                const ios27_startup_context *context,
                                int *fd, char *error, size_t error_size);
/* Loader side: verify the seals, map the payload read-only and rebuild the
 * NULL-terminated vectors. Every count, size and string is bounds checked. */
int ios27_launch_payload_open(int fd, ios27_launch_payload *payload);
void ios27_launch_payload_close(ios27_launch_payload *payload);

/* Spawn the trusted loader executable referenced by loader_fd (opened
 * O_CLOEXEC) with payload_fd as its sealed launch payload; see process.h. */
int ios27_process_supervise_exec(int loader_fd, int payload_fd,
                                 const ios27_process_options *options,
                                 ios27_process_result *result,
                                 char *error, size_t error_size);

/* Loader-only native entry, implemented by runner.c. The image, metadata and
 * startup vectors get the same preflight, guarded guest stack and final W^X
 * mappings as ios27_run_executable_context; then handoff(entry, call) runs in
 * place of LC_MAIN. handoff must install containment, call entry(call) and
 * terminate the process. If it returns, every guest mapping is removed and
 * -1 is returned. Requires ARM64 Linux/Android; elsewhere fails unmapped. */
int ios27_native_handoff(const uint8_t *data, size_t size,
                         const ios27_macho_info *info,
                         const ios27_startup_context *context,
                         void (*handoff)(int (*entry)(void *), void *call),
                         char *error, size_t error_size);

#endif
