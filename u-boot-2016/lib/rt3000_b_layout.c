// SPDX-License-Identifier: GPL-2.0+
/* H3C RT3000 HW-A/HW-B validated layout resolver. */
#include <common.h>
#include <errno.h>
#include <linux/crc32.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <asm/arch-qca-common/smem.h>
#include <rt3000_b_layout.h>

#define RT3_SMEM_MAGIC1 0x55ee73aaU
#define RT3_SMEM_MAGIC2 0xe35ebddbU
#define RT3_SMEM_PART_MAX 32U
#define RT3_SMEM_VERSION_MIN 3U
#define RT3_SMEM_VERSION_MAX 4U
#define RT3_ATTR_LOW_MASK 0x00ffffffU
#define RT3_ATTR_LOW_VALUE 0x0000ffffU
#define RT3_NAND_ATTR_PRIMARY 0U
#define RT3_NAND_ATTR_NAND 1U
#define RT3_NAND_ERASESIZE 0x00020000U
#define RT3_NAND_WRITESIZE 0x00000800U
#define RT3_NAND_SIZE 0x08000000ULL
#define RT3_ROOTFS_OFFSET 0x00900000ULL
#define RT3_DUAL_SIZE 0x02800000ULL
#define RT3_SINGLE_SIZE 0x05000000ULL
#define RT3_PREFIX_COUNT 15U
#define RT3_SUFFIX_COUNT 4U

/* Private SMEM ABI declarations matching smem.c's packed table. */
struct rt3_smem_part {
	char name[RT3_LAYOUT_PARTITION_NAME_LEN];
	u32 start;
	u32 size;
	u32 attr;
} __attribute__((packed));

struct rt3_smem_table {
	u32 magic[2];
	u32 version;
	u32 len;
	struct rt3_smem_part parts[RT3_SMEM_PART_MAX];
} __attribute__((packed));

struct rt3_protected_part {
	const char *name;
	u64 offset;
	u64 size;
};

static const struct rt3_protected_part boot_prefix[RT3_PREFIX_COUNT] = {
	{ "0:SBL1",      0x00000000ULL, 0x00080000ULL },
	{ "0:MIBIB",     0x00080000ULL, 0x00080000ULL },
	{ "0:BOOTCONFIG",0x00100000ULL, 0x00040000ULL },
	{ "0:BOOTCONFIG1",0x00140000ULL, 0x00040000ULL },
	{ "0:QSEE",      0x00180000ULL, 0x00100000ULL },
	{ "0:QSEE_1",    0x00280000ULL, 0x00100000ULL },
	{ "0:DEVCFG",    0x00380000ULL, 0x00040000ULL },
	{ "0:DEVCFG_1",  0x003c0000ULL, 0x00040000ULL },
	{ "0:CDT",       0x00400000ULL, 0x00040000ULL },
	{ "0:CDT_1",     0x00440000ULL, 0x00040000ULL },
	{ "0:APPSBLENV", 0x00480000ULL, 0x00080000ULL },
	{ "0:APPSBL",    0x00500000ULL, 0x00140000ULL },
	{ "0:APPSBL_1",  0x00640000ULL, 0x00140000ULL },
	{ "0:ART",       0x00780000ULL, 0x00100000ULL },
	{ "0:TRAINING",  0x00880000ULL, 0x00080000ULL },
};

static const struct rt3_protected_part trailing_parts[RT3_SUFFIX_COUNT] = {
	{ "pdt_data",   0x05900000ULL, 0x00600000ULL },
	{ "pdt_data_1", 0x05f00000ULL, 0x00600000ULL },
	{ "exp",        0x06500000ULL, 0x00100000ULL },
	{ "plugin",     0x06600000ULL, 0x01700000ULL },
};

extern unsigned int is_spi_nand_available(void);

struct rt3_geometry {
	u32 flash_type;
	u32 flash_index;
	u32 flash_chip_select;
	u32 flash_block_size;
	u32 flash_density;
	u32 flash_secondary_type;
	u64 nand_size;
	u32 nand_erasesize;
	u32 nand_writesize;
	u32 nand_oobsize;
};


static int rt3_name_valid(const char name[RT3_LAYOUT_PARTITION_NAME_LEN])
{
	const char *nul = memchr(name, '\0', RT3_LAYOUT_PARTITION_NAME_LEN);

	return nul && nul != name;
}

static u32 rt3_layout_fingerprint(const struct rt3_smem_table *table,
				  u32 len,
				  const struct rt3_geometry *geometry)
{
	size_t table_bytes = offsetof(struct rt3_smem_table, parts) +
			     len * sizeof(table->parts[0]);
	u32 crc = crc32(0, (const unsigned char *)table, table_bytes);

	return crc32(crc, (const unsigned char *)geometry, sizeof(*geometry));
}

/*
 * Pure table resolver. All offsets and lengths are derived from the validated
 * NAND geometry; only the known dual/single H3C RT3000 partition schemes pass.
 */
static int rt3_prefix_extent_matches(unsigned int index, u64 offset, u64 size)
{
	unsigned int peer = RT3_PREFIX_COUNT;
	switch (index) {
	case 4: peer = 5; break; case 5: peer = 4; break;
	case 6: peer = 7; break; case 7: peer = 6; break;
	case 8: peer = 9; break; case 9: peer = 8; break;
	case 11: peer = 12; break; case 12: peer = 11; break;
	default: break;
	}
	if (size != boot_prefix[index].size) return 0;
	if (offset == boot_prefix[index].offset) return 1;
	return peer < RT3_PREFIX_COUNT && offset == boot_prefix[peer].offset;
}

static int rt3_resolve_table(const struct rt3_smem_table *table,
			     const struct rt3_geometry *geometry,
			     struct rt3_layout *layout)
{
	u64 offsets[RT3_SMEM_PART_MAX], sizes[RT3_SMEM_PART_MAX];
	int rootfs_index = -1, rootfs1_index = -1;
	unsigned int i, j, rootfs_like = 0;
	unsigned int expected_count;
	int seen_prefix[RT3_PREFIX_COUNT] = { 0 };
	int seen_suffix[RT3_SUFFIX_COUNT] = { 0 };
	const struct rt3_smem_part *part;

	memset(layout, 0, sizeof(*layout));
	layout->mode = RT3_LAYOUT_UNSUPPORTED;

	if (!table || !geometry || table->magic[0] != RT3_SMEM_MAGIC1 ||
	    table->magic[1] != RT3_SMEM_MAGIC2 ||
	    (table->version < RT3_SMEM_VERSION_MIN ||
	     table->version > RT3_SMEM_VERSION_MAX) || !table->len ||
	    table->len > RT3_SMEM_PART_MAX)
		return -EINVAL;
	if ((geometry->flash_type != SMEM_BOOT_NAND_FLASH &&
	     geometry->flash_type != SMEM_BOOT_QSPI_NAND_FLASH) ||
	    geometry->nand_size != RT3_NAND_SIZE ||
	    geometry->nand_erasesize != RT3_NAND_ERASESIZE ||
	    geometry->nand_writesize != RT3_NAND_WRITESIZE ||
	    geometry->flash_block_size != geometry->nand_erasesize ||
	    geometry->nand_erasesize % geometry->nand_writesize)
		return -EINVAL;

	for (i = 0; i < table->len; i++) {
		part = &table->parts[i];
		if (!rt3_name_valid(part->name) ||
		    (((part->attr >> 24) & 0xffU) != RT3_NAND_ATTR_PRIMARY &&
		     ((part->attr >> 24) & 0xffU) != RT3_NAND_ATTR_NAND) ||
		    (part->attr & RT3_ATTR_LOW_MASK) != RT3_ATTR_LOW_VALUE)
			return -EINVAL;
		/* OEM MIBIB stores 0xffff in attr for these allowlisted partitions. The 0/1
		 * attribute values are accepted only under this board's exact
		 * NAND geometry; attr 1 must resolve to NAND device 0 as well.
		 */
		if (((part->attr >> 24) & 0xffU) == RT3_NAND_ATTR_NAND &&
		    is_spi_nand_available() != CONFIG_QPIC_NAND_NAND_INFO_IDX)
			return -ENODEV;
		for (j = 0; j < i; j++)
			if (!strcmp(part->name, table->parts[j].name))
				return -EEXIST;

		offsets[i] = (u64)part->start * geometry->nand_erasesize;
		if (part->size == 0xffffffffU)
			return -EINVAL;
		sizes[i] = (u64)part->size * geometry->nand_erasesize;
		if (offsets[i] > geometry->nand_size ||
		    !sizes[i] || sizes[i] > geometry->nand_size - offsets[i] ||
		    offsets[i] % geometry->nand_erasesize ||
		    sizes[i] % geometry->nand_erasesize)
			return -ERANGE;

		for (j = 0; j < i; j++)
			if (offsets[i] < offsets[j] + sizes[j] &&
			    offsets[j] < offsets[i] + sizes[i])
				return -EEXIST;

		if (!strcmp(part->name, "rootfs"))
			rootfs_index = i;
		else if (!strcmp(part->name, "rootfs_1"))
			rootfs1_index = i;
		if (!strncmp(part->name, "rootfs", 6))
			rootfs_like++;
	}

	if (rootfs_index < 0 ||
	    rootfs_like != (rootfs1_index >= 0 ? 2U : 1U))
		return -EINVAL;

	if (rootfs1_index >= 0) {
		int forward = offsets[rootfs_index] == RT3_ROOTFS_OFFSET &&
			      offsets[rootfs1_index] ==
			      RT3_ROOTFS_OFFSET + RT3_DUAL_SIZE;
		int reversed = offsets[rootfs_index] ==
			       RT3_ROOTFS_OFFSET + RT3_DUAL_SIZE &&
			       offsets[rootfs1_index] == RT3_ROOTFS_OFFSET;
		if (sizes[rootfs_index] != RT3_DUAL_SIZE ||
		    sizes[rootfs1_index] != RT3_DUAL_SIZE ||
		    (!forward && !reversed))
			return -EINVAL;
		layout->mode = RT3_LAYOUT_DUAL;
		layout->count = 2;
		expected_count = RT3_PREFIX_COUNT + 2 + RT3_SUFFIX_COUNT;
	} else {
		if (offsets[rootfs_index] != RT3_ROOTFS_OFFSET ||
		    sizes[rootfs_index] != RT3_SINGLE_SIZE)
			return -EINVAL;
		layout->mode = RT3_LAYOUT_SINGLE;
		layout->count = 1;
		expected_count = RT3_PREFIX_COUNT + 1 + RT3_SUFFIX_COUNT;
	}
	if (table->len != expected_count)
		return -EINVAL;

	/* Every non-rootfs entry must be one of the protected OEM extents. The
	 * table order may vary, so find records by exact name and byte range.
	 */
	for (i = 0; i < table->len; i++) {
		if ((int)i == rootfs_index || (int)i == rootfs1_index)
			continue;
		for (j = 0; j < RT3_PREFIX_COUNT; j++) {
			if (!strcmp(table->parts[i].name, boot_prefix[j].name) &&
			    rt3_prefix_extent_matches(j, offsets[i], sizes[i])) {
				seen_prefix[j] = 1;
				break;
			}
		}
		if (j < RT3_PREFIX_COUNT)
			continue;
		for (j = 0; j < RT3_SUFFIX_COUNT; j++) {
			if (!strcmp(table->parts[i].name, trailing_parts[j].name) &&
			    offsets[i] == trailing_parts[j].offset &&
			    sizes[i] == trailing_parts[j].size) {
				seen_suffix[j] = 1;
				break;
			}
		}
		if (j == RT3_SUFFIX_COUNT)
			return -EINVAL;
	}
	for (i = 0; i < RT3_PREFIX_COUNT; i++)
		if (!seen_prefix[i])
			return -EINVAL;
	for (i = 0; i < RT3_SUFFIX_COUNT; i++)
		if (!seen_suffix[i])
			return -EINVAL;

	layout->parts[0].display_index = rootfs_index + 1;
	strlcpy(layout->parts[0].name, table->parts[rootfs_index].name,
		sizeof(layout->parts[0].name));
	layout->parts[0].offset = offsets[rootfs_index];
	layout->parts[0].size = sizes[rootfs_index];
	if (rootfs1_index >= 0) {
		layout->parts[1].display_index = rootfs1_index + 1;
		strlcpy(layout->parts[1].name, table->parts[rootfs1_index].name,
			sizeof(layout->parts[1].name));
		layout->parts[1].offset = offsets[rootfs1_index];
		layout->parts[1].size = sizes[rootfs1_index];
	}
	layout->fingerprint = rt3_layout_fingerprint(table, table->len, geometry);
	return 0;
}

static int rt3_current_geometry(struct rt3_geometry *geometry)
{
	const qca_smem_flash_info_t *flash = &qca_smem_flash_info;
	nand_info_t *nand;
	unsigned int nand_id = is_spi_nand_available();

	if (!geometry || nand_id != CONFIG_QPIC_NAND_NAND_INFO_IDX)
		return -ENODEV;
	nand = &nand_info[nand_id];
	memset(geometry, 0, sizeof(*geometry));
	geometry->flash_type = flash->flash_type;
	geometry->flash_index = flash->flash_index;
	geometry->flash_chip_select = flash->flash_chip_select;
	geometry->flash_block_size = flash->flash_block_size;
	geometry->flash_density = flash->flash_density;
	geometry->flash_secondary_type = flash->flash_secondary_type;
	geometry->nand_size = nand->size;
	geometry->nand_erasesize = nand->erasesize;
	geometry->nand_writesize = nand->writesize;
	geometry->nand_oobsize = nand->oobsize;
	return 0;
}

int rt3_layout_resolve_raw(const void *table_data, size_t table_size,
			   struct rt3_layout *layout)
{
	struct rt3_smem_table copy;
	struct rt3_geometry geometry;
	int ret;

	if (!table_data || !layout || table_size < sizeof(copy))
		return -EINVAL;
	memcpy(&copy, table_data, sizeof(copy));
	ret = rt3_current_geometry(&geometry);
	if (ret)
		return ret;
	return rt3_resolve_table(&copy, &geometry, layout);
}

int rt3_layout_same_physical_mode(const struct rt3_layout *a,
				  const struct rt3_layout *b)
{
	unsigned int i, j;
	int found;

	if (!a || !b || a->mode == RT3_LAYOUT_UNSUPPORTED ||
	    a->mode != b->mode || a->count != b->count)
		return 0;
	for (i = 0; i < a->count; i++) {
		found = 0;
		for (j = 0; j < b->count; j++) {
			if (a->parts[i].offset == b->parts[j].offset &&
			    a->parts[i].size == b->parts[j].size) {
				found = 1;
				break;
			}
		}
		if (!found)
			return 0;
	}
	return 1;
}

int rt3_layout_read(struct rt3_layout *layout)
{
	const struct rt3_smem_table *table;
	const qca_smem_flash_info_t *flash = &qca_smem_flash_info;
	nand_info_t *nand;
	struct rt3_geometry geometry;
	struct rt3_layout resolved;
	unsigned int nand_id;
	int ret;

	if (!layout)
		return -EINVAL;
	memset(&resolved, 0, sizeof(resolved));
	resolved.mode = RT3_LAYOUT_UNSUPPORTED;
	table = get_smem_ptable_addr();
	nand_id = is_spi_nand_available();
	if (!table || nand_id != CONFIG_QPIC_NAND_NAND_INFO_IDX) {
		ret = -ENODEV;
		goto done;
	}
	nand = &nand_info[nand_id];
	memset(&geometry, 0, sizeof(geometry));
	geometry.flash_type = flash->flash_type;
	geometry.flash_index = flash->flash_index;
	geometry.flash_chip_select = flash->flash_chip_select;
	geometry.flash_block_size = flash->flash_block_size;
	geometry.flash_density = flash->flash_density;
	geometry.flash_secondary_type = flash->flash_secondary_type;
	geometry.nand_size = nand->size;
	geometry.nand_erasesize = nand->erasesize;
	geometry.nand_writesize = nand->writesize;
	geometry.nand_oobsize = nand->oobsize;
	ret = rt3_resolve_table(table, &geometry, &resolved);
done:
	*layout = resolved;
	return ret;
}

int rt3_layout_find(const struct rt3_layout *layout, const char *name,
		    const struct rt3_partition **partition)
{
	unsigned int i;

	if (partition)
		*partition = NULL;
	if (!layout || !name || !partition ||
	    layout->mode == RT3_LAYOUT_UNSUPPORTED)
		return -EINVAL;
	if (strcmp(name, "rootfs") && strcmp(name, "rootfs_1"))
		return -EINVAL;
	for (i = 0; i < layout->count; i++) {
		if (!strcmp(layout->parts[i].name, name)) {
			*partition = &layout->parts[i];
			return 0;
		}
	}
	return -ENOENT;
}

int rt3_layout_default_target(const struct rt3_layout *layout, int selector,
			      const struct rt3_partition **partition)
{
	const struct rt3_partition *found = NULL;
	u64 offset;
	unsigned int i;

	if (partition)
		*partition = NULL;
	if (!layout || !partition)
		return -EINVAL;
	if (layout->mode == RT3_LAYOUT_SINGLE) {
		if (selector != 0 || layout->count != 1 ||
		    strcmp(layout->parts[0].name, "rootfs") ||
		    layout->parts[0].offset != RT3_ROOTFS_OFFSET ||
		    layout->parts[0].size != RT3_SINGLE_SIZE)
			return -EINVAL;
		*partition = &layout->parts[0];
		return 0;
	}
	if (layout->mode != RT3_LAYOUT_DUAL || layout->count != 2 ||
	    (selector != 0 && selector != 1))
		return -EINVAL;

	offset = selector ? RT3_ROOTFS_OFFSET + RT3_DUAL_SIZE :
		RT3_ROOTFS_OFFSET;
	for (i = 0; i < layout->count; i++) {
		const struct rt3_partition *part = &layout->parts[i];

		if ((strcmp(part->name, "rootfs") &&
		     strcmp(part->name, "rootfs_1")) ||
		    part->size != RT3_DUAL_SIZE)
			continue;
		if (part->offset == offset) {
			if (found)
				return -EINVAL;
			found = part;
		}
	}
	if (!found)
		return -ENOENT;
	*partition = found;
	return 0;
}

int rt3_layout_linux_mtd_alias(const struct rt3_layout *l,u64 selected,
 const char *raw,u64 offset,char *out,size_t cap)
{
 const struct rt3_partition *r=NULL,*r1=NULL;
 char name[RT3_LAYOUT_PARTITION_NAME_LEN];
 unsigned i;
 if(!l||!raw||!out||!cap) return -EINVAL;
 if(l->mode==RT3_LAYOUT_SINGLE) {
  if(l->count!=1||strcmp(l->parts[0].name,"rootfs")||
     l->parts[0].offset!=RT3_ROOTFS_OFFSET||
     l->parts[0].size!=RT3_SINGLE_SIZE||selected!=RT3_ROOTFS_OFFSET)
   return -EINVAL;
  if(!strcmp(raw,"rootfs")&&offset!=RT3_ROOTFS_OFFSET) return -EINVAL;
 } else if(l->mode==RT3_LAYOUT_DUAL) {
  if(l->count!=2) return -EINVAL;
  for(i=0;i<2;i++) {
   if(!strcmp(l->parts[i].name,"rootfs")) r=&l->parts[i];
   else if(!strcmp(l->parts[i].name,"rootfs_1")) r1=&l->parts[i];
   else return -EINVAL;
  }
  if(!r||!r1||r->size!=RT3_DUAL_SIZE||r1->size!=RT3_DUAL_SIZE||
     !((r->offset==RT3_ROOTFS_OFFSET&&r1->offset==RT3_ROOTFS_OFFSET+RT3_DUAL_SIZE)||
       (r1->offset==RT3_ROOTFS_OFFSET&&r->offset==RT3_ROOTFS_OFFSET+RT3_DUAL_SIZE))||
     (selected!=r->offset&&selected!=r1->offset)) return -EINVAL;
  if((!strcmp(raw,"rootfs")&&offset!=r->offset)||
     (!strcmp(raw,"rootfs_1")&&offset!=r1->offset)) return -EINVAL;
 } else return -EINVAL;
 if(!strcmp(raw,"rootfs")||!strcmp(raw,"rootfs_1"))
  strlcpy(name,l->mode==RT3_LAYOUT_SINGLE||offset==selected?"rootfs":"rootfs_1",sizeof(name));
 else strlcpy(name,raw,sizeof(name));
 if(strlen(name)>=cap) return -ENOSPC;
 memcpy(out,name,strlen(name)+1);
 return 0;
}

const char *rt3_layout_mode_name(enum rt3_layout_mode mode)
{
	switch (mode) {
	case RT3_LAYOUT_DUAL:
		return "dual";
	case RT3_LAYOUT_SINGLE:
		return "single";
	case RT3_LAYOUT_UNSUPPORTED:
	default:
		return "unsupported";
	}
}

void rt3_layout_invalidate_cache(void)
{
	/* Layout is re-read from SMEM on every call; retained for API symmetry. */
}
