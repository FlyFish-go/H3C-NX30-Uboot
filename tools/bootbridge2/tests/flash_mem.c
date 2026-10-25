// SPDX-License-Identifier: GPL-2.0-or-later
#include "flash_mem.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERASE 0x20000u
#define PAGE 2048u

static int m_read(void *ctx, uint32_t off, void *buf, size_t len)
{
	struct mem_nand *m = ctx;

	for (uint32_t b = off / ERASE; b <= (off + len - 1) / ERASE; b++)
		if (m->unreadable[b])
			return -EBADMSG;
	memcpy(buf, m->data + off, len);
	return 0;
}

static int m_is_bad(void *ctx, uint32_t off)
{
	struct mem_nand *m = ctx;

	return m->bad[off / ERASE];
}

static int m_erase(void *ctx, uint32_t off)
{
	struct mem_nand *m = ctx;

	if (++m->erases == m->fail_erase_at)
		return -EIO;
	memset(m->data + off, 0xff, ERASE);
	memset(m->programmed + off / PAGE, 0, ERASE / PAGE);
	return 0;
}

static int m_write_page(void *ctx, uint32_t off, const void *buf)
{
	struct mem_nand *m = ctx;
	const uint8_t *s = buf;

	if (off % PAGE || m->programmed[off / PAGE])
		return -EIO;	/* real NAND: no second program without erase */
	if (++m->writes == m->fail_write_at) {
		/* A torn program: half the page lands. */
		for (uint32_t i = 0; i < PAGE / 2; i++)
			m->data[off + i] &= s[i];
		m->programmed[off / PAGE] = 1;
		return -EIO;
	}
	for (uint32_t i = 0; i < PAGE; i++)
		m->data[off + i] &= s[i];
	m->programmed[off / PAGE] = 1;
	return 0;
}

static void m_close(void *ctx)
{
	(void)ctx;
}

struct mem_nand *mem_nand_new(const char *name, uint32_t offset, uint32_t size,
			      const void *init)
{
	struct mem_nand *m = calloc(1, sizeof(*m));

	if (!m)
		return NULL;
	m->data = malloc(size);
	m->programmed = calloc(size / PAGE, 1);
	m->bad = calloc(size / ERASE, 1);
	m->unreadable = calloc(size / ERASE, 1);
	if (!m->data || !m->programmed || !m->bad || !m->unreadable) {
		mem_nand_free(m);
		return NULL;
	}
	if (init)
		memcpy(m->data, init, size);
	else
		memset(m->data, 0xff, size);
	/* Pages already holding data count as programmed. */
	for (uint32_t p = 0; p < size / PAGE; p++)
		for (uint32_t i = 0; i < PAGE; i++)
			if (m->data[p * PAGE + i] != 0xff) {
				m->programmed[p] = 1;
				break;
			}
	snprintf(m->f.name, sizeof(m->f.name), "%s", name);
	m->f.ctx = m;
	m->f.offset = offset;
	m->f.size = size;
	m->f.erase_size = ERASE;
	m->f.page_size = PAGE;
	m->f.read = m_read;
	m->f.is_bad = m_is_bad;
	m->f.erase = m_erase;
	m->f.write_page = m_write_page;
	m->f.close = m_close;
	return m;
}

void mem_nand_free(struct mem_nand *m)
{
	if (!m)
		return;
	free(m->data);
	free(m->programmed);
	free(m->bad);
	free(m->unreadable);
	free(m);
}
