#ifndef IOS27_SERVICE_BRIDGE_INTERNAL_H
#define IOS27_SERVICE_BRIDGE_INTERNAL_H

#include "ios27/service_bridge.h"

/* Reset a helper-private phase20e model and return its immutable entry table.
 * Called before guest entry. No host I/O, allocation or environment import.
 * One model per process; resetting invalidates all earlier arena spans. */
const ios27_service_api *ios27_service_bridge_initialize(void);

#endif
