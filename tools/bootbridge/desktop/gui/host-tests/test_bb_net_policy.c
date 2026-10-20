/* Host test: download URL/serial policy and the official query schema. */
#include <stdio.h>
#include <string.h>
#include "../../src/bb_net.h"
#include "../../src/official_api.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define OSS "h3c-magic.oss-cn-hangzhou.aliyuncs.com"

static void static_urls(void)
{
    CHECK(bbn_static_url("https://" OSS "/gateway-version/RT3000V100R015.bin"));
    CHECK(bbn_static_url("https://" OSS "/gateway-version/RC3000V100R013L01.bin"));
    CHECK(bbn_static_url("https://" OSS "/gateway-version/NX30V100R011.bin"));
    CHECK(!bbn_static_url(NULL));
    CHECK(!bbn_static_url(""));
    CHECK(!bbn_static_url("http://" OSS "/gateway-version/RT3000V100R015.bin"));
    CHECK(!bbn_static_url("https://" OSS "/gateway-version/RT3000V100R015.bin?x=1"));
    CHECK(!bbn_static_url("https://" OSS "/gateway-version/RT3000V100R015.bin#x"));
    CHECK(!bbn_static_url("https://" OSS "/gateway-version/.bin"));
    CHECK(!bbn_static_url("https://" OSS "/gateway-version/..bin"));
    CHECK(!bbn_static_url("https://" OSS "/gateway-version/a/b.bin"));
    CHECK(!bbn_static_url("https://" OSS "/gateway-version/a%2fb.bin"));
    CHECK(!bbn_static_url("https://" OSS "/gateway-version/RT3000V100R015.exe"));
    CHECK(!bbn_static_url("https://" OSS ":443/gateway-version/RT3000V100R015.bin"));
    CHECK(!bbn_static_url("https://evil.example/gateway-version/RT3000V100R015.bin"));
    CHECK(!bbn_static_url("https://" OSS ".evil.example/gateway-version/RT3000V100R015.bin"));
    CHECK(!bbn_static_url("https://user@" OSS "/gateway-version/RT3000V100R015.bin"));
    CHECK(!bbn_static_url("https://" OSS "/other/RT3000V100R015.bin"));
    {
        char longest[256], tooLong[256];
        strcpy(longest, "https://" OSS "/gateway-version/");
        strcpy(tooLong, longest);
        memset(longest + strlen(longest), 'A', 96); longest[strlen("https://" OSS "/gateway-version/") + 96] = 0;
        strcat(longest, ".bin");
        memset(tooLong + strlen(tooLong), 'A', 97); tooLong[strlen("https://" OSS "/gateway-version/") + 97] = 0;
        strcat(tooLong, ".bin");
        CHECK(bbn_static_url(longest));
        CHECK(!bbn_static_url(tooLong));
    }
}

static void secure_urls(void)
{
    char out[BBN_URL_CAP], host[64];
    const char *path = NULL;
    CHECK(bbn_secure_url("https://" OSS "/gateway-version/RW3000V100R012.bin", out, sizeof(out)) == BBN_OK);
    CHECK(!strcmp(out, "https://" OSS "/gateway-version/RW3000V100R012.bin"));
    CHECK(bbn_secure_url("http://" OSS "/gateway-version/NX30V100R012.bin?Expires=1&OSSAccessKeyId=a&Signature=b%2B", out, sizeof(out)) == BBN_OK);
    CHECK(!strcmp(out, "https://" OSS "/gateway-version/NX30V100R012.bin?Expires=1&OSSAccessKeyId=a&Signature=b%2B"));
    CHECK(bbn_split_url(out, host, sizeof(host), &path) == BBN_OK);
    CHECK(!strcmp(host, OSS));
    CHECK(path && !strcmp(path, "/gateway-version/NX30V100R012.bin?Expires=1&OSSAccessKeyId=a&Signature=b%2B"));
    CHECK(bbn_secure_url("https://" OSS "/gateway-version/NX30V100R012.img", out, sizeof(out)) == BBN_URL);
    CHECK(out[0] == 0);
    CHECK(bbn_secure_url("https://evil.example/x.bin?Expires=1", out, sizeof(out)) == BBN_URL);
    CHECK(bbn_secure_url("https://" OSS "/x.bin?a=1&amp;b=2", out, sizeof(out)) == BBN_URL);
    CHECK(bbn_secure_url("https://" OSS "/x.bin?a=%2526", out, sizeof(out)) == BBN_URL);
    CHECK(bbn_secure_url("https://" OSS "/x.bin?a=1 b", out, sizeof(out)) == BBN_URL);
    CHECK(bbn_split_url("https://magic.h3c.com/x", host, sizeof(host), &path) == BBN_URL);
    CHECK(path == NULL && host[0] == 0);
}

static void serials_and_routes(void)
{
    CHECK(bbn_serial_ok("TESTSN00000000000012"));
    CHECK(bbn_serial_ok("ABC-12"));
    CHECK(!bbn_serial_ok(NULL));
    CHECK(!bbn_serial_ok("ABC12"));
    CHECK(!bbn_serial_ok("TESTSN0000000000001 "));
    CHECK(!bbn_serial_ok("TESTSN00\"000000"));
    CHECK(!bbn_serial_ok("0123456789012345678901234567890"));
    CHECK(bbn_serial_ok("012345678901234567890123456789"));
    CHECK(bbn_query_version("NX30V100R012", 0) && !strcmp(bbn_query_version("NX30V100R012", 0), "NX30V100R011"));
    CHECK(bbn_query_version("NX30V100R012", 1) && !strcmp(bbn_query_version("NX30V100R012", 1), "NX30V100R012"));
    CHECK(bbn_query_version("NX30V100R012", 2) == NULL);
    CHECK(bbn_query_version("NX30V100R011", 0) == NULL);
    CHECK(bbn_query_version("RT3000V100R015", 0) == NULL);
    CHECK(bbn_query_version(NULL, 0) == NULL);
}

static void framing(void)
{
    uint64_t v = 7, next = 0;
    CHECK(bbn_parse_length("31981568", 8, BBN_DOWNLOAD_LIMIT, &v) == BBN_OK && v == 31981568U);
    CHECK(bbn_parse_length("0", 1, BBN_DOWNLOAD_LIMIT, &v) == BBN_LENGTH && v == 0);
    CHECK(bbn_parse_length("134217729", 9, BBN_DOWNLOAD_LIMIT, &v) == BBN_LENGTH);
    CHECK(bbn_parse_length("134217728", 9, BBN_DOWNLOAD_LIMIT, &v) == BBN_OK);
    CHECK(bbn_parse_length("12a", 3, BBN_DOWNLOAD_LIMIT, &v) == BBN_LENGTH);
    CHECK(bbn_parse_length("+12", 3, BBN_DOWNLOAD_LIMIT, &v) == BBN_LENGTH);
    CHECK(bbn_count_bytes(0, 10, 10, &next) == BBN_OK && next == 10);
    CHECK(bbn_count_bytes(10, 1, 10, &next) == BBN_LENGTH && next == 10);
    CHECK(bbn_count_bytes(11, 0, 10, &next) == BBN_LENGTH);
    CHECK(bbn_http_status(200) == BBN_OK);
    CHECK(bbn_http_status(206) == BBN_HTTP);
    CHECK(bbn_http_status(302) == BBN_REDIRECT);
    CHECK(bbn_http_status(403) == BBN_HTTP);
}

static void official_schema(void)
{
    char body[BBN_BODY_LIMIT];
    size_t written = 0;
    oa_response r;
    static const char ok[] =
        "{\"retCode\":0,\"gwBestVer\":\"NX30V100R012\",\"FileSize\":31981568,"
        "\"VerURL\":\"http://" OSS "/gateway-version/NX30V100R012.bin?Expires=1760000000&OSSAccessKeyId=k&Signature=s%3D\","
        "\"verNotes\":\"x\",\"NextAskTime\":3600}";
    static const char rejected[] = "{\"retCode\":2,\"msg\":\"no\"}";
    static const char wrong_host[] =
        "{\"retCode\":0,\"gwBestVer\":\"NX30V100R012\",\"VerURL\":\"https://example.com/a.bin?x=1\"}";
    CHECK(oa_encode_request("NX30V100R012", "TESTSN00000000000012", 0, body, sizeof(body), &written) == OA_OK);
    CHECK(!strcmp(body, "{\"gwVer\":\"NX30V100R012\",\"gwSn\":\"TESTSN00000000000012\",\"gwAskCount\":0}"));
    CHECK(written == strlen(body));
    CHECK(oa_encode_request("NX30V100R012", "", 0, body, sizeof(body), &written) == OA_IDENTITY_REQUIRED && written == 0);
    CHECK(oa_parse_response(ok, sizeof(ok) - 1, &r) == OA_OK);
    CHECK(!strcmp(r.version, "NX30V100R012") && r.has_file_size && r.file_size == 31981568U);
    CHECK(!strncmp(r.download_url, "https://" OSS "/gateway-version/NX30V100R012.bin?", strlen("https://" OSS "/gateway-version/NX30V100R012.bin?")));
    {
        char out[BBN_URL_CAP];
        CHECK(bbn_secure_url(r.download_url, out, sizeof(out)) == BBN_OK);
    }
    CHECK(oa_parse_response(rejected, sizeof(rejected) - 1, &r) == OA_SERVER_REJECTED && r.ret_code == 2);
    CHECK(oa_parse_response(wrong_host, sizeof(wrong_host) - 1, &r) == OA_URL);
    CHECK(oa_parse_response("{\"retCode\":0,\"retCode\":0}", 25, &r) == OA_DUPLICATE_KEY);
}

int main(void)
{
    static_urls();
    secure_urls();
    serials_and_routes();
    framing();
    official_schema();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("bb_net_policy host test: PASS");
    return 0;
}
