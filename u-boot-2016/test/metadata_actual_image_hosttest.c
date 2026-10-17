/* Actual-image regression for the production metadata core API. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt3000_b_metadata_core.h"

extern int rt3_md_prepare_elf(const rt3_md_u8 *, rt3_md_u32, rt3_md_u8 *,
                              rt3_md_u32, rt3_md_u32 *);
extern int rt3_md_validate_elf(const rt3_md_u8 *, rt3_md_u32);

static rt3_md_u8 *read_image(const char *path, rt3_md_u32 *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    rt3_md_u8 *p;
    if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 ||
        n > RT3_MD_APPSBL_SIZE || fseek(f, 0, SEEK_SET)) {
        if (f) fclose(f);
        return NULL;
    }
    p = (rt3_md_u8 *)malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p); fclose(f); return NULL;
    }
    fclose(f); *size = (rt3_md_u32)n; return p;
}

static void putle32(rt3_md_u8 *p, rt3_md_u32 v)
{ p[0]=(rt3_md_u8)v; p[1]=(rt3_md_u8)(v>>8); p[2]=(rt3_md_u8)(v>>16); p[3]=(rt3_md_u8)(v>>24); }

static int must_reject(const char *name, const rt3_md_u8 *data, rt3_md_u32 n)
{
    rt3_md_u8 *out = (rt3_md_u8 *)malloc(RT3_MD_APPSBL_SIZE);
    rt3_md_u32 outn = 0;
    int rejected = !out || rt3_md_prepare_elf(data, n, out,
                                               RT3_MD_APPSBL_SIZE, &outn) != 0;
    if (!rejected) fprintf(stderr, "negative accepted: %s\n", name);
    free(out);
    return rejected;
}

int main(int argc, char **argv)
{
    rt3_md_u32 en=0,mn=0,xn=0;
    rt3_md_u8 *elf, *mbn, *cross, *eout, *mout, *mut;
    rt3_md_u32 ecn=0,mcn=0;
    int failed=0;
    if (argc != 4) { fprintf(stderr,"usage: hosttest ELF MBN CROSS_BOARD_IMAGE\n"); return 2; }
    elf=read_image(argv[1],&en); mbn=read_image(argv[2],&mn); cross=read_image(argv[3],&xn);
    eout=(rt3_md_u8*)malloc(RT3_MD_APPSBL_SIZE); mout=(rt3_md_u8*)malloc(RT3_MD_APPSBL_SIZE);
    if (!elf||!mbn||!cross||!eout||!mout) { fprintf(stderr,"missing/oversize actual image input\n"); return 2; }
    if (rt3_md_prepare_elf(elf,en,eout,RT3_MD_APPSBL_SIZE,&ecn)) {
        fprintf(stderr,"same-board ELF rejected: %s\n",argv[1]); failed=1;
    }
    if (rt3_md_prepare_elf(mbn,mn,mout,RT3_MD_APPSBL_SIZE,&mcn)) {
        fprintf(stderr,"same-board MBN rejected: %s\n",argv[2]); failed=1;
    }
    if (!failed && (ecn != mcn || memcmp(eout,mout,ecn))) {
        fprintf(stderr,"ELF/MBN canonical outputs differ\n"); failed=1;
    }
    if (!failed && rt3_md_validate_elf(eout,ecn)) {
        fprintf(stderr,"canonical image failed production validator\n"); failed=1;
    }
    if (!failed) {
#if defined(RT3_METADATA_HOST_TEST_A)
        const char *expected_name = "H3C HW-A U-Boot ELF";
#elif defined(RT3_METADATA_HOST_TEST_C)
        const char *expected_name = "H3C HW-C U-Boot ELF";
#else
        const char *expected_name = "H3C HW-B U-Boot ELF";
#endif
        const char *board_name = rt3_md_uboot_board_name();
        const char *actual_name = rt3_md_uboot_image_name(eout, ecn);
        if (!board_name || strcmp(board_name, expected_name) ||
            !actual_name || strcmp(actual_name, expected_name)) {
            fprintf(stderr,"canonical board label mismatch: helper=%s image=%s expected=%s\n",
                    board_name ? board_name : "NULL",
                    actual_name ? actual_name : "NULL", expected_name);
            failed=1;
        }
    }
    if (!failed) printf("positive ELF+MBN canonical match and board label: %u bytes\n",ecn);
    if (!must_reject("cross-board DTB identity",cross,xn)) failed=1;
    if (ecn > 1 && !must_reject("truncated image",eout,ecn-1)) failed=1;

    /* Duplicate the PT_LOAD program header to make a real overlapping load. */
    if (!failed && ecn >= 52+3*32) {
        mut=(rt3_md_u8*)malloc(ecn); memcpy(mut,eout,ecn);
        memcpy(mut+52,mut+52+2*32,32);
        if (!must_reject("overlapping duplicate PT_LOAD",mut,ecn)) failed=1;
        free(mut);
    }
    /* Push the PT_LOAD file offset outside the actual input extent. */
    if (!failed && ecn >= 52+3*32) {
        mut=(rt3_md_u8*)malloc(ecn); memcpy(mut,eout,ecn);
        putle32(mut+52+2*32+4,0xfffffff0U);
        if (!must_reject("PT_LOAD file range out of bounds",mut,ecn)) failed=1;
        free(mut);
    }
    /* Alter a byte covered by the v3 segment hash without updating the hash. */
    if (!failed && ecn > 0x3000) {
        mut=(rt3_md_u8*)malloc(ecn); memcpy(mut,eout,ecn); mut[0x3000]^=1;
        if (rt3_md_validate_elf(mut,ecn)==0 ||
            !must_reject("v3 payload hash corruption",mut,ecn)) failed=1;
        free(mut);
    }
    if (!failed) printf("negative cases rejected: cross-board identity, truncation, overlapping PT_LOAD, range overflow, hash corruption\n");
    free(elf); free(mbn); free(cross); free(eout); free(mout);
    return failed ? 1 : 0;
}
