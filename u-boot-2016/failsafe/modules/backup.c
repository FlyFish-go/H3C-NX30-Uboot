/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe flash backup
 */

/*
 * Modified by: chenxin527
 */

#include <common.h>
#include <command.h>
#include <linux/ctype.h>
#include <part.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <mmc.h>
#include <sdhci.h>
#include <spi.h>
#include <spi_flash.h>
#include <malloc.h>
#include <errno.h>
#include <net/httpd.h>
#include <ipq_api.h>

#include "modules.h"

#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif

DECLARE_GLOBAL_DATA_PTR;

#define GPT_PART_NAME "0:GPT"
#define GPT_SIZE_IN_BLOCKS 34

#define SMEM_PTN_NAME_MAX     16

struct smem_part {
	char name[SMEM_PTN_NAME_MAX];
	unsigned start;
	unsigned size;
	uint32_t flash_type;
	int which_flash;
};

enum backup_src {
	BACKUP_SRC_SMEM = 0,
	BACKUP_SRC_MMC = 1,
};

enum backup_phase {
	BACKUP_PHASE_HDR = 0,
	BACKUP_PHASE_DATA = 1,
};

struct backup_session {
	enum backup_src src;
	enum backup_phase phase;

	u64 start;
	u64 total;
	u64 cur;
	u64 target_size;

	char filename[128];
	char hdr[512];
	int hdr_len;

	void *buf;
	size_t buf_size;

	struct smem_part smem;

	struct mmc *mmc;
	u64 mmc_base;

	struct spi_flash *spi;
	nand_info_t *nand;

	/* Full NAND partition backup: good blocks only, never past the end. */
	bool skip_bad;
	u64 phys;
};

static void str_sanitize_component(char *s, char sep)
{
	char *p;

	if (!s)
		return;

	for (p = s; *p; p++) {
		unsigned char c = *p;

		if (isalnum(c) || c == '-' || c == '_' || c == '.')
			continue;

		*p = sep;
	}
}

static int parse_u64_len(const char *s, u64 *out)
{
	char *end;
	unsigned long long v;

	if (!s || !*s || !out)
		return -EINVAL;

	v = simple_strtoull(s, &end, 0);
	if (end == s)
		return -EINVAL;

	while (*end == ' ' || *end == '\t')
		end++;

	if (!*end) {
		*out = (u64)v;
		return 0;
	}

	if (!strcasecmp(end, "k") || !strcasecmp(end, "kb") ||
	    !strcasecmp(end, "kib")) {
		*out = (u64)v * 1024ULL;
		return 0;
	} else if (!strcasecmp(end, "m") || !strcasecmp(end, "mb") ||
	           !strcasecmp(end, "mib")) {
		*out = (u64)v * 1024ULL * 1024ULL;
		return 0;
	}

	return -EINVAL;
}

static int smem_read_data(struct smem_part *smem_part, u64 offset, size_t size,
		size_t *readsize, void *buf, size_t buf_size)
{
	struct spi_flash *spi;
	nand_info_t *nand;
	ulong off, adj_offset;
    size_t adj_size, tmp_buf_size;
	void *tmp_buf;
	int ret;

	*readsize = size;

	if (smem_part->flash_type == SMEM_BOOT_SPI_FLASH &&
			smem_part->which_flash == 0) {
		spi = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		ret = spi_flash_read(spi, offset, size, buf);

		return ret;
	}

	nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	off = offset % nand->writesize;

	if (off) {
		adj_offset = offset - off;
		adj_size = size + off;
		tmp_buf_size = adj_size;

		tmp_buf = malloc(tmp_buf_size);
		if (!tmp_buf)
			return -ENOMEM;

		ret = nand_read_skip_bad(nand, adj_offset, &adj_size,
				NULL, tmp_buf_size, (u_char *)tmp_buf);
		memcpy(buf, tmp_buf + off, size);
		free(tmp_buf);
	} else {
		ret = nand_read_skip_bad(nand, offset, readsize,
				NULL, buf_size, (u_char *)buf);
	}

	return ret;
}

static int mmc_read_data(struct mmc *mmc, u64 offset, size_t size,
		void *buf, size_t buf_size)
{
	block_dev_desc_t *bd = &mmc->block_dev;
	ulong off = 0, start_block = 0, end_block = 0;
	ulong readblks, num_blocks;
	void *tmp_buf;

	if (!bd->blksz)
		return -EINVAL;

	off = offset % bd->blksz; /* 块内偏移（单位：字节） */
	start_block = offset / bd->blksz; /* 起始块 */
	end_block = (offset + size - 1) / bd->blksz; /* 结束块 */
	num_blocks = end_block - start_block + 1; /* 需要读取的块数 */

	tmp_buf = malloc(num_blocks * bd->blksz);
	if (!tmp_buf)
		return -ENOMEM;

	readblks = bd->block_read(mmc_host.dev_num, start_block, num_blocks, tmp_buf);

	memcpy(buf, tmp_buf + off, size);

	free(tmp_buf);

	return (readblks == num_blocks) ? 0 : 1;
}

static void backup_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	struct backup_session *st;
	struct httpd_form_value *mode, *target, *start, *size;
	char target_name[64] = "";
	char storage_sel[16] = "";
	u64 start_offset = 0, total_size = 0;
	uint32_t offset_bytes = 0, size_bytes = 0;
	int ret;

	if (status == HTTP_CB_NEW) {
		mode = httpd_request_find_value(request, "mode");
		target = httpd_request_find_value(request, "target");
		start = httpd_request_find_value(request, "start");
		size = httpd_request_find_value(request, "size");

		if (!mode || !mode->data || !target || !target->data)
			goto bad;

		strlcpy(target_name, target->data, sizeof(target_name));

		if (!strncmp(target_name, "raw:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "raw", sizeof(storage_sel));
		} else if (!strncmp(target_name, "smem:", 5)) {
			memmove(target_name, target_name + 5, strlen(target_name + 5) + 1);
			strlcpy(storage_sel, "smem", sizeof(storage_sel));
		} else if (!strncmp(target_name, "mmc:", 4)) {
			memmove(target_name, target_name + 4, strlen(target_name + 4) + 1);
			strlcpy(storage_sel, "mmc", sizeof(storage_sel));
		} else {
			goto bad;
		}

		if (!strcmp(mode->data, "full")) {
			start_offset = 0;
			total_size = ULLONG_MAX;
		} else if (!strcmp(mode->data, "range")) {
			if (!start || !size || !start->data || !size->data)
				goto bad;
			if (parse_u64_len(start->data, &start_offset))
				goto bad;
			if (parse_u64_len(size->data, &total_size))
				goto bad;
		} else {
			goto bad;
		}

		st = calloc(1, sizeof(*st));
		if (!st)
			goto oom;

		st->buf_size = 64 * 1024;
		st->buf = malloc(st->buf_size);
		if (!st->buf) {
			free(st);
			goto oom;
		}

		/* open target and get size */
		if (!strcasecmp(storage_sel, "raw")) {
			if (!strncmp(target_name, "spi", 3)) {
				st->spi = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
							CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
				if (!st->spi)
					goto bad_target;

				st->src = BACKUP_SRC_SMEM;
				st->target_size = st->spi->size;

				st->smem.start = 0;
				st->smem.size = st->target_size;
				st->smem.flash_type = SMEM_BOOT_SPI_FLASH;
				st->smem.which_flash = 0;
			} else if (!strncmp(target_name, "mmc", 3)) {
				if (!has_mmc())
					goto bad_target;

				st->mmc = find_mmc_device(mmc_host.dev_num);
				if (!st->mmc)
					goto bad_target;

				st->src = BACKUP_SRC_MMC;
				st->mmc_base = 0;
				st->target_size = st->mmc->capacity_user;
			} else if (!strncmp(target_name, "nand", 4)) {
				st->nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
				if (!st->nand)
					goto bad_target;

				st->src = BACKUP_SRC_SMEM;
				st->target_size = st->nand->size;

				st->smem.start = 0;
				st->smem.size = st->target_size;
				st->smem.flash_type = SMEM_BOOT_NAND_FLASH;
				st->smem.which_flash = 1;
			} else {
				goto bad_target;
			}
		} else if (!strcasecmp(storage_sel, "smem")) {
			ret = getpart_offset_size(target_name, &offset_bytes, &size_bytes);
			if (ret)
				goto bad_target;

			st->src = BACKUP_SRC_SMEM;
			st->target_size = (u64)size_bytes;

			st->smem.start = offset_bytes;
			st->smem.size = size_bytes;
			st->smem.flash_type = qca_smem_flash_info.flash_type;
			st->smem.which_flash = get_which_flash_param(target_name);
			strlcpy(st->smem.name, target_name, sizeof(st->smem.name));

			if (st->smem.which_flash && !has_nand())
				goto bad_target;
		} else {
			/* MMC path */
			if (!has_mmc())
				goto bad_target;

			st->mmc = find_mmc_device(mmc_host.dev_num);
			if (!st->mmc)
				goto bad_target;

			st->src = BACKUP_SRC_MMC;

			if (!strncmp(target_name, GPT_PART_NAME, sizeof(GPT_PART_NAME))) {
				st->mmc_base = 0;
				st->target_size = GPT_SIZE_IN_BLOCKS * st->mmc->block_dev.blksz;
			} else {
				disk_partition_t dpart = {0};

				ret = get_partition_info_efi_by_name(&st->mmc->block_dev,
						target_name, &dpart);
				if (ret)
					goto bad_target;

				st->mmc_base = (u64)dpart.start * dpart.blksz;
				st->target_size = (u64)dpart.size * dpart.blksz;
			}
		}

		/* range normalization */
		if (!strcmp(mode->data, "full")) {
			start_offset = 0;
			total_size = st->target_size;
		}

		if (start_offset + total_size > st->target_size)
			goto bad_range;

		/*
		 * nand_read_skip_bad() would step over a bad block and read into the
		 * next partition. Send only this partition's good blocks, so a UBI
		 * backup can be written back to the same partition.
		 */
		if (!strcmp(mode->data, "full") && !strcasecmp(storage_sel, "smem") &&
		    !(st->smem.flash_type == SMEM_BOOT_SPI_FLASH && !st->smem.which_flash)) {
			nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
			u64 off;

			if (nand->erasesize && !(st->smem.start % nand->erasesize) &&
			    !(st->smem.size % nand->erasesize)) {
				st->skip_bad = true;
				total_size = 0;
				for (off = 0; off < st->smem.size; off += nand->erasesize) {
					ret = nand_block_isbad(nand, st->smem.start + off);
					if (ret < 0)
						goto bad_target;
					if (!ret)
						total_size += nand->erasesize;
				}
			}
		}

		st->start = start_offset;
		st->total = total_size;
		st->cur = 0;
		st->phase = BACKUP_PHASE_HDR;

		/* filename */
		str_sanitize_component(target_name, '-');

		if (!strcmp(mode->data, "full")) {
			snprintf(st->filename, sizeof(st->filename),
				"%s_%s.bin", storage_sel, target_name);
		} else {
			snprintf(st->filename, sizeof(st->filename),
				"%s_%s_0x%llx@0x%llx.bin",
				storage_sel, target_name,
				(unsigned long long)st->total, (unsigned long long)st->start);
		}

		/* build HTTP header (CUSTOM response must include header) */
		st->hdr_len = snprintf(st->hdr, sizeof(st->hdr),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: application/octet-stream\r\n"
			"Content-Length: %llu\r\n"
			"Content-Disposition: attachment; filename=\"%s\"\r\n"
			"Cache-Control: no-store\r\n"
			"Connection: close\r\n"
			"\r\n",
			(unsigned long long)st->total,
			st->filename);

		response->session_data = st;
		response->status = HTTP_RESP_CUSTOM;
		response->data = st->hdr;
		response->size = st->hdr_len;
		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		u64 remain;
		size_t to_read, got = 0;

		st = response->session_data;
		if (!st) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		if (st->phase == BACKUP_PHASE_HDR)
			st->phase = BACKUP_PHASE_DATA;

		remain = st->total - st->cur;
		if (!remain) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		to_read = (size_t)min_t(u64, remain, st->buf_size);

		if (st->skip_bad) {
			nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
			size_t len;

			while (!(st->phys % nand->erasesize) && st->phys < st->smem.size &&
			       nand_block_isbad(nand, st->smem.start + st->phys))
				st->phys += nand->erasesize;
			if (st->phys >= st->smem.size)
				goto io_err;
			to_read = min_t(size_t, to_read,
					nand->erasesize - st->phys % nand->erasesize);
			len = to_read;
			/* Keep going past uncorrectable pages: a backup of a damaged
			 * partition is still worth having, and restore re-validates it. */
			ret = nand_read(nand, st->smem.start + st->phys, &len, st->buf);
			if ((ret && ret != -EUCLEAN && ret != -EBADMSG) || len != to_read)
				goto io_err;
			st->phys += to_read;
			got = to_read;
		} else if (st->src == BACKUP_SRC_SMEM) {
			size_t readsz = 0;

			ret = smem_read_data(&st->smem,
					st->smem.start + st->start + st->cur,
					to_read, &readsz, st->buf, st->buf_size);
			if (ret)
				goto io_err;

			got = readsz;
		} else {
			ret = mmc_read_data(st->mmc,
					st->mmc_base + st->start + st->cur,
					to_read, st->buf, st->buf_size);
			if (ret == -ENOMEM)
				goto oom;
			else if (ret)
				goto io_err;

			got = to_read;
		}

		if (!got)
			goto io_err;

		st->cur += got;

		response->status = HTTP_RESP_CUSTOM;
		response->data = (const char *)st->buf;
		response->size = got;
		return;

	io_err:
		response->status = HTTP_RESP_NONE;
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		st = response->session_data;
		if (st) {
			free(st->buf);
			free(st);
		}
	}

	return;

bad:
	response->status = HTTP_RESP_STD;
	response->data = "bad request";
	response->size = strlen(response->data);
	response->info.code = 400;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	return;

bad_target:
	response->status = HTTP_RESP_STD;
	response->data = "target not found";
	response->size = strlen(response->data);
	response->info.code = 404;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	free(st->buf);
	free(st);
	return;

bad_range:
	response->status = HTTP_RESP_STD;
	response->data = "invalid range";
	response->size = strlen(response->data);
	response->info.code = 400;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	free(st->buf);
	free(st);
	return;

oom:
	response->status = HTTP_RESP_STD;
	response->data = "no mem";
	response->size = strlen(response->data);
	response->info.code = 500;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	return;
}

void backup_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/backup", &backup_handler, NULL);
}
