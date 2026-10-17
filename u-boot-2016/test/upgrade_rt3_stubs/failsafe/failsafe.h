#ifndef HOST_FAILSAFE_H
#define HOST_FAILSAFE_H
#include <net/httpd.h>
#include <rt3000_b_metadata.h>
typedef enum { WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE=1, WEBFAILSAFE_UPGRADE_TYPE_UBOOT, WEBFAILSAFE_UPGRADE_TYPE_ART, WEBFAILSAFE_UPGRADE_TYPE_CDT, WEBFAILSAFE_UPGRADE_TYPE_PTABLE, WEBFAILSAFE_UPGRADE_TYPE_SIMG, WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS } upgrade_type_t;
#define RET_SUCCESS 0
#define RET_FAILURE -1
#define RET_UPLOAD_ID_MISMATCH -2
int failsafe_validate_image(upgrade_type_t,const char *,enum rt3_md_target,const void *,unsigned long,struct httpd_response *);
int failsafe_write_image(upgrade_type_t,unsigned long,unsigned long,struct httpd_response *);
#endif
