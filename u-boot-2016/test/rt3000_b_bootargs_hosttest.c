#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <rt3000_b_bootargs.h>

#define HIGH 0x03100000ULL
#define LOW  0x00900000ULL
#define SLOT 0x02800000ULL

/* Exact partition-map excerpt from the attached Product R1 kernel DT. */
static const char product_r1_args[] =
"coherent_pool=2M loglevel=7 mtdparts=qcom_nand.0:"
"0x80000@0(0:SBL1)ro,0x80000@0x80000(0:MIBIB)ro,"
"0x40000@0x100000(0:BOOTCONFIG),0x40000@0x140000(0:BOOTCONFIG1),"
"0x100000@0x180000(0:QSEE)ro,0x100000@0x280000(0:QSEE_1)ro,"
"0x40000@0x380000(0:DEVCFG)ro,0x40000@0x3c0000(0:DEVCFG_1)ro,"
"0x40000@0x400000(0:CDT)ro,0x40000@0x440000(0:CDT_1)ro,"
"0x80000@0x480000(0:APPSBLENV)ro,0x140000@0x500000(0:APPSBL)ro,"
"0x140000@0x640000(0:APPSBL_1)ro,0x100000@0x780000(0:ART)ro,"
"0x80000@0x880000(0:TRAINING)ro,"
"0x2800000@0x900000(oem_rootfs)ro,"
"0x2800000@0x3100000(rootfs),0x600000@0x5900000(pdt_data)ro,"
"0x600000@0x5f00000(pdt_data_1)ro,0x100000@0x6500000(exp)ro,"
"0x1700000@0x6600000(plugin)ro ubi.block=0,1 root=/dev/ubiblock0_1";

static void resolve(const char * const *args, const size_t *lengths,
                    unsigned int count, u64 offset, char *label, int *found)
{
    { int ret = rt3_bootargs_resolve_mtd_label(args, lengths, count, offset, SLOT, label, RT3_BOOTARGS_LABEL_MAX, found); if (ret) fprintf(stderr, "resolve ret=%d offset=%llx\n", ret, (unsigned long long)offset); assert(ret == 0); }
}

static void *load_blob(const char *path, size_t *size)
{
    static unsigned char blobs[2][8192];
    static unsigned int next_blob;
    FILE *f = fopen(path, "rb");
    void *data;
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    *size = ftell(f);
    assert(*size > 0);
    rewind(f);
    assert(next_blob < 2 && *size <= sizeof(blobs[0]));
    data = blobs[next_blob++];
    assert(fread(data, 1, *size, f) == *size);
    fclose(f);
    return data;
}
static unsigned int occurrences(const char *text, const char *needle)
{
    unsigned int n = 0;
    while ((text = strstr(text, needle)) != NULL) { n++; text += strlen(needle); }
    return n;
}
int main(int argc, char **argv)
{
    const char *sources[3];
    size_t lengths[3];
    char label[RT3_BOOTARGS_LABEL_MAX];
    int found;
    const char fallback[] = "ubi.mtd=rootfs_1 root=mtd:ubi_rootfs rootfstype=squashfs";
    const char bad[] = "mtdparts=qcom_nand.0:0x2800000@0x3100000(rootfs";
    const char mismatch[] = "mtdparts=qcom_nand.0:0x100000@0(rootfs)";
    const char duplicate[] = "mtdparts=qcom_nand.0:0x2800000@0x3100000(rootfs),0x1000@0x3100000(rootfs_1)";
    const char alternate[] = "mtdparts=qcom_nand.0:40M@0x3100000(rootfs_1)";
    const char suffix[] = "mtdparts=qcom_nand.0:40M@0x3100000(rootfs)";

    sources[0] = fallback; lengths[0] = sizeof(fallback) - 1;
    sources[1] = product_r1_args; lengths[1] = sizeof(product_r1_args) - 1;
    resolve(sources, lengths, 2, HIGH, label, &found);
    assert(found && !strcmp(label, "rootfs"));
    resolve(sources, lengths, 2, LOW, label, &found);
    assert(found && !strcmp(label, "oem_rootfs"));

    /* The FIT's partition labels are physical-map labels; SMEM logical names
     * may be swapped without changing this Linux MTD alias.
     */
    sources[0] = alternate; lengths[0] = sizeof(alternate) - 1;
    resolve(sources, lengths, 1, HIGH, label, &found);
    assert(found && !strcmp(label, "rootfs_1"));
    sources[0] = suffix; lengths[0] = sizeof(suffix) - 1;
    resolve(sources, lengths, 1, HIGH, label, &found);
    assert(found && !strcmp(label, "rootfs"));

    sources[0] = fallback; lengths[0] = sizeof(fallback) - 1;
    resolve(sources, lengths, 1, HIGH, label, &found);
    assert(!found && label[0] == '\0');

    sources[0] = bad; lengths[0] = sizeof(bad) - 1;
    assert(rt3_bootargs_resolve_mtd_label(sources, lengths, 1, HIGH, SLOT,
           label, sizeof(label), &found) == -EINVAL);
    sources[0] = mismatch; lengths[0] = sizeof(mismatch) - 1;
    assert(rt3_bootargs_resolve_mtd_label(sources, lengths, 1, HIGH, SLOT,
           label, sizeof(label), &found) == -ENOENT);
    sources[0] = duplicate; lengths[0] = sizeof(duplicate) - 1;
    assert(rt3_bootargs_resolve_mtd_label(sources, lengths, 1, HIGH, SLOT,
           label, sizeof(label), &found) == -EEXIST);

    sources[0] = suffix; lengths[0] = sizeof(suffix) - 1;
    sources[1] = alternate; lengths[1] = sizeof(alternate) - 1;
    assert(rt3_bootargs_resolve_mtd_label(sources, lengths, 2, HIGH, SLOT,
           label, sizeof(label), &found) == -EEXIST);
    lengths[0] = 10; /* truncated explicit map is rejected, no over-read */
    assert(rt3_bootargs_resolve_mtd_label(sources, lengths, 1, HIGH, SLOT,
           label, sizeof(label), &found) == -EINVAL);
    lengths[0] = sizeof(suffix) - 1;
    assert(rt3_bootargs_resolve_mtd_label(sources, lengths, 1, HIGH, SLOT,
           label, 4, &found) == -ENOSPC);

    {
        void *pf, *rf;
        size_t ps, rs;
        char cmdline[512];
        const char *base = "console=ttyMSM0 ubi.mtd=rootfs_1";
        const char *fallback_args = "ubi.mtd=rootfs_1 root=mtd:ubi_rootfs rootfstype=squashfs";
        assert(argc == 3);
        pf = load_blob(argv[1], &ps); rf = load_blob(argv[2], &rs);
        /* Selected Product R1 FDT provides the kernel root; mapping remains physical. */
        assert(rt3_bootargs_compose(base, "console=ttyMSM0 loglevel=7",
               fallback_args, pf, ps, HIGH, SLOT, cmdline, sizeof(cmdline)) == 0);
        assert(occurrences(cmdline, "ubi.mtd=") == 1);
        assert(strstr(cmdline, "ubi.mtd=rootfs") && !strstr(cmdline, "rootfs_1"));
        assert(!strstr(cmdline, "root=mtd:ubi_rootfs") && strstr(cmdline, "rootwait"));
        /* A mapped kernel DT with no root= keeps the generic root fallback. */
        assert(rt3_bootargs_compose(base, "console=ttyMSM0 loglevel=7",
               fallback_args, rf, rs, HIGH, SLOT, cmdline, sizeof(cmdline)) == 0);
        assert(occurrences(cmdline, "ubi.mtd=") == 1);
        assert(strstr(cmdline, "ubi.mtd=rootfs"));
        assert(strstr(cmdline, "root=mtd:ubi_rootfs") && strstr(cmdline, "rootfstype=squashfs"));
        /* FIT append root= suppresses the generic fallback root. */
        assert(rt3_bootargs_compose(base, "root=/dev/test", fallback_args,
               rf, rs, HIGH, SLOT, cmdline, sizeof(cmdline)) == 0);
        assert(strstr(cmdline, "root=/dev/test") && !strstr(cmdline, "root=mtd:ubi_rootfs"));
        /* No explicit map retains the generic fallback when FIT args are absent. */
        assert(rt3_bootargs_compose("console=ttyMSM0", NULL, fallback_args,
               NULL, 0, HIGH, SLOT, cmdline, sizeof(cmdline)) == 0);
        assert(strstr(cmdline, "ubi.mtd=rootfs_1") && strstr(cmdline, "root=mtd:ubi_rootfs"));
        assert(rt3_bootargs_compose(base, "console=long", fallback_args,
               pf, ps, HIGH, SLOT, cmdline, 8) == -ENOSPC);
        assert(rt3_bootargs_compose(base, NULL, fallback_args,
               pf, 8, HIGH, SLOT, cmdline, sizeof(cmdline)) == -EINVAL);
    }
    puts("RT3000-B bootargs mapping/composition host tests passed");
    return 0;
}
