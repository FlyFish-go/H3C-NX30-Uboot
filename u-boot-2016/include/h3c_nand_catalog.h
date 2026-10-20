/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Generated from audited NX30/RC3000/RT3000 OEM tables:
 * evidence/h3c-oem-complete-offline-20261010/nand/appsbl-nand-table.json
 * evidence/h3c-oem-complete-offline-20261010/nand/kernel-image-nand-rows.json
 * Runtime QPIC descriptor facts are cross-checked against qpic_nand.c.
 * OEM physical spare fields and Linux table OOB fields intentionally remain separate.
 * OEM source hashes (SHA-256): appsbl 96bf7d2de0800766157d8aef9c279fbbfad511ce76dcb83b624e9b8ded09358c; kernel 25b223de2e3e6298ec8ff6117f173fc29be48d663f514d9aa883aebb549bb3df.
 * OEM tables report two-byte ID prefixes; runtime QPIC must also validate its full selected ID/model.
 */
#ifndef __H3C_NAND_CATALOG_H__
#define __H3C_NAND_CATALOG_H__
#ifdef __UBOOT__
#include <linux/string.h>
#else
#include <string.h>
#endif
/* Use only C built-ins so this header is shared by U-Boot and host tools. */
#define H3C_NAND_ECC_MODE4 0x01U
#define H3C_NAND_ECC_MODE8 0x02U
#define H3C_NAND_SRC_APPSBL_A0 0x01U
#define H3C_NAND_SRC_APPSBL_A1 0x02U
#define H3C_NAND_SRC_APPSBL_B 0x04U
#define H3C_NAND_SRC_APPSBL_D 0x08U
#define H3C_NAND_SRC_APPSBL_C 0x10U
#define H3C_NAND_SRC_KERNEL 0x01U
#define H3C_NAND_SRC_QPIC 0x02U
#define H3C_NAND_OOB_FIELD_64 0x01U
#define H3C_NAND_OOB_FIELD_128 0x02U
#define H3C_NAND_OOB_FIELD_256 0x04U
#define H3C_NAND_CAP_APPSBL_ROW 0x01U
#define H3C_NAND_CAP_KERNEL_ROW 0x02U
#define H3C_NAND_CAP_QPIC_ID 0x04U
#define H3C_NAND_CAP_QPIC_GEOMETRY 0x08U
#define H3C_NAND_CAP_FIXED_READ 0x10U
#define H3C_NAND_CAP_D_LAYOUT_GEOMETRY 0x20U
struct h3c_nand_catalog_profile {
 const char *profile_id; const char *oem_model; const char *qpic_model;
 unsigned char id[2]; unsigned int capacity_bytes, qpic_capacity_bytes, page_bytes, pages_per_block, erase_bytes;
 unsigned short appsbl_raw_spare_bytes; unsigned char appsbl_ecc_mode_mask, appsbl_source_mask;
 unsigned char appsbl_ecc4_source_mask, appsbl_ecc8_source_mask;
 unsigned short kernel_oem_oob_field_mask; unsigned char kernel_ecc_mode_mask;
 unsigned short kernel_ecc_step_bytes; unsigned char kernel_image_rows;
 unsigned short qpic_raw_spare_bytes; unsigned char qpic_ecc_mode_mask, runtime_ecc_mode_mask;
 unsigned short qpic_mtd_oob_ecc4, qpic_mtd_oob_ecc8; unsigned char capabilities;
};
static const struct h3c_nand_catalog_profile h3c_nand_catalog_profiles[] = {
 {"h3c-oem-2c15-128m-2k-sp128","MT29F1G01ABBFDWB-IT","MT29F1G01ABBFDWB-IT",{0x2c,0x15},134217728U,134217728U,2048U,64U,131072U,128U,0x02U,0x1fU,0x00U,0x1fU,0x02U,0x02U,512U,47U,128U,0x02U,0x02U,64U,80U,0x3fU},
 {"h3c-oem-98d2-128m-2k-sp128","TC58CYG0S3HRAIG","TC58CYG0S3HRAIJ",{0x98,0xd2},134217728U,134217728U,2048U,64U,131072U,128U,0x02U,0x1fU,0x00U,0x1fU,0x02U,0x02U,512U,47U,128U,0x02U,0x02U,64U,80U,0x3fU},
 {"h3c-oem-98db-128m-2k-sp64","TC58CYG1S3HRAIJ","TC58CYG1S3HRAIJ",{0x98,0xdb},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-a1a5-128m-2k-sp64-p2048-e131072","FM25LS01","FM25LS01",{0xa1,0xa5},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x08U,0x08U,0x00U,0x01U,0x01U,512U,2U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-a1a5-128m-2k-sp128-p2048-e131072","FM25LS01","FM25LS01",{0xa1,0xa5},134217728U,134217728U,2048U,64U,131072U,128U,0x02U,0x17U,0x00U,0x17U,0x02U,0x02U,512U,45U,128U,0x02U,0x02U,0U,80U,0x3fU},
 {"h3c-oem-c292-128m-2k-sp64","MX35UF1GE4AC","MX35UF1GE4AC",{0xc2,0x92},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-c811-128m-2k-sp64","F50D1G41LB(2M)","F50D1G41LB(2M)",{0xc8,0x11},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-c821-128m-2k-sp64","GD5F1GQ5REYIH","GD5F1GQ5REYIH",{0xc8,0x21},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-c822-256m-2k-sp64","GD5F2GQ5REYIH","GD5F2GQ5REYIH",{0xc8,0x22},268435456U,268435456U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x1fU},
 {"h3c-oem-c841-128m-2k-sp128","GD5F1GQ5REYIG","GD5F1GQ5REYIG",{0xc8,0x41},134217728U,134217728U,2048U,64U,131072U,128U,0x03U,0x1fU,0x08U,0x17U,0x02U,0x03U,512U,47U,128U,0x03U,0x03U,64U,80U,0x3fU},
 {"h3c-oem-c845-128m-2k-sp128","GD5F4GQ6REYIGY","GD5F4GQ6RExxG",{0xc8,0x45},134217728U,536870912U,2048U,64U,131072U,128U,0x01U,0x1fU,0x1fU,0x00U,0x02U,0x01U,512U,47U,128U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-c881-128m-2k-sp128","GD5F1GM7REYIG","GD5F1GM7REYIG",{0xc8,0x81},134217728U,134217728U,2048U,64U,131072U,128U,0x02U,0x11U,0x00U,0x11U,0x00U,0x00U,0U,0U,128U,0x02U,0x02U,64U,80U,0x3dU},
 {"h3c-oem-c8c1-128m-2k-sp128","GD5F1GQ4RE9IG","GD5F1GQ4RE9IG",{0xc8,0xc1},134217728U,134217728U,2048U,64U,131072U,128U,0x02U,0x1fU,0x00U,0x1fU,0x02U,0x02U,512U,47U,128U,0x02U,0x02U,64U,80U,0x3fU},
 {"h3c-oem-c8c9-128m-2k-sp64","GD5F1GQ4RE9IH","GD5F1GQ4RE9IH",{0xc8,0xc9},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-e521-128m-2k-sp128","DS35M1GA-IB","DS35M1GA",{0xe5,0x21},134217728U,134217728U,2048U,64U,131072U,128U,0x01U,0x0eU,0x0eU,0x00U,0x00U,0x00U,0U,0U,128U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-e572-128m-2k-sp64","DS35Q2GA","DS35Q2GA",{0xe5,0x72},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-e5a4-128m-2k-sp128","DS35M4GM","DS35M4GM",{0xe5,0xa4},134217728U,134217728U,2048U,64U,131072U,128U,0x02U,0x1fU,0x00U,0x1fU,0x02U,0x02U,512U,47U,128U,0x02U,0x02U,64U,80U,0x3fU},
 {"h3c-oem-efba-128m-2k-sp64","W25N01GWZEIG","W25N01GWZEIG",{0xef,0xba},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-efbc-128m-2k-sp64","W25N01JW","W25N01JW",{0xef,0xbc},134217728U,134217728U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x3fU},
 {"h3c-oem-efbf-256m-2k-sp64","W25N02JWZEIF","W25N02JWZEIF",{0xef,0xbf},268435456U,268435456U,2048U,64U,131072U,64U,0x01U,0x1fU,0x1fU,0x00U,0x01U,0x01U,512U,47U,64U,0x01U,0x01U,64U,0U,0x1fU},
};
#define H3C_NAND_CATALOG_PROFILE_COUNT (sizeof(h3c_nand_catalog_profiles)/sizeof(h3c_nand_catalog_profiles[0]))
static inline unsigned char h3c_nand_catalog_mode_bit(unsigned int bits) { return bits==4U?H3C_NAND_ECC_MODE4:bits==8U?H3C_NAND_ECC_MODE8:0U; }
static inline const struct h3c_nand_catalog_profile *h3c_nand_catalog_by_profile_id(const char *id) { unsigned int i; if(!id)return 0; for(i=0;i<H3C_NAND_CATALOG_PROFILE_COUNT;i++) if(!strcmp(id,h3c_nand_catalog_profiles[i].profile_id)) return &h3c_nand_catalog_profiles[i]; return 0; }
static inline unsigned int h3c_nand_catalog_count_id(const unsigned char id[2]) { unsigned int i; unsigned int n=0; if(!id)return 0; for(i=0;i<H3C_NAND_CATALOG_PROFILE_COUNT;i++) if(h3c_nand_catalog_profiles[i].id[0]==id[0]&&h3c_nand_catalog_profiles[i].id[1]==id[1]) n++; return n; }
static inline const struct h3c_nand_catalog_profile *h3c_nand_catalog_match_qpic(const unsigned char id[2], unsigned int capacity,unsigned int page,unsigned int erase,unsigned int raw_spare,unsigned int ecc_bits,unsigned int mtd_oob) {
 unsigned int i; const struct h3c_nand_catalog_profile *found=0; unsigned char mode=h3c_nand_catalog_mode_bit(ecc_bits); if(!id||!mode)return 0;
 for(i=0;i<H3C_NAND_CATALOG_PROFILE_COUNT;i++){ const struct h3c_nand_catalog_profile *p=&h3c_nand_catalog_profiles[i]; unsigned int expected_oob=ecc_bits==4U?p->qpic_mtd_oob_ecc4:p->qpic_mtd_oob_ecc8;
  if(p->id[0]!=id[0]||p->id[1]!=id[1]||p->qpic_capacity_bytes!=capacity||p->page_bytes!=page||p->erase_bytes!=erase||p->qpic_raw_spare_bytes!=raw_spare||!(p->runtime_ecc_mode_mask&mode)||expected_oob!=mtd_oob) continue;
  if (found)
   return 0;
  found = p;
 } return found;
}
static inline const struct h3c_nand_catalog_profile *h3c_nand_catalog_match_oem_probe(const unsigned char id[2],const char *model,unsigned int capacity,unsigned int page,unsigned int erase,unsigned int raw_oob,unsigned int ecc_bits,unsigned int mtd_oob) {
 unsigned int i; const struct h3c_nand_catalog_profile *found=0; unsigned char mode=h3c_nand_catalog_mode_bit(ecc_bits); if(!id||!model||!mode)return 0;
 for(i=0;i<H3C_NAND_CATALOG_PROFILE_COUNT;i++){ const struct h3c_nand_catalog_profile *p=&h3c_nand_catalog_profiles[i]; unsigned int expected_oob=ecc_bits==4U?p->qpic_mtd_oob_ecc4:p->qpic_mtd_oob_ecc8;
  if(p->id[0]!=id[0]||p->id[1]!=id[1]||strcmp(p->oem_model,model)||p->capacity_bytes!=capacity||p->page_bytes!=page||p->erase_bytes!=erase||p->appsbl_raw_spare_bytes!=raw_oob||!(p->runtime_ecc_mode_mask&mode)||expected_oob!=mtd_oob)continue;
  if (found)
   return 0;
  found = p;
 }return found;
}
static inline unsigned int h3c_nand_catalog_index(
 const struct h3c_nand_catalog_profile *p)
{
 unsigned int i;
 if (!p) return H3C_NAND_CATALOG_PROFILE_COUNT;
 for (i = 0; i < H3C_NAND_CATALOG_PROFILE_COUNT; i++)
  if (p == &h3c_nand_catalog_profiles[i]) return i;
 return H3C_NAND_CATALOG_PROFILE_COUNT;
}
static inline int h3c_nand_catalog_runtime_read_qualified(
 const struct h3c_nand_catalog_profile *p, unsigned int ecc_bits)
{
 unsigned char mode = h3c_nand_catalog_mode_bit(ecc_bits);
 return p && mode && (p->capabilities & H3C_NAND_CAP_QPIC_ID) &&
        (p->capabilities & H3C_NAND_CAP_QPIC_GEOMETRY) &&
        (p->capabilities & H3C_NAND_CAP_FIXED_READ) &&
        (p->runtime_ecc_mode_mask & mode);
}
static inline const struct h3c_nand_catalog_profile *
h3c_nand_catalog_match_qpic_model(const unsigned char id[2], const char *model,
 unsigned int capacity, unsigned int page, unsigned int erase,
 unsigned int raw_spare, unsigned int ecc_bits, unsigned int mtd_oob)
{
 const struct h3c_nand_catalog_profile *p =
  h3c_nand_catalog_match_qpic(id, capacity, page, erase, raw_spare,
                               ecc_bits, mtd_oob);
 return p && model && !strcmp(p->qpic_model, model) ? p : 0;
}

/* Catalog facts describe table consistency and read candidates only. There is
 * deliberately no APPSBL installation-qualification API here: a matching ID,
 * geometry and OEM row cannot establish device/chain/candidate qualification. */

#endif /* __H3C_NAND_CATALOG_H__ */
