/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge_install_identity.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV_BYTES 0x80000u
#define ENV_CRC_BYTES 0x40000u

typedef struct {
    const char *board, *candidate, *machid, *env_alias;
    unsigned filter_env_root_keys, allow_missing_env_machid;
} candidate_policy;

typedef struct {
    const char *source_sha256, *source_product, *observed_product, *env_alias;
    unsigned board_mask;
} source_policy;

/* A fresh candidate is admitted only by a generated, compile-time pinned
 * policy. Runtime input cannot extend this registry. The archive descriptor,
 * payload digest and source matrix are independently checked by the builder
 * and provider before this ENV compatibility check runs. */
#ifdef BII_CANDIDATE_POLICY_HEADER
#include BII_CANDIDATE_POLICY_HEADER
#endif

/* Exact public release payloads from the reviewed 5f486f5f source archive.
 * These records identify offline controlled-trial bytes only; they grant no
 * install, ECC, SBL, OEM-return, or native qualification. B and D share a
 * machid, so both board and candidate digest are mandatory selectors.
 * `filter_env_root_keys` is enabled only where that exact release build
 * enables the C/D source filter in update_bootargs (C production port, D
 * RAM_WEB). A appends generic root args; B's RT3000 composer filters only
 * after resolving a runtime UBI label, so both remain conservative here. */
static const candidate_policy policies[] = {
#ifdef BII_EXTRA_CANDIDATE_POLICIES
    BII_EXTRA_CANDIDATE_POLICIES
#endif
    {"A", "8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb", "08040001", NULL, 0, 0},
    {"B", "912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589", "08040000", NULL, 0, 0},
    {"C", "57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e", "08040101", NULL, 1, 0},
    /* OEM R012 for the D sample uses this exact productname alias and omits
     * machid from both ENV and Linux cmdline. This narrow absence rule is
     * bound to D's exact candidate and board record; identity is not hardware
     * detection and does not replace the runtime NAND/loader checks. If a
     * machid is present in either source it must still be unique and exact. */
    {"D", "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362", "08040000", "NX30_2i5x", 1, 1}
};

#define BOARD_A (1u << 0)
#define BOARD_B (1u << 1)
#define BOARD_C (1u << 2)
#define BOARD_D (1u << 3)
#define BOARD_ABCD (BOARD_A|BOARD_B|BOARD_C|BOARD_D)
/* Each row is a precise source-product / observed-product / provider matrix
 * entry. */
static const source_policy sources[] = {
    {"69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44", "RC3000", "RC3000", NULL, BOARD_ABCD},
    {"cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b", "RT3000", "RT3000", NULL, BOARD_ABCD},
    {"5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5", "RW3000", "RW3000", NULL, BOARD_ABCD},
    /* The reviewed NX30 official response is also used for the observed
     * RX3000 identity. RX requires exact productname=RX3000 on any class;
     * NX30/NX30_2i5x remains D-only. No other RX/NX aliases are accepted. */
    {"61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "NX30", "RX3000", NULL, BOARD_ABCD},
    {"61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "NX30", "NX30", NULL, BOARD_D},
    {"61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "NX30", "NX30", "NX30_2i5x", BOARD_D}
};

static int eq(const char *a,const char *b)
{ return a&&b&&!strcmp(a,b); }

static int sha256_text(const char *s)
{
    if(!s||strlen(s)!=64)return 0;
    for(size_t i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}

static int parse_machid(const char *s,char out[16])
{
    const char *p=s;size_t n;
    if(!p||!*p)return -1;
    if(p[0]=='0'&&(p[1]=='x'||p[1]=='X'))p+=2;
    n=strlen(p);if(!n||n>8)return -1;
    for(size_t i=0;i<n;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')||(p[i]>='A'&&p[i]<='F')))return -1;
    unsigned long value;char *end;errno=0;value=strtoul(p,&end,16);
    if(errno||*end||value>0xfffffffful)return -1;
    snprintf(out,16,"%08lx",value);return 0;
}

static uint32_t env_crc(const uint8_t *p)
{
    uint32_t crc=0xffffffffu;
    for(size_t i=4;i<ENV_CRC_BYTES;i++){crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(int32_t)(crc&1));}
    return ~crc;
}
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int copy_value(char *dst,size_t cap,const uint8_t *p,size_t n)
{ if(n>=cap)return -1;memcpy(dst,p,n);dst[n]=0;return 0; }
static int root_key(const uint8_t *p,size_t n)
{
    static const char *const keys[]={"ubi.mtd=","ubi.block=","root=","rootfstype=","rootflags=","panic="};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++){size_t k=strlen(keys[i]);if(n>=k&&!memcmp(p,keys[i],k))return 1;}
    return (n==8&&!memcmp(p,"rootwait",8))||(n>=9&&!memcmp(p,"rootwait=",9));
}
static int bootargs_ok(const candidate_policy *policy,const uint8_t *p,size_t n)
{
    size_t at=0;
    while(at<n){size_t start;while(at<n&&(p[at]==' '||p[at]=='\t'||p[at]=='\r'||p[at]=='\n'))at++;if(at==n)break;
        start=at;while(at<n&&p[at]!=' '&&p[at]!='\t'&&p[at]!='\r'&&p[at]!='\n'){
            if(p[at]<0x21||p[at]==0x7f)return 0;
            at++;
        }
        if(!policy->filter_env_root_keys&&root_key(p+start,at-start))return 0;
    }
    return 1;
}
static int env_string(const uint8_t *env,size_t at,size_t end,char *dst,size_t cap)
{
    return copy_value(dst,cap,env+at,end-at);
}

static bii_code check_public_candidate(const char *board,const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid,const char *candidate_sha256,
                                   const uint8_t *env,size_t env_bytes,
                                   const char *cmdline,bii_result *out,
                                   unsigned runtime_platform_verified)
{
    const candidate_policy *policy=NULL;const source_policy *source=NULL;bii_result result;char expected[16];unsigned board_bit=0,source_context=0;
    unsigned products=0,machids=0,commands=0,args=0;size_t pos=4;
    if(!board||!source_product||!*source_product||strlen(source_product)>=64||
       !sha256_text(official_source_sha256)||!sha256_text(candidate_sha256)||parse_machid(machid,expected))return BII_POLICY;
    if(eq(board,"A"))board_bit=BOARD_A;else if(eq(board,"B"))board_bit=BOARD_B;
    else if(eq(board,"C"))board_bit=BOARD_C;else if(eq(board,"D"))board_bit=BOARD_D;
    if(!board_bit)return BII_POLICY;
    for(size_t i=0;i<sizeof(sources)/sizeof(sources[0]);i++)
        if(eq(official_source_sha256,sources[i].source_sha256)&&eq(source_product,sources[i].source_product)&&
           (sources[i].board_mask&board_bit)){source_context=1;break;}
    if(!source_context)return BII_POLICY;
    for(size_t i=0;i<sizeof(policies)/sizeof(policies[0]);i++)
        if(eq(board,policies[i].board)&&eq(candidate_sha256,policies[i].candidate)){policy=&policies[i];break;}
    if(!policy||!eq(expected,policy->machid))return BII_POLICY;
    if(!env||env_bytes!=ENV_BYTES)return BII_SIZE;
    if(env_crc(env)!=le32(env))return BII_CRC;
    memset(&result,0,sizeof(result));
    while(pos<ENV_CRC_BYTES){size_t end=pos,eqpos=SIZE_MAX;
        if(env[pos]==0)break;
        while(end<ENV_CRC_BYTES&&env[end])end++;
        if(end==ENV_CRC_BYTES)return BII_FORMAT;
        for(size_t i=pos;i<end;i++){
            if(env[i]<0x20||env[i]==0x7f)return BII_FORMAT;
            if(env[i]=='='&&eqpos==SIZE_MAX)eqpos=i;
        }
        if(eqpos==SIZE_MAX||eqpos==pos)return BII_FORMAT;
        size_t keylen=eqpos-pos,vlen=end-eqpos-1;const uint8_t *value=env+eqpos+1;
        if(keylen==11&&!memcmp(env+pos,"productname",11)){
            if(++products!=1||env_string(env,eqpos+1,end,result.product,sizeof(result.product)))return BII_PRODUCT;
        }else if(keylen==6&&!memcmp(env+pos,"machid",6)){
            char found[16];if(++machids!=1||env_string(env,eqpos+1,end,result.machid,sizeof(result.machid))||parse_machid(result.machid,found)||!eq(found,expected))return BII_ENV_MACHID;
        }else if(keylen==7&&!memcmp(env+pos,"bootcmd",7)){
            if(++commands!=1)return BII_FORMAT;
            result.have_bootcmd=1;
            /* Release autoboot ignores this value, but reject control bytes. */
            for(size_t i=0;i<vlen;i++)if(value[i]<0x20||value[i]==0x7f)return BII_FORMAT;
        }else if(keylen==8&&!memcmp(env+pos,"bootargs",8)){
            if(++args!=1||!bootargs_ok(policy,value,vlen))return BII_BOOTARGS;
            result.have_bootargs=1;
        }
        pos=end+1;
    }
    if(products!=1)return BII_PRODUCT;
    for(size_t i=0;i<sizeof(sources)/sizeof(sources[0]);i++)
        if(eq(official_source_sha256,sources[i].source_sha256)&&eq(source_product,sources[i].source_product)&&
           (sources[i].board_mask&board_bit)&&
           (eq(result.product,sources[i].observed_product)||
            (sources[i].env_alias&&eq(result.product,sources[i].env_alias)&&
             policy->env_alias&&eq(sources[i].env_alias,policy->env_alias)))){source=&sources[i];break;}
    if(!source)return BII_PRODUCT;
    if(machids!=1&&!(!machids&&(policy->allow_missing_env_machid||runtime_platform_verified)))return BII_ENV_MACHID;
    if(cmdline){
        const char *p=cmdline;unsigned seen=0;
        while(*p){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;if(!*p)break;
            const char *start=p;while(*p&&*p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n')p++;
            size_t n=(size_t)(p-start);
            if(n>=7&&!memcmp(start,"machid=",7)){
                char found[16],token[16];size_t m=n-7;
                if(++seen!=1||m>=sizeof(token)||copy_value(token,sizeof(token),(const uint8_t*)start+7,m)||parse_machid(token,found)||!eq(found,expected))return BII_CMDLINE_MACHID;
            }
        }
    }
    memcpy(result.board,policy->board,strlen(policy->board)+1);
    memcpy(result.machid,expected,strlen(expected)+1);
    result.trial_only=1;if(out)*out=result;return BII_OK;
}

bii_code bii_check_public_candidate(const char *board,const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid,const char *candidate_sha256,
                                   const uint8_t *env,size_t env_bytes,
                                   const char *cmdline,bii_result *out)
{
    return check_public_candidate(board,source_product,official_source_sha256,machid,
        candidate_sha256,env,env_bytes,cmdline,out,0);
}

/* The supplied RC R013L01/C stock ENV has no machid. Do not change the
 * candidate's missing-field policy: substitute only the live C route of the
 * running stock kernel. The OEM loader picks that FIT config from the board
 * itself, so the route is a property of the PCB, not of the product brand:
 * every source whose matrix row admits class C (checked by the caller's
 * source_context) may use it. Neither an absent route nor an A/B/D route
 * grants this platform assertion.
 *
 * This proof only stands in for a MISSING ENV machid; it never admits a
 * candidate by itself. policies[] stays the sole authority on which digests
 * are admissible, so the predicate is restricted to records that table
 * already admits for class C at machid 08040101. Enumerating the table rather
 * than naming one digest keeps a release from shipping a provider whose own
 * payload the runtime path then refuses as BII_ENV_MACHID. */
static unsigned policy_admits_c_candidate(const char *candidate)
{
    for(size_t i=0;i<sizeof(policies)/sizeof(policies[0]);i++)
        if(eq("C",policies[i].board)&&eq("08040101",policies[i].machid)&&
           eq(candidate,policies[i].candidate))return 1;
    return 0;
}

static unsigned c_runtime_platform(const char *board,const char *machid,
        const char *candidate,const uint8_t *compatible,size_t bytes)
{
    static const uint8_t exact_route[]="qcom,ipq5018-mp03.1-c2\0qcom,ipq5018";
    char normalized[16];
    return eq(board,"C")&&policy_admits_c_candidate(candidate)&&
        !parse_machid(machid,normalized)&&eq(normalized,"08040101")&&
        compatible&&bytes==sizeof(exact_route)&&!memcmp(compatible,exact_route,sizeof(exact_route));
}

bii_code bii_check_public_candidate_runtime(const char *board,const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid,const char *candidate_sha256,
                                   const uint8_t *env,size_t env_bytes,
                                   const char *cmdline,const uint8_t *compatible,
                                   size_t compatible_bytes,bii_result *out)
{
    return check_public_candidate(board,source_product,official_source_sha256,machid,
        candidate_sha256,env,env_bytes,cmdline,out,
        c_runtime_platform(board,machid,candidate_sha256,compatible,compatible_bytes));
}

int bii_return_env_product_allowed(const char *official_source_sha256,
                                   const char *source_product,
                                   const char *route,
                                   const char *observed_product)
{
    if(!sha256_text(official_source_sha256)||!source_product||!route||!observed_product)
        return 0;
    for(size_t i=0;i<sizeof(sources)/sizeof(sources[0]);i++){
        const source_policy *source=&sources[i];
        if(!eq(official_source_sha256,source->source_sha256)||
           !eq(source_product,source->source_product)||
           !(eq(observed_product,source->observed_product)||
             (source->env_alias&&eq(observed_product,source->env_alias))))continue;
        if(!(eq(route,"config@mp02.1")||eq(route,"config@mp03.1")||
             eq(route,"config@mp03.1-c2")))continue;
        /* NX and NX30_2i5x are D-only. The reviewed RX identity for the same
         * exact NX source is explicitly admitted on all four classes. */
        if(source->board_mask==BOARD_D&&!eq(route,"config@mp02.1"))continue;
        return 1;
    }
    return 0;
}

const char *bii_code_name(bii_code c)
{
    static const char *const names[]={"ok","policy","size","crc","format","product","env-machid","cmdline-machid","bootargs"};
    return (unsigned)c<sizeof(names)/sizeof(names[0])?names[c]:"unknown";
}
