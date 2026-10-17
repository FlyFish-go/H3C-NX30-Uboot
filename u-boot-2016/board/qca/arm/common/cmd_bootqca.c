/*
 * Copyright (c) 2015-2017, 2020 The Linux Foundation. All rights reserved.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <command.h>
#ifdef CONFIG_H3C_RELEASE_SYSTEM_DEFAULT
#include <h3c_boot_policy.h>
#endif
#include <bootm.h>
#include <image.h>
#include <nand.h>
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
#include <h3c_nand_ecc.h>
#endif
#ifdef CONFIG_H3C_HW_D_RAM_WEB
#include <nx30_d_bootargs.h>
#include <nx30_nor_autoecc.h>
#endif
#include <errno.h>
#include <asm/arch-qca-common/scm.h>
#include <part.h>
#include <linux/mtd/ubi.h>
#include <asm/arch-qca-common/smem.h>
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
#include <rt3000_b_layout.h>
#include <bootconfig.h>
#include <rt3000_b_bootargs.h>
#include <libfdt.h>
#endif
#include <mmc.h>
#include <part_efi.h>
#include <fdtdec.h>
#include "fdt_info.h"
#include <asm/errno.h>
#include <asm/arch-qca-common/qca_common.h>
#include <usb.h>
#include <elf.h>
#include <cli.h>

#define SEC_AUTH_SW_ID 		0x17
#define ROOTFS_IMAGE_TYPE       0x13
#define NO_OF_PROGRAM_HDRS	3
#define ELF_HDR_PLUS_PHDR_SIZE	sizeof(Elf32_Ehdr) + \
		(NO_OF_PROGRAM_HDRS * sizeof(Elf32_Phdr))

#define PRIMARY_PARTITION	1
#define SECONDARY_PARTITION	2

#define FIT_BOOTARGS_PROP	"append-bootargs"

extern int qca_scm_part_info(void *cmd_buf, size_t cmd_len);

unsigned long __stack_chk_guard = 0x000a0dff;
static int debug = 0;
static char mtdids[256];
DECLARE_GLOBAL_DATA_PTR;
static qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
int ipq_fs_on_nand ;
extern int nand_env_device;
extern qca_mmc mmc_host;
extern void set_minidump_bootargs(void);

#ifdef CONFIG_QCA_MMC
static qca_mmc *host = &mmc_host;
#endif

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB))
#include <h3c_board_profile.h>
#define RT3_BOOT_BOARD_LABEL H3C_BOARD_LABEL
/* Named selection is resolved against the validated complete SMEM table. */
#define RT3000B_ROOTFS_BOOTARGS \
	"ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs"
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
#define RT3000B_ROOTFS1_BOOTARGS \
	"ubi.mtd=rootfs_1 root=mtd:ubi_rootfs rootfstype=squashfs"
#else
#define RT3000B_ROOTFS1_BOOTARGS \
	"ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs"
#endif

/* Map the requested SMEM partition in RAM; do not write BC or ENV. */
static int rt3000b_prepare_boot_slot(int argc, char *const argv[])
{
	struct rt3_layout layout;
	const struct rt3_partition *part;
	const char *target;
#if !defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) && !defined(CONFIG_H3C_HW_C_RAM_READONLY) && !defined(CONFIG_H3C_HW_D_RAM_WEB)
	const char *bootargs;
#endif
	int ret, selector;

	if (argc != 1 && argc != 2)
		return CMD_RET_USAGE;
	if (sfi->flash_type != SMEM_BOOT_NAND_FLASH &&
	    sfi->flash_type != SMEM_BOOT_QSPI_NAND_FLASH) {
		printf("%s partition selection requires NAND boot\n", RT3_BOOT_BOARD_LABEL);
		return CMD_RET_FAILURE;
	}
	ret = rt3_layout_read(&layout);
	if (ret || layout.mode == RT3_LAYOUT_UNSUPPORTED) {
		printf("%s: refusing unsupported SMEM layout\n", RT3_BOOT_BOARD_LABEL);
		return CMD_RET_FAILURE;
	}
	if (argc == 2) {
		target = argv[1];
	} else if (layout.mode == RT3_LAYOUT_SINGLE) {
		target = "rootfs";
	} else {
		/* BOOTCONFIG selects a physical rootfs extent, not an SMEM alias name.
		 * The active table may pair those names with either physical extent.
		 */
		selector = get_bootconfig("rootfs");
		ret = rt3_layout_default_target(&layout, selector, &part);
		if (ret) {
			puts("H3C: invalid default boot selector or layout; enter recovery\n");
			return CMD_RET_FAILURE;
		}
		target = part->name;
	}
	if (argc == 2 || layout.mode == RT3_LAYOUT_SINGLE)
		ret = rt3_layout_find(&layout, target, &part);
	if (ret) {
		printf("%s: boot target must be rootfs or rootfs_1 in this layout\n", RT3_BOOT_BOARD_LABEL);
		return CMD_RET_USAGE;
	}

	sfi->rootfs.offset = part->offset;
	sfi->rootfs.size = part->size;
#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
	/* Final rootfs args are resolved from the selected FIT below. */
#else
	bootargs = !strcmp(part->name, "rootfs") ?
		RT3000B_ROOTFS_BOOTARGS : RT3000B_ROOTFS1_BOOTARGS;
	if (setenv("fsbootargs", bootargs))
		return CMD_RET_FAILURE;
#endif

	printf("%s: RAM-only boot %s, table row %u, rootfs 0x%llx+0x%llx\n",
	       RT3_BOOT_BOARD_LABEL,
	       part->name, part->display_index,
	       (unsigned long long)sfi->rootfs.offset,
	       (unsigned long long)sfi->rootfs.size);
	return 0;
}
#endif /* CONFIG_TARGET_IPQ5018_H3C_RT3000_B */

#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
static u64 h3c_hw_c_cells_u64(const fdt32_t *cells, int count)
{
	u64 value = 0;
	int i;
	for (i = 0; i < count; i++)
		value = (value << 32) | fdt32_to_cpu(cells[i]);
	return value;
}

/* Resolve the selected slot using the labels in the selected FIT FDT. */
#ifdef CONFIG_H3C_HW_D_STORAGE_PORT
volatile int nx30_d_oem_fit_handoff_verified;
#endif

#if !defined(CONFIG_H3C_HW_D_RAM_WEB)
enum h3c_hw_c_fit_profile {
	H3C_HW_C_PROFILE_INVALID = 0,
	H3C_HW_C_PROFILE_NATIVE = 1,
	H3C_HW_C_PROFILE_OEM = 2,
};

static int h3c_hw_c_classify_fit_profile(int has_native_layout,
		int is_native_compatible, int is_oem_compatible,
		const char *config_name, enum h3c_hw_c_fit_profile *profile)
{
	if (!config_name || !profile)
		return -EINVAL;
	*profile = H3C_HW_C_PROFILE_INVALID;
	if (has_native_layout) {
		if (!is_native_compatible)
			return -EINVAL;
		*profile = H3C_HW_C_PROFILE_NATIVE;
		return 0;
	}
	/* OEM FITs may list the native compatible as a fallback; the exact
	 * config and absence of native partition layout select the OEM path. */
	if (!is_oem_compatible || strcmp(config_name, "config@mp03.1-c2"))
		return -EINVAL;
	*profile = H3C_HW_C_PROFILE_OEM;
	return 0;
}
#endif

static int h3c_hw_c_set_fit_rootfs_bootargs(const void *fit, const char *name,
					   int *verified_oem)
{
	const void *fdt, *prop;
	const fdt32_t *reg;
	size_t fdt_size;
	char label[64], bootargs[192], alias[64];
	int conf, image, part, parent, len, reg_len;
	int matches = 0, root_labels = 0, extent_nodes = 0, ac, sc;
#if !defined(CONFIG_H3C_HW_D_RAM_WEB)
	int candidate_oem = 0;
#endif
	uint8_t comp;
	int is_native, is_oem;
#if !defined(CONFIG_H3C_HW_D_RAM_WEB)
	enum h3c_hw_c_fit_profile profile = H3C_HW_C_PROFILE_INVALID;
#endif
	if (!verified_oem)
		return CMD_RET_FAILURE;
	*verified_oem = 0;
#ifdef CONFIG_H3C_HW_D_STORAGE_PORT
	nx30_d_oem_fit_handoff_verified = 0;
#endif

	if (setenv("h3c_hw_c_keep_fit_partitions", "0"))
		return CMD_RET_FAILURE;
	conf = fit_conf_get_node(fit, name);
	if (conf < 0)
		return CMD_RET_FAILURE;
	image = fit_conf_get_prop_node(fit, conf, "fdt");
	if (image < 0 || fit_image_get_comp(fit, image, &comp) ||
	    comp != IH_COMP_NONE ||
	    fit_image_get_data(fit, image, &fdt, &fdt_size) ||
	    fdt_size < sizeof(struct fdt_header) || fdt_check_header(fdt) ||
	    fdt_totalsize(fdt) > fdt_size ||
	    fdt_check_full(fdt, fdt_totalsize(fdt)))
		return CMD_RET_FAILURE;

	prop = fdt_getprop(fdt, 0, "compatible", &len);
	if (!prop)
		return CMD_RET_FAILURE;
	#ifdef CONFIG_H3C_HW_D_RAM_WEB
	is_native = 0; /* This bounded D candidate accepts only checked OEM FITs. */
	is_oem = fdt_stringlist_contains(prop, len, "qcom,ipq5018-mp02.1") == 1;
#define H3C_FIT_OEM_CONFIG "config@mp02.1"
#else
	is_native = fdt_stringlist_contains(prop, len, "h3c,rt3000-rtl8367s") == 1;
	is_oem = fdt_stringlist_contains(prop, len, "qcom,ipq5018-mp03.1-c2") == 1;
#define H3C_FIT_OEM_CONFIG "config@mp03.1-c2"
#endif
	for (part = 0; part >= 0; part = fdt_next_node(fdt, part, NULL)) {
		const char *part_label;
		u64 offset, size;

		prop = fdt_getprop(fdt, part, "label", &len);
		part_label = prop;
		reg = fdt_getprop(fdt, part, "reg", &reg_len);
		if (reg) {
			parent = fdt_parent_offset(fdt, part);
			if (parent >= 0) {
				ac = fdt_address_cells(fdt, parent);
				sc = fdt_size_cells(fdt, parent);
				if (ac == 1 && sc == 1 &&
				    reg_len == 2 * (int)sizeof(*reg)) {
					offset = h3c_hw_c_cells_u64(reg, ac);
					size = h3c_hw_c_cells_u64(reg + ac, sc);
					if (size == 0x02800000 &&
					    (offset == 0x00900000 || offset == 0x03100000))
						extent_nodes++;
				} else {
					offset = ~0ULL;
					size = 0;
				}
			} else {
				offset = ~0ULL;
				size = 0;
			}
		} else {
			offset = ~0ULL;
			size = 0;
		}
		if (!prop || len < 2 || len > (int)sizeof(label) ||
		    ((const char *)prop)[len - 1] != '\0' ||
		    memchr(prop, '\0', len - 1) ||
		    (strcmp(part_label, "rootfs") && strcmp(part_label, "rootfs_1")))
			continue;
		root_labels++;
		if (offset != sfi->rootfs.offset || size != sfi->rootfs.size)
			continue;
		strlcpy(label, part_label, sizeof(label));
		matches++;
	}

#ifndef CONFIG_H3C_HW_D_RAM_WEB
	if (h3c_hw_c_classify_fit_profile(root_labels || extent_nodes,
			is_native, is_oem, name, &profile))
		return CMD_RET_FAILURE;
#endif
	if (root_labels || extent_nodes) {
		const char *expected;
#ifdef CONFIG_H3C_HW_D_RAM_WEB
		if (!is_native || root_labels != 2 || extent_nodes != 2 || matches != 1)
#else
		if (profile != H3C_HW_C_PROFILE_NATIVE || root_labels != 2 ||
		    extent_nodes != 2 || matches != 1)
#endif
			return CMD_RET_FAILURE;
		if (sfi->rootfs.offset == 0x03100000 &&
		    sfi->rootfs.size == 0x02800000)
			expected = "rootfs_1";
		else if (sfi->rootfs.offset == 0x00900000 &&
			 sfi->rootfs.size == 0x02800000)
			expected = "rootfs";
		else
			return CMD_RET_FAILURE;
		if (strcmp(label, expected) ||
		    setenv("h3c_hw_c_keep_fit_partitions", "1"))
			return CMD_RET_FAILURE;
		len = snprintf(bootargs, sizeof(bootargs),
			       "ubi.mtd=%s ubi.block=0,3 root=/dev/ubiblock0_3 rootfstype=squashfs panic=0",
			       label);
	} else {
		struct rt3_layout layout;
		const struct rt3_partition *entry = NULL;
		int i;
#ifdef CONFIG_H3C_HW_D_RAM_WEB
		if (!is_oem || strcmp(name, H3C_FIT_OEM_CONFIG) ||
		    rt3_layout_read(&layout))
#else
		if (profile != H3C_HW_C_PROFILE_OEM || rt3_layout_read(&layout))
#endif
			return CMD_RET_FAILURE;
		for (i = 0; i < layout.count; i++) {
			if (layout.parts[i].offset == sfi->rootfs.offset &&
			    layout.parts[i].size == sfi->rootfs.size) {
				if (entry)
					return CMD_RET_FAILURE;
				entry = &layout.parts[i];
			}
		}
		if (!entry || rt3_layout_linux_mtd_alias(&layout,
			    sfi->rootfs.offset, entry->name, entry->offset,
			    alias, sizeof(alias)) ||
			strcmp(alias, "rootfs"))
			return CMD_RET_FAILURE;
		strlcpy(label, alias, sizeof(label));
		len = snprintf(bootargs, sizeof(bootargs),
			       "ubi.mtd=%s root=mtd:ubi_rootfs rootfstype=squashfs",
			       label);
#ifndef CONFIG_H3C_HW_D_RAM_WEB
		candidate_oem = 1;
#endif
	}
	if (len < 0 || len >= (int)sizeof(bootargs) ||
	    setenv("fsbootargs", bootargs))
		return CMD_RET_FAILURE;
#if !defined(CONFIG_H3C_HW_D_RAM_WEB)
	*verified_oem = candidate_oem;
#endif
	printf("H3C HW-C: FIT %s maps 0x%llx+0x%llx to %s bootargs (%s)\n",
	       name, (unsigned long long)sfi->rootfs.offset,
	       (unsigned long long)sfi->rootfs.size,
	       is_native ? "native" : "OEM", bootargs);
#ifdef CONFIG_H3C_HW_D_STORAGE_PORT
	if (is_oem)
		nx30_d_oem_fit_handoff_verified = 1;
#endif
	return CMD_RET_SUCCESS;
}
#endif

typedef struct {
	unsigned int image_type;
	unsigned int header_vsn_num;
	unsigned int image_src;
	unsigned char *image_dest_ptr;
	unsigned int image_size;
	unsigned int code_size;
	unsigned char *signature_ptr;
	unsigned int signature_size;
	unsigned char *cert_chain_ptr;
	unsigned int cert_chain_size;
} mbn_header_t;

typedef struct {
	unsigned int kernel_load_addr;
	unsigned int kernel_load_size;
} kernel_img_info_t;

kernel_img_info_t kernel_img_info;

char dtb_config_name[64];

#ifdef CONFIG_IPQ_ELF_AUTH
typedef struct {
	unsigned int img_offset;
	unsigned int img_load_addr;
	unsigned int img_size;
} image_info;
#endif

extern bootm_headers_t images;		/* pointers to os/initrd/fdt images */
#ifdef CONFIG_LIST_OF_CONFIG_NAMES_SUPPORT
extern struct config_list config_entries;
#endif

static int boot_os(int argc, char *const argv[])
{

	return do_bootm_states(NULL, 0, argc, argv, BOOTM_STATE_START |
		BOOTM_STATE_FINDOS | BOOTM_STATE_FINDOTHER |
		BOOTM_STATE_LOADOS |
#if defined(CONFIG_PPC) || defined(CONFIG_MIPS)
		BOOTM_STATE_OS_CMDLINE |
#endif
		BOOTM_STATE_OS_PREP | BOOTM_STATE_OS_FAKE_GO |
		BOOTM_STATE_OS_GO, &images, 1);
}

void __stack_chk_fail(void)
{
	printf("stack-protector: U-boot stack is corrupted.\n");
	bad_mode ();
}

#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
static int h3c_hw_c_root_token(const char *token, size_t len)
{
	static const char *prefixes[] = {
		"ubi.mtd=", "ubi.block=", "root=", "rootfstype=",
		"rootflags=", "panic=",
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
		size_t n = strlen(prefixes[i]);
		if (len >= n && !memcmp(token, prefixes[i], n))
			return 1;
	}
	return (len == strlen("rootwait") &&
		!memcmp(token, "rootwait", len)) ||
	       (len >= strlen("rootwait=") &&
		!memcmp(token, "rootwait=", strlen("rootwait=")));
}

static int h3c_hw_c_append_filtered(char *dst, size_t capacity,
					   size_t *used, const char *src, size_t length)
{
	size_t pos = 0;

	while (pos < length) {
		size_t start, token_len;

		while (pos < length && (src[pos] == ' ' || src[pos] == '\t' ||
			       src[pos] == '\r' || src[pos] == '\n'))
			pos++;
		if (pos == length)
			break;
		start = pos;
		while (pos < length && src[pos] != ' ' && src[pos] != '\t' &&
		       src[pos] != '\r' && src[pos] != '\n')
			pos++;
		token_len = pos - start;
		if (h3c_hw_c_root_token(src + start, token_len))
			continue;
		if (*used && dst[*used - 1] != ' ') {
			if (*used + 1 >= capacity)
				return -1;
			dst[(*used)++] = ' ';
		}
		if (token_len >= capacity - *used)
			return -1;
		memcpy(dst + *used, src + start, token_len);
		*used += token_len;
		dst[*used] = '\0';
	}
	return 0;
}
#endif

#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
static int h3c_hw_c_append_oem_cnss_skip4(char *buf, size_t capacity,
					 size_t *used, int verified_oem)
{
#ifndef CONFIG_H3C_HW_C_OEM_CNSS_SKIP4
	(void)buf;
	(void)capacity;
	(void)used;
	(void)verified_oem;
	return 0;
#else
	static const char key[] = "cnss2.skip_radio_bmap";
	size_t pos = 0, key_count = 0, key_len = sizeof(key) - 1;

	if (!verified_oem)
		return 0;
	if (!buf || !used || *used >= capacity || buf[*used] != '\0')
		return -EINVAL;
	while (pos < *used) {
		size_t start, end, value_len;
		while (pos < *used && (buf[pos] == ' ' || buf[pos] == '\t' ||
			       buf[pos] == '\r' || buf[pos] == '\n'))
			pos++;
		if (pos == *used)
			break;
		start = pos;
		while (pos < *used && buf[pos] != ' ' && buf[pos] != '\t' &&
		       buf[pos] != '\r' && buf[pos] != '\n')
			pos++;
		end = pos;
		if (end - start < key_len || memcmp(buf + start, key, key_len))
			continue;
		if (end - start == key_len)
			return -EINVAL;
		if (buf[start + key_len] != '=')
			continue;
		value_len = end - start - key_len - 1;
		if (!value_len)
			return -EINVAL;
		key_count++;
	}
	if (key_count > 1)
		return -EINVAL;
	if (key_count == 1)
		return 0;
	if (*used + (size_t)(*used ? 1 : 0) + key_len + 2 >= capacity)
		return -ENOSPC;
	if (*used)
		buf[(*used)++] = ' ';
	memcpy(buf + *used, key, key_len);
	*used += key_len;
	buf[(*used)++] = '=';
	buf[(*used)++] = '4';
	buf[*used] = '\0';
	return 0;
#endif
}
#endif

#ifdef CONFIG_H3C_HW_C_OEM_CNSS_SKIP4
static char h3c_hw_c_base_bootargs[CONFIG_SYS_CBSIZE];
static char h3c_hw_c_last_bootargs[CONFIG_SYS_CBSIZE];
static int h3c_hw_c_bootargs_snapshot_valid;

static int h3c_hw_c_bootargs_input(const char **input)
{
	const char *current = getenv("bootargs");
	size_t length = current ? strlen(current) : 0;

	if (!input || length >= sizeof(h3c_hw_c_base_bootargs))
		return -E2BIG;
	/* Reuse the original input only while bootargs still equals our last
	 * generated value. A different explicit setenv becomes the new input.
	 * An explicit setenv to the exact same generated bytes is indistinguishable,
	 * but recomposition from base preserves the same effective policy without
	 * duplicating slot/root arguments. */
	if (h3c_hw_c_bootargs_snapshot_valid &&
	    !strcmp(current ? current : "", h3c_hw_c_last_bootargs)) {
		*input = h3c_hw_c_base_bootargs;
		return 0;
	}
	if (length)
		memcpy(h3c_hw_c_base_bootargs, current, length);
	h3c_hw_c_base_bootargs[length] = '\0';
	h3c_hw_c_bootargs_snapshot_valid = 0;
	*input = h3c_hw_c_base_bootargs;
	return 0;
}

static void h3c_hw_c_bootargs_output(const char *generated)
{
	size_t length = strlen(generated);
	if (length >= sizeof(h3c_hw_c_last_bootargs))
		return;
	memcpy(h3c_hw_c_last_bootargs, generated, length + 1);
	h3c_hw_c_bootargs_snapshot_valid = 1;
}
#endif

#ifdef CONFIG_H3C_NAND_ECC_COMPAT
static int h3c_normalize_nand_bootargs(char *cmd_line)
{
    char *normalized = malloc(CONFIG_SYS_CBSIZE);
    unsigned int mode = h3c_nand_ecc_io_mode();
    int ret = CMD_RET_FAILURE;
    if (normalized && mode == nand_info[0].ecc_strength &&
        !h3c_nand_ecc_bootargs(cmd_line, normalized, CONFIG_SYS_CBSIZE,
                             h3c_nand_ecc_spare(), mode)) {
        memcpy(cmd_line, normalized, strlen(normalized) + 1);
        ret = CMD_RET_SUCCESS;
    }
    free(normalized);
    return ret;
}
#endif

static int update_bootargs(void *addr, int config_noffset, int verified_oem)
{
	char *fit_bootargs;
	const char *strings;
	int len, ret = CMD_RET_SUCCESS;
	char * cmd_line = malloc(CONFIG_SYS_CBSIZE);
	if (!cmd_line) {
		printf("bootargs malloc failed \n");
		return CMD_RET_FAILURE;
	}

#ifdef CONFIG_QCA_APPSBL_DLOAD
	if (getenv("dump_to_nvmem"))
		set_minidump_bootargs();
#endif

       	strings = getenv("bootargs");
#ifdef CONFIG_H3C_HW_C_OEM_CNSS_SKIP4
	if (h3c_hw_c_bootargs_input(&strings)) {
		free(cmd_line);
		return CMD_RET_FAILURE;
	}
#endif
	memset(cmd_line, 0, CONFIG_SYS_CBSIZE);
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
	{
		const void *selected_fdt = NULL;
		size_t selected_fdt_size = 0;
		uint8_t comp;
		int fdt_noffset;

		fit_bootargs = (char *)fdt_getprop(addr, 0,
						   FIT_BOOTARGS_PROP, &len);
		if (fit_bootargs && (len < 1 || fit_bootargs[len - 1] != '\0' ||
				     memchr(fit_bootargs, '\0', len - 1))) {
			free(cmd_line);
			return CMD_RET_FAILURE;
		}
		fdt_noffset = fit_conf_get_prop_node(addr, config_noffset,
						     FIT_FDT_PROP);
		if (fdt_noffset >= 0) {
			if (fit_image_get_comp(addr, fdt_noffset, &comp) ||
			    comp != IH_COMP_NONE ||
			    fit_image_get_data(addr, fdt_noffset, &selected_fdt,
					       &selected_fdt_size)) {
				puts("RT3000-B: invalid or compressed FIT FDT\n");
				free(cmd_line);
				return CMD_RET_FAILURE;
			}
		} else if (fdt_noffset != -FDT_ERR_NOTFOUND) {
			puts("RT3000-B: selected FIT configuration has invalid FDT reference\n");
			free(cmd_line);
			return CMD_RET_FAILURE;
		}
		ret = rt3_bootargs_compose(strings, fit_bootargs,
					   getenv("fsbootargs"), selected_fdt,
					   selected_fdt_size, sfi->rootfs.offset,
					   sfi->rootfs.size, cmd_line,
					   CONFIG_SYS_CBSIZE);
		if (ret) {
			printf("RT3000-B: bootargs composition failed (%d)\n", ret);
			free(cmd_line);
			return CMD_RET_FAILURE;
		}
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
        if (h3c_normalize_nand_bootargs(cmd_line)) {
            free(cmd_line);
            return CMD_RET_FAILURE;
        }
#endif
		ret = setenv("bootargs", cmd_line);
		free(cmd_line);
		return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
	}
#endif
	fit_bootargs = (char *)fdt_getprop(addr, 0, FIT_BOOTARGS_PROP, &len);
#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
	{
		size_t used = 0;
		char *rootargs = getenv("fsbootargs");
		size_t base_len = strings ? strlen(strings) : 0;

		if ((strings && base_len >= (size_t)CONFIG_SYS_CBSIZE) ||
		    (strings && h3c_hw_c_append_filtered(cmd_line,
			 CONFIG_SYS_CBSIZE, &used, strings, base_len)))
			ret = CMD_RET_FAILURE;
		if (ret == CMD_RET_SUCCESS && fit_bootargs && len > 0) {
			if (len > CONFIG_SYS_CBSIZE || fit_bootargs[len - 1] != '\0' ||
			    memchr(fit_bootargs, '\0', len - 1) ||
			    h3c_hw_c_append_filtered(cmd_line, CONFIG_SYS_CBSIZE,
					     &used, fit_bootargs, len - 1))
				ret = CMD_RET_FAILURE;
		}
		if (!rootargs)
			ret = CMD_RET_FAILURE;
		if (ret == CMD_RET_SUCCESS) {
			int added = snprintf(cmd_line + used, CONFIG_SYS_CBSIZE - used,
					     "%s%s rootwait",
					     used ? " " : "", rootargs);
			if (added < 0 || added >= (int)(CONFIG_SYS_CBSIZE - used))
				ret = CMD_RET_FAILURE;
			else
				used += added;
		}
		if (ret == CMD_RET_SUCCESS &&
		    h3c_hw_c_append_oem_cnss_skip4(cmd_line, CONFIG_SYS_CBSIZE,
						    &used, verified_oem))
			ret = CMD_RET_FAILURE;
	}
#else
 {
  const char *rootargs = getenv("fsbootargs");
  size_t used = strings ? strlen(strings) : 0;
  int added;
  if (used >= CONFIG_SYS_CBSIZE) ret = CMD_RET_FAILURE;
  if (fit_bootargs) {
   if (len < 1 || len > CONFIG_SYS_CBSIZE || fit_bootargs[len - 1] != '\0' || memchr(fit_bootargs, '\0', len - 1)) ret = CMD_RET_FAILURE;
   rootargs = fit_bootargs;
  }
  if (!rootargs) ret = CMD_RET_FAILURE;
  if (ret == CMD_RET_SUCCESS) {
   if (used) memcpy(cmd_line, strings, used);
   added = snprintf(cmd_line + used, CONFIG_SYS_CBSIZE - used, "%s%s rootwait", used ? " " : "", rootargs);
   if (added < 0 || added >= (int)(CONFIG_SYS_CBSIZE - used)) ret = CMD_RET_FAILURE;
  }
 }

#endif

#ifdef CONFIG_H3C_NAND_ECC_COMPAT
 if (ret == CMD_RET_SUCCESS)
  ret = h3c_normalize_nand_bootargs(cmd_line);
#elif defined(CONFIG_H3C_HW_D_RAM_WEB)
 if (ret == CMD_RET_SUCCESS) {
  char *normalized = malloc(CONFIG_SYS_CBSIZE);
  unsigned int mode = nx30_nor_nand_autoecc_mode();
  if (!normalized || mode != nand_info[0].ecc_strength ||
      nx30_d_ecc_bootargs(cmd_line, normalized, CONFIG_SYS_CBSIZE, 128, mode))
   ret = CMD_RET_FAILURE;
  else memcpy(cmd_line, normalized, strlen(normalized) + 1);
  free(normalized);
 }
#endif
	if (ret == CMD_RET_FAILURE) {
		printf("Env size exceed ...\n");
	} else {
		if (setenv("bootargs", cmd_line))
			ret = CMD_RET_FAILURE;
#ifdef CONFIG_H3C_HW_C_OEM_CNSS_SKIP4
		else
			h3c_hw_c_bootargs_output(cmd_line);
#endif
	}

	free(cmd_line);
	return ret;
}
/*
 * Set the root device and bootargs for mounting root filesystem.
 */
static int set_fs_bootargs(int *fs_on_nand)
{
	char *bootargs;
	unsigned int active_part = 0;
	int ret = 0;
	char boot_args[MAX_BOOT_ARGS_SIZE] = {'\0'};



#define nand_rootfs "ubi.mtd=" QCA_ROOT_FS_PART_NAME " root=mtd:ubi_rootfs rootfstype=squashfs"

	if (sfi->flash_type == SMEM_BOOT_SPI_FLASH) {
		if (get_which_flash_param("rootfs") ||
		    ((sfi->flash_secondary_type == SMEM_BOOT_NAND_FLASH) ||
			(sfi->flash_secondary_type == SMEM_BOOT_QSPI_NAND_FLASH))) {
			bootargs = nand_rootfs;
			*fs_on_nand = 1;

			if (sfi->rootfs.offset == 0xBAD0FF5E) {
				if(smem_bootconfig_info() == 0)
					active_part = get_rootfs_active_partition();

				sfi->rootfs.offset = active_part * IPQ_NAND_ROOTFS_SIZE;
				sfi->rootfs.size = IPQ_NAND_ROOTFS_SIZE;
			}

			fdt_setprop((void *)gd->fdt_blob, 0, "nor_nand_available",
				    fs_on_nand, sizeof(int));
			snprintf(mtdids, sizeof(mtdids),
				 "nand%d=nand%d,nand%d=" QCA_SPI_NOR_DEVICE,
				 is_spi_nand_available(),
				 is_spi_nand_available(),
				CONFIG_SPI_FLASH_INFO_IDX
				);

			if (getenv("fsbootargs") == NULL)
				setenv("fsbootargs", bootargs);
		} else {
			bootargs = boot_args;
			if (smem_bootconfig_info() == 0) {
				active_part = get_rootfs_active_partition();
				// TODO: AP8220 rootfs1 rootfs2
				if (active_part) {
					strlcpy(bootargs, "rootfsname=rootfs_1", sizeof(boot_args));
					if (sfi->rootfs.offset == 0xBAD0FF5E)
						ret  = set_uuid_bootargs(bootargs, "rootfs_1", sizeof(boot_args), false);
				} else {
					strlcpy(bootargs, "rootfsname=rootfs", sizeof(boot_args));
					if (sfi->rootfs.offset == 0xBAD0FF5E)
						ret  = set_uuid_bootargs(bootargs, "rootfs", sizeof(boot_args), false);
				}
			} else {
				strlcpy(bootargs, "rootfsname=rootfs", sizeof(boot_args));
				if (sfi->rootfs.offset == 0xBAD0FF5E)
					ret  = set_uuid_bootargs(bootargs, "rootfs", sizeof(boot_args), false);
			}

			if (ret)
				return ret;

			*fs_on_nand = 0;

			snprintf(mtdids, sizeof(mtdids), "nand%d="
				QCA_SPI_NOR_DEVICE, CONFIG_SPI_FLASH_INFO_IDX);

			if (getenv("fsbootargs") == NULL)
				setenv("fsbootargs", bootargs);
		}
	} else if (((sfi->flash_type == SMEM_BOOT_NAND_FLASH) ||
			(sfi->flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {
		bootargs = nand_rootfs;
		if (getenv("fsbootargs") == NULL)
			setenv("fsbootargs", bootargs);
		*fs_on_nand = 1;

		snprintf(mtdids, sizeof(mtdids), "nand0=nand0");

#ifdef CONFIG_QCA_MMC
	} else if (sfi->flash_type == SMEM_BOOT_MMC_FLASH) {
		bootargs = boot_args;
		if (smem_bootconfig_info() == 0) {
			active_part = get_rootfs_active_partition();
			if (active_part) {
				strlcpy(bootargs, "rootfsname=rootfs_1 gpt", sizeof(boot_args));
				ret  = set_uuid_bootargs(bootargs, "rootfs_1", sizeof(boot_args), true);
			} else {
				strlcpy(bootargs, "rootfsname=rootfs gpt", sizeof(boot_args));
				bootargs = "rootfsname=rootfs gpt";
				ret  = set_uuid_bootargs(bootargs, "rootfs", sizeof(boot_args), true);
			}
		} else {
			strlcpy(bootargs, "rootfsname=rootfs gpt", sizeof(boot_args));
			ret  = set_uuid_bootargs(bootargs, "rootfs", sizeof(boot_args), true);
		}

		if (ret)
			return ret;

		*fs_on_nand = 0;
		if (getenv("fsbootargs") == NULL)
			setenv("fsbootargs", bootargs);
#endif
	} else {
		printf("bootipq: unsupported boot flash type\n");
		return -EINVAL;
	}

	return 0;
}

int config_select(unsigned int addr, char *rcmd, int rcmd_size)
{
	/* Selecting a config name from the list of available
	 * config names by passing them to the fit_conf_get_node()
	 * function which is used to get the node_offset with the
	 * config name passed. Based on the return value index based
	 * or board name based config is used.
	 */

#ifdef CONFIG_ARCH_IPQ806x
	int soc_version = 0;
#endif
	int i, strings_count, ret, config_noffset;
	int verified_oem = 0;
	const char *config = getenv("config_name");

	if (config) {
		printf("Manual device tree config selected!\n");
		strlcpy(dtb_config_name, config, sizeof(dtb_config_name));
		config_noffset = fit_conf_get_node((void *)addr, dtb_config_name);
		if (config_noffset >= 0) {
			#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
			if (h3c_hw_c_set_fit_rootfs_bootargs((void *)addr, dtb_config_name,
						      &verified_oem))
				goto fail;
#endif
			ret = update_bootargs((void *)addr, config_noffset, verified_oem);
			if (ret)
				goto fail;
			snprintf(rcmd, rcmd_size, "0x%x#%s",
				 addr, dtb_config_name);
			return 0;
		}
	} else {
#ifdef CONFIG_LIST_OF_CONFIG_NAMES_SUPPORT
		strings_count = config_entries.no_of_entries;
#else
		strings_count = fdt_count_strings(gd->fdt_blob, 0,
							"config_name");
#endif

		if (!strings_count) {
			printf("Failed to get config_name\n");
			return -1;
		}

		for (i = 0; i < strings_count; i++) {
#ifdef CONFIG_LIST_OF_CONFIG_NAMES_SUPPORT
			config = config_entries.entry[i];
#else
			fdt_get_string_index(gd->fdt_blob, 0, "config_name",
					   i, &config);
#endif
			snprintf((char *)dtb_config_name,
				 sizeof(dtb_config_name), "%s", config);

#ifdef CONFIG_ARCH_IPQ806x
			ipq_smem_get_socinfo_version((uint32_t *)&soc_version);
			if(SOCINFO_VERSION_MAJOR(soc_version) >= 2) {
				snprintf(dtb_config_name + strlen("config@"),
					 sizeof(dtb_config_name) - strlen("config@"),
					 "v%d.0-%s",
					 SOCINFO_VERSION_MAJOR(soc_version),
					 config + strlen("config@"));
			}
#endif
			config_noffset = fit_conf_get_node((void *)addr, dtb_config_name);
			if (config_noffset >= 0) {
				#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
			if (h3c_hw_c_set_fit_rootfs_bootargs((void *)addr, dtb_config_name,
						      &verified_oem))
				goto fail;
#endif
			ret = update_bootargs((void *)addr, config_noffset, verified_oem);
				if (ret)
					goto fail;
				snprintf(rcmd, rcmd_size, "0x%x#%s",
					 addr, dtb_config_name);
				return 0;
			}
		}
	}

	printf("Config not available\n");
fail:
	return -1;
}

__weak int switch_ce_channel_buf(unsigned int channel_id)
{
	return 0;
}

#ifdef CONFIG_IPQ_ELF_AUTH
static int parse_elf_image_phdr(image_info *img_info, unsigned int addr)
{
	Elf32_Ehdr *ehdr; /* Elf header structure pointer */
	Elf32_Phdr *phdr; /* Program header structure pointer */
	int i;

	ehdr = (Elf32_Ehdr *)addr;
	phdr = (Elf32_Phdr *)(addr + ehdr->e_phoff);

	if (!IS_ELF(*ehdr)) {
		printf("It is not a elf image \n");
		return -EINVAL;
	}

	if (ehdr->e_type != ET_EXEC) {
		printf("Not a valid elf image\n");
		return -EINVAL;
	}

	/* Load each program header */
	for (i = 0; i < NO_OF_PROGRAM_HDRS; ++i) {
		printf("Parsing phdr load addr 0x%x offset 0x%x size 0x%x type 0x%x\n",
		      phdr->p_paddr, phdr->p_offset, phdr->p_filesz, phdr->p_type);
		if(phdr->p_type == PT_LOAD) {
			img_info->img_offset = phdr->p_offset;
			img_info->img_load_addr = phdr->p_paddr;
			img_info->img_size =  phdr->p_filesz;
			return 0;
		}
		++phdr;
	}

	return -EINVAL;
}
#endif

#ifndef CONFIG_DISABLE_SIGNED_BOOT
static int copy_rootfs(unsigned int request, uint32_t size)
{
	char runcmd[256];
#ifdef CONFIG_QCA_MMC
	int ret;
	block_dev_desc_t *blk_dev;
	disk_partition_t disk_info;
	unsigned int active_part = 0;
#endif

	if (ipq_fs_on_nand) {
		snprintf(runcmd, sizeof(runcmd),
			"ubi read 0x%x ubi_rootfs &&", request);
#ifdef CONFIG_QCA_MMC
	} else if (sfi->flash_type == SMEM_BOOT_MMC_FLASH ||
			((sfi->flash_type == SMEM_BOOT_SPI_FLASH) &&
			(sfi->rootfs.offset == 0xBAD0FF5E))) {
		blk_dev = mmc_get_dev(host->dev_num);
		if (smem_bootconfig_info() == 0) {
			active_part = get_rootfs_active_partition();
			if (active_part) {
				ret = get_partition_info_efi_by_name(blk_dev,
						"rootfs_1", &disk_info);
			} else {
				ret = get_partition_info_efi_by_name(blk_dev,
						"rootfs", &disk_info);
			}
		}else {
			ret = get_partition_info_efi_by_name(blk_dev,
					"rootfs", &disk_info);
		}
		if(ret == 0)
			snprintf(runcmd, sizeof(runcmd), "mmc read 0x%x 0x%X 0x%X &&",
					request, (uint)disk_info.start,
					(uint)(size / disk_info.blksz) + 1);
		else
			return CMD_RET_FAILURE;
#endif
	} else {
		snprintf(runcmd, sizeof(runcmd),
			"sf read 0x%x 0x%x 0x%x && ",
			request, (uint)sfi->rootfs.offset, (uint)sfi->rootfs.size);
	}
	if (debug)
		printf("runcmd: %s\n", runcmd);
	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	return 0;
}

#ifndef CONFIG_IPQ_ELF_AUTH
static int authenticate_rootfs(unsigned int kernel_addr)
{
	unsigned int kernel_imgsize;
	unsigned int request;
	int ret;
	mbn_header_t *mbn_ptr;
	struct {
		unsigned long type;
		unsigned long size;
		unsigned long addr;
	} rootfs_img_info;

	request = CONFIG_ROOTFS_LOAD_ADDR;
	rootfs_img_info.addr = CONFIG_ROOTFS_LOAD_ADDR;
	rootfs_img_info.type = SEC_AUTH_SW_ID;
	request += sizeof(mbn_header_t);/* space for mbn header */

	/* get , kernel size = header + kernel + certificate */
	mbn_ptr = (mbn_header_t *) kernel_addr;
	kernel_imgsize = mbn_ptr->image_size + sizeof(mbn_header_t);

	/* get rootfs MBN header and validate it */
	mbn_ptr = (mbn_header_t *)((uint32_t)mbn_ptr + kernel_imgsize);
	if (mbn_ptr->image_type != ROOTFS_IMAGE_TYPE &&
			(mbn_ptr->code_size + mbn_ptr->signature_size +
			 mbn_ptr->cert_chain_size != mbn_ptr->image_size))
		return CMD_RET_FAILURE;

	/* pack, MBN header + rootfs + certificate */
	/* copy rootfs from the boot device */
	copy_rootfs(request, mbn_ptr->code_size);

	/* copy rootfs MBN header */
	memcpy((void *)CONFIG_ROOTFS_LOAD_ADDR, (void *)kernel_addr + kernel_imgsize,
			sizeof(mbn_header_t));
	/* copy rootfs certificate */
	memcpy((void *)request + mbn_ptr->code_size,
		(void *)kernel_addr + kernel_imgsize + sizeof(mbn_header_t),
		mbn_ptr->signature_size + mbn_ptr->cert_chain_size);

	/* copy rootfs size */
	rootfs_img_info.size = sizeof(mbn_header_t) + mbn_ptr->image_size;

	ret = qca_scm_secure_authenticate(&rootfs_img_info, sizeof(rootfs_img_info));

	memset((void *)kernel_img_info.kernel_load_addr,  0, sizeof(mbn_header_t));
	memset(mbn_ptr,  0,
		(sizeof(mbn_header_t) + mbn_ptr->signature_size + mbn_ptr->cert_chain_size));

	if (ret)
		return CMD_RET_FAILURE;

	return CMD_RET_SUCCESS;
}
#else

static int authenticate_rootfs_elf(unsigned int rootfs_hdr)
{
	int ret;
	unsigned int request;
	image_info img_info;
	struct {
		unsigned long type;
		unsigned long size;
		unsigned long addr;
	} rootfs_img_info;

	if (parse_elf_image_phdr(&img_info, rootfs_hdr))
		return CMD_RET_FAILURE;

	request = img_info.img_load_addr;
	memcpy((void*)request, (void*)rootfs_hdr, img_info.img_offset);

	/* copy rootfs from the boot device */
	copy_rootfs(request + img_info.img_offset, img_info.img_size);

	rootfs_img_info.addr = request;
	rootfs_img_info.type = SEC_AUTH_SW_ID;
	rootfs_img_info.size = img_info.img_offset + img_info.img_size;
	ret = qca_scm_secure_authenticate(&rootfs_img_info, sizeof(rootfs_img_info));
	memset((void *)rootfs_hdr, 0, img_info.img_offset);
	if (ret)
		return CMD_RET_FAILURE;

	return CMD_RET_SUCCESS;
}
#endif

static int do_boot_signedimg(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	char runcmd[256];
	char * const arg[1] = {runcmd};
	int ret;
	unsigned int request;
#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
	int part = PRIMARY_PARTITION;
#endif
#ifdef CONFIG_QCA_MMC
	block_dev_desc_t *blk_dev;
	disk_partition_t disk_info;
	unsigned int active_part = 0;
#endif
#ifdef CONFIG_IPQ_ELF_AUTH
	image_info img_info;
#endif

	if (argc == 2 && strncmp(argv[1], "debug", 5) == 0)
		debug = 1;

	if ((ret = set_fs_bootargs(&ipq_fs_on_nand)))
		return ret;

	/* check the smem info to see which flash used for booting */
	if (sfi->flash_type == SMEM_BOOT_SPI_FLASH) {
		if (debug) {
			printf("Using nand device %d\n", CONFIG_SPI_FLASH_INFO_IDX);
		}
	} else if (((sfi->flash_type == SMEM_BOOT_NAND_FLASH) ||
		(sfi->flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {
		if (debug) {
			printf("Using nand device 0\n");
		}
	} else if (sfi->flash_type == SMEM_BOOT_MMC_FLASH) {
		if (debug) {
			printf("Using MMC device\n");
		}
	} else {
		printf("Unsupported BOOT flash type\n");
		return -1;
	}
	if (debug) {
		run_command("printenv bootargs", 0);
		printf("Booting from flash\n");
	}

	request = CONFIG_SYS_LOAD_ADDR;
	kernel_img_info.kernel_load_addr = request;

#ifdef CONFIG_VERSION_ROLLBACK_PARTITION_INFO
	if (smem_bootconfig_info() == 0){
		ret = get_rootfs_active_partition();
		if (ret){
			part = SECONDARY_PARTITION;
		}
	}
	ret = qca_scm_part_info(&part, sizeof(part));
	if (ret) {
		printf(" Partition info authentication failed \n");
		BUG();
	}
#endif
	if (ipq_fs_on_nand) {
#ifdef CONFIG_CMD_UBI
		/*
		 * The kernel will be available inside a UBI volume
		 */
		snprintf(runcmd, sizeof(runcmd),
			 "nand device %d && "
			 "setenv mtdids nand%d=nand%d && "
			 "setenv mtdparts mtdparts=nand%d:0x%llx@0x%llx(fs),${msmparts} && "
			 "ubi part fs && ", is_spi_nand_available(),
			 is_spi_nand_available(),
			 is_spi_nand_available(),
			 is_spi_nand_available(),
			 sfi->rootfs.size, sfi->rootfs.offset);

		if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
			return CMD_RET_FAILURE;

#ifdef CONFIG_IPQ_ELF_AUTH
		snprintf(runcmd, sizeof(runcmd),
			 "ubi read 0x%x kernel 0x%x && ",
			 request, ELF_HDR_PLUS_PHDR_SIZE);

		if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
			return CMD_RET_FAILURE;

		if (parse_elf_image_phdr(&img_info, request))
			return CMD_RET_FAILURE;

		request = img_info.img_load_addr - img_info.img_offset;
#endif
		snprintf(runcmd, sizeof(runcmd),
			 "ubi read 0x%x kernel && ", request);

		if (debug)
			printf("%s", runcmd);

		if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
			return CMD_RET_FAILURE;

		kernel_img_info.kernel_load_size =
			(unsigned int)ubi_get_volume_size("kernel");
#endif
#ifdef CONFIG_QCA_MMC
	} else if (sfi->flash_type == SMEM_BOOT_MMC_FLASH ||
			((sfi->flash_type == SMEM_BOOT_SPI_FLASH) &&
			(sfi->rootfs.offset == 0xBAD0FF5E))) {
		blk_dev = mmc_get_dev(host->dev_num);
		if (smem_bootconfig_info() == 0) {
			active_part = get_rootfs_active_partition();
			if (active_part) {
				ret = get_partition_info_efi_by_name(blk_dev,
						"0:HLOS_1", &disk_info);
			} else {
				ret = get_partition_info_efi_by_name(blk_dev,
						"0:HLOS", &disk_info);
			}
		} else {
			ret = get_partition_info_efi_by_name(blk_dev,
						"0:HLOS", &disk_info);
		}

		if (ret == 0) {
#ifdef CONFIG_IPQ_ELF_AUTH
			snprintf(runcmd, sizeof(runcmd), "mmc read 0x%x 0x%X 0x%X",
				 CONFIG_SYS_LOAD_ADDR,
				 (uint)disk_info.start, ELF_HDR_PLUS_PHDR_SIZE);

			if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
				return CMD_RET_FAILURE;

			if (parse_elf_image_phdr(&img_info, request))
				return CMD_RET_FAILURE;

			request = img_info.img_load_addr - img_info.img_offset;
#endif
			snprintf(runcmd, sizeof(runcmd), "mmc read 0x%x 0x%X 0x%X",
				 request,
				 (uint)disk_info.start, (uint)disk_info.size);

			if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
				return CMD_RET_FAILURE;

			kernel_img_info.kernel_load_size = disk_info.size * disk_info.blksz;
		}
#endif
	} else {
		/*
		 * Kernel is in a separate partition
		 */
		snprintf(runcmd, sizeof(runcmd), "sf probe &&");

		if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
			return CMD_RET_FAILURE;

#ifdef CONFIG_IPQ_ELF_AUTH
		snprintf(runcmd, sizeof(runcmd),
			 "sf read 0x%x 0x%x 0x%x && ",
			 CONFIG_SYS_LOAD_ADDR,
			 (uint)sfi->hlos.offset, ELF_HDR_PLUS_PHDR_SIZE);

		if (debug)
			printf("%s", runcmd);

		if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
			return CMD_RET_FAILURE;

		if (parse_elf_image_phdr(&img_info, request))
			return CMD_RET_FAILURE;

		request = img_info.img_load_addr - img_info.img_offset;
#endif
		snprintf(runcmd, sizeof(runcmd),
			 "sf read 0x%x 0x%x 0x%x && ",
			 request,
			 (uint)sfi->hlos.offset, (uint)sfi->hlos.size);

		if (debug)
			printf("%s", runcmd);

		if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
			return CMD_RET_FAILURE;

		kernel_img_info.kernel_load_size =  sfi->hlos.size;
	}

	setenv("mtdids", mtdids);

#ifndef CONFIG_IPQ_ELF_AUTH
	mbn_header_t * mbn_ptr = (mbn_header_t *) request;
	request += sizeof(mbn_header_t);
#else
	kernel_img_info.kernel_load_addr = request;
	request = img_info.img_load_addr;
#endif

	/* This sys call will switch the CE1 channel to register usage */
	ret = switch_ce_channel_buf(0);

	if (ret)
		return CMD_RET_FAILURE;

	ret = qca_scm_auth_kernel(&kernel_img_info,
			sizeof(kernel_img_info));
	if (ret) {
		printf("Kernel image authentication failed \n");
		BUG();
	}
#ifndef CONFIG_IPQ_ELF_AUTH
	memset((void *)mbn_ptr->signature_ptr, 0,(mbn_ptr->signature_size + mbn_ptr->cert_chain_size));
#else
	memset((void *)kernel_img_info.kernel_load_addr,  0, img_info.img_offset);
#endif
	if (getenv("rootfs_auth")) {
#ifdef CONFIG_IPQ_ELF_AUTH
		if (authenticate_rootfs_elf(img_info.img_load_addr +
				img_info.img_size) != CMD_RET_SUCCESS) {
			printf("Rootfs elf image authentication failed\n");
			BUG();
		}
#else
		/* Rootfs's header and certificate at end of kernel image, copy from
		 * there and pack with rootfs image and authenticate rootfs */
		if (authenticate_rootfs(CONFIG_SYS_LOAD_ADDR) != CMD_RET_SUCCESS) {
			printf("Rootfs image authentication failed\n");
			BUG();
		}
#endif
	}

#ifdef CONFIG_SKIP_RESET
	if (apps_iscrashed())
		return 1;
#endif

	/*
	* This sys call will switch the CE1 channel to ADM usage
	* so that HLOS can use it.
	*/
	ret = switch_ce_channel_buf(1);

	if (ret)
		return CMD_RET_FAILURE;

	dcache_enable();

	ret = config_select(request, runcmd, sizeof(runcmd));

	if (debug)
		printf("%s", runcmd);

	if (ret < 0 || boot_os(1, arg) != CMD_RET_SUCCESS) {
#ifdef CONFIG_QCA_MMC
		mmc_initialize(gd->bd);
#endif
#ifdef CONFIG_USB_XHCI_IPQ
		ipq_board_usb_init();
#endif
		dcache_disable();
		return CMD_RET_FAILURE;
	}

#ifndef CONFIG_QCA_APPSBL_DLOAD
	reset_crashdump();
#endif
	return CMD_RET_SUCCESS;
}
#endif

static int do_boot_unsignedimg(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;
	char runcmd[256];
	char * const arg[1] = {runcmd};
#ifdef CONFIG_QCA_MMC
	block_dev_desc_t *blk_dev;
	disk_partition_t disk_info;
	unsigned int active_part = 0;
#endif
#ifdef CONFIG_IPQ_ELF_AUTH
	image_info img_info;
#endif

	if (argc == 2 && strncmp(argv[1], "debug", 5) == 0)
		debug = 1;

	if ((ret = set_fs_bootargs(&ipq_fs_on_nand)))
		return ret;

	if (debug) {
		run_command("printenv bootargs", 0);
		printf("Booting from flash\n");
	}

	if (((sfi->flash_type == SMEM_BOOT_NAND_FLASH) ||
			(sfi->flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {
		if (debug) {
			printf("Using nand device 0\n");
		}

		/*
		 * The kernel is in seperate partition
		 */
		if (sfi->rootfs.offset == 0xBAD0FF5E) {
			printf(" bad offset of hlos");
			return -1;
		}

		snprintf(runcmd, sizeof(runcmd),
			 "setenv mtdids nand0=nand0 && "
			 "setenv mtdparts mtdparts=nand0:0x%llx@0x%llx(fs),${msmparts} && "
			 "ubi part fs && "
			 "ubi read 0x%x kernel && ",
			 sfi->rootfs.size, sfi->rootfs.offset,
			 CONFIG_SYS_LOAD_ADDR);

	} else if (((sfi->flash_type == SMEM_BOOT_SPI_FLASH) &&
		    (sfi->rootfs.offset != 0xBAD0FF5E)) ||
		   ipq_fs_on_nand) {
		if (get_which_flash_param("rootfs") || ipq_fs_on_nand) {
			snprintf(runcmd, sizeof(runcmd),
				 "nand device %d && "
				 "setenv mtdids nand%d=nand%d && "
				 "setenv mtdparts mtdparts=nand%d:0x%llx@0x%llx(fs),${msmparts} && "
				 "ubi part fs && "
				 "ubi read 0x%x kernel && ",
				 is_spi_nand_available(),
				 is_spi_nand_available(),
				 is_spi_nand_available(),
				 is_spi_nand_available(),
				 sfi->rootfs.size, sfi->rootfs.offset,
				 CONFIG_SYS_LOAD_ADDR);
		} else {
			/*
			 * Kernel is in a separate partition
			 */
			snprintf(runcmd, sizeof(runcmd),
				 "sf probe &&"
				 "sf read 0x%x 0x%x 0x%x && ",
				 CONFIG_SYS_LOAD_ADDR, (uint)sfi->hlos.offset, (uint)sfi->hlos.size);
		}
#ifdef CONFIG_QCA_MMC
	} else if ((sfi->flash_type == SMEM_BOOT_MMC_FLASH) ||
			((sfi->flash_type == SMEM_BOOT_SPI_FLASH) &&
			(sfi->rootfs.offset == 0xBAD0FF5E))) {
		if (debug) {
			printf("Using MMC device\n");
		}
		blk_dev = mmc_get_dev(host->dev_num);
		if (smem_bootconfig_info() == 0) {
			active_part = get_rootfs_active_partition();
			if (active_part) {
				ret = get_partition_info_efi_by_name(blk_dev,
						"0:HLOS_1", &disk_info);
			} else {
				ret = get_partition_info_efi_by_name(blk_dev,
						"0:HLOS", &disk_info);
			}
		} else {
			ret = get_partition_info_efi_by_name(blk_dev,
						"0:HLOS", &disk_info);
		}

		if (ret == 0) {
			snprintf(runcmd, sizeof(runcmd), "mmc read 0x%x 0x%x 0x%x",
				 CONFIG_SYS_LOAD_ADDR,
				 (uint)disk_info.start, (uint)disk_info.size);
		}

#endif   /* CONFIG_QCA_MMC   */
	} else {
		printf("Unsupported BOOT flash type\n");
		return -1;
	}

	if (run_command(runcmd, 0) != CMD_RET_SUCCESS) {
#ifdef CONFIG_QCA_MMC
		mmc_initialize(gd->bd);
#endif
		return CMD_RET_FAILURE;
	}

	dcache_enable();

	setenv("mtdids", mtdids);

	ret = genimg_get_format((void *)CONFIG_SYS_LOAD_ADDR);
	if (ret == IMAGE_FORMAT_FIT) {
		ret = config_select(CONFIG_SYS_LOAD_ADDR,
				    runcmd, sizeof(runcmd));
	} else if (ret == IMAGE_FORMAT_LEGACY) {
		snprintf(runcmd, sizeof(runcmd),
			 "0x%x", CONFIG_SYS_LOAD_ADDR);
	} else {
		ret = genimg_get_format((void *)CONFIG_SYS_LOAD_ADDR +
					sizeof(mbn_header_t));
		if (ret == IMAGE_FORMAT_FIT) {
			ret = config_select((CONFIG_SYS_LOAD_ADDR
					     + sizeof(mbn_header_t)),
					    runcmd, sizeof(runcmd));
#ifdef CONFIG_IPQ_ELF_AUTH
		} else if (!parse_elf_image_phdr(&img_info,
					CONFIG_SYS_LOAD_ADDR)) {
			ret = config_select((CONFIG_SYS_LOAD_ADDR +
						img_info.img_offset),
					    runcmd, sizeof(runcmd));
#endif
		} else if (ret == IMAGE_FORMAT_LEGACY) {
			snprintf(runcmd, sizeof(runcmd),
				 "0x%x", (CONFIG_SYS_LOAD_ADDR +
						  sizeof(mbn_header_t)));

		} else {
			dcache_disable();
			return CMD_RET_FAILURE;
		}
	}

#ifdef CONFIG_SKIP_RESET
	if (apps_iscrashed())
		return 1;
#endif

	if (ret < 0 || boot_os(1, arg) != CMD_RET_SUCCESS) {
#ifdef CONFIG_USB_XHCI_IPQ
		ipq_board_usb_init();
#endif
		dcache_disable();
		return CMD_RET_FAILURE;
	}
#ifndef CONFIG_QCA_APPSBL_DLOAD
	reset_crashdump();
#endif
	return CMD_RET_SUCCESS;
}

static int do_bootipq(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;
#ifndef CONFIG_DISABLE_SIGNED_BOOT
	char buf = 0;
#endif
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
 if (!h3c_nand_ecc_ready()) {
  puts("H3C NAND: format unknown or conflicting; refuse kernel boot\n");
  ret = CMD_RET_FAILURE;
  goto boot_done;
 }
#elif defined(CONFIG_H3C_HW_D_RAM_AUTOECC)
 if (!nx30_nor_nand_autoecc_mode()) {
  puts("NX30 D: unknown or mixed sampled ECC; refuse kernel boot\n");
  ret = CMD_RET_FAILURE;
  goto boot_done;
 }
#endif
	ret = rt3000b_prepare_boot_slot(argc, argv);
	if (ret)
		goto boot_done;
#endif
	/*
	 * set fdt_high parameter so that u-boot will not load
	 * dtb above CONFIG_IPQ40XX_FDT_HIGH region.
	 */
	if (run_command("setenv fdt_high " MK_STR(CONFIG_IPQ_FDT_HIGH) "\n", 0)
	    != CMD_RET_SUCCESS) {
#ifdef CONFIG_H3C_RELEASE_SYSTEM_DEFAULT
		ret = CMD_RET_FAILURE;
		goto boot_done;
#else
		return CMD_RET_FAILURE;
#endif
	}

#if defined(CONFIG_IPQ9574_EDMA) || defined(CONFIG_IPQ5332_EDMA)
	aquantia_phy_reset_init_done();
#endif

#ifndef CONFIG_DISABLE_SIGNED_BOOT
	ret = qca_scm_call(SCM_SVC_FUSE, QFPROM_IS_AUTHENTICATE_CMD, &buf, sizeof(char));

	/*
	|| if atf is enable in env ,do_boot_signedimg is skip.
	|| Note: This features currently support in ipq50XX.
	*/
	if (ret == 0 && buf == 1 && !is_atf_enabled()) {
		ret = do_boot_signedimg(cmdtp, flag, argc, argv);
	} else if (ret == 0 || ret == -EOPNOTSUPP)
#endif
	{
		ret = do_boot_unsignedimg(cmdtp, flag, argc, argv);
	}

#if defined(CONFIG_H3C_RELEASE_SYSTEM_DEFAULT) || defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
boot_done:
#endif
#ifdef CONFIG_H3C_RELEASE_SYSTEM_DEFAULT
	if (h3c_boot_load_failed(ret)) {
		puts("bootipq failed, enter release web recovery\n");
		h3c_boot_recovery();
	}
#else
	if (ret == CMD_RET_FAILURE) {
#ifdef CONFIG_HTTPD
	puts("bootipq failed, enter web failsafe mode\n");
	run_command("httpd", 0);
	cli_loop();
#else
#if !defined(CONFIG_IPQ5332) && !defined(CONFIG_IPQ9574)
#ifdef CONFIG_IPQ_ETH_INIT_DEFER
		puts("\nNet:   ");
		eth_initialize();
#endif /* CONFIG_IPQ_ETH_INIT_DEFER */
#endif /* CONFIG_IPQ5332, CONFIG_IPQ9574 */
#endif /* CONFIG_HTTPD */
	}

#endif /* CONFIG_H3C_RELEASE_SYSTEM_DEFAULT */
	return ret;
}

#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)
U_BOOT_CMD(bootipq, 2, 0, do_bootipq,
	   "bootipq from flash device",
	   "bootipq [rootfs|rootfs_1] - Default BOOTCONFIG slot or named partition\n"
	   "  Selection is RAM-only; persistent state is not changed\n");
#else
U_BOOT_CMD(bootipq, 2, 0, do_bootipq,
	   "bootipq from flash device",
	   "bootipq [debug] - Load image(s) and boots the kernel\n");
#endif
