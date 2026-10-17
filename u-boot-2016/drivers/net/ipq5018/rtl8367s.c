/* SPDX-License-Identifier: GPL-2.0-only
 * RTL8367S fixed 1000/full SGMII setup for H3C HW-C.
 */
#include <common.h>
#include <asm/io.h>
#include <asm/errno.h>
#include "ipq_mdio.h"
#define RTL8367S_MDIO_CTRL0_VALUE 0x1502f
#define PHY 29
#define R_CTRL0 31
#define R_CTRL1 21
#define R_ADDR 23
#define R_WDATA 24
#define R_RDATA 25
#define OP_ADDR 0x000e
#define OP_READ 0x0001
#define OP_WRITE 0x0003
struct rtl8367s_jam { u16 reg,val; };
static const struct rtl8367s_jam rtl8367s_init_jam_8365mb_vc[] = {
	{ 0x13EB, 0x15BB }, { 0x1303, 0x06D6 }, { 0x1304, 0x0700 },
	{ 0x13E2, 0x003F }, { 0x13F9, 0x0090 }, { 0x121E, 0x03CA },
	{ 0x1233, 0x0352 }, { 0x1237, 0x00A0 }, { 0x123A, 0x0030 },
	{ 0x1239, 0x0084 }, { 0x0301, 0x1000 }, { 0x1349, 0x001F },
	{ 0x18E0, 0x4004 }, { 0x122B, 0x241C }, { 0x1305, 0xC000 },
	{ 0x13F0, 0x0000 },
};
static const struct rtl8367s_jam rtl8367s_init_jam_common[] = {
	{ 0x1200, 0x7FCB }, { 0x0884, 0x0003 }, { 0x06EB, 0x0001 },
	{ 0x03Fa, 0x0007 }, { 0x08C8, 0x00C0 }, { 0x0A30, 0x020E },
	{ 0x0800, 0x0000 }, { 0x0802, 0x0000 }, { 0x09DA, 0x0013 },
	{ 0x1D32, 0x0002 },
};
#ifdef CONFIG_TARGET_IPQ5018_H3C_HW_C
static const struct rtl8367s_jam rtl8367s_sds_jam_sgmii[] = {
	{ 0x0480, 0x04D7 }, { 0x0481, 0xF994 }, { 0x0482, 0x2420 },
	{ 0x0483, 0x6960 }, { 0x0484, 0x9728 }, { 0x0423, 0x9D85 },
	{ 0x0424, 0xD810 }, { 0x002E, 0x83F2 },
};
#endif

static int mdio_wait(void) {
 int i; for(i=0;i<2000;i++) {
  if(!(readl(IPQ_MDIO_BASE+MDIO_CTRL_4_REG)&MDIO_CTRL_4_ACCESS_BUSY)) return 0;
  udelay(10);
 } return -ETIMEDOUT;
}
static int c22_read(u8 reg,u16 *v) {
 int ret;
 writel(RTL8367S_MDIO_CTRL0_VALUE,IPQ_MDIO_BASE+MDIO_CTRL_0_REG);
 writel((PHY<<8)|reg,IPQ_MDIO_BASE+MDIO_CTRL_1_REG);
 writel(MDIO_CTRL_4_ACCESS_START|MDIO_CTRL_4_ACCESS_CODE_READ,IPQ_MDIO_BASE+MDIO_CTRL_4_REG);
 ret=mdio_wait();
 if(ret) {
  printf("RTL8367S MDIO C22 read timeout phy=%u reg=%u ret=%d\n",PHY,reg,ret);
  return ret;
 }
 *v=(u16)readl(IPQ_MDIO_BASE+MDIO_CTRL_3_REG);
 if(*v==0xffff) {
  printf("RTL8367S MDIO C22 read phy=%u reg=%u raw=0x%04x\n",PHY,reg,*v);
  return -EIO;
 }
 return 0;
}
static int c22_write(u8 reg,u16 v) {
 writel(RTL8367S_MDIO_CTRL0_VALUE,IPQ_MDIO_BASE+MDIO_CTRL_0_REG);
 writel((PHY<<8)|reg,IPQ_MDIO_BASE+MDIO_CTRL_1_REG);
 writel(v,IPQ_MDIO_BASE+MDIO_CTRL_2_REG);
 writel(MDIO_CTRL_4_ACCESS_START|MDIO_CTRL_4_ACCESS_CODE_WRITE,IPQ_MDIO_BASE+MDIO_CTRL_4_REG);
 {
  int ret=mdio_wait();
  if(ret)
   printf("RTL8367S MDIO C22 write timeout phy=%u reg=%u value=0x%04x ret=%d\n",PHY,reg,v,ret);
  return ret;
 }
}
static int mdio_begin(void) { return c22_write(R_CTRL0,OP_ADDR); }
static int reg_read(u16 reg,u16 *v) {
 int ret;
 if((ret=mdio_begin())) goto ctrl0_fail;
 if((ret=c22_write(R_ADDR,reg))) goto address_fail;
 if((ret=c22_write(R_CTRL1,OP_READ))) goto ctrl1_fail;
 if((ret=c22_read(R_RDATA,v))) {
  if(ret==-EIO)
   printf("RTL8367S switch read swreg=0x%04x stage=data c22reg=%u raw=0x%04x ret=%d\n",reg,R_RDATA,*v,ret);
  else
   printf("RTL8367S switch read swreg=0x%04x stage=data c22reg=%u ret=%d\n",reg,R_RDATA,ret);
  return ret;
 }
 return 0;
ctrl0_fail:
 printf("RTL8367S switch read swreg=0x%04x stage=ctrl0 ret=%d\n",reg,ret);
 return ret;
address_fail:
 printf("RTL8367S switch read swreg=0x%04x stage=address ret=%d\n",reg,ret);
 return ret;
ctrl1_fail:
 printf("RTL8367S switch read swreg=0x%04x stage=ctrl1 ret=%d\n",reg,ret);
 return ret;
}
static int reg_write(u16 reg,u16 v) {
 int ret;
 if((ret=mdio_begin())) goto ctrl0_fail;
 if((ret=c22_write(R_ADDR,reg))) goto address_fail;
 if((ret=c22_write(R_WDATA,v))) goto data_fail;
 if((ret=c22_write(R_CTRL1,OP_WRITE))) goto ctrl1_fail;
 return 0;
ctrl0_fail:
 printf("RTL8367S switch write swreg=0x%04x stage=ctrl0 value=0x%04x ret=%d\n",reg,v,ret);
 return ret;
address_fail:
 printf("RTL8367S switch write swreg=0x%04x stage=address value=0x%04x ret=%d\n",reg,v,ret);
 return ret;
data_fail:
 printf("RTL8367S switch write swreg=0x%04x stage=data value=0x%04x ret=%d\n",reg,v,ret);
 return ret;
ctrl1_fail:
 printf("RTL8367S switch write swreg=0x%04x stage=ctrl1 value=0x%04x ret=%d\n",reg,v,ret);
 return ret;
}
static int update(u16 reg,u16 mask,u16 val) {
 u16 old; int ret=reg_read(reg,&old);
 return ret?ret:reg_write(reg,(old&~mask)|(val&mask));
}
static int sds_wait(void) {
 u16 v; int i,ret;
 for(i=0;i<100;i++) { if((ret=reg_read(0x6600,&v))) return ret;
  if(!(v&0x0100)) return 0; udelay(10); }
 return -ETIMEDOUT;
}
static int sds_write(u16 addr,u16 val) {
 int ret; if((ret=sds_wait())) return ret;
 if((ret=reg_write(0x6602,val))) return ret;
 if((ret=reg_write(0x6601,addr))) return ret;
 if((ret=reg_write(0x6600,0x00c0))) return ret;
 return sds_wait();
}
static int sds_read(u16 addr,u16 *val) {
 int ret; if((ret=sds_wait())) return ret;
 if((ret=reg_write(0x6601,addr))) return ret;
 if((ret=reg_write(0x6600,0x0080))) return ret;
 if((ret=sds_wait())) return ret;
 return reg_read(0x6602,val);
}
#ifdef CONFIG_TARGET_IPQ5018_H3C_HW_C
/* RTL8365MB QSDK fixed 1000/full SGMII setup (port 6 / EXT1). */
static int sgmii_setup(void) {
 u16 option,nway; unsigned int i; int ret;
 if((ret=reg_write(0x13c0,0x0249))) return ret;
 ret=reg_read(0x13c1,&option);
 { int r=reg_write(0x13c0,0); if(!ret) ret=r; }
 if(ret) return ret;
 if(!option) return -EOPNOTSUPP;
 /* Stop DW8051 and bypass before the SGMII sequence; no firmware upload. */
 if((ret=update(0x1322,0x0010,0x0010))) return ret;
 if((ret=update(0x130c,0x0020,0))) return ret;
 if((ret=update(0x03f7,0x0002,0))) return ret;
 for(i=0;i<ARRAY_SIZE(rtl8367s_sds_jam_sgmii);i++)
  if((ret=sds_write(rtl8367s_sds_jam_sgmii[i].reg,rtl8367s_sds_jam_sgmii[i].val))) return ret;
 if((ret=update(0x1d11,0x0840,0x0040))) return ret;
 if((ret=update(0x1305,0x00f0,0x0090))) return ret;
 if((ret=sds_write(0x0003,0x7106))) return ret;
 if((ret=sds_write(0x0000,0x1401))) return ret;
 if((ret=sds_write(0x0000,0x1403))) return ret;
 if((ret=sds_read(0x0002,&nway))) return ret;
 nway=(nway&~0x0200)|0x0100;
 return sds_write(0x0002,nway);
}

int rtl8367s_sgmii_link_down(void) {
 int ret=reg_write(0x1311,0x1000);
 if(ret) return ret;
 return update(0x1d11,0x0200,0);
}

int rtl8367s_sgmii_link_up(void) {
 int ret=reg_write(0x1311,0x1016);
 if(ret) return ret;
 return update(0x1d11,0x0780,0x0700);
}
#endif

int rtl8367s_switch_init(void) {
 u16 id=0,ver=0,v; unsigned int i; int ret,clear_ret;
 /* QSDK RTL8365MB detect requires this temporary magic before chip-ID reads. */
 ret=reg_write(0x13c2,0x0249);
 if(ret) goto clear_magic;
 if((ret=reg_read(0x1300,&id))) goto clear_magic;
 if((ret=reg_read(0x1301,&ver))) goto clear_magic;
 printf("RTL8367S identity read chip=0x%04x version=0x%04x\n",id,ver);
clear_magic:
 clear_ret=reg_write(0x13c2,0);
 if(!ret && clear_ret) ret=clear_ret;
 if(ret) goto fail;
 if(id!=0x6367 || ver!=0x00a0) { ret=-ENODEV; goto fail; }
 if((ret=reg_write(0x1322,0x0001))) goto fail;
 mdelay(100);
 for(i=0;i<100;i++) { if((ret=reg_read(0x1322,&v))) goto fail;
  if(!(v&1)) break; mdelay(10); }
 if(i==100) { ret=-ETIMEDOUT; goto fail; }
 for(i=0;i<ARRAY_SIZE(rtl8367s_init_jam_8365mb_vc);i++)
  if((ret=reg_write(rtl8367s_init_jam_8365mb_vc[i].reg,rtl8367s_init_jam_8365mb_vc[i].val))) goto fail;
 for(i=0;i<ARRAY_SIZE(rtl8367s_init_jam_common);i++)
  if((ret=reg_write(rtl8367s_init_jam_common[i].reg,rtl8367s_init_jam_common[i].val))) goto fail;
 if((ret=sgmii_setup())) goto fail;
 if((ret=rtl8367s_sgmii_link_up())) goto fail;
 if((ret=reg_write(0x1219,0x0040))) goto fail;
 /* CPU enable, insert mode NONE, trap port 6; no tags on U-Boot frames. */
 if((ret=reg_write(0x121a,0x00b5))) goto fail;
 /* LAN ports swp1..3 and CPU6 share a bridge; swp0/4 and port5 stay isolated. */
 { static const u16 masks[7]={0x0001,0x004e,0x004e,0x004e,0x0010,0x0020,0x004e};
  for(i=0;i<ARRAY_SIZE(masks);i++) if((ret=reg_write(0x08a2+i,masks[i]))) goto fail;
 }
 /* STP forwarding (3) only on ports 1,2,3 and CPU6; other ports disabled. */
 if((ret=reg_write(0x0a00,0x30fc))) goto fail;
 if((ret=update(0x088c,0x3fff,1536))) goto fail;
 printf("RTL8367S 6367:00a0 initialized; raw CPU port6 SGMII 1000/full\n");
 return 0;
fail:
 printf("RTL8367S init failed (%d)\n",ret);
 return ret;
}
