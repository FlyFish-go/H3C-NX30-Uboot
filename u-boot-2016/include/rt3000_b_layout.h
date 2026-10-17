/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_B_LAYOUT_H__
#define __RT3000_B_LAYOUT_H__

#include <linux/types.h>

#define RT3_LAYOUT_PARTITION_NAME_LEN 16
#define RT3_LAYOUT_MAX_PARTITIONS 2

enum rt3_layout_mode {
	RT3_LAYOUT_UNSUPPORTED = 0,
	RT3_LAYOUT_DUAL,
	RT3_LAYOUT_SINGLE,
};

struct rt3_partition {
	char name[RT3_LAYOUT_PARTITION_NAME_LEN];
	/* One-based SMEM row for diagnostics; never a Linux MTD device number. */
	unsigned int display_index;
	u64 offset;
	u64 size;
};

struct rt3_layout {
	enum rt3_layout_mode mode;
	unsigned int count;
	struct rt3_partition parts[RT3_LAYOUT_MAX_PARTITIONS];
	/* CRC32 over validated complete SMEM table bytes and flash geometry. */
	u32 fingerprint;
};

/* Resolve and validate the full SMEM table plus the validated H3C RT3000 rootfs layout. */
int rt3_layout_read(struct rt3_layout *layout);

/* Resolve an untrusted raw SMEM table against current detected NAND geometry. */
int rt3_layout_resolve_raw(const void *table, size_t table_size,
			   struct rt3_layout *layout);

/* Require identical dual/single mode and identical physical rootfs extent set. */
int rt3_layout_same_physical_mode(const struct rt3_layout *a,
				  const struct rt3_layout *b);

/* Exact allowlist lookup; only rootfs and rootfs_1 can be returned. */
int rt3_layout_find(const struct rt3_layout *layout, const char *name,
		    const struct rt3_partition **partition);

/* Resolve a physical default slot to its validated logical rootfs entry. */
int rt3_layout_default_target(const struct rt3_layout *layout, int selector,
			      const struct rt3_partition **partition);

const char *rt3_layout_mode_name(enum rt3_layout_mode mode);

/* Return the Linux-visible alias for one raw SMEM entry in the selected
 * RAM boot view. The input layout and entry bytes are never modified.
 */
int rt3_layout_linux_mtd_alias(const struct rt3_layout *layout,
                              u64 selected_offset, const char *raw_name,
                              u64 raw_offset, char *out, size_t out_size);

/* Compatibility hook; layout reads always revalidate current SMEM bytes. */
void rt3_layout_invalidate_cache(void);

#endif /* __RT3000_B_LAYOUT_H__ */
