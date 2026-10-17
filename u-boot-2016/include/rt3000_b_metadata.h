/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_B_METADATA_H__
#define __RT3000_B_METADATA_H__
#include <linux/types.h>
#include <net/httpd.h>
#include <rt3000_b_metadata_core.h>

/* CDT callers bind a preview and commit to one copy. DEFAULT keeps B's legacy
 * paired-write contract when the older B upgrade form omits cdt_target. */
enum rt3_md_target {
	RT3_MD_TARGET_DEFAULT = 0,
	RT3_MD_TARGET_PRIMARY,
	RT3_MD_TARGET_SECONDARY,
};
int rt3_metadata_validate(enum rt3_md_kind kind, const void *data, u32 size,
			  char token_out[65], char digest_out[65]);
int rt3_metadata_target_parse(const char *data, u32 size,
			     enum rt3_md_target *target);
const char *rt3_metadata_target_name(enum rt3_md_target target);
int rt3_metadata_validate_target(enum rt3_md_kind kind,
				 enum rt3_md_target target,
				 const void *data, u32 size,
				 char token_out[65], char digest_out[65]);
const char *rt3_metadata_validation_error(int error);
const char *rt3_metadata_preview_name(enum rt3_md_kind kind, const void *data, u32 size);
int rt3_metadata_pending_target(enum rt3_md_kind kind, u32 *offset, u32 *size,
				 u32 *second_offset, u32 *second_size);
int rt3_metadata_write(enum rt3_md_kind kind, const char *token,
		       struct httpd_response *response);
int rt3_metadata_write_target(enum rt3_md_kind kind,
			      enum rt3_md_target target, const char *token,
			      struct httpd_response *response);
void rt3_metadata_register_uri_handlers(struct httpd_instance *inst);
void rt3_metadata_invalidate(void);
#endif
