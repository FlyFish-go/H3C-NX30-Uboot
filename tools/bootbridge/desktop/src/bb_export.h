#ifndef BB_EXPORT_H
#define BB_EXPORT_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t offset;              /* Coordinates in original input. */
    uint64_t remove_size;
    const unsigned char *bytes;   /* Immutable, compiled-in replacement. */
    size_t bytes_size;
} bb_span;

typedef struct {
    const char *id;
    uint64_t input_size;
    unsigned char input_sha256[32];
    uint64_t output_size;
    unsigned char output_sha256[32];
    const bb_span *spans;
    size_t span_count;
} bb_recipe;

typedef enum {
    BB_OK = 0, BB_UNKNOWN_RECIPE, BB_INVALID_RECIPE, BB_INVALID_PATH,
    BB_INPUT_ERROR, BB_INPUT_MISMATCH, BB_OUTPUT_EXISTS, BB_IO_ERROR,
    BB_OUTPUT_MISMATCH
} bb_status;

typedef struct {
    bb_status status;
    uint64_t bytes_written;
    int system_error;
} bb_result;

/* Pure lexical Windows path guard; available on every host for GUI/tests.
 * Allows ordinary relative, absolute drive and UNC paths. Rejects ADS,
 * device namespaces and components normalized by Win32 trailing-dot/space rules.
 */
int bb_windows_path_allowed(const char *path_utf8);

/* Registry MUST be immutable trusted recipes compiled by release engineering.
 * Never pass user-provided/runtime parsed recipes. Empty registry rejects all.
 * UTF-8 paths; output must end in .bin (ASCII case insensitive). No overwrite.
 * No companion files/logs. Call synchronously on a GUI worker thread.
 * Caller keeps recipes, spans and byte buffers alive and immutable throughout.
 */
bb_result bb_export_bin(const char *input_utf8, const char *output_utf8,
                        const bb_recipe *registry, size_t registry_count,
                        const char *recipe_id);
#endif
