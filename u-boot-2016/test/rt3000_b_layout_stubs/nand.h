#ifndef RT3_TEST_NAND_H
#define RT3_TEST_NAND_H
#include <stdint.h>
struct mtd_info { uint64_t size; uint32_t erasesize,writesize,oobsize,flags,type; };
typedef struct mtd_info nand_info_t; extern nand_info_t nand_info[2];
#endif
