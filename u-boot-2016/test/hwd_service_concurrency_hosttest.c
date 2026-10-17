#define HWD_SERVICE_HOST_TEST
#if __has_include("../service_hwd.c")
#include "../service_hwd.c"
#else
#include "../failsafe/modules/service_hwd.c"
#endif

#define CHECK(test) do { if (!(test)) { \
	 fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; \
} } while (0)

struct in_addr net_ip, net_netmask, net_server_ip;
static int mock_firmware_backend;
static int mock_appsbl_active = 1;
static int firmware_write_calls;
static int slot_cache_invalidations;
static int mock_default_pending;
static int default_prepare_calls;
static int default_abort_calls;

int nx30_d_layout_get(struct nx30_d_layout *layout)
{
	memset(layout, 0, sizeof(*layout));
	layout->generation = 0x12345678;
	layout->nand_size = 0x08000000;
	layout->erase_size = 0x20000;
	layout->write_size = 2048;
	layout->slot_a.size = 0x02800000;
	layout->slot_b.size = 0x02800000;
	return 0;
}

int nx30_d_slots_get(struct nx30_d_slots *slots)
{
	memset(slots, 0, sizeof(*slots));
	slots->generation = 0x12345678;
	slots->selector = 1;
	slots->age = 2;
	slots->ecc_bits = 4;
	slots->active = NX30_D_SLOT_B;
	slots->inactive = NX30_D_SLOT_A;
	strlcpy(slots->active_logical, "rootfs", sizeof(slots->active_logical));
	strlcpy(slots->logical_a, "rootfs_1", sizeof(slots->logical_a));
	strlcpy(slots->logical_b, "rootfs", sizeof(slots->logical_b));
	return 0;
}

void slots_rt3000_b_invalidate_cache(void) { slot_cache_invalidations++; }
int nx30_d_object_extent(enum nx30_d_object object, struct nx30_d_extent *extent)
{ (void)object; memset(extent, 0, sizeof(*extent)); return -EINVAL; }
int nx30_d_read_object(enum nx30_d_object object,u64 offset,void *buffer,size_t len,size_t *read)
{ (void)object;(void)offset;(void)buffer;(void)len;*read=0;return -EIO; }
int nx30_d_boot_slot(enum nx30_d_slot slot) { (void)slot; return 0; }
int nx30_d_default_prepare(enum nx30_d_slot s,char t[NX30_D_TOKEN_LEN],struct nx30_d_result *r)
{ (void)s;(void)r;if(mock_default_pending)return -EBUSY;mock_default_pending=1;default_prepare_calls++;strlcpy(t,"0123456789abcdef",NX30_D_TOKEN_LEN);return 0; }
int nx30_d_default_failure_get(struct nx30_d_result *r,u32 *c) { (void)r;(void)c;return -ENOENT; }
int nx30_d_default_commit(const char *t,struct nx30_d_result *r) { (void)t;(void)r;return -EOPNOTSUPP; }
int nx30_d_default_abort(const char *t)
{ if(!mock_default_pending||!t||strcmp(t,"0123456789abcdef"))return -EINVAL;mock_default_pending=0;default_abort_calls++;return 0; }
int nx30_d_firmware_validate(const void *i,size_t n,struct nx30_d_firmware_info *f) { if(!mock_firmware_backend)return -EOPNOTSUPP;memset(f,0,sizeof(*f));f->generation=7;f->image_crc32=0x1234;f->image_bytes=n;f->target=NX30_D_SLOT_A;strlcpy(f->release,"host-candidate",sizeof(f->release));(void)i;return 0; }
int nx30_d_firmware_write_inactive(const void *i,size_t n,u32 g,u32 c,struct nx30_d_result *r) { if(!mock_firmware_backend)return -EROFS;firmware_write_calls++;if(n!=3||g!=7||c!=0x1234||memcmp(i,"MBN",3))return -EINVAL;memset(r,0,sizeof(*r));r->target_offset=0x900000;r->target_size=0x2800000;r->bytes_verified=0x2800000;return 0; }
int nx30_d_env_get(struct nx30_d_env_snapshot *s) { (void)s;return -EOPNOTSUPP; }
int nx30_d_env_prepare(const struct nx30_d_env_change *c,size_t n,u32 g,char t[NX30_D_TOKEN_LEN],struct nx30_d_result *r) { (void)c;(void)n;(void)g;(void)t;(void)r;return -EROFS; }
int nx30_d_env_commit(const char *t,struct nx30_d_result *r) { (void)t;(void)r;return -EROFS; }
void nx30_d_env_abort(const char *t) { (void)t; }
int nx30_d_uboot_high_copy_active(void) { return mock_appsbl_active; }
int nx30_d_uboot_validate(const void *i,size_t n,struct nx30_d_firmware_info *f) { (void)i;(void)n;(void)f;return -EOPNOTSUPP; }
int nx30_d_uboot_write(const void *i,size_t n,u32 g,u32 c,struct nx30_d_result *r) { (void)i;(void)n;(void)g;(void)c;(void)r;return -EROFS; }
int gpio_get_value(unsigned int gpio) { (void)gpio;return 1; }
void led_on(const char *name) { (void)name; }
void led_off(const char *name) { (void)name; }
int run_command(const char *cmd,int flag) { (void)cmd;(void)flag;return 0; }

int main(void)
{
	struct httpd_request request = { .method = HTTP_GET };
	struct httpd_response first = { 0 }, second = { 0 };
	char first_copy[D_SERVICE_JSON_MAX];
	const char *first_data, *second_data;

	d_status_handler(HTTP_CB_NEW, &request, &first);
	d_status_handler(HTTP_CB_NEW, &request, &second);
	CHECK(first.status == HTTP_RESP_STD && second.status == HTTP_RESP_STD);
	CHECK(first.info.code == 200 && second.info.code == 200);
	CHECK(first.session_data && second.session_data);
	CHECK(first.session_data != second.session_data);
	CHECK(first.data != second.data);
	first_data = first.data;
	second_data = second.data;
	CHECK(first.size < sizeof(first_copy));
	memcpy(first_copy, first_data, first.size);
	first_copy[first.size] = '\0';
	d_status_handler(HTTP_CB_CLOSED, NULL, &first);
	CHECK(first.session_data == NULL);
	CHECK(second.data == second_data);
	CHECK(second.size == strlen(second.data));
	CHECK(strstr(second.data, "\"active_physical\":\"B\"") != NULL);
	CHECK(strstr(first_copy, "\"active_physical\":\"B\"") != NULL);
	CHECK(strstr(first_copy, "\"read_only\":false") != NULL);
	CHECK(strstr(first_copy, "\"env_transaction\":true") != NULL);
	CHECK(strstr(first_copy, "\"default_slot\":true") != NULL);
	CHECK(strstr(first_copy, "\"firmware_write\":true") != NULL);
	CHECK(strstr(first_copy, "\"appsbl_write\":true") != NULL);
	d_status_handler(HTTP_CB_CLOSED, NULL, &second);
	{
		struct httpd_response gated = { 0 };
		mock_appsbl_active = 0;
		d_status_handler(HTTP_CB_NEW, &request, &gated);
		CHECK(gated.info.code == 200);
		CHECK(strstr(gated.data, "\"appsbl_write\":false") != NULL);
		d_status_handler(HTTP_CB_CLOSED, NULL, &gated);
		mock_appsbl_active = 1;
	}
	CHECK(second.session_data == NULL);
	{
		struct httpd_form_value slot = { .name = "slot", .data = "b", .size = 1 };
		struct httpd_request prepare = { .method = HTTP_POST };
		struct httpd_response preflight = { 0 }, cancelled = { 0 }, again = { 0 };
		struct httpd_form_value token = { .name = "token", .data = "0123456789abcdef", .size = 16 };
		struct httpd_request abort = { .method = HTTP_POST };
		prepare.form.count = 1; prepare.form.values[0] = slot;
		d_default_prepare_handler(HTTP_CB_NEW, &prepare, &preflight);
		CHECK(preflight.info.code == 200 && mock_default_pending);
		d_default_prepare_handler(HTTP_CB_CLOSED, NULL, &preflight);
		abort.form.count = 1; abort.form.values[0] = token;
		d_default_abort_handler(HTTP_CB_NEW, &abort, &cancelled);
		CHECK(cancelled.info.code == 200 && !mock_default_pending);
		CHECK(default_abort_calls == 1);
		d_default_abort_handler(HTTP_CB_CLOSED, NULL, &cancelled);
		d_default_prepare_handler(HTTP_CB_NEW, &prepare, &again);
		CHECK(again.info.code == 200 && mock_default_pending);
		CHECK(default_prepare_calls == 2);
		d_default_prepare_handler(HTTP_CB_CLOSED, NULL, &again);
		/* Do not leave a mock pending transaction between test cases. */
		nx30_d_default_abort("0123456789abcdef");
	}
	{
		struct httpd_form_value image = { .name = "image", .data = "MBN", .size = 3 };
		struct httpd_request upload = { .method = HTTP_POST };
		struct httpd_response deferred = { 0 };
		upload.form.count = 1;
		upload.form.values[0] = image;
		d_firmware_validate_handler(HTTP_CB_NEW, &upload, &deferred);
		CHECK(deferred.status == HTTP_RESP_CUSTOM);
		CHECK(strncmp(deferred.data, "HTTP/1.0 200", 12) == 0);
		d_firmware_validate_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		CHECK(deferred.status == HTTP_RESP_CUSTOM);
		CHECK(strstr(deferred.data, "firmware_validate") != NULL);
		d_firmware_validate_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		CHECK(deferred.status == HTTP_RESP_NONE);
		d_firmware_validate_handler(HTTP_CB_CLOSED, NULL, &deferred);
		CHECK(deferred.session_data == NULL);
	}
	{
		struct httpd_form_value fields[2] = {
			{ .name = "image", .data = "MBN", .size = 3 },
			{ .name = "token", .data = "", .size = 0 },
		};
		struct httpd_request upload = { .method = HTTP_POST };
		struct httpd_response deferred = { 0 };
		char token[D_SERVICE_TOKEN_CHARS + 1];
		const char *found;
		unsigned int i;
		mock_firmware_backend = 1;
		upload.form.count = 1;
		upload.form.values[0] = fields[0];
		d_firmware_validate_handler(HTTP_CB_NEW, &upload, &deferred);
		d_firmware_validate_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		CHECK(strstr(deferred.data, "\"status\":\"success\"") != NULL);
		found = strstr(deferred.data, "\"token\":\"");
		CHECK(found != NULL);
		found += strlen("\"token\":\"");
		for (i = 0; i < D_SERVICE_TOKEN_CHARS; i++) token[i] = found[i];
		token[D_SERVICE_TOKEN_CHARS] = '\0';
		d_firmware_validate_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		d_firmware_validate_handler(HTTP_CB_CLOSED, NULL, &deferred);

		fields[0].data = "BAD";
		fields[1].data = token;
		fields[1].size = D_SERVICE_TOKEN_CHARS;
		upload.form.count = 2;
		upload.form.values[0] = fields[0]; upload.form.values[1] = fields[1];
		memset(&deferred, 0, sizeof(deferred));
		d_firmware_write_handler(HTTP_CB_NEW, &upload, &deferred);
		d_firmware_write_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		CHECK(strstr(deferred.data, "firmware_candidate_match") != NULL);
		CHECK(firmware_write_calls == 0);
		CHECK(slot_cache_invalidations == 0);
		d_firmware_write_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		d_firmware_write_handler(HTTP_CB_CLOSED, NULL, &deferred);

		fields[0].data = "MBN";
		upload.form.values[0] = fields[0];
		memset(&deferred, 0, sizeof(deferred));
		d_firmware_write_handler(HTTP_CB_NEW, &upload, &deferred);
		d_firmware_write_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		CHECK(strstr(deferred.data, "\"status\":\"success\"") != NULL);
		CHECK(firmware_write_calls == 1);
		CHECK(slot_cache_invalidations == 1);
		d_firmware_write_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		d_firmware_write_handler(HTTP_CB_CLOSED, NULL, &deferred);

		memset(&deferred, 0, sizeof(deferred));
		d_firmware_write_handler(HTTP_CB_NEW, &upload, &deferred);
		d_firmware_write_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		CHECK(strstr(deferred.data, "firmware_candidate_match") != NULL);
		CHECK(firmware_write_calls == 1);
		CHECK(slot_cache_invalidations == 1);
		d_firmware_write_handler(HTTP_CB_RESPONDING, &upload, &deferred);
		d_firmware_write_handler(HTTP_CB_CLOSED, NULL, &deferred);
	}
	puts("production HW-D JSON response isolation: PASS");
	return 0;
}
