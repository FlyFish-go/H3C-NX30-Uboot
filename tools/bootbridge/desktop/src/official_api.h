#ifndef BOOTBRIDGE_OFFICIAL_API_H
#define BOOTBRIDGE_OFFICIAL_API_H
#include <stddef.h>
#include <stdint.h>
#define OA_MAX_RESPONSE (2u*1024u*1024u)
#define OA_MAX_STRING 8192u
#define OA_MAX_VERSION 127u
#define OA_MAX_IDENTITY 30u /* UTF-8 bytes, excludes NUL */
#define OA_MAX_DOWNLOAD (128u*1024u*1024u)
#define OA_MAX_DEPTH 16u
#define OA_MAX_FIELDS 64u
#define OA_MAX_TOTAL_FIELDS 4096u
#define OA_MAX_VALUES 8192u
#define OA_MAX_KEY 128u
/* Every char buffer capacity includes its trailing NUL. No network, files,
 * logging, allocation or runtime product/version qualification is performed.
 * Pass buffers owned by one caller; input/output buffers must not overlap. */
typedef enum {
    OA_OK=0, OA_ARGUMENT, OA_LIMIT, OA_JSON, OA_UTF8, OA_DUPLICATE_KEY,
    OA_NUMBER_RANGE, OA_SCHEMA, OA_SERVER_REJECTED, OA_IDENTITY_REQUIRED,
    OA_BUFFER, OA_URL, OA_REDIRECT_REJECTED
} oa_code;
typedef struct {
    int64_t ret_code;
    int has_file_size, has_unknown_fields;
    uint64_t file_size;
    char version[OA_MAX_VERSION+1u]; /* opaque data, never HTML */
    char download_url[OA_MAX_STRING+2u]; /* HTTPS, original path/query */
} oa_response;
/* Strict complete UTF-8 JSON root object. Unknown nested values are fully
 * checked, including duplicate decoded object keys. Numbers: signed 64-bit
 * integers, finite decimal floats; token <=127 bytes, exponent abs<=10000.
 * Floats only allowed in ignored metadata, never retCode/FileSize.
 * A nonzero retCode returns SERVER_REJECTED with only ret_code and extension
 * flag retained. Every other failure clears the complete result.
 * Success means candidate metadata parsed, never authentic/adapted/bootable. */
oa_code oa_parse_response(const void *json, size_t length, oa_response *result);
/* The legacy schema is statically traced in pinned NX30/RC/RT/RW clients.
 * Version is opaque and upper layers must choose a compiled reviewed route;
 * no RX, no-SN or retailmodel route is implied. Missing SN returns
 * IDENTITY_REQUIRED for this schema only, not for every H3C API.
 * Values must be strict UTF-8 and have no C0/C1 control code points. ask_count<=1000000.
 * On failure the entire output buffer is zeroed and *written=0; no identity is recorded by this module. */
oa_code oa_encode_request(const char *gw_version, const char *gw_serial,
                          uint32_t ask_count, char *out, size_t capacity,
                          size_t *written);
/* Exact allowlisted ASCII host, case insensitive; no port, userinfo, fragment,
 * control, backslash, empty path/query, bad percent escape or HTML ampersand.
 * Accept HTTP/HTTPS source; output uses HTTPS. Both raw and normalized URL
 * lengths must be <=OA_MAX_STRING bytes, excluding NUL; HTTP therefore has
 * one fewer usable source byte. Raw path/query are preserved. Revalidation
 * of a successful normalized output is idempotent with sufficient capacity.
 * Reject encoded separators at %25(26|3d), including nested %25 wrappers.
 * No signature verification is performed. On failure clears the entire output buffer. */
oa_code oa_secure_download_url(const char *raw, char *out, size_t capacity);
/* All redirect statuses 300..399 are rejected; others return OK. Callers must
 * separately enforce query=200, probe=206, full-download=200 contracts. */
oa_code oa_check_redirect(unsigned http_status);
#endif
