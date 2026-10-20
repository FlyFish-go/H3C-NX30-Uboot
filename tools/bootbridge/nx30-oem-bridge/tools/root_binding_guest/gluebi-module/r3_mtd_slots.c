// SPDX-License-Identifier: GPL-2.0-or-later
/* Disposable guest-only MTD index placeholders; no host MTD is touched. */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>

#define R3_SLOT_COUNT 20

static struct mtd_info *slots[R3_SLOT_COUNT];
static bool registered[R3_SLOT_COUNT];

static int slot_read(struct mtd_info *mtd, loff_t from, size_t len,
                     size_t *retlen, u_char *buffer)
{
    if (from < 0 || from > mtd->size || len > mtd->size - from)
        return -EINVAL;
    memset(buffer, 0xff, len);
    *retlen = len;
    return 0;
}

static int slot_write(struct mtd_info *mtd, loff_t to, size_t len,
                      size_t *retlen, const u_char *buffer)
{
    (void)mtd;
    (void)to;
    (void)len;
    (void)retlen;
    (void)buffer;
    return -EROFS;
}

static int slot_erase(struct mtd_info *mtd, struct erase_info *instr)
{
    (void)mtd;
    (void)instr;
    return -EROFS;
}

static void unregister_slots(void)
{
    int i;
    for (i = R3_SLOT_COUNT - 1; i >= 0; --i) {
        if (!slots[i])
            continue;
        if (registered[i])
            mtd_device_unregister(slots[i]);
        kfree(slots[i]->name);
        kfree(slots[i]);
        slots[i] = NULL;
    }
}

static int __init r3_slots_init(void)
{
    unsigned int i;
    int result;

    for (i = 0; i < R3_SLOT_COUNT; ++i) {
        slots[i] = kzalloc(sizeof(*slots[i]), GFP_KERNEL);
        if (!slots[i]) {
            result = -ENOMEM;
            goto fail;
        }
        slots[i]->name = kasprintf(GFP_KERNEL, "r3-dummy-%02u", i);
        if (!slots[i]->name) {
            result = -ENOMEM;
            goto fail;
        }
        slots[i]->type = MTD_RAM;
        slots[i]->flags = 0;
        slots[i]->size = 4096;
        slots[i]->erasesize = 4096;
        slots[i]->writesize = 1;
        slots[i]->writebufsize = 1;
        slots[i]->_read = slot_read;
        slots[i]->_write = slot_write;
        slots[i]->_erase = slot_erase;
        slots[i]->owner = THIS_MODULE;
        result = mtd_device_register(slots[i], NULL, 0);
        if (result)
            goto fail;
        registered[i] = true;
    }
    return 0;

fail:
    unregister_slots();
    return result;
}

static void __exit r3_slots_exit(void)
{
    unregister_slots();
}

module_init(r3_slots_init);
module_exit(r3_slots_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Isolated R3 guest MTD index placeholders");
