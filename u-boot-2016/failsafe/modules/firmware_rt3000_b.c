// SPDX-License-Identifier: GPL-2.0
/* Explicit-target, one-shot RT3000-B firmware transaction endpoints. */
#include <common.h>
#include <malloc.h>
#include <asm/errno.h>
#include <asm/unaligned.h>
#include <linux/crc32.h>
#include <linux/mtd/mtd.h>
#include <linux/sizes.h>
#include <nand.h>
#include <net/httpd.h>
#include <u-boot/sha256.h>
#include <libfdt.h>
#include <rt3000_b_layout.h>
#include <rt3000_b_fw_write.h>
#include <rt3000_b_fw_core.h>
#include "modules.h"
#include "../../drivers/mtd/ubi/ubi-media.h"
#if defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY)
#include "firmware_oem_hw_a.h"
#else
#include "firmware_oem_rt3000_b.h"
#endif

#define RT3_FW_MAX_UPLOAD       (80U * SZ_1M)
#define RT3_FW_MAX_PEB_COUNT    640U
#define RT3_UBI_PEB_SIZE        0x20000U
#define RT3_UBI_PAGE_SIZE       2048U
#define RT3_UBI_VID_OFFSET      2048U
#define RT3_UBI_DATA_OFFSET     4096U
#define RT3_UBI_LEB_SIZE        (RT3_UBI_PEB_SIZE - RT3_UBI_DATA_OFFSET)
#define RT3_UBI_VTBL_REC_SIZE   172U
#define RT3_UBI_VTBL_SIZE       (128U * RT3_UBI_VTBL_REC_SIZE)
#define RT3_UBI_LAYOUT_ID       (0x7fffffffU - 4096U)
#define RT3_UBI_MIN_RESERVE     4U /* 2 layout + WL + EBA PEBs */
#define RT3_FIT_OFFSET          160U

struct rt3_fw_pending {
	const u8 *file;
	size_t file_size;
	u32 token;
	u32 layout_fingerprint;
	u64 target_offset;
	u64 target_size;
	char target[RT3_LAYOUT_PARTITION_NAME_LEN];
	char mode[8];
	u8 digest[SHA256_SUM_LEN];
	int valid;
};
struct rt3_ubi_summary {
	u32 image_pebs;
	u32 reserved_pebs;
	int oem_volumes;	/* wifi_fw or bt_fw: an H3C OEM system */
};
struct rt3_fw_oem {
	const struct rt3_oem_fw *catalog;
	const u8 *ubi;
	size_t ubi_size;
};

static struct rt3_fw_pending fw_pending;
static char fw_reply[1024];
static u32 rt3_good_blocks[RT3_FW_MAX_PEB_COUNT];
static u8 *rt3_verify_buf;
static u8 *rt3_write_buf;

static void fw_reply_json(struct httpd_response *response, int code,
			  const char *json)
{
	response->status = HTTP_RESP_STD;
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
	response->data = json;
	response->size = strlen(json);
}

static int fw_form_count(struct httpd_request *request, const char *name)
{
	unsigned int i, count = 0;
	if (!request || !name) return 0;
	for (i = 0; i < request->form.count; i++)
		if (request->form.values[i].name &&
		    !strcmp(request->form.values[i].name, name))
			count++;
	return count;
}

static int fw_form_text(struct httpd_request *request, const char *name,
			char *out, size_t out_size)
{
	struct httpd_form_value *v;
	if (fw_form_count(request, name) != 1) return -EINVAL;
	v = httpd_request_find_value(request, name);
	if (!v || !v->data || !v->size || v->size >= out_size ||
	    memchr(v->data, '\0', v->size))
		return -EINVAL;
	memcpy(out, v->data, v->size);
	out[v->size] = '\0';
	return 0;
}

static int fw_get_layout(struct rt3_layout *layout)
{
	int ret;
	memset(layout, 0, sizeof(*layout));
	rt3_layout_invalidate_cache();
	ret = rt3_layout_read(layout);
	if (ret)
		return ret;
	if (!layout->count || layout->count > RT3_LAYOUT_MAX_PARTITIONS ||
	    layout->mode == RT3_LAYOUT_UNSUPPORTED)
		return -EINVAL;
	return 0;
}

static int fw_pick_target(const struct rt3_layout *layout, const char *name,
			  const char *mode,
			  const struct rt3_partition **part)
{
	const char *expected = rt3_layout_mode_name(layout->mode);
	if (!expected || strcmp(expected, mode))
		return -EINVAL;
	if (layout->mode == RT3_LAYOUT_SINGLE && strcmp(name, "rootfs"))
		return -EINVAL;
	if (layout->mode == RT3_LAYOUT_DUAL &&
	    strcmp(name, "rootfs") && strcmp(name, "rootfs_1"))
		return -EINVAL;
	return rt3_layout_find(layout, name, part);
}

static void fw_sha256(const void *data, size_t len, u8 digest[SHA256_SUM_LEN])
{
	sha256_csum_wd(data, (unsigned int)len, digest, CHUNKSZ_SHA256);
}

static void fw_hex(const u8 *data, size_t len, char *out)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;
	for (i = 0; i < len; i++) {
		out[2 * i] = hex[data[i] >> 4];
		out[2 * i + 1] = hex[data[i] & 0xf];
	}
	out[2 * len] = '\0';
}

static int fw_parse_token(const char *text, u32 *token)
{
	u32 value = 0;
	int i;
	if (!text || !token || strlen(text) != 8) return -EINVAL;
	for (i = 0; i < 8; i++) {
		char c = text[i];
		u32 digit;
		if (c >= '0' && c <= '9') digit = c - '0';
		else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
		else return -EINVAL;
		value = (value << 4) | digit;
	}
	*token = value;
	return 0;
}

static int fw_digest_matches(const u8 digest[SHA256_SUM_LEN], const char *hex)
{
	char got[SHA256_SUM_LEN * 2 + 1];
	fw_hex(digest, SHA256_SUM_LEN, got);
	return !strcmp(got, hex);
}

static u64 fw_get_unaligned_le64(const u8 *p)
{
	return (u64)p[0] | ((u64)p[1] << 8) | ((u64)p[2] << 16) |
	       ((u64)p[3] << 24) | ((u64)p[4] << 32) |
	       ((u64)p[5] << 40) | ((u64)p[6] << 48) |
	       ((u64)p[7] << 56);
}

static u16 fw_get_unaligned_le16(const u8 *p)
{
	return p[0] | ((u16)p[1] << 8);
}

static int fw_crc_ok(const u8 *data, size_t data_len, const u8 *crc_ptr)
{
	return get_unaligned_be32(crc_ptr) ==
	       crc32(UBI_CRC32_INIT, data, data_len);
}

static int fw_all_value(const u8 *p, size_t len, u8 value)
{
	size_t i;
	for (i = 0; i < len; i++)
		if (p[i] != value)
			return 0;
	return 1;
}

/* Validate UBI v1 PEB stream, redundant volume tables, volume geometry and CRCs. */
static int fw_validate_ubi(const u8 *image, size_t size,
			   struct rt3_ubi_summary *summary)
{
	const u8 *vtbl[2] = { NULL, NULL };
	static u8 volume_seen[128];
	static u32 volume_reserved[128];
	static u32 volume_data_pad[128];
	static u32 volume_used_ebs[128];
	static u8 volume_type[128];
	static u8 volume_used_seen[128];
	u8 lnum_seen[128][(RT3_FW_MAX_PEB_COUNT + 7) / 8];
	const char *volume_name[128];
	u32 pebs, i, table_copies = 0, table_seen = 0, total_reserved = 0;
	u32 image_seq = 0;
	int image_seq_set = 0;
	int have_kernel = 0, have_rootfs = 0, rootfs_data = 0, kernel_data = 0;
	int oem_volumes = 0;
	u32 kernel_needed_lebs = 0, rootfs_needed_lebs = 0;
	u32 kernel_id = 0xffffffffU, rootfs_id = 0xffffffffU;

	if (!image || !summary || size < RT3_UBI_PEB_SIZE ||
	    size % RT3_UBI_PEB_SIZE || size > RT3_FW_MAX_UPLOAD)
		return -EINVAL;
	pebs = size / RT3_UBI_PEB_SIZE;
	if (pebs > RT3_FW_MAX_PEB_COUNT)
		return -EFBIG;
	memset(vtbl, 0, sizeof(vtbl));
	memset(volume_seen, 0, sizeof(volume_seen));
	memset(volume_reserved, 0, sizeof(volume_reserved));
	memset(volume_data_pad, 0, sizeof(volume_data_pad));
	memset(volume_used_ebs, 0, sizeof(volume_used_ebs));
	memset(volume_type, 0, sizeof(volume_type));
	memset(volume_used_seen, 0, sizeof(volume_used_seen));
	memset(lnum_seen, 0, sizeof(lnum_seen));
	memset(volume_name, 0, sizeof(volume_name));

	for (i = 0; i < pebs; i++) {
		const u8 *peb = image + (size_t)i * RT3_UBI_PEB_SIZE;
		const u8 *ec = peb;
		const u8 *vid = peb + RT3_UBI_VID_OFFSET;
		u32 vol_id, lnum, data_size, data_pad;
		if (get_unaligned_be32(ec) != UBI_EC_HDR_MAGIC ||
		    ec[4] != UBI_VERSION || get_unaligned_be32(ec + 16) != RT3_UBI_VID_OFFSET ||
		    get_unaligned_be32(ec + 20) != RT3_UBI_DATA_OFFSET ||
		    !fw_crc_ok(ec, 60, ec + 60))
			return -EBADMSG;
		if (!image_seq_set) {
			image_seq = get_unaligned_be32(ec + 24);
			image_seq_set = 1;
		} else if (image_seq != get_unaligned_be32(ec + 24)) {
			return -EINVAL;
		}
		if (fw_all_value(vid, UBI_VID_HDR_SIZE, 0xff))
			continue;
		if (get_unaligned_be32(vid) != UBI_VID_HDR_MAGIC ||
		    vid[4] != UBI_VERSION || !fw_crc_ok(vid, 60, vid + 60))
			return -EBADMSG;
		vol_id = get_unaligned_be32(vid + 8);
		lnum = get_unaligned_be32(vid + 12);
		data_size = get_unaligned_be32(vid + 20);
		data_pad = get_unaligned_be32(vid + 28);
		if (data_pad > RT3_UBI_LEB_SIZE || data_size > RT3_UBI_LEB_SIZE - data_pad)
			return -EINVAL;
		if (vol_id == RT3_UBI_LAYOUT_ID) {
			if (lnum > 1 || table_copies >= 2 || (table_seen & (1U << lnum)) || data_pad ||
			    vid[5] != UBI_VID_DYNAMIC || vid[7] != UBI_COMPAT_REJECT ||
			    RT3_UBI_VTBL_SIZE > RT3_UBI_LEB_SIZE)
				return -EINVAL;
			vtbl[lnum] = peb + RT3_UBI_DATA_OFFSET;
			table_seen |= 1U << lnum;
			table_copies++;
			continue;
		}
		/* Fastmap stores physical mappings which this writer intentionally remaps. */
		if (vol_id >= ARRAY_SIZE(volume_seen))
			return -EINVAL;
		if (lnum >= RT3_FW_MAX_PEB_COUNT || data_size > RT3_UBI_LEB_SIZE - data_pad)
			return -EINVAL;
	}
	if (table_copies != 2 || table_seen != 3 ||
	    memcmp(vtbl[0], vtbl[1], RT3_UBI_VTBL_SIZE))
		return -EBADMSG;

	for (i = 0; i < 128; i++) {
		const u8 *r = vtbl[0] + (size_t)i * RT3_UBI_VTBL_REC_SIZE;
		u32 reserved = get_unaligned_be32(r);
		u32 align = get_unaligned_be32(r + 4);
		u32 data_pad = get_unaligned_be32(r + 8);
		u16 name_len = get_unaligned_be16(r + 14);
		if (!reserved) {
			if (!fw_all_value(r, RT3_UBI_VTBL_REC_SIZE - 4, 0) ||
			    get_unaligned_be32(r + RT3_UBI_VTBL_REC_SIZE - 4) !=
			    crc32(UBI_CRC32_INIT, r, RT3_UBI_VTBL_REC_SIZE - 4))
				return -EBADMSG;
			continue;
		}
		if (!fw_crc_ok(r, RT3_UBI_VTBL_REC_SIZE - 4,
			       r + RT3_UBI_VTBL_REC_SIZE - 4) ||
		    !align || align > RT3_UBI_LEB_SIZE || name_len == 0 ||
		    name_len > UBI_VOL_NAME_MAX || r[16 + name_len] != '\0' ||
		    r[12] < UBI_VID_DYNAMIC || r[12] > UBI_VID_STATIC ||
		    r[13] > 1 || data_pad != RT3_UBI_LEB_SIZE % align)
			return -EBADMSG;
		volume_seen[i] = 1;
		volume_reserved[i] = reserved;
		volume_type[i] = r[12];
		volume_data_pad[i] = data_pad;
		volume_name[i] = (const char *)r + 16;
		{
			u32 j;
			for (j = 0; j < i; j++)
				if (volume_seen[j] && !strcmp(volume_name[j], volume_name[i]))
					return -EINVAL;
		}
		if (!strcmp(volume_name[i], "kernel")) { have_kernel++; kernel_id = i; }
		if (!strcmp(volume_name[i], "ubi_rootfs")) { have_rootfs++; rootfs_id = i; }
		if (!strcmp(volume_name[i], "wifi_fw") || !strcmp(volume_name[i], "bt_fw"))
			oem_volumes = 1;
		if (reserved > RT3_FW_MAX_PEB_COUNT - total_reserved)
			return -EOVERFLOW;
		total_reserved += reserved;
	}
	if (have_kernel != 1 || have_rootfs != 1)
		return -EINVAL;

	/* Every ordinary VID header must refer to a defined volume and reserved LEB. */
	for (i = 0; i < pebs; i++) {
		const u8 *peb = image + (size_t)i * RT3_UBI_PEB_SIZE;
		const u8 *vid = peb + RT3_UBI_VID_OFFSET;
		u32 vol_id;
		if (fw_all_value(vid, UBI_VID_HDR_SIZE, 0xff)) continue;
		vol_id = get_unaligned_be32(vid + 8);
		if (vol_id == RT3_UBI_LAYOUT_ID || vol_id >= ARRAY_SIZE(volume_seen)) continue;
		{
			u32 lnum = get_unaligned_be32(vid + 12);
			u32 data_size = get_unaligned_be32(vid + 20);
			u32 used_ebs = get_unaligned_be32(vid + 24);
			u32 data_crc = get_unaligned_be32(vid + 32);
			u8 bit = (u8)(1U << (lnum & 7));
			if (!volume_seen[vol_id] || lnum >= volume_reserved[vol_id] ||
			    vid[5] != volume_type[vol_id] ||
			    get_unaligned_be32(vid + 28) != volume_data_pad[vol_id] ||
			    (lnum_seen[vol_id][lnum >> 3] & bit))
				return -EINVAL;
			lnum_seen[vol_id][lnum >> 3] |= bit;
			if (volume_type[vol_id] == UBI_VID_DYNAMIC && !vid[6] &&
			    (data_size || used_ebs || data_crc))
				return -EINVAL;
			if (volume_type[vol_id] == UBI_VID_STATIC) {
				if (vid[6] || !used_ebs || used_ebs > volume_reserved[vol_id] ||
				    lnum >= used_ebs || !data_size ||
				    data_size > RT3_UBI_LEB_SIZE - volume_data_pad[vol_id])
					return -EINVAL;
				if (!volume_used_seen[vol_id]) {
					volume_used_ebs[vol_id] = used_ebs;
					volume_used_seen[vol_id] = 1;
				} else if (volume_used_ebs[vol_id] != used_ebs) {
					return -EINVAL;
				}
			}
			if (data_size && (vid[6] || volume_type[vol_id] == UBI_VID_STATIC) &&
			    crc32(UBI_CRC32_INIT, peb + RT3_UBI_DATA_OFFSET, data_size) !=
			    get_unaligned_be32(vid + 32))
				return -EBADMSG;
		}
		if (get_unaligned_be32(vid + 12) == 0) {
			const u8 *data = peb + RT3_UBI_DATA_OFFSET;
			if (vol_id == kernel_id) {
				u32 fit_bytes;
				if (get_unaligned_be32(data) != 0xd00dfeed)
					return -EINVAL;
				fit_bytes = get_unaligned_be32(data + 4);
				if (fit_bytes < 40 || fit_bytes > volume_reserved[vol_id] * RT3_UBI_LEB_SIZE)
					return -EINVAL;
				kernel_needed_lebs = (fit_bytes + RT3_UBI_LEB_SIZE - 1) / RT3_UBI_LEB_SIZE;
				kernel_data = 1;
			}
			if (vol_id == rootfs_id) {
				u64 squash_bytes;
				if (memcmp(data, "hsqs", 4) ||
				    fw_get_unaligned_le16(data + 28) != 4)
					return -EINVAL;
				squash_bytes = fw_get_unaligned_le64(data + 40);
				if (squash_bytes < 96 || squash_bytes >
				    (u64)volume_reserved[vol_id] * RT3_UBI_LEB_SIZE)
					return -EINVAL;
				rootfs_needed_lebs = (squash_bytes + RT3_UBI_LEB_SIZE - 1) / RT3_UBI_LEB_SIZE;
				rootfs_data = 1;
			}
		}
	}
	for (i = 0; i < 128; i++) {
		u32 lnum;
		if (!volume_seen[i] || volume_type[i] != UBI_VID_STATIC) continue;
		for (lnum = 0; lnum < volume_used_ebs[i]; lnum++)
			if (!(lnum_seen[i][lnum >> 3] & (1U << (lnum & 7))))
				return -EINVAL;
	}
	if (!kernel_data || !rootfs_data || !kernel_needed_lebs || !rootfs_needed_lebs)
		return -EINVAL;
	for (i = 0; i < kernel_needed_lebs; i++)
		if (!(lnum_seen[kernel_id][i >> 3] & (1U << (i & 7))))
			return -EINVAL;
	for (i = 0; i < rootfs_needed_lebs; i++)
		if (!(lnum_seen[rootfs_id][i >> 3] & (1U << (i & 7))))
			return -EINVAL;
	summary->image_pebs = pebs;
	summary->reserved_pebs = total_reserved;
	summary->oem_volumes = oem_volumes;
	return 0;
}

static const struct rt3_oem_fw *fw_catalog_match(size_t size,
						 const u8 digest[SHA256_SUM_LEN])
{
	char hex[SHA256_SUM_LEN * 2 + 1];
	u32 i;
	fw_hex(digest, SHA256_SUM_LEN, hex);
	for (i = 0; i < ARRAY_SIZE(rt3_oem_fw_catalog); i++)
		if (size == rt3_oem_fw_catalog[i].package_bytes &&
		    !strcmp(hex, rt3_oem_fw_catalog[i].package_sha256))
			return &rt3_oem_fw_catalog[i];
	return NULL;
}

static int fw_extract_input(const u8 *file, size_t file_size,
			    const u8 file_hash[SHA256_SUM_LEN],
			    struct rt3_fw_oem *out,
			    const char **format)
{
	const struct rt3_oem_fw *entry;
	const u8 *fit, *data;
	char path[160];
	int node, len;
	struct rt3_ubi_summary sum;

	memset(out, 0, sizeof(*out));
	if (file_size >= 4 && get_unaligned_be32(file) == UBI_EC_HDR_MAGIC) {
		out->ubi = file;
		out->ubi_size = file_size;
		*format = "ubi_unidentified";
	} else {
		entry = fw_catalog_match(file_size, file_hash);
		if (!entry)
			return -EPERM;
		if (file_size <= RT3_FIT_OFFSET || entry->fit_bytes > file_size - RT3_FIT_OFFSET)
			return -EINVAL;
		fit = file + RT3_FIT_OFFSET;
		if (fdt_check_header(fit) || fdt_totalsize(fit) != entry->fit_bytes)
			return -EBADMSG;
		snprintf(path, sizeof(path), "/images/%s", entry->ubi_name);
		node = fdt_path_offset(fit, path);
		if (node < 0)
			return -ENOENT;
		data = fdt_getprop(fit, node, "data", &len);
		if (!data || len < 0 || (u32)len != entry->ubi_bytes)
			return -EBADMSG;
		{
			u8 ubi_hash[SHA256_SUM_LEN];
			fw_sha256(data, len, ubi_hash);
			if (!fw_digest_matches(ubi_hash, entry->ubi_sha256))
				return -EBADMSG;
		}
		out->catalog = entry;
		out->ubi = data;
		out->ubi_size = len;
		*format = "h3c_rt3000_oem_ubi";
	}
	if (fw_validate_ubi(out->ubi, out->ubi_size, &sum))
		return -EBADMSG;
	return 0;
}

/*
 * Match what UBI needs to attach: the volumes plus its two layout PEBs and
 * the WL and EBA reserves. When good PEBs run short UBI shrinks the bad-PEB
 * reserve with a warning instead of refusing, so demanding the full reserve
 * here refused images that UBI and the flash command accept, such as a full
 * backup of the partition.
 */
static u32 fw_reserve_margin(const struct mtd_info *mtd)
{
	if (!mtd || !mtd->erasesize)
		return 0xffffffffU;
	return RT3_UBI_MIN_RESERVE;
}

static int fw_target_geometry(const struct rt3_partition *part,
			      struct mtd_info **mtd_out)
{
	struct mtd_info *mtd = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	if (!part || !mtd || mtd->type != MTD_NANDFLASH ||
	    mtd->writesize != RT3_UBI_PAGE_SIZE ||
	    mtd->erasesize != RT3_UBI_PEB_SIZE || !part->size ||
	    part->offset % mtd->erasesize || part->size % mtd->erasesize ||
	    part->offset > mtd->size || part->size > mtd->size - part->offset ||
	    part->size / mtd->erasesize > RT3_FW_MAX_PEB_COUNT)
		return -EINVAL;
	*mtd_out = mtd;
	return 0;
}

static int fw_http_upload_range_ok(const void *ptr, size_t len)
{
	uintptr_t start = (uintptr_t)ptr, end;
	if (!ptr || !len || start > (uintptr_t)-1 - len)
		return 0;
	end = start + len;
	/* Multipart firmware data must remain in the persistent reserved upload RAM. */
	if (start < 0x44000000UL ||
	    end > 0x44000000UL + RT3_FW_MAX_UPLOAD + SZ_64K)
		return 0;
	return 1;
}

static void fw_layout_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	struct rt3_layout layout;
	unsigned int i;
	int ret;
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_GET) {
		fw_reply_json(response, 405, "{\"status\":\"fail\",\"error\":\"GET required\"}");
		return;
	}
	ret = fw_get_layout(&layout);
	if (ret) {
		fw_reply_json(response, 503, "{\"status\":\"unsupported\",\"write_supported\":false,\"ptable_write_supported\":false}");
		return;
	}
	#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
snprintf(fw_reply, sizeof(fw_reply),
         "{\"status\":\"ok\",\"mode\":\"%s\",\"read_only\":true,\"write_supported\":false,\"ptable_write_supported\":false,\"partitions\":[",
         rt3_layout_mode_name(layout.mode));
#else
snprintf(fw_reply, sizeof(fw_reply),
         "{\"status\":\"ok\",\"mode\":\"%s\",\"read_only\":false,\"write_supported\":true,\"ptable_write_supported\":true,\"partitions\":[",
         rt3_layout_mode_name(layout.mode));
#endif
	for (i = 0; i < layout.count; i++) {
		char part_json[256];
		snprintf(part_json, sizeof(part_json),
			 "{\"name\":\"%s\",\"display_index\":%u,\"offset\":%llu,\"size\":%llu}%s",
			 layout.parts[i].name, layout.parts[i].display_index,
			 (unsigned long long)layout.parts[i].offset,
			 (unsigned long long)layout.parts[i].size,
			 i + 1 == layout.count ? "]}" : ",");
		strlcat(fw_reply, part_json, sizeof(fw_reply));
	}
	fw_reply_json(response, 200, fw_reply);
}

static void fw_validate_handler(enum httpd_uri_handler_status status,
				struct httpd_request *request,
				struct httpd_response *response)
{
	struct httpd_form_value *file;
	struct rt3_layout layout;
	const struct rt3_partition *part;
	struct rt3_fw_oem extracted;
	struct rt3_ubi_summary summary;
	struct mtd_info *mtd;
	const char *format = "unknown";
	u32 reserve_margin;
	char target[RT3_LAYOUT_PARTITION_NAME_LEN], mode[8], digest_hex[65];
	char identity[32] = "unidentified";
	u8 digest[SHA256_SUM_LEN];
	int ret;
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) {
		fw_reply_json(response, 405, "{\"status\":\"fail\",\"error\":\"POST required\"}");
		return;
	}
	memset(&fw_pending, 0, sizeof(fw_pending));
	if (fw_form_count(request, "firmware") != 1) {
		fw_reply_json(response, 400, "{\"status\":\"fail\",\"error\":\"exactly one firmware field required\"}");
		return;
	}
	file = httpd_request_find_value(request, "firmware");
	if (!file || !file->data ||
	    !rt3_fw_core_input_size_ok(file->size, RT3_FW_MAX_UPLOAD,
				       file->size) ||
	    !fw_http_upload_range_ok(file->data, file->size)) {
		fw_reply_json(response, 413, "{\"status\":\"fail\",\"error\":\"invalid or oversized firmware field\"}");
		return;
	}
	if (fw_form_text(request, "target", target, sizeof(target)) ||
	    fw_form_text(request, "mode", mode, sizeof(mode))) {
		fw_reply_json(response, 400, "{\"status\":\"fail\",\"error\":\"target and mode required\"}");
		return;
	}
	if ((ret = fw_get_layout(&layout)) ||
	    (ret = fw_pick_target(&layout, target, mode, &part)) ||
	    (ret = fw_target_geometry(part, &mtd))) {
		fw_reply_json(response, 409, "{\"status\":\"fail\",\"error\":\"layout, mode, or target mismatch\"}");
		return;
	}
	if (!rt3_fw_core_input_size_ok(file->size, RT3_FW_MAX_UPLOAD,
				       part->size)) {
		fw_reply_json(response, 413, "{\"status\":\"fail\",\"error\":\"image exceeds selected extent\"}");
		return;
	}
	fw_sha256(file->data, file->size, digest);
	ret = fw_extract_input((const u8 *)file->data, file->size, digest, &extracted, &format);
	if (ret || extracted.ubi_size > part->size ||
	    fw_validate_ubi(extracted.ubi, extracted.ubi_size, &summary)) {
		fw_reply_json(response, 422, "{\"status\":\"fail\",\"error\":\"unsupported package or invalid UBI image\"}");
		return;
	}
	/* The big single rootfs is for third-party systems; OEM firmware was
	 * never verified there. Restore the dual MIBIB to run it again. */
	if (layout.mode == RT3_LAYOUT_SINGLE &&
	    (extracted.catalog || summary.oem_volumes)) {
		fw_reply_json(response, 422, "{\"status\":\"fail\",\"error\":\"oem_firmware_needs_dual_layout\"}");
		return;
	}
	reserve_margin = fw_reserve_margin(mtd);
	if (!rt3_fw_core_capacity_ok(part->size / mtd->erasesize,
				     summary.image_pebs, summary.reserved_pebs,
				     reserve_margin)) {
		fw_reply_json(response, 422, "{\"status\":\"fail\",\"error\":\"UBI capacity leaves insufficient layout and wear-level reserve\"}");
		return;
	}
	memset(&fw_pending, 0, sizeof(fw_pending));
	fw_pending.file = (const u8 *)file->data;
	fw_pending.file_size = file->size;
	fw_pending.token = rand();
	if (!fw_pending.token) fw_pending.token = 1;
	fw_pending.layout_fingerprint = layout.fingerprint;
	fw_pending.target_offset = part->offset;
	fw_pending.target_size = part->size;
	strlcpy(fw_pending.target, target, sizeof(fw_pending.target));
	strlcpy(fw_pending.mode, mode, sizeof(fw_pending.mode));
	memcpy(fw_pending.digest, digest, sizeof(digest));
	fw_pending.valid = 1;
	fw_hex(digest, sizeof(digest), digest_hex);
	if (extracted.catalog)
		snprintf(identity, sizeof(identity), "%s %s", extracted.catalog->model, extracted.catalog->release);
	snprintf(fw_reply, sizeof(fw_reply),
		 "{\"status\":\"ok\",\"token\":\"%08x\",\"target\":\"%s\",\"mode\":\"%s\",\"bytes\":%lu,\"sha256\":\"%s\",\"format\":\"%s\",\"firmware_identity\":\"%s\"}",
		 fw_pending.token, target, mode, (ulong)file->size, digest_hex, format, identity);
	fw_reply_json(response, 200, fw_reply);
}

struct fw_mtd_io {
	struct mtd_info *mtd;
	u8 *write_buf;
};

static int fw_io_bad(void *ctx, u64 offset)
{
	struct fw_mtd_io *io = ctx;
	return nand_block_isbad(io->mtd, offset);
}

static int fw_io_erase(void *ctx, u64 offset, u32 size)
{
	struct fw_mtd_io *io = ctx;
	struct erase_info erase;
	memset(&erase, 0, sizeof(erase));
	erase.mtd = io->mtd;
	erase.addr = offset;
	erase.len = size;
	return mtd_erase(io->mtd, &erase);
}

static int fw_io_write(void *ctx, u64 offset, const u8 *buf,
		       u32 size, u32 *written)
{
	struct fw_mtd_io *io = ctx;
	u32 page = io->mtd->writesize, off;
	size_t done;
	int ret;
	*written = 0;
	if (!io->write_buf || size > io->mtd->erasesize || !page || size % page)
		return -EINVAL;
	memcpy(io->write_buf, buf, size);
	/*
	 * Leave all-0xff pages erased. Programming them stores ECC for an empty
	 * page, and Linux UBI later writes over such pages in "free" PEBs,
	 * leaving pages whose ECC can never be corrected.
	 */
	for (off = 0; off < size; off += page) {
		if (memchr_inv(io->write_buf + off, 0xff, page)) {
			done = 0;
			ret = mtd_write(io->mtd, offset + off, page, &done,
					io->write_buf + off);
			if (ret || done != page)
				return ret ? ret : -EIO;
		}
		*written += page;
	}
	return 0;
}

static int fw_io_read(void *ctx, u64 offset, u8 *buf, u32 size, u32 *read)
{
	struct fw_mtd_io *io = ctx;
	size_t done = 0;
	int ret = mtd_read(io->mtd, offset, size, &done, buf);
	*read = done;
	if (ret == -EUCLEAN && done == size)
		return 0;
	return ret;
}

static int fw_io_window_open(void *ctx, u64 offset, u64 size)
{
	struct fw_mtd_io *io = ctx;
	return rt3_fw_write_window_open(io->mtd, offset, size);
}

static void fw_io_window_close(void *ctx)
{
	(void)ctx;
	rt3_fw_write_window_close();
}

static int fw_write_image(struct mtd_info *mtd,
			  const struct rt3_partition *part,
			  const u8 *ubi, size_t ubi_size,
			  const struct rt3_ubi_summary *sum, int *stage)
{
	static const struct rt3_fw_io io_template = {
		.is_bad = fw_io_bad,
		.erase = fw_io_erase,
		.write = fw_io_write,
		.read = fw_io_read,
		.window_open = fw_io_window_open,
		.window_close = fw_io_window_close,
	};
	struct rt3_fw_io io = io_template;
	struct fw_mtd_io ctx;
	u32 good = 0, pebs, i;
	u64 off;
	u8 *peb;
	if (ubi_size > 0xffffffffU || ubi_size % mtd->erasesize ||
	    ubi_size > part->size || sum->image_pebs != ubi_size / mtd->erasesize)
		return -EINVAL;
	/*
	 * A full backup holds one PEB per partition block. Where the target has
	 * bad blocks, drop free PEBs (EC header only) until the image fits; UBI
	 * does not care where its PEBs sit. The staged upload is consumed by this
	 * write, so it is compacted in place.
	 */
	for (off = part->offset; off < part->offset + part->size; off += mtd->erasesize) {
		int bad = nand_block_isbad(mtd, off);
		if (bad < 0)
			return bad;
		good += !bad;
	}
	pebs = sum->image_pebs;
	for (i = pebs; i-- > 0 && pebs > good;) {
		peb = (u8 *)ubi + (size_t)i * mtd->erasesize;
		if (!fw_all_value(peb + RT3_UBI_VID_OFFSET, UBI_VID_HDR_SIZE, 0xff))
			continue;
		memmove(peb, peb + mtd->erasesize, (size_t)(pebs - i - 1) * mtd->erasesize);
		pebs--;
	}
	if (!rt3_verify_buf) {
		rt3_verify_buf = memalign(ARCH_DMA_MINALIGN, mtd->erasesize);
		if (!rt3_verify_buf) return -ENOMEM;
	}
	if (!rt3_write_buf) {
		rt3_write_buf = memalign(ARCH_DMA_MINALIGN, mtd->erasesize);
		if (!rt3_write_buf) return -ENOMEM;
	}
	ctx.mtd = mtd;
	ctx.write_buf = rt3_write_buf;
	io.ctx = &ctx;
	io.stage = stage;
	return rt3_fw_core_transaction(&io, part->offset, part->size, ubi,
				       pebs * mtd->erasesize, mtd->erasesize,
				       pebs, sum->reserved_pebs,
				       fw_reserve_margin(mtd), rt3_good_blocks,
				       ARRAY_SIZE(rt3_good_blocks), rt3_verify_buf);
}

static void fw_write_handler(enum httpd_uri_handler_status status,
			     struct httpd_request *request,
			     struct httpd_response *response)
{
	struct rt3_fw_pending tx;
	struct rt3_layout layout;
	const struct rt3_partition *part;
	struct rt3_fw_oem extracted;
	struct rt3_ubi_summary sum;
	struct mtd_info *mtd;
	const char *format;
	char token_text[16], target[RT3_LAYOUT_PARTITION_NAME_LEN], mode[8];
	u32 token;
	u8 digest[SHA256_SUM_LEN];
	int ret, stage = RT3_FW_STAGE_NONE;
	if (status != HTTP_CB_NEW) return;
#if defined(CONFIG_H3C_HW_C_RAM_READONLY)
	fw_reply_json(response, 403, "{\"status\":\"fail\",\"error\":\"read-only target\"}");
	return;
#endif
	if (!request || request->method != HTTP_POST) {
		fw_reply_json(response, 405, "{\"status\":\"fail\",\"error\":\"POST required\"}");
		return;
	}
	if (fw_form_text(request, "token", token_text, sizeof(token_text)) ||
	    fw_form_text(request, "target", target, sizeof(target)) ||
	    fw_form_text(request, "mode", mode, sizeof(mode))) {
		fw_reply_json(response, 400, "{\"status\":\"fail\",\"error\":\"token, target, and mode required\"}");
		return;
	}
	if (fw_parse_token(token_text, &token)) {
		fw_reply_json(response, 400, "{\"status\":\"fail\",\"error\":\"invalid token\"}");
		return;
	}
	if (!fw_pending.valid || token != fw_pending.token) {
		fw_reply_json(response, 409, "{\"status\":\"fail\",\"error\":\"unknown or consumed token\"}");
		return;
	}
	/* Consume before any validation or flash operation: a token is one-shot. */
	tx = fw_pending;
	memset(&fw_pending, 0, sizeof(fw_pending));
	if (!fw_http_upload_range_ok(tx.file, tx.file_size)) {
		fw_reply_json(response, 409, "{\"status\":\"fail\",\"error\":\"staged upload range invalid\"}");
		return;
	}
	fw_sha256(tx.file, tx.file_size, digest);
	if (memcmp(digest, tx.digest, sizeof(digest))) {
		fw_reply_json(response, 409, "{\"status\":\"fail\",\"error\":\"staged upload changed\"}");
		return;
	}
	if ((ret = fw_get_layout(&layout)) ||
	    (ret = fw_pick_target(&layout, target, mode, &part)) ||
	    part->offset != tx.target_offset || part->size != tx.target_size ||
	    (ret = fw_target_geometry(part, &mtd))) {
		fw_reply_json(response, 409, "{\"status\":\"fail\",\"error\":\"layout changed since validation\"}");
		return;
	}
	ret = fw_extract_input(tx.file, tx.file_size, digest, &extracted, &format);
	if (!rt3_fw_core_request_matches(tx.token, token, tx.target, target,
					 tx.mode, mode, tx.layout_fingerprint,
					 layout.fingerprint, tx.digest, digest,
					 sizeof(digest))) {
		fw_reply_json(response, 409, "{\"status\":\"fail\",\"error\":\"token, staged bytes, target, or layout changed\"}");
		return;
	}
	if (ret || fw_validate_ubi(extracted.ubi, extracted.ubi_size, &sum)) {
		fw_reply_json(response, 422, "{\"status\":\"fail\",\"error\":\"staged image no longer validates\"}");
		return;
	}
	ret = fw_write_image(mtd, part, extracted.ubi, extracted.ubi_size, &sum,
			     &stage);
	rt3_layout_invalidate_cache();
	slots_rt3000_b_invalidate_cache();
	if (ret) {
		/* Report where it stopped: before ERASE the target is untouched. */
		const char *error = "write or full readback failed";
		if (stage < RT3_FW_STAGE_ERASE)
			error = "write refused before erase";
		else if (stage < RT3_FW_STAGE_VERIFY)
			error = "erase or write failed";
		printf("firmware: write %s failed at stage %d, errno %d\n",
		       target, stage, ret);
		snprintf(fw_reply, sizeof(fw_reply),
			 "{\"status\":\"fail\",\"target\":\"%s\",\"mode\":\"%s\",\"error\":\"%s\",\"stage\":%d,\"flash_changed\":%s,\"errno\":%d}",
			 target, mode, error, stage,
			 stage >= RT3_FW_STAGE_ERASE ? "true" : "false", ret);
		fw_reply_json(response, 500, fw_reply);
		return;
	}
	rt3_layout_invalidate_cache();
	snprintf(fw_reply, sizeof(fw_reply),
		 "{\"status\":\"ok\",\"target\":\"%s\",\"mode\":\"%s\",\"verified\":true,\"auto_reboot\":false,\"boot_select_changed\":false,\"env_changed\":false,\"ptable_changed\":false}",
		 target, mode);
	fw_reply_json(response, 200, fw_reply);
}

void firmware_rt3000_b_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/firmware/layout", fw_layout_handler, NULL);
	httpd_register_uri_handler(inst, "/firmware/validate", fw_validate_handler, NULL);
	httpd_register_uri_handler(inst, "/firmware/write", fw_write_handler, NULL);
}
