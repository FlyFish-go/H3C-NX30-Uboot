#include "bridge_backup.h"
#include "bridge_sha256.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_BYTES 16384U
#define MANIFEST_BYTES 16384U
#define NAND_128M_BYTES UINT64_C(0x8000000)
#define NAND_256M_BYTES UINT64_C(0x10000000)
#define STARTUP_END UINT64_C(0x900000)
#define SYSTEM_END UINT64_C(0x5900000)

struct record {
    struct bbk_entry entry;
    uint32_t crc, local_offset;
    unsigned char digest[32];
};
struct work {
    struct bbk_io io;
    struct bbk_geometry geometry;
    enum bbk_layout layout;
    struct record records[BBK_MAX_ENTRIES + 1U];
    uint32_t crc_table[256];
    unsigned char chunk[CHUNK_BYTES];
    char manifest[MANIFEST_BYTES];
    size_t count, manifest_used;
    uint64_t position, data_bytes;
};

static void put16(unsigned char *p, uint16_t x) {
    p[0] = (unsigned char)x; p[1] = (unsigned char)(x >> 8);
}
static void put32(unsigned char *p, uint32_t x) {
    put16(p, (uint16_t)x); put16(p + 2, (uint16_t)(x >> 16));
}
static void crc_init(struct work *w) {
    unsigned i, j;
    for (i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (j = 0; j < 8; ++j)
            c = (c >> 1) ^ (UINT32_C(0xedb88320) & (0U - (c & 1U)));
        w->crc_table[i] = c;
    }
}
static uint32_t crc_add(const struct work *w, uint32_t c,
                        const unsigned char *p, size_t n) {
    while (n--) c = w->crc_table[(c ^ *p++) & 255U] ^ (c >> 8);
    return c;
}
static unsigned lower(unsigned x) { return x >= 'A' && x <= 'Z' ? x + 32U : x; }
static int same_name(const char *a, const char *b) {
    while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b)) { ++a; ++b; }
    return *a == *b;
}
static int valid_name(const char name[BBK_NAME_BYTES]) {
    const char *end = memchr(name, 0, BBK_NAME_BYTES);
    size_t i, stem, n;
    char base[BBK_NAME_BYTES];
    if (!end) return 0;
    n = (size_t)(end - name);
    if (n < 5 || memcmp(name + n - 4, ".bin", 4)) return 0;
    stem = n - 4;
    for (i = 0; i < stem; ++i) {
        unsigned c = (unsigned char)name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || (i && (c == '_' || c == '-')))) return 0;
        base[i] = (char)lower(c);
    }
    base[stem] = 0;
    if (!strcmp(base, "con") || !strcmp(base, "prn") || !strcmp(base, "aux") ||
        !strcmp(base, "nul") || (stem == 4 && (!memcmp(base, "com", 3) ||
        !memcmp(base, "lpt", 3)) && base[3] >= '1' && base[3] <= '9')) return 0;
    return 1;
}
static int inside(uint64_t off, uint64_t size, uint64_t begin, uint64_t end) {
    return off >= begin && off < end && size <= end - off;
}
static int allowed(const struct bbk_entry *e) {
    /* Exact current-D physical MIBIB windows. Never use an MTD index/name to
     * infer these extents. A caller cannot grant another range via metadata. */
    static const uint64_t config[][2] = {
        { UINT64_C(0x5900000), UINT64_C(0x5f00000) },
        { UINT64_C(0x5f00000), UINT64_C(0x6500000) },
        { UINT64_C(0x6500000), UINT64_C(0x6600000) },
        { UINT64_C(0x6600000), UINT64_C(0x7d00000) }
    };
    size_t i;
    if (e->role == BBK_STARTUP) return inside(e->offset, e->size, 0, STARTUP_END);
    if (e->role != BBK_OEM_PERSISTENT_DATA) return 0;
    for (i = 0; i < sizeof(config) / sizeof(config[0]); ++i)
        if (inside(e->offset, e->size, config[i][0], config[i][1])) return 1;
    return 0;
}
static int validate(struct work *w, const struct bbk_spec *s) {
    size_t i, j;
    if (!s || !((s->layout == BBK_LAYOUT_H3C_D_128M_DUAL40_V1 &&
                 s->geometry.capacity_bytes == NAND_128M_BYTES) ||
                (s->layout == BBK_LAYOUT_H3C_D_DUAL40_V2 &&
                 (s->geometry.capacity_bytes == NAND_128M_BYTES ||
                  s->geometry.capacity_bytes == NAND_256M_BYTES))) ||
        s->format != BBK_ECC_DECODED_MAIN_NO_OOB || !s->entries ||
        !s->entry_count || s->entry_count > BBK_MAX_ENTRIES) return BBK_INVALID;
    w->geometry = s->geometry;
    w->layout = s->layout;
    if ((w->geometry.capacity_bytes != NAND_128M_BYTES &&
         w->geometry.capacity_bytes != NAND_256M_BYTES) || w->geometry.page_bytes != 2048 ||
        w->geometry.erase_bytes != 131072 ||
        !((w->geometry.physical_spare_bytes == 64 && w->geometry.ecc_bits == 4) ||
          (w->geometry.physical_spare_bytes == 128 &&
           (w->geometry.ecc_bits == 4 || w->geometry.ecc_bits == 8)))) return BBK_INVALID;
    w->count = s->entry_count;
    for (i = 0; i < w->count; ++i) {
        struct bbk_entry *e = &w->records[i].entry;
        *e = s->entries[i];
        if (!valid_name(e->name) || !e->size) return BBK_INVALID;
        if (e->size > BBK_MAX_ENTRY_BYTES || e->size > BBK_MAX_DATA_BYTES - w->data_bytes)
            return BBK_LIMIT;
        w->data_bytes += e->size;
        if (e->offset > w->geometry.capacity_bytes ||
            e->size > w->geometry.capacity_bytes - e->offset ||
            e->offset % w->geometry.page_bytes || e->size % w->geometry.page_bytes ||
            /* Explicitly exclude both adjacent 40 MiB system slots. */
            (e->offset < SYSTEM_END && e->offset + e->size > STARTUP_END) || !allowed(e))
            return BBK_RANGE;
        for (j = 0; j < i; ++j) {
            const struct bbk_entry *p = &w->records[j].entry;
            if (same_name(e->name, p->name)) return BBK_INVALID;
            if (e->offset < p->offset + p->size && p->offset < e->offset + e->size)
                return BBK_RANGE;
        }
    }
    return BBK_OK;
}
static int emit(struct work *w, const void *p, size_t n) {
    if (w->position > UINT32_MAX || n > UINT32_MAX - w->position ||
        w->position > BBK_MAX_ZIP_BYTES || n > BBK_MAX_ZIP_BYTES - w->position)
        return BBK_LIMIT;
    if (w->io.sink(w->io.sink_context, p, n)) return BBK_SINK_FAILED;
    w->position += n;
    return BBK_OK;
}
static int digest_entry(struct work *w, struct record *r, int output) {
    bi_sha256_ctx sha;
    unsigned char digest[32];
    uint32_t crc = UINT32_MAX;
    uint64_t done = 0;
    bi_sha256_init(&sha);
    while (done < r->entry.size) {
        size_t n = r->entry.size - done > CHUNK_BYTES ? CHUNK_BYTES : (size_t)(r->entry.size - done);
        int result;
        if (w->io.read_at(w->io.read_context, r->entry.offset + done, w->chunk, n))
            return BBK_READ_FAILED;
        crc = crc_add(w, crc, w->chunk, n);
        bi_sha256_update(&sha, w->chunk, n);
        if (output && (result = emit(w, w->chunk, n)) != BBK_OK) return result;
        done += n;
    }
    crc ^= UINT32_MAX;
    bi_sha256_final(&sha, digest);
    if (output) {
        if (crc != r->crc || memcmp(digest, r->digest, 32)) return BBK_SOURCE_CHANGED;
    } else {
        r->crc = crc; memcpy(r->digest, digest, 32);
    }
    return BBK_OK;
}
static int append(struct work *w, const char *format, ...) {
    va_list ap;
    int n;
    size_t left = sizeof(w->manifest) - w->manifest_used;
    va_start(ap, format); n = vsnprintf(w->manifest + w->manifest_used, left, format, ap); va_end(ap);
    if (n < 0 || (size_t)n >= left) return BBK_LIMIT;
    w->manifest_used += (size_t)n;
    return BBK_OK;
}
static int make_manifest(struct work *w) {
    size_t i;
    int result;
    struct record *m = &w->records[w->count];
    const char *layout_id = w->layout == BBK_LAYOUT_H3C_D_128M_DUAL40_V1 ?
        "h3c-d-128m-dual40-v1" : "h3c-dual40-v2";
    const char *tail = w->geometry.capacity_bytes == NAND_128M_BYTES ?
        "{\"offset\":131072000,\"size\":3145728}" :
        "{\"status\":\"not-captured-profile-specific\"}";
    if ((result = append(w, "{\"format_version\":1,\"container\":\"zip-store\","
        "\"layout_id\":\"%s\",\"data_format\":\"ecc-decoded-main-area\","
        "\"contains_oob\":false,\"contains_ecc_bytes\":false,\"programmer_image\":false,"
        "\"ecc_scope\":\"read-io-mode-only\",\"preloader_format\":\"unknown\","
        "\"appsbl_storage_format\":\"unknown\",\"restore_format_qualified\":false,"
        "\"sensitive_data\":true,\"restore_api\":false,\"data_bytes\":%" PRIu64 ","
        "\"data_limit_bytes\":%u,\"zip_limit_bytes\":%u,"
        "\"geometry\":{\"capacity_bytes\":%" PRIu64 ",\"page_bytes\":%u,\"erase_bytes\":%u,"
        "\"physical_spare_bytes\":%u,\"ecc_bits\":%u},"
        "\"excluded_system_slots\":[{\"offset\":9437184,\"size\":41943040},"
        "{\"offset\":51380224,\"size\":41943040}],"
        "\"excluded_unpartitioned_tail\":%s,"
        "\"recovery_rule\":\"verify exact entry physical ranges, layout, geometry and storage format before any separately authorized recovery; never treat as raw OOB or whole-chip image\","
        "\"entries\":[", layout_id, w->data_bytes, BBK_MAX_DATA_BYTES, BBK_MAX_ZIP_BYTES,
        w->geometry.capacity_bytes, w->geometry.page_bytes, w->geometry.erase_bytes,
        w->geometry.physical_spare_bytes, w->geometry.ecc_bits, tail)) != BBK_OK) return result;
    for (i = 0; i < w->count; ++i) {
        const struct record *r = &w->records[i];
        char hex[65]; bi_sha256_hex(r->digest, hex);
        if ((result = append(w, "%s{\"name\":\"%s\",\"offset\":%" PRIu64 ",\"size\":%" PRIu64
            ",\"role\":\"%s\",\"crc32\":\"%08" PRIx32 "\",\"sha256\":\"%s\","
            "\"data_format\":\"ecc-decoded-main-area\",\"contains_oob\":false}",
            i ? "," : "", r->entry.name, r->entry.offset, r->entry.size,
            r->entry.role == BBK_STARTUP ? "startup" : "oem-persistent-data", r->crc, hex)) != BBK_OK)
            return result;
    }
    if ((result = append(w, "]}\n")) != BBK_OK) return result;
    memcpy(m->entry.name, "manifest.json", sizeof("manifest.json"));
    m->entry.size = w->manifest_used;
    m->crc = crc_add(w, UINT32_MAX, (const unsigned char *)w->manifest, w->manifest_used) ^ UINT32_MAX;
    return BBK_OK;
}
static int local_header(struct work *w, struct record *r) {
    unsigned char h[30] = {0};
    size_t n = strlen(r->entry.name);
    int result;
    r->local_offset = (uint32_t)w->position;
    put32(h, UINT32_C(0x04034b50)); put16(h + 4, 20);
    put16(h + 12, 33); /* DOS 1980-01-01: deterministic, no user timestamp. */
    put32(h + 14, r->crc); put32(h + 18, (uint32_t)r->entry.size);
    put32(h + 22, (uint32_t)r->entry.size); put16(h + 26, (uint16_t)n);
    if ((result = emit(w, h, sizeof(h))) != BBK_OK) return result;
    return emit(w, r->entry.name, n);
}
static int central_header(struct work *w, const struct record *r) {
    unsigned char h[46] = {0};
    size_t n = strlen(r->entry.name);
    int result;
    put32(h, UINT32_C(0x02014b50)); put16(h + 4, (3U << 8) | 20U); put16(h + 6, 20);
    put16(h + 14, 33); put32(h + 16, r->crc);
    put32(h + 20, (uint32_t)r->entry.size); put32(h + 24, (uint32_t)r->entry.size);
    put16(h + 28, (uint16_t)n); put32(h + 38, UINT32_C(0100600) << 16);
    put32(h + 42, r->local_offset);
    if ((result = emit(w, h, sizeof(h))) != BBK_OK) return result;
    return emit(w, r->entry.name, n);
}
int bbk_write_zip(const struct bbk_spec *spec, const struct bbk_io *io) {
    struct work *w;
    size_t i;
    uint64_t budget = 22, directory;
    int result;
    unsigned char end[22] = {0};
    if (!io || !io->read_at || !io->sink) return BBK_INVALID;
    w = calloc(1, sizeof(*w)); if (!w) return BBK_NO_MEMORY;
    w->io = *io;
    if ((result = validate(w, spec)) != BBK_OK) goto done;
    crc_init(w);
    for (i = 0; i < w->count; ++i)
        if ((result = digest_entry(w, &w->records[i], 0)) != BBK_OK) goto done;
    if ((result = make_manifest(w)) != BBK_OK) goto done;
    for (i = 0; i <= w->count; ++i) {
        uint64_t add = 76U + 2U * strlen(w->records[i].entry.name) + w->records[i].entry.size;
        if (add > UINT32_MAX - budget) { result = BBK_LIMIT; goto done; }
        budget += add;
    }
    if (budget > BBK_MAX_ZIP_BYTES) { result = BBK_LIMIT; goto done; }
    for (i = 0; i <= w->count; ++i) {
        if ((result = local_header(w, &w->records[i])) != BBK_OK) goto done;
        result = i == w->count ? emit(w, w->manifest, w->manifest_used) : digest_entry(w, &w->records[i], 1);
        if (result != BBK_OK) goto done;
    }
    directory = w->position;
    for (i = 0; i <= w->count; ++i)
        if ((result = central_header(w, &w->records[i])) != BBK_OK) goto done;
    put32(end, UINT32_C(0x06054b50)); put16(end + 8, (uint16_t)(w->count + 1U));
    put16(end + 10, (uint16_t)(w->count + 1U)); put32(end + 12, (uint32_t)(w->position - directory));
    put32(end + 16, (uint32_t)directory);
    result = emit(w, end, sizeof(end));
    if (result == BBK_OK && w->position != budget) result = BBK_INVALID;
done:
    /* No logging of data or identifiers; caller owns sensitive temporary IO. */
    free(w);
    return result;
}
