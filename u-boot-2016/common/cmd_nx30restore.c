// SPDX-License-Identifier: GPL-2.0+
/* Temporary, exact-image recovery of NX30 physical NAND slot B. */
#if !defined(CONFIG_NX30_D_B_RECOVERY)
#error "nx30restore is only for the separately enabled recovery build"
#endif
#if !defined(CONFIG_H3C_HW_D_RAM_READONLY) || \
    !defined(CONFIG_H3C_HW_D_SERVICE_RAM) || \
    defined(CONFIG_H3C_HW_D_NOR_RESCUE)
#error "nx30restore requires the temporary HW-D RAM-only profile"
#endif

#include <common.h>
#include <command.h>
#include <malloc.h>
#include <nand.h>
#include <linux/mtd/mtd.h>
#include <u-boot/sha256.h>
#include <nx30_d_service.h>
#include <nx30_d_read_core.h>
#include <nx30_nor_nand.h>

#define NX30_RESTORE_ADDR       0x44000000UL
#define NX30_RESTORE_BYTES      0x02800000U
#define NX30_RESTORE_B_OFFSET   0x03100000ULL
#define NX30_RESTORE_PEB        0x00020000U
#define NX30_RESTORE_PEB_COUNT  320U
#define NX30_RESTORE_ECC        4U

static int nx30_restore_command_consumed;

static const u8 nx30_restore_sha256[SHA256_SUM_LEN] = {
	0x20,0x3a,0x9e,0xd5,0xe1,0x0a,0x6b,0xac,
	0xa2,0x3d,0x80,0xe9,0x98,0xdc,0xae,0x3f,
	0xb2,0x3c,0x86,0x85,0xd8,0x83,0x4f,0xd4,
	0xdd,0xaf,0x4b,0x36,0x9c,0xac,0x11,0xd0
};

static int nx30_restore_read_window(void *ctx, u64 offset, size_t length,
				    size_t *read_length, u8 *buffer)
{
	struct mtd_info *mtd = ctx;

	return mtd_read(mtd, offset, length, read_length, buffer);
}

static int nx30_restore_preflight(struct mtd_info *mtd, const u8 *image,
				  struct nx30_d_layout *layout,
				  struct nx30_d_slots *slots, u8 *scratch)
{
	u8 digest[SHA256_SUM_LEN];
	u32 peb;
	int ret;

	if (!image || !scratch || !layout || !slots)
		return -EINVAL;
	sha256_csum_wd(image, NX30_RESTORE_BYTES, digest, CHUNKSZ_SHA256);
	if (memcmp(digest, nx30_restore_sha256, sizeof(digest))) {
		printf("NX30RESTORE reject: backup SHA-256 mismatch\n");
		return -EBADMSG;
	}

	ret = nx30_nor_nand_init_once();
	if (ret)
		return ret;
	ret = nx30_nor_nand_set_read_ecc(NX30_RESTORE_ECC);
	if (ret)
		return ret;
	if (!mtd || mtd != &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX] ||
	    mtd->type != MTD_NANDFLASH || mtd->size != 0x08000000ULL ||
	    mtd->erasesize != NX30_RESTORE_PEB || mtd->writesize != 0x800 ||
	    mtd->ecc_strength != NX30_RESTORE_ECC)
		return -ENODEV;

	ret = nx30_d_layout_get(layout);
	if (ret)
		return ret;
	ret = nx30_d_slots_get(slots);
	if (ret)
		return ret;
	if (layout->nand_size != 0x08000000U ||
	    layout->erase_size != NX30_RESTORE_PEB ||
	    layout->write_size != 0x800 || layout->ecc_bits != NX30_RESTORE_ECC ||
	    layout->slot_b.offset != NX30_RESTORE_B_OFFSET ||
	    layout->slot_b.size != NX30_RESTORE_BYTES ||
	    slots->generation != layout->generation ||
	    slots->ecc_bits != NX30_RESTORE_ECC || slots->selector != 1 ||
	    slots->age != 2 || slots->active != NX30_D_SLOT_B) {
		printf("NX30RESTORE reject: layout/ECC/BC state mismatch\n");
		return -EPERM;
	}

	/* Confirm every block is good before any write-window is opened. */
	for (peb = 0; peb < NX30_RESTORE_PEB_COUNT; ++peb) {
		u64 off = NX30_RESTORE_B_OFFSET + (u64)peb * NX30_RESTORE_PEB;
		ret = nand_block_isbad(mtd, off);
		if (ret) {
			printf("NX30RESTORE reject: bad-block scan PEB %u result %d\n",
			       peb, ret);
			return ret > 0 ? -EIO : ret;
		}
	}

	/* The whole physical B extent must still be erased, with no skipped PEBs. */
	for (peb = 0; peb < NX30_RESTORE_PEB_COUNT; ++peb) {
		u64 off = NX30_RESTORE_B_OFFSET + (u64)peb * NX30_RESTORE_PEB;
		u32 i;

		ret = nx30_d_read_extent(nx30_restore_read_window, mtd, off,
					 NX30_RESTORE_PEB, scratch);
		if (ret) {
			printf("NX30RESTORE reject: ECC4 scan PEB %u failed %d\n",
			       peb, ret);
			return ret;
		}
		for (i = 0; i < NX30_RESTORE_PEB; ++i) {
			if (scratch[i] != 0xff) {
				printf("NX30RESTORE reject: B is not fully erased at PEB %u byte %u\n",
				       peb, i);
				return -EUCLEAN;
			}
		}
	}
	return 0;
}

static int do_nx30restore(cmd_tbl_t *cmdtp, int flag, int argc,
			  char * const argv[])
{
	struct mtd_info *mtd = &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
	struct nx30_d_layout layout;
	struct nx30_d_slots slots;
	struct erase_info erase;
	const u8 *image = (const u8 *)(uintptr_t)NX30_RESTORE_ADDR;
	u8 *writebuf = NULL, *verify = NULL;
	u32 peb, written_pebs = 0;
	int ret, opened = 0, close_ret = 0;

	(void)cmdtp;
	(void)flag;
	if (argc != 3 || strcmp(argv[1], "44000000") ||
	    strcmp(argv[2], "magicconfirm"))
		return CMD_RET_USAGE;
	if (nx30_restore_command_consumed) {
		puts("NX30RESTORE command already consumed in this boot; do not retry\n");
		return CMD_RET_FAILURE;
	}
	/* One accepted invocation consumes the command even if preflight fails. */
	nx30_restore_command_consumed = 1;
	if (!is_memory_region_available(NX30_RESTORE_ADDR, NX30_RESTORE_BYTES)) {
		puts("NX30RESTORE reject: fixed 40MiB RAM input range unavailable\n");
		return CMD_RET_FAILURE;
	}

	writebuf = memalign(ARCH_DMA_MINALIGN, NX30_RESTORE_PEB);
	verify = memalign(ARCH_DMA_MINALIGN, NX30_RESTORE_PEB);
	if (!writebuf || !verify) {
		ret = -ENOMEM;
		goto preflight_fail;
	}
	ret = nx30_restore_preflight(mtd, image, &layout, &slots, verify);
	if (ret)
		goto preflight_fail;

	printf("NX30RESTORE preflight PASS: C8:41 ECC4 RAM role, B=0x03100000/40MiB, selector=1 AGE=2, 320 good+erased PEBs\n");
	ret = nx30_d_qpic_write_window_open(mtd, NX30_RESTORE_B_OFFSET,
					    NX30_RESTORE_BYTES, NX30_RESTORE_ECC);
	if (ret) {
		printf("NX30RESTORE stopped before erase: window open failed %d\n", ret);
		goto out;
	}
	opened = 1;

	/* One PEB at a time: erase, write, full decoded-data compare, then advance. */
	for (peb = 0; peb < NX30_RESTORE_PEB_COUNT; ++peb) {
		u64 off = NX30_RESTORE_B_OFFSET + (u64)peb * NX30_RESTORE_PEB;
		size_t done = 0;

		memcpy(writebuf, image + (size_t)peb * NX30_RESTORE_PEB,
		       NX30_RESTORE_PEB);
		memset(&erase, 0, sizeof(erase));
		erase.mtd = mtd;
		erase.addr = off;
		erase.len = NX30_RESTORE_PEB;
		ret = mtd_erase(mtd, &erase);
		if (ret)
			goto write_fail;
		ret = mtd_write(mtd, off, NX30_RESTORE_PEB, &done, writebuf);
		if (ret || done != NX30_RESTORE_PEB) {
			if (!ret)
				ret = -EIO;
			goto write_fail;
		}
		ret = nx30_d_read_extent(nx30_restore_read_window, mtd, off,
					 NX30_RESTORE_PEB, verify);
		if (ret)
			goto write_fail;
		if (memcmp(verify, writebuf, NX30_RESTORE_PEB)) {
			ret = -EIO;
			goto write_fail;
		}
		written_pebs++;
	}
	ret = 0;
	goto close_window;

write_fail:
	printf("NX30RESTORE FAIL-STOP at PEB %u after %u verified; do not retry this candidate (error %d)\n",
	       peb, written_pebs, ret);
close_window:
	if (opened) {
		nx30_d_qpic_write_window_close();
		close_ret = nx30_d_qpic_write_window_close_error();
		if (close_ret) {
			printf("NX30RESTORE PROTECTION RESTORE FAILURE %d; stop and do not retry\n",
			       close_ret);
			ret = close_ret;
		}
	}
	if (!ret) {
		printf("NX30RESTORE PASS: physical B restored; 320/320 PEBs fully compared; BC unchanged (selector=%u AGE=%u)\n",
		       slots.selector, slots.age);
	} else if (written_pebs == NX30_RESTORE_PEB_COUNT) {
		printf("NX30RESTORE stopped after complete data readback due to window-close failure; do not retry\n");
	}
	goto out;

preflight_fail:
	printf("NX30RESTORE rejected before write: %d\n", ret);
out:
	free(writebuf);
	free(verify);
	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(nx30restore, 3, 0, do_nx30restore,
	   "restore only the exact NX30 physical-B recovery backup",
	   "44000000 magicconfirm - fixed image/address; all recovery guards are mandatory");
