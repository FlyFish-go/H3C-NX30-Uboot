// SPDX-License-Identifier: GPL-2.0-or-later
#include "facts.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV_BYTES 0x40000u	/* U-Boot CRC covers this window, not the whole MTD */

int bb_facts_parse_dt_model(struct bb_facts *f, const char *model)
{
	const char *ap = strstr(model, "/AP-");
	size_t n = 0;

	snprintf(f->dt_model, sizeof(f->dt_model), "%s", model);
	if (!ap)
		return -1;
	for (ap += 4; ap[n] && !isspace((unsigned char)ap[n]) && n + 1 < sizeof(f->route); n++)
		f->route[n] = tolower((unsigned char)ap[n]);
	f->route[n] = 0;
	f->have_route = n > 0;
	return f->have_route ? 0 : -1;
}

/* CDT: "CDT\0", version, ..., then a table of (u16 offset, u16 size) blocks
 * starting at 0x0e. The first block is the platform id:
 *   [format, platform, version, subtype hi, subtype lo]
 * machid = platform << 24 | version << 16 | subtype (16 bits).
 * e.g. 02 08 04 00 00 -> 0x08040000 (mp02.1). */
int bb_facts_parse_cdt(struct bb_facts *f, const uint8_t *cdt, size_t len)
{
	unsigned off, size;

	if (len < 0x12 || memcmp(cdt, "CDT", 4))
		return -1;
	off = cdt[0x0e] | cdt[0x0f] << 8;
	size = cdt[0x10] | cdt[0x11] << 8;
	if (size != 5 || off + size > len || cdt[off] != 2)
		return -1;
	f->machid = (uint32_t)cdt[off + 1] << 24 | cdt[off + 2] << 16 | cdt[off + 3] << 8 |
		    cdt[off + 4];
	f->have_machid = 1;
	return 0;
}

static void parse_kmsg_line(struct bb_facts *f, const char *line)
{
	const char *p = strstr(line, "nand: ");
	unsigned m, c, mib, erase, page, oob;
	char cell[16];

	if (!p)
		return;
	if (sscanf(p, "nand: device found, Manufacturer ID: 0x%x, Chip ID: 0x%x", &m, &c) == 2) {
		f->nand_mfr = m & 0xff;
		f->nand_chip = c & 0xff;
		f->have_nand_id = 1;
	} else if (sscanf(p, "nand: %u MiB, %15[^,], erase size: %u KiB, page size: %u, OOB size: %u",
			  &mib, cell, &erase, &page, &oob) == 5) {
		f->nand_mib = mib;
		f->nand_erase_kib = erase;
		f->nand_page = page;
		f->nand_oob = oob;
		f->have_nand_geometry = 1;
	} else if (strstr(p, " SPI NAND") &&
		   sscanf(p, "nand: %31s %63s", f->nand_vendor, f->nand_model) == 2) {
		f->have_nand_model = 1;
	}
}

int bb_facts_parse_kmsg(struct bb_facts *f, const char *log)
{
	char line[256];

	while (*log) {
		size_t n = strcspn(log, "\n");

		if (n >= sizeof(line))
			n = sizeof(line) - 1;
		memcpy(line, log, n);
		line[n] = 0;
		parse_kmsg_line(f, line);
		log += strcspn(log, "\n");
		if (*log)
			log++;
	}
	return f->have_nand_id && f->have_nand_model && f->have_nand_geometry ? 0 : -1;
}

static uint32_t crc32_std(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffffu;

	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & -(c & 1));
	}
	return ~c;
}

int bb_facts_parse_env(struct bb_facts *f, const uint8_t *env, size_t len)
{
	size_t pos = 4;

	if (len < ENV_BYTES)
		return -1;
	f->env_crc_ok = crc32_std(env + 4, ENV_BYTES - 4) ==
			(uint32_t)(env[0] | env[1] << 8 | env[2] << 16 | (uint32_t)env[3] << 24);
	if (!f->env_crc_ok)
		return -1;
	while (pos < ENV_BYTES && env[pos]) {
		const char *kv = (const char *)env + pos;
		size_t n = strnlen(kv, ENV_BYTES - pos);

		if (n >= 12 && !strncmp(kv, "productname=", 12))
			snprintf(f->productname, sizeof(f->productname), "%.*s", (int)(n - 12), kv + 12);
		else if (n >= 19 && !strncmp(kv, "manucode=", 9))
			snprintf(f->item_code, sizeof(f->item_code), "%.10s", kv + 9);
		pos += n + 1;
	}
	return 0;
}

int bb_facts_parse_art(struct bb_facts *f, const uint8_t *art, size_t len)
{
	if (len < BB_ART5G_OFFSET + BB_ART5G_BYTES)
		return -1;
	memcpy(f->art5g, art + BB_ART5G_OFFSET, BB_ART5G_BYTES);
	f->have_art5g = 1;
	return 0;
}

struct out {
	char *p;
	size_t len, used;
	int err;
};

static void put(struct out *o, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (o->err)
		return;
	va_start(ap, fmt);
	n = vsnprintf(o->p + o->used, o->len - o->used, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= o->len - o->used)
		o->err = 1;
	else
		o->used += n;
}

/* JSON string; facts only hold printable ASCII, anything else becomes '?'. */
static void put_str(struct out *o, const char *s)
{
	put(o, "\"");
	for (; *s; s++) {
		unsigned char c = *s;

		if (c == '"' || c == '\\')
			put(o, "\\%c", c);
		else
			put(o, "%c", c >= 0x20 && c < 0x7f ? c : '?');
	}
	put(o, "\"");
}

int bb_facts_json(const struct bb_facts *f, char *buf, size_t len)
{
	struct out o = { buf, len, 0, 0 };

	put(&o, "{\"schema\":\"bb-facts-v1\",\"route\":");
	if (f->have_route)
		put_str(&o, f->route);
	else
		put(&o, "null");
	put(&o, ",\"dt_model\":");
	put_str(&o, f->dt_model);
	if (f->have_machid)
		put(&o, ",\"machid\":\"0x%08x\"", f->machid);
	else
		put(&o, ",\"machid\":null");
	put(&o, ",\"nand\":{\"id\":");
	if (f->have_nand_id)
		put(&o, "\"%02x:%02x\"", f->nand_mfr, f->nand_chip);
	else
		put(&o, "null");
	put(&o, ",\"model\":");
	put_str(&o, f->have_nand_model ? f->nand_model : "");
	put(&o, ",\"mib\":%u,\"erase_kib\":%u,\"page\":%u,\"oob\":%u,\"mtd_oob\":%u,\"ecc\":%u}",
	    f->nand_mib, f->nand_erase_kib, f->nand_page, f->nand_oob, f->mtd_oob,
	    f->mtd_ecc_strength);
	put(&o, ",\"art5g\":");
	if (f->have_art5g) {
		put(&o, "\"");
		for (unsigned i = 0; i < BB_ART5G_BYTES; i++)
			put(&o, "%02x", f->art5g[i]);
		put(&o, "\"");
	} else {
		put(&o, "null");
	}
	put(&o, ",\"env\":{\"crc_ok\":%s,\"productname\":", f->env_crc_ok ? "true" : "false");
	put_str(&o, f->productname);
	put(&o, ",\"item_code\":");
	put_str(&o, f->item_code);
	put(&o, "},\"appsbl_sha256\":[");
	put_str(&o, f->appsbl_sha256[0]);
	put(&o, ",");
	put_str(&o, f->appsbl_sha256[1]);
	put(&o, "]}");
	return o.err ? -1 : (int)o.used;
}
