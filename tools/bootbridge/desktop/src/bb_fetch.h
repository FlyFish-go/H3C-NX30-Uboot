#ifndef BOOTBRIDGE_FETCH_H
#define BOOTBRIDGE_FETCH_H
/* Download one pinned official firmware image to a user-chosen path.
 * Synchronous; run it on a worker thread. The file appears at destination
 * only after its size and SHA-256 equal the pinned values; otherwise the
 * exclusive temporary file is deleted. */
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include "bb_net.h"

typedef enum { BBF_ROUTE_NONE = 0, BBF_ROUTE_STATIC, BBF_ROUTE_QUERY } bbf_route;

typedef enum {
    BBF_OK = 0, BBF_ARGUMENT, BBF_SERIAL, BBF_CANCELLED, BBF_NETWORK, BBF_TLS,
    BBF_TIMEOUT, BBF_HTTP, BBF_SERVER_REJECTED, BBF_BAD_RESPONSE, BBF_URL,
    BBF_OTHER_VERSION, BBF_SIZE, BBF_HASH, BBF_FILE
} bbf_status;

enum { BBF_PHASE_QUERY = 1, BBF_PHASE_DOWNLOAD = 2, BBF_PHASE_FINISH = 3 };

typedef struct {
    bbf_route route;
    const char *static_url;       /* STATIC: bbn_static_url form */
    const char *serial;           /* QUERY: router SN; caller wipes it */
    const char *target_version;   /* the pinned version, e.g. NX30V100R012 */
    uint64_t expected_bytes;
    unsigned char expected_sha256[32];
    const wchar_t *destination;   /* absolute path chosen by the user */
    int (*cancelled)(void *context);
    void (*progress)(void *context, int phase, uint64_t done, uint64_t total);
    void *context;
} bbf_request;

typedef struct {
    bbf_status status;
    unsigned int http_status;
    unsigned long system_error;
    long long server_code;        /* SERVER_REJECTED: retCode */
    char offered_version[128];    /* OTHER_VERSION: what the server offered */
    uint64_t bytes;
} bbf_result;

/* STATIC when the pinned model has a reviewed public URL, QUERY when
 * bbn_query_version names a "from" version for the target, else NONE. */
bbf_route bbf_route_for(const char *static_url, const char *target_version);
bbf_result bbf_fetch(const bbf_request *request);
#endif
