/* SPDX-License-Identifier: GPL-2.0 */
#include <common.h>
#include <asm/errno.h>
#include <malloc.h>
#include <net/httpd.h>
#include <spi_flash.h>
#include <bootconfig.h>
#include <flashrw.h>
#include <ipq_api.h>
#include <u-boot/sha256.h>
#include <asm/arch-qca-common/smem.h>
#include "modules.h"
#include "nor_upgrade_core.h"

#define NX30_NOR_RESCUE_JEDEC_8M  0xef6017U /* W25Q64DW */
#define NX30_NOR_RESCUE_JEDEC_16M 0xef6018U /* W25Q128FW */
#define NX30_NOR_TOKEN_SIZE  16

extern qca_smem_flash_info_t qca_smem_flash_info;

static const struct spi_flash *nor_flash;
static const u8 *pending_package;
static u32 pending_package_size;
static char pending_token[NX30_NOR_TOKEN_SIZE + 1];
static bool pending_valid;
struct nor_http_session {
	char header[512];
	char body[1024];
	bool body_sent;
};
static const char *nor_stage = "idle";
static const char nor_token_alphabet[] = "0123456789abcdef";

static void nor_clear_pending(void)
{
	if (pending_package)
		free((void *)pending_package);
	pending_package = NULL;
	pending_package_size = 0;
	pending_token[0] = 0;
	pending_valid = false;
}

static void nor_digest(const void *data, u32 size, u8 digest[32])
{
	sha256_csum_wd(data, size, digest, CHUNKSZ_SHA256);
}

static int nor_read_partition(const char *name, u32 *offset, u32 *size)
{
	char mutable_name[32];

	if (!name || strlen(name) >= sizeof(mutable_name))
		return -EINVAL;
	strlcpy(mutable_name, name, sizeof(mutable_name));
	return getpart_offset_size(mutable_name, offset, size);
}

static int nor_partition_is_nor(const char *name)
{
	char mutable_name[32];

	if (!name || strlen(name) >= sizeof(mutable_name))
		return -EINVAL;
	strlcpy(mutable_name, name, sizeof(mutable_name));
	return get_which_flash_param(mutable_name) == 0 ? 0 : -EXDEV;
}

static int nor_bootconfig_valid(void)
{
	bootconfig_info_t primary, backup;
	int ret, i, found, qsee, devcfg, cdt;

	memset(&primary, 0, sizeof(primary));
	memset(&backup, 0, sizeof(backup));
	primary.part_name = BOOTCONFIG_PART_NAME;
	backup.part_name = BOOTCONFIG_BACKUP_PART_NAME;
	primary.size = sizeof(primary.info);
	backup.size = sizeof(backup.info);
	ret = read_bootconfig(&primary, false, true);
	if (ret)
		return ret;
	ret = read_bootconfig(&backup, false, true);
	if (ret)
		return ret;
	if (memcmp(&primary.info, &backup.info, sizeof(primary.info)) ||
	    primary.info.magic_start != BOOTCONFIG_MAGIC_START ||
	    primary.info.age != 1 || primary.info.numaltpart != 8)
		return -EINVAL;
	found = qsee = devcfg = cdt = 0;
	for (i = 0; i < primary.info.numaltpart; i++) {
		struct bootconfig_part_entry *entry = &primary.info.per_part_entry[i];
		int is_primary = !memcmp(entry->name, "0:APPSBL", 8) &&
				 entry->name[8] == '\0';
		int is_backup = !memcmp(entry->name, "0:APPSBL_1", 10) &&
				entry->name[10] == '\0';
		int is_qsee = !memcmp(entry->name, "0:QSEE", 6) &&
			      entry->name[6] == '\0';
		int is_devcfg = !memcmp(entry->name, "0:DEVCFG", 8) &&
				 entry->name[8] == '\0';
		int is_cdt = !memcmp(entry->name, "0:CDT", 5) &&
			     entry->name[5] == '\0';

		if (is_primary || is_backup || is_qsee || is_devcfg || is_cdt) {
			if (entry->primaryboot != 0)
				return -EINVAL;
			if (is_primary) found++;
			if (is_qsee) qsee++;
			if (is_devcfg) devcfg++;
			if (is_cdt) cdt++;
		}
	}
	return found == 1 && qsee == 1 && devcfg == 1 && cdt == 1 ? 0 : -EINVAL;
}

static int nor_verify_layout(void *ctx, u32 requested_nor_size)
{
	const struct spi_flash *flash = get_detected_spi_flash();
	u32 primary_offset, primary_size, backup_offset, backup_size;
	u32 mibib_offset, mibib_size;
	int ret;

	(void)ctx;
	nor_stage = "layout_and_bootconfig";
	if (qca_smem_flash_info.flash_type != SMEM_BOOT_SPI_FLASH)
		return -ENODEV;
	if (!flash) {
		detect_flash_device();
		flash = get_detected_spi_flash();
	}
	if (!flash ||
	    !((flash->jedec == NX30_NOR_RESCUE_JEDEC_8M &&
	       flash->size == NX30_NOR_FLASH_SIZE_8M) ||
	      (flash->jedec == NX30_NOR_RESCUE_JEDEC_16M &&
	       flash->size == NX30_NOR_FLASH_SIZE_16M)))
		return -ENODEV;
	if (requested_nor_size != flash->size)
		return -EXDEV;
	if (nor_partition_is_nor("0:APPSBL") ||
	    nor_partition_is_nor("0:APPSBL_1") ||
	    nor_partition_is_nor("0:BOOTCONFIG") ||
	    nor_partition_is_nor("0:BOOTCONFIG1") ||
	    nor_partition_is_nor("0:MIBIB") ||
	    nor_read_partition("0:MIBIB", &mibib_offset, &mibib_size) ||
	    !mibib_size || mibib_offset > flash->size ||
	    mibib_size > flash->size - mibib_offset ||
	    nor_read_partition("0:APPSBL", &primary_offset, &primary_size) ||
	    nor_read_partition("0:APPSBL_1", &backup_offset, &backup_size) ||
	    primary_offset != NX30_NOR_PRIMARY_OFFSET ||
	    backup_offset != NX30_NOR_BACKUP_OFFSET ||
	    primary_size != NX30_NOR_SLOT_SIZE || backup_size != NX30_NOR_SLOT_SIZE)
		return -EXDEV;
	{
		u32 bc0_offset, bc0_size, bc1_offset, bc1_size;
		if (nor_read_partition("0:BOOTCONFIG", &bc0_offset, &bc0_size) ||
		    nor_read_partition("0:BOOTCONFIG1", &bc1_offset, &bc1_size) ||
		    bc0_offset != 0x00040000U || bc1_offset != 0x00050000U ||
		    bc0_size != 0x00010000U || bc1_size != 0x00010000U)
			return -EXDEV;
	}
	ret = nor_bootconfig_valid();
	if (ret)
		return ret;
	nor_flash = flash;
	nor_stage = "backup_erase";
	return 0;
}

static int nor_erase(void *ctx, u32 offset, u32 size)
{
	(void)ctx;
	nor_stage = offset == NX30_NOR_BACKUP_OFFSET ? "backup_erase" : "primary_erase";
	if (!nor_flash || offset < NX30_NOR_PRIMARY_OFFSET ||
	    (offset != NX30_NOR_PRIMARY_OFFSET && offset != NX30_NOR_BACKUP_OFFSET) ||
	    size != NX30_NOR_SLOT_SIZE || offset + size > nor_flash->size ||
	    !nor_flash->erase_size || size % nor_flash->erase_size)
		return -EINVAL;
	return spi_flash_erase((struct spi_flash *)nor_flash, offset, size);
}

static int nor_write(void *ctx, u32 offset, const void *data, u32 size)
{
	(void)ctx;
	nor_stage = offset == NX30_NOR_BACKUP_OFFSET ? "backup_program" : "primary_program";
	if (!nor_flash || (offset != NX30_NOR_PRIMARY_OFFSET &&
	    offset != NX30_NOR_BACKUP_OFFSET) || !data || !size ||
	    size > NX30_NOR_SLOT_SIZE || offset + size > nor_flash->size)
		return -EINVAL;
	return spi_flash_write((struct spi_flash *)nor_flash, offset, size, data);
}

static int nor_read(void *ctx, u32 offset, void *data, u32 size)
{
	(void)ctx;
	nor_stage = offset >= NX30_NOR_BACKUP_OFFSET &&
	    offset < NX30_NOR_BACKUP_OFFSET + NX30_NOR_SLOT_SIZE ?
	    "backup_readback" : "primary_readback";
	if (!nor_flash || !data || !size ||
	    !((offset >= NX30_NOR_BACKUP_OFFSET &&
	       offset <= NX30_NOR_BACKUP_OFFSET + NX30_NOR_SLOT_SIZE &&
	       size <= NX30_NOR_BACKUP_OFFSET + NX30_NOR_SLOT_SIZE - offset &&
	       offset + size <= nor_flash->size) ||
	      (offset >= NX30_NOR_PRIMARY_OFFSET &&
	       offset <= NX30_NOR_PRIMARY_OFFSET + NX30_NOR_SLOT_SIZE &&
	       size <= NX30_NOR_PRIMARY_OFFSET + NX30_NOR_SLOT_SIZE - offset &&
	       offset + size <= nor_flash->size)))
		return -EINVAL;
	return spi_flash_read((struct spi_flash *)nor_flash, offset, size, data);
}

static const struct nx30_nor_flash_ops nor_flash_ops = {
	.verify_layout_and_bootconfig = nor_verify_layout,
	.erase = nor_erase,
	.write = nor_write,
	.read = nor_read,
};

static int nor_find_upload(struct httpd_request *request,
			   struct httpd_form_value **upload)
{
	struct httpd_form_value *value = NULL;
	unsigned int i, count = 0;

	if (!request || !upload)
		return -EINVAL;
	for (i = 0; i < request->form.count; i++) {
		if (request->form.values[i].name &&
		    !strcmp(request->form.values[i].name, "nor_image")) {
			value = &request->form.values[i];
			count++;
		}
	}
	if (count != 1 || request->form.count != 1 || !value ||
	    !value->data || !value->size)
		return -EINVAL;
	*upload = value;
	return 0;
}

static const char *nor_error_name(int ret)
{
	switch (ret) {
	case -ENODEV: return "expected_spi_nor_not_detected";
	case -EXDEV: return "runtime_partition_layout_mismatch";
	case -EFBIG: return "package_too_large";
	case -EBADMSG: return "payload_sha256_mismatch";
	case -EIO: return "nor_io_or_readback_error";
	case -EINVAL: return "wrapper_elf_or_bootconfig_invalid";
	case -ENOMEM: return "out_of_memory";
	default: return "candidate_or_device_rejected";
	}
}

static void nor_reply_failure(struct nor_http_session *session,
			      struct httpd_response *response, int ret)
{
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"fail\",\"error\":\"%s\","
		 "\"code\":%d,\"stage\":\"%s\"}",
		 nor_error_name(ret), ret, nor_stage);
	response->data = session->body;
	response->size = strlen(session->body);
}

static void nor_reply(struct nor_http_session *session,
		      struct httpd_response *response, const char *status,
		      const char *error, const char *extra)
{
	if (error)
		snprintf(session->body, sizeof(session->body),
			 "{\"status\":\"%s\",\"error\":\"%s\"%s}",
			 status, error, extra ? extra : "");
	else
		snprintf(session->body, sizeof(session->body),
			 "{\"status\":\"%s\"%s}", status,
			 extra ? extra : "");
	response->data = session->body;
	response->size = strlen(session->body);
}

static void nor_session_closed(struct httpd_response *response)
{
	if (response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
	}
}

static void nor_validate_handler(enum httpd_uri_handler_status status,
				 struct httpd_request *request,
				 struct httpd_response *response)
{
	struct nor_http_session *session = response->session_data;
	struct httpd_form_value *upload = NULL;
	struct nx30_nor_image image;
	char token[NX30_NOR_TOKEN_SIZE + 1];
	int ret, i;

	if (status == HTTP_CB_CLOSED) {
		nor_session_closed(response);
		return;
	}
	if (status == HTTP_CB_NEW) {
		session = calloc(1, sizeof(*session));
		if (!session) {
			response->status = HTTP_RESP_STD;
			response->info.code = 500;
			response->info.content_type = "application/json";
			response->info.connection_close = 1;
			response->data = "{\"status\":\"fail\",\"error\":\"out_of_memory\"}";
			response->size = strlen(response->data);
			return;
		}
		nor_clear_pending();
		response->session_data = session;
		response->status = HTTP_RESP_CUSTOM;
		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->info.code = 200;
		response->size = http_make_response_header(&response->info,
						  session->header,
						  sizeof(session->header));
		response->data = session->header;
		return;
	}
	if (status != HTTP_CB_RESPONDING || !session)
		return;
	if (session->body_sent) {
		response->status = HTTP_RESP_NONE;
		return;
	}
	ret = nor_find_upload(request, &upload);
	if (!ret && upload->size > NX30_NOR_WRAP_HEADER_SIZE + NX30_NOR_SLOT_SIZE)
		ret = -EFBIG;
	if (!ret)
		ret = nx30_nor_validate_package(upload->data, upload->size,
						 nor_digest, &image);
	if (!ret)
		ret = nor_verify_layout(NULL, image.nor_size);
	if (ret) {
		nor_clear_pending();
		nor_reply_failure(session, response, ret);
		session->body_sent = true;
		return;
	}
	nor_clear_pending();
	pending_package = malloc(upload->size);
	if (!pending_package) {
		nor_clear_pending();
		nor_reply_failure(session, response, -ENOMEM);
		session->body_sent = true;
		return;
	}
	memcpy((void *)pending_package, upload->data, upload->size);
	pending_package_size = upload->size;
	for (i = 0; i < NX30_NOR_TOKEN_SIZE; i++)
		token[i] = nor_token_alphabet[rand() & 0xf];
	token[NX30_NOR_TOKEN_SIZE] = 0;
	strlcpy(pending_token, token, sizeof(pending_token));
	pending_valid = true;
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"token\":\"%s\","
		 "\"payload_size\":%u}", token, image.payload_size);
	response->data = session->body;
	response->size = strlen(session->body);
	session->body_sent = true;
}

static void nor_write_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	struct nor_http_session *session;
	struct httpd_form_value *token;
	struct nx30_nor_image image;
	int ret;

	if (status == HTTP_CB_CLOSED) {
		nor_session_closed(response);
		return;
	}
	if (status != HTTP_CB_NEW)
		return;
	session = calloc(1, sizeof(*session));
	response->session_data = session;
	response->status = HTTP_RESP_STD;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
	response->info.code = session ? 200 : 500;
	if (!session) {
		response->data = "{\"status\":\"fail\",\"error\":\"out_of_memory\"}";
		response->size = strlen(response->data);
		return;
	}
	if (!pending_valid || !request || request->method != HTTP_POST ||
	    request->form.count != 1 ||
	    !(token = httpd_request_find_value(request, "token")) ||
	    !token->data || token->size != NX30_NOR_TOKEN_SIZE ||
	    memcmp(token->data, pending_token, NX30_NOR_TOKEN_SIZE)) {
		nor_clear_pending();
		nor_reply(session, response, "fail", "confirmation_expired_or_invalid", NULL);
		return;
	}
	pending_valid = false;
	nor_stage = "wrapper_revalidation";
	ret = nx30_nor_validate_package(pending_package, pending_package_size,
					 nor_digest, &image);
	if (!ret)
		ret = nx30_nor_write_both(&image, &nor_flash_ops, NULL);
	nor_clear_pending();
	if (ret) {
		printf("NOR rescue update stopped with error %d at %s; BOOTCONFIG unchanged\n",
		       ret, nor_stage);
		nor_reply_failure(session, response, ret);
		return;
	}
	nor_reply(session, response, "success", NULL,
		  ",\"message\":\"both APPSBL copies verified; BOOTCONFIG unchanged\"");
}

void nor_upgrade_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/nor/validate", &nor_validate_handler, NULL);
	httpd_register_uri_handler(inst, "/nor/write", &nor_write_handler, NULL);
}
