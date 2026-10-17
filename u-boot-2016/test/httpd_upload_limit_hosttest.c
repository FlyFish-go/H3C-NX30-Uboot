#include <stdio.h>
#include <net/httpd_upload_limits.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
 const unsigned long fit_request=3442633UL;
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
 CHECK(httpd_upload_length_allowed("/upload",fit_request));
 CHECK(httpd_upload_length_allowed("/upload",32UL*SZ_1M));
 CHECK(!httpd_upload_length_allowed("/upload",32UL*SZ_1M+1UL));
#elif defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
 CHECK(httpd_upload_length_allowed("/upload",fit_request));
 CHECK(httpd_upload_length_allowed("/upload",16UL*SZ_1M));
 CHECK(httpd_upload_length_allowed("/upload",RT3_HTTP_RECOVERY_UPLOAD_LIMIT));
 CHECK(!httpd_upload_length_allowed("/upload",RT3_HTTP_RECOVERY_UPLOAD_LIMIT+1UL));
#else
 CHECK(!httpd_upload_length_allowed("/upload",fit_request));
 CHECK(httpd_upload_length_allowed("/upload",RT3_HTTP_METADATA_UPLOAD_LIMIT));
 CHECK(!httpd_upload_length_allowed("/upload",RT3_HTTP_METADATA_UPLOAD_LIMIT+1UL));
#endif
 CHECK(!httpd_upload_length_allowed("/firmware/validate",RT3_HTTP_UPLOAD_LIMIT+1UL));
 puts("HTTP Content-Length upload policy: PASS");
 return 0;
}
