/*
 * (C) Copyright 2000-2010
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * (C) Copyright 2008
 * Stuart Wood, Lab X Technologies <stuart.wood@labxtechnologies.com>
 *
 * (C) Copyright 2004
 * Jian Zhang, Texas Instruments, jzhang@ti.com.
 *
 * (C) Copyright 2001 Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Andreas Heppel <aheppel@sysgo.de>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <environment.h>
#include <linux/stddef.h>
#include <malloc.h>
#include <memalign.h>
#include <nand.h>
#include <search.h>
#include <errno.h>
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
#include <asm/arch-qca-common/smem.h>
#include <rt3000_b_fw_write.h>
#include <rt3000_b_env.h>
#endif

#include <h3c_board_profile.h>
#define RT3_ENV_BOARD_LABEL H3C_BOARD_LABEL

#if defined(CONFIG_CMD_SAVEENV) && defined(CONFIG_CMD_NAND)
#define CMD_SAVEENV
#elif defined(CONFIG_ENV_OFFSET_REDUND)
#error CONFIG_ENV_OFFSET_REDUND must have CONFIG_CMD_SAVEENV & CONFIG_CMD_NAND
#endif
#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
/* Compile the checked read/tail helper; saveenv remains unregistered and QPIC rejects writes. */
#ifndef CMD_SAVEENV
#define CMD_SAVEENV
#endif
#endif

#if defined(CONFIG_ENV_SIZE_REDUND) &&	\
	(CONFIG_ENV_SIZE_REDUND != CONFIG_ENV_SIZE)
#error CONFIG_ENV_SIZE_REDUND should be the same as CONFIG_ENV_SIZE
#endif

#ifndef CONFIG_ENV_RANGE
#define CONFIG_ENV_RANGE	CONFIG_ENV_SIZE
#endif

char *nand_env_name_spec = "NAND";

#if defined(ENV_IS_EMBEDDED)
env_t *nand_env_ptr = &environment;
#elif defined(CONFIG_NAND_ENV_DST)
env_t *nand_env_ptr = (env_t *)CONFIG_NAND_ENV_DST;
#else /* ! ENV_IS_EMBEDDED */
env_t *nand_env_ptr;
#endif /* ENV_IS_EMBEDDED */

DECLARE_GLOBAL_DATA_PTR;

int nand_env_device = 0;
/*
 * This is called before nand_init() so we can't read NAND to
 * validate env data.
 *
 * Mark it OK for now. env_relocate() in env_common.c will call our
 * relocate function which does the real validation.
 *
 * When using a NAND boot image (like sequoia_nand), the environment
 * can be embedded or attached to the U-Boot image in NAND flash.
 * This way the SPL loads not only the U-Boot image from NAND but
 * also the environment.
 */
int nand_env_init(void)
{
#if defined(ENV_IS_EMBEDDED) || defined(CONFIG_NAND_ENV_DST)
	int crc1_ok = 0, crc2_ok = 0;
	env_t *tmp_env1;

#ifdef CONFIG_ENV_OFFSET_REDUND
	env_t *tmp_env2;

	tmp_env2 = (env_t *)((ulong)env_ptr + CONFIG_ENV_SIZE);
	crc2_ok = crc32(0, tmp_env2->data, ENV_SIZE) == tmp_env2->crc;
#endif
	tmp_env1 = env_ptr;
	crc1_ok = crc32(0, tmp_env1->data, ENV_SIZE) == tmp_env1->crc;

	if (!crc1_ok && !crc2_ok) {
		gd->env_addr	= 0;
		gd->env_valid	= 0;

		return 0;
	} else if (crc1_ok && !crc2_ok) {
		gd->env_valid = 1;
	}
#ifdef CONFIG_ENV_OFFSET_REDUND
	else if (!crc1_ok && crc2_ok) {
		gd->env_valid = 2;
	} else {
		/* both ok - check serial */
		if (tmp_env1->flags == 255 && tmp_env2->flags == 0)
			gd->env_valid = 2;
		else if (tmp_env2->flags == 255 && tmp_env1->flags == 0)
			gd->env_valid = 1;
		else if (tmp_env1->flags > tmp_env2->flags)
			gd->env_valid = 1;
		else if (tmp_env2->flags > tmp_env1->flags)
			gd->env_valid = 2;
		else /* flags are equal - almost impossible */
			gd->env_valid = 1;
	}

	if (gd->env_valid == 2)
		env_ptr = tmp_env2;
	else
#endif
	if (gd->env_valid == 1)
		env_ptr = tmp_env1;

	gd->env_addr = (ulong)env_ptr->data;

#else /* ENV_IS_EMBEDDED || CONFIG_NAND_ENV_DST */
	gd->env_addr	= (ulong)&default_environment[0];
	gd->env_valid	= 1;
#endif /* ENV_IS_EMBEDDED || CONFIG_NAND_ENV_DST */

	return 0;
}

#ifdef CMD_SAVEENV
/*
 * The legacy NAND code saved the environment in the first NAND device i.e.,
 * nand_dev_desc + 0. This is also the behaviour using the new NAND code.
 */
#if !(defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
static int writeenv(size_t offset, u_char *buf)
{
	size_t end = offset + board_env_size;
	size_t amount_saved = 0;
	size_t blocksize, len;
	u_char *char_ptr;

	blocksize = nand_info[nand_env_device].erasesize;
	len = min(blocksize, (size_t)CONFIG_ENV_RANGE);

	while (amount_saved < CONFIG_ENV_RANGE && offset < end) {
		if (nand_block_isbad(&nand_info[nand_env_device], offset)) {
			offset += blocksize;
		} else {
			char_ptr = &buf[amount_saved];
			if (nand_write(&nand_info[nand_env_device],
				       offset, &len, char_ptr))
				return 1;

			offset += blocksize;
			amount_saved += len;
		}
	}
	if (amount_saved != CONFIG_ENV_RANGE)
		return 1;

	return 0;
}
#endif

struct env_location {
	const char *name;
	nand_erase_options_t erase_opts;
};

#if !(defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
static int erase_and_write_env(const struct env_location *location,
		u_char *env_new)
{
	int ret = 0;

	printf("Erasing %s...\n", location->name);
	if (nand_erase_opts(&nand_info[nand_env_device], &location->erase_opts))
		return 1;

	printf("Writing to %s... ", location->name);
	ret = writeenv(location->erase_opts.offset, env_new);
	puts(ret ? "FAILED!\n" : "OK\n");

	return ret;
}
#endif

#ifdef CONFIG_ENV_OFFSET_REDUND
static unsigned char env_flags;
#endif

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
/* Resolve and preflight the exact single APPSBLENV partition before any write. */
static int rt3_env_target(nand_info_t **nand_out, loff_t *offset_out,
			  size_t *size_out)
{
	nand_info_t *nand;
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	u32 start_blocks = 0, size_blocks = 0;
	u64 offset, size, end;
	unsigned int i;
	int ret;

	if (nand_env_device != CONFIG_NAND_FLASH_INFO_IDX ||
	    nand_info[CONFIG_NAND_FLASH_INFO_IDX].size != (128ULL << 20) ||
	    nand_info[CONFIG_NAND_FLASH_INFO_IDX].erasesize != (128U << 10) ||
	    nand_info[CONFIG_NAND_FLASH_INFO_IDX].writesize != 2048 ||
	    CONFIG_ENV_SIZE != CONFIG_ENV_SIZE_MAX ||
	    CONFIG_ENV_RANGE != CONFIG_ENV_SIZE_MAX ||
	    CONFIG_ENV_SIZE_MAX != (256U << 10) ||
	    board_env_range != CONFIG_ENV_SIZE_MAX ||
	    board_env_size != (512U << 10) || board_env_offset < 0)
		return -EROFS;
	nand = &nand_info[nand_env_device];
	if (!nand->name || !nand->size || !nand->erasesize || !nand->writesize ||
	    nand->erasesize != sfi->flash_block_size ||
	    CONFIG_ENV_SIZE_MAX % nand->erasesize ||
	    CONFIG_ENV_SIZE_MAX % nand->writesize)
		return -EROFS;
	ret = smem_getpart("0:APPSBLENV", &start_blocks, &size_blocks);
	if (ret || !size_blocks)
		return -EROFS;
	offset = (u64)start_blocks * nand->erasesize;
	size = (u64)size_blocks * nand->erasesize;
	end = offset + size;
	if (end < offset || offset != 0x480000 || size != 0x80000 ||
	    offset != (u64)board_env_offset ||
	    offset != (u64)CONFIG_ENV_OFFSET || size != (u64)board_env_size ||
	    size != (512U << 10) || end > nand->size ||
	    offset % nand->erasesize || size % nand->erasesize)
		return -EROFS;
	/* A bad block would require spilling into a protected adjacent partition. */
	for (i = 0; i < size / nand->erasesize; i++) {
		ret = nand_block_isbad(nand, offset + (u64)i * nand->erasesize);
		if (ret)
			return ret < 0 ? ret : -ENOSPC;
	}
	*nand_out = nand;
	*offset_out = offset;
	*size_out = size;
	return 0;
}

static int rt3_env_nand_read(nand_info_t *nand, loff_t offset, u_char *buf,
			     size_t size)
{
	size_t read_len = size;
	int ret = nand_read(nand, offset, &read_len, buf);

	if (ret == -EUCLEAN && read_len == size)
		return 0;
	return ret || read_len != size ? -EIO : 0;
}

static int rt3_env_nand_write(nand_info_t *nand, loff_t offset,
			      u_char *buf, size_t size)
{
	size_t write_len = size;
	int ret = nand_write(nand, offset, &write_len, buf);

	return ret || write_len != size ? -EIO : 0;
}

static int rt3_env_erase(nand_info_t *nand, loff_t offset, size_t size)
{
	nand_erase_options_t opts;

	memset(&opts, 0, sizeof(opts));
	opts.offset = offset;
	opts.length = size;
	return nand_erase_opts(nand, &opts) ? -EIO : 0;
}

int rt3_nand_env_tail_matches(const void *tail, size_t tail_size)
{
	nand_info_t *nand;
	loff_t offset;
	size_t size;
	u_char *current;
	int ret;

	if (!tail || CONFIG_ENV_SIZE_MAX != (256U << 10) ||
	    tail_size != (256U << 10))
		return -EINVAL;
	ret = rt3_env_target(&nand, &offset, &size);
	if (ret)
		return ret;
	if (size != (512U << 10))
		return -EROFS;
	current = (u_char *)memalign(ARCH_DMA_MINALIGN, size);
	if (!current)
		return -ENOMEM;
	ret = rt3_env_nand_read(nand, offset, current, size);
	if (!ret && memcmp((const u_char *)tail, current + CONFIG_ENV_SIZE,
			  tail_size))
		ret = -EINVAL;
	free(current);
	return ret;
}

static int rt3_nand_saveenv(void)
{
	nand_info_t *nand;
	loff_t offset;
	size_t size;
	env_t *env_new = NULL;
	u_char *old_bytes = NULL, *new_partition = NULL, *verify = NULL;
	int ret, rollback_ret;
	int window_open = 0;

	ret = rt3_env_target(&nand, &offset, &size);
	if (ret)
		return ret;
	env_new = (env_t *)memalign(ARCH_DMA_MINALIGN, CONFIG_ENV_SIZE);
	old_bytes = (u_char *)memalign(ARCH_DMA_MINALIGN, size);
	new_partition = (u_char *)memalign(ARCH_DMA_MINALIGN, size);
	verify = (u_char *)memalign(ARCH_DMA_MINALIGN, size);
	if (!env_new || !old_bytes || !new_partition || !verify) {
		ret = -ENOMEM;
		goto out;
	}
	memset(env_new, 0, CONFIG_ENV_SIZE);
	ret = env_export(env_new);
	if (ret) {
		ret = -EIO;
		goto out;
	}
	if (crc32(0, env_new->data, ENV_SIZE) != env_new->crc) {
		ret = -EIO;
		goto out;
	}
	/* Preserve a byte-exact rollback image before opening the write aperture. */
	ret = rt3_env_nand_read(nand, offset, old_bytes, size);
	if (ret)
		goto out;
	/* Keep the OEM's CRC-protected 256 KiB environment and preserve its tail. */
	memcpy(new_partition, env_new, CONFIG_ENV_SIZE);
	memcpy(new_partition + CONFIG_ENV_SIZE, old_bytes + CONFIG_ENV_SIZE,
	       size - CONFIG_ENV_SIZE);
	ret = rt3_metadata_write_window_open(nand, "0:APPSBLENV", offset, size);
	if (ret)
		goto out;
	window_open = 1;
	ret = rt3_env_erase(nand, offset, size);
	if (!ret)
		ret = rt3_env_nand_write(nand, offset, new_partition, size);
	if (!ret)
		ret = rt3_env_nand_read(nand, offset, verify, size);
	if (!ret && memcmp(verify, new_partition, size))
		ret = -EIO;
	if (!ret)
		goto out;

	/* Best-effort exact restoration on synchronous erase/program/readback errors. */
	rollback_ret = rt3_env_erase(nand, offset, size);
	if (!rollback_ret)
		rollback_ret = rt3_env_nand_write(nand, offset, old_bytes, size);
	if (!rollback_ret)
		rollback_ret = rt3_env_nand_read(nand, offset, verify, size);
	if (!rollback_ret && memcmp(verify, old_bytes, size))
		rollback_ret = -EIO;
	if (rollback_ret)
		printf("%s APPSBLENV rollback failed (%d); persistent ENV needs recovery\n",
		       RT3_ENV_BOARD_LABEL, rollback_ret);
out:
	if (window_open) {
		rt3_fw_write_window_close();
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
		if (rt3_fw_write_window_close_status()) ret = -EIO;
#endif
	}
	free(verify);
	free(new_partition);
	free(old_bytes);
	free(env_new);
	return ret;
}
#endif

int nand_saveenv(void)
{
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	return rt3_nand_saveenv();
#else
	int	ret = 0;
	int	env_idx = 0;
	struct env_location location[] = {
		{
			.name = "NAND",
			.erase_opts = {
				.length = board_env_size,
				.offset = CONFIG_ENV_OFFSET,
			},
		},
#ifdef CONFIG_ENV_OFFSET_REDUND
		{
			.name = "redundant NAND",
			.erase_opts = {
				.length = CONFIG_ENV_RANGE,
				.offset = CONFIG_ENV_OFFSET_REDUND,
			},
		},
#endif
	};

	env_t *env_new = (env_t *)memalign(ARCH_DMA_MINALIGN, CONFIG_ENV_SIZE);
	if (!env_new) {
		printf("Error: Cannot allocate %d bytes\n", CONFIG_ENV_SIZE);
		return 1;
	}

	if (CONFIG_ENV_RANGE > board_env_size) {
		free(env_new);
		return 1;
	}

	ret = env_export(env_new);
	if (ret) {
		free(env_new);
		return ret;
	}

#ifdef CONFIG_ENV_OFFSET_REDUND
	env_new->flags = ++env_flags; /* increase the serial */
	env_idx = (gd->env_valid == 1);
#endif
	ret = erase_and_write_env(&location[env_idx], (u_char *)env_new);
#ifdef CONFIG_ENV_OFFSET_REDUND
	if (!ret) {
		/* preset other copy for next write */
		gd->env_valid = gd->env_valid == 2 ? 1 : 2;
		free(env_new);
		return ret;
	}

	env_idx = (env_idx + 1) & 1;
	ret = erase_and_write_env(&location[env_idx], (u_char *)env_new);
	if (!ret)
		printf("Warning: primary env write failed,"
				" redundancy is lost!\n");
#endif
	free(env_new);
	return ret;
#endif
}
#endif /* CMD_SAVEENV */

#if defined(CONFIG_SPL_BUILD)
static int readenv(size_t offset, u_char *buf)
{
	return nand_spl_load_image(offset, CONFIG_ENV_SIZE, buf);
}
#else
static int readenv(size_t offset, u_char *buf)
{
	size_t end = offset + board_env_size;
	size_t amount_loaded = 0;
	size_t blocksize, len;
	u_char *char_ptr;

	blocksize = nand_info[nand_env_device].erasesize;
	if (!blocksize)
		return 1;

	len = min(blocksize, (size_t)CONFIG_ENV_RANGE);

	while (amount_loaded < CONFIG_ENV_RANGE && offset < end) {
		if (nand_block_isbad(&nand_info[nand_env_device], offset)) {
			offset += blocksize;
		} else {
			char_ptr = &buf[amount_loaded];
			if (nand_read_skip_bad(&nand_info[nand_env_device], offset,
					       &len, NULL,
					       nand_info[nand_env_device].size, char_ptr))
				return 1;

			offset += blocksize;
			amount_loaded += len;
		}
	}

	if (amount_loaded != CONFIG_ENV_RANGE)
		return 1;

	return 0;
}
#endif /* #if defined(CONFIG_SPL_BUILD) */

#ifdef CONFIG_ENV_OFFSET_OOB
int get_nand_env_oob(nand_info_t *nand, unsigned long *result)
{
	struct mtd_oob_ops ops;
	uint32_t oob_buf[ENV_OFFSET_SIZE / sizeof(uint32_t)];
	int ret;

	ops.datbuf	= NULL;
	ops.mode	= MTD_OOB_AUTO;
	ops.ooboffs	= 0;
	ops.ooblen	= ENV_OFFSET_SIZE;
	ops.oobbuf	= (void *)oob_buf;

	ret = nand->read_oob(nand, ENV_OFFSET_SIZE, &ops);
	if (ret) {
		printf("error reading OOB block 0\n");
		return ret;
	}

	if (oob_buf[0] == ENV_OOB_MARKER) {
		*result = oob_buf[1] * nand->erasesize;
	} else if (oob_buf[0] == ENV_OOB_MARKER_OLD) {
		*result = oob_buf[1];
	} else {
		printf("No dynamic environment marker in OOB block 0\n");
		return -ENOENT;
	}

	return 0;
}
#endif

#ifdef CONFIG_ENV_OFFSET_REDUND
void nand_env_relocate_spec(void)
{
#if !defined(ENV_IS_EMBEDDED)
	int read1_fail = 0, read2_fail = 0;
	int crc1_ok = 0, crc2_ok = 0;
	env_t *ep, *tmp_env1, *tmp_env2;

	tmp_env1 = (env_t *)malloc(CONFIG_ENV_SIZE);
	tmp_env2 = (env_t *)malloc(CONFIG_ENV_SIZE);
	if (tmp_env1 == NULL || tmp_env2 == NULL) {
		puts("Can't allocate buffers for environment\n");
		set_default_env("!malloc() failed");
		goto done;
	}

	read1_fail = readenv(CONFIG_ENV_OFFSET, (u_char *) tmp_env1);
	read2_fail = readenv(CONFIG_ENV_OFFSET_REDUND, (u_char *) tmp_env2);

	if (read1_fail && read2_fail)
		puts("*** Error - No Valid Environment Area found\n");
	else if (read1_fail || read2_fail)
		puts("*** Warning - some problems detected "
		     "reading environment; recovered successfully\n");

	crc1_ok = !read1_fail &&
		(crc32(0, tmp_env1->data, ENV_SIZE) == tmp_env1->crc);
	crc2_ok = !read2_fail &&
		(crc32(0, tmp_env2->data, ENV_SIZE) == tmp_env2->crc);

	if (!crc1_ok && !crc2_ok) {
		set_default_env("!bad CRC");
		goto done;
	} else if (crc1_ok && !crc2_ok) {
		gd->env_valid = 1;
	} else if (!crc1_ok && crc2_ok) {
		gd->env_valid = 2;
	} else {
		/* both ok - check serial */
		if (tmp_env1->flags == 255 && tmp_env2->flags == 0)
			gd->env_valid = 2;
		else if (tmp_env2->flags == 255 && tmp_env1->flags == 0)
			gd->env_valid = 1;
		else if (tmp_env1->flags > tmp_env2->flags)
			gd->env_valid = 1;
		else if (tmp_env2->flags > tmp_env1->flags)
			gd->env_valid = 2;
		else /* flags are equal - almost impossible */
			gd->env_valid = 1;
	}

	free(env_ptr);

	if (gd->env_valid == 1)
		ep = tmp_env1;
	else
		ep = tmp_env2;

	env_flags = ep->flags;
	env_import((char *)ep, 0);

done:
	free(tmp_env1);
	free(tmp_env2);

#endif /* ! ENV_IS_EMBEDDED */
}
#else /* ! CONFIG_ENV_OFFSET_REDUND */
/*
 * The legacy NAND code saved the environment in the first NAND
 * device i.e., nand_dev_desc + 0. This is also the behaviour using
 * the new NAND code.
 */
void nand_env_relocate_spec(void)
{
#if !defined(ENV_IS_EMBEDDED)
	int ret;
	char *buf = NULL;
	buf = (char *)memalign(ARCH_DMA_MINALIGN, CONFIG_ENV_SIZE);
	if (!buf) {
		printf("Error: Cannot allocate %d bytes\n", CONFIG_ENV_SIZE);
		set_default_env(NULL);
		return;
        }

#if defined(CONFIG_ENV_OFFSET_OOB)
	ret = get_nand_env_oob(&nand_info[nand_env_device], &nand_env_oob_offset);
	/*
	 * If unable to read environment offset from NAND OOB then fall through
	 * to the normal environment reading code below
	 */
	if (!ret) {
		printf("Found Environment offset in OOB..\n");
	} else {
		set_default_env("!no env offset in OOB");
		free(buf);
		return;
	}
#endif

	ret = readenv(CONFIG_ENV_OFFSET, (u_char *)buf);
	if (ret) {
		set_default_env("!readenv() failed");
		free(buf);
		return;
	}

	env_import(buf, 1);
	free(buf);
#endif /* ! ENV_IS_EMBEDDED */
}
#endif /* CONFIG_ENV_OFFSET_REDUND */
