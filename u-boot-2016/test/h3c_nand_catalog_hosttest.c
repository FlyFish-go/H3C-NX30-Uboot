#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <h3c_nand_catalog.h>
int main(void) {
 const unsigned char efba[2]={0xef,0xba}, fm[2]={0xa1,0xa5};
 const unsigned char e521[2]={0xe5,0x21}, c845[2]={0xc8,0x45};
 const unsigned char c2[2]={0x2c,0x15};
 const struct h3c_nand_catalog_profile *p;
 assert(H3C_NAND_CATALOG_PROFILE_COUNT==20);
 assert(h3c_nand_catalog_count_id(fm)==2);
 p=h3c_nand_catalog_by_profile_id("h3c-oem-efba-128m-2k-sp64");
 assert(p && p->runtime_ecc_mode_mask==H3C_NAND_ECC_MODE4);
 assert(p->appsbl_raw_spare_bytes==64 && p->qpic_raw_spare_bytes==64);
 assert(p->qpic_mtd_oob_ecc4==64 && p->kernel_oem_oob_field_mask==H3C_NAND_OOB_FIELD_64);
 assert(h3c_nand_catalog_match_qpic(efba,134217728U,2048U,131072U,64U,4U,64U)==p);
 assert(h3c_nand_catalog_match_oem_probe(efba,"W25N01GWZEIG",134217728U,2048U,131072U,64U,4U,64U)==p);
 assert(h3c_nand_catalog_match_qpic(c2,134217728U,2048U,131072U,128U,8U,80U));
 p=h3c_nand_catalog_match_qpic(fm,134217728U,2048U,131072U,64U,4U,64U);
 assert(p && !strcmp(p->profile_id,"h3c-oem-a1a5-128m-2k-sp64-p2048-e131072"));
 p=h3c_nand_catalog_match_qpic(fm,134217728U,2048U,131072U,128U,8U,80U);
 assert(p && !strcmp(p->profile_id,"h3c-oem-a1a5-128m-2k-sp128-p2048-e131072"));
 p=h3c_nand_catalog_match_qpic(e521,134217728U,2048U,131072U,128U,4U,64U);
 assert(p && !strcmp(p->profile_id,"h3c-oem-e521-128m-2k-sp128"));
 p=h3c_nand_catalog_match_qpic(c845,536870912U,2048U,131072U,128U,4U,64U);
 assert(p && p->capacity_bytes==134217728U && p->qpic_capacity_bytes==536870912U);
 assert(!h3c_nand_catalog_match_qpic(c845,134217728U,2048U,131072U,128U,4U,64U));
 assert(!h3c_nand_catalog_match_oem_probe(efba,"W25N01GWZEIG",134217728U,2048U,131072U,64U,8U,80U));
 puts("H3C shared OEM NAND catalog host checks: PASS");
 return 0;
}

