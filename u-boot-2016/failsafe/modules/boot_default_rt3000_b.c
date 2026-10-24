// SPDX-License-Identifier: GPL-2.0+
/* Persistent, exact-scope BOOTCONFIG default selection for RT3000-B. */
#include <common.h>
#include <malloc.h>
#include <asm/errno.h>
#include <linux/mtd/mtd.h>
#include <net/httpd.h>
#include <nand.h>
#include <ipq_api.h>
#include <rt3000_b_layout.h>
#include <rt3000_b_fw_write.h>
#include "boot_default_rt3000_b_core.h"
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
#include <nx30_d_service.h>
#endif

#define BC_SIZE RT3_BOOTCFG_SIZE
#define BC_PAGE 2048U
#define BC_ERASE 0x20000U
#define BC_CHIP 0x08000000U

struct bc_extent { const char *name; u32 off, size; };
static const struct bc_extent bc0 = { "0:BOOTCONFIG", 0, BC_SIZE };
static const struct bc_extent bc1 = { "0:BOOTCONFIG1", 0, BC_SIZE };
static char reply[512];
static int partial_update_lock;
static u8 *last_backup_snapshot;
static int backup_snapshot_ready;
static void answer(struct httpd_response *r, int code, const char *json);

#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
/* Export the separate service transaction's pinned preimages even if full BC
 * equality is lost. These handlers never read slots or acquire a write window. */
static void retained_failure_handler(enum httpd_uri_handler_status status,
 struct httpd_request *request,struct httpd_response *response)
{
 struct nx30_d_result failure;
 u32 copy;
 int ret;
 if (status!=HTTP_CB_NEW) return;
 if (!request || request->method!=HTTP_GET) {
  answer(response,405,"{\"error\":\"GET_required\"}"); return;
 }
 ret=nx30_d_default_failure_get(&failure,&copy);
 if (ret) { answer(response,404,"{\"error\":\"no_retained_failure\"}"); return; }
 snprintf(reply,sizeof(reply),
  "{\"status\":\"recovery_required\",\"backend_errno\":%d,\"stage\":%u,\"copy\":%u,\"bytes_verified\":%u,"
  "\"original0\":\"/boot/default/retained-original0.bin\",\"original1\":\"/boot/default/retained-original1.bin\","
  "\"message\":\"Download both RAM preimages before reboot. New service default transactions are locked.\"}",
  failure.status,failure.stage,copy,failure.bytes_verified);
 answer(response,200,reply);
}
static void retained_original_handler(enum httpd_uri_handler_status status,
 struct httpd_request *request,struct httpd_response *response,unsigned int copy)
{
 const void *bytes;
 size_t length;
 if (status!=HTTP_CB_NEW) return;
 if (!request || request->method!=HTTP_GET) {
  answer(response,405,"{\"error\":\"GET_required\"}"); return;
 }
 if (nx30_d_default_original_get(copy,&bytes,&length)) {
  answer(response,404,"{\"error\":\"no_RAM_backup\"}"); return;
 }
 response->status=HTTP_RESP_STD;response->info.code=200;
 response->info.connection_close=1;response->info.content_type="application/octet-stream";
 response->data=bytes;response->size=length;
}
static void retained_original0_handler(enum httpd_uri_handler_status s,
 struct httpd_request *q,struct httpd_response *r)
{ retained_original_handler(s,q,r,0); }
static void retained_original1_handler(enum httpd_uri_handler_status s,
 struct httpd_request *q,struct httpd_response *r)
{ retained_original_handler(s,q,r,1); }
#endif

static int extent_read(const struct bc_extent *tmpl, struct bc_extent *out)
{
 u32 off = 0, size = 0;
 if (!tmpl || !out || getpart_offset_size((char *)tmpl->name, &off, &size) ||
     size != BC_SIZE || off % BC_ERASE || off > BC_CHIP || size > BC_CHIP - off
#if (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || \
    defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY)
     || (!strcmp(tmpl->name, "0:BOOTCONFIG") && off != 0x100000)
     || (!strcmp(tmpl->name, "0:BOOTCONFIG1") && off != 0x140000)
#endif
     )
  return -EXDEV;
 *out = *tmpl; out->off = off; out->size = size; return 0;
}

static int layout_read_dual(struct rt3_layout *l)
{
 int r = rt3_layout_read(l);
 return r || !l || l->mode != RT3_LAYOUT_DUAL || l->count != 2 ? -EXDEV : 0;
}

/* Find the physical selector by asking the validated current SMEM map. */
static int selector_for_name(const struct rt3_layout *l, const char *name,
                             u32 *selector)
{
 const struct rt3_partition *wanted, *mapped;
 int s;
 if (!l || !name || !selector || rt3_layout_find(l, name, &wanted)) return -EXDEV;
 for (s = 0; s <= 1; s++) {
  if (!rt3_layout_default_target(l, s, &mapped) &&
      mapped->offset == wanted->offset && mapped->size == wanted->size &&
      !strcmp(mapped->name, wanted->name)) { *selector = s; return 0; }
 }
 return -EXDEV;
}

static int current_name(const struct rt3_layout *l, u32 selector, char out[16])
{
 const struct rt3_partition *p;
 if (selector > 1 || rt3_layout_default_target(l, selector, &p) ||
     (strcmp(p->name, "rootfs") && strcmp(p->name, "rootfs_1"))) return -EXDEV;
 strlcpy(out, p->name, 16); return 0;
}

static int bootcmd_compatible(void)
{
#ifdef CONFIG_H3C_HW_C_RAM_READONLY
 return 1;
#else
 const char *s = getenv("bootcmd");
 /* C production uses the current RAM command after legacy ENV normalization. */
 /* A saved slot-specific or otherwise custom command would override BC. */
 return !s || !*s || !strcmp(s, "bootipq");
#endif
}

static int read_exact(struct mtd_info *m, u32 off, void *buf, u32 n)
{
 size_t done = 0;
 int r = mtd_read(m, off, n, &done, buf);
 if (r == -EUCLEAN && done == n) r = 0;
 return r || done != n ? (r ? r : -EIO) : 0;
}

static int read_full(struct mtd_info *m, const struct bc_extent *e, u8 *out, u8 *page)
{
 u32 off; int r = 0;
 if (!page) return -EINVAL;
 for (off = 0; off < e->size; off += BC_PAGE) {
  r = read_exact(m, e->off + off, page, BC_PAGE);
  if (r) break;
  memcpy(out + off, page, BC_PAGE);
 }
 return r;
}

static int read_matches(struct mtd_info *m, const struct bc_extent *e,
                        const u8 *expected, u8 *page)
{
 u32 off; int r;
 for (off = 0; off < e->size; off += BC_PAGE) {
  r = read_exact(m, e->off + off, page, BC_PAGE);
  if (r || memcmp(page, expected + off, BC_PAGE)) return r ? r : -ESTALE;
 }
 return 0;
}

static int partition_ok(struct mtd_info *m, const struct bc_extent *a,
                        const struct bc_extent *b)
{
 if (!m || m->type != MTD_NANDFLASH || m->size != BC_CHIP ||
     m->writesize != BC_PAGE || m->erasesize != BC_ERASE ||
     a->off == b->off || (a->off < b->off + b->size && b->off < a->off + a->size))
  return -EINVAL;
 return 0;
}

static int no_bad_blocks(struct mtd_info *m, const struct bc_extent *e)
{
 u32 off;
 for (off = 0; off < e->size; off += m->erasesize) {
  int r = nand_block_isbad(m, e->off + off);
  if (r) return r > 0 ? -EIO : r;
 }
 return 0;
}

static int extent_still_same(const struct bc_extent *tmpl,
                             const struct bc_extent *expected)
{
 struct bc_extent now;
 return extent_read(tmpl, &now) || now.off != expected->off ||
        now.size != expected->size ? -EXDEV : 0;
}

static int write_one(struct mtd_info *m, const struct bc_extent *tmpl,
                     const struct bc_extent *e, const u8 *expected,
                     const u8 *own_baseline,
                     u32 fingerprint, const struct rt3_layout *staged_layout,
                     const struct bc_extent *other_tmpl,
                     const struct bc_extent *other_expected,
                     const u8 *other_baseline, const u8 *other_expected_data,
                     int other_written, u8 *page, u8 *check, int *mutation_started)
{
 struct erase_info er;
 struct rt3_layout now;
 u32 off; int r, opened = 0;
 if (mutation_started) *mutation_started = 0;
 if (layout_read_dual(&now) || now.fingerprint != fingerprint ||
     now.mode != staged_layout->mode || extent_still_same(tmpl, e) ||
     extent_still_same(other_tmpl, other_expected)) return -EXDEV;
 if (no_bad_blocks(m, e)) return -EIO;
 /* Revalidate the untouched other copy before this partition can be erased. */
 if (!other_written) {
  r = read_matches(m, other_expected, other_baseline, check);
  if (r) return r;
 } else {
  if (!other_expected_data) return -EINVAL;
  r = read_matches(m, other_expected, other_expected_data, check);
  if (r) return r;
 }
 if (!page || !check) return -ENOMEM;
 #if (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT)
 r = rt3_bootconfig_write_window_open(m, tmpl->name, e->off, e->size);
#else
 r = rt3_named_nand_write_window_open(m, tmpl->name, e->off, e->size);
#endif
 if (r) goto out;
 opened = 1;
 /* Close the race between the preflight and the write-window acquisition. */
 {
  struct rt3_layout opened_layout;
  struct bc_extent opened_target, opened_other;
  if (layout_read_dual(&opened_layout) || opened_layout.fingerprint != fingerprint ||
      extent_read(tmpl, &opened_target) || extent_read(other_tmpl, &opened_other) ||
      opened_target.off != e->off || opened_target.size != e->size ||
      opened_other.off != other_expected->off || opened_other.size != other_expected->size) {
   r = -EXDEV; goto out;
  }
 }
 /* Check the full partition being changed again after opening its exclusive window. */
 r = read_matches(m, e, own_baseline, check);
 if (r) goto out;
 if (mutation_started) *mutation_started = 1;
 for (off = 0; off < e->size; off += m->erasesize) {
  memset(&er, 0, sizeof(er)); er.mtd = m; er.addr = e->off + off;
  er.len = m->erasesize; r = mtd_erase(m, &er); if (r) goto out;
 }
 for (off = 0; off < e->size; off += BC_PAGE) {
  size_t done = 0;
  memcpy(page, expected + off, BC_PAGE);
  r = mtd_write(m, e->off + off, BC_PAGE, &done, page);
  if (r || done != BC_PAGE) { r = r ? r : -EIO; goto out; }
 }
 for (off = 0; off < e->size; off += BC_PAGE) {
  r = read_exact(m, e->off + off, check, BC_PAGE);
  if (r || memcmp(check, expected + off, BC_PAGE)) { r = r ? r : -EIO; goto out; }
 }
 r = 0;
 out:
 if (opened) {rt3_fw_write_window_close();
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
 if(rt3_fw_write_window_close_status())r=-EIO;
#endif
 }
 return r;
}

static void default_backup_handler(enum httpd_uri_handler_status status,
                                   struct httpd_request *request,
                                   struct httpd_response *response,
                                   unsigned int which)
{
 if (status != HTTP_CB_NEW) return;
 if (!request || request->method != HTTP_GET) {
  answer(response, 405, "{\"status\":\"fail\",\"error\":\"GET_required\"}"); return;
 }
 if (!backup_snapshot_ready || !last_backup_snapshot) {
  answer(response, 404, "{\"status\":\"fail\",\"error\":\"no_RAM_backup\"}"); return;
 }
 response->status = HTTP_RESP_STD; response->info.code = 200;
 response->info.connection_close = 1; response->info.content_type = "application/octet-stream";
 response->data = (const char *)(last_backup_snapshot + which * BC_SIZE);
 response->size = BC_SIZE;
}
static void backup_primary_handler(enum httpd_uri_handler_status s,
                                  struct httpd_request *q, struct httpd_response *r)
{ default_backup_handler(s, q, r, 0); }
static void backup_copy1_handler(enum httpd_uri_handler_status s,
                                 struct httpd_request *q, struct httpd_response *r)
{ default_backup_handler(s, q, r, 1); }

static void answer(struct httpd_response *r, int code, const char *json)
{
 r->status = HTTP_RESP_STD; r->info.code = code;
 r->info.connection_close = 1; r->info.content_type = "application/json";
 r->data = json; r->size = strlen(json);
}

static void default_handler(enum httpd_uri_handler_status status,
                            struct httpd_request *request,
                            struct httpd_response *response)
{
 struct rt3_layout layout;
 struct bc_extent p, b;
 struct mtd_info *m = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
 u8 *storage = NULL, *base0, *base1, *stage0, *stage1, *page = NULL, *check = NULL;
 struct rt3_bootcfg_view view;
 char active[16] = "", target[16] = "";
 u32 selector = 0;
 int r, post, duplicate = 0;
 unsigned int i;
 struct httpd_form_value *v = NULL;
 if (status != HTTP_CB_NEW) return;
 if (!request || (request->method != HTTP_GET && request->method != HTTP_POST)) {
  answer(response, 405, "{\"status\":\"fail\",\"error\":\"method_not_allowed\"}"); return;
 }
 post = request->method == HTTP_POST;
#if defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_A_RAM_READONLY)
 if (post) {
  answer(response, 403, "{\"status\":\"fail\",\"error\":\"readonly_profile\"}");
  return;
 }
#endif
 if (post) {
  for (i = 0; i < request->form.count; i++) {
   if (request->form.values[i].name && !strcmp(request->form.values[i].name, "target")) { v = &request->form.values[i]; duplicate++; }
  }
  if (request->form.count != 1 || duplicate != 1 || !v || !v->data ||
      memchr(v->data, 0, v->size) ||
      !((v->size == 6 && !memcmp(v->data, "rootfs", 6)) ||
        (v->size == 8 && !memcmp(v->data, "rootfs_1", 8)))) {
   answer(response, 400, "{\"status\":\"fail\",\"error\":\"invalid_target\"}"); return;
  }
  memcpy(target, v->data, v->size); target[v->size] = 0;
 }
 if (partial_update_lock && post) {
  answer(response, 409, "{\"status\":\"partial\",\"error\":\"write_lock_after_partial_update\"}"); return;
 }
 #ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
 if (post) {
  struct nx30_d_result failure; u32 copy;
  if (!nx30_d_default_failure_get(&failure,&copy)) {
   answer(response,409,"{\"status\":\"partial\",\"error\":\"retained_service_failure\",\"diagnosis\":\"/boot/default/retained-failure\"}"); return;
  }
 }
#endif
 if ((r = layout_read_dual(&layout)) || (r = extent_read(&bc0, &p)) ||
     (r = extent_read(&bc1, &b)) || (r = partition_ok(m, &p, &b))) {
  answer(response, 409, "{\"status\":\"fail\",\"error\":\"unsupported_layout_or_partitions\"}"); return;
 }
 storage = malloc(4U * BC_SIZE);
 page = memalign(ARCH_DMA_MINALIGN, BC_PAGE);
 check = memalign(ARCH_DMA_MINALIGN, BC_PAGE);
 if (!storage || !page || !check) { free(check); free(page); free(storage); answer(response, 500, "{\"status\":\"fail\",\"error\":\"allocation_failed\"}"); return; }
 base0 = storage; base1 = base0 + BC_SIZE; stage0 = base1 + BC_SIZE; stage1 = stage0 + BC_SIZE;
 /* The first two buffers are the complete in-RAM backups of both copies. */
 if ((r = read_full(m, &p, base0, page)) || (r = read_full(m, &b, base1, page)) ||
     rt3_bootcfg_validate_pair(base0, base1, BC_SIZE, &view) ||
     current_name(&layout, view.rootfs_selector, active)) {
  free(check); free(page); free(storage); answer(response, 409, "{\"status\":\"fail\",\"error\":\"invalid_or_ambiguous_bootconfig\"}"); return;
 }
 if (!post) {
#if defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_A_RAM_READONLY)
  snprintf(reply, sizeof(reply), "{\"status\":\"ok\",\"default\":\"%s\",\"can_change\":false,\"locked\":%s,\"reason\":\"readonly_profile\",\"auto_web\":true,\"default_applies_to\":\"bootipq\"}",
           active, partial_update_lock ? "true" : "false");
#elif defined(CONFIG_H3C_DEFAULT_WEB)
  snprintf(reply, sizeof(reply), "{\"status\":\"ok\",\"default\":\"%s\",\"can_change\":%s,\"locked\":%s,\"reason\":\"%s\",\"auto_web\":true,\"default_applies_to\":\"bootipq\"}",
           active, bootcmd_compatible() && !partial_update_lock ? "true" : "false",
           partial_update_lock ? "true" : "false",
           partial_update_lock ? "partial_update" : (!bootcmd_compatible() ? "custom_bootcmd" : ""));
#elif (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT)
  snprintf(reply, sizeof(reply), "{\"status\":\"ok\",\"default\":\"%s\",\"can_change\":%s,\"locked\":%s,\"reason\":\"%s\",\"auto_web\":false,\"default_applies_to\":\"bootipq\"}",
           active, bootcmd_compatible() && !partial_update_lock ? "true" : "false",
           partial_update_lock ? "true" : "false",
           partial_update_lock ? "partial_update" : (!bootcmd_compatible() ? "custom_bootcmd" : ""));
#else
  snprintf(reply, sizeof(reply), "{\"status\":\"ok\",\"default\":\"%s\",\"can_change\":%s,\"locked\":%s,\"reason\":\"%s\"}",
           active, bootcmd_compatible() && !partial_update_lock ? "true" : "false",
           partial_update_lock ? "true" : "false",
           partial_update_lock ? "partial_update" : (!bootcmd_compatible() ? "custom_bootcmd" : ""));
#endif
  free(check); free(page); free(storage); answer(response, 200, reply); return;
 }
 if (!bootcmd_compatible()) {
  free(check); free(page); free(storage); answer(response, 409, "{\"status\":\"fail\",\"error\":\"custom_bootcmd_may_override_default\"}"); return;
 }
 if (!strcmp(active, target)) {
  snprintf(reply, sizeof(reply), "{\"status\":\"ok\",\"default\":\"%s\",\"changed\":false}", active);
  free(check); free(page); free(storage); answer(response, 200, reply); return;
 }
 if (selector_for_name(&layout, target, &selector) ||
     rt3_bootcfg_stage(base0, BC_SIZE, selector, stage0) ||
     rt3_bootcfg_stage(base1, BC_SIZE, selector, stage1) ||
     no_bad_blocks(m, &p) || no_bad_blocks(m, &b)) {
  free(check); free(page); free(storage); answer(response, 409, "{\"status\":\"fail\",\"error\":\"preflight_failed_no_write\"}"); return;
 }
 /* Recheck both full baselines and the complete layout immediately before erase. */
 {
  struct rt3_layout now; struct bc_extent pnow, bnow;
  u32 off;
  if (layout_read_dual(&now) || now.fingerprint != layout.fingerprint ||
      extent_read(&bc0, &pnow) || extent_read(&bc1, &bnow) ||
      pnow.off != p.off || bnow.off != b.off) {
   free(check); free(page); free(storage); answer(response, 409, "{\"status\":\"fail\",\"error\":\"baseline_or_layout_changed_no_write\"}"); return;
  }
  for (off = 0; off < BC_SIZE; off += BC_PAGE) {
   r = read_exact(m, p.off + off, check, BC_PAGE);
   if (r || memcmp(check, base0 + off, BC_PAGE)) break;
  }
  if (!r && off == BC_SIZE) for (off = 0; off < BC_SIZE; off += BC_PAGE) {
   r = read_exact(m, b.off + off, check, BC_PAGE);
   if (r || memcmp(check, base1 + off, BC_PAGE)) break;
  }
  if (r || off != BC_SIZE) { free(check); free(page); free(storage); answer(response, 409, "{\"status\":\"fail\",\"error\":\"baseline_changed_no_write\"}"); return; }
 }
 /* Keep complete original copies in RAM for download/recovery after the write. */
 free(last_backup_snapshot);
 last_backup_snapshot = storage;
 backup_snapshot_ready = 1;
 storage = NULL;
 /* Backup BOOTCONFIG1 first, then primary BOOTCONFIG. Never auto-retry/reset. */
 {
  int started = 0;
  r = write_one(m, &bc1, &b, stage1, base1, layout.fingerprint, &layout,
               &bc0, &p, base0, NULL, 0, page, check, &started);
  if (r) { partial_update_lock = started; free(check); free(page); free(storage); answer(response, started ? 500 : 409, started ? "{\"status\":\"partial\",\"copy\":\"BOOTCONFIG1\",\"error\":\"backup_write_or_readback_failed\"}" : "{\"status\":\"fail\",\"error\":\"preflight_failed_no_write\"}"); return; }
 }
 r = write_one(m, &bc0, &p, stage0, base0, layout.fingerprint, &layout,
               &bc1, &b, base1, stage1, 1, page, check, NULL);
 if (r) { partial_update_lock = 1; free(check); free(page); free(storage); answer(response, 500, "{\"status\":\"partial\",\"copy\":\"BOOTCONFIG\",\"error\":\"primary_write_or_readback_failed\"}"); return; }
 if (read_matches(m, &p, stage0, check) || read_matches(m, &b, stage1, check)) {
  partial_update_lock = 1; free(check); free(page); free(storage); answer(response, 500, "{\"status\":\"partial\",\"error\":\"final_full_readback_failed\"}"); return;
 }
 snprintf(reply, sizeof(reply), "{\"status\":\"ok\",\"default\":\"%s\",\"changed\":true}", target);
 free(check); free(page); free(storage); answer(response, 200, reply);
}

void boot_default_rt3000_b_register_uri_handlers(struct httpd_instance *inst)
{
 httpd_register_uri_handler(inst, "/boot/default", default_handler, NULL);
 httpd_register_uri_handler(inst, "/boot/default/backup/bootconfig.bin", backup_primary_handler, NULL);
 httpd_register_uri_handler(inst, "/boot/default/backup/bootconfig1.bin", backup_copy1_handler, NULL);
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
 httpd_register_uri_handler(inst,"/boot/default/retained-failure",retained_failure_handler,NULL);
 httpd_register_uri_handler(inst,"/boot/default/retained-original0.bin",retained_original0_handler,NULL);
 httpd_register_uri_handler(inst,"/boot/default/retained-original1.bin",retained_original1_handler,NULL);
#endif
}
