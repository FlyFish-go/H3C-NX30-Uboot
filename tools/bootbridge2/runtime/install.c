// SPDX-License-Identifier: GPL-2.0-or-later
#include "install.h"
#include "sha256.h"

#include <stdlib.h>
#include <string.h>

static int sha_matches(const uint8_t *buf, size_t len, const char *hex)
{
	struct bb_sha256 s;
	uint8_t d[32];
	char h[65];

	bb_sha256_init(&s);
	bb_sha256_update(&s, buf, len);
	bb_sha256_final(&s, d);
	bb_sha256_hex(d, h);
	return !strcmp(h, hex);
}

static int holds_image(const struct bb_flash *f, const uint8_t *image, uint32_t len)
{
	uint8_t *buf = malloc(f->erase_size);
	int same = buf != NULL;

	for (uint32_t off = 0; same && off < len; off += f->erase_size)
		same = !bb_flash_read(f, off, buf, f->erase_size) &&
		       !memcmp(buf, image + off, f->erase_size);
	free(buf);
	return same;
}

/* Which APPSBL copy the boot chain uses: selector 0 is the lower one. */
static int active_copy(const struct bb_inst_inputs *in, const struct bb_ret_plan *rp)
{
	int sel = -1;
	int lower;

	for (int c = 0; c < 2; c++) {
		if (!rp->valid[c])
			continue;
		/* Do not guess which copy SBL selected when the records conflict. */
		if (!rp->bc[c].have_appsbl || rp->bc[c].appsbl_selector > 1 ||
		    (sel >= 0 && sel != (int)rp->bc[c].appsbl_selector))
			return -1;
		sel = rp->bc[c].appsbl_selector;
	}
	if (sel < 0)
		return -1;
	lower = in->appsbl[0]->offset < in->appsbl[1]->offset ? 0 : 1;
	return sel == 0 ? lower : !lower;
}

void bb_inst_plan(const struct bb_inst_inputs *in, const struct bb_candidate *c,
		  const uint8_t *payload, size_t len, struct bb_inst_plan *p)
{
	const struct bb_flash *a = in->appsbl[0], *b = in->appsbl[1];
	int active;

	memset(p, 0, sizeof(*p));
	p->code = BB_INST_PAYLOAD;
	if (len != c->bytes || !sha_matches(payload, len, c->sha256) || c->part_bytes < len)
		return;

	p->code = BB_INST_PARTITIONS;
	if (!a || !b || a->size != b->size || a->size != c->part_bytes ||
	    !a->erase_size || !a->page_size || a->erase_size % a->page_size ||
	    a->page_size != b->page_size || a->erase_size != b->erase_size ||
	    a->size % a->erase_size || a->offset == b->offset)
		return;

	p->code = BB_INST_NO_MEM;
	p->image = malloc(c->part_bytes);
	if (!p->image)
		return;
	p->image_len = c->part_bytes;
	memcpy(p->image, payload, len);
	memset(p->image + len, 0xff, c->part_bytes - len);
	p->code = BB_INST_PAYLOAD;
	if (!sha_matches(p->image, p->image_len, c->part_sha256))
		return;

	p->code = BB_INST_BAD_BLOCK;
	for (int i = 0; i < 2; i++)
		for (uint32_t off = 0; off < in->appsbl[i]->size; off += in->appsbl[i]->erase_size)
			if (in->appsbl[i]->is_bad(in->appsbl[i]->ctx, off))
				return;

	/* Installing is only allowed when going back to OEM afterwards works. */
	bb_ret_plan(&in->ret, &p->ret);
	p->ret_code = p->ret.code;
	p->code = BB_INST_RETURN;
	if (p->ret.code != BB_RET_READY && p->ret.code != BB_RET_ALREADY)
		return;

	active = active_copy(in, &p->ret);
	p->code = BB_INST_BOOTCONFIG;
	if (active < 0)
		return;
	p->first = !active;
	p->second = active;
	for (int i = 0; i < 2; i++)
		p->already[i] = holds_image(in->appsbl[i], p->image, p->image_len);
	p->code = BB_INST_READY;
}

static int write_copy(const struct bb_flash *f, const uint8_t *image, uint32_t len)
{
	for (uint32_t off = 0; off < len; off += f->erase_size)
		if (bb_flash_rewrite_block(f, off, image + off))
			return -1;
	return 0;
}

enum bb_inst_code bb_inst_commit(const struct bb_inst_inputs *in, struct bb_inst_plan *p)
{
	if (p->code != BB_INST_READY)
		return p->code;
	if (!p->already[p->first] &&
	    write_copy(in->appsbl[p->first], p->image, p->image_len))
		return p->code = BB_INST_WRITE_FIRST;
	if (!p->already[p->second] &&
	    write_copy(in->appsbl[p->second], p->image, p->image_len))
		return p->code = BB_INST_WRITE_SECOND;
	if (p->ret.code == BB_RET_READY) {
		p->ret_code = bb_ret_commit(&in->ret, &p->ret);
		if (p->ret_code != BB_RET_DONE)
			return p->code = BB_INST_RETURN_FAILED;
	}
	return p->code = BB_INST_DONE;
}

void bb_inst_plan_free(struct bb_inst_plan *p)
{
	free(p->image);
	p->image = NULL;
	bb_ret_plan_free(&p->ret);
}

const char *bb_inst_code_str(enum bb_inst_code c)
{
	switch (c) {
	case BB_INST_READY: return "ready";
	case BB_INST_DONE: return "done";
	case BB_INST_PAYLOAD: return "payload_mismatch";
	case BB_INST_PARTITIONS: return "bad_partitions";
	case BB_INST_BAD_BLOCK: return "bad_block";
	case BB_INST_RETURN: return "oem_return_unavailable";
	case BB_INST_WRITE_FIRST: return "write_first_failed";
	case BB_INST_WRITE_SECOND: return "write_second_failed";
	case BB_INST_RETURN_FAILED: return "return_failed";
	case BB_INST_NO_MEM: return "no_memory";
	case BB_INST_BOOTCONFIG: return "appsbl_selector_ambiguous";
	}
	return "unknown";
}

const char *bb_inst_code_text(enum bb_inst_code c)
{
	switch (c) {
	case BB_INST_READY: return "可以安装";
	case BB_INST_DONE: return "U-Boot 已写入两份并校验，启动配置已指向原厂系统，可以重启";
	case BB_INST_PAYLOAD: return "U-Boot 文件与规则不符，已拒绝";
	case BB_INST_PARTITIONS: return "APPSBL 分区与候选不匹配";
	case BB_INST_BAD_BLOCK: return "APPSBL 分区有坏块，已拒绝";
	case BB_INST_RETURN: return "无法确认安装后能回到原厂系统，已拒绝安装";
	case BB_INST_WRITE_FIRST: return "备用 U-Boot 写入失败；正在使用的 U-Boot 没有改动，可以重试或返回原厂";
	case BB_INST_WRITE_SECOND: return "第二份 U-Boot 写入失败；第一份已写好。请不要断电，重试安装";
	case BB_INST_RETURN_FAILED: return "U-Boot 已装好，但启动配置切换失败；请重试返回原厂";
	case BB_INST_NO_MEM: return "内存不足";
	case BB_INST_BOOTCONFIG: return "无法确定当前启动的 U-Boot 副本，已拒绝安装；仍可返回原厂系统";
	}
	return "未知错误";
}
