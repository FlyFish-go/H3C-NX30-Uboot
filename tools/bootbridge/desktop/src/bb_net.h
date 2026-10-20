#ifndef BOOTBRIDGE_NET_H
#define BOOTBRIDGE_NET_H
/* Firmware download network boundary for the desktop GUI.
 *
 * Exactly two HTTPS hosts are ever contacted:
 *   magic.h3c.com                          one POST, official version query
 *   h3c-magic.oss-cn-hangzhou.aliyuncs.com GET of the firmware object
 * No redirects, cookies, authentication, retries, logging or files here.
 * The portable policy helpers (bb_net_policy.c) are shared with host tests;
 * the WinHTTP implementation is bb_net_win.c. */
#include <stddef.h>
#include <stdint.h>

#define BBN_QUERY_HOST "magic.h3c.com"
#define BBN_QUERY_PATH "/smarthomeback/rest/ihomers/getGwDownloadNew"
#define BBN_OBJECT_HOST "h3c-magic.oss-cn-hangzhou.aliyuncs.com"
#define BBN_STATIC_PREFIX "https://" BBN_OBJECT_HOST "/gateway-version/"
#define BBN_QUERY_LIMIT (256U * 1024U)      /* response bytes */
#define BBN_BODY_LIMIT 1024U                /* request body bytes */
#define BBN_DOWNLOAD_LIMIT (128U * 1024U * 1024U)
#define BBN_URL_CAP 8194U                   /* OA_MAX_STRING + 2 */
#define BBN_SERIAL_MIN 6U
#define BBN_SERIAL_MAX 30U

typedef enum {
    BBN_OK = 0, BBN_ARGUMENT, BBN_URL, BBN_LIMIT, BBN_HTTP, BBN_REDIRECT,
    BBN_LENGTH, BBN_ENCODING, BBN_CANCELLED, BBN_TIMEOUT, BBN_TLS,
    BBN_RESOURCE, BBN_NETWORK, BBN_SINK
} bbn_status;

typedef struct {
    bbn_status status;
    unsigned int http_status;
    unsigned long system_error;
    uint64_t bytes; /* bytes accepted into output/sink before return */
} bbn_result;

typedef int (*bbn_cancel_fn)(void *context);                                   /* nonzero cancels */
typedef int (*bbn_sink_fn)(void *context, const unsigned char *data, size_t bytes); /* zero accepts */

/* ---- portable policy (bb_net_policy.c) ---- */

/* 1 only for https://h3c-magic.oss-cn-hangzhou.aliyuncs.com/gateway-version/NAME.bin
 * with NAME in [A-Za-z0-9._-]{1,96}, not starting with '.', and no query. */
int bbn_static_url(const char *url);
/* Static URL as above, or a signed object URL accepted by
 * oa_secure_download_url (exact host, HTTPS output, non-empty query).
 * Writes the HTTPS form to out; on failure out is cleared. */
bbn_status bbn_secure_url(const char *url, char *out, size_t capacity);
/* Splits a bbn_secure_url result into host (NUL terminated) and path+query. */
bbn_status bbn_split_url(const char *secure, char *host, size_t host_capacity, const char **path);
/* Router serial: 6..30 bytes of [0-9A-Za-z-]. */
int bbn_serial_ok(const char *serial);
/* Versions reported to H3C, in order, when querying a pinned target that has
 * no public static URL; NULL past the last attempt or without a query route. */
const char *bbn_query_version(const char *target_version, unsigned attempt);

bbn_status bbn_http_status(unsigned int code);
bbn_status bbn_parse_length(const char *text, size_t length, uint64_t maximum, uint64_t *value);
/* Fails (LENGTH) once current + incoming would pass expected. */
bbn_status bbn_count_bytes(uint64_t current, size_t incoming, uint64_t expected, uint64_t *next);

/* ---- WinHTTP (bb_net_win.c); synchronous, call from a worker thread ---- */

/* POST body to https://magic.h3c.com/smarthomeback/rest/ihomers/getGwDownloadNew.
 * On failure *written = 0 and output is cleared. */
bbn_result bbn_query(const char *body, size_t body_bytes, unsigned char *output,
                     size_t capacity, size_t *written,
                     bbn_cancel_fn cancelled, void *cancel_context);
/* GET a bbn_secure_url object. The response must be 200 with exactly one
 * Content-Length equal to expected_bytes and identity encoding. The sink may
 * have seen partial data on failure. */
bbn_result bbn_download(const char *url, uint64_t expected_bytes,
                        bbn_sink_fn sink, void *sink_context,
                        bbn_cancel_fn cancelled, void *cancel_context);
#endif
