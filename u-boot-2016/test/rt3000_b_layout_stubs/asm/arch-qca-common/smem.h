#ifndef RT3_TEST_SMEM_H
#define RT3_TEST_SMEM_H
#include <stdint.h>
#define SMEM_BOOT_NAND_FLASH 2
#define SMEM_BOOT_QSPI_NAND_FLASH 11
#define SMEM_BOOT_SPI_FLASH 6
#define SMEM_BOOT_NORPLUSNAND 7
#define CONFIG_QPIC_NAND_NAND_INFO_IDX 0
typedef struct { uint32_t flash_type,flash_index,flash_chip_select,flash_block_size,flash_density,flash_secondary_type; } qca_smem_flash_info_t;
extern qca_smem_flash_info_t qca_smem_flash_info;
const void *get_smem_ptable_addr(void); uint32_t get_nand_block_size(uint8_t); unsigned int is_spi_nand_available(void);
#endif
