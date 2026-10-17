#ifndef TEST_UBOOT_SHA256_H
#define TEST_UBOOT_SHA256_H
#define SHA256_SUM_LEN 32
#define CHUNKSZ_SHA256 4096
void sha256_csum_wd(const unsigned char *input, unsigned int len,
                    unsigned char *output, unsigned int chunk);
#endif
