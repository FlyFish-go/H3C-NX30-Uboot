#ifndef MD_HOST_NAND_H
#define MD_HOST_NAND_H
#include <linux/mtd/mtd.h>
extern struct mtd_info nand_info[1];
int nand_block_isbad(struct mtd_info *,uint64_t);
#endif
