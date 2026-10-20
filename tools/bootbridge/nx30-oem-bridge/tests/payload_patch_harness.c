/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge_install.h"
#include <stdio.h>
#include <stdlib.h>

#define TEST_APPSBL_BYTES 0x140000u

int main(int argc, char **argv)
{
    FILE *input;
    unsigned char *image;
    size_t n;
    if (argc != 2 || !(input = fopen(argv[1], "rb"))) return 2;
    image = malloc(TEST_APPSBL_BYTES);
    if (!image) { fclose(input); return 3; }
    n = fread(image, 1, TEST_APPSBL_BYTES, input);
    if (n != TEST_APPSBL_BYTES || fgetc(input) != EOF || fclose(input)) {
        free(image);
        return 4;
    }
    if (bi_test_reconstruct_payload(image, n)) { free(image); return 5; }
    if (fwrite(image, 1, n, stdout) != n) { free(image); return 6; }
    free(image);
    return 0;
}
