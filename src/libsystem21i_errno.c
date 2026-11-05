#include "ios27/libsystem21i_errno.h"
#include <errno.h>

int ios27_21i_errno_to_darwin(int native_errno) {
    switch (native_errno) {
    case 0: return IOS27_21I_DARWIN_OK;
    case EPERM: return IOS27_21I_DARWIN_EPERM;
    case ESRCH: return IOS27_21I_DARWIN_ESRCH;
    case EINTR: return IOS27_21I_DARWIN_EINTR;
    case EIO: return IOS27_21I_DARWIN_EIO;
    case EDEADLK: return IOS27_21I_DARWIN_EDEADLK;
    case ENOMEM: return IOS27_21I_DARWIN_ENOMEM;
    case EACCES: return IOS27_21I_DARWIN_EACCES;
    case EFAULT: return IOS27_21I_DARWIN_EFAULT;
    case EBUSY: return IOS27_21I_DARWIN_EBUSY;
    case ENODEV: return IOS27_21I_DARWIN_ENODEV;
    case EINVAL: return IOS27_21I_DARWIN_EINVAL;
    case ENOSPC: return IOS27_21I_DARWIN_ENOSPC;
    case ERANGE: return IOS27_21I_DARWIN_ERANGE;
    case EAGAIN: return IOS27_21I_DARWIN_EAGAIN;
    case EINPROGRESS: return IOS27_21I_DARWIN_EINPROGRESS;
    case ENOTSUP: return IOS27_21I_DARWIN_ENOTSUP;
    case ETIMEDOUT: return IOS27_21I_DARWIN_ETIMEDOUT;
    case ENOSYS: return IOS27_21I_DARWIN_ENOSYS;
    case EOVERFLOW: return IOS27_21I_DARWIN_EOVERFLOW;
#ifdef ENOTRECOVERABLE
    case ENOTRECOVERABLE: return IOS27_21I_DARWIN_ENOTRECOVERABLE;
#endif
#ifdef EOWNERDEAD
    case EOWNERDEAD: return IOS27_21I_DARWIN_EOWNERDEAD;
#endif
    default: return IOS27_21I_DARWIN_EIO;
    }
}
