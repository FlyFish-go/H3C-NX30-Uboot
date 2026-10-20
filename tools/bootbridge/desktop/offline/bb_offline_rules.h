#ifndef BB_OFFLINE_RULES_H
#define BB_OFFLINE_RULES_H
#include "../catalog/bb_catalog.h"
#include <wchar.h>

/* Load the exact adjacent BootBridge.rules.dat once, verify its compiled size
 * and SHA-256 pin, and return the immutable process-lifetime catalog. */
int bb_offline_rules_load_verify(bb_bundle_catalog *out_catalog,
                                 wchar_t *message, size_t capacity);

/* Release identity compiled in from the generated catalogue header; the same
 * value the loader reports, so callers never compare against a placeholder. */
const char *bb_offline_rules_release_set(void);

#endif
