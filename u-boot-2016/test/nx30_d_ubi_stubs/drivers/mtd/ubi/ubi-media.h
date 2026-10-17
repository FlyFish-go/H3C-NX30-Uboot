#ifndef HOST_UBI_MEDIA_H
#define HOST_UBI_MEDIA_H
#include <stdint.h>
#define UBI_VERSION 1
#define UBI_CRC32_INIT 0xffffffffU
#define UBI_EC_HDR_MAGIC 0x55424923U
#define UBI_VID_HDR_MAGIC 0x55424921U
#define UBI_VID_DYNAMIC 1
#define UBI_VID_STATIC 2
#define UBI_COMPAT_REJECT 5
#define UBI_VOL_NAME_MAX 127
#define UBI_EC_HDR_SIZE 64
#define UBI_VID_HDR_SIZE 64
#endif
