#include "bb_net.h"
#include "official_api.h"
#include <string.h>

static size_t bounded_length(const char *text, size_t limit)
{
    size_t n = 0;
    while (n < limit && text[n]) n++;
    return n;
}

static int name_char(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '.' || c == '_' || c == '-';
}

int bbn_static_url(const char *url)
{
    static const char prefix[] = BBN_STATIC_PREFIX;
    const size_t p = sizeof(prefix) - 1;
    size_t n, i;
    if (!url) return 0;
    n = bounded_length(url, p + 96 + 4 + 1);
    if (n <= p + 4 || n > p + 96 + 4 || url[n] != 0) return 0;
    if (memcmp(url, prefix, p) || memcmp(url + n - 4, ".bin", 4)) return 0;
    if (url[p] == '.') return 0;
    for (i = p; i < n - 4; i++)
        if (!name_char((unsigned char)url[i])) return 0;
    return 1;
}

bbn_status bbn_secure_url(const char *url, char *out, size_t capacity)
{
    size_t n;
    if (out && capacity) memset(out, 0, capacity);
    if (!url || !out || !capacity) return BBN_ARGUMENT;
    if (bbn_static_url(url)) {
        n = strlen(url);
        if (n + 1 > capacity) return BBN_LIMIT;
        memcpy(out, url, n + 1);
        return BBN_OK;
    }
    if (oa_secure_download_url(url, out, capacity) == OA_OK) return BBN_OK;
    memset(out, 0, capacity);
    return BBN_URL;
}

static int ascii_iequal(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

bbn_status bbn_split_url(const char *secure, char *host, size_t host_capacity, const char **path)
{
    static const char object_host[] = BBN_OBJECT_HOST;
    const char *slash;
    size_t n;
    if (host && host_capacity) memset(host, 0, host_capacity);
    if (path) *path = NULL;
    if (!secure || !host || !host_capacity || !path) return BBN_ARGUMENT;
    if (strncmp(secure, "https://", 8)) return BBN_URL;
    slash = strchr(secure + 8, '/');
    if (!slash) return BBN_URL;
    n = (size_t)(slash - (secure + 8));
    if (n != sizeof(object_host) - 1 || !ascii_iequal(secure + 8, object_host, n)) return BBN_URL;
    if (n + 1 > host_capacity) return BBN_LIMIT;
    memcpy(host, object_host, n + 1);
    *path = slash;
    return BBN_OK;
}

int bbn_serial_ok(const char *serial)
{
    size_t n, i;
    if (!serial) return 0;
    n = bounded_length(serial, BBN_SERIAL_MAX + 1);
    if (n < BBN_SERIAL_MIN || n > BBN_SERIAL_MAX) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)serial[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-'))
            return 0;
    }
    return 1;
}

/* NX30 (RX3000) R012 has no public static object; getGwDownloadNew returns a
 * signed link for a router SN plus the version it reports. The user types
 * only the SN: BootBridge asks as R011 (the release R012 replaces) and, if
 * H3C refuses or offers another version, asks once more as R012. Both
 * returned R012 in live tests. Only an answer naming the pinned target with
 * its pinned size and hash is ever kept. */
static const char *const nx30_r012_from[] = {"NX30V100R011", "NX30V100R012"};

const char *bbn_query_version(const char *target_version, unsigned attempt)
{
    if (target_version && !strcmp(target_version, "NX30V100R012") &&
        attempt < sizeof(nx30_r012_from) / sizeof(nx30_r012_from[0]))
        return nx30_r012_from[attempt];
    return NULL;
}

bbn_status bbn_http_status(unsigned int code)
{
    if (code >= 300 && code < 400) return BBN_REDIRECT;
    return code == 200 ? BBN_OK : BBN_HTTP;
}

bbn_status bbn_parse_length(const char *text, size_t length, uint64_t maximum, uint64_t *value)
{
    uint64_t n = 0;
    size_t i;
    if (value) *value = 0;
    if (!value || !text || !length || !maximum || length > 20) return BBN_LENGTH;
    for (i = 0; i < length; i++) {
        unsigned int digit = (unsigned char)text[i] - '0';
        if (digit > 9 || n > maximum / 10 || (n == maximum / 10 && digit > maximum % 10)) return BBN_LENGTH;
        n = n * 10 + digit;
    }
    if (!n) return BBN_LENGTH;
    *value = n;
    return BBN_OK;
}

bbn_status bbn_count_bytes(uint64_t current, size_t incoming, uint64_t expected, uint64_t *next)
{
    if (!next) return BBN_ARGUMENT;
    *next = current;
    if (current > expected || (uint64_t)incoming > expected - current) return BBN_LENGTH;
    *next = current + (uint64_t)incoming;
    return BBN_OK;
}
