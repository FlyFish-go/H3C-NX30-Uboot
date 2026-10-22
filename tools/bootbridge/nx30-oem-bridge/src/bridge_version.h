#ifndef BRIDGE_VERSION_H
#define BRIDGE_VERSION_H

#include <stddef.h>
#include <stdint.h>

typedef enum { BV_OEM_BASELINE = 1, BV_CUSTOM_RELEASE = 2 } bv_kind;
typedef struct {
    const char *sha256; /* exact whole padded APPSBL partition */
    const char *scope;  /* reviewed binary family/layout/storage contract */
    uint64_t bytes, sequence;
    bv_kind kind;
} bv_record;
typedef enum {
    BV_FIRST_INSTALL = 0, BV_SKIP_IDENTICAL, BV_UPGRADE,
    BV_REJECT_DOWNGRADE, BV_REJECT_UNKNOWN, BV_REJECT_INCOMPATIBLE,
    BV_INVALID_REGISTRY,
    /* Installed APPSBL is unknown, but the candidate is a valid registered
     * custom release. Never produced by bv_evaluate(); the installer derives
     * it to overwrite whatever U-Boot is installed. */
    BV_OVERRIDE_UNKNOWN
} bv_decision;
typedef struct {
    bv_decision decision;
    uint64_t installed_sequence, candidate_sequence;
} bv_result;

/* The immutable registry is release-engineering compiled data, never runtime
 * version strings or a device-supplied manifest. Sequence is explicit release
 * order; OEM baselines have sequence 0. Display names are not compared.
 * Hashes must come from complete actual physical partition reads, rechecked at
 * commit and bound to the operation token. This policy does NOT establish NAND,
 * preloader, storage, board, bootability or authorization to write. All those
 * independent gates must still pass before a write/activation can be offered.
 */
bv_result bv_evaluate(const bv_record *registry, size_t count,
                      const char *scope, const char *candidate_sha256,
                      uint64_t candidate_bytes, const char *installed_sha256,
                      uint64_t installed_bytes);
#endif
