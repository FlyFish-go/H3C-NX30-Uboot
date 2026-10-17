#!/usr/bin/env python3
"""Host regression using the production shared A/B/C QPIC table, functions and verified OEM descriptors."""
import json, pathlib, re, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
src = (root / "drivers/mtd/nand/qpic_nand.c").read_text()
def func(name):
    m = re.search(r"(?:static\s+)?(?:int|void)\s+" + name + r"\s*\([^;]*?\)\s*\{", src, re.S)
    if not m: raise RuntimeError("production function missing: " + name)
    b = src.index("{", m.start()); depth = 0
    for i in range(b, len(src)):
        if src[i] == "{": depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0: return src[m.start():i+1]
a = src.index("static struct qpic_serial_nand_params qpic_serial_nand_tbl[] = {")
b = src.index("\n};", a) + 3
table = src[a:b]
functions = "\n".join(func(n) for n in ("qpic_serial_update_dev_params", "qpic_nand_save_config", "qpic_nand_read_id_serial", "qpic_spi_nand_config"))
header = (root / "arch/arm/include/asm/arch-qca-common/qpic_nand.h").read_text()
struct_start = header.index("struct qpic_serial_nand_params {")
params_struct = header[struct_start:header.index("\n};", struct_start)+3]
fixture = json.loads((root / "test/qpic_h3c_oem_nand_profiles.json").read_text())
pre = r"""
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_H3C_IPQ5018_COMMON 1
#define CONFIG_H3C_QPIC_DUAL_ECC 1
#define CONFIG_QPIC_SERIAL 1
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MTD_QPIC_NAND_DEV(m) ((m)->priv)
#define MTD_NAND_CHIP(m) ((m)->chip)
#define WINBOND_MFR_ID 0xef
#define MICRON_DEVICE_ID 0x152c152c
#define CMD3_MASK 0xfff0ffff
#define NAND_CW_DIV_RIGHT_SHIFT 9
#define QPIC_NAND_MAX_CWS_IN_PAGE 8
#define NAND_CW_SIZE_4_BIT_ECC 528
#define NAND_CW_SIZE_8_BIT_ECC 532
#define DATA_BYTES_IN_IMG_PER_CW 516
#define USER_DATA_BYTES_PER_CW 500
#define NAND_DEV0_ECC_MODE_SHIFT 0
#define NAND_DEV0_CFG0_SPARE_SZ_BYTES_SHIFT 0
#define NAND_DEV0_ECC_PARITY_SZ_BYTES_SHIFT 0
#define NAND_DEV0_CFG0_CW_PER_PAGE_SHIFT 0
#define NAND_DEV0_CFG0_UD_SIZE_BYTES_SHIFT 0
#define NAND_DEV0_CFG0_ADDR_CYCLE_SHIFT 0
#define NAND_DEV0_CFG0_DIS_STS_AFTER_WR_SHIFT 0
#define NAND_DEV0_CFG1_RECOVERY_CYCLES_SHIFT 0
#define NAND_DEV0_CFG1_CS_ACTIVE_BSY_SHIFT 0
#define NAND_DEV0_CFG1_BAD_BLK_BYTE_NUM_SHIFT 0
#define NAND_DEV0_CFG1_BAD_BLK_IN_SPARE_SHIFT 0
#define NAND_DEV0_CFG1_WR_RD_BSY_GAP_SHIFT 0
#define NAND_DEV0_CFG1_WIDE_BUS_SHIFT 0
#define NAND_DEV0_ECC_DISABLE_SHIFT 0
#define NAND_DEV0_ECC_SW_RESET_SHIFT 0
#define NAND_DEV0_ECC_NUM_DATA_BYTES 0
#define NAND_DEV0_ECC_FORCE_CLK_OPEN_SHIFT 0
#define FLASH_SPI_NAND_BLK_PROCT_ADDR 0xa0
#define FLASH_SPI_NAND_BLK_PROCT_ENABLE 0x7c
#define FLASH_SPI_NAND_BLK_PROCT_DISABLE 0
#define FLASH_SPI_NAND_FR_ADDR 0xb0
#define FLASH_SPI_NAND_FR_ECC_ENABLE 0x10
#define FLASH_SPI_NAND_FR_BUFF_ENABLE 0x8
#define FLASH_SPI_NAND_FR_QUAD_ENABLE 1
#define NAND_FLASH_DEV_CMD3_VAL 0x3F310015U
#define SPI_NAND_DEV_CMD3 0
#define QPIC_SERIAL_ERROR 42
#define EINVAL 22
#define CONFIG_QSPI_LAYOUT_SWITCH 0
#define GENMASK(h,l) 0
static unsigned cmd3_written;
#define writel(v,a) (cmd3_written=(v),(void)(a))
#define qspi_debug(...) ((void)0)
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
PARAMS_STRUCT_PLACEHOLDER
struct qpic_nand_dev { uint32_t id; unsigned page_size,block_size,num_blocks,widebus,density,spare_size,num_pages_per_blk,num_pages_per_blk_mask,timing_mode_support; bool quad_mode,check_quad_config; unsigned cws_per_page,cw_size,ecc_bch_cfg,ecc_bytes_hw,spare_bytes,bbm_size,cfg0,cfg1,oob_per_page,bad_blk_loc,cfg0_raw,cfg1_raw; uint32_t vendor; };
struct nand_ecclayout { int dummy; };
struct nand_chip { struct { struct nand_ecclayout *layout; } ecc; };
struct mtd_info { struct qpic_nand_dev *priv; struct nand_chip *chip; unsigned writesize,erasesize,oobsize,oobavail,writebufsize,ecc_strength; uint64_t size; };
static struct nand_ecclayout fake_ecc_layout;
enum qpic_nand_layout { QPIC_NAND_LAYOUT_SBL, QPIC_NAND_LAYOUT_LINUX }; static enum qpic_nand_layout qpic_layout;
static bool qpic_serial_model_identified;
static struct qpic_nand_dev dev;
static unsigned feature[256];
static int ignore_clear, ignore_quad, get_error, set_error;
static int qpic_nand_fetch_id(struct mtd_info *m){(void)m;return 0;}
static int qpic_serial_get_feature(struct mtd_info*m,uint32_t a){(void)m;return get_error ? get_error : (int)(feature[a]<<8);}
static int qpic_serial_set_feature(struct mtd_info*m,uint32_t a,uint32_t v){(void)m;if(set_error)return set_error;if(a==0xb0&&ignore_quad)v&=~1U;if(!(a==FLASH_SPI_NAND_FR_ADDR&&ignore_clear&&(feature[a]&16)&&!(v&16)))feature[a]=v&255;return 0;}
"""

pre = pre.replace("PARAMS_STRUCT_PLACEHOLDER", params_struct)
oem_checks = []
for row in fixture["rows"]:
    raw = bytes.fromhex(row["descriptor_hex"])
    expected = ",".join(str(x) for x in raw[:34])
    ident = int.from_bytes(bytes.fromhex(row["id_hex"]), "little")
    oem_checks.append('profile(%d,"%s",%d,%d,%d);' %
                      (ident, row["display_name"], row["spare_size"],
                       row["num_bits_ecc_correctability"],
                       80 if row["num_bits_ecc_correctability"] == 8 else 64))
    oem_checks.append('{ const unsigned char expected[34]={%s};' % expected +
                      'assert(!memcmp(serial_params,expected,18));' +
                      'assert(!memcmp((const unsigned char*)serial_params+20,expected+20,14));' +
                      'assert(serial_params->no_of_dies==0 && serial_params->prev_die_id==0 && serial_params->protec_bpx==0 && serial_params->pages_per_die==0);}')
    oem_checks.append('init_features(%d);' % ident)

post = r"""
static void profile(uint32_t id,const char*n,unsigned spare,unsigned ecc,unsigned oob){
 struct nand_chip c={0};struct mtd_info m={.priv=&dev,.chip=&c};memset(&dev,0,sizeof dev);dev.id=id;
 assert(qpic_nand_read_id_serial(&m)==0&&qpic_serial_model_identified);assert(!strcmp(serial_params->name,n));
 qpic_serial_update_dev_params(&m);assert(m.size==0x08000000ULL&&m.writesize==2048&&m.erasesize==0x20000);
 assert(dev.spare_size==spare&&m.oobsize==spare&&m.ecc_strength==ecc&&m.oobsize>=oob);
 assert(qpic_nand_save_config(&m)==0&&m.oobsize==oob&&c.ecc.layout==&fake_ecc_layout);
}
static void alias(uint32_t id,const char*n){struct mtd_info m={.priv=&dev};memset(&dev,0,sizeof dev);dev.id=id;assert(qpic_nand_read_id_serial(&m)==0&&qpic_serial_model_identified&&!strcmp(serial_params->name,n));}
static void init_features(uint32_t id) {
 struct mtd_info m={.priv=&dev};
 dev.id=id; assert(qpic_nand_read_id_serial(&m)==0);
 dev.vendor=id&255;
 assert(dev.quad_mode && dev.check_quad_config);
 memset(feature,0,sizeof feature); feature[0xa0]=0x7c; feature[0xb0]=0xb8;
 cmd3_written=NAND_FLASH_DEV_CMD3_VAL;
 assert(qpic_spi_nand_config(&m)==0);
 assert(feature[0xa0]==0 && feature[0xb0]==0xa9 && dev.quad_mode);
 assert(cmd3_written==NAND_FLASH_DEV_CMD3_VAL);
}
static void negative_and_feature_errors(void) {
 struct mtd_info m={.priv=&dev};
 dev.id=0xdead; assert(qpic_nand_read_id_serial(&m)<0 && !qpic_serial_model_identified);
 alias(0x21bcef,"W25N01JW");
 dev.id=0x22bcef; assert(qpic_nand_read_id_serial(&m)<0 && !qpic_serial_model_identified);
 init_features(0x21bcef);
 init_features(0x22bfef);
 feature[0xb0]=0xb8; ignore_quad=1;
 assert(qpic_spi_nand_config(&m)==0 && !dev.quad_mode && feature[0xb0]==0xa8);
 ignore_quad=0; dev.quad_mode=true; feature[0xb0]=0xb8; ignore_clear=1;
 assert(qpic_spi_nand_config(&m)==-QPIC_SERIAL_ERROR); ignore_clear=0;
 get_error=-17; assert(qpic_spi_nand_config(&m)==-17); get_error=0;
 set_error=-18; assert(qpic_spi_nand_config(&m)==-18); set_error=0;
 feature[0xb0]=0xa8; set_error=-19; assert(qpic_spi_nand_config(&m)==-19); set_error=0;
 dev.id=MICRON_DEVICE_ID; dev.vendor=0x2c; dev.check_quad_config=false;
 assert(qpic_spi_nand_config(&m)==0 && cmd3_written==0x3F300015U);
}
int main(void){
 assert(offsetof(struct qpic_serial_nand_params,page_size)==4);
 assert(offsetof(struct qpic_serial_nand_params,no_of_blocks)==16);
 assert(offsetof(struct qpic_serial_nand_params,density)==20);
 assert(offsetof(struct qpic_serial_nand_params,num_bits_ecc_correctability)==30);
 assert(offsetof(struct qpic_serial_nand_params,quad_mode)==32);
 for(unsigned i=0;i<ARRAY_SIZE(qpic_serial_nand_tbl);i++) {
  for(unsigned j=i+1;j<ARRAY_SIZE(qpic_serial_nand_tbl);j++) {
   const struct qpic_serial_nand_params *a=&qpic_serial_nand_tbl[i], *b=&qpic_serial_nand_tbl[j];
   assert(a->id[0]!=b->id[0] || a->id[1]!=b->id[1] || (a->id[0]==WINBOND_MFR_ID && a->id[2]!=b->id[2]));
  }
 }
 OEM_CHECKS_PLACEHOLDER
 negative_and_feature_errors();
 profile(0x11c8,"F50D1G41LB(2M)",64,4,64);profile(0xa5a1,"FM25LS01",128,4,64);
 profile(0x21baef,"W25N01GWZEIG",64,4,64);profile(0x41c8,"GD5F1GQ5REYIG",128,8,80);
 alias(0x20baef,"W25N512GW");alias(0x22baef,"W25N02KWZEIR");alias(0x23baef,"W25N04KWZEIR");
 {struct mtd_info m={.priv=&dev};memset(&dev,0,sizeof dev);dev.vendor=WINBOND_MFR_ID;feature[0xb0]=16;
 assert(qpic_spi_nand_config(&m)==0&&!(feature[0xb0]&16));feature[0xb0]=16;ignore_clear=1;assert(qpic_spi_nand_config(&m)<0);ignore_clear=0;}
 puts("PASS production table: four OEM descriptors, geometry/BCH/OOB, existing samples, Winbond three-byte aliases, unknown IDs, QE bit preservation/fallback, ECC guard and feature errors");return 0;
}
"""
post = post.replace("OEM_CHECKS_PLACEHOLDER", "\n".join(oem_checks))
for target in ("IPQ5018_H3C_RT3000_B", "IPQ5018_H3C_HW_C", "IPQ5018_H3C_HW_A"):
    with tempfile.TemporaryDirectory() as td:
        c=pathlib.Path(td)/"t.c"; exe=pathlib.Path(td)/"t"
        c.write_text("#define CONFIG_TARGET_"+target+" 1\n"+pre+"\n"+table+"\nstruct qpic_serial_nand_params *serial_params;\n"+functions+"\n"+post)
        cc=subprocess.run(["gcc","-std=gnu99","-w","-fsanitize=undefined","-fno-sanitize-recover=all",str(c),"-o",str(exe)],capture_output=True,text=True)
        if cc.returncode: raise SystemExit(cc.stderr)
        run=subprocess.run([str(exe)],capture_output=True,text=True)
        print(target+": "+run.stdout,end="")
        if run.returncode: raise SystemExit(run.stderr or "host regression failed")
