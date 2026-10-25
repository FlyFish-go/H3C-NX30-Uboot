// SPDX-License-Identifier: GPL-2.0-or-later
/* bbd: the bridge's maintenance service on 192.168.10.1:80.
 *
 *   GET  /, /main.js, /style.css     page (from /www)
 *   GET  /api/state                  facts, decision, OEM return check, progress
 *   GET  /api/diagnostics            the same, as a download
 *   POST /api/backup                 build the recovery ZIP
 *   GET  /backup.zip                 download it
 *   POST /api/install                install the matched U-Boot (backup required)
 *   POST /api/return                 switch BOOTCONFIG back to the OEM system
 *   POST /api/reboot
 *
 * POST requests must carry "X-BootBridge: 1"; browsers do not send custom
 * headers cross-origin without a preflight this server never answers.
 * Single-threaded on purpose: one flash operation at a time. */
#include "backup.h"
#include "flash_mtd.h"
#include "install.h"
#include "pack.h"
#include "operation_lock.h"
#include "sha256.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define BACKUP_PATH "/tmp/bridge/backup.zip"
#define PACK_MAX (8u << 20)

static const char *www = "/www";

/* ---- state ------------------------------------------------------------- */

static struct {
	struct bb_facts facts;
	struct bb_rules rules;
	int have_rules;
	enum bb_pack_status pack_status;
	char pack_where[160];		/* diagnostics: where and what was read */
	uint8_t *pack_buf;
	struct bb_pack pack;
	struct bb_decision decision;
	struct bb_backup_result backup;
	int backup_ready;
	char last_action[16];
	char last_code[40];
	char last_message[256];
} S;

/* Flash handles for one operation; opened fresh each time and closed after. */
struct parts {
	struct bb_flash appsbl[2], bc[2], slot[2];
	int open_mask;
	struct bb_inst_inputs in;
};

static void parts_close(struct parts *p)
{
	struct bb_flash *all[6] = { &p->appsbl[0], &p->appsbl[1], &p->bc[0], &p->bc[1],
				    &p->slot[0], &p->slot[1] };

	for (int i = 0; i < 6; i++)
		if (p->open_mask & (1 << i))
			all[i]->close(all[i]->ctx);
	p->open_mask = 0;
}

static int open_named(const char *name, int writable, struct bb_flash *f, struct parts *p, int bit)
{
	int idx = bb_mtd_find(name, 0);

	if (idx < 0 || bb_mtd_open(idx, writable, f))
		return -1;
	p->open_mask |= 1 << bit;
	return 0;
}

static int running_offset(struct parts *p, uint32_t *out)
{
	char path[64];

	for (int u = 0; u < 8; u++) {
		struct bb_flash f;
		FILE *fp;
		int mtd;

		snprintf(path, sizeof(path), "/sys/class/ubi/ubi%d/mtd_num", u);
		fp = fopen(path, "r");
		if (!fp)
			continue;
		if (fscanf(fp, "%d", &mtd) != 1)
			mtd = -1;
		fclose(fp);
		if (mtd < 0 || bb_mtd_open(mtd, 0, &f))
			continue;
		f.close(f.ctx);
		if (f.offset == p->slot[0].offset || f.offset == p->slot[1].offset) {
			*out = f.offset;
			return 0;
		}
	}
	return -1;
}

/* Returns 0 when every partition is open; `writable` opens BOOTCONFIG and APPSBL rw. */
static int parts_open(struct parts *p, int writable)
{
	memset(p, 0, sizeof(*p));
	if (open_named("0:APPSBL", writable, &p->appsbl[0], p, 0) ||
	    open_named("0:APPSBL_1", writable, &p->appsbl[1], p, 1) ||
	    open_named("0:BOOTCONFIG", writable, &p->bc[0], p, 2) ||
	    open_named("0:BOOTCONFIG1", writable, &p->bc[1], p, 3) ||
	    open_named("rootfs", 0, &p->slot[0], p, 4) ||
	    open_named("rootfs_1", 0, &p->slot[1], p, 5) ||
	    running_offset(p, &p->in.ret.running_offset)) {
		parts_close(p);
		return -1;
	}
	for (int i = 0; i < 2; i++) {
		p->in.appsbl[i] = &p->appsbl[i];
		p->in.ret.bc[i] = &p->bc[i];
		p->in.ret.slot[i] = &p->slot[i];
	}
	return 0;
}

static void set_result(const char *action, const char *code, const char *message)
{
	snprintf(S.last_action, sizeof(S.last_action), "%s", action);
	snprintf(S.last_code, sizeof(S.last_code), "%s", code);
	snprintf(S.last_message, sizeof(S.last_message), "%s", message);
}

/* The pack sits in the ubi_rootfs volume right after the SquashFS image,
 * at the next LEB boundary (see build/build_bridge.py).
 *
 * Reads go one NAND page at a time (see flash_mtd.c): on the OEM 4.4 kernel
 * a multi-page read that starts on the page the NAND layer has cached
 * returns 0xff for the rest of that eraseblock. */
#define READ_CHUNK 2048

static int pread_all(int fd, void *buf, size_t len, off_t off, unsigned leb)
{
	size_t done = 0;

	while (done < len) {
		size_t want = len - done, to_leb = leb - (off + done) % leb;
		ssize_t n;

		if (want > READ_CHUNK)
			want = READ_CHUNK;
		if (want > to_leb)
			want = to_leb;
		n = pread(fd, (uint8_t *)buf + done, want, off + done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		done += n;
	}
	return 0;
}

static int sys_uint_file(const char *path, unsigned *out)
{
	FILE *f = fopen(path, "r");
	int ok;

	if (!f)
		return -1;
	ok = fscanf(f, "%u", out) == 1;
	fclose(f);
	return ok ? 0 : -1;
}

static void load_pack(void)
{
	char path[96], name[64];
	uint8_t sb[64];
	uint64_t used = 0, off = 0;
	uint32_t total = 0, rl, ll;
	unsigned leb = 0;
	int idx = -1, fd;

	S.pack_status = BB_PACK_NONE;
	S.have_rules = 0;
	bb_rules_free(&S.rules);
	memset(&S.pack, 0, sizeof(S.pack));
	free(S.pack_buf);
	S.pack_buf = NULL;
	for (int i = 0; i < 64 && idx < 0; i++) {
		FILE *f;

		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/name", i);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f) && !strcmp(strtok(name, "\n"), "ubi_rootfs"))
			idx = i;
		fclose(f);
	}
	if (idx < 0)
		return;
	/* gluebi: the erase size is the usable LEB size */
	snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/erasesize", idx);
	if (sys_uint_file(path, &leb) || !leb)
		return;
	snprintf(path, sizeof(path), "/dev/mtd%d", idx);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	if (pread_all(fd, sb, sizeof(sb), 0, leb) || memcmp(sb, "hsqs", 4))
		goto out;
	memcpy(&used, sb + 40, 8);
	off = (used + leb - 1) / leb * leb;
	if (pread_all(fd, sb, sizeof(sb), off, leb) || memcmp(sb, "BBPK", 4))
		goto out;
	memcpy(&rl, sb + 8, 4);
	memcpy(&ll, sb + 12, 4);
	S.pack_status = BB_PACK_BAD;
	if (rl >= PACK_MAX || ll >= PACK_MAX || BB_PACK_HEADER + rl + ll > PACK_MAX)
		goto out;
	total = BB_PACK_HEADER + rl + ll;
	S.pack_buf = malloc(total + 1);
	if (!S.pack_buf || pread_all(fd, S.pack_buf, total, off, leb))
		goto out;
	S.pack_status = bb_pack_open(S.pack_buf, total, &S.pack);
out:
	close(fd);
	snprintf(S.pack_where, sizeof(S.pack_where), "mtd%d leb %u used %llu off %llu len %u: %s", idx,
		 leb, (unsigned long long)used, (unsigned long long)off, total,
		 bb_pack_status_str(S.pack_status));
	if (S.pack_status == BB_PACK_OK) {
		char *text = strndup(S.pack.rules, S.pack.rules_len);

		S.have_rules = text && !bb_rules_load(&S.rules, text, NULL);
		if (!S.have_rules)
			S.pack_status = BB_PACK_BAD;
		free(text);
	}
}

/* Load the pack, retrying briefly; a failure is re-checked whenever the
 * state is asked for, so the page reflects a later success. */
static time_t boot_time;
static unsigned pack_attempts;

static void ensure_pack(void)
{
	if (!boot_time)
		boot_time = time(NULL);
	if (pack_attempts && (S.pack_status == BB_PACK_OK || S.pack_status == BB_PACK_NONE))
		return;
	for (;;) {
		char where[sizeof(S.pack_where)];

		load_pack();
		pack_attempts++;
		snprintf(where, sizeof(where), "%.120s, attempt %u at +%lds", S.pack_where, pack_attempts,
			 (long)(time(NULL) - boot_time));
		snprintf(S.pack_where, sizeof(S.pack_where), "%s", where);
		if (S.pack_status == BB_PACK_OK || S.pack_status == BB_PACK_NONE ||
		    time(NULL) - boot_time > 10)
			break;
		sleep(2);
	}
}

static void refresh(void)
{
	bb_facts_collect(&S.facts);
	memset(&S.decision, 0, sizeof(S.decision));
	if (S.have_rules)
		bb_rules_decide(&S.rules, &S.facts, &S.decision);
}

/* ---- HTTP -------------------------------------------------------------- */

struct buf {
	char *p;
	size_t len, cap;
};

static void bprintf(struct buf *b, const char *fmt, ...)
{
	va_list ap;
	int n;

	for (;;) {
		va_start(ap, fmt);
		n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
		va_end(ap);
		if (n < 0)
			return;
		if ((size_t)n < b->cap - b->len) {
			b->len += n;
			return;
		}
		b->cap = b->cap * 2 + n;
		b->p = realloc(b->p, b->cap);
		if (!b->p)
			exit(1);
	}
}

static void send_all(int fd, const void *p, size_t n)
{
	const char *c = p;

	while (n) {
		ssize_t w = write(fd, c, n);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return;
		c += w;
		n -= w;
	}
}

static void respond(int fd, int status, const char *type, const char *extra, const void *body,
		    size_t len)
{
	char head[512];
	const char *text = status == 200 ? "OK" : status == 404 ? "Not Found" :
			   status == 403 ? "Forbidden" : status == 405 ? "Method Not Allowed" : "Error";
	int n = snprintf(head, sizeof(head),
			 "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
			 "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
			 "Connection: close\r\n%s\r\n",
			 status, text, type, len, extra ? extra : "");

	send_all(fd, head, n);
	send_all(fd, body, len);
}

static void state_json(struct buf *b)
{
	char facts[8192];
	struct parts p;
	struct bb_ret_plan rp;

	if (S.pack_status != BB_PACK_OK && S.pack_status != BB_PACK_NONE) {
		ensure_pack();
		if (S.pack_status == BB_PACK_OK)
			refresh();
	}
	memset(&rp, 0, sizeof(rp));
	rp.code = BB_RET_BAD_SLOTS;
	if (!parts_open(&p, 0)) {
		bb_ret_plan(&p.in.ret, &rp);
		parts_close(&p);
	}
	if (bb_facts_json(&S.facts, facts, sizeof(facts)) < 0)
		snprintf(facts, sizeof(facts), "null");
	bprintf(b, "{\"schema\":\"bb-state-v1\",\"facts\":%s,\"pack\":\"%s\",\"pack_where\":\"%s\",\"rule_set\":\"%s\","
		"\"decision\":{\"code\":\"%s\",\"message\":\"%s\",\"board\":\"%s\",\"nand\":\"%s\","
		"\"candidate\":\"%s\",\"detail\":\"%s\"},"
		"\"oem_return\":{\"code\":\"%s\",\"message\":\"%s\",\"oem_offset\":%u,\"oem_slot\":\"%s\"},"
		"\"backup\":{\"ready\":%s,\"bytes\":%llu,\"sha256\":\"%s\",\"bad_blocks\":%u,"
		"\"unreadable_blocks\":%u},"
		"\"last\":{\"action\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}}",
		facts, bb_pack_status_str(S.pack_status), S.pack_where, S.have_rules ? S.rules.set : "",
		S.have_rules ? bb_decide_str(S.decision.code) : "no_rules",
		S.have_rules ? bb_decide_text(S.decision.code) : "桥接包里没有可用的规则包",
		S.decision.board, S.decision.nand, S.decision.cand ? S.decision.cand->id : "",
		S.decision.detail, bb_ret_code_str(rp.code), bb_ret_code_text(rp.code), rp.oem_offset,
		bb_ubi_status_str(rp.ubi_status), S.backup_ready ? "true" : "false",
		(unsigned long long)S.backup.bytes, S.backup.sha256, S.backup.bad_blocks,
		S.backup.unreadable_blocks, S.last_action, S.last_code, S.last_message);
	bb_ret_plan_free(&rp);
}

static int backup_valid(void)
{
	FILE *f = fopen(BACKUP_PATH, "rb");
	int ok = f && !bb_backup_verify(f, &S.backup);

	if (f)
		fclose(f);
	return ok;
}

static void do_backup(void)
{
	static const char *const startup[] = {
		"0:SBL1", "0:MIBIB", "0:BOOTCONFIG", "0:BOOTCONFIG1", "0:QSEE", "0:QSEE_1",
		"0:DEVCFG", "0:DEVCFG_1", "0:CDT", "0:CDT_1", "0:APPSBLENV", "0:APPSBL",
		"0:APPSBL_1", "0:ART", "0:TRAINING",
	};
	static const char *const persistent[] = { "pdt_data", "pdt_data_1", "exp", "plugin" };
	struct bb_flash parts[BB_BACKUP_MAX];
	struct bb_backup_entry e[BB_BACKUP_MAX];
	const size_t nstart = sizeof(startup) / sizeof(startup[0]);
	const size_t npers = sizeof(persistent) / sizeof(persistent[0]);
	unsigned n = 0;
	FILE *out;
	int ret;

	S.backup_ready = 0;
	for (size_t i = 0; i < nstart + npers; i++) {
		int is_startup = i < nstart;
		const char *name = is_startup ? startup[i] : persistent[i - nstart];
		int idx = bb_mtd_find(name, 0);

		if (idx < 0 || bb_mtd_open(idx, 0, &parts[n])) {
			if (is_startup) {
				set_result("backup", "missing_partition", "缺少启动区分区，无法备份");
				goto close;
			}
			continue;	/* not every model has all persistent partitions */
		}
		e[n].part = &parts[n];
		e[n].role = is_startup ? "startup" : "oem-persistent-data";
		if (is_startup)
			snprintf(e[n].file, sizeof(e[n].file), "startup_%06x.bin", parts[n].offset);
		else
			snprintf(e[n].file, sizeof(e[n].file), "%s.bin", name);
		n++;
	}
	mkdir("/tmp/bridge", 0700);
	out = fopen(BACKUP_PATH, "wb");
	if (!out) {
		set_result("backup", "io", "无法创建备份文件");
		goto close;
	}
	ret = bb_backup_write(out, e, n, &S.backup);
	if (fclose(out) || ret) {
		unlink(BACKUP_PATH);
		set_result("backup", "io", "备份失败（读取或空间不足）");
		goto close;
	}
	if (!backup_valid()) {
		unlink(BACKUP_PATH);
		set_result("backup", "verify", "备份文件读回校验失败，请重新生成备份");
		goto close;
	}
	S.backup_ready = 1;
	set_result("backup", "ok", S.backup.unreadable_blocks ?
		   "备份完成，但有读不出的块已用 0xFF 代替，详见 manifest.json" : "备份完成");
close:
	for (unsigned i = 0; i < n; i++)
		parts[i].close(parts[i].ctx);
}

static void do_return(void)
{
	struct parts p;
	struct bb_ret_plan rp;

	if (parts_open(&p, 1)) {
		set_result("return", "bad_slots", bb_ret_code_text(BB_RET_BAD_SLOTS));
		return;
	}
	bb_ret_plan(&p.in.ret, &rp);
	if (rp.code == BB_RET_READY)
		bb_ret_commit(&p.in.ret, &rp);
	set_result("return", bb_ret_code_str(rp.code), bb_ret_code_text(rp.code));
	bb_ret_plan_free(&rp);
	parts_close(&p);
}

static void do_install(void)
{
	struct bb_inst_plan ip;
	struct parts p;
	uint8_t *mbn = NULL;
	size_t len = 0;

	if (!S.backup_ready || !backup_valid()) {
		S.backup_ready = 0;
		set_result("install", "no_backup", "请先生成并校验完整备份");
		return;
	}
	refresh();	/* decide again on what the flash says now */
	if (!S.have_rules || S.decision.code != BB_DEC_OK) {
		set_result("install", "not_matched", S.have_rules ? bb_decide_text(S.decision.code) :
			   "桥接包里没有可用的规则包");
		return;
	}
	if (bb_pack_extract(&S.pack, S.decision.cand->file, &mbn, &len) != BB_PACK_OK) {
		set_result("install", "pack", "规则包中取不出对应的 U-Boot");
		return;
	}
	if (parts_open(&p, 1)) {
		free(mbn);
		set_result("install", "partitions", bb_inst_code_text(BB_INST_PARTITIONS));
		return;
	}
	bb_inst_plan(&p.in, S.decision.cand, mbn, len, &ip);
	if (ip.code == BB_INST_READY)
		bb_inst_commit(&p.in, &ip);
	set_result("install", bb_inst_code_str(ip.code), bb_inst_code_text(ip.code));
	bb_inst_plan_free(&ip);
	parts_close(&p);
	free(mbn);
	refresh();
}

static void serve_file(int fd, const char *name, const char *type, const char *extra)
{
	char path[256];
	struct stat st;
	char *data;
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", www, name);
	f = fopen(path, "rb");
	if (!f || fstat(fileno(f), &st) || !S_ISREG(st.st_mode)) {
		if (f)
			fclose(f);
		respond(fd, 404, "text/plain", NULL, "not found\n", 10);
		return;
	}
	data = malloc(st.st_size + 1);
	if (data && fread(data, 1, st.st_size, f) == (size_t)st.st_size)
		respond(fd, 200, type, extra, data, st.st_size);
	free(data);
	fclose(f);
}

static void serve_backup(int fd)
{
	char head[256], chunk[65536];
	FILE *f = S.backup_ready ? fopen(BACKUP_PATH, "rb") : NULL;
	size_t n;

	if (!f) {
		respond(fd, 404, "text/plain", NULL, "no backup\n", 10);
		return;
	}
	n = snprintf(head, sizeof(head),
		     "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: %llu\r\n"
		     "Content-Disposition: attachment; filename=\"bootbridge-recovery-backup.zip\"\r\n"
		     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
		     (unsigned long long)S.backup.bytes);
	send_all(fd, head, n);
	while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
		send_all(fd, chunk, n);
	fclose(f);
}

static void handle(int fd)
{
	char req[8192], method[8], path[128];
	size_t got = 0;
	struct buf b = { malloc(16384), 0, 16384 };
	int post, trusted;

	while (got < sizeof(req) - 1) {
		ssize_t r = read(fd, req + got, sizeof(req) - 1 - got);

		if (r <= 0)
			break;
		got += r;
		req[got] = 0;
		if (strstr(req, "\r\n\r\n"))
			break;
	}
	req[got] = 0;
	if (!b.p || sscanf(req, "%7s %127s", method, path) != 2) {
		free(b.p);
		return;
	}
	post = !strcmp(method, "POST");
	trusted = strcasestr(req, "\r\nX-BootBridge: 1\r\n") != NULL;
	if (post && !trusted) {
		respond(fd, 403, "text/plain", NULL, "forbidden\n", 10);
	} else if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
		serve_file(fd, "index.html", "text/html; charset=utf-8", NULL);
	} else if (!strcmp(path, "/main.js")) {
		serve_file(fd, "main.js", "application/javascript; charset=utf-8", NULL);
	} else if (!strcmp(path, "/style.css")) {
		serve_file(fd, "style.css", "text/css; charset=utf-8", NULL);
	} else if (!strcmp(path, "/backup.zip") && !post) {
		serve_backup(fd);
	} else if (!strncmp(path, "/api/", 5)) {
		const char *api = path + 5, *extra = NULL;

		if (post && (!strcmp(api, "backup") || !strcmp(api, "install") || !strcmp(api, "return"))) {
			int lock = bb_operation_lock();

			if (lock < 0) {
				set_result(api, "busy", "另一项操作正在进行，或无法获取操作锁，请稍后重试");
				state_json(&b);
				respond(fd, 409, "application/json; charset=utf-8", NULL, b.p, b.len);
				free(b.p);
				return;
			}
			if (!strcmp(api, "backup"))
				do_backup();
			else if (!strcmp(api, "install"))
				do_install();
			else
				do_return();
			close(lock);
		}
		else if (post && !strcmp(api, "reboot")) {
			int lock = bb_operation_lock();

			if (lock < 0) {
				set_result(api, "busy", "另一项操作正在进行，暂时不能重启");
				state_json(&b);
				respond(fd, 409, "application/json; charset=utf-8", NULL, b.p, b.len);
				free(b.p);
				return;
			}
			set_result("reboot", "ok", "正在重启");
			state_json(&b);
			respond(fd, 200, "application/json; charset=utf-8", NULL, b.p, b.len);
			free(b.p);
			sync();
			sleep(1);
			reboot(RB_AUTOBOOT);
			close(lock);
			return;
		}
		else if (!post && !strcmp(api, "diagnostics"))
			extra = "Content-Disposition: attachment; filename=\"bootbridge-diagnostics.json\"\r\n";
		else if (post || strcmp(api, "state")) {
			respond(fd, 404, "text/plain", NULL, "not found\n", 10);
			free(b.p);
			return;
		}
		state_json(&b);
		respond(fd, 200, "application/json; charset=utf-8", extra, b.p, b.len);
	} else {
		respond(fd, 404, "text/plain", NULL, "not found\n", 10);
	}
	free(b.p);
}

int main(int argc, char **argv)
{
	const char *bind_ip = "192.168.10.1";
	int port = 80, opt, s, one = 1;
	struct sockaddr_in a = { .sin_family = AF_INET };

	while ((opt = getopt(argc, argv, "b:p:w:")) != -1) {
		if (opt == 'b')
			bind_ip = optarg;
		else if (opt == 'p')
			port = atoi(optarg);
		else if (opt == 'w')
			www = optarg;
		else
			return 2;
	}
	signal(SIGPIPE, SIG_IGN);
	ensure_pack();
	refresh();

	s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	a.sin_port = htons(port);
	if (s < 0 || inet_pton(AF_INET, bind_ip, &a.sin_addr) != 1 ||
	    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
	    bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 8)) {
		perror("bbd: listen");
		return 1;
	}
	for (;;) {
		int c = accept4(s, NULL, NULL, SOCK_CLOEXEC);
		struct timeval tv = { 10, 0 };

		if (c < 0)
			continue;
		setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		handle(c);
		close(c);
	}
}
