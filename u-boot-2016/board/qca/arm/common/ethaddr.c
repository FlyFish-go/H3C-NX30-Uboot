/*
 * Copyright (c) 2016-2017, 2020 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
#define RT3_MAC_BOARD_LABEL "H3C HW-C"
#else
#define RT3_MAC_BOARD_LABEL "RT3000-B"
#endif

#include <common.h>
#include <net.h>
#include <asm/errno.h>
#include <nand.h>
#include <part.h>
#include <mmc.h>
#include <asm/arch-qca-common/smem.h>
#include <asm/arch-qca-common/qca_common.h>
#include <sdhci.h>
#ifdef CONFIG_IPQ_TINY_SPI_NOR
#include <spi.h>
#include <spi_flash.h>
#endif
#if defined(CONFIG_ART_COMPRESSED) &&	\
	(defined(CONFIG_GZIP) || defined(CONFIG_LZMA))
#ifndef CONFIG_COMPRESSED_LOAD_ADDR
#define CONFIG_COMPRESSED_LOAD_ADDR CONFIG_SYS_LOAD_ADDR
#endif
#include <mapmem.h>
#include <lzma/LzmaTools.h>
#endif
#ifdef CONFIG_QCA_MMC
#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern  struct sdhci_host mmc_host;
#endif
#endif

/*
 * Gets the ethernet address from the ART partition table and return the value
 */
int get_eth_mac_address(uchar *enetaddr, uint no_of_macs)
{
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
	static const uchar generic_ethaddr[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
	static const uchar generic_eth1addr[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x56 };
	uchar base_mac[6];
	uint i;
	int j;

	if (no_of_macs > 2)
		return -EINVAL;

	/* RT3000-B factory MAC lives in its own APPSBLENV, not ART. */
	if (!eth_getenv_enetaddr("ethaddr", base_mac)) {
		printf("%s: valid factory ethaddr is required\n", RT3_MAC_BOARD_LABEL);
		return -EINVAL;
	}
	for (i = 0; i < no_of_macs; i++) {
		uchar *mac = enetaddr + i * 6;
		if (eth_getenv_enetaddr_by_index("eth", i, mac) &&
		    !(i > 0 &&
		      (!memcmp(mac, generic_ethaddr, sizeof(generic_ethaddr)) ||
		       !memcmp(mac, generic_eth1addr, sizeof(generic_eth1addr)))))
			continue;
		/* Derive absent or generic secondary MACs from this base in RAM. */
		memcpy(mac, base_mac, 6);
		for (j = 5; j >= 0; j--) {
			if (++mac[j])
				break;
		}
		if (!is_valid_ethaddr(mac))
			return -EINVAL;
		/* The generic Ethernet layer otherwise reimports the OEM placeholder. */
		if (i > 0 && eth_setenv_enetaddr("eth1addr", mac))
			return -EINVAL;
	}
	return 0;
#elif defined(CONFIG_H3C_HW_D_RAM_READONLY)
	static const uchar temporary_ethaddr[6] = { 0x02, 0x4e, 0x58, 0x30, 0x00, 0x01 };
	static const uchar temporary_eth1addr[6] = { 0x02, 0x4e, 0x58, 0x30, 0x00, 0x02 };
	static const uchar generic_ethaddr[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
	static const uchar generic_eth1addr[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x56 };
	uchar base_mac[6], mac[6];
	uint i;

	if (no_of_macs > 2)
		return -EINVAL;
	if (!eth_getenv_enetaddr("ethaddr", base_mac) ||
	    !is_valid_ethaddr(base_mac) ||
	    !memcmp(base_mac, generic_ethaddr, sizeof(base_mac)) ||
	    !memcmp(base_mac, generic_eth1addr, sizeof(base_mac))) {
		memcpy(base_mac, temporary_ethaddr, sizeof(base_mac));
		if (eth_setenv_enetaddr("ethaddr", base_mac))
			return -EINVAL;
		printf("NX30 D temporary ethaddr=02:4e:58:30:00:01 (RAM only)\n");
	}
	memcpy(enetaddr, base_mac, sizeof(base_mac));
	for (i = 1; i < no_of_macs; i++) {
		if (eth_getenv_enetaddr("eth1addr", mac) && is_valid_ethaddr(mac) &&
		    memcmp(mac, generic_ethaddr, sizeof(mac)) &&
		    memcmp(mac, generic_eth1addr, sizeof(mac))) {
			memcpy(enetaddr + i * 6, mac, sizeof(mac));
			continue;
		}
		memcpy(mac, temporary_eth1addr, sizeof(mac));
		if (memcmp(base_mac, temporary_ethaddr, sizeof(base_mac))) {
			memcpy(mac, base_mac, sizeof(mac));
			for (int j = 5; j >= 0; j--)
				if (++mac[j])
					break;
		}
		if (!is_valid_ethaddr(mac) || eth_setenv_enetaddr("eth1addr", mac))
			return -EINVAL;
		memcpy(enetaddr + i * 6, mac, sizeof(mac));
		if (!memcmp(mac, temporary_eth1addr, sizeof(mac)))
			printf("NX30 D temporary eth1addr=02:4e:58:30:00:02 (RAM only)\n");
	}
	return 0;
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
	static const uchar generic_ethaddr[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
	static const uchar generic_eth1addr[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x56 };
	uchar base_mac[6];
	uint i;
	int j;

	/* HW-A factory identity is already in the loaded OEM ENV; ART is invalid. */
	if (no_of_macs > 2 || !eth_getenv_enetaddr("ethaddr", base_mac) ||
	    !is_valid_ethaddr(base_mac) ||
	    !memcmp(base_mac, generic_ethaddr, sizeof(generic_ethaddr)) ||
	    !memcmp(base_mac, generic_eth1addr, sizeof(generic_eth1addr)))
		return -EINVAL;
	for (i = 0; i < no_of_macs; i++) {
		uchar *mac = enetaddr + i * 6;
		if (eth_getenv_enetaddr_by_index("eth", i, mac) &&
		    is_valid_ethaddr(mac) &&
		    memcmp(mac, generic_ethaddr, sizeof(generic_ethaddr)) &&
		    memcmp(mac, generic_eth1addr, sizeof(generic_eth1addr)))
			continue;
		memcpy(mac, base_mac, 6);
		for (j = 5; i && j >= 0; j--)
			if (++mac[j])
				break;
		if (!is_valid_ethaddr(mac))
			return -EINVAL;
	}
	return 0;
#else

	s32 ret = 0 ;
	u32 start_blocks;
	u32 size_blocks;
	u32 length = (6 * no_of_macs);
	u32 flash_type;
	loff_t art_offset;
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
#ifdef CONFIG_QCA_MMC
	block_dev_desc_t *blk_dev;
	disk_partition_t disk_info;
	struct mmc *mmc;
	char mmc_blks[512];
#endif
#ifdef CONFIG_IPQ_TINY_SPI_NOR
	struct spi_flash *flash = NULL;
#if defined(CONFIG_ART_COMPRESSED) && (defined(CONFIG_GZIP) || defined(CONFIG_LZMA))
	void *load_buf, *image_buf;
	unsigned long img_size;
	unsigned long desMaxSize;
#endif
#endif

	if (sfi->flash_type != SMEM_BOOT_MMC_FLASH) {
		if (qca_smem_flash_info.flash_type == SMEM_BOOT_SPI_FLASH)
			flash_type = CONFIG_SPI_FLASH_INFO_IDX;
		else if (qca_smem_flash_info.flash_type == SMEM_BOOT_NAND_FLASH)
			flash_type = CONFIG_NAND_FLASH_INFO_IDX;
		else if (qca_smem_flash_info.flash_type == SMEM_BOOT_QSPI_NAND_FLASH)
			flash_type = CONFIG_NAND_FLASH_INFO_IDX;
		else {
			printf("Unknown flash type\n");
			return -EINVAL;
		}

		ret = smem_getpart("0:ART", &start_blocks, &size_blocks);
		if (ret < 0) {
			printf("No ART partition found\n");
			return ret;
		}

		/*
		 * ART partition 0th position will contain Mac address.
		 */
		art_offset =
		((loff_t) qca_smem_flash_info.flash_block_size * start_blocks);

#ifdef CONFIG_IPQ_TINY_SPI_NOR
		flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		if (flash == NULL){
			printf("No SPI flash device found\n");
			ret = -1;
		} else {
#if defined(CONFIG_ART_COMPRESSED) && (defined(CONFIG_GZIP) || defined(CONFIG_LZMA))
		image_buf = map_sysmem(CONFIG_COMPRESSED_LOAD_ADDR, 0);
		load_buf = map_sysmem(CONFIG_COMPRESSED_LOAD_ADDR + 0x100000, 0);
		img_size = qca_smem_flash_info.flash_block_size * size_blocks;
		desMaxSize = 0x100000;
		ret = spi_flash_read(flash, art_offset, img_size, image_buf);
		if (ret == 0) {
			ret = -1;
#ifdef CONFIG_GZIP
			ret = gunzip(load_buf, desMaxSize, image_buf, &img_size);
#endif
#ifdef CONFIG_LZMA
			if (ret != 0)
				ret = lzmaBuffToBuffDecompress(load_buf,
					(SizeT *)&desMaxSize,
					image_buf,
					(SizeT)img_size);
#endif
			if (ret == 0) {
				memcpy(enetaddr, load_buf, length);
			} else {
				printf("Invalid compression type..\n");
				ret = -1;
			}
		}
#else
		ret = spi_flash_read(flash, art_offset, length, enetaddr);
#endif
		}
		/*
		 * Avoid unused warning
		 */
		(void)flash_type;
#else
		ret = nand_read(&nand_info[flash_type],
				art_offset, &length, enetaddr);
#endif
		if (ret < 0)
			printf("ART partition read failed..\n");
#ifdef CONFIG_QCA_MMC
	} else {
		blk_dev = mmc_get_dev(mmc_host.dev_num);
		ret = get_partition_info_efi_by_name(blk_dev, "0:ART", &disk_info);
		/*
		 * ART partition 0th position will contain MAC address.
		 * Read 1 block.
		 */
		if (ret == 0) {
			mmc = mmc_host.mmc;
			ret = mmc->block_dev.block_read
				(mmc_host.dev_num, disk_info.start,
						1, mmc_blks);
			memcpy(enetaddr, mmc_blks, length);
                }
		if (ret < 0)
			printf("ART partition read failed..\n");
#endif
	}
	return ret;
#endif
}

void set_ethmac_addr(void)
{
#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_C) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_D)
	/* Keep the OEM RAM ethaddr unchanged; GMAC reads it directly. */
	return;
#else
	int i, ret;
	uchar enetaddr[CONFIG_IPQ_NO_MACS * 6];
	uchar *mac_addr;
	char ethaddr[16] = "ethaddr";
	char mac[64];
	/* Get the MAC address from ART partition */
	ret = get_eth_mac_address(enetaddr, CONFIG_IPQ_NO_MACS);
	for (i = 0; (ret >= 0) && (i < CONFIG_IPQ_NO_MACS); i++) {
		mac_addr = &enetaddr[i * 6];
		if (!is_valid_ethaddr(mac_addr)) {
			printf("eth%d MAC Address from ART is not valid\n", i);
		} else {
			/*
			 * U-Boot uses these to patch the 'local-mac-address'
			 * dts entry for the ethernet entries, which in turn
			 * will be picked up by the HLOS driver
			 */
			snprintf(mac, sizeof(mac), "%x:%x:%x:%x:%x:%x",
					mac_addr[0], mac_addr[1],
					mac_addr[2], mac_addr[3],
					mac_addr[4], mac_addr[5]);
			setenv(ethaddr, mac);
		}
		snprintf(ethaddr, sizeof(ethaddr), "eth%daddr", (i + 1));
	}
#endif
}
