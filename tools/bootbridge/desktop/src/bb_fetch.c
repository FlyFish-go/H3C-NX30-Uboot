#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "bb_fetch.h"
#include "official_api.h"
#include "bridge_sha256.h"

typedef struct {
    const bbf_request *request;
    HANDLE file;
    bi_sha256_ctx sha;
    uint64_t written;
    DWORD error;
} sink_state;

static int cancel_hook(void *context)
{
    const bbf_request *request = (const bbf_request *)context;
    return request->cancelled ? request->cancelled(request->context) : 0;
}

static void progress(const bbf_request *request, int phase, uint64_t done, uint64_t total)
{
    if (request->progress) request->progress(request->context, phase, done, total);
}

static int write_sink(void *context, const unsigned char *data, size_t bytes)
{
    sink_state *s = (sink_state *)context;
    while (bytes) {
        DWORD chunk = bytes > 1048576U ? 1048576U : (DWORD)bytes, done = 0;
        if (!WriteFile(s->file, data, chunk, &done, NULL) || done != chunk) {
            s->error = GetLastError();
            if (!s->error) s->error = ERROR_WRITE_FAULT;
            return 1;
        }
        bi_sha256_update(&s->sha, data, chunk);
        data += chunk; bytes -= chunk; s->written += chunk;
    }
    progress(s->request, BBF_PHASE_DOWNLOAD, s->written, s->request->expected_bytes);
    return 0;
}

static bbf_status from_net(bbn_status status)
{
    switch (status) {
    case BBN_OK: return BBF_OK;
    case BBN_CANCELLED: return BBF_CANCELLED;
    case BBN_TIMEOUT: return BBF_TIMEOUT;
    case BBN_TLS: return BBF_TLS;
    case BBN_HTTP: case BBN_REDIRECT: return BBF_HTTP;
    case BBN_URL: return BBF_URL;
    case BBN_LENGTH: return BBF_SIZE;
    case BBN_LIMIT: case BBN_ENCODING: return BBF_BAD_RESPONSE;
    case BBN_SINK: return BBF_FILE;
    case BBN_ARGUMENT: return BBF_ARGUMENT;
    default: return BBF_NETWORK;
    }
}

bbf_route bbf_route_for(const char *static_url, const char *target_version)
{
    if (static_url && static_url[0]) return bbn_static_url(static_url) ? BBF_ROUTE_STATIC : BBF_ROUTE_NONE;
    return bbn_query_version(target_version, 0) ? BBF_ROUTE_QUERY : BBF_ROUTE_NONE;
}

/* Query getGwDownloadNew with the router SN and the reviewed "from" version
 * and accept only an answer that names the pinned target with its pinned size. */
static bbf_result ask_official(const bbf_request *request, const char *current, char *url, size_t url_capacity)
{
    bbf_result result;
    char body[BBN_BODY_LIMIT];
    size_t body_bytes = 0, got = 0;
    unsigned char *reply = NULL;
    oa_response *parsed = NULL;
    bbn_result net;
    oa_code code;
    memset(&result, 0, sizeof(result));
    memset(url, 0, url_capacity);
    if (!bbn_serial_ok(request->serial)) { result.status = BBF_SERIAL; return result; }
    if (!current) { result.status = BBF_ARGUMENT; return result; }
    if (oa_encode_request(current, request->serial, 0, body, sizeof(body), &body_bytes) != OA_OK) {
        result.status = BBF_SERIAL;
        goto done;
    }
    reply = (unsigned char *)malloc(BBN_QUERY_LIMIT);
    parsed = (oa_response *)malloc(sizeof(*parsed));
    if (!reply || !parsed) { result.status = BBF_NETWORK; result.system_error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    progress(request, BBF_PHASE_QUERY, 0, 0);
    net = bbn_query(body, body_bytes, reply, BBN_QUERY_LIMIT, &got, cancel_hook, (void *)request);
    result.http_status = net.http_status;
    result.system_error = net.system_error;
    if (net.status != BBN_OK) { result.status = from_net(net.status); if (result.status == BBF_SIZE) result.status = BBF_BAD_RESPONSE; goto done; }
    code = oa_parse_response(reply, got, parsed);
    if (code == OA_SERVER_REJECTED) { result.status = BBF_SERVER_REJECTED; result.server_code = (long long)parsed->ret_code; goto done; }
    if (code == OA_URL || code == OA_REDIRECT_REJECTED) { result.status = BBF_URL; goto done; }
    if (code != OA_OK) { result.status = BBF_BAD_RESPONSE; goto done; }
    if (strcmp(parsed->version, request->target_version)) {
        size_t n = strlen(parsed->version);
        if (n >= sizeof(result.offered_version)) n = sizeof(result.offered_version) - 1;
        memcpy(result.offered_version, parsed->version, n);
        result.status = BBF_OTHER_VERSION;
        goto done;
    }
    if (parsed->has_file_size && parsed->file_size != request->expected_bytes) { result.status = BBF_SIZE; goto done; }
    if (bbn_secure_url(parsed->download_url, url, url_capacity) != BBN_OK) { result.status = BBF_URL; goto done; }
    result.status = BBF_OK;
done:
    SecureZeroMemory(body, sizeof(body));
    if (reply) { SecureZeroMemory(reply, BBN_QUERY_LIMIT); free(reply); }
    if (parsed) { SecureZeroMemory(parsed, sizeof(*parsed)); free(parsed); }
    if (result.status != BBF_OK) memset(url, 0, url_capacity);
    return result;
}

static int temporary_path(const wchar_t *destination, wchar_t *out, size_t capacity, HANDLE *file, DWORD *error)
{
    unsigned n;
    *file = INVALID_HANDLE_VALUE;
    for (n = 0; n < 100; n++) {
        int written = n ? swprintf(out, capacity, L"%ls.%u.part", destination, n)
                        : swprintf(out, capacity, L"%ls.part", destination);
        if (written < 0 || (size_t)written >= capacity) { *error = ERROR_FILENAME_EXCED_RANGE; return 0; }
        *file = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (*file != INVALID_HANDLE_VALUE) return 1;
        *error = GetLastError();
        if (*error != ERROR_FILE_EXISTS && *error != ERROR_ALREADY_EXISTS) return 0;
    }
    return 0;
}

bbf_result bbf_fetch(const bbf_request *request)
{
    bbf_result result;
    char *url = NULL;
    wchar_t *part = NULL;
    sink_state sink;
    bbn_result net;
    size_t destination_length;
    int created = 0;
    memset(&result, 0, sizeof(result));
    memset(&sink, 0, sizeof(sink));
    sink.file = INVALID_HANDLE_VALUE;
    if (!request || !request->destination || !request->destination[0] || !request->target_version ||
        !request->expected_bytes || request->expected_bytes > BBN_DOWNLOAD_LIMIT) {
        result.status = BBF_ARGUMENT;
        return result;
    }
    sink.request = request;
    url = (char *)calloc(BBN_URL_CAP, 1);
    destination_length = wcslen(request->destination);
    part = (wchar_t *)calloc(destination_length + 16, sizeof(*part));
    if (!url || !part) { result.status = BBF_FILE; result.system_error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    if (request->route == BBF_ROUTE_STATIC) {
        if (bbf_route_for(request->static_url, request->target_version) != BBF_ROUTE_STATIC ||
            bbn_secure_url(request->static_url, url, BBN_URL_CAP) != BBN_OK) { result.status = BBF_URL; goto done; }
    } else if (request->route == BBF_ROUTE_QUERY) {
        const char *current;
        unsigned attempt;
        result.status = BBF_ARGUMENT;
        /* Retry with the next reported version only when H3C answered but
         * refused or named another version; never after network errors. */
        for (attempt = 0; (current = bbn_query_version(request->target_version, attempt)) != NULL; attempt++) {
            result = ask_official(request, current, url, BBN_URL_CAP);
            if (result.status != BBF_SERVER_REJECTED && result.status != BBF_OTHER_VERSION) break;
        }
        if (result.status != BBF_OK) goto done;
    } else {
        result.status = BBF_ARGUMENT;
        goto done;
    }
    if (cancel_hook((void *)request)) { result.status = BBF_CANCELLED; goto done; }
    if (!temporary_path(request->destination, part, destination_length + 16, &sink.file, &sink.error)) {
        result.status = BBF_FILE; result.system_error = sink.error; goto done;
    }
    created = 1;
    bi_sha256_init(&sink.sha);
    progress(request, BBF_PHASE_DOWNLOAD, 0, request->expected_bytes);
    net = bbn_download(url, request->expected_bytes, write_sink, &sink, cancel_hook, (void *)request);
    SecureZeroMemory(url, BBN_URL_CAP);
    result.http_status = net.http_status;
    result.bytes = sink.written;
    if (net.status != BBN_OK) {
        result.status = from_net(net.status);
        result.system_error = net.status == BBN_SINK ? sink.error : net.system_error;
        goto done;
    }
    progress(request, BBF_PHASE_FINISH, sink.written, request->expected_bytes);
    if (sink.written != request->expected_bytes) { result.status = BBF_SIZE; goto done; }
    {
        unsigned char digest[32];
        bi_sha256_final(&sink.sha, digest);
        if (memcmp(digest, request->expected_sha256, 32)) { result.status = BBF_HASH; goto done; }
    }
    if (!FlushFileBuffers(sink.file)) { result.status = BBF_FILE; result.system_error = GetLastError(); goto done; }
    CloseHandle(sink.file);
    sink.file = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(part, request->destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        result.status = BBF_FILE; result.system_error = GetLastError(); goto done;
    }
    created = 0; /* renamed: nothing left to delete */
    result.status = BBF_OK;
done:
    if (sink.file != INVALID_HANDLE_VALUE) CloseHandle(sink.file);
    /* Only the exclusive temporary file this call created is ever removed. */
    if (created) DeleteFileW(part);
    if (url) { SecureZeroMemory(url, BBN_URL_CAP); free(url); }
    free(part);
    return result;
}
