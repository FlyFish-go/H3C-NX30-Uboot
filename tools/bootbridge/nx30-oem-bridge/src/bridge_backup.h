#ifndef BRIDGE_BACKUP_H
#define BRIDGE_BACKUP_H

#include <stddef.h>
#include <stdint.h>

#define BBK_MAX_ENTRIES 32U
#define BBK_NAME_BYTES 64U
#define BBK_MAX_ENTRY_BYTES (32U * 1024U * 1024U)
#define BBK_MAX_DATA_BYTES (64U * 1024U * 1024U)
#define BBK_MAX_ZIP_BYTES (BBK_MAX_DATA_BYTES + 128U * 1024U)

enum bbk_layout {
    BBK_LAYOUT_H3C_D_128M_DUAL40_V1 = 1,
    BBK_LAYOUT_H3C_D_DUAL40_V2 = 2
};
enum bbk_format { BBK_ECC_DECODED_MAIN_NO_OOB = 1 };
enum bbk_role { BBK_STARTUP = 1, BBK_OEM_PERSISTENT_DATA = 2 };
enum bbk_result {
    BBK_OK = 0, BBK_INVALID = -1, BBK_RANGE = -2, BBK_LIMIT = -3,
    BBK_READ_FAILED = -4, BBK_SINK_FAILED = -5, BBK_SOURCE_CHANGED = -6,
    BBK_NO_MEMORY = -7
};

struct bbk_geometry {
    uint64_t capacity_bytes;
    uint32_t page_bytes, erase_bytes, physical_spare_bytes, ecc_bits;
};
struct bbk_entry {
    char name[BBK_NAME_BYTES]; /* ASCII basename ending in .bin; no paths. */
    uint64_t offset, size;
    enum bbk_role role;
};
struct bbk_spec {
    enum bbk_layout layout;
    enum bbk_format format;
    struct bbk_geometry geometry;
    const struct bbk_entry *entries;
    size_t entry_count;
};
/* Return 0 only after exactly size bytes were read/written. No device/path API.
 * read_at offsets are absolute physical main-area byte offsets. The trusted
 * caller must bind these to verified read-only partition descriptors, check
 * geometry/ECC and sufficient RAM/tmp capacity, and serialize its snapshot. */
typedef int (*bbk_read_at_fn)(void *, uint64_t, void *, size_t);
typedef int (*bbk_sink_fn)(void *, const void *, size_t);
struct bbk_io {
    void *read_context;
    bbk_read_at_fn read_at;
    void *sink_context;
    bbk_sink_fn sink;
};
/* Two-pass archive creation; BBK_OK is the only publishable result. On ANY
 * failure discard the entire private temporary output, including bytes already
 * accepted by sink. Never send an HTTP success header before BBK_OK. Data is
 * sensitive even though the manifest carries no SN/MAC/credential metadata.
 * CRC/SHA are integrity checks, not authorization or hardware restore proof.
 * There is intentionally no restore/writeback API. */
int bbk_write_zip(const struct bbk_spec *, const struct bbk_io *);

#endif
