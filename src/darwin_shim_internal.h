#ifndef IOS27_DARWIN_SHIM_INTERNAL_H
#define IOS27_DARWIN_SHIM_INTERNAL_H

#include "ios27/service_bindings.h"

/* Host tests install their own table here. In Mach-O this original record
 * resides in __DATA,__ios27_api and is populated by the services loader. */
extern ios27_service_binding_slot ios27_shim_binding;

#endif
