#ifndef IOS27_LIBSYSTEM21I_ERRNO_H
#define IOS27_LIBSYSTEM21I_ERRNO_H
/* Darwin values derived from Apple XNU bsd/sys/errno.h at
 * f6217f891ac0bb64f3d375211650a4c1ff8ca1ea.
 * Never pass Linux host errno numbers unchanged to guest pthread calls. */
enum ios27_21i_darwin_errno {
    IOS27_21I_DARWIN_OK = 0,
    IOS27_21I_DARWIN_EPERM = 1,
    IOS27_21I_DARWIN_ESRCH = 3,
    IOS27_21I_DARWIN_EINTR = 4,
    IOS27_21I_DARWIN_EIO = 5,
    IOS27_21I_DARWIN_EDEADLK = 11,
    IOS27_21I_DARWIN_ENOMEM = 12,
    IOS27_21I_DARWIN_EACCES = 13,
    IOS27_21I_DARWIN_EFAULT = 14,
    IOS27_21I_DARWIN_EBUSY = 16,
    IOS27_21I_DARWIN_ENODEV = 19,
    IOS27_21I_DARWIN_EINVAL = 22,
    IOS27_21I_DARWIN_ENOSPC = 28,
    IOS27_21I_DARWIN_ERANGE = 34,
    IOS27_21I_DARWIN_EAGAIN = 35,
    IOS27_21I_DARWIN_EINPROGRESS = 36,
    IOS27_21I_DARWIN_ENOTSUP = 45,
    IOS27_21I_DARWIN_ETIMEDOUT = 60,
    IOS27_21I_DARWIN_ENOSYS = 78,
    IOS27_21I_DARWIN_EOVERFLOW = 84,
    IOS27_21I_DARWIN_ENOTRECOVERABLE = 104,
    IOS27_21I_DARWIN_EOWNERDEAD = 105
};
/* Host errno is preserved by internal APIs; convert at the guest ABI edge.
 * Unexpected host errors are mapped to Darwin EIO, not blindly forwarded. */
int ios27_21i_errno_to_darwin(int host_errno);
#endif
