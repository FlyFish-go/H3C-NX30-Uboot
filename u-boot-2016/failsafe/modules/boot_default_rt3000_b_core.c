#include "boot_default_rt3000_b_core.h"
#ifdef RT3_BOOTCFG_HOST_TEST
#include <string.h>
#else
#include <linux/string.h>
#endif

static rt3_boot_u32 rd32(const rt3_boot_u8 *p)
{
    return (rt3_boot_u32)p[0] | ((rt3_boot_u32)p[1] << 8) |
           ((rt3_boot_u32)p[2] << 16) | ((rt3_boot_u32)p[3] << 24);
}

static void wr32(rt3_boot_u8 *p, rt3_boot_u32 v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static int parse(const rt3_boot_u8 *b, size_t size, struct rt3_bootcfg_view *v)
{
    rt3_boot_u32 magic, count, i, root = 0, selector = 0;
    if (!b || !v || size != RT3_BOOTCFG_SIZE || size < RT3_BOOTCFG_INFO_SIZE)
        return -1;
    magic = rd32(b);
    count = rd32(b + 8);
    if ((magic != RT3_BOOTCFG_MAGIC && magic != RT3_BOOTCFG_TRY_MAGIC) ||
        rd32(b + 332) != RT3_BOOTCFG_END || !count ||
        count > RT3_BOOTCFG_ENTRIES)
        return -1;
    for (i = 0; i < count; i++) {
        const rt3_boot_u8 *e = b + 12 + i * 20;
        const rt3_boot_u8 *nul = memchr(e, 0, 16);
        rt3_boot_u32 value = rd32(e + 16), j;
        if (!nul || nul == e || value > 1)
            return -1;
        for (j = 0; j < i; j++) {
            const rt3_boot_u8 *prev = b + 12 + j * 20;
            if (!strcmp((const char *)e, (const char *)prev))
                return -1;
        }
        if (!strcmp((const char *)e, "rootfs")) {
            root++;
            selector = value;
            v->rootfs_row = i;
        }
    }
    if (root != 1)
        return -1;
    v->age = rd32(b + 4);
    v->count = count;
    v->rootfs_selector = selector;
    return 0;
}

int rt3_bootcfg_validate_pair(const rt3_boot_u8 *primary, const rt3_boot_u8 *backup,
                              size_t size, struct rt3_bootcfg_view *view)
{
    struct rt3_bootcfg_view a, b;
    static const char * const critical_names[] = {
        "rootfs", "0:APPSBL", "0:QSEE", "0:DEVCFG", "0:CDT", "0:HLOS"
    };
    rt3_boot_u32 i, j;
    if (!primary || !backup || !view || parse(primary, size, &a) ||
        parse(backup, size, &b))
        return -1;
    if (rd32(primary) != rd32(backup) || a.count != b.count ||
        rd32(primary + 332) != rd32(backup + 332))
        return -1;
    for (i = 0; i < a.count; i++) {
        /* Selectors are shared state even when unrelated row names are local. */
        if (rd32(primary + 12 + i * 20 + 16) !=
            rd32(backup + 12 + i * 20 + 16))
            return -1;
    }
    if (a.rootfs_selector != b.rootfs_selector || a.rootfs_row != b.rootfs_row)
        return -1;

    /* Critical boot rows must have the same identity and row in both copies. */
    for (j = 0; j < sizeof(critical_names) / sizeof(critical_names[0]); j++) {
        int primary_row = -1, backup_row = -1;
        for (i = 0; i < a.count; i++) {
            const rt3_boot_u8 *pa = primary + 12 + i * 20;
            const rt3_boot_u8 *pb = backup + 12 + i * 20;
            if (!strcmp((const char *)pa, critical_names[j]))
                primary_row = (int)i;
            if (!strcmp((const char *)pb, critical_names[j]))
                backup_row = (int)i;
        }
        if (primary_row != backup_row)
            return -1;
    }
    *view = a;
    return 0;
}

int rt3_bootcfg_stage(const rt3_boot_u8 *source, size_t size, rt3_boot_u32 selector,
                      rt3_boot_u8 *staged)
{
    struct rt3_bootcfg_view v;
    if (!source || !staged || selector > 1 ||
        parse(source, size, &v))
        return -1;
    memcpy(staged, source, size);
    wr32(staged + 12 + v.rootfs_row * 20 + 16, selector);
    return 0;
}
