// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RT3_INITRAMFS_HOST_TEST
#include <rt3000_b_initramfs.h>

static uintptr_t copied_destination, copied_source;
static size_t copied_size;
static int copy_calls;

static bool available(uintptr_t start, size_t size)
{
	return start == RT3_HW_B_INITRAMFS_STAGE_START &&
		size <= RT3_HW_B_INITRAMFS_STAGE_END - start;
}

static void copy_image(uintptr_t destination, uintptr_t source, size_t size)
{
	copied_destination = destination;
	copied_source = source;
	copied_size = size;
	copy_calls++;
}

static void set_be32(uint8_t *p, uint32_t value)
{
	p[0] = value >> 24;
	p[1] = value >> 16;
	p[2] = value >> 8;
	p[3] = value;
}

int main(void)
{
	uint8_t fit_header[40] = { 0 };
	char ram_args[256], too_small[12];
	struct rt3_hw_b_initramfs_validation validation = { 0 };
	uintptr_t staged = 0;
	const uintptr_t source = 0x41000098UL;
	const size_t image_size = 11U * 1024U * 1024U;

	set_be32(fit_header, 0xd00dfeedU);
	set_be32(fit_header + 4, sizeof(fit_header));
	assert(rt3_hw_b_fit_extent_valid(fit_header, sizeof(fit_header)));
	assert(!rt3_hw_b_fit_extent_valid(fit_header, sizeof(fit_header) - 1));
	assert(!rt3_hw_b_fit_extent_valid(fit_header, sizeof(fit_header) + 1));
	set_be32(fit_header + 4, RT3_HW_B_HTTP_INITRAMFS_LIMIT + 1);
	assert(!rt3_hw_b_fit_extent_valid(fit_header, sizeof(fit_header)));
	set_be32(fit_header, 0x12345678U);
	set_be32(fit_header + 4, sizeof(fit_header));
	assert(!rt3_hw_b_fit_extent_valid(fit_header, sizeof(fit_header)));

	assert(rt3_hw_b_http_content_length_allowed(
		RT3_HW_B_HTTP_INITRAMFS_LIMIT, true));
	assert(!rt3_hw_b_http_content_length_allowed(
		RT3_HW_B_HTTP_INITRAMFS_LIMIT + 1, true));
	assert(rt3_hw_b_http_content_length_allowed(
		RT3_HW_B_HTTP_UPLOAD_LIMIT, false));
	assert(!rt3_hw_b_http_content_length_allowed(
		RT3_HW_B_HTTP_UPLOAD_LIMIT + 1, false));
	assert(rt3_hw_b_ram_bootargs(
		"console=ttyMSM0,115200n8 ubi.mtd=5 root=mtd:rootfs_1 "
		"rootfstype=ubifs rootwait ubi.mtd=rootfs root=/dev/ubiblock0_1 "
		"cnss2.skip_radio_bmap=4 mtdparts=nand:1m(boot) rdinit=/bin/sh",
		ram_args, sizeof(ram_args)));
	assert(!strcmp(ram_args,
		"console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 "
		"mtdparts=nand:1m(boot) rdinit=/init"));
	assert(!rt3_hw_b_ram_bootargs("console=long", too_small, sizeof(too_small)));

	assert(rt3_hw_b_initramfs_range_valid(source, image_size,
		0x40000000UL, 0x10000000UL));
	assert(!rt3_hw_b_initramfs_range_valid(0x43000000UL, 1,
		0x40000000UL, 0x10000000UL));
	assert(!rt3_hw_b_initramfs_range_valid(source,
		RT3_HW_B_HTTP_INITRAMFS_LIMIT + 1,
		0x40000000UL, 0x10000000UL));
	assert(!rt3_hw_b_initramfs_range_valid(source, image_size,
		0x40000000UL, 0x4000000UL));
	assert(!rt3_hw_b_initramfs_range_valid(source, image_size,
		0x42000000UL, 0x0e000000UL));

	copy_calls = 0;
	assert(!rt3_hw_b_ram_bootargs("console=long", too_small, sizeof(too_small)));
	assert(copy_calls == 0);
	assert(rt3_hw_b_stage_initramfs(source, image_size,
		0x40000000UL, 0x10000000UL, available, copy_image, &staged));
	assert(staged == 0x44000000UL);
	assert(copy_calls == 1);
	assert(copied_destination == staged && copied_source == source);
	assert(copied_size == image_size);
	assert(!rt3_hw_b_stage_initramfs(source, image_size,
		0x40000000UL, 0x10000000UL, NULL, copy_image, &staged));
	assert(copy_calls == 1);

	rt3_hw_b_initramfs_record(&validation, source, image_size);
	assert(rt3_hw_b_initramfs_matches(&validation, source, image_size));
	assert(!rt3_hw_b_initramfs_matches(&validation, source + 4, image_size));
	assert(!rt3_hw_b_initramfs_matches(&validation, source, image_size - 1));
	rt3_hw_b_initramfs_invalidate(&validation);
	assert(!rt3_hw_b_initramfs_validated_size(&validation, source));

	puts("HW-B initramfs helper checks passed");
	return 0;
}
