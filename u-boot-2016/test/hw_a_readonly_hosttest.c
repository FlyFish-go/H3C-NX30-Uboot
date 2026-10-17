#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h3c_hw_a_readonly.h"
#include "failsafe_env_text.h"
int main(void)
{
	assert(h3c_hw_a_nand_write_rejected() == -EROFS);
	struct failsafe_env_text_buffer t = { NULL, 0, 0 };
	char *large = malloc(600 * 1024), *out;
	const char *writes[] = { "/backup", "/upload", "/result", "/write", "/env/set", "/env/unset", "/env/reset/all", "/env/reset/single", "/env/restore", "/mac/set", "/network/set", "/network/reset", "/mibib/reload", "/reboot", "/webterm/exec", "/webterm/upload" };
	const char *reads[] = { "/", "/sysinfo", "/env/list", "/env/info", "/mac/info", "/network/info", "/version" };
	size_t i;
	assert(large); memset(large, 120, 600 * 1024 - 1); large[600 * 1024 - 1] = 0;
	assert(!failsafe_env_text_append(&t, "large", large));
	assert(t.len > 512 * 1024);
	out = malloc(t.len + 1); assert(out);
	t.data = out; t.size = t.len + 1; t.len = 0; out[0] = 0;
	assert(!failsafe_env_text_append(&t, "large", large));
	assert(t.len == 600 * 1024 + 6 && out[t.len - 1] == 10 && out[t.len] == 0);
	free(out); free(large);
	for (i = 0; i < sizeof(writes)/sizeof(writes[0]); i++) {
		assert(h3c_hw_a_request_rejected(1, 0, writes[i]));
		assert(h3c_hw_a_request_rejected(0, 0, writes[i]));
		if (strcmp(writes[i], "/backup"))
			assert(h3c_hw_a_request_rejected(0, 1, writes[i]));
	}
	assert(!h3c_hw_a_request_rejected(0, 1, "/backup"));
	assert(h3c_hw_a_request_rejected(0, 1, "/backup/"));
	assert(h3c_hw_a_request_rejected(0, 1, "/sysinfo"));
	assert(h3c_hw_a_request_rejected(0, 0, "/backup")); /* PUT/HEAD/etc. */
	for (i = 0; i < sizeof(reads)/sizeof(reads[0]); i++) {
		assert(!h3c_hw_a_request_rejected(1, 0, reads[i]));
		assert(h3c_hw_a_request_rejected(0, 0, reads[i]));
		assert(h3c_hw_a_request_rejected(0, 1, reads[i]));
	}
	assert(h3c_hw_a_request_rejected(1, 0, NULL));
	puts("HW-A dynamic ENV and POST /backup method matrix: PASS");
	return 0;
}
