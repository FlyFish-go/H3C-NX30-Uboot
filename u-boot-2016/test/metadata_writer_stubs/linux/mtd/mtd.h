#ifndef MD_HOST_MTD_H
#define MD_HOST_MTD_H
#include <stddef.h>
#include <stdint.h>
struct mtd_info { int type; uint64_t size; uint32_t writesize,erasesize; unsigned int flags; unsigned int ecc_strength; };
struct erase_info { struct mtd_info *mtd; uint64_t addr,len; };
int mtd_read(struct mtd_info *,uint64_t,size_t,size_t *,uint8_t *);
int mtd_write(struct mtd_info *,uint64_t,size_t,size_t *,const uint8_t *);
int mtd_erase(struct mtd_info *,struct erase_info *);
#endif
