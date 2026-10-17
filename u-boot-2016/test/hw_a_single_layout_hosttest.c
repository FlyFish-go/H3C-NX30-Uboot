#define main rt3000_b_layout_baseline_main
#include "rt3000_b_layout_hosttest.c"
#undef main

int main(void)
{
	struct rt3_layout layout;
	const struct rt3_partition *part;
	int ret;

	fixture(1);
	/* RC-A2-512 live SMEM aliases these paired extents by boot selection. */
	T()->parts[4].start = 0x280000 / ERASE;
	T()->parts[5].start = 0x180000 / ERASE;
	T()->parts[8].start = 0x440000 / ERASE;
	T()->parts[9].start = 0x400000 / ERASE;
	T()->parts[11].start = 0x640000 / ERASE;
	T()->parts[12].start = 0x500000 / ERASE;
	ret = rt3_resolve_table(T(), &G, &layout);
	if (ret || layout.mode != RT3_LAYOUT_SINGLE || layout.count != 1)
		return 1;
	if (rt3_layout_default_target(&layout, 0, &part) ||
	    strcmp(part->name, "rootfs") || part->offset != 0x900000 ||
	    part->size != 0x5000000)
		return 2;
	if (rt3_layout_default_target(&layout, 1, &part) != -EINVAL)
		return 3;
	if (rt3_layout_find(&layout, "rootfs_1", &part) != -ENOENT)
		return 4;
	puts("HW-A swapped-alias single-layout resolver: PASS");
	return 0;
}
