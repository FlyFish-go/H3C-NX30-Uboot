#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <libfdt.h>

void *calloc(size_t nmemb, size_t size);
#include <h3c_mainline_fdt.h>

static void *load(const char *path, size_t extra)
{
	FILE *f = fopen(path, "rb"); long n; void *b;
	assert(f); fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
	b = calloc(1, n + extra); assert(b); assert(fread(b, 1, n, f) == (size_t)n); fclose(f);
	assert(!fdt_open_into(b, b, n + extra));
	return b;
}

static unsigned int u32prop(const void *fdt, const char *path, const char *prop)
{
	int len, node = fdt_path_offset(fdt, path);
	const fdt32_t *v;
	assert(node >= 0);
	v = fdt_getprop(fdt, node, prop, &len);
	assert(v && len == 4);
	return fdt32_to_cpu(*v);
}

int main(int argc, char **argv)
{
	char out[256];
	void *ml, *qsdk;

	assert(argc == 3);
	ml = load(argv[1], 4096);
	qsdk = load(argv[2], 4096);

	assert(h3c_fdt_uses_smem_parts(ml) == 1);
	assert(h3c_fdt_uses_smem_parts(qsdk) == 0);
	assert(h3c_fdt_uses_smem_parts(NULL) < 0);

	assert(!h3c_ubi_mtd_rewrite("console=ttyMSM0 ubi.mtd=rootfs root=mtd:ubi_rootfs rootwait", "rootfs_1", out, sizeof(out)));
	assert(!strcmp(out, "console=ttyMSM0 root=mtd:ubi_rootfs rootwait ubi.mtd=rootfs_1"));
	assert(!h3c_ubi_mtd_rewrite("a  ubi.mtd=x ubi.mtd=y   b", "rootfs", out, sizeof(out)));
	assert(!strcmp(out, "a b ubi.mtd=rootfs"));
	assert(!h3c_ubi_mtd_rewrite("", "rootfs", out, sizeof(out)) && !strcmp(out, "ubi.mtd=rootfs"));
	assert(!h3c_ubi_mtd_rewrite(NULL, "rootfs", out, sizeof(out)) && !strcmp(out, "ubi.mtd=rootfs"));
	assert(h3c_ubi_mtd_rewrite("console=ttyMSM0", "rootfs_1", out, 20) == -ENOSPC);
	assert(h3c_ubi_mtd_rewrite("x", "", out, sizeof(out)) == -EINVAL);
	assert(h3c_ubi_mtd_rewrite("x", "a b", out, sizeof(out)) == -EINVAL);

	assert(h3c_fdt_set_snand_ecc(ml, 8) == 1);
	assert(u32prop(ml, "/soc/spi@79b0000/flash@0", "nand-ecc-strength") == 8);
	assert(u32prop(ml, "/soc/spi@79b0000/flash@0", "nand-ecc-step-size") == 512);
	assert(h3c_fdt_set_snand_ecc(ml, 4) == 1);
	assert(u32prop(ml, "/soc/spi@79b0000/flash@0", "nand-ecc-strength") == 4);
	/* a spi-nand behind some other controller is left alone */
	assert(fdt_getprop(ml, fdt_path_offset(ml, "/soc/spi@1000/flash@0"), "nand-ecc-strength", NULL) == NULL);
	assert(h3c_fdt_set_snand_ecc(qsdk, 8) == 0);
	assert(h3c_fdt_set_snand_ecc(ml, 2) == -EINVAL);
	puts("h3c_mainline_fdt hosttest: PASS");
	return 0;
}
