/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "bridge_dispatch.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const SOURCE = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *const HASH = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char *const FILE_HASH = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
static const char *const models_gd[]={"GD5F1GQ5REYIG","GD5F1GQ5REYIG-ALT"};
static const char *const models_winbond[]={"W25N01GWZEIG"};
static const bdi_nand_profile profile_gd={"gd5f1gq5reyig-128m-2k-sp128-ecc8",models_gd,2,2,0xc8,0x41,128,2048,131072,128,128,8,10};
static const bdi_nand_profile profile_winbond={"w25n01gwzeig-128m-2k-sp64-ecc4",models_winbond,1,2,0xef,0xba,128,2048,131072,64,64,4,8};
static const bdi_nand_profile profile_gd_b_only={"gd5f1gq5reyig-b-only",models_gd,2,2,0xc8,0x41,128,2048,131072,128,128,8,2};

static bdi_provider_record row(const char *id, const char *cls, const char *model,
                               unsigned mfr, unsigned chip, unsigned raw, unsigned oob,
                               unsigned ecc, const char *compatible)
{
    bdi_provider_record r;
    memset(&r, 0, sizeof(r));
    r.record_id=id;r.candidate_id=cls[0]=='B'?"candidate-b":"candidate-d";
    r.hardware_class=cls; r.source_sha256=SOURCE; r.product="RT3000";
    r.route="conf-mp02.1"; r.route_compatible=compatible;
    (void)model;(void)mfr;(void)chip;(void)raw;(void)oob;(void)ecc;
    r.source_revision_sha256=HASH; r.stock_kernel_sha256=HASH;
    r.bridge_kernel_sha256=HASH; r.nand_dt_sha256=HASH;
    r.profile_set_id="rt3000-reviewed-nand-set-v2";r.candidate_set_id="rt3000-candidates-v2";
    r.nand_profiles=cls[0]=='B'?&profile_gd:&profile_winbond;r.nand_profile_count=1;
    r.provider_path=cls[0]=='B'?"/usr/sbin/bridge-candidates/rt-b/provider":"/usr/sbin/bridge-candidates/rt-d/provider";
    r.provider_sha256=HASH; r.provider_bytes=4096;
    r.payload_path=cls[0]=='B'?"/etc/bridge/providers/rt-b/uboot.bin":"/etc/bridge/providers/rt-d/uboot.bin";
    r.payload_sha256=HASH; r.payload_bytes=1310720;
    return r;
}

static bdi_facts facts(const char *model, unsigned mfr, unsigned chip,
                       unsigned raw, unsigned oob, unsigned ecc, const char *compat)
{
    bdi_facts f;
    memset(&f, 0, sizeof(f));
    size_t n=strlen(compat)+1; assert(n<=sizeof(f.compatible));
    memcpy(f.compatible,compat,n);f.compatible_bytes=n;
    strcpy(f.nand_model,model);f.jedec_bytes_observed=2;
    f.manufacturer_id=mfr;f.chip_id=chip;f.capacity_mib=128;
    f.page_bytes=2048;f.erase_bytes=131072;f.raw_spare_bytes=raw;
    f.mtd_oob_bytes=oob;f.ecc_bits=ecc;
    f.have_compatible=f.have_nand_model=f.have_jedec=f.have_geometry=f.have_mtd_format=1;
    return f;
}

static int selection_tests(void)
{
    bdi_provider_record r[2]={
        row("RT-B","B","GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1"),
        row("RT-D","D","W25N01GWZEIG",0xef,0xba,64,64,4,"qcom,ipq5018-mp02.1")};
    bdi_result result; bdi_facts f;
    f=facts("GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
    const bdi_nand_profile *matched=NULL;
    assert(bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)==&r[0]);
    assert(matched==&profile_gd);
    assert(result==BDI_OK);
    f=facts("GD5F1GQ5REYIG-ALT",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
    assert(bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)==&r[0]);
    assert(matched==&profile_gd&&result==BDI_OK);
    f=facts("W25N01GWZEIG",0xef,0xba,64,64,4,"qcom,ipq5018-mp02.1");
    assert(bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)==&r[1]&&result==BDI_OK);
    assert(matched==&profile_winbond);

    r[1].route_compatible="qcom,other-reviewed-route";
    f=facts("GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
    { static const uint8_t two_routes[]="qcom,ipq5018-mp02.1\0qcom,other-reviewed-route";
      memcpy(f.compatible,two_routes,sizeof(two_routes));f.compatible_bytes=sizeof(two_routes); }
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_FACTS_CONFLICT);
    r[1].route_compatible="qcom,ipq5018-mp02.1";

    f=facts("W25N01GWZEIG",0xef,0xba,64,64,4,"qcom,unknown");
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_NO_MATCH);
    f=facts("W25N01GWZEIG",0xef,0xba,64,64,8,"qcom,ipq5018-mp02.1");
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_NO_MATCH);
    f=facts("W25N01GWZEIG",0xef,0xba,128,64,4,"qcom,ipq5018-mp02.1");
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_NO_MATCH);
    f=facts("W25N01GWZEIG",0xef,0xba,64,64,4,"qcom,ipq5018-mp02.1");f.have_jedec=0;
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_FACTS_MISSING);
    f=facts("W25N01GWZEIG",0xef,0xba,64,64,8,"qcom,ipq5018-mp02.1");
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_NO_MATCH);
    f=facts("W25N01GWZEIG",0xef,0xba,64,64,4,"qcom,ipq5018-mp02.1");
    f=facts("W25N01GWZEIG",0xef,0xba,64,64,4,"qcom,ipq5018-mp02.1");
    { static const uint8_t dup[]="qcom,ipq5018-mp02.1\0qcom,ipq5018-mp02.1";
      memcpy(f.compatible,dup,sizeof(dup));f.compatible_bytes=sizeof(dup); }
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_FACTS_CONFLICT);

    /* Same mp02.1 route and same NAND facts cannot distinguish B/D. A
     * source+board+candidate-bound full OEM preflight may select one; two
     * passing contracts remain blocked. */
    { bdi_provider_record variants[2]={
          row("rt-b-cand-b","B","GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1"),
          row("rt-d-cand-d","D","GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1")};
      variants[1].nand_profiles=&profile_gd;
      f=facts("GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
      static const char report_b[]="{\"install_ready\":true,\"return_ready\":true,\"source_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"product\":\"RT3000\",\"route\":\"conf-mp02.1\",\"hardware_class\":\"B\",\"candidate_id\":\"candidate-b\",\"reason\":\"matched\"}\n";
      assert(bdi_inspect_report_matches(report_b,&variants[0]));
      assert(!bdi_inspect_report_matches(report_b,&variants[1]));
      assert(!bdi_inspect_report_matches("{\"install_ready\":true,\"source_sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\",\"product\":\"RT3000\",\"route\":\"conf-mp02.1\",\"hardware_class\":\"B\",\"candidate_id\":\"candidate-b\"}",&variants[0]));
      assert(!bdi_inspect_report_matches("{\"install_ready\":true,\"source_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"product\":\"RC3000\",\"route\":\"conf-mp02.1\",\"hardware_class\":\"B\",\"candidate_id\":\"candidate-b\"}",&variants[0]));
      assert(!bdi_inspect_report_matches("{\"install_ready\":true,\"source_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"product\":\"RT3000\",\"route\":\"conf-other\",\"hardware_class\":\"B\",\"candidate_id\":\"candidate-b\"}",&variants[0]));
      assert(!bdi_inspect_report_matches("{\"install_ready\":true,\"source_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"product\":\"RT3000\",\"route\":\"conf-mp02.1\",\"hardware_class\":\"D\",\"candidate_id\":\"candidate-b\"}",&variants[0]));
      assert(!bdi_inspect_report_matches("{\"install_ready\":true,\"source_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"product\":\"RT3000\",\"route\":\"conf-mp02.1\",\"hardware_class\":\"B\",\"candidate_id\":\"different-candidate\"}",&variants[0]));
      int probes[2]={1,0};
      assert(bdi_select_inspected_provider(variants,2,SOURCE,"RT3000",&f,probes,&matched,&result)==&variants[0]);
      assert(result==BDI_OK&&matched==&profile_gd);
      probes[1]=1;
      assert(!bdi_select_inspected_provider(variants,2,SOURCE,"RT3000",&f,probes,&matched,&result)&&result==BDI_CANDIDATE_AMBIGUOUS);
      probes[1]=0;probes[0]=0;
      assert(!bdi_select_inspected_provider(variants,2,SOURCE,"RT3000",&f,probes,&matched,&result)&&result==BDI_CANDIDATE_UNAVAILABLE);
      probes[0]=1;
      probes[0]=-1;
      assert(!bdi_select_inspected_provider(variants,2,SOURCE,"RT3000",&f,probes,&matched,&result)&&result==BDI_EXEC_FAILED);
    }
    f=facts("W25N01GWZEIG",0xef,0xba,64,64,4,"qcom,ipq5018-mp02.1");
    assert(!bdi_select_provider(r,2,"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc","RT3000",&f,&matched,&result)&&result==BDI_BAD_BUNDLE);

    /* Identical route+full tuple rows are allowed in a bundle but never select arbitrarily. */
    r[1]=row("RT-D-ALIAS","D","GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
    r[1].nand_profiles=&profile_gd;
    f=facts("GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_AMBIGUOUS);
    r[1].nand_profiles=&profile_gd_b_only;
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_BAD_BUNDLE);
    r[1].record_id=r[0].record_id;
    assert(!bdi_select_provider(r,2,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_BAD_BUNDLE);

    /* ABI v2 is a flat board×candidate multiset: repeated classes are valid,
     * and the supported bound is well above the former four-row limit. */
    { enum { MANY=80 };bdi_provider_record many[MANY];char ids[MANY][32],candidates[MANY][32],providers[MANY][128],payloads[MANY][128];
      for(unsigned i=0;i<MANY;i++){
          many[i]=row("placeholder","B","GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
          snprintf(ids[i],sizeof(ids[i]),"RT-B-VARIANT-%03u",i);many[i].record_id=ids[i];
          snprintf(candidates[i],sizeof(candidates[i]),"candidate-%03u",i);many[i].candidate_id=candidates[i];
          snprintf(providers[i],sizeof(providers[i]),"/usr/sbin/bridge-candidates/c-%03u/provider",i);many[i].provider_path=providers[i];
          snprintf(payloads[i],sizeof(payloads[i]),"/etc/bridge/providers/c-%03u/uboot.bin",i);many[i].payload_path=payloads[i];
      }
      f=facts("GD5F1GQ5REYIG",0xc8,0x41,128,128,8,"qcom,ipq5018-mp02.1");
      assert(!bdi_select_provider(many,MANY,SOURCE,"RT3000",&f,&matched,&result)&&result==BDI_AMBIGUOUS);
    }
    return 0;
}

static int artifact_tests(const char *dir)
{
    int root=open(dir,O_RDONLY|O_DIRECTORY);assert(root>=0);
    bdi_result result;int fd=bdi_open_verified_at(root,"/resource.bin",FILE_HASH,4,0,&result);
    assert(fd>=0&&result==BDI_OK);close(fd);
    fd=bdi_open_verified_at(root,"/resource.bin","cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",4,0,&result);
    assert(fd<0&&result==BDI_BAD_HASH);
    fd=bdi_open_verified_at(root,"/link.bin",HASH,4,0,&result);
    assert(fd<0&&result==BDI_BAD_PATH);
    fd=bdi_open_verified_at(root,"/resource.bin",HASH,5,0,&result);
    assert(fd<0&&result==BDI_BAD_FILE);
    close(root);return 0;
}

static int sysfs_tests(const char *dir)
{
    char path[4096],out[4096];
    snprintf(path,sizeof(path),"%s/sys/class/mtd/mtd0",dir);
    assert(symlink("../../devices/virtual/mtd/mtd0",path)==0);
    snprintf(path,sizeof(path),"%s/sys/devices/virtual/mtd/mtd0",dir);
    assert(mkdir(path,0755)==0);
    assert(bdi_resolve_mtd0_sysfs(dir,out,sizeof(out))==0);
    assert(strstr(out,"/sys/devices/virtual/mtd/mtd0")!=NULL);
    snprintf(path,sizeof(path),"%s/sys/class/mtd/mtd0",dir);
    assert(unlink(path)==0);
    assert(symlink("/etc",path)==0);
    assert(bdi_resolve_mtd0_sysfs(dir,out,sizeof(out))!=0);
    assert(unlink(path)==0);
    assert(symlink("../../devices/virtual/mtd/mtd1",path)==0);
    assert(bdi_resolve_mtd0_sysfs(dir,out,sizeof(out))!=0);
    assert(unlink(path)==0);
    assert(mkdir(path,0755)==0);
    assert(bdi_resolve_mtd0_sysfs(dir,out,sizeof(out))!=0);
    return 0;
}

static int parser_tests(const char *kernel_log_path)
{
    FILE *fp=fopen(kernel_log_path,"rb");assert(fp);
    char log[16384];size_t n=fread(log,1,sizeof(log),fp);assert(!ferror(fp));assert(feof(fp));fclose(fp);
    bdi_facts f;memset(&f,0,sizeof(f));
    assert(bdi_parse_kernel_identity(log,n,&f)==0);
    assert(f.manufacturer_id==0xef&&f.chip_id==0xba&&f.jedec_bytes_observed==2);
    assert(!strcmp(f.nand_model,"W25N01GWZEIG")&&f.capacity_mib==128);
    assert(f.page_bytes==2048&&f.erase_bytes==131072&&f.raw_spare_bytes==64);
    assert(bdi_parse_kernel_identity(log,n-1,&f)==0); /* parser uses the supplied byte count, not a terminator */
    { static const char duplicate[]="\n[ 1.0] nand: device found, Manufacturer ID: 0xef, Chip ID: 0xba\n";
      char *bad=malloc(n+sizeof(duplicate));assert(bad);memcpy(bad,log,n);memcpy(bad+n,duplicate,sizeof(duplicate)-1);
      assert(bdi_parse_kernel_identity(bad,n+sizeof(duplicate)-1,&f)!=0);free(bad); }

    { static const uint8_t compatible[]={"qcom,ipq5018-mp02.1\0qcom,ipq5018"};
      assert(bdi_parse_compatible(compatible,sizeof(compatible),&f)==0);
      assert(f.have_compatible&&f.compatible_bytes==sizeof(compatible)); }

    bdi_mtd_sysfs_values v;
    /* Linux 4.4 sysfs text attributes end with newlines; dev is MAJOR:MINOR. */
    assert(bdi_parse_mtd_sysfs_optional_offset(0,"0\n","nand\n","134217728\n","131072\n",
        "2048\n","64\n","4\n","90:0\n",&v)==0);
    assert(v.offset==0&&v.size==134217728&&v.erase_bytes==131072&&v.page_bytes==2048);
    assert(v.oob_bytes==64&&v.ecc_bits==4&&v.dev_major==90&&v.dev_minor==0);
    assert(bdi_parse_mtd_sysfs_optional_offset(1,NULL,"nand\n","134217728\n","131072\n",
        "2048\n","64\n","4\n","90:0\n",&v)==0&&v.offset==0);
    assert(bdi_parse_mtd_sysfs_optional_offset(0,"4096\n","nand\n","134217728\n","131072\n",
        "2048\n","64\n","4\n","90:0\n",&v)!=0);
    assert(bdi_parse_mtd_sysfs_optional_offset(1,"0\n","nand\n","134217728\n","131072\n",
        "2048\n","64\n","4\n","90:0\n",&v)!=0);
    assert(bdi_parse_mtd_sysfs_optional_offset(0,"0\n","nor\n","134217728\n","131072\n",
        "2048\n","64\n","4\n","90:0\n",&v)!=0);
    assert(bdi_parse_mtd_sysfs_optional_offset(0,"0\n","nand\n","134217728\n","131072\n",
        "2048\n","64\n","4\n","90:0\nextra",&v)!=0);
    assert(bdi_parse_mtd_sysfs_optional_offset(-1,NULL,"nand\n","134217728\n","131072\n",
        "2048\n","64\n","4\n","90:0\n",&v)!=0);

    { static const char proc[]="dev:    size   erasesize  name\nmtd0: 08000000 00020000 \"sbl\"\nmtd1: 00200000 00020000 \"tz\"";
      assert(bdi_parse_proc_mtd0(proc,sizeof(proc)-1,0x08000000,0x20000)==0);
      assert(bdi_parse_proc_mtd0(proc,sizeof(proc)-1,0x04000000,0x20000)!=0);
      static const char duplicate[]="dev: size erasesize name\nmtd0: 08000000 00020000 \"sbl\"\nmtd0: 08000000 00020000 \"again\"";
      assert(bdi_parse_proc_mtd0(duplicate,sizeof(duplicate)-1,0x08000000,0x20000)!=0);
      static const char malformed[]="dev: size erasesize name\nmtd0: bad row";
      assert(bdi_parse_proc_mtd0(malformed,sizeof(malformed)-1,0x08000000,0x20000)!=0); }
    return 0;
}

int main(int argc,char **argv)
{
    assert(argc==3);selection_tests();artifact_tests(argv[1]);sysfs_tests(argv[1]);parser_tests(argv[2]);
    puts("dispatcher host vectors PASS");return 0;
}
