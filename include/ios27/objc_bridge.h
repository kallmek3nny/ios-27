#ifndef IOS27_OBJC_BRIDGE_H
#define IOS27_OBJC_BRIDGE_H
#include "ios27/objc_metadata.h"
#include "ios27/objc_runtime.h"

/* Bind inspected, compiler-generated class/selector metadata to explicit,
 * trusted host function implementations. NO implicit executable pointer
 * conversion, dyld linking or invocation of unverified Mach-O bytes. */
typedef ios27_objc_imp (*ios27_objc_resolve_host_imp)(void *context,
                                                       const char *class_name,
                                                       const char *selector);
int ios27_objc_register_metadata(ios27_objc_runtime *runtime,
                                 const ios27_objc_metadata *metadata,
                                 ios27_objc_resolve_host_imp resolver,
                                 void *context);
#endif
