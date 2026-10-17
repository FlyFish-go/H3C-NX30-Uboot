#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../include/nx30_d_fdt_nor_guard.h"

static int check(int nand, const void *root, int root_len, int path_count,
		int flash_count, const void *parent, int parent_len,
		const void *chip, int chip_len, const void *reg, int reg_len,
		int expected_ret, int expected_disable)
{
	int disable = 99;
	int ret = nx30_d_fdt_nor_guard_validate(1, nand, root, root_len,
		path_count, flash_count, parent, parent_len,
		chip, chip_len, reg, reg_len, &disable);
	assert(ret == expected_ret);
	assert(disable == expected_disable);
	return ret;
}

int main(void)
{
	static const char root[] = "qcom,ipq5018-mp02.1\0qca,ipq5018\0";
	static const char parent[] = "qcom,spi-qup-v2.2.1\0";
	static const char n25[] = "n25q128a11\0";
	static const char micron[] = "micron,n25q128a11\0";
	static const unsigned char reg0[] = { 0, 0, 0, 0 };
	static const unsigned char reg1[] = { 0, 0, 0, 1 };
	int disable = 9;

	assert(nx30_d_fdt_nor_guard_validate(0, 0, NULL, 0, 0, 0,
		NULL, 0, NULL, 0, NULL, 0, &disable) == 0);
	assert(disable == 0); /* Non-OEM path stays untouched. */
	check(1, root, sizeof(root) - 1, 1, 1, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, reg0, sizeof(reg0), 0, 1);
	check(1, root, sizeof(root) - 1, 1, 1, parent, sizeof(parent) - 1,
	      micron, sizeof(micron) - 1, reg0, sizeof(reg0), 0, 1);
	check(0, root, sizeof(root) - 1, 1, 1, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, reg0, sizeof(reg0), -EINVAL, 0);
	check(1, "qcom,wrong\0", sizeof("qcom,wrong\0") - 1, 1, 1,
	      parent, sizeof(parent) - 1, n25, sizeof(n25) - 1,
	      reg0, sizeof(reg0), -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 0, 1, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, reg0, sizeof(reg0), -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 2, 1, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, reg0, sizeof(reg0), -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 1, 2, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, reg0, sizeof(reg0), -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 1, 1, "qcom,wrong\0",
	      sizeof("qcom,wrong\0") - 1, n25, sizeof(n25) - 1,
	      reg0, sizeof(reg0), -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 1, 1, parent, sizeof(parent) - 1,
	      "other\0", sizeof("other\0") - 1, reg0, sizeof(reg0), -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 1, 1, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, reg1, sizeof(reg1), -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 1, 1, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, reg0, sizeof(reg0) - 1, -EINVAL, 0);
	check(1, root, sizeof(root) - 1, 1, 1, parent, sizeof(parent) - 1,
	      n25, sizeof(n25) - 1, "bad", 3, -EINVAL, 0);
	assert(nx30_d_fdt_stringlist_has("qcom,bad", 8,
					 "qcom,ipq5018-mp02.1") == -EINVAL);
	puts("NX30 D FDT NOR guard hosttest: PASS");
	return 0;
}
