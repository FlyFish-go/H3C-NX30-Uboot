/* Both rescue page MIBIB presets pass the writer's checks: MIBIB structure
 * and the layout resolver (dual and 80 MiB single rootfs). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <rt3000_b_layout.h>
#include <rt3000_mibib.h>
#include <nand.h>
#include <asm/arch-qca-common/smem.h>
#define ERASE 0x20000U
nand_info_t nand_info[2]; qca_smem_flash_info_t qca_smem_flash_info;
uint32_t crc32(uint32_t c,const unsigned char*d,size_t n){size_t i; c=~c;for(i=0;i<n;i++){int b;c^=d[i];for(b=0;b<8;b++)c=(c>>1)^(0xedb88320U&-(int)(c&1));}return ~c;}
unsigned int is_spi_nand_available(void){return 0;} uint32_t get_nand_block_size(uint8_t id){return nand_info[id].erasesize;}
static unsigned char raw[RT3_MIBIB_PAGE];
const void *get_smem_ptable_addr(void){return raw;}
#include "../lib/rt3000_b_layout.c"
static unsigned char img[RT3_MIBIB_SIZE];
static int check(const char *file, enum rt3_layout_mode mode, uint64_t rootfs_size)
{
	struct rt3_layout l; uint32_t ages[RT3_MIBIB_COPIES]; FILE *f = fopen(file, "rb");
	if (!f || fread(img, 1, sizeof(img), f) != sizeof(img)) return 1;
	fclose(f);
	if (rt3_mibib_check(img, sizeof(img), ages) || ages[0] != 0 || ages[1] != 1) return 2;
	if (rt3_layout_resolve_raw(img + RT3_MIBIB_TABLE_PAGE(0), RT3_MIBIB_PAGE, &l)) return 3;
	if (l.mode != mode || l.parts[0].offset != 0x900000 || l.parts[0].size != rootfs_size) return 4;
	return 0;
}
int main(int argc, char **argv)
{
	int r;
	nand_info[0].size = 0x08000000ULL; nand_info[0].erasesize = ERASE;
	nand_info[0].writesize = 2048; nand_info[0].oobsize = 64;
	qca_smem_flash_info.flash_type = 11; qca_smem_flash_info.flash_block_size = ERASE;
	if (argc != 3) return 9;
	if ((r = check(argv[1], RT3_LAYOUT_DUAL, 0x2800000))) { printf("dual preset FAIL %d\n", r); return 1; }
	if ((r = check(argv[2], RT3_LAYOUT_SINGLE, 0x5000000))) { printf("single preset FAIL %d\n", r); return 1; }
	puts("MIBIB presets: structure, ages and dual/single layout resolve PASS");
	return 0;
}
