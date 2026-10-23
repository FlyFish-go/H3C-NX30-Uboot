#define _GNU_SOURCE
#include "bridge_install.h"
#include "bridge_root_binding.h"
#include "bridge_oem_slot.h"
#include "bridge_oem_return.h"
#include "bridge_return_env_policy.h"
#include "bridge_sha256.h"
#include "bridge_version.h"
#include "bridge_backup.h"
#include "bridge_mtd_observer.h"
#include "bridge_first_install.h"
#include "bridge_install_identity.h"
#include "h3c_nand_catalog.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <mtd/mtd-user.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(BRIDGE_INSTALL_TEST) && defined(__GLIBC__)
#include <sys/random.h>
#endif

#ifdef BRIDGE_INSTALL_TEST
#define BI_TEST 1
#ifdef BRIDGE_VERSION_COMPILED_TEST_REGISTRY
#include <test_version_registry.h>
#endif
#else
#define BI_TEST 0
#endif
#if defined(BRIDGE_RETURN_REGISTRY_TEST) && !defined(BRIDGE_INSTALL_TEST)
#error BRIDGE_RETURN_REGISTRY_TEST requires the synthetic BRIDGE_INSTALL_TEST build
#endif

#if defined(BI_ROOT_BINDING_CONTRACT_HEADER)
#include BI_ROOT_BINDING_CONTRACT_HEADER
#elif BI_TEST
/* Synthetic historical fixture only. Production always requires its exact
 * source package's generated contract, including RETURN_ONLY helpers. */
#define BI_ROOT_BINDING_CONTRACT {UINT64_C(17395712), UINT64_C(126976), UINT64_C(137), UINT64_C(2048)}
#define BI_ROOT_BINDING_CONTRACT_SHA256 "synthetic-fixture-only"
#else
#error Production root binding requires BI_ROOT_BINDING_CONTRACT_HEADER
#endif
static const br_gluebi_contract g_bridge_root_contract = BI_ROOT_BINDING_CONTRACT;

#if defined(BRIDGE_RETURN_ONLY)
#define BI_HAVE_PAYLOAD 0
#elif !BI_TEST
#include <bridge_payload.h>
#define BI_HAVE_PAYLOAD 1
#else
#define BI_HAVE_PAYLOAD 1
#endif
#ifndef BI_HAVE_PAYLOAD
#define BI_HAVE_PAYLOAD 0
#endif
#if defined(BI_PAYLOAD_PATCH_HEADER) && (!BI_TEST || defined(BRIDGE_PAYLOAD_PATCH_TEST))
#include BI_PAYLOAD_PATCH_HEADER
#define BI_HAVE_PAYLOAD_PATCHES 1
#else
#define BI_HAVE_PAYLOAD_PATCHES 0
#endif
#if defined(BRIDGE_PAYLOAD_PATCH_TEST) && !defined(BRIDGE_INSTALL_TEST)
#error BRIDGE_PAYLOAD_PATCH_TEST requires the synthetic BRIDGE_INSTALL_TEST build
#endif

#ifndef BI_PROFILE
#define BI_PROFILE "H3C-BOOTBRIDGE-MULTICONFIG-V1"
#endif
#ifndef BI_IP
#define BI_IP "192.168.10.1"
#endif
#ifndef BI_PAYLOAD_VERSION
#define BI_PAYLOAD_VERSION "unavailable"
#endif
#ifndef BI_PAYLOAD_BYTES
#define BI_PAYLOAD_BYTES 0
#endif
#if defined(BRIDGE_RETURN_ONLY)
#define BI_STATUS_CANDIDATE_BYTES 0u
#else
#define BI_STATUS_CANDIDATE_BYTES (BI_TEST?APPSBL_BYTES:BI_PAYLOAD_BYTES)
#endif
#ifndef BI_SBL_VALID_BYTES
#define BI_SBL_VALID_BYTES 0
#endif
#ifndef BI_PAYLOAD_PATH
#define BI_PAYLOAD_PATH ""
#endif
#ifndef BI_PAYLOAD_SHA256
#define BI_PAYLOAD_SHA256 ""
#endif
#ifndef BI_SBL_SHA256
#define BI_SBL_SHA256 ""
#endif
#ifndef BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE
#define BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE 0
#endif
#ifndef BI_FIRST_INSTALL_RECORD
#define BI_FIRST_INSTALL_RECORD ""
#endif
#ifndef BI_NAND_CATALOG_SHA256
#define BI_NAND_CATALOG_SHA256 ""
#endif
#ifndef BI_SUPPORTED_NAND_PROFILE_IDS
#if BI_TEST
#define BI_SUPPORTED_NAND_PROFILE_IDS "h3c-oem-efba-128m-2k-sp64"
#else
#define BI_SUPPORTED_NAND_PROFILE_IDS ""
#endif
#endif
#ifndef BI_APPSBL_LOAD_PROFILE_IDS
#if BI_TEST
#define BI_APPSBL_LOAD_PROFILE_IDS "h3c-oem-efba-128m-2k-sp64"
#else
#define BI_APPSBL_LOAD_PROFILE_IDS ""
#endif
#endif
#ifndef BI_APPSBL_STORAGE_PROFILE_IDS
#if BI_TEST
#define BI_APPSBL_STORAGE_PROFILE_IDS "h3c-oem-efba-128m-2k-sp64"
#else
#define BI_APPSBL_STORAGE_PROFILE_IDS ""
#endif
#endif
#ifndef BI_APPSBL_LOAD_ECC_BITS
#if BI_TEST
#define BI_APPSBL_LOAD_ECC_BITS 4
#else
#define BI_APPSBL_LOAD_ECC_BITS 0
#endif
#endif
#ifndef BI_APPSBL_STORAGE_ECC_BITS
#if BI_TEST
#define BI_APPSBL_STORAGE_ECC_BITS 4
#else
#define BI_APPSBL_STORAGE_ECC_BITS 0
#endif
#endif

/* Production has no implicit OEM oracle. Build-only headers contain exact
 * candidate/geometry-bound records, never a runtime "verified" switch. */
#if BI_TEST && defined(BRIDGE_OEM_COMPILED_TEST_MANIFEST)
#include <test_oem_manifest.h>
#define BI_HAVE_OEM_MANIFEST 1
#elif !BI_TEST && defined(BI_OEM_MANIFEST_HEADER)
#include BI_OEM_MANIFEST_HEADER
#define BI_HAVE_OEM_MANIFEST 1
#else
#define BI_HAVE_OEM_MANIFEST 0
#endif
#if BI_HAVE_OEM_MANIFEST
#ifndef BI_OEM_MANIFESTS
#define BI_OEM_MANIFESTS (&bootbridge_oem_manifest)
#define BI_OEM_MANIFEST_COUNT 1u
#endif
#endif

#if (!BI_TEST || defined(BRIDGE_RETURN_REGISTRY_TEST)) && defined(BI_RETURN_SOURCE_HEADER)
#include BI_RETURN_SOURCE_HEADER
#endif
#if defined(BI_RETURN_SOURCE_REGISTRY_HEADER) && \
    (!BI_TEST || defined(BRIDGE_RETURN_REGISTRY_TEST))
#include BI_RETURN_SOURCE_REGISTRY_HEADER
#define BI_HAVE_RETURN_SOURCE_REGISTRY 1
#else
#define BI_HAVE_RETURN_SOURCE_REGISTRY 0
#endif
#if defined(BI_RETURN_SOURCE_REGISTRY_V2) && !BI_HAVE_RETURN_SOURCE_REGISTRY
#error "source-bound install-provider return policy requires its compiled source registry"
#endif

/* The builder selects this descriptor from a pinned board candidate and an
 * independently pinned OEM source. It is never read from runtime JSON/ENV. */
#if !BI_TEST && defined(BI_FIRST_INSTALL_PROFILE_HEADER)
#include BI_FIRST_INSTALL_PROFILE_HEADER
#define BI_HAVE_INSTALL_PROFILE 1
#else
#define BI_HAVE_INSTALL_PROFILE 0
#endif
#ifndef BI_CANDIDATE_ID
#define BI_CANDIDATE_ID BI_PAYLOAD_VERSION
#endif

#ifndef BI_RETURN_SOURCE_OFFICIAL_SHA256
#define BI_RETURN_SOURCE_OFFICIAL_SHA256 ""
#define BI_RETURN_SOURCE_STOCK_KERNEL_SHA256 ""
#define BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256 ""
#define BI_RETURN_SOURCE_REVISION_SHA256 ""
#define BI_RETURN_SOURCE_COMPRESSED_KERNEL_SHA256 ""
#define BI_RETURN_SOURCE_NAND_DT_SHA256 ""
#define BI_RETURN_SOURCE_ROUTE ""
#define BI_RETURN_SOURCE_COMPATIBLE ""
#define BI_RETURN_SOURCE_PRODUCT 0u
#endif
#ifndef BI_HARDWARE_CLASS
#define BI_HARDWARE_CLASS ""
#endif
#ifndef BI_EXPECTED_PRODUCT
#define BI_EXPECTED_PRODUCT ""
#endif
#ifndef BI_FIRST_INSTALL_ROUTE
#define BI_FIRST_INSTALL_ROUTE BI_RETURN_SOURCE_ROUTE
#endif
#if !BI_TEST && !defined(BRIDGE_RETURN_ONLY) && !defined(BI_RETURN_SOURCE_REGISTRY_V2)
#include "bridge_oem_return_reviewed.h"
#endif
#ifndef BI_RETURN_RULES
#define BI_RETURN_RULES NULL
#define BI_RETURN_RULE_COUNT 0u
#endif

#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
/* This helper binds a compiled multi-route registry. A single-route revision
 * marker would be false here; the selected exact revision is reported after
 * matching the read-only OEM source and device-tree route. */
static const char g_return_source_binary_binding[]="BB_RETURN_SOURCE_REGISTRY:compiled-source-routes";
#else
static const char g_return_source_binary_binding[]="BB_RETURN_SOURCE_REVISION:" BI_RETURN_SOURCE_REVISION_SHA256;
#endif

#define PAGE_BYTES 2048u
#define ERASE_BYTES 0x20000u
#define OOB_BYTES 64u
#define APPSBL_BYTES 0x140000u
#define BC_BYTES 0x40000u
#define ART_BYTES 0x100000u
#define STARTUP_END 0x900000u
#define APPSBL_LO 0x500000u
#define APPSBL_HI 0x640000u
#define ENV_OFF 0x480000u
#define ART_OFF 0x780000u
#define BC0_OFF 0x100000u
#define BC1_OFF 0x140000u
#define MAX_PARTS 64
#define STATUS_CAP 16384
#define MAGIC 0xa3a2a1a0u
#define TAIL 0xb3b2b1b0u

typedef struct {
    unsigned index;
    uint32_t off, size, erase, page, oob, ecc;
    dev_t dev;
    char name[96], path[PATH_MAX];
    int fd;
    uint8_t *snapshot;
} bi_part;

typedef struct {
    char name[64], status[16], detail[256];
} bi_check;

/* Identity evidence comes from this boot's kernel probe log. Geometry alone
 * cannot distinguish two dies with different ROM/APPSBL compatibility. */
typedef struct {
    uint32_t manufacturer, chip_id;
    uint32_t capacity_mib, erase_kib, page_bytes, raw_oob_bytes;
    char vendor[32], model[64];
    int have_manufacturer, have_chip_id, have_vendor, have_model, have_geometry;
} bi_nand_identity;

static char g_root[PATH_MAX] = "/";
static char g_tmpdir[PATH_MAX] = "";
static char g_status_file[PATH_MAX] = "";
static char g_backup_file[PATH_MAX] = "";
static char g_lock_file[PATH_MAX] = "";
static char g_token[65] = "";
static char g_session[33] = "";
static char g_phase[16] = "blocked";
static char g_stage[64] = "not_started";
static char g_message[256] = "安装核心尚未初始化";
static char g_blocked[256] = "尚未完成受限前置检查";
static char g_candidate_hash[65] = "";
static char g_target_name[96] = "";
static uint32_t g_target_off = 0;
static uint64_t g_progress = 0, g_total = 0;
static uint64_t g_backup_size = 0;
static char g_backup_hash[65] = "";
#define BI_BACKUP_DATA_BYTES (UINT64_C(45)*1024U*1024U)
#define BI_BACKUP_ZIP_LIMIT (BI_BACKUP_DATA_BYTES+128U*1024U)
static int g_modified = 0, g_can_reboot = 0, g_started = 0, g_token_used = 0;
static int g_backup_available = 0;
/* Preserve authoritative parent state when file publication fails. */
static int g_status_memory_only = 0, g_reboot_requested = 0;
static int g_bound_fingerprint_valid = 0;
static char g_bound_fingerprint[65] = "";
static char g_bound_payload_hash[65] = "";
static char g_active_hash[65] = "";
static bv_result g_version = { BV_REJECT_UNKNOWN, 0, 0 };
static const char *version_scope(void);
static char g_boot_id[80] = "";
static int g_boot_lock_seen = 0;
static int g_ids_ok = 0;
static pid_t g_child = -1;
static bi_check g_checks[48];
static size_t g_nchecks;
static char g_nand_profile_id[96];
static char g_nand_identity_hash[65];
static char g_bound_nand_identity_hash[65];
static int g_oem_verified, g_oem_records_verified, g_oem_bound, g_oem_post_valid;
static int g_oem_return_mode, g_oem_boot_policy_verified, g_install_ready;
#if !BI_TEST
static int g_first_install_qualified;
static const bfi_d_candidate_record *g_first_install_record;
static const bfi_install_profile *g_first_install_profile;
#endif
static uint8_t g_oem_bc_before[BC_BYTES];
static uint8_t g_oem_bc1_before[BC_BYTES];
static char g_oem_post_fingerprint[65];
static int validate_oem_post_state(void);
static uint32_t g_oem_offset, g_oem_selector, g_oem_index, g_oem_bridge_off;
static uint32_t g_oem_post_age, g_oem_post_appsbl;
static dev_t g_oem_dev;
static uint8_t g_oem_snapshot[32], g_oem_post_bc_sha[32], g_oem_post_bc1_sha[32];
static bos_pool_match g_oem_pool_match;
static int g_return_ready, g_common_bound;
static const br_return_source_record *g_return_source_record __attribute__((unused));
static uint32_t g_stock_baseline, g_return_rule;
static uint8_t g_bridge_snapshot[32], g_return_source_revision[32], g_return_evidence[32];
static char g_return_reason[256]="原厂退出合同尚未核验";
typedef enum { BI_OP_NONE=0, BI_OP_INSTALL=1, BI_OP_RETURN_OEM=2 } bi_operation;
static bi_operation g_bound_operation;
static char g_operation_ticket[65];
static char g_oem_reason[256]="原厂核验尚未完成";
static char g_root_binding_reason[192]="根绑定诊断尚未运行";
static char g_dispatch_code[65]="", g_dispatch_reason[257]="", g_dispatch_candidate_revision[65]="";
static char g_dispatch_execution_detail[2048]="";
static char g_install_rejected_check[65]="", g_install_rejected_reason[257]="";
static int g_inspection_mode;
static char g_running_mtd_binding[128];
static int oem_return_available(void);
static int verify_return_sbl(bi_part*);


static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put_le32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int safe_copy(char *out,size_t cap,const char *in){int n=snprintf(out,cap,"%s",in?in:"");return n>=0&&(size_t)n<cap?0:-1;}
static int path_join(char *out,size_t cap,const char *rel){int n=snprintf(out,cap,"%s/%s",g_root,rel);return n>=0&&(size_t)n<cap?0:-1;}
static int text_file(const char *rel,char *out,size_t cap){char p[PATH_MAX];int fd;ssize_t n;if(!cap||path_join(p,sizeof(p),rel))return -1;fd=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;n=read(fd,out,cap-1);if(n<0||n==(ssize_t)(cap-1)){close(fd);return -1;}out[n]=0;close(fd);return 0;}
static int parse_u32(const char*s,uint32_t*out){char*e;unsigned long v;errno=0;v=strtoul(s,&e,0);while(*e=='\n'||*e=='\r'||*e==' '||*e=='\t')e++;if(errno||e==s||*e||v>UINT32_MAX)return -1;*out=(uint32_t)v;return 0;}
static int set_probe_value(uint32_t *dst,int *have,uint32_t value){if(*have)return *dst==value?0:-1;*dst=value;*have=1;return 0;}
static int set_probe_string(char *dst,size_t cap,int *have,const char *value){if(*have)return !strcmp(dst,value)?0:-1;if(!value||!*value||strlen(value)>=cap)return -1;snprintf(dst,cap,"%s",value);*have=1;return 0;}
static int profile_list_contains(const char *list,const char *id){if(!list||!id||!*id)return 0;size_t need=strlen(id);for(const char*p=list;*p;){while(*p==';'||*p==','||*p=='|'||*p==' '||*p=='\t')p++;const char*start=p;while(*p&&*p!=';'&&*p!=','&&*p!='|'&&*p!=' '&&*p!='\t')p++;if((size_t)(p-start)==need&&!memcmp(start,id,need))return 1;}return 0;}
static int parse_nand_probe_log(const char *raw,bi_nand_identity *id){
    char *copy=strdup(raw);if(!copy)return -1;memset(id,0,sizeof(*id));
    for(char *line=copy;line&&*line;){char *next=strchr(line,'\n');if(next)*next++=0;line[strcspn(line,"\r")]=0;
        const char *p=strstr(line,"nand: device found, Manufacturer ID:");
        if(p){unsigned long long m=0,c=0;int used=0;if(sscanf(p,"nand: device found, Manufacturer ID: 0x%llx, Chip ID: 0x%llx%n",&m,&c,&used)!=2||!used||p[used]||m>255||c>255||set_probe_value(&id->manufacturer,&id->have_manufacturer,(uint32_t)m)||set_probe_value(&id->chip_id,&id->have_chip_id,(uint32_t)c)){free(copy);return -1;}}
        p=strstr(line,"nand: ");
        if(p&&strstr(p," SPI NAND ")){char vendor[32]={0},model[64]={0};if(sscanf(p,"nand: %31s %63s",vendor,model)!=2||set_probe_string(id->vendor,sizeof(id->vendor),&id->have_vendor,vendor)||set_probe_string(id->model,sizeof(id->model),&id->have_model,model)){free(copy);return -1;}}
        p=strstr(line,"nand: ");
        if(p&&strstr(p,"erase size:")&&strstr(p,"page size:")&&strstr(p,"OOB size:")){uint64_t cap=0,erase=0,page=0,oob=0;char cell[32]={0};int used=0;if(sscanf(p,"nand: %"SCNu64" MiB, %31[^,], erase size: %"SCNu64" KiB, page size: %"SCNu64", OOB size: %"SCNu64"%n",&cap,cell,&erase,&page,&oob,&used)!=5||!used||p[used]||strcmp(cell,"SLC")||cap==0||cap>4096||erase==0||erase>1048576||page==0||page>1048576||oob==0||oob>65536){free(copy);return -1;}if(id->have_geometry&&(id->capacity_mib!=cap||id->erase_kib!=erase||id->page_bytes!=page||id->raw_oob_bytes!=oob)){free(copy);return -1;}id->capacity_mib=(uint32_t)cap;id->erase_kib=(uint32_t)erase;id->page_bytes=(uint32_t)page;id->raw_oob_bytes=(uint32_t)oob;id->have_geometry=1;}
        line=next;
    }
    free(copy);return id->have_manufacturer&&id->have_chip_id&&id->have_vendor&&id->have_model&&id->have_geometry?0:-1;
}
static int read_nand_probe_identity(bi_nand_identity *id){
#if BI_TEST
    char log[16384];if(text_file("proc/nand-probe-log",log,sizeof(log)))return -1;return parse_nand_probe_log(log,id);
#else
#if defined(SYS_syslog)
    long cap=syscall(SYS_syslog,10,NULL,0);if(cap<=0||cap>1024*1024)return -1;char *log=malloc((size_t)cap+1);if(!log)return -1;long n=syscall(SYS_syslog,3,log,(int)cap);if(n<=0||n>cap){free(log);return -1;}log[n]=0;int rc=parse_nand_probe_log(log,id);free(log);return rc;
#else
    (void)id;return -1;
#endif
#endif
}
static int dt_compat_has(const uint8_t*b,size_t n,const char*wanted){size_t pos=0,z=strlen(wanted);while(pos<n){size_t end=pos;while(end<n&&b[end])end++;if(end-pos==z&&!memcmp(b+pos,wanted,z))return 1;if(end==n)break;pos=end+1;}return 0;}
#if !BI_TEST
static int attr(unsigned idx,const char*name,uint32_t*out){char rel[128],b[80];snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/%s",idx,name);return text_file(rel,b,sizeof(b))? -1:parse_u32(b,out);}
#endif
static void add_check(const char*n,const char*s,const char*d){
    /* Inspection has no web server. Publish only bounded check/status tokens
     * on its private stderr pipe; never log device data or grant readiness. */
    if(g_inspection_mode){fprintf(stderr,"BI_INSPECT_STAGE %.64s %.31s\n",n,s);fflush(stderr);}
    if(!strcmp(s,"blocked")){snprintf(g_blocked,sizeof(g_blocked),"%s",d);snprintf(g_install_rejected_check,sizeof(g_install_rejected_check),"%s",n);snprintf(g_install_rejected_reason,sizeof(g_install_rejected_reason),"%s",d);}if(g_nchecks>=sizeof(g_checks)/sizeof(g_checks[0]))return;bi_check*c=&g_checks[g_nchecks++];snprintf(c->name,sizeof(c->name),"%s",n);snprintf(c->status,sizeof(c->status),"%s",s);snprintf(c->detail,sizeof(c->detail),"%s",d);
}
static int json_string(char *out,size_t cap,size_t *used,const char *s){
    if(*used>=cap)return -1;
    out[(*used)++]='"';
    for(const unsigned char*p=(const unsigned char*)(s?s:"");*p;p++){
        if(*used+7>=cap)return -1;
        if(*p=='"'||*p=='\\'){out[(*used)++]='\\';out[(*used)++]=(char)*p;}
        else if(*p<0x20){int n=snprintf(out+*used,cap-*used,"\\u%04x",*p);if(n!=6)return -1;*used+=6;}
        else out[(*used)++]=(char)*p;
    }
    if(*used>=cap)return -1;
    out[(*used)++]='"';out[*used]=0;return 0;
}

static int random_bytes(uint8_t *p,size_t n){
#if BI_TEST
#if defined(__GLIBC__)
    size_t d=0;while(d<n){ssize_t r=getrandom(p+d,n-d,0);if(r<0&&errno==EINTR)continue;if(r<=0)break;d+=(size_t)r;}if(d==n)return 0;
    return -1;
#else
    struct timespec ts;if(clock_gettime(CLOCK_REALTIME,&ts))return -1;uint32_t x=(uint32_t)ts.tv_nsec^(uint32_t)ts.tv_sec^(uint32_t)getpid();for(size_t i=0;i<n;i++){x^=x<<13;x^=x>>17;x^=x<<5;p[i]=(uint8_t)x;}return 0;
#endif
#else
    size_t d=0;int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;while(d<n){ssize_t r=read(fd,p+d,n-d);if(r<0&&errno==EINTR)continue;if(r<=0){close(fd);return -1;}d+=(size_t)r;}close(fd);return 0;
#endif
}
static int make_ids(void){uint8_t b[48];static const char h[]="0123456789abcdef";size_t need=g_inspection_mode?16:48;if(random_bytes(b,need))return -1;for(size_t i=0;i<16;i++){g_session[2*i]=h[b[i]>>4];g_session[2*i+1]=h[b[i]&15];}g_session[32]=0;g_token[0]=0;if(!g_inspection_mode){for(size_t i=0;i<32;i++){g_token[2*i]=h[b[16+i]>>4];g_token[2*i+1]=h[b[16+i]&15];}g_token[64]=0;}return 0;}
static int load_boot_id(void){
#if BI_TEST
    if(text_file("proc/sys/kernel/random/boot_id",g_boot_id,sizeof(g_boot_id)))return -1;
#else
    if(text_file("proc/sys/kernel/random/boot_id",g_boot_id,sizeof(g_boot_id)))return -1;
#endif
    g_boot_id[strcspn(g_boot_id,"\r\n")]=0;return strlen(g_boot_id)>=16?0:-1;
}
static int boot_lock_exists(void){struct stat st;int fd=open(g_lock_file,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return errno==ENOENT?0:1;if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return 1;}close(fd);return 1;}
static int create_boot_lock(void){int fd=open(g_lock_file,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;size_t n=strlen(g_boot_id),d=0;while(d<n){ssize_t z=write(fd,g_boot_id+d,n-d);if(z<0&&errno==EINTR)continue;if(z<=0){close(fd);return -1;}d+=(size_t)z;}return close(fd);}
static int format_status(char *buf,size_t cap){
    size_t used=0;
#define APP(...) do { int z=snprintf(buf+used,cap-used,__VA_ARGS__); if(z<0||(size_t)z>=cap-used){return -1;}used+=(size_t)z;} while(0)
#define Q(v) do { if(json_string(buf,cap,&used,(v))){return -1;} } while(0)
    APP("{\"session\":");Q(g_session);APP(",\"token\":");Q(g_token);APP(",\"phase\":");Q(g_phase);
    APP(",\"install_ready\":%s,\"blocked_reason\":",(!strcmp(g_phase,"ready")&&g_install_ready)?"true":"false");Q(g_blocked);
    APP(",\"stage\":");Q(g_stage);APP(",\"progress_bytes\":%"PRIu64",\"total_bytes\":%"PRIu64",\"message\":",g_progress,g_total);Q(g_message);
    APP(",\"oem\":{\"slot_verified\":%s,\"boot_records_verified\":%s},\"actions\":{\"return_oem\":%s,\"reboot_oem\":%s}",g_oem_verified?"true":"false",g_oem_records_verified?"true":"false",oem_return_available()?"true":"false",(g_can_reboot&&g_oem_post_valid&&g_oem_boot_policy_verified)?"true":"false");
    APP(",\"oem_return\":{\"available\":%s,\"physical_offset\":%u,\"selector\":%u,\"source_sha256\":",oem_return_available()?"true":"false",g_oem_offset,g_oem_selector);Q(g_return_source_record?g_return_source_record->official_sha256:"");APP(",\"source_revision_sha256\":");Q(g_return_source_record?g_return_source_record->source_revision_sha256:"");APP(",\"product\":");
    {const char*product="unknown";if(g_return_source_record){switch(g_return_source_record->product){case BOS_PRODUCT_RC3000:product="RC3000";break;case BOS_PRODUCT_RT3000:product="RT3000";break;case BOS_PRODUCT_RW3000:product="RW3000";break;case BOS_PRODUCT_NX30:product="NX30";break;default:break;}}Q(product);}
    APP(",\"route\":");Q(g_return_source_record?g_return_source_record->route:"");APP(",\"reason\":");Q(g_return_ready?g_oem_reason:g_return_reason);APP("}");
    APP(",\"source\":");
    if(g_return_source_record){
        APP("{\"product\":");
        {const char*product="unknown";switch(g_return_source_record->product){case BOS_PRODUCT_RC3000:product="RC3000";break;case BOS_PRODUCT_RT3000:product="RT3000";break;case BOS_PRODUCT_RW3000:product="RW3000";break;case BOS_PRODUCT_NX30:product="NX30";break;default:break;}Q(product);}
        APP(",\"route\":");Q(g_return_source_record->route);
        APP(",\"sha256\":");Q(g_return_source_record->official_sha256);
        APP(",\"source_sha256\":");Q(g_return_source_record->official_sha256);
        APP(",\"source_revision_sha256\":");Q(g_return_source_record->source_revision_sha256);APP("}");
    }else APP("null");
    APP(",\"nand_profile\":");
    if(g_nand_profile_id[0]){APP("{\"id\":");Q(g_nand_profile_id);APP("}");}
    else APP("null");
    APP(",\"root_binding_contract\":{\"volume_bytes\":%"PRIu64",\"usable_leb_bytes\":%"PRIu64",\"reserved_ebs\":%"PRIu64",\"mtd_write_bytes\":%"PRIu64",\"contract_sha256\":",g_bridge_root_contract.volume_bytes,g_bridge_root_contract.usable_leb_bytes,g_bridge_root_contract.reserved_ebs,g_bridge_root_contract.mtd_write_bytes);Q(BI_ROOT_BINDING_CONTRACT_SHA256);APP("}");
    APP(",\"oem_post_fingerprint\":");Q(g_oem_post_fingerprint);
    APP(",\"dispatch\":{\"status\":");Q(g_dispatch_code[0]?"blocked":"normal");
    APP(",\"code\":");Q(g_dispatch_code);APP(",\"result_code\":");Q(g_dispatch_code);
    APP(",\"reason\":");Q(g_dispatch_reason);APP(",\"candidate_source_revision_sha256\":");Q(g_dispatch_candidate_revision);
    APP(",\"execution_detail\":");Q(g_dispatch_execution_detail);APP(",\"tool_revision\":\"R6-preflight-v1\"");
    APP("},\"dispatch_fallback\":{\"active\":%s,\"code\":",g_dispatch_code[0]?"true":"false");Q(g_dispatch_code);APP(",\"reason\":");Q(g_dispatch_reason);APP("}");
    APP(",\"operation\":\"%s\",\"boot_policy\":{\"verified\":%s,\"normal_boot\":\"%s\",\"maintenance_entry\":\"%s\",\"reset_verified\":%s,\"next_boot\":\"%s\"}",g_oem_return_mode?"return_oem":"install",g_oem_boot_policy_verified?"true":"false",g_oem_boot_policy_verified?"selected_system":"unknown",g_oem_boot_policy_verified&&!g_oem_return_mode?(BI_TEST?"reset":"uart"):"unknown",BI_TEST&&g_oem_boot_policy_verified?"true":"false",g_oem_boot_policy_verified&&g_oem_post_valid?"oem":"unknown");
    APP(",\"may_have_modified\":%s,\"can_reboot\":%s,\"candidate\":{\"version\":",g_modified?"true":"false",g_can_reboot?"true":"false");Q(BI_PAYLOAD_VERSION);
    APP(",\"sha256\":");Q(g_candidate_hash);APP(",\"bytes\":%u,\"ip\":",(unsigned)BI_STATUS_CANDIDATE_BYTES);Q(BI_IP);
    APP(",\"scope\":");Q(version_scope());APP(",\"sequence\":%"PRIu64,g_version.candidate_sequence);
    APP("},\"backup\":{\"available\":%s,\"complete\":%s,\"format\":\"zip\",\"size_bytes\":%"PRIu64",\"data_bytes\":%"PRIu64",\"sha256\":",g_backup_available?"true":"false",g_backup_available?"true":"false",g_backup_size,g_backup_available?BI_BACKUP_DATA_BYTES:UINT64_C(0));Q(g_backup_hash);APP(",\"url\":\"/backup.zip\"},\"target\":{\"name\":");Q(g_target_name);
    APP(",\"offset\":\"0x%06x\",\"size\":%u},\"checks\":[",g_target_off,(unsigned)APPSBL_BYTES);
    /* Target offset is kept as the physical offset string in target_name when ready. */
    if(g_nchecks==0) APP("]}\n");
    else {for(size_t i=0;i<g_nchecks;i++){APP("%s{\"name\":",i?",":"");Q(g_checks[i].name);APP(",\"status\":");Q(g_checks[i].status);APP(",\"detail\":");Q(g_checks[i].detail);APP("}");}APP("]}\n");}
#undef APP
#undef Q
    return (int)used;
}
static int write_status(void){
    char tmp[PATH_MAX],buf[STATUS_CAP];int fd,n;
    if(!g_status_file[0])return -1;
    n=snprintf(tmp,sizeof(tmp),"%s.new",g_status_file);if(n<0||(size_t)n>=sizeof(tmp))return -1;
    int size=format_status(buf,sizeof(buf));if(size<0)return -1;
    fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;
    size_t used=(size_t)size,done=0;while(done<used){ssize_t w=write(fd,buf+done,used-done);if(w<0&&errno==EINTR)continue;if(w<=0){close(fd);unlink(tmp);return -1;}done+=(size_t)w;}
    if(close(fd)||rename(tmp,g_status_file)){unlink(tmp);return -1;}return 0;
}

static int read_all(int fd,void *buf,size_t n,off_t off){size_t d=0;while(d<n){size_t z=n-d;if(z>PAGE_BYTES)z=PAGE_BYTES;ssize_t r;do{r=pread(fd,(uint8_t*)buf+d,z,off+(off_t)d);}while(r<0&&errno==EINTR);if(r!=(ssize_t)z)return -1;d+=z;}return 0;}
static int write_all(int fd,const void *buf,size_t n,off_t off){size_t d=0;while(d<n){size_t z=n-d;if(z>PAGE_BYTES)z=PAGE_BYTES;ssize_t r;do{r=pwrite(fd,(const uint8_t*)buf+d,z,off+(off_t)d);}while(r<0&&errno==EINTR);if(r!=(ssize_t)z)return -1;d+=z;}return 0;}
static void hash_hex(const void *p,size_t n,char out[65]){bi_sha256_ctx c;uint8_t d[32];bi_sha256_init(&c);bi_sha256_update(&c,p,n);bi_sha256_final(&c,d);bi_sha256_hex(d,out);}
static void hash_nand_identity(const bi_nand_identity *id,char out[65]){bi_sha256_ctx c;uint8_t d[32],meta[24];put_le32(meta,id->manufacturer);put_le32(meta+4,id->chip_id);put_le32(meta+8,id->capacity_mib);put_le32(meta+12,id->erase_kib);put_le32(meta+16,id->page_bytes);put_le32(meta+20,id->raw_oob_bytes);bi_sha256_init(&c);bi_sha256_update(&c,meta,sizeof(meta));bi_sha256_update(&c,id->vendor,strlen(id->vendor)+1);bi_sha256_update(&c,id->model,strlen(id->model)+1);bi_sha256_final(&c,d);bi_sha256_hex(d,out);}
static int bytes_are_ff(const uint8_t*p,size_t n){for(size_t i=0;i<n;i++)if(p[i]!=0xff)return 0;return 1;}

#if BI_TEST
static int fixture_attr(unsigned idx,const char*attr,uint32_t*out){char rel[128],b[80];snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/%s",idx,attr);if(text_file(rel,b,sizeof(b)))return -1;return parse_u32(b,out);}
static const char *fixture_fault(void){static char f[80];if(text_file("fault",f,sizeof(f)))return "";f[strcspn(f,"\r\n")]=0;return f;}
#endif

static int part_close(bi_part*p){if(p->fd<0)return 0;int f=p->fd;p->fd=-1;return close(f);}
static int part_open(unsigned idx,bi_part*p,int writable){
    memset(p,0,sizeof(*p));p->fd=-1;p->index=idx;
#if BI_TEST
    char rel[128],v[128],rooted[PATH_MAX];uint32_t maj,min;struct stat st;
    snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/name",idx);if(text_file(rel,v,sizeof(v)))return -1;v[strcspn(v,"\r\n")]=0;if(!v[0]||strlen(v)>=sizeof(p->name))return -1;strcpy(p->name,v);
    if(fixture_attr(idx,"offset",&p->off)||fixture_attr(idx,"size",&p->size)||fixture_attr(idx,"erasesize",&p->erase)||fixture_attr(idx,"writesize",&p->page)||fixture_attr(idx,"oobsize",&p->oob)||fixture_attr(idx,"ecc_strength",&p->ecc))return -1;
    snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/dev",idx);char extra;if(text_file(rel,v,sizeof(v)))return -1;v[strcspn(v,"\r\n")]=0;if(sscanf(v,"%u:%u%c",&maj,&min,&extra)!=2)return -1;
    snprintf(rel,sizeof(rel),"dev/mtd%u",idx);if(path_join(rooted,sizeof(rooted),rel))return -1;snprintf(p->path,sizeof(p->path),"%s",rooted);
    p->fd=open(p->path,(writable?O_RDWR:O_RDONLY)|O_CLOEXEC|O_NOFOLLOW);if(p->fd<0)return -1;
    if(fstat(p->fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)p->size){part_close(p);return -1;}p->dev=makedev(maj,min);
    (void)maj;(void)min;
#else
    char rel[128],v[128],devpath[PATH_MAX];uint32_t maj,min;struct stat lst,st;struct mtd_info_user info;
    snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/name",idx);if(text_file(rel,v,sizeof(v)))return -1;v[strcspn(v,"\r\n")]=0;if(!v[0]||strlen(v)>=sizeof(p->name))return -1;strcpy(p->name,v);
    if(attr(idx,"offset",&p->off)||attr(idx,"size",&p->size)||attr(idx,"erasesize",&p->erase)||attr(idx,"writesize",&p->page)||attr(idx,"oobsize",&p->oob)||attr(idx,"ecc_strength",&p->ecc))return -1;
    snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/dev",idx);char extra;if(text_file(rel,v,sizeof(v)))return -1;v[strcspn(v,"\r\n")]=0;if(sscanf(v,"%u:%u%c",&maj,&min,&extra)!=2)return -1;
    snprintf(devpath,sizeof(devpath),"/dev/mtd%u",idx);if(lstat(devpath,&lst)||!S_ISCHR(lst.st_mode)||major(lst.st_rdev)!=maj||minor(lst.st_rdev)!=min)return -1;
    p->fd=open(devpath,(writable?O_RDWR:O_RDONLY)|O_CLOEXEC|O_NOFOLLOW);if(p->fd<0)return -1;
    if(fstat(p->fd,&st)||!S_ISCHR(st.st_mode)||st.st_rdev!=lst.st_rdev||ioctl(p->fd,MEMGETINFO,&info)||info.type!=MTD_NANDFLASH||info.size!=p->size||info.erasesize!=p->erase||info.writesize!=p->page||info.oobsize!=p->oob||(writable&&!(info.flags&MTD_WRITEABLE))){part_close(p);return -1;}p->dev=st.st_rdev;snprintf(p->path,sizeof(p->path),"%s",devpath);
#endif
    return 0;
}
static int part_reopen_write(bi_part*p){
    unsigned idx=p->index;dev_t dev=p->dev;uint32_t off=p->off,size=p->size,erase=p->erase,page=p->page,oob=p->oob,ecc=p->ecc;char name[sizeof(p->name)];uint8_t*snapshot=p->snapshot;memcpy(name,p->name,sizeof(name));
    if(part_close(p))return -1;
    bi_part rw;
    if(part_open(idx,&rw,1))return -1;
    int same=rw.dev==dev&&rw.off==off&&rw.size==size&&rw.erase==erase&&rw.page==page&&rw.oob==oob&&rw.ecc==ecc&&!strcmp(rw.name,name);
    if(!same){part_close(&rw);errno=ESTALE;return -1;}rw.snapshot=snapshot;*p=rw;return 0;
}

static int badblock(bi_part*p,uint32_t rel){
#if BI_TEST
    (void)p;(void)rel;return strstr(fixture_fault(),"badblock")?1:0;
#else
    loff_t o=(loff_t)rel;int r=ioctl(p->fd,MEMGETBADBLOCK,&o);return r==0?0:1;
#endif
}
static int target_has_badblock(bi_part*p){for(uint32_t o=0;o<p->size;o+=ERASE_BYTES)if(badblock(p,o))return 1;return 0;}
/* Qualification needs to distinguish a known bad eraseblock from an ioctl
 * failure. Both reject the slot, but only the former is evidence about media. */
static int __attribute__((unused)) qualification_badblock_scan(bi_part*p,uint32_t*where,int*saved_errno){
#if BI_TEST
    (void)saved_errno;
#endif
    for(uint32_t o=0;o<p->size;o+=ERASE_BYTES){
#if BI_TEST
        if(strstr(fixture_fault(),"badblock")){if(where)*where=o;return 1;}
#else
        loff_t pos=(loff_t)o;int r=ioctl(p->fd,MEMGETBADBLOCK,&pos);
        if(r<0){if(where)*where=o;if(saved_errno)*saved_errno=errno;return -1;}
        if(r>0){if(where)*where=o;return 1;}
#endif
    }
    return 0;
}
static int erase_part(bi_part*p){
#if BI_TEST
    if(!strcmp(fixture_fault(),"erase")){errno=EIO;return -1;}uint8_t ff[PAGE_BYTES];memset(ff,0xff,sizeof(ff));for(uint32_t o=0;o<p->size;o+=sizeof(ff))if(write_all(p->fd,ff,sizeof(ff),o))return -1;return 0;
#else
    struct erase_info_user e={.start=0,.length=p->size};return ioctl(p->fd,MEMERASE,&e);
#endif
}

static int discover_all(bi_part *parts,size_t *count){
    char mtdbuf[8192];size_t n=0;
    *count=0;
    if(text_file("proc/mtd",mtdbuf,sizeof(mtdbuf)))return -1;
    char *line=strchr(mtdbuf,'\n');if(!line)return -1;line++;
    while(*line){
        char *end=strchr(line,'\n');if(end)*end=0;unsigned idx;unsigned sz,er;char nm[96]={0};int consumed=0;
        if(sscanf(line," mtd%u: %x %x \"%95[^\"]\"%n",&idx,&sz,&er,nm,&consumed)==4&&consumed>0){
            for(const char*t=line+consumed;*t;t++)if(*t!=' '&&*t!='\t'&&*t!='\r')return -1;
            char rel[128],type[48];uint32_t off=UINT32_MAX;snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/type",idx);
            if(text_file(rel,type,sizeof(type)))return -1;
            type[strcspn(type,"\r\n")]=0;
            snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/offset",idx);
            char offbuf[64];if(text_file(rel,offbuf,sizeof(offbuf))==0)(void)parse_u32(offbuf,&off);
            int named_target=!strcmp(nm,"0:SBL1")||!strcmp(nm,"SBL1")||!strcmp(nm,"0:APPSBLENV")||!strcmp(nm,"APPSBLENV")||!strcmp(nm,"0:ART")||!strcmp(nm,"ART")||!strcmp(nm,"0:BOOTCONFIG")||!strcmp(nm,"BOOTCONFIG")||!strcmp(nm,"0:BOOTCONFIG1")||!strcmp(nm,"BOOTCONFIG1")||!strcmp(nm,"0:APPSBL")||!strcmp(nm,"APPSBL")||!strcmp(nm,"0:APPSBL_1")||!strcmp(nm,"APPSBL_1");
            int target_off=off==0||off==ENV_OFF||off==ART_OFF||off==BC0_OFF||off==BC1_OFF||off==APPSBL_LO||off==APPSBL_HI;
            if(!strcmp(type,"ubi")){if(named_target||target_off)return -1;}
            else if(strcmp(type,"nand"))return -1;
            else {if(n>=MAX_PARTS)return -1;bi_part*p=&parts[n];if(part_open(idx,p,0))return -1;if(p->size!=sz||p->erase!=er||(strcmp(p->name,nm)&&(p->off<STARTUP_END||p->off==0x900000u||p->off==0x3100000u||!strcmp(p->name,"rootfs")||!strcmp(p->name,"rootfs_1")||!strcmp(nm,"rootfs")||!strcmp(nm,"rootfs_1")))){part_close(p);return -1;}for(size_t k=0;k<n;k++)if(parts[k].dev==p->dev){part_close(p);return -1;}n++;*count=n;}
        }else return -1;
        if(!end)break;
        line=end+1;
    }
    *count=n;return n?0:-1;
}
static int profile_geometry(bi_part*ps,size_t n,const struct h3c_nand_catalog_profile *profile,uint32_t ecc_bits,uint32_t mtd_oob,bi_part **sbl,bi_part **env,bi_part **art,bi_part **bc0,bi_part **bc1,bi_part **appslo,bi_part **appshi){
    unsigned sbln=0,envn=0,artn=0,bc0n=0,bc1n=0,al=0,ah=0;
    if(!profile)return -1;
    unsigned oob_field=mtd_oob==64u?1u:mtd_oob==128u?2u:mtd_oob==256u?4u:0u;
    if(!oob_field||!(profile->kernel_oem_oob_field_mask&oob_field)||
       !(profile->kernel_ecc_mode_mask&h3c_nand_catalog_mode_bit(ecc_bits)))return -1;
    for(size_t i=0;i<n;i++){
        bi_part*p=&ps[i];
        if((uint64_t)p->off+p->size>profile->capacity_bytes)return -1;
        if(p->off>=STARTUP_END)continue;
        if(p->off>STARTUP_END||p->size>STARTUP_END-p->off)return -1;
        if(p->page!=profile->page_bytes||p->erase!=profile->erase_bytes||p->oob!=mtd_oob||p->ecc!=ecc_bits||p->size==0||p->size%profile->page_bytes)return -1;
        for(size_t j=i+1;j<n;j++){bi_part*q=&ps[j];if(q->off>=STARTUP_END)continue;if(q->off>STARTUP_END||q->size>STARTUP_END-q->off)return -1;if(p->off<q->off+q->size&&q->off<p->off+p->size)return -1;}
        if(p->off==0&&p->size==0x80000&&(!strcmp(p->name,"0:SBL1")||!strcmp(p->name,"SBL1"))){*sbl=p;sbln++;}
        if(p->off==ENV_OFF&&p->size==0x80000&&(!strcmp(p->name,"0:APPSBLENV")||!strcmp(p->name,"APPSBLENV"))){*env=p;envn++;}
        if(p->off==ART_OFF&&p->size==ART_BYTES&&(!strcmp(p->name,"0:ART")||!strcmp(p->name,"ART"))){*art=p;artn++;}
        if(p->off==BC0_OFF&&p->size==BC_BYTES&&(!strcmp(p->name,"0:BOOTCONFIG")||!strcmp(p->name,"BOOTCONFIG"))){*bc0=p;bc0n++;}
        if(p->off==BC1_OFF&&p->size==BC_BYTES&&(!strcmp(p->name,"0:BOOTCONFIG1")||!strcmp(p->name,"BOOTCONFIG1"))){*bc1=p;bc1n++;}
        if(p->size==APPSBL_BYTES&&(!strcmp(p->name,"0:APPSBL")||!strcmp(p->name,"APPSBL")||!strcmp(p->name,"0:APPSBL_1")||!strcmp(p->name,"APPSBL_1"))){
            if(p->off==APPSBL_LO){*appslo=p;al++;}
            if(p->off==APPSBL_HI){*appshi=p;ah++;}
        }
    }
    return sbln==1&&envn==1&&artn==1&&bc0n==1&&bc1n==1&&al==1&&ah==1?0:-1;
}

static const struct h3c_nand_catalog_profile *match_linux_oem_probe(const bi_nand_identity *id,
                                                                 unsigned ecc,unsigned mtd_oob){
    const struct h3c_nand_catalog_profile *found=NULL;
    unsigned mode=h3c_nand_catalog_mode_bit(ecc);
    unsigned oob_field=mtd_oob==64u?1u:mtd_oob==128u?2u:mtd_oob==256u?4u:0u;
    if(!id||!mode||!oob_field||id->manufacturer>255||id->chip_id>255)return NULL;
    for(size_t i=0;i<H3C_NAND_CATALOG_PROFILE_COUNT;i++){
        const struct h3c_nand_catalog_profile *p=&h3c_nand_catalog_profiles[i];
        if(p->id[0]!=id->manufacturer||p->id[1]!=id->chip_id||strcmp(p->oem_model,id->model)||
           p->capacity_bytes!=(uint64_t)id->capacity_mib*1024u*1024u||
           p->page_bytes!=id->page_bytes||p->erase_bytes!=(uint64_t)id->erase_kib*1024u||
           p->appsbl_raw_spare_bytes!=id->raw_oob_bytes||!(p->kernel_ecc_mode_mask&mode)||
           !(p->kernel_oem_oob_field_mask&oob_field))continue;
        if(found)return NULL;
        found=p;
    }
    return found;
}

static const struct h3c_nand_catalog_profile *match_nand_profile(const bi_part *parts,size_t n,const bi_nand_identity *id,uint32_t *ecc_bits,uint32_t *mtd_oob){
    const bi_part *first=NULL;for(size_t i=0;i<n;i++)if(parts[i].off<STARTUP_END){first=&parts[i];break;}if(!first||!id||!ecc_bits||!mtd_oob)return NULL;
    if(first->ecc!=4&&first->ecc!=8)return NULL;
    uint64_t capacity=(uint64_t)id->capacity_mib*1024ULL*1024ULL;if(capacity>UINT32_MAX)return NULL;
    const struct h3c_nand_catalog_profile *p=match_linux_oem_probe(id,first->ecc,first->oob);
    if(!p)return NULL;
    /* Read identification and bounded candidate geometry only. The catalog
     * supplies no live PBL/APPSBL storage or installation qualification. */
    if(!h3c_nand_catalog_runtime_read_qualified(p,first->ecc)||
       !p->page_bytes||!p->erase_bytes)return NULL;
    if(!profile_list_contains(BI_SUPPORTED_NAND_PROFILE_IDS,p->profile_id)||!profile_list_contains(BI_APPSBL_LOAD_PROFILE_IDS,p->profile_id)||!profile_list_contains(BI_APPSBL_STORAGE_PROFILE_IDS,p->profile_id))return NULL;
#if BI_HAVE_INSTALL_PROFILE
#if defined(BI_BOUND_NAND_PROFILE_ID)
    if(strcmp(p->profile_id,BI_BOUND_NAND_PROFILE_ID)||
       p->id[0]!=BI_BOUND_NAND_MANUFACTURER_ID||p->id[1]!=BI_BOUND_NAND_CHIP_ID||
       p->capacity_bytes!=(uint64_t)BI_BOUND_NAND_CAPACITY_BYTES||
       p->appsbl_raw_spare_bytes!=BI_BOUND_NAND_RAW_SPARE_BYTES||
       p->page_bytes!=BI_BOUND_NAND_PAGE_BYTES||p->erase_bytes!=BI_BOUND_NAND_ERASE_BYTES||
       first->ecc!=BI_BOUND_NAND_ECC_BITS||
       !bfi_install_profile_accepts_mtd_oob(&bootbridge_first_install_profiles[0],first->oob))return NULL;
#else
    if(first->ecc!=bootbridge_first_install_profiles[0].linux_ecc||
       !bfi_install_profile_accepts_mtd_oob(&bootbridge_first_install_profiles[0],first->oob))return NULL;
#endif
    if(BI_APPSBL_LOAD_ECC_BITS!=bootbridge_first_install_profiles[0].load_ecc_bits||
       BI_APPSBL_STORAGE_ECC_BITS!=bootbridge_first_install_profiles[0].storage_ecc_bits)return NULL;
#else
    if(first->ecc!=4||BI_APPSBL_LOAD_ECC_BITS!=4||BI_APPSBL_STORAGE_ECC_BITS!=4)return NULL;
#endif
    *ecc_bits=first->ecc;*mtd_oob=first->oob;snprintf(g_nand_profile_id,sizeof(g_nand_profile_id),"%s",p->profile_id);return p;
}

static const struct h3c_nand_catalog_profile *match_runtime_profile(const bi_part*parts,size_t n,const bi_nand_identity*id,uint32_t*ecc,uint32_t*oob){
    const bi_part*first=NULL;for(size_t i=0;i<n;i++)if(parts[i].off<STARTUP_END){first=&parts[i];break;}
    if(!first||!id||(first->ecc!=4&&first->ecc!=8))return NULL;
    uint64_t capacity=(uint64_t)id->capacity_mib*1024ULL*1024ULL;if(capacity>UINT32_MAX)return NULL;
    const struct h3c_nand_catalog_profile*p=match_linux_oem_probe(id,first->ecc,first->oob);
    if(!p||!h3c_nand_catalog_runtime_read_qualified(p,first->ecc)||!p->page_bytes||!p->erase_bytes)return NULL;
    *ecc=first->ecc;*oob=first->oob;snprintf(g_nand_profile_id,sizeof(g_nand_profile_id),"%s",p->profile_id);return p;
}

/* One compiled empirical record is collected and checked below. Catalog
 * membership and the observer alone never grant installation. */
static int install_chain_qualified(void){
#if BI_TEST
    return 1; /* synthetic fixture admission, never production qualification */
#else
    return BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE==1&&g_first_install_qualified&&
        g_first_install_record!=NULL;
#endif
}
static int oem_boot_contract(const char *hash,int installed){
    if(!installed)return g_stock_baseline==1&&!strcmp(hash,g_active_hash);
#if BI_TEST && defined(BRIDGE_OEM_COMPILED_TEST_MANIFEST)
    return !strcmp(hash,installed?TEST_BOOT_CANDIDATE_SHA256:TEST_BOOT_OEM_SHA256);
#else
#if BI_TEST
    (void)hash;(void)installed;return 0;
#else
#if BI_HAVE_INSTALL_PROFILE
    const bfi_install_profile *p=g_first_install_profile;
    if(!p)return 0;
    return g_first_install_qualified&&g_first_install_record&&
        !strcmp(BI_FIRST_INSTALL_RECORD,g_first_install_record->name)&&
        !strcmp(hash,g_first_install_record->partition_sha256)&&
        g_oem_selector==p->oem_selector&&g_oem_offset==p->oem_offset&&
        g_oem_bridge_off==p->bridge_offset;
#else
    return g_first_install_qualified&&g_first_install_record&&
        !strcmp(BI_FIRST_INSTALL_RECORD,g_first_install_record->name)&&
        !strcmp(hash,g_first_install_record->partition_sha256)&&
        g_oem_selector==1&&g_oem_offset==0x3100000u;
#endif
#endif
#endif
}
static int sha_bytes(const char *text,uint8_t out[32]){
    if(!text||strlen(text)!=64)return -1;
    for(unsigned i=0;i<32;i++){unsigned a=(unsigned char)text[2*i],b=(unsigned char)text[2*i+1];
        if(a>='0'&&a<='9')a-='0';else if(a>='a'&&a<='f')a=a-'a'+10;else return -1;
        if(b>='0'&&b<='9')b-='0';else if(b>='a'&&b<='f')b=b-'a'+10;else return -1;
        out[i]=(uint8_t)((a<<4)|b);
    }return 0;
}
static int oem_read(void *context,uint64_t off,void *data,size_t bytes){
    bi_part*p=context;if(off>p->size||bytes>p->size-off)return -1;
    /* read_all issues pread calls in PAGE_BYTES (2 KiB) units. Keep BOS's
     * 128 KiB eraseblock request pagewise on Linux 4.4 MTD character devices. */
    return read_all(p->fd,data,bytes,(off_t)off);
}
static int part_identity_equal(const bi_part*a,const bi_part*b){return a->index==b->index&&a->dev==b->dev&&a->off==b->off&&a->size==b->size&&a->erase==b->erase&&a->page==b->page&&a->oob==b->oob&&a->ecc==b->ecc&&!strcmp(a->name,b->name);}
static const char*bos_code_name(bos_code c){
    switch(c){case BOS_OK:return "ok";case BOS_ARGUMENT:return "argument";case BOS_UNTRUSTED_BINDING:return "untrusted-binding";
    case BOS_UNSUPPORTED:return "unsupported-format-or-geometry";case BOS_IO:return "read-or-allocation";case BOS_EC:return "erase-counter-header";
    case BOS_VID:return "volume-identifier-header";case BOS_TABLE:return "volume-table";case BOS_VOLUME:return "volume-layout";
    case BOS_STATIC_CRC:return "static-volume-crc";case BOS_DIGEST:return "immutable-volume-digest";case BOS_CHANGED:return "snapshot-changed-during-measurement";
    case BOS_NO_MATCH:return "no-compiled-pool-match";case BOS_AMBIGUOUS:return "ambiguous-pool-match";case BOS_POLICY_UNKNOWN:return "table-policy-unknown";
    case BOS_LIMIT:return "pool-alias-limit";default:return "unknown-code";}
}
/* Return helpers and V2 install providers use exact source-bound component
 * records. Legacy combined builds retain their frozen baseline behavior. */
static uint32_t lookup_stock_baseline(const char*hash){
#if (defined(BRIDGE_RETURN_ONLY) || defined(BI_RETURN_SOURCE_REGISTRY_V2)) && BI_HAVE_RETURN_SOURCE_REGISTRY
    for(size_t i=0;i<BI_RETURN_SOURCE_COUNT;i++)if(!strcmp(hash,BI_RETURN_SOURCES[i].appsbl_sha256))return 1u;
    return 0u;
#elif BI_TEST && defined(BRIDGE_OEM_COMPILED_TEST_MANIFEST)
    return !strcmp(hash,TEST_BOOT_OEM_SHA256)?1u:0u;
#else
    return !strcmp(hash,"7f1f7bf0938aae5d6c5816d6816524d5877eefb41e9fbdf4427b24bf34fb2b48")?1u:0u;
#endif
}
static int parse_u64_value(const char*s,uint64_t*out){char*e;unsigned long long v;if(!s||!out)return -1;errno=0;v=strtoull(s,&e,0);if(errno||e==s)return -1;while(*e=='\n'||*e=='\r'||*e==' '||*e=='\t')e++;if(*e)return -1;*out=(uint64_t)v;return 0;}
static int read_attr_text(unsigned idx,const char*name,char*out,size_t cap){char rel[160];if(snprintf(rel,sizeof(rel),"sys/class/mtd/mtd%u/%s",idx,name)>=(int)sizeof(rel))return -1;if(text_file(rel,out,cap))return -1;out[strcspn(out,"\r\n")]=0;return 0;}
static int read_uevent_key(const char*text,const char*key,char*out,size_t cap){char*copy=strdup(text);if(!copy)return -1;size_t found=0;char*save=NULL;for(char*l=strtok_r(copy,"\n",&save);l;l=strtok_r(NULL,"\n",&save)){size_t n=strlen(key);if(!strncmp(l,key,n)&&l[n]=='='){if(++found!=1||safe_copy(out,cap,l+n+1)){free(copy);return -1;}}}free(copy);return found==1?0:-1;}
static int root_binding_fail(const char*reason){safe_copy(g_root_binding_reason,sizeof(g_root_binding_reason),reason);return -1;}
static int scan_gluebi_mtd(unsigned expected_idx,br_gluebi_evidence*e,char*mtd_name,size_t mtd_name_cap,char*mtd_type,size_t mtd_type_cap){
    char dirpath[PATH_MAX];if(path_join(dirpath,sizeof(dirpath),"sys/class/mtd"))return root_binding_fail("GlueBI MTD scan: sysfs path overflow");DIR*d=opendir(dirpath);if(!d)return root_binding_fail("GlueBI MTD scan: /sys/class/mtd unavailable");struct dirent*de;unsigned named=0;int rc=0;
    while((de=readdir(d))){unsigned idx;char extra,name[128];if(sscanf(de->d_name,"mtd%u%c",&idx,&extra)!=1)continue;if(read_attr_text(idx,"name",name,sizeof(name))){rc=-1;break;}if(strcmp(name,"ubi_rootfs"))continue;named++;if(idx!=expected_idx){rc=-1;break;}char type[64],size[64],erase[64],write[64],dev[64],*colon;uint64_t v;
        if(read_attr_text(idx,"type",type,sizeof(type))||read_attr_text(idx,"size",size,sizeof(size))||read_attr_text(idx,"erasesize",erase,sizeof(erase))||read_attr_text(idx,"writesize",write,sizeof(write))||read_attr_text(idx,"dev",dev,sizeof(dev))){rc=-1;break;}
        if(safe_copy(mtd_name,mtd_name_cap,name)||safe_copy(mtd_type,mtd_type_cap,type)){rc=-1;break;}e->mtd_name=mtd_name;e->mtd_type=mtd_type;
        if(parse_u64_value(size,&e->mtd_size)||parse_u64_value(erase,&e->mtd_erase)||parse_u64_value(write,&e->mtd_write)){rc=-1;break;}
        colon=strchr(dev,':');if(!colon){rc=-1;break;}*colon++=0;if(parse_u64_value(dev,&v)||v>UINT32_MAX){rc=-1;break;}e->mtd_major=(uint32_t)v;if(parse_u64_value(colon,&v)||v>UINT32_MAX){rc=-1;break;}e->mtd_minor=(uint32_t)v;
    }
    if(closedir(d))rc=-1;
    e->mtd_name_matches=named;
    if(named!=1||rc)return root_binding_fail("GlueBI MTD scan: name is missing/duplicated, wrong index, or sysfs attributes unreadable");
    char proc[8192];if(text_file("proc/mtd",proc,sizeof(proc)))return root_binding_fail("GlueBI MTD scan: /proc/mtd unavailable");unsigned rows=0;char*save=NULL;
    for(char*l=strtok_r(proc,"\n",&save);l;l=strtok_r(NULL,"\n",&save)){unsigned idx;unsigned long long size,erase;char got[128];if(sscanf(l,"mtd%u: %llx %llx \"%127[^\"]\"",&idx,&size,&erase,got)!=4)continue;if(strcmp(got,"ubi_rootfs"))continue;rows++;if(idx!=expected_idx||size!=e->mtd_size||erase!=e->mtd_erase)return root_binding_fail("GlueBI MTD scan: /proc/mtd row disagrees with sysfs identity/geometry");}
    e->proc_mtd_name_matches=rows;return rows==1?0:root_binding_fail("GlueBI MTD scan: /proc/mtd does not map one unique ubi_rootfs row");
}
static int scan_attached_ubi(uint32_t expected_mtd,br_gluebi_evidence*e,char*volume_type,size_t volume_type_cap){
    char dirpath[PATH_MAX];if(path_join(dirpath,sizeof(dirpath),"sys/class/ubi"))return root_binding_fail("UBI scan: sysfs path overflow");DIR*d=opendir(dirpath);if(!d)return root_binding_fail("UBI scan: /sys/class/ubi unavailable");struct dirent*de;unsigned count=0,ubi_id=UINT32_MAX,mtd_num=UINT32_MAX;int rc=0;
    while((de=readdir(d))){unsigned n;char extra;if(sscanf(de->d_name,"ubi%u%c",&n,&extra)!=1)continue;char exact[32];snprintf(exact,sizeof(exact),"ubi%u",n);if(strcmp(exact,de->d_name))continue;count++;if(count!=1){rc=-1;break;}char rel[128],value[64];snprintf(rel,sizeof(rel),"sys/class/ubi/%s/mtd_num",exact);if(text_file(rel,value,sizeof(value))||parse_u32(value,&mtd_num)){rc=-1;break;}ubi_id=n;}
    if(closedir(d))rc=-1;
    if(rc||count!=1||ubi_id!=0||mtd_num!=expected_mtd)return root_binding_fail("UBI scan: attached set must be only ubi0 mapped to the discovered root MTD");
    e->attached_ubi_count=count;e->ubi_id=ubi_id;e->ubi_mtd_index=mtd_num;
    char ubi_dir[PATH_MAX];if(path_join(ubi_dir,sizeof(ubi_dir),"sys/class/ubi"))return root_binding_fail("UBI volume scan: sysfs path overflow");d=opendir(ubi_dir);if(!d)return root_binding_fail("UBI volume scan: /sys/class/ubi unavailable");unsigned names=0;unsigned vol_id=UINT32_MAX;uint64_t data=0,reserved=0,usable=0;uint32_t alignment=UINT32_MAX,corrupted=UINT32_MAX,upd=UINT32_MAX;
    while((de=readdir(d))){unsigned u,v;char extra;if(sscanf(de->d_name,"ubi%u_%u%c",&u,&v,&extra)!=2)continue;char exact[40];snprintf(exact,sizeof(exact),"ubi%u_%u",u,v);if(strcmp(exact,de->d_name))continue;char rel[128],value[128];snprintf(rel,sizeof(rel),"sys/class/ubi/%s/name",exact);if(text_file(rel,value,sizeof(value))) {rc=-1;break;}value[strcspn(value,"\r\n")]=0;if(strcmp(value,"ubi_rootfs"))continue;names++;if(names!=1||u!=ubi_id){rc=-1;break;}vol_id=v;snprintf(rel,sizeof(rel),"sys/class/ubi/%s/type",exact);if(text_file(rel,volume_type,volume_type_cap)){rc=-1;break;}volume_type[strcspn(volume_type,"\r\n")]=0;
        snprintf(rel,sizeof(rel),"sys/class/ubi/%s/data_bytes",exact);if(text_file(rel,value,sizeof(value))||parse_u64_value(value,&data)){rc=-1;break;}
        snprintf(rel,sizeof(rel),"sys/class/ubi/%s/reserved_ebs",exact);if(text_file(rel,value,sizeof(value))||parse_u64_value(value,&reserved)){rc=-1;break;}
        snprintf(rel,sizeof(rel),"sys/class/ubi/%s/usable_eb_size",exact);if(text_file(rel,value,sizeof(value))||parse_u64_value(value,&usable)){rc=-1;break;}
        snprintf(rel,sizeof(rel),"sys/class/ubi/%s/alignment",exact);if(text_file(rel,value,sizeof(value))||parse_u32(value,&alignment)){rc=-1;break;}
        snprintf(rel,sizeof(rel),"sys/class/ubi/%s/corrupted",exact);if(text_file(rel,value,sizeof(value))||parse_u32(value,&corrupted)){rc=-1;break;}
        snprintf(rel,sizeof(rel),"sys/class/ubi/%s/upd_marker",exact);if(text_file(rel,value,sizeof(value))||parse_u32(value,&upd)){rc=-1;break;}
    }
    if(closedir(d))rc=-1;
    if(rc||names!=1)return root_binding_fail("UBI volume scan: rootfs volume name is missing/duplicated, on another UBI, or its attributes are unreadable");
    e->root_volume_name_matches=names;e->volume_ubi_id=ubi_id;e->volume_id=vol_id;e->volume_name="ubi_rootfs";e->volume_type=volume_type;e->volume_data_bytes=data;e->volume_reserved_ebs=reserved;e->usable_leb_size=usable;e->volume_alignment=alignment;e->volume_corrupted=corrupted;e->volume_update_marker=upd;return 0;
}
static int verify_gluebi_root(const char*mount_source,const char*mount_target,const char*mount_type,const char*mount_options,const char*cmdline,uint32_t attached_index){
    br_gluebi_evidence e;memset(&e,0,sizeof(e));char mtd_name[64],mtd_type[32],volume_type[32];struct stat st;
    if(stat("/",&st))return root_binding_fail("GlueBI root: stat(/) failed");
    e.root_major=(uint32_t)major(st.st_dev);e.root_minor=(uint32_t)minor(st.st_dev);
    char rel[160],event[512],key[128],block_path[PATH_MAX],resolved[PATH_MAX];
    if(snprintf(rel,sizeof(rel),"sys/dev/block/%u:%u/uevent",e.root_major,e.root_minor)>=(int)sizeof(rel)||text_file(rel,event,sizeof(event)))return root_binding_fail("GlueBI root: sys/dev/block/<root dev_t>/uevent unavailable");
    if(read_uevent_key(event,"MAJOR",key,sizeof(key))||parse_u32(key,&e.event_major)||read_uevent_key(event,"MINOR",key,sizeof(key))||parse_u32(key,&e.event_minor))return root_binding_fail("GlueBI root: uevent MAJOR/MINOR missing or malformed");
    if(read_uevent_key(event,"DEVNAME",key,sizeof(key)))return root_binding_fail("GlueBI root: uevent DEVNAME missing or duplicated");
    e.block_devname=strdup(key);if(!e.block_devname)return root_binding_fail("GlueBI root: out of memory reading DEVNAME");
    if(read_uevent_key(event,"DEVTYPE",key,sizeof(key))){free((void*)e.block_devname);return root_binding_fail("GlueBI root: uevent DEVTYPE missing or duplicated");}
    e.block_devtype=strdup(key);if(!e.block_devtype){free((void*)e.block_devname);return root_binding_fail("GlueBI root: out of memory reading DEVTYPE");}
    unsigned mtd_idx;char trailing;
    if(sscanf(e.block_devname,"mtdblock%u%c",&mtd_idx,&trailing)!=1){free((void*)e.block_devname);free((void*)e.block_devtype);return root_binding_fail("GlueBI root: DEVNAME is not an exact mtdblockN identity");}
    e.block_mtd_index=mtd_idx;
    if(snprintf(rel,sizeof(rel),"sys/dev/block/%u:%u",e.root_major,e.root_minor)>=(int)sizeof(rel)||path_join(block_path,sizeof(block_path),rel)||!realpath(block_path,resolved)){free((void*)e.block_devname);free((void*)e.block_devtype);return root_binding_fail("GlueBI root: sys/dev/block symlink cannot be resolved");}
    const char*sys_prefix=strstr(resolved,"/sys/devices/");
    if(!sys_prefix){free((void*)e.block_devname);free((void*)e.block_devtype);return root_binding_fail("GlueBI root: resolved block path is outside /sys/devices");}
    e.block_sysfs_path=sys_prefix;
    if(scan_gluebi_mtd(mtd_idx,&e,mtd_name,sizeof(mtd_name),mtd_type,sizeof(mtd_type))){free((void*)e.block_devname);free((void*)e.block_devtype);return -1;}
    if(scan_attached_ubi(attached_index,&e,volume_type,sizeof(volume_type))){free((void*)e.block_devname);free((void*)e.block_devtype);return -1;}
    e.mount_source=mount_source;e.mount_target=mount_target;e.mount_type=mount_type;e.mount_options=mount_options;e.cmdline=cmdline;
    int ok=br_validate_gluebi_root(&e,&g_bridge_root_contract,&attached_index);
    if(ok)root_binding_fail(br_gluebi_result_string((br_gluebi_result)ok));
    else safe_copy(g_root_binding_reason,sizeof(g_root_binding_reason),"GlueBI root: observed path contract PASS");
    free((void*)e.block_devname);free((void*)e.block_devtype);return ok;
}
static int running_bridge_index(unsigned *index){
    char value[128],cmd[2048],cmd_parse[2048],mounts[8192],name[128],root_source[128]="",root_target[128]="",root_type[64]="",root_options[256]="";uint32_t idx;unsigned roots=0;int source_ok=0,gluebi=0;
    safe_copy(g_root_binding_reason,sizeof(g_root_binding_reason),"root binding not evaluated");
    if(text_file("sys/class/ubi/ubi0/mtd_num",value,sizeof(value))||parse_u32(value,&idx)||idx>=MAX_PARTS||text_file("sys/class/ubi/ubi0_3/name",name,sizeof(name)))return root_binding_fail("Bridge root discovery: ubi0 mtd_num or ubi0_3/name unavailable/invalid");
    name[strcspn(name,"\r\n")]=0;if(strcmp(name,"ubi_rootfs"))return root_binding_fail("Bridge root discovery: ubi0_3 is not named ubi_rootfs");
    /* /proc/mounts is a symlink to self/mounts on the target kernel. Keep the
     * no-follow file reader and open the canonical procfs endpoint directly. */
    if(text_file("proc/self/mounts",mounts,sizeof(mounts)))return root_binding_fail("Bridge root discovery: /proc/self/mounts unavailable");
    char *save=NULL;
    for(char*l=strtok_r(mounts,"\n",&save);l;l=strtok_r(NULL,"\n",&save)){
        char source[128],target[128],type[64],options[256];
        if(sscanf(l,"%127s %127s %63s %255s",source,target,type,options)!=4)return root_binding_fail("Bridge root discovery: malformed /proc/mounts row");
        if(strcmp(target,"/"))continue;
        roots++;if(strcmp(type,"squashfs"))return root_binding_fail("Bridge root discovery: root filesystem is not squashfs");snprintf(root_source,sizeof(root_source),"%s",source);snprintf(root_target,sizeof(root_target),"%s",target);snprintf(root_type,sizeof(root_type),"%s",type);snprintf(root_options,sizeof(root_options),"%s",options);
        int readonly=0,writable=0;char*option_save=NULL;for(char*t=strtok_r(options,",",&option_save);t;t=strtok_r(NULL,",",&option_save)){if(!strcmp(t,"ro"))readonly++;if(!strcmp(t,"rw"))writable++;}
        if(readonly!=1||writable)return root_binding_fail("Bridge root discovery: root mount is writable or lacks one ro option");
        if(!strcmp(source,"/dev/ubiblock0_3"))source_ok=1;
        else if(!strcmp(source,"mtd:ubi_rootfs"))gluebi=1;
#if !BI_TEST
        else if(!strcmp(source,"/dev/root")){
            struct stat st;char rel[128],event[512];if(stat("/",&st))return -1;
            snprintf(rel,sizeof(rel),"sys/dev/block/%u:%u/uevent",major(st.st_dev),minor(st.st_dev));
            if(text_file(rel,event,sizeof(event)))return -1;
            char *event_save=NULL;for(char*t=strtok_r(event,"\n",&event_save);t;t=strtok_r(NULL,"\n",&event_save))if(!strcmp(t,"DEVNAME=ubiblock0_3"))source_ok=1;
        }
#endif
    }
    if(roots!=1)return root_binding_fail("Bridge root discovery: expected exactly one / mount");
    if(!source_ok&&!gluebi)return root_binding_fail("Bridge root discovery: root mount source is not an accepted ubiblock/GlueBI source");
    if(text_file("proc/cmdline",cmd,sizeof(cmd)))return root_binding_fail("Bridge root discovery: /proc/cmdline unavailable");
    if(safe_copy(cmd_parse,sizeof(cmd_parse),cmd))return root_binding_fail("Bridge root discovery: command line exceeds parser capacity");
    unsigned bindings=0;save=NULL;
    for(char*t=strtok_r(cmd_parse," \t\r\n",&save);t;t=strtok_r(NULL," \t\r\n",&save))if(!strncmp(t,"ubi.mtd=",8)){
        if(++bindings!=1||!t[8]||strlen(t+8)>=sizeof(g_running_mtd_binding))return root_binding_fail("Bridge root discovery: ubi.mtd is duplicate, empty, or too long");
        snprintf(g_running_mtd_binding,sizeof(g_running_mtd_binding),"%s",t+8);
    }
    if(bindings!=1)return root_binding_fail("Bridge root discovery: command line must contain one ubi.mtd binding");
    if(gluebi&&verify_gluebi_root(root_source,root_target,root_type,root_options,cmd,idx))return -1;
    if(!gluebi)safe_copy(g_root_binding_reason,sizeof(g_root_binding_reason),"ubiblock root binding PASS");
    *index=idx;return 0;
}
static int locate_system_slots(bi_part*parts,size_t n,uint32_t selected,bi_part**bridge,bi_part**oem){
    bi_part*a=NULL,*b=NULL;unsigned an=0,bn=0,idx=0;
    for(size_t i=0;i<n;i++){bi_part*p=&parts[i];if(p->off==0x900000u){a=p;an++;}if(p->off==0x3100000u){b=p;bn++;}}
    if(an!=1||bn!=1||!a||!b||a->size!=0x2800000u||b->size!=0x2800000u){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行系统分区布局不符合两槽物理边界合同");return -1;}
    if(running_bridge_index(&idx)){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行桥接根绑定失败：%s",g_root_binding_reason);return -1;}
    bi_part*pair[2]={a,b};
    for(unsigned j=0;j<2;j++){
        bi_part*p=pair[j];if(p->erase!=ERASE_BYTES||p->page!=PAGE_BYTES||p->oob!=a->oob||p->ecc!=a->ecc||p->oob!=b->oob||p->ecc!=b->ecc){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行系统槽NAND几何不一致");return -1;}
        for(size_t i=0;i<n;i++){bi_part*q=&parts[i];if(q!=p&&p->off<(uint64_t)q->off+q->size&&q->off<(uint64_t)p->off+p->size){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行系统槽物理边界与其他MTD分区重叠");return -1;}}
    }
    if(idx==a->index){*bridge=a;*oem=b;}else if(idx==b->index){*bridge=b;*oem=a;}else{snprintf(g_oem_reason,sizeof(g_oem_reason),"运行根UBI mtd_num不映射到已核验的A/B系统槽");return -1;}
    if(strcmp((*bridge)->name,"rootfs")||strcmp((*oem)->name,"rootfs_1")){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行根与返回原厂槽的逻辑名称不符合A/B映射");return -1;}
    char numeric[32];snprintf(numeric,sizeof(numeric),"%u",idx);
    if(strcmp(g_running_mtd_binding,(*bridge)->name)&&strcmp(g_running_mtd_binding,numeric)){snprintf(g_oem_reason,sizeof(g_oem_reason),"内核ubi.mtd=%s与正在运行桥接槽不符",g_running_mtd_binding);return -1;}
    /* At initial discovery, boot records and the running bridge must agree.
     * Following a verified commit, Linux's MTD aliases intentionally stay as
     * booted; the next-boot selector is checked separately against the bound
     * postimage, never used to guess this process's physical root. */
    if(!g_oem_post_valid&&selected!=((*bridge)->off==0x900000u?0u:1u)){snprintf(g_oem_reason,sizeof(g_oem_reason),"当前BOOTCONFIG rootfs selector与正在运行物理根槽不一致");return -1;}
    if(g_oem_bridge_off&&g_oem_bridge_off!=(*bridge)->off){snprintf(g_oem_reason,sizeof(g_oem_reason),"事务绑定的桥接物理槽发生变化");return -1;}
    return 0;
}
static int verify_oem_slot(bi_part*parts,size_t n,uint32_t selected,int bind){
    bi_part*bridge=NULL,*oem=NULL;g_oem_verified=0;g_oem_records_verified=0;
    snprintf(g_oem_reason,sizeof(g_oem_reason),"原厂完整内容或运行槽身份尚未核验");
    if(locate_system_slots(parts,n,selected,&bridge,&oem))return -1;
    bi_part fresh_bridge,fresh_oem;
    if(part_open(bridge->index,&fresh_bridge,0)){snprintf(g_oem_reason,sizeof(g_oem_reason),"桥接槽重开失败；物理身份未能复核");return -1;}
    if(!part_identity_equal(bridge,&fresh_bridge)){part_close(&fresh_bridge);snprintf(g_oem_reason,sizeof(g_oem_reason),"桥接槽重开后设备号/范围/几何/名称变化");return -1;}part_close(&fresh_bridge);
    if(part_open(oem->index,&fresh_oem,0)){snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM槽重开失败；物理身份未能复核");return -1;}
    if(!part_identity_equal(oem,&fresh_oem)){part_close(&fresh_oem);snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM槽重开后设备号/范围/几何/名称变化");return -1;}
    if(part_close(oem)){part_close(&fresh_oem);snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM槽旧句柄关闭失败");return -1;}oem->fd=fresh_oem.fd;fresh_oem.fd=-1;
    bos_geometry observed={oem->off,oem->size,oem->erase,oem->page,oem->oob,oem->ecc};
    uint8_t candidate[32];bos_measurement measurement;bos_pool_match matched;
#if BI_HAVE_OEM_MANIFEST && defined(BI_OEM_POOL) && defined(BI_OEM_POOL_COUNT) && defined(BI_OEM_POOL_REVISION)
    if(BI_OEM_POOL_COUNT==0||BI_OEM_POOL_COUNT>BOS_POOL_MAX){snprintf(g_oem_reason,sizeof(g_oem_reason),"编译原厂池大小超出合同范围");return -1;}
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    memset(candidate,0,sizeof(candidate));
#else
    if(sha_bytes(g_candidate_hash,candidate)){snprintf(g_oem_reason,sizeof(g_oem_reason),"桥接候选绑定摘要格式无效");return -1;}
#endif
    {uint32_t bad_at=0;int scan_errno=0;int bad=qualification_badblock_scan(oem,&bad_at,&scan_errno);
        if(bad>0){snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM槽含坏块；物理块偏移 0x%x",bad_at);return -1;}
        if(bad<0){snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM槽坏块状态读取失败；物理块偏移 0x%x",bad_at);(void)scan_errno;return -1;}}
    {bos_code measured=bos_measure(oem_read,oem,&observed,&measurement);
        if(measured!=BOS_OK){snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM槽内容测量失败：%s",bos_code_name(measured));return -1;}}
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    {unsigned found=0;bos_pool_match selected;const br_return_source_record*source=NULL;
        char runtime_compat[2048],compat_path[PATH_MAX];if(path_join(compat_path,sizeof(compat_path),"proc/device-tree/compatible")){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行compatible路径溢出");return -1;}int fd=open(compat_path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);ssize_t compat_n=fd<0?-1:read(fd,runtime_compat,sizeof(runtime_compat));if(fd>=0)close(fd);
        if(compat_n<=0||compat_n==(ssize_t)sizeof(runtime_compat)){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行compatible缺失或超出限制");return -1;}
        for(size_t si=0;si<BI_RETURN_SOURCE_COUNT;si++){uint8_t source_hash[32];bos_pool_match current;
            int duplicate=0;for(size_t earlier=0;earlier<si;earlier++)if(!strcmp(BI_RETURN_SOURCES[earlier].official_sha256,BI_RETURN_SOURCES[si].official_sha256)){duplicate=1;break;}if(duplicate)continue;
            if(sha_bytes(BI_RETURN_SOURCES[si].official_sha256,source_hash))return -1;
            bos_code rc=bos_pool_select(&measurement,BI_OEM_POOL,BI_OEM_POOL_COUNT,source_hash,BI_OEM_POOL_REVISION,&current);
            if(rc==BOS_OK){const br_return_source_record*route=NULL;unsigned route_matches=0;
                for(size_t ri=si;ri<BI_RETURN_SOURCE_COUNT;ri++)if(!strcmp(BI_RETURN_SOURCES[ri].official_sha256,BI_RETURN_SOURCES[si].official_sha256)&&dt_compat_has((const uint8_t*)runtime_compat,(size_t)compat_n,BI_RETURN_SOURCES[ri].compatible)){route=&BI_RETURN_SOURCES[ri];route_matches++;}
                if(route_matches!=1){snprintf(g_oem_reason,sizeof(g_oem_reason),"运行DT与精确OEM source route未唯一匹配");return -1;}
                selected=current;source=route;memcpy(candidate,source_hash,32);found++;}
            else if(rc==BOS_AMBIGUOUS){snprintf(g_oem_reason,sizeof(g_oem_reason),"OEM源池存在歧义；拒绝返回");return -1;}
        }
        if(found!=1||!source){snprintf(g_oem_reason,sizeof(g_oem_reason),"source-bound OEM池未唯一命中精确原厂内容");return -1;}
        matched=selected;g_return_source_record=source;
    }
#elif defined(BI_RETURN_SOURCE_REGISTRY_V2) && BI_HAVE_RETURN_SOURCE_REGISTRY
    {bos_code selected_pool=bos_pool_select(&measurement,BI_OEM_POOL,BI_OEM_POOL_COUNT,candidate,BI_OEM_POOL_REVISION,&matched);
        if(selected_pool!=BOS_OK){snprintf(g_oem_reason,sizeof(g_oem_reason),"候选绑定原厂池未命中：%s",bos_code_name(selected_pool));return -1;}}
    {unsigned matches=0;const br_return_source_record*source=NULL;
        for(size_t i=0;i<BI_RETURN_SOURCE_COUNT;i++){
            const br_return_source_record*r=&BI_RETURN_SOURCES[i];
            if(strcmp(r->official_sha256,BI_RETURN_SOURCE_OFFICIAL_SHA256)||
               strcmp(r->stock_kernel_sha256,BI_RETURN_SOURCE_STOCK_KERNEL_SHA256)||
               strcmp(r->bridge_kernel_sha256,BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256)||
               strcmp(r->source_revision_sha256,BI_RETURN_SOURCE_REVISION_SHA256)||
               strcmp(r->compressed_kernel_sha256,BI_RETURN_SOURCE_COMPRESSED_KERNEL_SHA256)||
               strcmp(r->nand_dt_sha256,BI_RETURN_SOURCE_NAND_DT_SHA256)||
               strcmp(r->route,BI_RETURN_SOURCE_ROUTE)||strcmp(r->compatible,BI_RETURN_SOURCE_COMPATIBLE)||
               r->product!=BI_RETURN_SOURCE_PRODUCT)continue;
            source=r;matches++;
        }
        if(matches!=1||!source||matched.product!=source->product||
           matched.exit_policy!=BOS_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V1){
            snprintf(g_oem_reason,sizeof(g_oem_reason),"候选OEM池与唯一source-route或安装退出策略2不匹配");return -1;}
        g_return_source_record=source;
    }
#else
    {bos_code selected_pool=bos_pool_select(&measurement,BI_OEM_POOL,BI_OEM_POOL_COUNT,candidate,BI_OEM_POOL_REVISION,&matched);
        if(selected_pool!=BOS_OK){snprintf(g_oem_reason,sizeof(g_oem_reason),"候选绑定原厂池未命中：%s",bos_code_name(selected_pool));return -1;}}
#endif
    if(!matched.logical_content_verified){snprintf(g_oem_reason,sizeof(g_oem_reason),"原厂池未置逻辑内容核验标志；保持拒绝");return -1;}
#else
    (void)observed;(void)candidate;(void)measurement;(void)matched;(void)oem_read;
#if BI_HAVE_OEM_MANIFEST
    (void)BI_OEM_MANIFESTS;
#endif
    snprintf(g_oem_reason,sizeof(g_oem_reason),"没有候选/出处/revision绑定的编译原厂manifest池；未知保持关闭");return -1;
#endif
    bos_geometry bg={bridge->off,bridge->size,bridge->erase,bridge->page,bridge->oob,bridge->ecc};
    bos_measurement running;uint8_t expected[32],stock[32],revision[32];
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(!g_return_source_record||g_return_source_record->product!=matched.product||
       sha_bytes(g_return_source_record->stock_kernel_sha256,stock)||
       sha_bytes(g_return_source_record->bridge_kernel_sha256,expected)||
       sha_bytes(g_return_source_record->source_revision_sha256,revision)||
       !g_return_source_record->route||!g_return_source_record->compatible||
       sha_bytes(g_return_source_record->nand_dt_sha256,candidate)){
        snprintf(g_oem_reason,sizeof(g_oem_reason),"可信四来源source/kernel/route registry不匹配");return -1;
    }
#else
    if(sha_bytes(BI_RETURN_SOURCE_OFFICIAL_SHA256,expected)||sha_bytes(BI_RETURN_SOURCE_STOCK_KERNEL_SHA256,stock)||
       sha_bytes(BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256,expected)||sha_bytes(BI_RETURN_SOURCE_REVISION_SHA256,revision)||
       !BI_RETURN_SOURCE_PRODUCT||!BI_RETURN_SOURCE_ROUTE[0]||!BI_RETURN_SOURCE_COMPATIBLE[0]){
        snprintf(g_oem_reason,sizeof(g_oem_reason),"可信source/kernel/route编译绑定缺失；不以neutral标识代替");return -1;
    }
#endif
    {bos_code running_rc=bos_measure(oem_read,bridge,&bg,&running);
        if(running_rc!=BOS_OK){snprintf(g_oem_reason,sizeof(g_oem_reason),"当前桥接槽内容测量失败：%s",bos_code_name(running_rc));return -1;}}
    if(memcmp(expected,running.immutable[0].sha256,32)){snprintf(g_oem_reason,sizeof(g_oem_reason),"当前运行桥接kernel不匹配编译来源");return -1;}
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(memcmp(stock,measurement.immutable[0].sha256,32)||matched.product!=g_return_source_record->product){snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM kernel/product不匹配唯一编译来源");return -1;}
#elif defined(BI_RETURN_SOURCE_REGISTRY_V2) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(memcmp(stock,measurement.immutable[0].sha256,32)||matched.product!=g_return_source_record->product){snprintf(g_oem_reason,sizeof(g_oem_reason),"候选OEM kernel/product不匹配唯一编译来源");return -1;}
#else
    if(memcmp(stock,measurement.immutable[0].sha256,32)){snprintf(g_oem_reason,sizeof(g_oem_reason),"返回OEM kernel不匹配编译原厂来源");return -1;}
    if(matched.product!=BI_RETURN_SOURCE_PRODUCT){snprintf(g_oem_reason,sizeof(g_oem_reason),"原厂池产品策略与返回loader来源合同不一致");return -1;}
#endif
    if(g_common_bound&&(memcmp(g_bridge_snapshot,running.snapshot_sha256,32)||memcmp(g_return_source_revision,revision,32))){
        snprintf(g_oem_reason,sizeof(g_oem_reason),"运行桥接快照或可信source revision变化");return -1;
    }
    if(bind&&!g_common_bound){memcpy(g_bridge_snapshot,running.snapshot_sha256,32);memcpy(g_return_source_revision,revision,32);g_common_bound=1;}
    /* Rootfs_data may be sparse or empty; its exact initial snapshot is still
     * commit-bound, without pretending it is an immutable official volume. */
    if(g_oem_bound&&(g_oem_offset!=oem->off||g_oem_index!=oem->index||g_oem_dev!=oem->dev||memcmp(g_oem_snapshot,measurement.snapshot_sha256,32)||memcmp(g_oem_pool_match.candidate_sha256,matched.candidate_sha256,32)||memcmp(g_oem_pool_match.pool_revision_sha256,matched.pool_revision_sha256,32)||g_oem_pool_match.content_group!=matched.content_group||g_oem_pool_match.exit_policy!=matched.exit_policy||g_oem_pool_match.product!=matched.product)){snprintf(g_oem_reason,sizeof(g_oem_reason),"预检后原厂槽内容或物理身份变化");return -1;}
    if(bind&&!g_oem_bound){memcpy(g_oem_snapshot,measurement.snapshot_sha256,32);g_oem_pool_match=matched;g_oem_bound=1;}
    g_oem_offset=oem->off;g_oem_index=oem->index;g_oem_dev=oem->dev;g_oem_selector=oem->off==0x900000u?0u:1u;g_oem_bridge_off=bridge->off;
    g_oem_verified=1;
    snprintf(g_oem_reason,sizeof(g_oem_reason),"完整原厂逻辑内容与物理槽核验通过；不代表无线/运行时或硬件启动PASS");return 0;
}
static int oem_return_available(void){return g_return_ready&&g_oem_verified&&g_oem_records_verified&&g_stock_baseline==1&&!g_modified&&!g_boot_lock_seen&&!g_token_used&&!strcmp(g_phase,"ready");}

static int return_source_compatible(const uint8_t *compat,size_t n){
#if (defined(BRIDGE_RETURN_ONLY) || defined(BI_RETURN_SOURCE_REGISTRY_V2)) && BI_HAVE_RETURN_SOURCE_REGISTRY
    for(size_t i=0;i<BI_RETURN_SOURCE_COUNT;i++)if(BI_RETURN_SOURCES[i].compatible&&dt_compat_has(compat,n,BI_RETURN_SOURCES[i].compatible))return 1;
    return 0;
#else
    return dt_compat_has(compat,n,BI_RETURN_SOURCE_COMPATIBLE);
#endif
}

static int verify_bridge_identity(void){
#if BI_TEST
    char profile[128],rid[128],compat[512],kernel[128],marker[PATH_MAX];int fd;ssize_t n;
    if(text_file("etc/bridge/profile",profile,sizeof(profile))||text_file("etc/bridge/rootfs-id",rid,sizeof(rid))||text_file("proc/sys/kernel/osrelease",kernel,sizeof(kernel)))return -1;
    profile[strcspn(profile,"\r\n")]=0;rid[strcspn(rid,"\r\n")]=0;
    kernel[strcspn(kernel,"\r\n")]=0;
    if(path_join(marker,sizeof(marker),"proc/device-tree/h3c,bridge-profile"))return -1;
    fd=open(marker,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;n=read(fd,compat,sizeof(compat));close(fd);
    if(n!=(ssize_t)sizeof("H3C-BOOTBRIDGE-MULTICONFIG-V1")||memcmp(compat,"H3C-BOOTBRIDGE-MULTICONFIG-V1",sizeof("H3C-BOOTBRIDGE-MULTICONFIG-V1")))return -1;
    if(path_join(marker,sizeof(marker),"proc/device-tree/compatible"))return -1;
    fd=open(marker,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;n=read(fd,compat,sizeof(compat));if(n<=0||n==(ssize_t)sizeof(compat)){close(fd);return -1;}close(fd);
    return !strcmp(profile,"H3C-BOOTBRIDGE-RUNTIME-v1")&&!strcmp(rid,"H3C-BOOTBRIDGE-ROOTFS-v1")&&return_source_compatible((const uint8_t*)compat,(size_t)n)&&!strcmp(kernel,"4.4.60")?0:-1;
#else
    char b[512],compat[2048],kernel[128];int fd;ssize_t n;
    const char *provenance=getenv("BRIDGE_INIT_VERIFIED");
    if(!provenance||strcmp(provenance,"H3C-BOOTBRIDGE-RUNTIME-v1"))return -2;
    if(text_file("etc/bridge/profile",b,sizeof(b))||(strcmp(b,"H3C-BOOTBRIDGE-RUNTIME-v1\n")&&strcmp(b,"H3C-BOOTBRIDGE-RUNTIME-v1")))return -3;
    if(text_file("etc/bridge/rootfs-id",b,sizeof(b))||(strcmp(b,"H3C-BOOTBRIDGE-ROOTFS-v1\n")&&strcmp(b,"H3C-BOOTBRIDGE-ROOTFS-v1")))return -4;
    if(path_join(b,sizeof(b),"proc/device-tree/compatible"))return -5;
    fd=open(b,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -5;
    n=read(fd,compat,sizeof(compat));close(fd);
    if(n<=0||n==(ssize_t)sizeof(compat)||!return_source_compatible((const uint8_t*)compat,(size_t)n))return -5;
    if(text_file("proc/sys/kernel/osrelease",kernel,sizeof(kernel)))return -6;
    kernel[strcspn(kernel,"\r\n")]=0;
    if(strcmp(kernel,"4.4.60"))return -6;
    if(path_join(b,sizeof(b),"proc/device-tree/h3c,bridge-profile"))return -7;
    fd=open(b,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -7;
    n=read(fd,compat,sizeof(compat)-1);if(n<=0||(size_t)n>=sizeof(compat)-1){close(fd);return -7;}
    close(fd);compat[n]=0;
    return (n==(ssize_t)sizeof("H3C-BOOTBRIDGE-MULTICONFIG-V1")&&!memcmp(compat,"H3C-BOOTBRIDGE-MULTICONFIG-V1",sizeof("H3C-BOOTBRIDGE-MULTICONFIG-V1")))?0:-7;
#endif
}
static const char *bridge_identity_reason(int result){
    switch(result){
    case -2:return "启动校验标记BRIDGE_INIT_VERIFIED缺失或不一致";
    case -3:return "运行profile文件缺失或内容不匹配";
    case -4:return "rootfs-id文件缺失或内容不匹配";
    case -5:return "设备树compatible读取失败或与可信source route不匹配";
    case -6:return "运行内核版本读取失败或不是4.4.60";
    case -7:return "设备树h3c,bridge-profile标记读取失败或内容不匹配";
    default:return "neutral DT/profile/rootfs或可信source route不匹配";
    }
}
static int verify_qfprom_authenticate(void){char value[64];uint32_t v;if(text_file("sys/devices/system/qfprom/qfprom0/authenticate",value,sizeof(value)))return -1;return parse_u32(value,&v)==0&&v==0?0:-1;}
#if BI_HAVE_INSTALL_PROFILE
/* Read fresh runtime platform evidence at qualification and again before
 * reboot. The narrow identity API never treats failed/truncated reads as
 * proof for the observed stock ENV that lacks a machid field. */
static bii_code check_current_candidate_identity(const uint8_t *env,size_t env_bytes,
        const char *cmdline,const char *candidate_hash)
{
    uint8_t compatible[2048];ssize_t bytes=-1;char path[PATH_MAX],machid[32];
    if(!path_join(path,sizeof(path),"proc/device-tree/compatible")){
        int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        if(fd>=0){bytes=read(fd,compatible,sizeof(compatible));close(fd);}
    }
    snprintf(machid,sizeof(machid),"%x",(unsigned)BI_EXPECTED_MACHID);
    return bii_check_public_candidate_runtime(BI_HARDWARE_CLASS,BI_EXPECTED_PRODUCT,
        BI_FIRST_INSTALL_SOURCE_SHA256,machid,candidate_hash,env,env_bytes,cmdline,
        bytes>0&&bytes<(ssize_t)sizeof(compatible)?compatible:NULL,
        bytes>0&&bytes<(ssize_t)sizeof(compatible)?(size_t)bytes:0,NULL);
}
/* Keep the code name first (status JSON and tests key on it) and say in
 * plain words which ENV/platform fact did not fit. */
static const char *__attribute__((unused)) identity_reason(bii_code c)
{
    switch(c){
    case BII_POLICY:return "policy：本构建的板型/候选/原厂固件组合不在身份登记表中";
    case BII_SIZE:return "size：APPSBLENV分区大小不是512 KiB";
    case BII_CRC:return "crc：APPSBLENV的CRC校验失败（ENV损坏或由其他U-Boot以不同大小保存）";
    case BII_FORMAT:return "format：APPSBLENV含控制字符、重复bootcmd或无法解析的变量";
    case BII_PRODUCT:return "product：ENV中productname缺失、重复或与原厂固件机型不一致";
    case BII_ENV_MACHID:return "env-machid：ENV中machid缺失（且无可替代的设备树路由证明）、重复或与板型不符";
    case BII_CMDLINE_MACHID:return "cmdline-machid：内核启动参数中的machid重复或与板型不符";
    case BII_BOOTARGS:return "bootargs：ENV中bootargs重复，或含本板型U-Boot会自行追加的root/ubi参数";
    default:return bii_code_name(c);
    }
}
#endif
static int machid_value_ok(const char *value){char *end;unsigned long parsed;const char*p=value;if(!p||!*p)return 0;if(p[0]=='0'&&(p[1]=='x'||p[1]=='X'))p+=2;if(!*p)return 0;for(const char*q=p;*q;q++)if(!((*q>='0'&&*q<='9')||(*q>='a'&&*q<='f')||(*q>='A'&&*q<='F')))return 0;errno=0;parsed=strtoul(value,&end,16);return !errno&&end!=value&&!*end&&parsed==0x8040000UL;}
static int __attribute__((unused)) verify_cmdline_machid(void){char line[2048],value[64]="";if(text_file("proc/cmdline",line,sizeof(line)))return -1;char *save=NULL;for(char*t=strtok_r(line," \t\r\n",&save);t;t=strtok_r(NULL," \t\r\n",&save)){if(!strncmp(t,"machid=",7)){const char*v=t+7;if(!machid_value_ok(v))return -1;if(value[0]&&strcmp(value,v))return -1;if(!value[0]&&safe_copy(value,sizeof(value),v))return -1;}}return 0;}

static int __attribute__((unused)) parse_env_product(bi_part*env,const char**reason){*reason="APPSBLENV 完整读取或内存分配失败";uint8_t*buf=malloc(0x40000);if(!buf)return -1;int ok=0;const char*accepted_product=NULL;if(read_all(env->fd,buf,0x40000,0))goto out;uint32_t got=le32(buf),crc=0xffffffffu;/* U-Boot CRC covers the 256 KiB active environment, not the full MTD. */
    for(size_t i=4;i<0x40000;i++){crc^=buf[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(int32_t)(crc&1));}crc=~crc;if(crc!=got){*reason="APPSBLENV CRC 校验失败";goto out;}
    *reason="APPSBLENV 字段结构、唯一产品名或平台标识不符合候选安装要求";
    size_t pos=4;unsigned product_count=0,machid_count=0;while(pos<0x40000){size_t end=pos;while(end<0x40000&&buf[end])end++;if(end==pos)break;if(end==0x40000)goto out;
        if(end-pos>=12&&!memcmp(buf+pos,"productname=",12)){product_count++;if(end-pos==sizeof("productname=NX30")-1&&!memcmp(buf+pos,"productname=NX30",sizeof("productname=NX30")-1))accepted_product="NX30";else if(end-pos==sizeof("productname=NX30_2i5x")-1&&!memcmp(buf+pos,"productname=NX30_2i5x",sizeof("productname=NX30_2i5x")-1))accepted_product="NX30_2i5x";else goto out;}
        if(end-pos>=7&&!memcmp(buf+pos,"machid=",7)){char val[64];if(++machid_count>1||end-pos-7>=sizeof(val))goto out;memcpy(val,buf+pos+7,end-pos-7);val[end-pos-7]=0;if(!machid_value_ok(val))goto out;}
        pos=end+1;
    }ok=product_count==1;
out:if(ok&&accepted_product)*reason=accepted_product;free(buf);return ok?0:-1;}

static int verify_common_env(bi_part*env,const char**reason){
    uint8_t*b=malloc(0x40000);unsigned products=0,machids=0;size_t pos=4;int ok=0;
    *reason="APPSBLENV完整读取失败";if(!b)return -1;
    if(read_all(env->fd,b,0x40000,0))goto done;
    uint32_t c=0xffffffffu;for(size_t i=4;i<0x40000;i++){c^=b[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}
    if(~c!=le32(b)){*reason="APPSBLENV CRC 校验失败";goto done;}
    *reason="APPSBLENV字段结构或唯一产品名/平台键重复";
    while(pos<0x40000){size_t end=pos;while(end<0x40000&&b[end])end++;if(end==pos){
            if(products>1||machids>1){*reason="APPSBLENV字段结构或唯一产品名/平台键重复";goto done;}
            ok=1;break;
        }if(end==0x40000)goto done;
        if(end-pos>=12&&!memcmp(b+pos,"productname=",12)&&++products>1)goto done;
        if(end-pos>=7&&!memcmp(b+pos,"machid=",7)&&++machids>1)goto done;
        pos=end+1;
    }
done:free(b);return ok?0:-1;
}

static int return_env_boot_contract(bi_part*env){
    uint8_t*b=malloc(0x40000);int ok=0;unsigned commands=0,args=0,delays=0,products=0;size_t pos=4;
    if(!b)return -1;
    if(read_all(env->fd,b,0x40000,0))goto done;
    uint32_t c=0xffffffffu;for(size_t i=4;i<0x40000;i++){c^=b[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}
    if(~c!=le32(b))goto done;
    while(pos<0x40000){size_t end=pos;while(end<0x40000&&b[end])end++;if(end==pos)break;if(end==0x40000)goto done;
        const uint8_t*p=b+pos;size_t n=end-pos;
        if(n>=8&&!memcmp(p,"bootcmd=",8)){if(++commands!=1||n!=15||memcmp(p+8,"bootipq",7))goto done;}
        if(n>=12&&!memcmp(p,"productname=",12)){
            const char*expected=NULL;
#if (defined(BRIDGE_RETURN_ONLY) || defined(BI_RETURN_SOURCE_REGISTRY_V2)) && BI_HAVE_RETURN_SOURCE_REGISTRY
            if(g_return_source_record){switch(g_return_source_record->product){case BOS_PRODUCT_RC3000:expected="RC3000";break;case BOS_PRODUCT_RT3000:expected="RT3000";break;case BOS_PRODUCT_RW3000:expected="RW3000";break;case BOS_PRODUCT_NX30:expected="NX30";break;default:break;}}
#else
            if(BI_RETURN_SOURCE_PRODUCT==BOS_PRODUCT_RC3000)expected="RC3000";else if(BI_RETURN_SOURCE_PRODUCT==BOS_PRODUCT_RT3000)expected="RT3000";else if(BI_RETURN_SOURCE_PRODUCT==BOS_PRODUCT_RW3000)expected="RW3000";else if(BI_RETURN_SOURCE_PRODUCT==BOS_PRODUCT_NX30)expected="NX30";
#endif
            const char *return_sha=g_return_source_record?g_return_source_record->official_sha256:BI_RETURN_SOURCE_OFFICIAL_SHA256;
            const char *return_route=g_return_source_record?g_return_source_record->route:BI_RETURN_SOURCE_ROUTE;
            char observed_product[32];
            if(++products!=1||!expected||n-12>=sizeof(observed_product))goto done;
            memcpy(observed_product,p+12,n-12);observed_product[n-12]=0;
            if(!bii_return_env_product_allowed(return_sha,expected,return_route,observed_product))goto done;
        }
        if(n>=9&&!memcmp(p,"bootargs=",9)){
            char value[2048];if(++args!=1||n-9==0||n-9>=sizeof(value))goto done;
            memcpy(value,p+9,n-9);value[n-9]=0;
            if(!br_return_bootargs_allowed(
                g_return_source_record?g_return_source_record->official_sha256:BI_RETURN_SOURCE_OFFICIAL_SHA256,
                g_return_source_record?g_return_source_record->route:BI_RETURN_SOURCE_ROUTE,value))goto done;
        }
        if(n>=10&&!memcmp(p,"bootdelay=",10)){uint32_t v;char value[16];if(++delays!=1||n-10>=sizeof(value))goto done;memcpy(value,p+10,n-10);value[n-10]=0;if(parse_u32(value,&v)||v>10)goto done;}
        pos=end+1;
    }
    ok=commands==1&&args==1&&delays==1&&products==1;
done:free(b);return ok?0:-1;
}
static int qualify_oem_return(bi_part*sbl,bi_part*env,const bi_part*bc0){
    const br_return_rule*rules=BI_RETURN_RULES;size_t count=BI_RETURN_RULE_COUNT;unsigned matches=0;uint32_t selected=0;uint8_t selected_evidence[32]={0};
    g_return_ready=0;snprintf(g_return_reason,sizeof(g_return_reason),"退出provider缺失或运行组合未经批准");
    if(!g_stock_baseline){snprintf(g_return_reason,sizeof(g_return_reason),"当前活动APPSBL不匹配四来源登记的精确原厂baseline");return -1;}
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(!g_return_source_record){snprintf(g_return_reason,sizeof(g_return_reason),"OEM原厂内容尚未唯一绑定四来源记录");return -1;}
#elif defined(BI_RETURN_SOURCE_REGISTRY_V2) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(!g_return_source_record){snprintf(g_return_reason,sizeof(g_return_reason),"候选OEM内容尚未唯一绑定source-route记录");return -1;}
#endif
    if(g_oem_pool_match.product<1||g_oem_pool_match.product>4)return -1;
    if(!rules||!count||count>2048)return -1;
    for(size_t i=0;i<count;i++){
        const br_return_rule*r=&rules[i];uint8_t ignored[32];
        if(!r->id||r->product!=g_oem_pool_match.product||r->exit_policy!=g_oem_pool_match.exit_policy||
           !r->route||!r->compatible||!r->nand_profile||sha_bytes(r->source_sha256,ignored)||sha_bytes(r->stock_kernel_sha256,ignored)||
           sha_bytes(r->bridge_kernel_sha256,ignored)||sha_bytes(r->source_revision_sha256,ignored)||
           sha_bytes(r->compressed_kernel_sha256,ignored)||sha_bytes(r->nand_dt_sha256,ignored)||sha_bytes(r->evidence_revision_sha256,ignored))return -1;
#if (defined(BRIDGE_RETURN_ONLY) || defined(BI_RETURN_SOURCE_REGISTRY_V2)) && BI_HAVE_RETURN_SOURCE_REGISTRY
        if(strcmp(r->source_sha256,g_return_source_record->official_sha256)||strcmp(r->stock_kernel_sha256,g_return_source_record->stock_kernel_sha256)||
           strcmp(r->bridge_kernel_sha256,g_return_source_record->bridge_kernel_sha256)||strcmp(r->source_revision_sha256,g_return_source_record->source_revision_sha256)||
           strcmp(r->compressed_kernel_sha256,g_return_source_record->compressed_kernel_sha256)||strcmp(r->nand_dt_sha256,g_return_source_record->nand_dt_sha256)||
           strcmp(r->route,g_return_source_record->route)||strcmp(r->compatible,g_return_source_record->compatible))continue;
#else
        if(strcmp(r->source_sha256,BI_RETURN_SOURCE_OFFICIAL_SHA256)||strcmp(r->stock_kernel_sha256,BI_RETURN_SOURCE_STOCK_KERNEL_SHA256)||
           strcmp(r->bridge_kernel_sha256,BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256)||strcmp(r->source_revision_sha256,BI_RETURN_SOURCE_REVISION_SHA256)||
           strcmp(r->compressed_kernel_sha256,BI_RETURN_SOURCE_COMPRESSED_KERNEL_SHA256)||strcmp(r->nand_dt_sha256,BI_RETURN_SOURCE_NAND_DT_SHA256)||
           strcmp(r->route,BI_RETURN_SOURCE_ROUTE)||strcmp(r->compatible,BI_RETURN_SOURCE_COMPATIBLE))continue;
#endif
        if(strcmp(r->nand_profile,g_nand_profile_id)||
#if defined(BI_RETURN_SOURCE_REGISTRY_V2)
           r->exit_policy!=BR_RETURN_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V2||
#else
           r->exit_policy!=BR_RETURN_EXIT_SOURCE_BOOTIPQ_V1||
#endif
           r->ecc_bits!=bc0->ecc||r->mtd_oob_bytes!=bc0->oob||r->bc_write_policy!=BR_RETURN_BC_WRITE_REVIEWED_LINUX_MTD_V1||r->env_policy!=BR_RETURN_ENV_EXPLICIT_BOOTIPQ_V1)continue;
#if !BI_TEST && !defined(BRIDGE_RETURN_ONLY) && !defined(BI_RETURN_SOURCE_REGISTRY_V2)
        /* Existing NX R012 domain only. Source format and neutral labels do not
         * add another APPSBL baseline, board or NAND write qualification. */
        if(strcmp(r->source_sha256,"61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922")||
           strcmp(r->stock_kernel_sha256,"5878e39213f37fb3d26b51a5197e09b05e7de18ee275fd7f430ea6eac5e7dc85")||
           strcmp(r->route,"config@mp02.1")||strcmp(r->compatible,"qcom,ipq5018-mp02.1")||
           r->ecc_bits!=4||r->mtd_oob_bytes!=64||strcmp(r->nand_profile,"h3c-oem-efba-128m-2k-sp64"))continue;
#endif
        selected=r->id;if(sha_bytes(r->evidence_revision_sha256,selected_evidence))return -1;matches++;
    }
    if(matches!=1){snprintf(g_return_reason,sizeof(g_return_reason),"精确source/kernel/route/NAND-ECC/BC写退出规则缺失或歧义");return -1;}
    if(verify_return_sbl(sbl)){snprintf(g_return_reason,sizeof(g_return_reason),"退出所需R012 SBL有效内容/FF尾不匹配");return -1;}
    if(return_env_boot_contract(env)){snprintf(g_return_reason,sizeof(g_return_reason),"ENV启动合同UNKNOWN：需明确bootipq、已审核bootargs和有界bootdelay；不猜空ENV默认行为");return -1;}
    if(g_return_rule&&(g_return_rule!=selected||memcmp(g_return_evidence,selected_evidence,32)))return -1;
    g_return_rule=selected;memcpy(g_return_evidence,selected_evidence,32);g_return_ready=1;snprintf(g_return_reason,sizeof(g_return_reason),"已登记OEMbaseline0与独立BC写/ENV/退出加载合同核验通过");return 0;
}

static int parse_bc(uint8_t*b,uint32_t*age,uint32_t*appsbl,uint32_t*rootfs){
    static const char *const expected[8]={"0:QSEE","0:DEVCFG","0:CDT","0:APPSBL","0:HLOS","rootfs","0:WIFIFW","0:BTFW"};
    if(le32(b)!=MAGIC||le32(b+8)!=8||le32(b+332)!=TAIL)return -1;
    *age=le32(b+4);unsigned ap=0,rf=0;char seen[8][17];memset(seen,0,sizeof(seen));
    for(unsigned i=0;i<8;i++){
        uint8_t*e=b+12+i*20;size_t z=strnlen((char*)e,16);
        if(!z||z==16||e[z]!=0||strcmp((char*)e,expected[i]))return -1;
        for(unsigned j=0;j<i;j++)if(!strcmp(seen[j],(char*)e))return -1;
        memcpy(seen[i],e,z);uint32_t v=le32(e+16);if(v>1)return -1;
        if(i==3){*appsbl=v;ap++;}if(i==5){*rootfs=v;rf++;}
    }
    return ap==1&&rf==1?0:-1;
}
static const char *version_scope(void){
#if defined(BRIDGE_RETURN_ONLY)
    return "oem-return-only-no-candidate";
#elif BI_TEST && !defined(BRIDGE_VERSION_COMPILED_TEST_REGISTRY)
    return "synthetic-fixture-d-ecc4";
#else
    return BI_VERSION_SCOPE;
#endif
}
static bv_result evaluate_registry(const char *installed,const char *candidate){
#if defined(BRIDGE_RETURN_ONLY)
    (void)installed;(void)candidate;
    bv_result reject={BV_REJECT_UNKNOWN,0,0};return reject;
#elif BI_TEST && !defined(BRIDGE_VERSION_COMPILED_TEST_REGISTRY)
    /* Legacy synthetic fixtures only. Production never reads this runtime list.
     * The actual policy still evaluates typed OEM(0)/candidate(1) records. */
    char text[4096],hashes[65][65];bv_record records[65];size_t n=0;
    bv_result reject={BV_REJECT_UNKNOWN,0,0};
    if(text_file("etc/bridge/test-allowed-appsbl-sha256",text,sizeof(text)))return reject;
    char *save=NULL;
    for(char *t=strtok_r(text,"\r\n ,",&save);t;t=strtok_r(NULL,"\r\n ,",&save)){
        if(!strcmp(t,candidate))continue;
        if(strlen(t)!=64||n>=64)return reject;
        memcpy(hashes[n],t,65);
        records[n]=(bv_record){hashes[n],version_scope(),APPSBL_BYTES,0,BV_OEM_BASELINE};n++;
    }
    records[n++]=(bv_record){candidate,version_scope(),APPSBL_BYTES,1,BV_CUSTOM_RELEASE};
    return bv_evaluate(records,n,version_scope(),candidate,APPSBL_BYTES,installed,APPSBL_BYTES);
#else
    return bv_evaluate(BI_VERSION_REGISTRY,BI_VERSION_REGISTRY_COUNT,version_scope(),
                       candidate,APPSBL_BYTES,installed,APPSBL_BYTES);
#endif
}
/* An unknown installed APPSBL (e.g. an earlier custom U-Boot) is overwritten
 * only when the candidate itself is a registered custom release: evaluating
 * the candidate against itself yields SKIP_IDENTICAL exactly then. */
static bv_result evaluate_version(const char *installed,const char *candidate){
    bv_result r=evaluate_registry(installed,candidate);
    if(r.decision==BV_REJECT_UNKNOWN&&evaluate_registry(candidate,candidate).decision==BV_SKIP_IDENTICAL)
        r.decision=BV_OVERRIDE_UNKNOWN;
    return r;
}
static const char *version_reason(bv_decision decision){
    switch(decision){
    case BV_FIRST_INSTALL:return "可信OEM基线：允许首次安装已登记候选";
    case BV_SKIP_IDENTICAL:return "可信候选已安装：跳过重复写入";
    case BV_UPGRADE:return "可信旧版本：允许升级到已登记的新序号";
    case BV_REJECT_DOWNGRADE:return "已安装可信较新版本：拒绝降级";
    case BV_REJECT_INCOMPATIBLE:return "APPSBL版本scope或候选类型不兼容";
    case BV_INVALID_REGISTRY:return "编译可信版本注册表无效：拒绝安装";
    case BV_OVERRIDE_UNKNOWN:return "当前APPSBL不是原厂或已登记版本：覆盖安装，不保留原U-Boot";
    default:return "完整APPSBL未命中可信版本注册表：拒绝安装";
    }
}
#if !defined(BRIDGE_RETURN_ONLY)
static const char *payload_path(void){
#if BI_TEST
    static char p[PATH_MAX];if(path_join(p,sizeof(p),"etc/bridge/uboot.bin"))return "";return p;
#else
    return BI_PAYLOAD_PATH;
#endif
}
#endif
static int __attribute__((unused)) read_regular(const char*path,uint8_t*out,size_t n){struct stat st;int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)n){close(fd);return -1;}int r=read_all(fd,out,n,0);int e=errno;close(fd);errno=e;return r;}

static int set_state(const char*phase,const char*stage,const char*message){snprintf(g_phase,sizeof(g_phase),"%s",phase);snprintf(g_stage,sizeof(g_stage),"%s",stage);snprintf(g_message,sizeof(g_message),"%s",message);return write_status();}
static int protected_snapshot(bi_part*ps,size_t n){for(size_t i=0;i<n;i++){bi_part*p=&ps[i];if(p->off>=STARTUP_END)continue;p->snapshot=malloc(p->size);if(!p->snapshot||read_all(p->fd,p->snapshot,p->size,0))return -1;}return 0;}
static int snapshot_fingerprint(bi_part*ps,size_t n,char out[65]){bi_sha256_ctx c;uint8_t d[32],meta[32];bi_sha256_init(&c);bi_sha256_update(&c,g_oem_snapshot,sizeof(g_oem_snapshot));bi_sha256_update(&c,g_bridge_snapshot,32);bi_sha256_update(&c,g_return_source_revision,32);bi_sha256_update(&c,g_return_evidence,32);put_le32(meta,g_stock_baseline);put_le32(meta+4,g_return_rule);bi_sha256_update(&c,meta,8);bi_sha256_update(&c,g_oem_pool_match.candidate_sha256,32);bi_sha256_update(&c,g_oem_pool_match.pool_revision_sha256,32);put_le32(meta,g_oem_pool_match.content_group);put_le32(meta+4,g_oem_pool_match.exit_policy);put_le32(meta+8,g_oem_pool_match.product);bi_sha256_update(&c,meta,12);put_le32(meta,g_oem_offset);put_le32(meta+4,g_oem_bridge_off);put_le32(meta+8,g_oem_index);bi_sha256_update(&c,meta,12);bi_sha256_update(&c,g_running_mtd_binding,strlen(g_running_mtd_binding)+1);bi_sha256_update(&c,g_nand_identity_hash,strlen(g_nand_identity_hash)+1);bi_sha256_update(&c,g_nand_profile_id,strlen(g_nand_profile_id)+1);bi_sha256_update(&c,g_active_hash,strlen(g_active_hash)+1);for(size_t i=0;i<n;i++){bi_part*p=&ps[i];if(p->off>=STARTUP_END)continue;put_le32(meta,p->off);put_le32(meta+4,p->size);put_le32(meta+8,p->index);put_le32(meta+12,p->erase);put_le32(meta+16,p->page);put_le32(meta+20,p->oob);put_le32(meta+24,p->ecc);memset(meta+28,0,4);bi_sha256_update(&c,meta,sizeof(meta));bi_sha256_update(&c,&p->dev,sizeof(p->dev));bi_sha256_update(&c,p->name,strlen(p->name)+1);bi_sha256_update(&c,p->snapshot,p->size);}bi_sha256_final(&c,d);bi_sha256_hex(d,out);return 0;}
static int snapshots_unchanged(bi_part*ps,size_t n){uint8_t*buf=malloc(ERASE_BYTES);if(!buf)return -1;for(size_t i=0;i<n;i++){bi_part*p=&ps[i];if(p->off>=STARTUP_END)continue;for(uint32_t o=0;o<p->size;o+=ERASE_BYTES){uint32_t z=p->size-o;if(z>ERASE_BYTES)z=ERASE_BYTES;if(read_all(p->fd,buf,z,o)||memcmp(buf,p->snapshot+o,z)){free(buf);return -1;}}}free(buf);return 0;}
static int snapshots_unchanged_except(bi_part*ps,size_t n,bi_part*appsbl,bi_part*bc0,bi_part*bc1){uint8_t*buf=malloc(ERASE_BYTES);if(!buf)return -1;for(size_t i=0;i<n;i++){bi_part*p=&ps[i];if(p==appsbl||p==bc0||p==bc1||p->off>=STARTUP_END)continue;for(uint32_t o=0;o<p->size;o+=ERASE_BYTES){uint32_t z=p->size-o;if(z>ERASE_BYTES)z=ERASE_BYTES;if(read_all(p->fd,buf,z,o)||memcmp(buf,p->snapshot+o,z)){free(buf);return -1;}}}free(buf);return 0;}
typedef struct {
    bi_part *parts[BBK_MAX_ENTRIES];
    size_t count;
    int output;
    uint64_t read_bytes,written;
    bi_sha256_ctx output_sha;
} bi_backup_io;
static int backup_read_at(void *opaque,uint64_t offset,void *buffer,size_t size){
    bi_backup_io *b=opaque;
#if BI_TEST
    if(!strcmp(fixture_fault(),"backupreadfail")&&offset>=STARTUP_END)return -1;
#endif
    for(size_t i=0;i<b->count;i++){
        bi_part *p=b->parts[i];
        if(offset<p->off||offset-p->off>=p->size||size>p->size-(offset-p->off))continue;
        if(read_all(p->fd,buffer,size,(off_t)(offset-p->off)))return -1;
        if(p->off<STARTUP_END&&(!p->snapshot||memcmp(buffer,p->snapshot+(size_t)(offset-p->off),size)))return -1;
#if BI_TEST
        if(!strcmp(fixture_fault(),"backupchange")&&b->read_bytes>=BI_BACKUP_DATA_BYTES&&offset>=STARTUP_END&&size)((uint8_t*)buffer)[0]^=1;
#endif
        b->read_bytes+=size;return 0;
    }
    return -1;
}
static int backup_sink(void *opaque,const void *buffer,size_t size){
    bi_backup_io *b=opaque;
    if(size>BI_BACKUP_ZIP_LIMIT-b->written)return -1;
#if BI_TEST
    if(!strcmp(fixture_fault(),"backupspacefail")&&b->written+size>1024U*1024U){errno=ENOSPC;return -1;}
#endif
    if(write_all(b->output,buffer,size,(off_t)b->written))return -1;
    bi_sha256_update(&b->output_sha,buffer,size);b->written+=size;return 0;
}
static int backup_capacity(void){
    struct statvfs space;
#if BI_TEST
    if(!strcmp(fixture_fault(),"backupnomem"))return -1;
#else
    struct statfs fs;struct sysinfo memory;
    if(statfs(g_tmpdir,&fs)||fs.f_type!=0x01021994||sysinfo(&memory))return -1; /* tmpfs only */
    uint64_t available=((uint64_t)memory.freeram+memory.bufferram)*memory.mem_unit;
    if(available<BI_BACKUP_ZIP_LIMIT+16U*1024U*1024U)return -1;
#endif
    if(statvfs(g_tmpdir,&space)||!space.f_frsize||
       space.f_bavail<(BI_BACKUP_ZIP_LIMIT+space.f_frsize-1)/space.f_frsize)return -1;
    return 0;
}
static int prepare_backup(bi_part*ps,size_t n){
    static const struct {uint32_t off,size;const char *name;} windows[]={
        {0x5900000,0x600000,"pdt_data"},{0x5f00000,0x600000,"pdt_data_1"},
        {0x6500000,0x100000,"exp"},{0x6600000,0x1700000,"plugin"}};
    bi_backup_io b;struct bbk_entry entries[BBK_MAX_ENTRIES];
    struct bbk_spec spec;struct bbk_io io;char temporary[PATH_MAX];
    uint8_t expected[32],actual[32],buffer[16384];bi_sha256_ctx readback;
    uint64_t total=0;uint32_t cursor=0;int fd=-1,ok=0,published=0;
    memset(&b,0,sizeof(b));memset(entries,0,sizeof(entries));
    if(g_backup_available)return 0; /* frozen initial ZIP is never rebuilt by a worker */
#if BI_TEST
    if(!strcmp(fixture_fault(),"backupfail"))return -1;
#endif
    const struct h3c_nand_catalog_profile *profile=h3c_nand_catalog_by_profile_id(g_nand_profile_id);
    if(!profile||(profile->capacity_bytes!=UINT64_C(0x08000000)&&
                  profile->capacity_bytes!=UINT64_C(0x10000000))||backup_capacity())return -1;
    for(size_t i=0;i<n;i++)if(ps[i].off<STARTUP_END){
        if(!ps[i].snapshot||b.count>=BBK_MAX_ENTRIES-4)return -1;
        b.parts[b.count++]=&ps[i];
    }
    for(size_t i=0;i<b.count;i++)for(size_t j=i+1;j<b.count;j++)if(b.parts[j]->off<b.parts[i]->off){bi_part*p=b.parts[i];b.parts[i]=b.parts[j];b.parts[j]=p;}
    for(size_t i=0;i<b.count;i++){
        bi_part*p=b.parts[i];if(p->off!=cursor||p->size>STARTUP_END-cursor)return -1;
        cursor+=p->size;snprintf(entries[i].name,sizeof(entries[i].name),"startup_%06x.bin",p->off);
        entries[i].offset=p->off;entries[i].size=p->size;entries[i].role=BBK_STARTUP;total+=p->size;
    }
    if(cursor!=STARTUP_END)return -1;
    for(size_t k=0;k<4;k++){
        bi_part *found=NULL;
        for(size_t i=0;i<n;i++)if(ps[i].off==windows[k].off){if(found)return -1;found=&ps[i];}
        if(!found||found->size!=windows[k].size||strcmp(found->name,windows[k].name)||
           found->page!=profile->page_bytes||found->erase!=profile->erase_bytes||
           found->ecc!=b.parts[0]->ecc||found->oob!=b.parts[0]->oob)return -1;
        for(size_t i=0;i<n;i++)if(&ps[i]!=found&&
            (!strcmp(ps[i].name,windows[k].name)||
             ((uint64_t)ps[i].off<((uint64_t)found->off+found->size)&&
              (uint64_t)found->off<((uint64_t)ps[i].off+ps[i].size))))return -1;
        size_t at=b.count++;b.parts[at]=found;
        snprintf(entries[at].name,sizeof(entries[at].name),"%s.bin",windows[k].name);
        entries[at].offset=found->off;entries[at].size=found->size;entries[at].role=BBK_OEM_PERSISTENT_DATA;total+=found->size;
    }
    if(total!=BI_BACKUP_DATA_BYTES||snprintf(temporary,sizeof(temporary),"%s.partial",g_backup_file)>=(int)sizeof(temporary))return -1;
    fd=open(temporary,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return -1;
    b.output=fd;bi_sha256_init(&b.output_sha);
    spec=(struct bbk_spec){profile->capacity_bytes==UINT64_C(0x08000000)?
        BBK_LAYOUT_H3C_D_128M_DUAL40_V1:BBK_LAYOUT_H3C_D_DUAL40_V2,
        BBK_ECC_DECODED_MAIN_NO_OOB,
        {profile->capacity_bytes,profile->page_bytes,profile->erase_bytes,
         profile->appsbl_raw_spare_bytes,b.parts[0]->ecc},entries,b.count};
    io=(struct bbk_io){&b,backup_read_at,&b,backup_sink};
    if(bbk_write_zip(&spec,&io)!=BBK_OK||b.written<BI_BACKUP_DATA_BYTES||fsync(fd))goto done;
    bi_sha256_final(&b.output_sha,expected);bi_sha256_init(&readback);
    for(uint64_t offset=0;offset<b.written;){
        size_t size=b.written-offset>sizeof(buffer)?sizeof(buffer):(size_t)(b.written-offset);
        if(read_all(fd,buffer,size,(off_t)offset))goto done;
        bi_sha256_update(&readback,buffer,size);offset+=size;
    }
    bi_sha256_final(&readback,actual);
#if BI_TEST
    if(!strcmp(fixture_fault(),"backupverifyfail"))actual[0]^=1;
#endif
    if(memcmp(expected,actual,sizeof(actual))||snapshots_unchanged(ps,n)||fchmod(fd,0400))goto done;
    {struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=b.written)goto done;}
    if(close(fd)){fd=-1;goto done;}fd=-1;
    /* link publishes atomically without overwriting any existing frozen file. */
    if(link(temporary,g_backup_file))goto done;
    published=1;
    if(unlink(temporary))goto done;
    bi_sha256_hex(actual,g_backup_hash);g_backup_size=b.written;g_backup_available=1;ok=1;
done:
    if(fd>=0)close(fd);
    if(!ok){unlink(temporary);if(published)unlink(g_backup_file);g_backup_hash[0]=0;g_backup_size=0;g_backup_available=0;}
    return ok?0:-1;
}
static int verify_sbl(bi_part*p){
#if BI_TEST
    uint32_t valid=0;if(fixture_attr(p->index,"sbl_valid_bytes",&valid))return -1;char expected[80];if(text_file("etc/bridge/test-sbl-sha256",expected,sizeof(expected)))return -1;expected[strcspn(expected,"\r\n")]=0;
#else
#if !BI_HAVE_PAYLOAD
    return -1;
#endif
    uint32_t valid=(uint32_t)BI_SBL_VALID_BYTES;const char*expected=BI_SBL_SHA256;
#endif
    if(!valid||valid>=p->size)return -1;
    uint8_t*buf=malloc(p->size);if(!buf)return -1;
    if(read_all(p->fd,buf,p->size,0)){free(buf);return -1;}
    char hash[65];hash_hex(buf,valid,hash);int ok=!strcmp(hash,expected)&&bytes_are_ff(buf+valid,p->size-valid);free(buf);return ok?0:-1;
}
static int verify_return_sbl(bi_part*p){
#if BI_TEST && !defined(BRIDGE_RETURN_REGISTRY_TEST)
    return verify_sbl(p);
#else
    const uint32_t valid=BI_HAVE_RETURN_SOURCE_REGISTRY&&g_return_source_record?g_return_source_record->sbl_valid_bytes:178176u;
    const char*expected=BI_HAVE_RETURN_SOURCE_REGISTRY&&g_return_source_record?g_return_source_record->sbl_sha256:"c8bc5e1d35a3092dc1292d1b8ba13061082e08992a2cb07c05dbf7561a1244b5";
    uint8_t*b=malloc(p->size);char hash[65];int ok=0;
    if(!b||p->size!=0x80000u||!valid||valid>=p->size){free(b);return -1;}
    if(!read_all(p->fd,b,p->size,0)){hash_hex(b,valid,hash);ok=!strcmp(hash,expected)&&bytes_are_ff(b+valid,p->size-valid);}
    free(b);return ok?0:-1;
#endif
}

#if BI_HAVE_PAYLOAD_PATCHES
static int apply_compiled_payload_patches(uint8_t *image,size_t image_bytes){
    if(!image||image_bytes!=APPSBL_BYTES||BI_PAYLOAD_BASE_BYTES!=APPSBL_BYTES||
       !BI_PAYLOAD_BASE_SHA256[0]||BI_PAYLOAD_PATCH_COUNT>APPSBL_BYTES||
       BI_PAYLOAD_PATCH_DATA_BYTES>APPSBL_BYTES||
       BI_PAYLOAD_PATCH_DATA_BYTES>sizeof(BI_PAYLOAD_PATCH_DATA))return -1;
    char base_hash[65];hash_hex(image,image_bytes,base_hash);
    if(strcmp(base_hash,BI_PAYLOAD_BASE_SHA256))return -1;
#if BI_PAYLOAD_PATCH_COUNT == 0
    /* C needs a sentinel array for the exact-base candidate. It represents
     * no ranges or data; it must never be interpreted as a writable range. */
    if(BI_PAYLOAD_PATCH_DATA_BYTES!=0||
       sizeof(BI_PAYLOAD_PATCHES)/sizeof(BI_PAYLOAD_PATCHES[0])!=1||
       BI_PAYLOAD_PATCHES[0].offset||BI_PAYLOAD_PATCHES[0].bytes||
       BI_PAYLOAD_PATCHES[0].data_offset||sizeof(BI_PAYLOAD_PATCH_DATA)!=1||
       BI_PAYLOAD_PATCH_DATA[0])return -1;
#else
    if(BI_PAYLOAD_PATCH_COUNT!=sizeof(BI_PAYLOAD_PATCHES)/sizeof(BI_PAYLOAD_PATCHES[0])||
       BI_PAYLOAD_PATCH_DATA_BYTES!=sizeof(BI_PAYLOAD_PATCH_DATA))return -1;
    uint64_t previous_end=0,data_end=0;
    for(uint32_t i=0;i<BI_PAYLOAD_PATCH_COUNT;i++){
        const struct bi_payload_patch *patch=&BI_PAYLOAD_PATCHES[i];
        uint64_t end=(uint64_t)patch->offset+patch->bytes;
        uint64_t data=(uint64_t)patch->data_offset+patch->bytes;
        if(!patch->bytes||patch->offset<previous_end||end>image_bytes||
           patch->data_offset!=data_end||data>BI_PAYLOAD_PATCH_DATA_BYTES)return -1;
        memcpy(image+patch->offset,BI_PAYLOAD_PATCH_DATA+patch->data_offset,patch->bytes);
        previous_end=end;data_end=data;
    }
    if(data_end!=BI_PAYLOAD_PATCH_DATA_BYTES)return -1;
#endif
    return 0;
}
#endif

#if BI_TEST && defined(BRIDGE_PAYLOAD_PATCH_TEST)
int bi_test_reconstruct_payload(uint8_t *image,size_t image_bytes){
    char hash[65];
    if(apply_compiled_payload_patches(image,image_bytes))return -1;
    hash_hex(image,image_bytes,hash);
    return strcmp(hash,BI_PAYLOAD_SHA256)?-1:0;
}
#endif

static int read_payload(uint8_t **out,char hash[65]){uint8_t*p=malloc(APPSBL_BYTES);if(!p)return -1;
#if !BI_HAVE_PAYLOAD
    (void)out;(void)hash;free(p);return -1;
#else
#if !BI_TEST || BI_HAVE_PAYLOAD_PATCHES
    if(BI_PAYLOAD_BYTES!=APPSBL_BYTES){free(p);return -1;}
#endif
#if BI_HAVE_PAYLOAD_PATCHES
    if(BI_PAYLOAD_BASE_BYTES!=APPSBL_BYTES||
       read_regular(payload_path(),p,BI_PAYLOAD_BASE_BYTES)||
       apply_compiled_payload_patches(p,APPSBL_BYTES)){free(p);return -1;}
#else
    if(read_regular(payload_path(),p,APPSBL_BYTES)){free(p);return -1;}
#endif
    hash_hex(p,APPSBL_BYTES,hash);
#if !BI_TEST || BI_HAVE_PAYLOAD_PATCHES
    if(strcmp(hash,BI_PAYLOAD_SHA256)){free(p);return -1;}
#else
    {char expected[80];if(text_file("etc/bridge/test-payload-sha256",expected,sizeof(expected))){free(p);return -1;}expected[strcspn(expected,"\r\n")]=0;if(strcmp(hash,expected)){free(p);return -1;}}
#endif
    *out=p;return 0;
#endif
}

static int discover_common(bi_part *parts,size_t *n,bi_part **sbl,bi_part **env,bi_part **art,bi_part **bc0,bi_part **bc1,bi_part **app0,bi_part **app1,uint8_t **payload,uint8_t **bc0_before,uint8_t **bc1_before,uint32_t *old_age,uint32_t *old_sel,uint32_t *root_sel){
    *payload=NULL;
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(BI_RETURN_SOURCE_COUNT<1||BI_RETURN_SOURCE_COUNT>3){add_check("runtime_source","blocked","source-bound return registry route count differs");return -1;}
#elif defined(BI_RETURN_SOURCE_REGISTRY_V2) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(BI_RETURN_SOURCE_COUNT!=1){add_check("runtime_source","blocked","provider source registry must contain its single compiled candidate route");return -1;}
#else
    {uint8_t d[32];if(sha_bytes(BI_RETURN_SOURCE_OFFICIAL_SHA256,d)||sha_bytes(BI_RETURN_SOURCE_STOCK_KERNEL_SHA256,d)||
       sha_bytes(BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256,d)||sha_bytes(BI_RETURN_SOURCE_REVISION_SHA256,d)||sha_bytes(BI_RETURN_SOURCE_COMPRESSED_KERNEL_SHA256,d)||sha_bytes(BI_RETURN_SOURCE_NAND_DT_SHA256,d)||
       !BI_RETURN_SOURCE_PRODUCT||!BI_RETURN_SOURCE_ROUTE[0]||!BI_RETURN_SOURCE_COMPATIBLE[0]){
        add_check("runtime_source","blocked","可信编译source/kernel/route或原厂manifest绑定缺失；退出保持关闭");return -1;}}
#endif
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    add_check("runtime_source","checking",g_return_source_binary_binding);
#else
    add_check("runtime_source","pass",g_return_source_binary_binding);
#endif
    add_check("bridge_runtime","checking","核对neutral bridge运行上下文与可信source route，不判硬件D");
    int bridge_identity=verify_bridge_identity();
    if(bridge_identity){add_check("bridge_runtime","blocked",bridge_identity_reason(bridge_identity));return -1;}
    add_check("bridge_runtime","pass","neutral运行标识一致；硬件/BC写资格由独立provider判断");
    add_check("mtd_map","checking","扫描 MTD sysfs 和 proc/mtd，按物理 offset 唯一绑定目标");if(discover_all(parts,n)){add_check("mtd_map","blocked","MTD 动态扫描失败或节点身份不一致");return -1;}
    bi_nand_identity nand_id;add_check("nand_identity","checking","从本次启动的只读内核探测日志核对NAND身份，不以几何推型号");if(read_nand_probe_identity(&nand_id)){add_check("nand_identity","blocked","无法从本次启动只读内核日志取得完整且无冲突的JEDEC/型号/几何");return -1;}
    uint32_t observed_ecc=0,observed_mtd_oob=0;const struct h3c_nand_catalog_profile *nand_profile=match_runtime_profile(parts,*n,&nand_id,&observed_ecc,&observed_mtd_oob);
    if(!nand_profile){add_check("nand_catalog","blocked","实际NAND身份/几何未命中runtime-read格式目录；不是独立安装或BC写资格");return -1;}
    uint64_t capacity_bytes=(uint64_t)nand_id.capacity_mib*1024ULL*1024ULL;
    if(capacity_bytes>UINT32_MAX||capacity_bytes!=nand_profile->capacity_bytes||(uint64_t)nand_id.erase_kib*1024ULL!=nand_profile->erase_bytes||nand_id.page_bytes!=nand_profile->page_bytes){add_check("nand_identity","blocked","日志几何与catalog profile不完全一致");return -1;}
    hash_nand_identity(&nand_id,g_nand_identity_hash);if(g_bound_nand_identity_hash[0]&&strcmp(g_nand_identity_hash,g_bound_nand_identity_hash)){add_check("nand_identity","blocked","自初次预检以来NAND身份/探测几何变化");return -1;}if(!g_bound_nand_identity_hash[0])snprintf(g_bound_nand_identity_hash,sizeof(g_bound_nand_identity_hash),"%s",g_nand_identity_hash);
#if !BI_TEST
    if(!BI_NAND_CATALOG_SHA256[0]){add_check("nand_catalog","blocked","候选没有绑定共用NAND catalog摘要与资格元数据；拒绝旧候选");return -1;}
#endif
    add_check("nand_catalog_digest","pass",BI_NAND_CATALOG_SHA256);
    if(profile_geometry(parts,*n,nand_profile,observed_ecc,observed_mtd_oob,sbl,env,art,bc0,bc1,app0,app1)){add_check("mtd_map","blocked","MTD几何/ECC与精确catalog profile不匹配，或NX30 D物理分区映射存在歧义");return -1;}
    add_check("mtd_map","pass","MTD 动态身份、物理映射、几何与精确 catalog profile 一致");
    add_check("nand_identity","pass","JEDEC、完整型号、容量、页、擦除、原始OOB及MTD ECC profile精确匹配");add_check("nand_catalog","pass",nand_profile->profile_id);
    add_check("sbl1_observed","pass","实际SBL物理范围已绑定；退出/安装分别核验其可信合同");
    const char*env_structure_reason=NULL;
    if(verify_common_env(*env,&env_structure_reason)){add_check("env_structure","blocked",env_structure_reason);return -1;}
    add_check("env_structure","pass","ENV CRC/完整字段结构已核验；不从ENV字符串判硬件等级");
    *bc0_before=malloc(BC_BYTES);*bc1_before=malloc(BC_BYTES);if(!*bc0_before||!*bc1_before||read_all((*bc0)->fd,*bc0_before,BC_BYTES,0)||read_all((*bc1)->fd,*bc1_before,BC_BYTES,0)){add_check("bootconfig","blocked","BOOTCONFIG 完整读取失败");return -1;}
    uint32_t a0,s0,r0,a1,s1,r1;if(parse_bc(*bc0_before,&a0,&s0,&r0)||parse_bc(*bc1_before,&a1,&s1,&r1)||a0!=a1||s0!=s1||r0!=r1||a0==UINT32_MAX){add_check("bootconfig","blocked","两份 BC 结构损坏、AGE/selector不一致或计数溢出");return -1;}*old_age=a0;*old_sel=s0;*root_sel=r0;if(!g_oem_bound){memcpy(g_oem_bc_before,*bc0_before,BC_BYTES);memcpy(g_oem_bc1_before,*bc1_before,BC_BYTES);}add_check("bootconfig","pass","两份BC结构和目标字段一致；逐副本保留各自未修改字节");
    bi_part*active=(*old_sel==0)?*app0:*app1;bi_part*inactive=(*old_sel==0)?*app1:*app0;
    const char*active_expected=strstr(active->name,"0:")==active->name?"0:APPSBL":"APPSBL";
    const char*inactive_expected=strstr(inactive->name,"0:")==inactive->name?"0:APPSBL_1":"APPSBL_1";
    if(strcmp(active->name,active_expected)||strcmp(inactive->name,inactive_expected)){add_check("active_appsbl","blocked","BC selector 与 APPSBL/APPSBL_1 当前逻辑名称映射不符");return -1;}
    if(target_has_badblock(*bc0)||target_has_badblock(*bc1)){add_check("write_targets","blocked","任一 BOOTCONFIG 含坏块；拒绝跳块/擦写");return -1;}
    add_check("write_targets","pass","两份 BOOTCONFIG 坏块扫描通过；未据此授权写入");
    uint8_t*active_bytes=malloc(APPSBL_BYTES);if(!active_bytes)return -1;if(read_all(active->fd,active_bytes,APPSBL_BYTES,0)){free(active_bytes);add_check("active_appsbl","blocked","活动 APPSBL 全量读取失败");return -1;}char active_hash[65];hash_hex(active_bytes,APPSBL_BYTES,active_hash);snprintf(g_active_hash,sizeof(g_active_hash),"%s",active_hash);free(active_bytes);add_check("active_appsbl","pass","BC selector 与物理映射一致；已读取完整APPSBL摘要供可信版本决策");snprintf(g_target_name,sizeof(g_target_name),"%s",inactive->name);g_target_off=inactive->off;
    g_stock_baseline=lookup_stock_baseline(g_active_hash);
    add_check("oem_slot","checking","完整测量OEM与运行桥接槽；此步骤包含全槽读取与哈希校验");
    if(verify_oem_slot(parts,*n,*root_sel,1)){add_check("oem_slot","blocked",g_oem_reason);return -1;}
    g_oem_records_verified=1;add_check("oem_slot","pass",g_oem_reason);
#if defined(BRIDGE_RETURN_ONLY) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(!g_return_source_record){add_check("runtime_source","blocked","原厂route尚未绑定到编译source registry");return -1;}
    add_check("runtime_source","pass",g_return_source_record->source_revision_sha256);
#endif
#if defined(BI_RETURN_SOURCE_REGISTRY_V2) && BI_HAVE_RETURN_SOURCE_REGISTRY
    if(!g_return_source_record||
       strcmp(g_return_source_record->official_sha256,BI_RETURN_SOURCE_OFFICIAL_SHA256)||
       strcmp(g_return_source_record->stock_kernel_sha256,BI_RETURN_SOURCE_STOCK_KERNEL_SHA256)||
       strcmp(g_return_source_record->bridge_kernel_sha256,BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256)||
       strcmp(g_return_source_record->source_revision_sha256,BI_RETURN_SOURCE_REVISION_SHA256)||
       strcmp(g_return_source_record->compressed_kernel_sha256,BI_RETURN_SOURCE_COMPRESSED_KERNEL_SHA256)||
       strcmp(g_return_source_record->nand_dt_sha256,BI_RETURN_SOURCE_NAND_DT_SHA256)||
       strcmp(g_return_source_record->route,BI_RETURN_SOURCE_ROUTE)||
       strcmp(g_return_source_record->compatible,BI_RETURN_SOURCE_COMPATIBLE)||
       g_return_source_record->product!=BI_RETURN_SOURCE_PRODUCT){
        add_check("runtime_source","blocked","provider source registry is not its single exact compiled candidate route");return -1;}
#endif
    (void)qualify_oem_return(*sbl,*env,*bc0);
    add_check("oem_return_contract",g_return_ready?"pass":"unavailable",g_return_reason);
    add_check("protected_snapshot","checking","全量读取启动保护区并绑定事务快照");
    if(protected_snapshot(parts,*n)){add_check("protected_snapshot","blocked","启动保护区无法全量只读快照");return -1;}
    if(memcmp((*bc0)->snapshot,*bc0_before,BC_BYTES)||memcmp((*bc1)->snapshot,*bc1_before,BC_BYTES)){add_check("bootconfig","blocked","启动记录在结构读取与完整快照绑定之间变化");g_oem_records_verified=0;return -1;}
    {char fresh[65];snapshot_fingerprint(parts,*n,fresh);if(g_bound_fingerprint_valid){if(strcmp(fresh,g_bound_fingerprint)){add_check("protected_snapshot","blocked","自预检以来启动保护区或物理映射变化");return -1;}}else{snprintf(g_bound_fingerprint,sizeof(g_bound_fingerprint),"%s",fresh);g_bound_fingerprint_valid=1;}}
    add_check("protected_snapshot","pass","安装前保护区快照已保存在 RAM，安装时会重新全量比较");
    /* Only init publishes the optional backup. The worker never truncates a path a concurrent GET may be reading. */
    if(g_inspection_mode)add_check("backup","suppressed","只读候选检查跳过可选的完整恢复备份");
    else if(!g_started&&!g_backup_available){g_backup_size=0;g_backup_hash[0]=0;if(prepare_backup(parts,*n)==0)add_check("backup","available","完整45MiB恢复ZIP已冻结并完整读回校验；不含系统槽、尾部或OOB/ECC");else add_check("backup","optional","完整45MiB恢复ZIP不可用；没有发布部分备份，备份可跳过且安装门槛独立执行");}
    else if(g_backup_available)add_check("backup","available","安装事务保留首次预检生成的只读备份文件");
    else add_check("backup","optional","首次预检未能生成可下载备份；安装硬门槛独立通过");
    return 0;
}

static void install_unavailable(const char*name,const char*reason){
#if !BI_TEST
    g_first_install_qualified=0;
    g_first_install_record=NULL;
    g_first_install_profile=NULL;
#endif
    /* Qualification keeps going after a failure so one boot reports every
     * blocker; the first one stays the headline reason. */
    int first=!g_install_rejected_check[0];
    if(first){
        snprintf(g_install_rejected_check,sizeof(g_install_rejected_check),"%s",name);
        snprintf(g_install_rejected_reason,sizeof(g_install_rejected_reason),"%s",reason);
    }
    add_check(name,"unavailable",reason);if(first&&!g_return_ready)snprintf(g_blocked,sizeof(g_blocked),"%s",reason);
}
/* Observe the actual Linux MTD read path. This adds no SBL/load-format grant.
 * The complete reads must still match this transaction's physical snapshots.
 * Regular-file installer fixtures exercise transaction behavior separately;
 * bridge_mtd_observer tests exercise the same production syscall path. */
static int observe_install_appsbl(bi_part *parts,size_t n,struct bmo_appsbl_pair_report *out){
#if BI_TEST
    (void)parts;(void)n;(void)out;
    add_check("appsbl_read_observation","synthetic","普通文件事务夹具；真实MTD采集由独立syscall夹具验证");
    return 0;
#else
    bi_part *copies[2]={NULL,NULL};struct bmo_appsbl_pair_report report;
    char current_boot[80],expected[65],detail[256];
    for(size_t i=0;i<n;i++){
        int index=parts[i].off==APPSBL_LO?0:parts[i].off==APPSBL_HI?1:-1;
        if(index<0)continue;
        if(copies[index]){install_unavailable("appsbl_read_observation","APPSBL物理范围重复");return -1;}
        copies[index]=&parts[i];
    }
    for(size_t i=0;i<2;i++)if(!copies[i]||copies[i]->size!=APPSBL_BYTES||
       copies[i]->page!=PAGE_BYTES||copies[i]->erase!=ERASE_BYTES||!copies[i]->snapshot){
        install_unavailable("appsbl_read_observation","两份APPSBL物理范围/几何或完整快照缺失");return -1;
    }
    if(text_file("proc/sys/kernel/random/boot_id",current_boot,sizeof(current_boot))){install_unavailable("appsbl_read_observation","无法读取当前启动会话");return -1;}
    current_boot[strcspn(current_boot,"\r\n")]=0;
    if(strcmp(current_boot,g_boot_id)){install_unavailable("appsbl_read_observation","采集启动会话与预检不一致");return -1;}
    int rc=bmo_observe_appsbl_pair(copies[0]->fd,copies[0]->dev,
        copies[1]->fd,copies[1]->dev,PAGE_BYTES,ERASE_BYTES,&report);
    /* NON_CLEAN also covers corrected-only bitflips; those stay installable.
     * Uncorrectable reads and unsupported ECC counters still refuse. */
    int ecc_ok=1;unsigned corrected[2]={0,0};
    for(size_t i=0;i<2;i++){
        const struct bmo_partition_report *r=&report.appsbl[i];
        if(r->ecc_stats_state!=BMO_ECC_STATS_SUPPORTED||r->ecc_uncorrected_before||r->ecc_uncorrected_after)ecc_ok=0;
        corrected[i]=r->ecc_corrected_after;
    }
    if((rc!=BMO_RESULT_OK&&rc!=BMO_RESULT_NON_CLEAN)||!report.observation_complete||!report.binding_valid||!ecc_ok){
        snprintf(detail,sizeof(detail),"MTD逐页观察未通过：%.160s",report.reason);
        install_unavailable("appsbl_read_observation",detail);return -1;
    }
    if(corrected[0]||corrected[1]){
        snprintf(detail,sizeof(detail),"两份APPSBL自开机累计已纠正位翻转 %u / %u，均在ECC纠错能力内，不影响安装",corrected[0],corrected[1]);
        add_check("appsbl_ecc","pass",detail);
    }
    for(size_t i=0;i<2;i++){
        hash_hex(copies[i]->snapshot,APPSBL_BYTES,expected);
        if(report.appsbl[i].observed_oob_bytes!=copies[i]->oob||strcmp(expected,report.appsbl[i].sha256)){
            install_unavailable("appsbl_read_observation","逐页读取的OOB几何或完整摘要与物理快照不一致");return -1;
        }
    }
    if(text_file("proc/sys/kernel/random/boot_id",current_boot,sizeof(current_boot))){install_unavailable("appsbl_read_observation","无法复核采集后的启动会话");return -1;}
    current_boot[strcspn(current_boot,"\r\n")]=0;
    if(strcmp(current_boot,g_boot_id)){install_unavailable("appsbl_read_observation","采集期间启动会话变化");return -1;}
    *out=report;
    add_check("appsbl_read_observation","pass","两份各640页、坏块与ECC计数、完整摘要和物理快照一致；不替代前级加载资格");
    return 0;
#endif
}
/* The shared collector binds profile-declared physical ranges to this
 * preflight's live MTD descriptors and protected snapshots. It gathers facts
 * only; candidate, OEM route, and ENV policy remain in the D wrapper. */
static bi_part *profile_part_at(bi_part *parts,size_t n,unsigned offset){
    bi_part *found=NULL;
    for(size_t i=0;i<n;i++)if(parts[i].off==offset){if(found)return NULL;found=&parts[i];}
    return found;
}
static int profile_part_matches(const bfi_install_profile *p,const bi_part *part,unsigned bytes,int require_snapshot){
    return part&&(!require_snapshot||part->snapshot)&&part->size==bytes&&part->page==p->page&&part->erase==p->erase&&
        bfi_install_profile_accepts_mtd_oob(p,part->oob)&&part->ecc==p->storage_ecc_bits;
}
static int __attribute__((unused)) collect_install_facts(const bfi_install_profile *p,bi_part *parts,size_t n,
                                 bi_part *sbl,const bi_nand_identity *id,
                                 const struct bmo_appsbl_pair_report *pages,bfi_facts *f){
    bi_part *bc[2]={NULL,NULL},*active,*target,*bridge,*oem;
    uint32_t age[2],sel[2],root[2];
    if(!p||!id||!pages||!f||!sbl||p->sbl_bytes>p->sbl_partition_bytes||
       !profile_part_matches(p,sbl,p->sbl_partition_bytes,1)||
       sbl->off!=p->sbl_offset)return -1;
    active=profile_part_at(parts,n,p->active_offset);target=profile_part_at(parts,n,p->target_offset);
    bridge=profile_part_at(parts,n,p->bridge_offset);oem=profile_part_at(parts,n,p->oem_offset);
    for(size_t i=0;i<n;i++){
        if(parts[i].off==BC0_OFF){if(bc[0])return -1;bc[0]=&parts[i];}
        if(parts[i].off==BC1_OFF){if(bc[1])return -1;bc[1]=&parts[i];}
    }
    if(!profile_part_matches(p,active,p->target_size,1)||!profile_part_matches(p,target,p->target_size,1)||
       !profile_part_matches(p,bridge,p->bridge_bytes,0)||!profile_part_matches(p,oem,p->oem_bytes,0)||
       !bc[0]||!bc[1]||!bc[0]->snapshot||!bc[1]->snapshot||bc[0]->size!=BC_BYTES||bc[1]->size!=BC_BYTES||
       !bc[0]->erase||bc[0]->erase!=bc[1]->erase)return -1;
    memset(f,0,sizeof(*f));
    snprintf(f->boot_before,sizeof(f->boot_before),"%s",g_boot_id);
    if(text_file("proc/sys/kernel/random/boot_id",f->boot_after,sizeof(f->boot_after)))return -1;
    f->boot_after[strcspn(f->boot_after,"\r\n")]=0;
    if(id->manufacturer>255||id->chip_id>255||strlen(id->model)>=sizeof(f->model))return -1;
    strcpy(f->model,id->model);f->manufacturer=id->manufacturer;f->chip_id=id->chip_id;
    f->capacity_mib=id->capacity_mib;f->raw_spare=id->raw_oob_bytes;
    f->page=active->page;f->erase=active->erase;f->linux_ecc=active->ecc;f->observed_mtd_oob=active->oob;
    f->sbl_bytes=p->sbl_bytes;hash_hex(sbl->snapshot,f->sbl_bytes,f->sbl_sha256);
    f->sbl_tail_ff=bytes_are_ff(sbl->snapshot+f->sbl_bytes,sbl->size-f->sbl_bytes);
    for(unsigned i=0;i<2;i++)if(parse_bc(bc[i]->snapshot,&age[i],&sel[i],&root[i]))return -1;
    f->bc_equal_valid=age[0]==age[1]&&sel[0]==sel[1]&&root[0]==root[1]&&
        age[0]!=UINT32_MAX&&!memcmp(bc[0]->snapshot,bc[1]->snapshot,BC_BYTES);
    f->bc_appsbl=sel[0];f->bc_current_rootfs=root[0];
    /* The descriptor fixes the expected active/target selector. A mismatch
     * maps to the opposite declared range and is then rejected by the matcher. */
    unsigned selected_active=(sel[0]==p->bc_appsbl)?p->active_offset:p->target_offset;
    unsigned selected_target=(selected_active==p->active_offset)?p->target_offset:p->active_offset;
    bi_part *actual_active=profile_part_at(parts,n,selected_active);
    if(!profile_part_matches(p,actual_active,p->target_size,1))return -1;
    hash_hex(actual_active->snapshot,actual_active->size,f->active_sha256);
    f->active_offset=selected_active;f->target_offset=selected_target;f->target_size=p->target_size;
    f->bridge_offset=bridge->off;f->bridge_bytes=bridge->size;
    f->oem_offset=oem->off;f->oem_selector=g_oem_selector;f->oem_bytes=oem->size;
    f->bridge_and_oem_verified=g_common_bound&&g_oem_bound&&g_oem_verified&&g_oem_records_verified&&
        g_oem_bridge_off==p->bridge_offset&&g_oem_offset==p->oem_offset&&
        bridge->off==p->bridge_offset&&bridge->size==p->bridge_bytes&&
        oem->off==p->oem_offset&&oem->size==p->oem_bytes;
    f->pages=*pages;
    return 0;
}

#if !BI_TEST
/* Facts are shared-profile observations; this wrapper retains the reviewed D
 * candidate, OEM source-route, QFPROM, and ENV policy without broadening it. */
static int __attribute__((unused)) qualify_d_first_install(bi_part *parts,size_t n,bi_part *sbl,bi_part *env,
                                  const bi_nand_identity *id,const uint8_t *payload,
                                  const struct bmo_appsbl_pair_report *pages){
    const bfi_install_profile *profile=bfi_d_install_profile();
    if(BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE!=1||!BI_FIRST_INSTALL_RECORD[0]){
        install_unavailable("first_install_record","构建未绑定精确D候选record名称与已审核来源");return -1;}
    bfi_facts f;
    if(collect_install_facts(profile,parts,n,sbl,id,pages,&f)){
        install_unavailable("first_install_record","profile物理分区/只读快照事实采集不完整或不匹配");return -1;}
    f.r012_route_verified=!strcmp(BI_RETURN_SOURCE_OFFICIAL_SHA256,
        "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922")&&
        !strcmp(BI_RETURN_SOURCE_STOCK_KERNEL_SHA256,
        "5878e39213f37fb3d26b51a5197e09b05e7de18ee275fd7f430ea6eac5e7dc85")&&
        !strcmp(BI_RETURN_SOURCE_ROUTE,"config@mp02.1")&&
        !strcmp(BI_RETURN_SOURCE_COMPATIBLE,"qcom,ipq5018-mp02.1");
    f.unsigned_boot_allowed=verify_qfprom_authenticate()==0;
    f.env_bootargs_verified=env->snapshot&&bfi_d_env_bootargs(env->snapshot,env->size);
    f.declared_load_ecc=BI_APPSBL_LOAD_ECC_BITS;f.declared_storage_ecc=BI_APPSBL_STORAGE_ECC_BITS;
    const bfi_d_candidate_record *selected=NULL;
    const char *reason=bfi_check_d_first_install_record(&f,payload,APPSBL_BYTES,&selected);
    if(reason){char detail[160];snprintf(detail,sizeof(detail),"精确D首次安装组合不匹配：%s",reason);
        install_unavailable("first_install_record",detail);return -1;}
    if(!selected||strcmp(BI_FIRST_INSTALL_RECORD,selected->name)){
        install_unavailable("first_install_record","精确候选虽有已知record，但与本构建BI_FIRST_INSTALL_RECORD不匹配");return -1;}
    if(g_version.decision!=BV_FIRST_INSTALL){install_unavailable("first_install_record","本记录只覆盖保留OEM HIGH的首次安装");return -1;}
    g_first_install_record=selected;
    g_first_install_qualified=1;
    {char detail[256];snprintf(detail,sizeof(detail),"%s；%s经验加载记录匹配，当前PBL选档未由Linux观测",
        selected->name,selected->native_qualified?"":"受控trial候选(native_qualified=0)，");
        add_check("first_install_record","pass",detail);}
    return 0;
}

#if BI_HAVE_INSTALL_PROFILE
static int qualify_compiled_first_install(bi_part *parts,size_t n,bi_part *sbl,bi_part *env,
                                         const bi_nand_identity *id,const uint8_t *payload,
                                         const struct bmo_appsbl_pair_report *pages){
    const bfi_board_record *board=bfi_board_candidate_record(BI_HARDWARE_CLASS);
    const bfi_candidate_record *candidate=bfi_board_candidate_select(BI_HARDWARE_CLASS,payload,APPSBL_BYTES);
    bi_part *bc=profile_part_at(parts,n,BC0_OFF);
    uint32_t current_age=0,current_app=0,current_root=0;
    if(!bc||!bc->snapshot||parse_bc(bc->snapshot,&current_age,&current_app,&current_root)){
        install_unavailable("bootconfig","无法从当前完整BC核验APPSBL与系统槽方向");return -1;
    }
    const bfi_install_profile *selected_profile=bfi_select_install_profile(
        bootbridge_first_install_profiles,BI_FIRST_INSTALL_PROFILE_COUNT,
        g_oem_bridge_off,g_oem_offset,g_oem_selector,current_root,current_app);
    bfi_install_profile profile;
    const bfi_candidate_record *const candidates[]={candidate};
    bfi_facts facts;char cmdline[2048];
    /* ENV/platform identity depends only on the candidate, so report it
     * alongside any profile mismatch below instead of hiding behind it. */
    bii_code identity=BII_POLICY;int identity_failed=0;
    if(candidate){
        if(text_file("proc/cmdline",cmdline,sizeof(cmdline))||!env->snapshot){
            install_unavailable("install_identity","无法读取当前启动参数或完整ENV快照");identity_failed=1;
        }else{
            identity=check_current_candidate_identity(env->snapshot,env->size,cmdline,candidate->partition_sha256);
            if(identity!=BII_OK){install_unavailable("install_identity",identity_reason(identity));identity_failed=1;}
        }
    }
    if(BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE!=1||!board||!candidate||!selected_profile||
       strcmp(candidate->name,BI_FIRST_INSTALL_RECORD)||board->machid!=BI_EXPECTED_MACHID||
       selected_profile->candidates||selected_profile->candidate_count){
        install_unavailable("first_install_record","编译板类、候选、方向profile或桥接/OEM实测槽位不匹配");return -1;
    }
    profile=*selected_profile;
    if(profile.page!=board->page||profile.erase!=board->erase||
       profile.capacity_mib!=board->capacity_mib||profile.raw_spare!=board->raw_spare||
       profile.mtd_oob!=board->mtd_oob||profile.linux_ecc!=board->observed_linux_ecc||
       strcmp(profile.model,board->nand_model)||
       (board->jedec_bytes_observed>=2&&(profile.manufacturer!=board->manufacturer||profile.chip_id!=board->chip_id))||
       profile.sbl_offset!=0||profile.sbl_partition_bytes!=0x80000u||
       profile.sbl_bytes!=BI_SBL_VALID_BYTES||strcmp(profile.sbl_sha256,BI_SBL_SHA256)||
       strcmp(profile.active_oem_sha256,BI_EXPECTED_OEM_APPSBL_SHA256)||
       profile.active_offset!=(current_app?APPSBL_HI:APPSBL_LO)||
       profile.target_offset!=(current_app?APPSBL_LO:APPSBL_HI)||
       profile.target_size!=APPSBL_BYTES||profile.startup_bytes!=STARTUP_END||
       profile.bc_appsbl!=current_app||profile.bc_current_rootfs!=current_root||
       profile.bridge_bytes!=0x2800000u||profile.oem_bytes!=0x2800000u){
        install_unavailable("first_install_record","编译板类、候选、原厂启动链或物理布局绑定冲突");return -1;
    }
    profile.candidates=candidates;profile.candidate_count=1;
    if(collect_install_facts(&profile,parts,n,sbl,id,pages,&facts)){
        install_unavailable("first_install_record","当前分区、保护快照或NAND事实不符合编译安装组合");return -1;
    }
    /* Overwrite install: bind the running APPSBL that the version decision saw
     * instead of the OEM one. Every other NAND/SBL/BC/slot/page gate below
     * still applies unchanged. */
    if(g_version.decision==BV_OVERRIDE_UNKNOWN){
        if(strcmp(facts.active_sha256,g_active_hash)){
            install_unavailable("first_install_record","覆盖安装：活动APPSBL与版本判定时的内容不一致");return -1;
        }
        profile.active_oem_sha256=facts.active_sha256;
    }
    facts.route_contract_verified=
        !strcmp(BI_FIRST_INSTALL_SOURCE_SHA256,BI_RETURN_SOURCE_OFFICIAL_SHA256)&&
        !strcmp(BI_FIRST_INSTALL_ROUTE,BI_RETURN_SOURCE_ROUTE)&&
        !strcmp(BI_FIRST_INSTALL_COMPATIBLE,BI_RETURN_SOURCE_COMPATIBLE)&&
        BI_FIRST_INSTALL_PRODUCT==BI_RETURN_SOURCE_PRODUCT&&
        g_oem_pool_match.product==BI_RETURN_SOURCE_PRODUCT&&
        BI_FIRST_INSTALL_OEM_EXIT_POLICY==BOS_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V1&&
        g_oem_pool_match.exit_policy==BI_FIRST_INSTALL_OEM_EXIT_POLICY;
    facts.unsigned_boot_allowed=verify_qfprom_authenticate()==0;
    facts.declared_load_ecc=BI_APPSBL_LOAD_ECC_BITS;
    facts.declared_storage_ecc=BI_APPSBL_STORAGE_ECC_BITS;
    facts.env_bootargs_verified=identity==BII_OK;
    if(identity_failed)return -1;
    const char *reason=bfi_match_install_profile(&profile,candidate,&facts);
    if(reason){install_unavailable("first_install_record",reason);return -1;}
    if(g_version.decision!=BV_FIRST_INSTALL&&g_version.decision!=BV_OVERRIDE_UNKNOWN){
        install_unavailable("first_install_record","当前路径仅允许首次安装或覆盖安装");return -1;
    }
    g_first_install_record=candidate;g_first_install_profile=selected_profile;g_first_install_qualified=1;
    add_check("first_install_record","pass",candidate->name);
    add_check("install_identity","pass","显式板类候选、原厂产品及平台参数一致；不据此自动识别PCB或声称实机验收");
    return 0;
}
#endif
#endif

static void qualify_install(bi_part*parts,size_t n,bi_part*sbl,bi_part*env,bi_part*inactive,uint8_t**payload){
    bi_nand_identity id;uint32_t ecc=0,oob=0;char hash[65];const char*why=NULL;
    g_install_ready=0;*payload=NULL;
    g_install_rejected_check[0]=0;g_install_rejected_reason[0]=0;
#if !BI_TEST
    g_first_install_qualified=0;
    g_first_install_record=NULL;
    g_first_install_profile=NULL;
#endif
    /* Every read-only check runs even after an earlier one failed, so the
     * diagnostics list all blockers at once. Checks that need an earlier
     * result (NAND identity, payload, page report) run only when it exists.
     * Any failure still leaves g_install_ready at 0. */
    int ok=1,have_id=0,have_payload=0,have_pages=0;
    if(verify_qfprom_authenticate()){install_unavailable("qfprom_authenticate","qfprom非明确0：未签名候选安装关闭；不替代原厂退出合同");ok=0;}
    else add_check("qfprom_authenticate","pass","未签名候选安装所需qfprom明确0");
    if(read_nand_probe_identity(&id)||!match_nand_profile(parts,n,&id,&ecc,&oob)){
        install_unavailable("candidate_format","candidate格式名单/读格式条件不匹配；不代表独立stage资格");ok=0;}
    else have_id=1;
    if(verify_sbl(sbl)){install_unavailable("sbl1","候选安装SBL内容合同未通过");ok=0;}
#if !BI_HAVE_INSTALL_PROFILE
    if(parse_env_product(env,&why)){install_unavailable("appsblenv",why);ok=0;}
    else{char detail[256];snprintf(detail,sizeof(detail),"候选安装ENV产品 %.64s / CRC条件通过；不作为硬件资格",why&&why[0]?why:"有效唯一产品名");add_check("appsblenv","pass",detail);}
    if(verify_cmdline_machid()){install_unavailable("machid","候选安装平台字段冲突；不据machid推断硬件等级");ok=0;}
    else add_check("machid","pass","候选平台字段无冲突；不据此区分B/D");
#else
    (void)why; /* Board/source identity is checked after exact candidate binding. */
#endif
    if(target_has_badblock(inactive)){install_unavailable("candidate_target","非活动APPSBL含坏块；安装关闭");ok=0;}
    if(read_payload(payload,hash)||strcmp(hash,g_candidate_hash)){
        install_unavailable("candidate_payload","候选文件缺失/大小/hash不匹配；原厂退出独立核验");ok=0;}
    else if(g_bound_payload_hash[0]&&strcmp(g_bound_payload_hash,hash)){install_unavailable("candidate_payload","候选文件在本次会话变化");ok=0;}
    else{
        if(!g_bound_payload_hash[0])snprintf(g_bound_payload_hash,sizeof(g_bound_payload_hash),"%s",hash);
        add_check("candidate_payload","pass","候选文件全分区大小/hash与编译构建ID一致");
        have_payload=1;
    }
    g_version=evaluate_version(g_active_hash,g_candidate_hash);
    if(g_version.decision!=BV_FIRST_INSTALL&&g_version.decision!=BV_UPGRADE&&g_version.decision!=BV_SKIP_IDENTICAL&&
       g_version.decision!=BV_OVERRIDE_UNKNOWN){
        install_unavailable("trusted_version",version_reason(g_version.decision));ok=0;}
    else add_check("trusted_version","pass",version_reason(g_version.decision));
    struct bmo_appsbl_pair_report pages;memset(&pages,0,sizeof(pages));
    if(observe_install_appsbl(parts,n,&pages))ok=0;
    else have_pages=1;
#if !BI_TEST
    if(have_id&&have_payload&&have_pages){
#if BI_HAVE_INSTALL_PROFILE
        if(qualify_compiled_first_install(parts,n,sbl,env,&id,*payload,&pages))ok=0;
#else
        if(qualify_d_first_install(parts,n,sbl,env,&id,*payload,&pages))ok=0;
#endif
    }
#else
    (void)have_id;(void)have_payload;(void)have_pages;
#endif
#if !BI_HAVE_INSTALL_PROFILE
    if(g_oem_pool_match.exit_policy!=BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0||g_oem_pool_match.product!=BOS_PRODUCT_NX30){install_unavailable("oem_install_handoff","候选对应OEM加载策略未知");ok=0;}
#endif
    if(!ok){
#if !BI_TEST
        /* A record may have matched while an independent check failed. */
        g_first_install_qualified=0;g_first_install_record=NULL;g_first_install_profile=NULL;
#endif
        return;
    }
    g_install_ready=install_chain_qualified()&&oem_boot_contract(g_candidate_hash,1);
    add_check("install_chain_qualification",install_chain_qualified()?"pass":"unavailable",install_chain_qualified()?"当前会话与精确首次安装记录匹配（合成测试另标）":"首次安装组合不匹配；安装关闭");
    add_check("oem_boot_contract",oem_boot_contract(g_candidate_hash,1)?"pass":"unavailable",oem_boot_contract(g_candidate_hash,1)?"候选默认原厂系统/维护入口合同已绑定":"候选默认系统/维护入口合同缺失；原厂退出独立判断");
    if(!g_install_ready)install_unavailable("oem_boot_contract","首次安装资格或候选默认系统/维护入口合同未通过");
}
static int discover_preflight(bi_part*parts,size_t*n,bi_part**sbl,bi_part**env,bi_part**art,bi_part**bc0,bi_part**bc1,bi_part**app0,bi_part**app1,uint8_t**payload,uint8_t**before0,uint8_t**before1,uint32_t*age,uint32_t*app,uint32_t*root){
    if(discover_common(parts,n,sbl,env,art,bc0,bc1,app0,app1,payload,before0,before1,age,app,root))return -1;
    if(!g_oem_return_mode)qualify_install(parts,*n,*sbl,*env,*app==0?*app1:*app0,payload);
    return 0;
}

static void operation_ticket(char out[65]){
    static const char domain[]="BOOTBRIDGE-OPERATION-TICKET-V1";bi_sha256_ctx c;uint8_t d[32],op[4];
    bi_sha256_init(&c);bi_sha256_update(&c,domain,sizeof(domain));put_le32(op,(uint32_t)g_bound_operation);bi_sha256_update(&c,op,4);
    bi_sha256_update(&c,g_bound_fingerprint,strlen(g_bound_fingerprint)+1);bi_sha256_update(&c,g_token,strlen(g_token)+1);
    bi_sha256_final(&c,d);bi_sha256_hex(d,out);
}
static int operation_ticket_valid(void){char actual[65];if(g_bound_operation==BI_OP_NONE||!g_operation_ticket[0])return 0;operation_ticket(actual);return !strcmp(actual,g_operation_ticket);}

static int write_appsbl(bi_part *target,const uint8_t*payload){
    if(part_reopen_write(target))return -1;
    uint8_t*preimage=malloc(target->size);
    if(!preimage)return -1;
    int changed=!target->snapshot||read_all(target->fd,preimage,target->size,0)||memcmp(preimage,target->snapshot,target->size);
    free(preimage);if(changed){errno=ESTALE;return -1;}
    for(uint32_t o=0;o<target->size;o+=ERASE_BYTES)if(badblock(target,o)){errno=EIO;return -1;}
    if(set_state("installing","appsbl_erase","正在擦除经 selector 确认的非活动 APPSBL"))return -1;
#if BI_TEST
    if(!strcmp(fixture_fault(),"before_erase"))return -1;
#endif
    g_modified=1;if(write_status())return -1;
    if(erase_part(target))return -1;
    if(set_state("installing","appsbl_write","正在逐页写入内置 APPSBL"))return -1;
#if BI_TEST
    if(!strcmp(fixture_fault(),"partial_write")){(void)write_all(target->fd,payload,2*PAGE_BYTES,0);return -1;}
#endif
    for(uint32_t o=0;o<APPSBL_BYTES;o+=ERASE_BYTES){if(write_all(target->fd,payload+o,ERASE_BYTES,o))return -1;g_progress=o+ERASE_BYTES;if(write_status())return -1;}
    if(set_state("installing","appsbl_readback","正在关闭并重新发现 APPSBL，执行完整分区读回比较"))return -1;
    if(part_close(target))return -1;
    bi_part check;
    if(part_open(target->index,&check,0))return -1;
    int same=check.dev==target->dev&&check.off==target->off&&check.size==target->size;target->fd=check.fd;check.fd=-1;if(!same)return -1;
    uint8_t*back=malloc(APPSBL_BYTES);if(!back)return -1;if(read_all(target->fd,back,APPSBL_BYTES,0)||memcmp(back,payload,APPSBL_BYTES)){free(back);return -1;}free(back);return 0;
}
static void make_bc_candidate(uint8_t*out,const uint8_t*before,uint32_t age,uint32_t selector,uint32_t rootfs){memcpy(out,before,BC_BYTES);put_le32(out+4,age+1);put_le32(out+0x58,selector);put_le32(out+0x80,rootfs);}
static int write_bc(bi_part*p,const uint8_t*pre,const uint8_t*candidate,const char*phase){
    if(!p||!pre||!candidate||!phase||p->fd<0||p->size!=BC_BYTES||p->erase!=ERASE_BYTES||p->page!=PAGE_BYTES){errno=EINVAL;return -1;}
    uint8_t*cur=malloc(BC_BYTES);if(!cur)return -1;if(read_all(p->fd,cur,BC_BYTES,0)||memcmp(cur,pre,BC_BYTES)){free(cur);errno=ESTALE;return -1;}free(cur);
    if(part_reopen_write(p))return -1;
    cur=malloc(BC_BYTES);if(!cur)return -1;if(read_all(p->fd,cur,BC_BYTES,0)||memcmp(cur,pre,BC_BYTES)){free(cur);errno=ESTALE;return -1;}free(cur);
    for(uint32_t o=0;o<p->size;o+=ERASE_BYTES)if(badblock(p,o)){errno=EIO;return -1;}
    char stage[64],msg[128];snprintf(stage,sizeof(stage),"%s_erase",phase);snprintf(msg,sizeof(msg),"正在提交 %s 激活记录",p->name);if(set_state("installing",stage,msg))return -1;
#if BI_TEST
    if(!strcmp(fixture_fault(),"bc1_before_erase")&&!strcmp(phase,"bootconfig1"))return -1;
    if(!strcmp(fixture_fault(),"bc0_before_erase")&&!strcmp(phase,"bootconfig0"))return -1;
#endif
    g_oem_records_verified=0;
    g_modified=1;if(write_status())return -1;if(erase_part(p))return -1;snprintf(stage,sizeof(stage),"%s_write",phase);if(set_state("installing",stage,msg))return -1;
#if BI_TEST
    if(!strcmp(fixture_fault(),"bc1_partial")&&!strcmp(phase,"bootconfig1")){(void)write_all(p->fd,candidate,PAGE_BYTES,0);return -1;}
#endif
    for(uint32_t o=0;o<p->size;o+=ERASE_BYTES){if(write_all(p->fd,candidate+o,ERASE_BYTES,o))return -1;g_progress+=ERASE_BYTES;if(write_status())return -1;}
    if(set_state("installing","bootconfig_readback","正在关闭并重新发现 BOOTCONFIG，执行完整分区读回比较"))return -1;
    if(part_close(p))return -1;
    bi_part ro;
    if(part_open(p->index,&ro,0))return -1;
    int same=ro.dev==p->dev&&ro.off==p->off&&ro.size==p->size;p->fd=ro.fd;ro.fd=-1;if(!same)return -1;
    uint8_t*back=malloc(p->size);if(!back)return -1;int ok=!read_all(p->fd,back,p->size,0)&&!memcmp(back,candidate,p->size);free(back);return ok?0:-1;
}

static void child_close_web_fds(void){long max=sysconf(_SC_OPEN_MAX);if(max<0||max>65536)max=65536;for(int fd=3;fd<max;fd++)close(fd);}
static int bc_candidate_valid(const uint8_t*before,uint8_t*candidate,uint32_t age,uint32_t appsbl,uint32_t rootfs){
    uint32_t a,s,r;if(parse_bc(candidate,&a,&s,&r)||a!=age+1||s!=appsbl||r!=rootfs)return -1;
    for(size_t k=0;k<BC_BYTES;k++)if(candidate[k]!=before[k]&&!((k>=4&&k<8)||(k>=0x58&&k<0x5c)||(k>=0x80&&k<0x84)))return -1;
    return 0;
}
static void child_transaction(bi_operation operation){
    child_close_web_fds();
#if BI_TEST
    if(!strcmp(fixture_fault(),"ticket_tamper"))g_bound_operation=operation==BI_OP_RETURN_OEM?BI_OP_INSTALL:BI_OP_RETURN_OEM;
#endif
    if(g_bound_operation!=operation||g_oem_return_mode!=(operation==BI_OP_RETURN_OEM)||!operation_ticket_valid()){
        g_can_reboot=0;(void)set_state("failed","operation_ticket_failed","操作类型与一次性票据绑定不符；未写入");_exit(1);
    }if(set_state("installing","recheck","重新发现物理槽、完整原厂内容及两份启动记录"))_exit(1);
#if BI_TEST
    if(!strcmp(fixture_fault(),"worker_exit"))_exit(99);
#endif
    bi_part parts[MAX_PARTS];memset(parts,0,sizeof(parts));for(size_t i=0;i<MAX_PARTS;i++)parts[i].fd=-1;
    size_t n=0;bi_part *sbl=NULL,*env=NULL,*art=NULL,*bc0=NULL,*bc1=NULL,*app0=NULL,*app1=NULL;
    uint8_t*payload=NULL,*old0=NULL,*old1=NULL,*c0=NULL,*c1=NULL;uint32_t age=0,sel=0,rootfs=0;
    int failed=1;char why[192]="原厂内容、启动保护区或物理映射变化";g_nchecks=0;
    if(discover_preflight(parts,&n,&sbl,&env,&art,&bc0,&bc1,&app0,&app1,&payload,&old0,&old1,&age,&sel,&rootfs)){
        if(g_blocked[0])snprintf(why,sizeof(why),"%.191s",g_blocked);
        goto done;
    }
    if(g_oem_return_mode){
        if(!g_return_ready||g_stock_baseline!=1||!oem_boot_contract(g_active_hash,0)){snprintf(why,sizeof(why),"当前APPSBL不是精确信任的原厂基线；拒绝退出");goto done;}
    }else if(!g_install_ready||!install_chain_qualified()||!oem_boot_contract(g_candidate_hash,1)){
        snprintf(why,sizeof(why),"安装独立资格或候选默认系统/维护入口合同不可用");goto done;
    }
    if(!n||snapshots_unchanged(parts,n)||verify_oem_slot(parts,n,rootfs,0))goto done;
    int skip_appsbl=g_oem_return_mode||g_version.decision==BV_SKIP_IDENTICAL;
    uint32_t next_appsbl=skip_appsbl?sel:1-sel;
    bi_part*target=(sel==0)?app1:app0;
    if(operation==BI_OP_INSTALL&&!skip_appsbl){
        uint8_t*now=malloc(APPSBL_BYTES);if(!now||!target->snapshot||read_all(target->fd,now,APPSBL_BYTES,0)||memcmp(now,target->snapshot,APPSBL_BYTES)){free(now);snprintf(why,sizeof(why),"非活动APPSBL与初次绑定完整快照不同");goto done;}free(now);
        if(write_appsbl(target,payload)){snprintf(why,sizeof(why),"APPSBL擦写或完整读回失败；停止，不自动重试");goto done;}
    }
    c0=malloc(BC_BYTES);c1=malloc(BC_BYTES);if(!c0||!c1){snprintf(why,sizeof(why),"BC候选内存分配失败");goto done;}
    make_bc_candidate(c0,old0,age,next_appsbl,g_oem_selector);make_bc_candidate(c1,old1,age,next_appsbl,g_oem_selector);
    if(bc_candidate_valid(old0,c0,age,next_appsbl,g_oem_selector)||bc_candidate_valid(old1,c1,age,next_appsbl,g_oem_selector)){snprintf(why,sizeof(why),"逐副本BC候选含非预期字段");goto done;}
#if BI_TEST
    if(!strcmp(fixture_fault(),"oem_pause_before_bc")){(void)set_state("installing","oem_before_commit_gate","合成TOCTOU暂停点");raise(SIGSTOP);}
#endif
    /* The original system is revalidated after a possibly slow APPSBL write,
     * immediately before any activation record can change. */
    if(verify_oem_slot(parts,n,rootfs,0)||snapshots_unchanged_except(parts,n,skip_appsbl?NULL:target,NULL,NULL)){snprintf(why,sizeof(why),"BC提交前原厂槽或受保护区变化");goto done;}
    if(write_bc(bc1,old1,c1,"bootconfig1")){snprintf(why,sizeof(why),"BOOTCONFIG1写入失败，副本可能已改变；冻结本次启动，不重试、不重启");goto done;}
#if BI_TEST
    if(!strcmp(fixture_fault(),"oem_pause_after_bc1")){(void)set_state("installing","oem_after_backup_gate","合成TOCTOU暂停点");raise(SIGSTOP);}
#endif
    if(verify_oem_slot(parts,n,rootfs,0)||snapshots_unchanged_except(parts,n,skip_appsbl?NULL:target,NULL,bc1)){snprintf(why,sizeof(why),"备用BC提交后原厂槽或主BC变化；停止，不提交主副本");goto done;}
    if(write_bc(bc0,old0,c0,"bootconfig0")){snprintf(why,sizeof(why),"BOOTCONFIG1已提交；BOOTCONFIG主副本写入失败且可能已改变；冻结本次启动，不重试、不重启");goto done;}
    {uint8_t*v0=malloc(BC_BYTES),*v1=malloc(BC_BYTES);
        if(!v0||!v1||read_all(bc0->fd,v0,BC_BYTES,0)||read_all(bc1->fd,v1,BC_BYTES,0)||memcmp(v0,c0,BC_BYTES)||memcmp(v1,c1,BC_BYTES)){free(v0);free(v1);snprintf(why,sizeof(why),"两份BC最终全量读回不等于精确候选");goto done;}
        free(v0);free(v1);
    }
    g_oem_post_valid=1;g_oem_post_age=age+1;g_oem_post_appsbl=next_appsbl;
    {bi_sha256_ctx c;bi_sha256_init(&c);bi_sha256_update(&c,c0,BC_BYTES);bi_sha256_final(&c,g_oem_post_bc_sha);bi_sha256_init(&c);bi_sha256_update(&c,c1,BC_BYTES);bi_sha256_final(&c,g_oem_post_bc1_sha);}
    if(verify_oem_slot(parts,n,g_oem_selector,0)||snapshots_unchanged_except(parts,n,skip_appsbl?NULL:target,bc0,bc1)){snprintf(why,sizeof(why),"提交后原厂槽或受保护区变化");goto done;}
    if(!skip_appsbl){uint8_t*installed=malloc(APPSBL_BYTES);if(!installed||read_all(target->fd,installed,APPSBL_BYTES,0)||memcmp(installed,payload,APPSBL_BYTES)){free(installed);snprintf(why,sizeof(why),"APPSBL最终完整读回失败");goto done;}free(installed);}
    /* Bind the complete verified postimage for the later explicit reboot. */
    for(size_t i=0;i<n;i++){free(parts[i].snapshot);parts[i].snapshot=NULL;}
    if(protected_snapshot(parts,n)||snapshot_fingerprint(parts,n,g_oem_post_fingerprint)){snprintf(why,sizeof(why),"最终启动区postimage无法绑定");goto done;}
    g_oem_records_verified=1;
    g_oem_boot_policy_verified=!g_oem_return_mode&&oem_boot_contract(g_candidate_hash,1);
    failed=0;
done:
    for(size_t i=0;i<n;i++)part_close(&parts[i]);
    for(size_t i=0;i<n;i++)free(parts[i].snapshot);
    free(payload);free(old0);free(old1);free(c0);free(c1);
    if(failed){g_can_reboot=0;g_oem_verified=0;g_oem_records_verified=0;g_oem_post_valid=0;g_oem_boot_policy_verified=0;g_install_ready=0;snprintf(g_blocked,sizeof(g_blocked),"%s",why);(void)set_state("failed","stopped",why);_exit(1);}
    g_can_reboot=!g_oem_return_mode&&g_oem_boot_policy_verified;g_progress=g_total;g_blocked[0]=0;
    if(!g_oem_return_mode){
        (void)set_state("complete",g_version.decision==BV_SKIP_IDENTICAL?"identical_oem_selected":"verified","原厂槽与两份启动记录完整核验；已选择原厂作为默认系统，等待手动重启");_exit(0);
    }
    /* Only a user's explicit return request enters here. Installation never
     * calls this branch and never automatically restarts the device. */
    if(set_state("complete","oem_return_verified","原厂启动记录完整读回；按本次返回请求安排一次受限重启")||validate_oem_post_state()){
        g_can_reboot=0;g_oem_post_valid=0;(void)set_state("failed","oem_return_recheck_failed","返回提交后重新核验失败；不重启、不重试");_exit(1);
    }
    sleep(2);
    if(validate_oem_post_state()){g_oem_post_valid=0;(void)set_state("failed","oem_return_recheck_failed","重启前原厂内容或启动记录变化；已冻结");_exit(1);}
#if BI_TEST
    (void)set_state("complete","oem_return_reboot_simulated","合成夹具：原厂返回重启已模拟，未调用系统重启");_exit(0);
#else
    sync();reboot(RB_AUTOBOOT);g_oem_post_valid=0;(void)set_state("failed","oem_return_reboot_failed","原厂返回重启调用失败；保留诊断，不重试");_exit(1);
#endif
}

static void child_install(void){child_transaction(BI_OP_INSTALL);}
static void child_return_oem(void){child_transaction(BI_OP_RETURN_OEM);}

static void make_status_paths(void){
    char tmpl[PATH_MAX];
#if BI_TEST
    char rel[96];
    if(path_join(tmpl,sizeof(tmpl),"tmp"))return;
    (void)mkdir(tmpl,0700);
    snprintf(rel,sizeof(rel),"tmp/nx30-bridge-install-%s",g_session);
    if(path_join(tmpl,sizeof(tmpl),rel))return;
    snprintf(rel,sizeof(rel),"tmp/nx30-bridge-install-boot.lock");
    if(path_join(g_lock_file,sizeof(g_lock_file),rel))return;
#else
    int n=snprintf(tmpl,sizeof(tmpl),"/tmp/nx30-bridge-install-%s",g_session);if(n<0||(size_t)n>=sizeof(tmpl))return;
    if(snprintf(g_lock_file,sizeof(g_lock_file),"/tmp/nx30-bridge-install-boot.lock")>=(int)sizeof(g_lock_file))return;
#endif
    if(mkdir(tmpl,0700))return;
    snprintf(g_tmpdir,sizeof(g_tmpdir),"%s",tmpl);
    size_t z=strlen(tmpl);
    if(z+sizeof("/status.json")>sizeof(g_status_file)||z+sizeof("/backup.zip")>sizeof(g_backup_file))return;
    memcpy(g_status_file,tmpl,z);memcpy(g_status_file+z,"/status.json",sizeof("/status.json"));
    memcpy(g_backup_file,tmpl,z);memcpy(g_backup_file+z,"/backup.zip",sizeof("/backup.zip"));
}

void bi_init(const char *fixture_root){
    g_install_rejected_check[0]=0;g_install_rejected_reason[0]=0;
    g_bound_operation=BI_OP_NONE;g_operation_ticket[0]=0;g_return_ready=0;g_common_bound=0;g_stock_baseline=0;g_return_rule=0;g_return_source_record=NULL;memset(g_bridge_snapshot,0,32);memset(g_return_source_revision,0,32);memset(g_return_evidence,0,32);
    g_install_ready=0;g_oem_post_fingerprint[0]=0;memset(g_oem_bc_before,0,sizeof(g_oem_bc_before));
    g_oem_verified=0;g_oem_records_verified=0;g_oem_bound=0;g_oem_post_valid=0;g_oem_return_mode=0;g_oem_boot_policy_verified=0;
#if defined(BRIDGE_RETURN_ONLY)
    g_oem_return_mode=1;
#endif
#if !BI_TEST
    g_first_install_qualified=0;
    g_first_install_record=NULL;
    g_first_install_profile=NULL;
#endif
    g_oem_offset=0;g_oem_selector=0;g_oem_index=0;g_oem_bridge_off=0;g_oem_post_age=0;g_oem_post_appsbl=0;g_oem_dev=0;
    memset(&g_oem_pool_match,0,sizeof(g_oem_pool_match));memset(g_oem_snapshot,0,sizeof(g_oem_snapshot));memset(g_oem_post_bc_sha,0,sizeof(g_oem_post_bc_sha));memset(g_oem_post_bc1_sha,0,sizeof(g_oem_post_bc1_sha));memset(g_oem_bc_before,0,sizeof(g_oem_bc_before));memset(g_oem_bc1_before,0,sizeof(g_oem_bc1_before));g_running_mtd_binding[0]=0;snprintf(g_oem_reason,sizeof(g_oem_reason),"原厂核验尚未完成");

    g_version=(bv_result){BV_REJECT_UNKNOWN,0,0};
    g_backup_hash[0]=0;
    g_status_memory_only=0;g_reboot_requested=0;g_nchecks=0;g_started=0;g_token_used=0;g_modified=0;g_can_reboot=0;g_ids_ok=0;g_child=-1;g_progress=0;g_total=APPSBL_BYTES+2*BC_BYTES;g_backup_available=0;g_backup_size=0;g_phase[0]=0;g_blocked[0]=0;g_candidate_hash[0]=0;g_active_hash[0]=0;g_bound_payload_hash[0]=0;g_bound_fingerprint_valid=0;g_bound_fingerprint[0]=0;g_bound_nand_identity_hash[0]=0;g_nand_identity_hash[0]=0;g_nand_profile_id[0]=0;g_target_name[0]=0;g_status_file[0]=0;g_backup_file[0]=0;g_tmpdir[0]=0;g_lock_file[0]=0;g_boot_id[0]=0;
    if(BI_TEST){if(fixture_root&&fixture_root[0])snprintf(g_root,sizeof(g_root),"%s",fixture_root);else snprintf(g_root,sizeof(g_root),".");}else g_root[0]=0;
#if !BI_TEST
    if(fixture_root&&fixture_root[0]){snprintf(g_session,sizeof(g_session),"rejected-%08lx",(unsigned long)((unsigned long)getpid()^(unsigned long)time(NULL)));g_token[0]=0;snprintf(g_candidate_hash,sizeof(g_candidate_hash),"%s",BI_PAYLOAD_SHA256);make_status_paths();snprintf(g_phase,sizeof(g_phase),"blocked");snprintf(g_stage,sizeof(g_stage),"blocked");snprintf(g_blocked,sizeof(g_blocked),"生产构建拒绝 fixture_root；不会访问设备或夹具");snprintf(g_message,sizeof(g_message),"%s",g_blocked);(void)write_status();return;}
#endif
    if(make_ids()==0)g_ids_ok=1;else{strcpy(g_session,"unavailable");g_token[0]=0;}
#if !BI_TEST && !defined(BRIDGE_RETURN_ONLY)
    snprintf(g_candidate_hash,sizeof(g_candidate_hash),"%s",BI_PAYLOAD_SHA256);
#endif
#if BI_TEST && defined(BRIDGE_OEM_COMPILED_TEST_MANIFEST)
    snprintf(g_candidate_hash,sizeof(g_candidate_hash),"%s",TEST_BUILD_CANDIDATE_SHA256);
#endif
    make_status_paths();snprintf(g_phase,sizeof(g_phase),"checking");snprintf(g_stage,sizeof(g_stage),"preflight");snprintf(g_message,sizeof(g_message),"检查运行桥接、原厂退出与独立安装资格");snprintf(g_blocked,sizeof(g_blocked),"检查未完成");
    g_boot_lock_seen=load_boot_id()!=0||boot_lock_exists();if(g_boot_lock_seen)g_modified=1;
    bi_part parts[MAX_PARTS];memset(parts,0,sizeof(parts));for(size_t i=0;i<MAX_PARTS;i++)parts[i].fd=-1;size_t n=0;bi_part *sbl=NULL,*env=NULL,*art=NULL,*bc0=NULL,*bc1=NULL,*app0=NULL,*app1=NULL;uint8_t *payload=NULL,*b0=NULL,*b1=NULL;uint32_t age=0,sel=0,rootfs=0;
    if(!BI_TEST&&!BI_HAVE_PAYLOAD&&!g_oem_return_mode){add_check("payload","blocked","payload header 尚未提供；安装保持禁用");goto blocked;}
    if(!g_ids_ok){snprintf(g_blocked,sizeof(g_blocked),"无法获取安全随机会话 ID；安装保持禁用");goto blocked;}
    if(discover_preflight(parts,&n,&sbl,&env,&art,&bc0,&bc1,&app0,&app1,&payload,&b0,&b1,&age,&sel,&rootfs))goto blocked;
    if(g_boot_lock_seen){snprintf(g_blocked,sizeof(g_blocked),"本次启动已有安装尝试或无法核验启动锁；需要重新启动 bridge 后再检查");goto blocked;}
    snprintf(g_phase,sizeof(g_phase),"ready");snprintf(g_stage,sizeof(g_stage),"ready");g_blocked[0]=0;snprintf(g_message,sizeof(g_message),"核验通过。备份下载可跳过；点击安装后只写未选中 APPSBL，再提交两份 BC。");g_started=1;
    if(!g_install_ready&&!oem_return_available()){if(!g_blocked[0])snprintf(g_blocked,sizeof(g_blocked),"%s",g_return_reason);goto blocked;}
    if(!g_install_ready&&g_return_ready)snprintf(g_message,sizeof(g_message),"原厂退出合同已核验；安装资格不可用，可明确选择返回原厂");
    if(g_version.decision==BV_SKIP_IDENTICAL){g_total=2*BC_BYTES;snprintf(g_message,sizeof(g_message),"可信相同U-Boot跳过APPSBL写入；仍须核验并选择保留原厂槽");}
    /* Snapshot is needed only until an accepted install. Keep a private RAM copy? Preflight buffers die here;
       the child performs an independent fresh preflight at transaction start and binds all preimages then. */
    for(size_t i=0;i<n;i++)part_close(&parts[i]);
    for(size_t i=0;i<n;i++)free(parts[i].snapshot);
    free(payload);free(b0);free(b1);(void)sbl;(void)env;(void)art;(void)bc0;(void)bc1;(void)app0;(void)app1;(void)age;(void)sel;(void)rootfs;
    if(!g_inspection_mode&&write_status()){g_started=0;snprintf(g_phase,sizeof(g_phase),"blocked");snprintf(g_stage,sizeof(g_stage),"blocked");snprintf(g_blocked,sizeof(g_blocked),"状态文件不可持久化；安装保持禁用");snprintf(g_message,sizeof(g_message),"%s",g_blocked);return;}return;
blocked:
    for(size_t i=0;i<n;i++)part_close(&parts[i]);
    for(size_t i=0;i<n;i++)free(parts[i].snapshot);
    free(payload);free(b0);free(b1);
    if(!g_blocked[0])snprintf(g_blocked,sizeof(g_blocked),"严格前置检查失败；安装不可用");
    snprintf(g_phase,sizeof(g_phase),"blocked");snprintf(g_stage,sizeof(g_stage),"blocked");snprintf(g_message,sizeof(g_message),"%s",g_blocked);if(!g_inspection_mode)(void)write_status();
}

static int token_ok(const char *token){return g_ids_ok&&g_token[0]&&token&&strlen(token)==64&&strlen(token)==strlen(g_token)&&memcmp(token,g_token,strlen(g_token))==0;}
static int start_operation(const char*token,int returning,char*error,size_t cap){
    if(error&&cap)error[0]=0;
    if(!g_started||strcmp(g_phase,"ready")||g_token_used||!token_ok(token)||
       (returning?!oem_return_available():!g_install_ready)){
        if(error&&cap)snprintf(error,cap,"操作未就绪、已锁定或会话无效；原厂返回与安装资格分别核验");
        return -1;
    }
    if(create_boot_lock()){if(error&&cap)snprintf(error,cap,"不能建立本启动事务锁；未开始写入");return -1;}
    g_boot_lock_seen=1;g_token_used=1;g_oem_return_mode=returning;g_can_reboot=0;g_progress=0;g_modified=0;
    /* Accepted operation is explicit and immutable for this single-use token. */
    g_bound_operation=returning?BI_OP_RETURN_OEM:BI_OP_INSTALL;operation_ticket(g_operation_ticket);
    g_total=returning||g_version.decision==BV_SKIP_IDENTICAL?2*BC_BYTES:APPSBL_BYTES+2*BC_BYTES;
    g_oem_post_age=le32(g_oem_bc_before+4)+1;
    g_oem_post_appsbl=le32(g_oem_bc_before+0x58);
    if(!returning&&g_version.decision!=BV_SKIP_IDENTICAL)g_oem_post_appsbl=1-g_oem_post_appsbl;
    uint8_t*post=malloc(BC_BYTES);if(!post){g_started=0;(void)set_state("failed","memory_failed","事务postimage分配失败；本次启动冻结");return -1;}
    make_bc_candidate(post,g_oem_bc_before,g_oem_post_age-1,g_oem_post_appsbl,g_oem_selector);
    bi_sha256_ctx hash;bi_sha256_init(&hash);bi_sha256_update(&hash,post,BC_BYTES);bi_sha256_final(&hash,g_oem_post_bc_sha);
    make_bc_candidate(post,g_oem_bc1_before,g_oem_post_age-1,g_oem_post_appsbl,g_oem_selector);
    bi_sha256_init(&hash);bi_sha256_update(&hash,post,BC_BYTES);bi_sha256_final(&hash,g_oem_post_bc1_sha);free(post);
    if(set_state("installing","accepted",returning?"原厂返回请求已接受；核验后只提交启动记录并重启":"安装请求已接受；完整核验后提交原厂默认启动记录")){
        g_started=0;g_status_memory_only=1;snprintf(g_blocked,sizeof(g_blocked),"状态文件不可持久化；未开始写入，本次启动冻结");
        (void)set_state("failed","status_failed",g_blocked);if(error&&cap)snprintf(error,cap,"%s",g_blocked);return -1;
    }
    pid_t p=fork();if(p<0){g_started=0;snprintf(g_blocked,sizeof(g_blocked),"无法启动事务；本次启动冻结");(void)set_state("failed","fork_failed",g_blocked);if(error&&cap)snprintf(error,cap,"%s",g_blocked);return -1;}
    if(p==0){if(returning)child_return_oem();else child_install();}
    g_child=p;return 0;
}
int bi_start(const char*token,char*error,size_t cap){return start_operation(token,0,error,cap);}
int bi_oem_return_start(const char*token,char*error,size_t cap){return start_operation(token,1,error,cap);}

int bi_backup_path(char*out,size_t cap){if(!out||!cap)return -1;out[0]=0;if(!g_backup_available)return 0;if(safe_copy(out,cap,g_backup_file))return -1;return 1;}
static int read_status_file(char*out,size_t cap){int fd=open(g_status_file,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<2||st.st_size>=(off_t)cap){close(fd);return -1;}size_t done=0;while(done<cap-1){ssize_t n=read(fd,out+done,cap-1-done);if(n<0&&errno==EINTR)continue;if(n<0){close(fd);return -1;}if(n==0)break;done+=(size_t)n;}close(fd);if(done==0||done!=(size_t)st.st_size)return -1;out[done]=0;return 0;}
static void refresh_parent_state(const char*json){const char*p=strstr(json,"\"phase\":\"");if(p){p+=9;const char*e=strchr(p,'\"');if(e&&(size_t)(e-p)<sizeof(g_phase)){memcpy(g_phase,p,(size_t)(e-p));g_phase[e-p]=0;}}p=strstr(json,"\"can_reboot\":");if(p)g_can_reboot=!strncmp(p+13,"true",4);p=strstr(json,"\"may_have_modified\":");if(p)g_modified=!strncmp(p+20,"true",4);p=strstr(json,"\"blocked_reason\":\"");if(p){p+=18;const char*e=strchr(p,'\"');if(e&&(size_t)(e-p)<sizeof(g_blocked)){memcpy(g_blocked,p,(size_t)(e-p));g_blocked[e-p]=0;}}if(!strcmp(g_phase,"complete")&&g_token_used){const char*q=strstr(json,"\"oem_post_fingerprint\":\"");if(q){q+=24;const char*e=strchr(q,'"');uint8_t digest[32];if(e&&e-q==64){char fingerprint[65];memcpy(fingerprint,q,64);fingerprint[64]=0;if(!sha_bytes(fingerprint,digest)){snprintf(g_oem_post_fingerprint,sizeof(g_oem_post_fingerprint),"%s",fingerprint);g_oem_post_valid=1;g_oem_boot_policy_verified=!g_oem_return_mode&&oem_boot_contract(g_candidate_hash,1);}}}}else if(!strcmp(g_phase,"failed")||!strcmp(g_phase,"blocked")){g_oem_post_valid=0;g_oem_boot_policy_verified=0;}}
int bi_status_json(char*out,size_t cap){if(!out||cap<2)return -1;if(g_status_memory_only)return format_status(out,cap)<0?-1:0;if(read_status_file(out,cap)==0){refresh_parent_state(out);return 0;}if(write_status())return -1;if(read_status_file(out,cap))return -1;refresh_parent_state(out);return 0;}
/* Recheck the complete logical OEM slot, exact BC postimage, APPSBL identity,
 * and all startup preimages after the user has chosen to reboot. */
static int validate_oem_post_state(void){
    g_oem_records_verified=0;
    if(!g_oem_post_valid||!g_oem_bound||!g_oem_post_fingerprint[0]||!operation_ticket_valid())return -1;
    bi_part parts[MAX_PARTS];memset(parts,0,sizeof(parts));for(size_t i=0;i<MAX_PARTS;i++)parts[i].fd=-1;
    size_t n=0;bi_part *sbl=NULL,*env=NULL,*art=NULL,*bc0=NULL,*bc1=NULL,*app0=NULL,*app1=NULL;int result=-1;
    bi_nand_identity id;char identity[65];uint32_t ecc=0,oob=0;uint8_t *post_env=NULL;
    if(verify_bridge_identity()||(!g_oem_return_mode&&verify_qfprom_authenticate())||read_nand_probe_identity(&id))goto done;
    hash_nand_identity(&id,identity);if(strcmp(identity,g_bound_nand_identity_hash)||discover_all(parts,&n))goto done;
    const struct h3c_nand_catalog_profile*profile=g_oem_return_mode?match_runtime_profile(parts,n,&id,&ecc,&oob):match_nand_profile(parts,n,&id,&ecc,&oob);
    if(!profile||profile_geometry(parts,n,profile,ecc,oob,&sbl,&env,&art,&bc0,&bc1,&app0,&app1)||(!g_oem_return_mode&&verify_sbl(sbl)))goto done;
#if BI_HAVE_INSTALL_PROFILE
    if(!g_oem_return_mode){
        char cmdline[2048];
        if(!env||env->size!=0x80000u||text_file("proc/cmdline",cmdline,sizeof(cmdline)))goto done;
        post_env=malloc(env->size);if(!post_env||read_all(env->fd,post_env,env->size,0))goto done;
        if(check_current_candidate_identity(post_env,env->size,cmdline,g_candidate_hash)!=BII_OK)goto done;
    }
#else
    const char*why=NULL;
    if(!g_oem_return_mode&&(parse_env_product(env,&why)||verify_cmdline_machid()))goto done;
#endif
    if(g_oem_return_mode&&qualify_oem_return(sbl,env,bc0))goto done;
    uint8_t*b0=malloc(BC_BYTES),*b1=malloc(BC_BYTES);if(!b0||!b1){free(b0);free(b1);goto done;}
    uint32_t age=0,app=0,root=0,age1=0,app1v=0,root1=0;uint8_t sha[32],sha1[32];bi_sha256_ctx h;
    int bad=read_all(bc0->fd,b0,BC_BYTES,0)||read_all(bc1->fd,b1,BC_BYTES,0)||parse_bc(b0,&age,&app,&root)||parse_bc(b1,&age1,&app1v,&root1)||age!=age1||app!=app1v||root!=root1;
    if(bad){free(b0);free(b1);goto done;}
    bi_sha256_init(&h);bi_sha256_update(&h,b0,BC_BYTES);bi_sha256_final(&h,sha);bi_sha256_init(&h);bi_sha256_update(&h,b1,BC_BYTES);bi_sha256_final(&h,sha1);free(b0);free(b1);
    if(age!=g_oem_post_age||app!=g_oem_post_appsbl||root!=g_oem_selector||memcmp(sha,g_oem_post_bc_sha,32)||memcmp(sha1,g_oem_post_bc1_sha,32))goto done;
    bi_part*active=app==0?app0:app1;uint8_t*bytes=malloc(APPSBL_BYTES);char hash[65];
    if(!bytes)goto done;
    bad=read_all(active->fd,bytes,APPSBL_BYTES,0);if(!bad)hash_hex(bytes,APPSBL_BYTES,hash);free(bytes);if(bad)goto done;
    if(strcmp(hash,g_oem_return_mode?g_active_hash:g_candidate_hash)||!oem_boot_contract(hash,!g_oem_return_mode))goto done;
    if(verify_oem_slot(parts,n,root,0)||protected_snapshot(parts,n))goto done;
    char fingerprint[65];if(snapshot_fingerprint(parts,n,fingerprint)||strcmp(fingerprint,g_oem_post_fingerprint))goto done;
    g_oem_records_verified=1;result=0;
done:
    free(post_env);
    for(size_t i=0;i<n;i++){part_close(&parts[i]);free(parts[i].snapshot);}
    return result;
}
int bi_oem_reboot_start(const char*token,char*error,size_t cap){
    if(error&&cap)error[0]=0;
    char state[STATUS_CAP];
    if(!g_status_memory_only&&read_status_file(state,sizeof(state))==0)refresh_parent_state(state);
    if(g_reboot_requested||!token_ok(token)||strcmp(g_phase,"complete")||!g_can_reboot||!g_oem_post_valid||!g_oem_boot_policy_verified||g_oem_return_mode){
        if(error&&cap)snprintf(error,cap,"只允许已核验安装、原厂启动记录和精确候选启动合同的手动原厂重启");
        return -1;
    }
    if(validate_oem_post_state()){
        g_can_reboot=0;g_oem_post_valid=0;g_reboot_requested=1;(void)set_state("failed","oem_reboot_recheck_failed","手动重启前原厂槽或启动保护区变化；已冻结");
        if(error&&cap)snprintf(error,cap,"重启前完整原厂/启动记录核验失败；未重启且不重试");
        return -1;
    }
    g_reboot_requested=1;g_can_reboot=0;pid_t p=fork();
    if(p<0){(void)set_state("failed","oem_reboot_fork_failed","无法启动受限重启任务；不重试");return -1;}
    if(p==0){child_close_web_fds();sleep(2);
        if(validate_oem_post_state()){g_oem_post_valid=0;(void)set_state("failed","oem_reboot_recheck_failed","延迟期间原厂槽或启动记录变化；不重启");_exit(1);}
#if BI_TEST
        (void)set_state("complete","reboot_simulated","合成夹具：已核验原厂重启已模拟，未调用系统重启");_exit(0);
#else
        sync();reboot(RB_AUTOBOOT);_exit(1);
#endif
    }
    g_child=p;if(set_state("complete","reboot_scheduled","已接受手动原厂重启；延迟后再次核验，不自动重试"))g_status_memory_only=1;return 0;
}
int bi_reboot_start(const char*token,char*error,size_t cap){
    (void)token;if(error&&cap)snprintf(error,cap,"通用重启入口已禁用；使用经完整核验的原厂重启接口");return -1;
}

void bi_reap(void){
    if(g_child<=0)return;
    int st;pid_t p=waitpid(g_child,&st,WNOHANG);
    if(p!=g_child&&!(p<0&&errno==ECHILD))return;
    g_child=-1;
    char state[STATUS_CAP];
    if(!g_status_memory_only&&read_status_file(state,sizeof(state))==0)refresh_parent_state(state);
    if(g_reboot_requested){
        /* Reboot returning/exiting abnormally does not undo verified installation.
           Keep the recorded modification fact and never offer install/reboot retry. */
        if(p<0||!WIFEXITED(st)||WEXITSTATUS(st)!=0){
            g_can_reboot=0;g_blocked[0]=0;
            if(set_state("complete","reboot_failed","安装已完成；系统重启失败，未再次安装或自动重试；请检查设备状态"))g_status_memory_only=1;
        }
    }else if(strcmp(g_phase,"complete")&&strcmp(g_phase,"failed")){
        g_modified=1;g_can_reboot=0;
        snprintf(g_blocked,sizeof(g_blocked),"安装子进程异常退出；无法证明写入前状态，锁定本次启动");
        (void)set_state("failed","worker_exit",g_blocked);
    }
}

void bi_set_dispatch_diagnostic(const char *code,const char *reason,const char *candidate_source_revision){
    if(!code||!reason||!candidate_source_revision||strlen(code)>=sizeof(g_dispatch_code)||strlen(reason)>=sizeof(g_dispatch_reason)||strlen(candidate_source_revision)>=sizeof(g_dispatch_candidate_revision))return;
    for(const unsigned char*p=(const unsigned char*)code;*p;p++)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return;
    for(const unsigned char*p=(const unsigned char*)reason;*p;p++)if(*p<0x20||*p==0x7f)return;
    if(*candidate_source_revision){if(strlen(candidate_source_revision)!=64)return;for(const char*p=candidate_source_revision;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')))return;}
    snprintf(g_dispatch_code,sizeof(g_dispatch_code),"%s",code);
    snprintf(g_dispatch_reason,sizeof(g_dispatch_reason),"%s",reason);
    snprintf(g_dispatch_candidate_revision,sizeof(g_dispatch_candidate_revision),"%s",candidate_source_revision);
}
void bi_set_inspection_mode(int enabled){g_inspection_mode=enabled?1:0;}
int bi_set_dispatch_execution_detail(const char *detail){
    if(!detail||strlen(detail)>=sizeof(g_dispatch_execution_detail))return -1;
    for(const unsigned char *p=(const unsigned char*)detail;*p;p++)if(*p<0x20u||*p>0x7eu)return -1;
    snprintf(g_dispatch_execution_detail,sizeof(g_dispatch_execution_detail),"%s",detail);return 0;
}
int bi_dispatch_inspect_json(char *out,size_t cap){
    size_t used=0;
    if(!out||cap<2)return -1;
#define INSPECT_APP(...) do{int z=snprintf(out+used,cap-used,__VA_ARGS__);if(z<0||(size_t)z>=cap-used)return -1;used+=(size_t)z;}while(0)
    INSPECT_APP("{\"install_ready\":%s,\"return_ready\":%s,\"source_sha256\":",
        (!strcmp(g_phase,"ready")&&g_install_ready)?"true":"false",oem_return_available()?"true":"false");
    if(json_string(out,cap,&used,BI_RETURN_SOURCE_OFFICIAL_SHA256))return -1;
    INSPECT_APP(",\"product\":");if(json_string(out,cap,&used,BI_EXPECTED_PRODUCT))return -1;
    INSPECT_APP(",\"route\":");if(json_string(out,cap,&used,BI_FIRST_INSTALL_ROUTE))return -1;
    INSPECT_APP(",\"hardware_class\":");if(json_string(out,cap,&used,BI_HARDWARE_CLASS))return -1;
    INSPECT_APP(",\"candidate_id\":");if(json_string(out,cap,&used,BI_CANDIDATE_ID))return -1;
    INSPECT_APP(",\"rejected_check\":");
    if(json_string(out,cap,&used,g_install_ready?"":g_install_rejected_check[0]?g_install_rejected_check:"preflight"))return -1;
    INSPECT_APP(",\"reason\":");
    if(json_string(out,cap,&used,g_install_ready?g_return_reason:
        g_install_rejected_reason[0]?g_install_rejected_reason:g_blocked[0]?g_blocked:g_return_reason))return -1;
    INSPECT_APP("}\n");
#undef INSPECT_APP
    return 0;
}
