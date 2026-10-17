/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RT3000_B_INITRAMFS_H__
#define __RT3000_B_INITRAMFS_H__

#ifdef RT3_INITRAMFS_HOST_TEST
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
#else
#include <linux/types.h>
#endif

typedef unsigned long rt3_hw_b_addr_t;

#define RT3_HW_B_HTTP_UPLOAD_LIMIT       (80U * 1024U * 1024U + 64U * 1024U)
#define RT3_HW_B_HTTP_INITRAMFS_LIMIT    (32U * 1024U * 1024U)
#define RT3_HW_B_INITRAMFS_SOURCE_START  ((rt3_hw_b_addr_t)0x41000000UL)
#define RT3_HW_B_INITRAMFS_SOURCE_END    ((rt3_hw_b_addr_t)0x43000000UL)
#define RT3_HW_B_INITRAMFS_STAGE_START   ((rt3_hw_b_addr_t)0x44000000UL)
#define RT3_HW_B_INITRAMFS_STAGE_END     ((rt3_hw_b_addr_t)0x46000000UL)

static inline bool rt3_hw_b_http_content_length_allowed(size_t length,
							bool is_upload_uri)
{
	return length <= (is_upload_uri ? RT3_HW_B_HTTP_INITRAMFS_LIMIT :
						 RT3_HW_B_HTTP_UPLOAD_LIMIT);
}

static inline bool rt3_hw_b_token_prefix(const char *token, size_t token_size,
					 const char *prefix, size_t prefix_size)
{
	size_t i;

	if (token_size < prefix_size)
		return false;
	for (i = 0; i < prefix_size; i++)
		if (token[i] != prefix[i])
			return false;
	return true;
}

static inline bool rt3_hw_b_token_equal(const char *token, size_t token_size,
					const char *value, size_t value_size)
{
	return token_size == value_size &&
		rt3_hw_b_token_prefix(token, token_size, value, value_size);
}

static inline bool rt3_hw_b_ram_bootargs(const char *input, char *output,
					 size_t capacity)
{
	static const char init_arg[] = "rdinit=/init";
	size_t in = 0, out = 0, start, length, i;
	bool remove;

	if (!output || !capacity)
		return false;
	if (!input)
		input = "";

	while (input[in]) {
		while (input[in] == ' ' || input[in] == '\t' ||
		       input[in] == '\r' || input[in] == '\n')
			in++;
		if (!input[in])
			break;
		start = in;
		while (input[in] && input[in] != ' ' && input[in] != '\t' &&
		       input[in] != '\r' && input[in] != '\n')
			in++;
		length = in - start;
		remove = rt3_hw_b_token_prefix(input + start, length, "ubi.mtd=", 8) ||
			rt3_hw_b_token_prefix(input + start, length, "root=", 5) ||
			rt3_hw_b_token_prefix(input + start, length, "rootfstype=", 11) ||
			rt3_hw_b_token_prefix(input + start, length, "rdinit=", 7) ||
			rt3_hw_b_token_equal(input + start, length, "rootwait", 8);
		if (remove)
			continue;
		if (out && out + 1 >= capacity)
			return false;
		if (out)
			output[out++] = ' ';
		if (length >= capacity - out)
			return false;
		for (i = 0; i < length; i++)
			output[out++] = input[start + i];
	}

	if (out && out + 1 >= capacity)
		return false;
	if (out)
		output[out++] = ' ';
	if (sizeof(init_arg) > capacity - out)
		return false;
	for (i = 0; i < sizeof(init_arg); i++)
		output[out++] = init_arg[i];
	return true;
}

static inline bool rt3_hw_b_fit_extent_valid(const void *image,
						 size_t available_size)
{
	const u8 *bytes = (const u8 *)image;
	u32 total_size;

	/* fdt_check_header() reads the complete fixed header. */
	if (!bytes || available_size < 40)
		return false;
	if (((u32)bytes[0] << 24 | (u32)bytes[1] << 16 |
	     (u32)bytes[2] << 8 | bytes[3]) != 0xd00dfeedU)
		return false;

	total_size = (u32)bytes[4] << 24 | (u32)bytes[5] << 16 |
		     (u32)bytes[6] << 8 | bytes[7];
	return total_size >= 40 && total_size == available_size &&
		total_size <= RT3_HW_B_HTTP_INITRAMFS_LIMIT;
}

static inline bool rt3_hw_b_initramfs_range_valid(rt3_hw_b_addr_t source,
						  size_t image_size,
						  rt3_hw_b_addr_t ram_base,
						  size_t ram_size)
{
	rt3_hw_b_addr_t source_end, ram_end, stage_end;

	if (!image_size || image_size > RT3_HW_B_HTTP_INITRAMFS_LIMIT)
		return false;
	if (image_size > (rt3_hw_b_addr_t)-1 - source ||
	    ram_size > (rt3_hw_b_addr_t)-1 - ram_base ||
	    image_size > (rt3_hw_b_addr_t)-1 - RT3_HW_B_INITRAMFS_STAGE_START)
		return false;

	source_end = source + image_size;
	ram_end = ram_base + ram_size;
	stage_end = RT3_HW_B_INITRAMFS_STAGE_START + image_size;

	return source >= RT3_HW_B_INITRAMFS_SOURCE_START &&
		source_end <= RT3_HW_B_INITRAMFS_SOURCE_END &&
		source >= ram_base && source_end <= ram_end &&
		RT3_HW_B_INITRAMFS_STAGE_START >= ram_base &&
		stage_end <= ram_end &&
		stage_end <= RT3_HW_B_INITRAMFS_STAGE_END &&
		(source_end <= RT3_HW_B_INITRAMFS_STAGE_START ||
		 source >= stage_end);
}

struct rt3_hw_b_initramfs_validation {
	rt3_hw_b_addr_t address;
	size_t size;
};

static inline void rt3_hw_b_initramfs_invalidate(
		struct rt3_hw_b_initramfs_validation *validation)
{
	if (validation) {
		validation->address = 0;
		validation->size = 0;
	}
}

static inline void rt3_hw_b_initramfs_record(
		struct rt3_hw_b_initramfs_validation *validation,
		rt3_hw_b_addr_t address, size_t size)
{
	if (validation) {
		validation->address = address;
		validation->size = size;
	}
}

static inline bool rt3_hw_b_initramfs_matches(
		const struct rt3_hw_b_initramfs_validation *validation,
		rt3_hw_b_addr_t address, size_t size)
{
	return validation && size && validation->address == address &&
		validation->size == size;
}

static inline size_t rt3_hw_b_initramfs_validated_size(
		const struct rt3_hw_b_initramfs_validation *validation,
		rt3_hw_b_addr_t address)
{
	return validation && validation->size && validation->address == address ?
		validation->size : 0;
}

typedef bool (*rt3_hw_b_ram_available_fn)(rt3_hw_b_addr_t start, size_t size);
typedef void (*rt3_hw_b_memmove_fn)(rt3_hw_b_addr_t destination,
				    rt3_hw_b_addr_t source, size_t size);

static inline bool rt3_hw_b_stage_initramfs(rt3_hw_b_addr_t source,
					    size_t image_size,
					    rt3_hw_b_addr_t ram_base,
					    size_t ram_size,
					    rt3_hw_b_ram_available_fn available,
					    rt3_hw_b_memmove_fn move,
					    rt3_hw_b_addr_t *destination)
{
	if (!destination || !available || !move ||
	    !rt3_hw_b_initramfs_range_valid(source, image_size,
					    ram_base, ram_size) ||
	    !available(RT3_HW_B_INITRAMFS_STAGE_START, image_size))
		return false;

	move(RT3_HW_B_INITRAMFS_STAGE_START, source, image_size);
	*destination = RT3_HW_B_INITRAMFS_STAGE_START;
	return true;
}

#endif /* __RT3000_B_INITRAMFS_H__ */
