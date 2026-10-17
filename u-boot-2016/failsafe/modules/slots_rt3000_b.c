// SPDX-License-Identifier: GPL-2.0
/* RT3000-B read-only slot identity summary; classification is content-only. */
#include <common.h>
#include <exports.h>
#include <environment.h>
#include <linux/crc32.h>
#include <asm/errno.h>
#include <linux/mtd/partitions.h>
#include <jffs2/load_kernel.h>
#include <nand.h>
#include <net/httpd.h>
#include <ubi_uboot.h>
#include <squashfs_release.h>
#include <u-boot/sha256.h>
#include <asm/arch-qca-common/smem.h>
#include <rt3000_b_layout.h>
#include "modules.h"

DECLARE_GLOBAL_DATA_PTR;

extern long long ubi_get_volume_size(char *volume);

#define SLOT_N 2
#define HASH_MAX 0x02000000U
#define HASH_ADDR 0x42000000UL
#define HASH_END (HASH_ADDR + HASH_MAX)
#define SB_SIZE 96U
#define SQUASH_MAGIC 0x73717368U

struct slot_info {
 u64 offset, size;
 unsigned int display_index;
 char label[96], part_name[64];
 char status[16], type[16], version[97], detection[40], error[48];
};
struct known_image {
 const char *sha256;
 const char *version;
 const char *label;
};
/* Catalog hashes cover only ubi_rootfs SquashFS bytes_used. */
#include "slots_oem_catalog.h"
static const char *known_qsdk = "31115a4c01c29357f6ed1e27a1f5007ef6e25904342054be48542610c5605f74";
static struct slot_info slots[SLOT_N];
static char slots_json[4096];
static char escaped_part[SLOT_N][128];
static char escaped_label[SLOT_N][128];
static char escaped_version[SLOT_N][256];
#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || (defined(CONFIG_H3C_HW_D_RAM_READONLY) && !defined(CONFIG_H3C_HW_D_PRODUCTION_PORT))
#define SLOT_ACCESS_JSON "\"read_only\":true,\"boot_supported\":false,"
#else
#define SLOT_ACCESS_JSON "\"read_only\":false,\"boot_supported\":true,"
#endif
static int slots_ready;
static struct rt3_layout scanned_layout;
static char *env_snapshot;
static ssize_t env_snapshot_len;
static u32 env_snapshot_crc;
static int environment_restored = 1;

static u32 get_le32(const u8 *p)
{
 return p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u64 get_le64(const u8 *p)
{
 return get_le32(p) | ((u64)get_le32(p + 4) << 32);
}
static int squashfs_size(const u8 *sb, u64 volume_size, u64 *used)
{
 if (get_le32(sb) != SQUASH_MAGIC) return -EINVAL;
 *used = get_le64(sb + 40);
 if (get_le32(sb + 12) < 4096 || get_le32(sb + 12) > 1048576 ||
     sb[28] != 4 || sb[30] != 0 || *used < SB_SIZE ||
     *used > HASH_MAX || *used > volume_size) return -EFBIG;
 return 0;
}
static void hex_digest(const u8 digest[32], char out[65])
{
 static const char h[] = "0123456789abcdef";
 int i;
 for (i = 0; i < 32; i++) { out[2*i] = h[digest[i] >> 4]; out[2*i+1] = h[digest[i] & 15]; }
 out[64] = 0;
}
/* Return 1 when the hash is in the OEM catalog (including ambiguous entries). */
static int classify_oem(const char *hash, struct slot_info *s,
                        const struct known_image *catalog, unsigned int count,
                        const char *qsdk_hash)
{
 const struct known_image *match = NULL;
 unsigned int i;
 int found = 0, ambiguous = 0;

 for (i = 0; i < count; i++) {
  if (strcmp(hash, catalog[i].sha256)) continue;
  if (!match) match = &catalog[i];
  else if (strcmp(match->version, catalog[i].version) ||
           strcmp(match->label, catalog[i].label)) ambiguous = 1;
  found++;
 }
 if (!found) return 0;
 if (ambiguous || !strcmp(hash, qsdk_hash)) {
  strlcpy(s->label, "Ambiguous OEM image", sizeof(s->label));
  strlcpy(s->detection, "ambiguous_oem_rootfs_sha256", sizeof(s->detection));
  return 1;
 }
 strlcpy(s->status, "known", sizeof(s->status));
 strlcpy(s->type, "OEM", sizeof(s->type));
 strlcpy(s->version, match->version, sizeof(s->version));
 strlcpy(s->label, match->label, sizeof(s->label));
 strlcpy(s->detection, "known_rootfs_sha256", sizeof(s->detection));
 return 1;
}
#ifdef CONFIG_H3C_HW_D_RAM_WEB
#include "slots_nx30_d_oem.h"
#endif
static void classify(const char *hash, struct slot_info *s)
{
 strlcpy(s->status, "unknown", sizeof(s->status));
 strlcpy(s->type, "unknown", sizeof(s->type));
 strlcpy(s->version, "unknown", sizeof(s->version));
 strlcpy(s->label, "Unknown system", sizeof(s->label));
 strlcpy(s->detection, "unknown_rootfs_sha256", sizeof(s->detection));
 #ifdef CONFIG_H3C_HW_D_RAM_WEB
 if (classify_oem(hash, s, known_nx30_d_oem, ARRAY_SIZE(known_nx30_d_oem), known_qsdk)) return;
 #endif
 if (classify_oem(hash, s, known_oem, ARRAY_SIZE(known_oem), known_qsdk))
  return;
 if (!strcmp(hash, known_qsdk)) {
  strlcpy(s->status, "known", sizeof(s->status));
  strlcpy(s->type, "QSDK", sizeof(s->type));
  strlcpy(s->version, "Product R1 Upgrade Preview", sizeof(s->version));
  strlcpy(s->label, "QSDK Product R1 Upgrade Preview", sizeof(s->label));
  strlcpy(s->detection, "known_rootfs_sha256", sizeof(s->detection));
 }
}
static void classify_release_file(struct slot_info *s, const char *version)
{
 strlcpy(s->status, "known", sizeof(s->status));
 if (!strncmp(version, "OpenWrt ", 8))
  strlcpy(s->type, "OpenWrt", sizeof(s->type));
 else if (!strncmp(version, "ImmortalWrt ", 12))
  strlcpy(s->type, "ImmortalWrt", sizeof(s->type));
 else if (!strncmp(version, "LEDE ", 5))
  strlcpy(s->type, "LEDE", sizeof(s->type));
 else
  strlcpy(s->type, "ThirdParty", sizeof(s->type));
 strlcpy(s->version, version, sizeof(s->version));
 strlcpy(s->label, "Third-party system", sizeof(s->label));
 strlcpy(s->detection, "rootfs_release_file", sizeof(s->detection));
}
/* hexport allocates the used environment size, independent of env_t size. */
static int env_save(void)
{
	ssize_t len;

	if (env_snapshot)
		return -EBUSY;
	len = hexport_r(&env_htab, '\0', 0, &env_snapshot, 0, 0, NULL);
	if (len <= 0 || !env_snapshot) {
		free(env_snapshot);
		env_snapshot = NULL;
		return -EIO;
	}
	env_snapshot_len = len;
	env_snapshot_crc = crc32(0, (const unsigned char *)env_snapshot,
				 env_snapshot_len);
	return 0;
}

static int env_restore(void)
{
	char *verify = NULL;
	ssize_t verify_len;
	int ret;

	if (!env_snapshot || env_snapshot_len <= 0)
		return -EINVAL;
	ret = himport_r(&env_htab, env_snapshot, env_snapshot_len, '\0',
			0, 0, 0, NULL);
	if (!ret)
		return -EIO;
	verify_len = hexport_r(&env_htab, '\0', 0, &verify, 0, 0, NULL);
	if (verify_len < 0 || !verify) {
		free(verify);
		return -EIO;
	}
	if (verify_len != env_snapshot_len ||
	    crc32(0, (const unsigned char *)verify, verify_len) !=
	    env_snapshot_crc ||
	    memcmp(verify, env_snapshot, env_snapshot_len)) {
		free(verify);
		return -EIO;
	}
	free(verify);
	free(env_snapshot);
	env_snapshot = NULL;
	env_snapshot_len = 0;
	env_snapshot_crc = 0;
	return 0;
}

static void set_error(struct slot_info *s, const char *e)
{
 strlcpy(s->status, "unknown", sizeof(s->status));
 strlcpy(s->type, "unknown", sizeof(s->type));
 strlcpy(s->version, "unknown", sizeof(s->version));
 strlcpy(s->label, "Unknown system", sizeof(s->label));
 strlcpy(s->detection, "unknown_rootfs_sha256", sizeof(s->detection));
 strlcpy(s->error, e, sizeof(s->error));
}
static int set_scan_mtd(const struct slot_info *s)
{
 char mtdparts[112];
 int n;

 if (strcmp(s->part_name, "rootfs") && strcmp(s->part_name, "rootfs_1"))
  return -EINVAL;
 if (!s->size)
  return -EINVAL;
 if (setenv("mtdids", "nand0=nand0"))
  return -EIO;
 n = snprintf(mtdparts, sizeof(mtdparts),
              "mtdparts=nand0:0x%llx@0x%llx(%s)",
              (unsigned long long)s->size,
              (unsigned long long)s->offset, s->part_name);
 if (n < 0 || (size_t)n >= sizeof(mtdparts))
  return -ENOSPC;
 if (setenv("mtdparts", mtdparts))
  return -EIO;
 return 0;
}
static int read_rootfs(struct slot_info *s)
{
 struct ubi_device *ubi;
 struct ubi_volume *v;
 u8 digest[32];
 char hex[65], release[97];
 u64 used;
 long long signed_vsize;
 u64 vsize;
 int i, found = 0, malformed = 0, ret, ubi_attempted = 0;
 ulong addr = HASH_ADDR;

 if (addr < CONFIG_SYS_SDRAM_BASE || HASH_END < addr ||
     HASH_END - CONFIG_SYS_SDRAM_BASE > gd->ram_size ||
     HASH_END > 0x44000000UL) return -ERANGE;
 ret = env_save();
 if (ret) {
  environment_restored = 0;
  return ret;
 }
 ret = set_scan_mtd(s);
 if (ret) goto restore_env;
 ubi_attempted = 1;
 ret = ubi_part_readonly(s->part_name, NULL);
 if (ret) { ret = -ENODEV; goto detach; }
 ubi = ubi_devices[0];
 if (!ubi) { ret = -ENODEV; goto detach; }
 for (i = 0; i < ubi->vtbl_slots + 1; i++) {
  v = ubi->volumes[i];
  /* Other OEM volumes, notably wifi_fw, may also contain SquashFS. */
  if (!v || strcmp(v->name, "ubi_rootfs") || v->used_bytes < SB_SIZE) continue;
  signed_vsize = ubi_get_volume_size(v->name);
  if (signed_vsize < SB_SIZE) continue;
  vsize = (u64)signed_vsize;
  ret = ubi_volume_read(v->name, (char *)addr, SB_SIZE);
  if (ret) continue;
  ret = squashfs_size((u8 *)addr, vsize, &used);
  if (ret == -EFBIG) { malformed = 1; continue; }
  if (ret) continue;
  if (found++) { ret = -EEXIST; goto detach; }
  ret = ubi_volume_read(v->name, (char *)addr, (size_t)used);
  if (ret) goto detach;
  sha256_csum_wd((u8 *)addr, (unsigned int)used, digest, 0x10000);
  hex_digest(digest, hex);
  classify(hex, s);
  if (!strcmp(s->status, "unknown") &&
      !rt3_squashfs_read_release((const void *)addr, (size_t)used,
                                 release, sizeof(release)))
   classify_release_file(s, release);
  s->error[0] = 0;
 }
 if (!found) ret = malformed ? -EFBIG : -ENOENT;
 else ret = 0;
detach:
 if (ubi_attempted)
  ubi_exit_readonly();
restore_env:
 if (env_restore()) {
  environment_restored = 0;
  ret = -EIO;
 }
 return ret;
}
static void format_slots(const struct rt3_layout *layout)
{
 int len,i,n,cmdlen; unsigned int physical_index; u64 physical_end;
 char boot_command[48];
 const char *mode=rt3_layout_mode_name(layout->mode);
 len=snprintf(slots_json,sizeof(slots_json),
  "{\"environment_restored\":%s,\"mode\":\"%s\",\"count\":%u,\"fingerprint\":\"0x%08x\",%s\"slots\":[",
  environment_restored?"true":"false",mode,layout->count,layout->fingerprint,SLOT_ACCESS_JSON);
 if(len<0||(size_t)len>=sizeof(slots_json))goto too_large;
 for(i=0;i<layout->count;i++){
  physical_index=1;
  if(layout->count==2&&slots[i].offset>slots[1-i].offset)physical_index=2;
  physical_end=slots[i].offset+slots[i].size-1;
  json_escape(slots[i].part_name,escaped_part[i],sizeof(escaped_part[i]));
  json_escape(slots[i].label,escaped_label[i],sizeof(escaped_label[i]));
  json_escape(slots[i].version,escaped_version[i],sizeof(escaped_version[i]));
  cmdlen=snprintf(boot_command,sizeof(boot_command),"bootipq %s",slots[i].part_name);
  if(cmdlen<0||(size_t)cmdlen>=sizeof(boot_command))goto too_large;
  n=snprintf(slots_json+len,sizeof(slots_json)-len,
   "%s{\"name\":\"%s\",\"part_name\":\"%s\",\"display_index\":%u,\"physical_index\":%u,\"offset\":\"0x%llx\",\"physical_end\":\"0x%llx\",\"size\":\"0x%llx\",\"boot_command\":\"%s\",\"label\":\"%s\",\"known\":%s,\"detection\":\"%s\",\"type\":\"%s\",\"version\":\"%s\",\"firmware_version\":%s%s%s,\"error\":\"%s\"}",
   i?",":"",escaped_part[i],escaped_part[i],slots[i].display_index,physical_index,
   (unsigned long long)slots[i].offset,(unsigned long long)physical_end,(unsigned long long)slots[i].size,
   boot_command,escaped_label[i],!strcmp(slots[i].status,"known")?"true":"false",
   slots[i].detection,slots[i].type,escaped_version[i],
   !strcmp(slots[i].status,"known")&&!strcmp(slots[i].type,"OEM")?"\"":"",
   !strcmp(slots[i].status,"known")&&!strcmp(slots[i].type,"OEM")?escaped_version[i]:"null",
   !strcmp(slots[i].status,"known")&&!strcmp(slots[i].type,"OEM")?"\"":"",
   slots[i].error);
  if(n<0||(size_t)n>=sizeof(slots_json)-len)goto too_large;
  len+=n;
 }
 n=snprintf(slots_json+len,sizeof(slots_json)-len,"]}");
 if(n<0||(size_t)n>=sizeof(slots_json)-len)goto too_large;
 return;
too_large:
 strlcpy(slots_json,"{\"environment_restored\":false,\"mode\":\"unsupported\",\"count\":0,\"slots\":[],\"error\":\"response_too_large\"}",sizeof(slots_json));
}

static int same_layout(const struct rt3_layout *a, const struct rt3_layout *b)
{
        unsigned int i;
        if (a->fingerprint != b->fingerprint || a->mode != b->mode ||
            a->count != b->count || a->count > RT3_LAYOUT_MAX_PARTITIONS)
                return 0;
        for (i = 0; i < a->count; i++) {
                if (a->parts[i].display_index != b->parts[i].display_index ||
                    a->parts[i].offset != b->parts[i].offset ||
                    a->parts[i].size != b->parts[i].size ||
                    strcmp(a->parts[i].name, b->parts[i].name))
                        return 0;
        }
        return 1;
}

static void scan_slots(const struct rt3_layout *layout)
{
        const struct rt3_partition *part;
        int i, ret;
        memset(slots, 0, sizeof(slots));
        environment_restored = 1;
        for (i = 0; i < layout->count; i++) {
                struct slot_info *slot = &slots[i];
                part = &layout->parts[i];
                slot->offset = part->offset;
                slot->size = part->size;
                slot->display_index = part->display_index;
                strlcpy(slot->part_name, part->name, sizeof(slot->part_name));
                strlcpy(slot->status, "unknown", sizeof(slot->status));
                strlcpy(slot->type, "unknown", sizeof(slot->type));
                strlcpy(slot->version, "unknown", sizeof(slot->version));
                strlcpy(slot->label, "Unknown system", sizeof(slot->label));
                strlcpy(slot->detection, "unknown_rootfs_sha256", sizeof(slot->detection));
                if (!environment_restored) {
                        set_error(slot, "environment_restore_failed");
                        continue;
                }
                ret = read_rootfs(slot);
                if (ret == -ENOENT)
                        set_error(slot, "squashfs_not_found");
                else if (ret == -EEXIST)
                        set_error(slot, "multiple_squashfs_volumes");
                else if (ret == -ERANGE || ret == -EFBIG)
                        set_error(slot, "rootfs_hash_limit_or_range");
                else if (ret && !environment_restored)
                        set_error(slot, "environment_restore_failed");
                else if (ret)
                        set_error(slot, "ubi_read_failed");
        }
        format_slots(layout);
        slots_ready = 1;
}

void slots_rt3000_b_invalidate_cache(void)
{
	slots_ready = 0;
	memset(slots, 0, sizeof(slots));
	memset(slots_json, 0, sizeof(slots_json));
	rt3_layout_invalidate_cache();
}

static void boot_slots_handler(enum httpd_uri_handler_status status,
 struct httpd_request *request, struct httpd_response *response)
{
 struct rt3_layout layout;
 int ret;

 if (status != HTTP_CB_NEW) return;
 response->status = HTTP_RESP_STD;
 response->info.code = 200;
 response->info.connection_close = 1;
 response->info.content_type = "application/json";
 if (!request || request->method != HTTP_GET) {
  response->info.code = 405;
  response->data = "{\"error\":\"GET required\"}";
  response->size = strlen(response->data);
  return;
 }
 memset(&layout, 0, sizeof(layout));
 layout.mode = RT3_LAYOUT_UNSUPPORTED;
 ret = rt3_layout_read(&layout);
 if (ret || layout.mode == RT3_LAYOUT_UNSUPPORTED ||
     !layout.count || layout.count > SLOT_N) {
  memset(slots, 0, sizeof(slots));
  set_error(&slots[0], ret ? "layout_validation_failed" :
            "unsupported_rootfs_layout");
  layout.mode = RT3_LAYOUT_UNSUPPORTED;
  layout.count = 0;
  layout.fingerprint = 0;
  format_slots(&layout);
  slots_ready = 0;
 } else if (!slots_ready || !same_layout(&scanned_layout, &layout)) {
  scan_slots(&layout);
  scanned_layout = layout;
 }
 response->data = slots_json;
 response->size = strlen(response->data);
}
void slots_rt3000_b_register_uri_handlers(struct httpd_instance *inst)
{
 httpd_register_uri_handler(inst, "/boot/slots", boot_slots_handler, NULL);
}

