#include <stdio.h>
#include <string.h>
#include <net/httpd_upload_limits.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
 CHECK(httpd_upload_length_allowed("/service/firmware/validate", NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT));
 CHECK(!httpd_upload_length_allowed("/service/firmware/validate", NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT + 1UL));
 CHECK(httpd_upload_length_allowed("/service/firmware/write", NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT));
 CHECK(!httpd_upload_length_allowed("/service/firmware/write", NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT + 1UL));
 CHECK(httpd_upload_length_allowed("/service/uboot/validate", NX30_D_SERVICE_UBOOT_UPLOAD_LIMIT));
 CHECK(!httpd_upload_length_allowed("/service/uboot/validate", NX30_D_SERVICE_UBOOT_UPLOAD_LIMIT + 1UL));
 CHECK(httpd_upload_length_allowed("/service/uboot/write", NX30_D_SERVICE_UBOOT_UPLOAD_LIMIT));
 CHECK(!httpd_upload_length_allowed("/service/uboot/write", NX30_D_SERVICE_UBOOT_UPLOAD_LIMIT + 1UL));
 CHECK(httpd_upload_length_allowed("/service/env/prepare", 2048UL));
 CHECK(!httpd_upload_length_allowed("/service/env/prepare", 2049UL));
 CHECK(httpd_upload_length_allowed("/service/backup", 2048UL));
 CHECK(!httpd_upload_length_allowed("/service/backup", 2049UL));
 CHECK(!httpd_upload_length_allowed("/service/unknown", 16UL));
 CHECK(!httpd_upload_length_allowed("/service/unknown", NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT));
 CHECK(!httpd_upload_length_allowed("/upload", 512UL));
 CHECK(!httpd_upload_length_allowed("/firmware/validate", 512UL));
 CHECK(!httpd_upload_length_allowed("/service-other/route", 16UL));
 puts("HW-D service upload bounds: PASS");
 return 0;
}
