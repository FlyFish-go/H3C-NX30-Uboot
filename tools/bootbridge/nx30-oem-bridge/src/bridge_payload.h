#ifndef NX30_BRIDGE_PAYLOAD_H
#define NX30_BRIDGE_PAYLOAD_H
#include "bridge_version.h"
/* Bound to the offline, OEM-layout-audited fixed-address D derivative of RC1.
 * This is a profile whitelist, not a model-wide hardware compatibility claim. */
#define BI_PROFILE "NX30-D-BRIDGE-INSTALL1"
#define BI_IP "192.168.10.1"
#define BI_PAYLOAD_PATH "/etc/bridge/uboot.bin"
#define BI_PAYLOAD_BYTES 1310720U
#define BI_PAYLOAD_SHA256 "c7f9ee8d82cffe86c7bbd896e6f6d063c5658abf079d19fbb962341dd2b84246"
#define BI_PAYLOAD_VERSION "h3c-common4-bridge10-97f591cfc92c"
#define BI_SBL_VALID_BYTES 178176U
#define BI_SBL_SHA256 "c8bc5e1d35a3092dc1292d1b8ba13061082e08992a2cb07c05dbf7561a1244b5"
/* The template has no invented sequence for an historical lab payload.
 * The builder emits a registry only from explicit reviewed release identity. */
/* BEGIN TRUSTED VERSION REGISTRY */
#define BI_VERSION_SCOPE ""
#define BI_VERSION_REGISTRY NULL
#define BI_VERSION_REGISTRY_COUNT 0U
/* END TRUSTED VERSION REGISTRY */
#endif
