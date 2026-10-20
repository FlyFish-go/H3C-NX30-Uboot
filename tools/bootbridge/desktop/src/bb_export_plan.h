#ifndef BB_EXPORT_PLAN_H
#define BB_EXPORT_PLAN_H
#include "bb_export.h"

/* Release-engineering compiled delta programs only. No external patch/parser,
 * executable code, decompressor, target device or downloaded rule is accepted.
 * COPY/XOR read the same pinned original input handle in arbitrary order.
 * LITERAL is reserved for reviewed new code/metadata; the offline compiler must
 * account separately for unchanged original content and literal provenance. */
typedef enum { BB_PLAN_COPY=1, BB_PLAN_LITERAL=2, BB_PLAN_XOR=3 } bb_plan_kind;
typedef struct {
    bb_plan_kind kind;
    uint64_t input_offset, size;
    const unsigned char *bytes; /* NULL for COPY; exact size for LITERAL/XOR. */
} bb_plan_op;
typedef struct {
    const char *id;
    uint64_t input_size;
    unsigned char input_sha256[32];
    uint64_t output_size;
    unsigned char output_sha256[32];
    const bb_plan_op *operations;
    size_t operation_count;
} bb_plan_recipe;
#define BB_PLAN_MAX_OPERATIONS 262144u

/* Same single-BIN, original-readonly and no-overwrite contract as V1. Hashes
 * and ranges are compiled trusted data; the actual input is hashed before and
 * after all COPY/XOR operations, and the complete output is independently read
 * back before commit. No companion output or partial final BIN on failure. */
bb_result bb_export_plan_bin(const char *,const char *,const bb_plan_recipe *,
                             size_t,const char *);
#endif
