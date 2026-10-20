/* WinHTTP implementation of bb_net.h. Derived from the reviewed
 * bootbridge-modifier asynchronous facade: one pending block owned by a
 * reference count, so late WinHTTP callbacks never touch caller memory. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "bb_net.h"

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

#define BBN_CHUNK 65536U
#define BBN_USER_AGENT L"BootBridge/0.4 (official firmware download)"
#define QUERY_DEADLINE_MS 45000U
#define DOWNLOAD_DEADLINE_MS (30U * 60U * 1000U)
#define STALL_MS 60000U

typedef struct {
    HANDLE event;
    volatile LONG references;
    volatile LONG expected, error, received;
    unsigned char buffer[BBN_CHUNK];
    unsigned char body[BBN_BODY_LIMIT];
} pending;

static void release_pending(pending *p)
{
    if (InterlockedDecrement(&p->references) == 0) {
        if (p->event) CloseHandle(p->event);
        SecureZeroMemory(p, sizeof(*p));
        free(p);
    }
}

static void notify_pending(pending *p)
{
    if (!SetEvent(p->event)) InterlockedExchange(&p->error, (LONG)GetLastError());
}

static void CALLBACK completed(HINTERNET handle, DWORD_PTR context, DWORD status,
                               LPVOID information, DWORD bytes)
{
    pending *p = (pending *)context;
    (void)handle;
    if (!p) return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { release_pending(p); return; }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        DWORD error = information && bytes == sizeof(WINHTTP_ASYNC_RESULT)
                          ? ((WINHTTP_ASYNC_RESULT *)information)->dwError
                          : ERROR_WINHTTP_INTERNAL_ERROR;
        if (!error) error = ERROR_WINHTTP_INTERNAL_ERROR;
        InterlockedExchange(&p->error, (LONG)error);
        notify_pending(p);
    } else if (status == (DWORD)InterlockedCompareExchange(&p->expected, 0, 0)) {
        InterlockedExchange(&p->received, (LONG)bytes);
        notify_pending(p);
    }
}

static bbn_status win_error(DWORD error)
{
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT: return BBN_TIMEOUT;
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
    case ERROR_WINHTTP_SECURE_INVALID_CA:
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    case ERROR_WINHTTP_SECURE_INVALID_CERT:
    case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        return BBN_TLS;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return BBN_RESOURCE;
    case ERROR_WINHTTP_OPERATION_CANCELLED:
        return BBN_CANCELLED;
    default:
        return BBN_NETWORK;
    }
}

static ULONGLONG step_deadline(ULONGLONG overall)
{
    ULONGLONG stall = GetTickCount64() + STALL_MS;
    return stall < overall ? stall : overall;
}

static bbn_status interrupted(bbn_cancel_fn cancel, void *context, ULONGLONG deadline)
{
    if (cancel && cancel(context)) return BBN_CANCELLED;
    return GetTickCount64() >= deadline ? BBN_TIMEOUT : BBN_OK;
}

static int prepare(pending *p, DWORD expected)
{
    if (!ResetEvent(p->event)) return 0;
    InterlockedExchange(&p->error, 0);
    InterlockedExchange(&p->received, 0);
    InterlockedExchange(&p->expected, (LONG)expected);
    return 1;
}

static bbn_status wait_pending(pending *p, ULONGLONG overall, bbn_cancel_fn cancel,
                               void *context, DWORD *error)
{
    ULONGLONG deadline = step_deadline(overall);
    for (;;) {
        bbn_status s = interrupted(cancel, context, deadline);
        DWORD wait;
        if (s != BBN_OK) return s;
        *error = (DWORD)InterlockedCompareExchange(&p->error, 0, 0);
        if (*error) return win_error(*error);
        wait = WaitForSingleObject(p->event, 100);
        if (wait == WAIT_OBJECT_0) {
            *error = (DWORD)InterlockedCompareExchange(&p->error, 0, 0);
            return *error ? win_error(*error) : BBN_OK;
        }
        if (wait != WAIT_TIMEOUT) { *error = GetLastError(); return BBN_RESOURCE; }
    }
}

/* 0 absent, 1 single bounded value, -1 failure/duplicate/oversize. */
static int one_header(HINTERNET request, DWORD which, wchar_t *out, DWORD capacity, DWORD *error)
{
    DWORD size = capacity * sizeof(*out), index = 0;
    wchar_t duplicate[2];
    DWORD other = sizeof(duplicate);
    if (!WinHttpQueryHeaders(request, which, WINHTTP_HEADER_NAME_BY_INDEX, out, &size, &index)) {
        DWORD e = GetLastError();
        if (e == ERROR_WINHTTP_HEADER_NOT_FOUND) return 0;
        *error = e;
        return -1;
    }
    if (WinHttpQueryHeaders(request, which, WINHTTP_HEADER_NAME_BY_INDEX, duplicate, &other, &index) ||
        GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) {
        *error = ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
        return -1;
    }
    return 1;
}

static bbn_status response_framing(HINTERNET request, int query, uint64_t maximum,
                                   int *has_length, uint64_t *length, DWORD *error)
{
    wchar_t header[128];
    char ascii[128];
    int found;
    size_t i;
    *has_length = 0; *length = 0;
    found = one_header(request, WINHTTP_QUERY_CONTENT_ENCODING, header, 128, error);
    if (found < 0) return BBN_ENCODING;
    if (found && _wcsicmp(header, L"identity")) return BBN_ENCODING;
    found = one_header(request, WINHTTP_QUERY_CONTENT_LENGTH, header, 128, error);
    if (found < 0) return BBN_LENGTH;
    if (found) {
        for (i = 0; header[i] && i < 127; i++) {
            if (header[i] > 127) return BBN_LENGTH;
            ascii[i] = (char)header[i];
        }
        ascii[i] = 0;
        if (bbn_parse_length(ascii, i, maximum, length) != BBN_OK) return BBN_LENGTH;
        *has_length = 1;
    }
    found = one_header(request, WINHTTP_QUERY_TRANSFER_ENCODING, header, 128, error);
    if (found < 0) return BBN_LENGTH;
    if (found && (*has_length || !query || _wcsicmp(header, L"chunked"))) return BBN_LENGTH;
    if (!query && !*has_length) return BBN_LENGTH;
    return BBN_OK;
}

static HINTERNET open_session(DWORD *error)
{
    HINTERNET session = WinHttpOpen(BBN_USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!session) /* Windows 7/8: no automatic proxy access type */
        session = WinHttpOpen(BBN_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!session) *error = GetLastError();
    return session;
}

/* TLS 1.2 is required; 1.3 is added where WinHTTP knows it. Windows 7
 * without KB3140245 rejects the TLS 1.2 flag: report that as TLS. */
static int secure_protocols(HINTERNET session, DWORD *error)
{
    DWORD value = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &value, sizeof(value))) return 1;
    value = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &value, sizeof(value))) return 1;
    *error = GetLastError();
    return 0;
}

static bbn_result transfer(const wchar_t *host, const wchar_t *object, const char *body, size_t body_bytes,
                           unsigned char *out, size_t capacity, bbn_sink_fn sink, void *sink_context,
                           uint64_t maximum, uint64_t expected_length,
                           bbn_cancel_fn cancel, void *cancel_context)
{
    bbn_result result = {BBN_NETWORK, 0, 0, 0};
    HINTERNET session = NULL, connection = NULL, request = NULL;
    pending *p = NULL;
    int has_length = 0, query = body != NULL;
    uint64_t length = 0;
    DWORD error = 0, value;
    BOOL issued;
    ULONGLONG deadline = GetTickCount64() + (query ? QUERY_DEADLINE_MS : DOWNLOAD_DEADLINE_MS);
    LPCWSTR types[] = {query ? L"application/json" : L"application/octet-stream", NULL};
#define FAIL_WIN() do { error = GetLastError(); result.status = win_error(error); goto done; } while (0)
#define CHECK_CANCEL() do { result.status = interrupted(cancel, cancel_context, deadline); if (result.status != BBN_OK) goto done; } while (0)
#define PREPARE(v) do { if (!prepare(p, (v))) { error = GetLastError(); result.status = BBN_RESOURCE; goto done; } } while (0)
#define WAIT_FOR() do { if (!issued && GetLastError() != ERROR_IO_PENDING) FAIL_WIN(); \
        result.status = wait_pending(p, deadline, cancel, cancel_context, &error); \
        if (result.status != BBN_OK) goto done; \
        CHECK_CANCEL(); } while (0)
    CHECK_CANCEL();
    if (body_bytes > BBN_BODY_LIMIT) { result.status = BBN_ARGUMENT; goto done; }
    p = (pending *)calloc(1, sizeof(*p));
    if (!p) { result.status = BBN_RESOURCE; error = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    p->references = 1;
    p->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!p->event) { error = GetLastError(); result.status = BBN_RESOURCE; goto done; }
    if (body_bytes) memcpy(p->body, body, body_bytes);
    session = open_session(&error);
    if (!session) { result.status = win_error(error); goto done; }
    if (!secure_protocols(session, &error)) { result.status = BBN_TLS; goto done; }
    if (!WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000)) FAIL_WIN();
    connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) FAIL_WIN();
    request = WinHttpOpenRequest(connection, query ? L"POST" : L"GET", object, NULL, WINHTTP_NO_REFERER, types,
                                 WINHTTP_FLAG_SECURE | WINHTTP_FLAG_ESCAPE_DISABLE | WINHTTP_FLAG_ESCAPE_DISABLE_QUERY);
    if (!request) FAIL_WIN();
    value = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &value, sizeof(value))) FAIL_WIN();
    value = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &value, sizeof(value))) FAIL_WIN();
    value = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &value, sizeof(value))) FAIL_WIN();
    value = 32768U;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &value, sizeof(value))) FAIL_WIN();
    {
        DWORD_PTR context = (DWORD_PTR)p;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) FAIL_WIN();
    }
    InterlockedIncrement(&p->references);
    if (WinHttpSetStatusCallback(request, completed,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        DWORD callback_error = GetLastError();
        release_pending(p);
        SetLastError(callback_error);
        FAIL_WIN();
    }
    CHECK_CANCEL();
    PREPARE(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    /* Signed object URLs cover Content-Type: a GET sends no Content-Type. */
    issued = WinHttpSendRequest(request,
                                query ? L"Content-Type: application/json\r\nAccept-Encoding: identity\r\n"
                                      : L"Accept-Encoding: identity\r\n",
                                (DWORD)-1, body_bytes ? p->body : WINHTTP_NO_REQUEST_DATA,
                                (DWORD)body_bytes, (DWORD)body_bytes, (DWORD_PTR)p);
    WAIT_FOR();
    PREPARE(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    issued = WinHttpReceiveResponse(request, NULL);
    WAIT_FOR();
    {
        DWORD size = sizeof(value);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &value, &size, WINHTTP_NO_HEADER_INDEX)) FAIL_WIN();
    }
    result.http_status = value;
    result.status = bbn_http_status(value);
    if (result.status != BBN_OK) goto done;
    result.status = response_framing(request, query, maximum, &has_length, &length, &error);
    if (result.status != BBN_OK) goto done;
    if (!query && length != expected_length) { result.status = BBN_LENGTH; goto done; }
    for (;;) {
        uint64_t next;
        DWORD received;
        CHECK_CANCEL();
        PREPARE(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        issued = WinHttpReadData(request, p->buffer, BBN_CHUNK, NULL);
        WAIT_FOR();
        received = (DWORD)InterlockedCompareExchange(&p->received, 0, 0);
        if (!received) break;
        result.status = bbn_count_bytes(result.bytes, received, has_length ? length : maximum, &next);
        if (result.status != BBN_OK) { if (!has_length) result.status = BBN_LIMIT; goto done; }
        if (out) {
            if (next > capacity) { result.status = BBN_LIMIT; goto done; }
            memcpy(out + (size_t)result.bytes, p->buffer, received);
        } else if (sink(sink_context, p->buffer, received)) {
            result.status = BBN_SINK;
            goto done;
        }
        result.bytes = next;
    }
    result.status = has_length && result.bytes != length ? BBN_LENGTH : BBN_OK;
done:
    if (request && !WinHttpCloseHandle(request) && result.status == BBN_OK) { error = GetLastError(); result.status = BBN_RESOURCE; }
    if (connection && !WinHttpCloseHandle(connection) && result.status == BBN_OK) { error = GetLastError(); result.status = BBN_RESOURCE; }
    if (session && !WinHttpCloseHandle(session) && result.status == BBN_OK) { error = GetLastError(); result.status = BBN_RESOURCE; }
    result.system_error = error;
    /* Closing callbacks may still arrive; their reference owns the buffers. */
    if (p) release_pending(p);
    return result;
#undef FAIL_WIN
#undef CHECK_CANCEL
#undef PREPARE
#undef WAIT_FOR
}

bbn_result bbn_query(const char *body, size_t length, unsigned char *out, size_t capacity, size_t *written,
                     bbn_cancel_fn cancel, void *context)
{
    bbn_result result = {BBN_ARGUMENT, 0, 0, 0};
    if (written) *written = 0;
    if (out && capacity && capacity <= BBN_QUERY_LIMIT) memset(out, 0, capacity);
    if (!body || !length || length > BBN_BODY_LIMIT || !out || !capacity ||
        capacity > BBN_QUERY_LIMIT || !written) return result;
    result = transfer(L"" BBN_QUERY_HOST, L"" BBN_QUERY_PATH, body, length,
                      out, capacity, NULL, NULL, capacity, 0, cancel, context);
    if (result.status == BBN_OK) *written = (size_t)result.bytes;
    else SecureZeroMemory(out, capacity);
    return result;
}

bbn_result bbn_download(const char *url, uint64_t expected_bytes, bbn_sink_fn sink, void *sink_context,
                        bbn_cancel_fn cancel, void *context)
{
    char secure[BBN_URL_CAP], host[64];
    wchar_t whost[64], *object = NULL;
    const char *path = NULL;
    size_t i, n = 0;
    bbn_result result = {BBN_ARGUMENT, 0, 0, 0};
    if (!url || !sink || !expected_bytes || expected_bytes > BBN_DOWNLOAD_LIMIT) return result;
    if (bbn_secure_url(url, secure, sizeof(secure)) != BBN_OK ||
        bbn_split_url(secure, host, sizeof(host), &path) != BBN_OK) {
        result.status = BBN_URL;
        goto done;
    }
    n = strlen(path);
    object = (wchar_t *)calloc(n + 1, sizeof(*object));
    if (!object) { result.status = BBN_RESOURCE; goto done; }
    for (i = 0; host[i]; i++) whost[i] = (unsigned char)host[i];
    whost[i] = 0;
    for (i = 0; i < n; i++) object[i] = (unsigned char)path[i];
    result = transfer(whost, object, NULL, 0, NULL, 0, sink, sink_context,
                      expected_bytes, expected_bytes, cancel, context);
done:
    if (object) { SecureZeroMemory(object, (n + 1) * sizeof(*object)); free(object); }
    SecureZeroMemory(secure, sizeof(secure));
    return result;
}
