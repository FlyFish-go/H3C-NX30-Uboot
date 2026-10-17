#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <h3c_nand_ecc.h>

static void expect_args(const char *input, unsigned int spare,
			unsigned int bits, const char *expected)
{
	char output[512];

	assert(h3c_nand_ecc_bootargs(input, output, sizeof(output), spare,
					     bits) == 0);
	assert(!strcmp(output, expected));
}

static unsigned int token_count(const char *text, const char *token)
{
	unsigned int count = 0;
	size_t length = strlen(token);

	while ((text = strstr(text, token)) != NULL) {
		count++;
		text += length;
	}
	return count;
}

int main(void)
{
	char output[512];
	struct {
		char text[32];
		unsigned char guard[16];
	} bounded;
	const char *canonical = "nandspare=128 nandeccbits=4";
	const char *boundary = "console=x rootwait nandspare=64 nandeccbits=4";
	char exact[sizeof("console=x rootwait nandspare=64 nandeccbits=4")];

	/* Supported profiles and preservation of unrelated parameters. */
	expect_args("console=ttyMSM0 cnss2.skip_radio_bmap=6", 128, 4,
		    "console=ttyMSM0 cnss2.skip_radio_bmap=6 "
		    "nandspare=128 nandeccbits=4");
	expect_args("rootwait", 128, 8,
		    "rootwait nandspare=128 nandeccbits=8");
	expect_args("  console=x\trootwait\r\n", 64, 4,
		    "console=x rootwait nandspare=64 nandeccbits=4");
	expect_args(" \t\r\n", 128, 4, canonical);
	expect_args("", 64, 4,
		    "nandspare=64 nandeccbits=4");

	/* Same-value duplicates are removed and emitted as exactly one pair. */
	expect_args("nandspare=128 nandspare=128\tnandeccbits=4 "
		    "nandeccbits=4 keep=1", 128, 4,
		    "keep=1 nandspare=128 nandeccbits=4");
	assert(!h3c_nand_ecc_bootargs("nandspare=128 nandeccbits=4", output,
					     sizeof(output), 128, 4));
	assert(token_count(output, "nandspare=") == 1);
	assert(token_count(output, "nandeccbits=") == 1);

	/* Decimal only, strict token values, conflicting duplicates, bad profiles. */
	assert(h3c_nand_ecc_bootargs("nandspare=128 nandeccbits=8", output,
					     sizeof(output), 128, 4) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandspare=128 nandspare=64", output,
					     sizeof(output), 128, 4) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandeccbits=4 nandeccbits=8", output,
					     sizeof(output), 128, 4) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandspare=0x80", output,
					     sizeof(output), 128, 4) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandspare", output, sizeof(output),
					     128, 4) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandeccbits=", output, sizeof(output),
					     128, 4) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandspare=64junk", output,
					     sizeof(output), 64, 4) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandeccbits=99999999999999999999",
					     output, sizeof(output), 128, 8) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandspare=64 nandeccbits=8", output,
					     sizeof(output), 64, 8) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandspare=128", output, sizeof(output),
					     128, 3) == -EINVAL);
	assert(h3c_nand_ecc_bootargs("nandspare=128", output, sizeof(output),
					     96, 4) == -EINVAL);

	/* Near-boundary success, one-byte-short failure, and canary protection. */
	assert(!h3c_nand_ecc_bootargs("console=x rootwait nandspare=64",
					     exact, sizeof(exact), 64, 4));
	assert(!strcmp(exact, boundary));
	assert(h3c_nand_ecc_bootargs("console=x rootwait", exact,
					     sizeof(exact) - 1U, 64, 4) == -ENOSPC);
	memset(&bounded, 0xa5, sizeof(bounded));
	assert(h3c_nand_ecc_bootargs("console=ttyMSM0", bounded.text,
					     sizeof(bounded.text), 128, 4) == -ENOSPC);
	assert(bounded.text[0] == '\0');
	for (size_t i = 0; i < sizeof(bounded.guard); i++)
		assert(bounded.guard[i] == 0xa5);
	assert(h3c_nand_ecc_bootargs("nandspare=128", output, sizeof(output),
					     128, 4) == 0);
	assert(!h3c_nand_ecc_bootargs("nandsparex=64 foo=", output,
					     sizeof(output), 128, 4));
	assert(!strcmp(output,
		       "nandsparex=64 foo= nandspare=128 nandeccbits=4"));
	assert(h3c_nand_ecc_bootargs("x", output, sizeof(output), 128, 4) == 0);
	assert(h3c_nand_ecc_bootargs(output, output, sizeof(output), 128, 4) ==
	       -EINVAL);

	/* The detector accepts only unique evidence. */
	assert(h3c_nand_ecc_unique_mode(1, 0) == 4);
	assert(h3c_nand_ecc_unique_mode(0, 1) == 8);
	assert(h3c_nand_ecc_unique_mode(0, 0) == 0);
	assert(h3c_nand_ecc_unique_mode(1, 1) == 0);

	puts("H3C NAND ECC profile validation, bootargs normalization and unique-mode tests: PASS");
	return 0;
}
