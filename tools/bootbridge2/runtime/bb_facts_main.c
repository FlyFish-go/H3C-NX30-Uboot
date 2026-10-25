// SPDX-License-Identifier: GPL-2.0-or-later
/* bb-facts: print the device's hardware facts as JSON; with a rule file, also
 * the decision. Read-only.
 *
 *   bb-facts [rules.txt]
 */
#include "rules.h"

#include <stdio.h>
#include <stdlib.h>

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *buf = NULL;
	long n;

	if (!f)
		return NULL;
	if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET) &&
	    (buf = malloc(n + 1))) {
		if (fread(buf, 1, n, f) == (size_t)n) {
			buf[n] = 0;
		} else {
			free(buf);
			buf = NULL;
		}
	}
	fclose(f);
	return buf;
}

int main(int argc, char **argv)
{
	static char json[8192];
	struct bb_facts f;
	struct bb_rules r;
	struct bb_decision d;
	unsigned line = 0;
	char *text;

	bb_facts_collect(&f);
	if (bb_facts_json(&f, json, sizeof(json)) < 0)
		return 1;
	if (argc < 2) {
		printf("%s\n", json);
		return 0;
	}
	text = slurp(argv[1]);
	if (!text || bb_rules_load(&r, text, &line)) {
		fprintf(stderr, "cannot load rules %s (line %u)\n", argv[1], line);
		return 1;
	}
	bb_rules_decide(&r, &f, &d);
	printf("{\"facts\":%s,\"rule_set\":\"%s\",\"decision\":{\"code\":\"%s\",\"message\":\"%s\","
	       "\"board\":\"%s\",\"nand\":\"%s\",\"candidate\":\"%s\",\"detail\":\"%s\"}}\n",
	       json, r.set, bb_decide_str(d.code), bb_decide_text(d.code), d.board, d.nand,
	       d.cand ? d.cand->id : "", d.detail);
	bb_rules_free(&r);
	free(text);
	return d.code == BB_DEC_OK ? 0 : 1;
}
