/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "bridge_dispatch.h"
#include "bridge_sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(BDI_INSPECT_TEST) && !defined(BDI_TEST)
#error BDI_INSPECT_TEST_requires_BDI_TEST
#endif
#if defined(BDI_INSPECT_TIMEOUT_MS) && !(defined(BDI_TEST) && defined(BDI_INSPECT_TEST))
#error BDI_INSPECT_TIMEOUT_MS_is_test_only
#endif

#ifndef BDI_TEST
#include <bridge_provider_bundle.h>
#include "bridge_oem_return_exec.h"
#endif

#define BDI_PAYLOAD_BYTES 1310720u
#define BDI_HTTP_PORT 80u
#define BDI_HTTP_BODY_MAX 4096u
#define BDI_KERNEL_LOG_MAX (1024u * 1024u)
#define BDI_BUNDLE_VERSION 2u
#define BDI_PROVIDER_ROWS_MAX 160u
#ifndef BDI_INSPECT_TIMEOUT_MS
#define BDI_INSPECT_TIMEOUT_MS 180000
#endif

static int digest_valid(const char *s)
{
    if (!s || strlen(s) != 64) return 0;
    for (size_t i=0;i<64;i++)
        if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return 1;
}

static int same(const char *a,const char *b)
{ return a && b && strcmp(a,b)==0; }

static int bounded_text(const char *s,size_t max)
{
    if(!s||!s[0])return 0;
    size_t n=strlen(s);
    if(n>=max)return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)s[i]<0x20u||(unsigned char)s[i]==0x7fu)return 0;
    return 1;
}

static int class_char(const char *s)
{ return s && s[0] && !s[1] && s[0]>='A' && s[0]<='D'; }

static const char *variant_slug(const char *path,const char *prefix,const char *suffix)
{
    if(!path||!prefix||!suffix||strncmp(path,prefix,strlen(prefix)))return NULL;
    const char *slug=path+strlen(prefix),*end=strchr(slug,'/');
    if(!end||end==slug||strcmp(end,suffix))return NULL;
    for(const char *p=slug;p<end;p++)
        if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-'))return NULL;
    return slug;
}

static int digest_path_component(const char *path,const char *prefix,const char *suffix,
                                 const char *expected_digest)
{
    if(!path||!prefix||!suffix||!expected_digest||
       strncmp(path,prefix,strlen(prefix)))return 0;
    const char *name=path+strlen(prefix),*end=strchr(name,'/');
    if(end||strlen(name)!=64u+strlen(suffix)||memcmp(name+64u,suffix,strlen(suffix)))return 0;
    char digest[65];memcpy(digest,name,64u);digest[64]=0;
    return digest_valid(digest)&&same(digest,expected_digest);
}

static int variant_paths_valid(const bdi_provider_record *r)
{
    const char *provider=variant_slug(r->provider_path,"/usr/sbin/bridge-candidates/","/provider");
    const char *payload=variant_slug(r->payload_path,"/etc/bridge/providers/","/uboot.bin");
    if(!provider)return 0;
    if(provider&&payload){
        size_t n=(size_t)(strchr(provider,'/')-provider);
        if(strlen(payload)>n&&!memcmp(provider,payload,n)&&payload[n]=='/')return 1;
    }
    /* Shared payloads are content-addressed immutable base files. The path
     * digest must equal the pinned file digest; bundle_valid additionally
     * requires every reuse of that path to have the same digest and size. */
    return digest_path_component(r->payload_path,
        "/etc/bridge/provider-payload-bases/",".bin",r->payload_sha256);
}

static unsigned class_catalogue_bit(const char *hardware_class);

static int profile_valid(const bdi_nand_profile *p)
{
    if(!p||!bounded_text(p->profile_id,96u)||!p->model_aliases||
       !p->model_alias_count||p->model_alias_count>32u||
       p->jedec_bytes_observed<2u||p->jedec_bytes_observed>8u||
       p->manufacturer_id>255u||p->chip_id>255u||!p->capacity_mib||
       !p->page_bytes||!p->erase_bytes||p->erase_bytes%p->page_bytes||
       !p->raw_spare_bytes||!p->mtd_oob_bytes||p->mtd_oob_bytes>p->raw_spare_bytes||
       (p->ecc_bits!=4u&&p->ecc_bits!=8u)||!p->catalogue_eligible||
       (p->catalogue_eligible&~15u))return 0;
    for(size_t i=0;i<p->model_alias_count;i++){
        if(!bounded_text(p->model_aliases[i],BDI_MODEL_MAX))return 0;
        for(size_t j=0;j<i;j++)if(same(p->model_aliases[i],p->model_aliases[j]))return 0;
    }
    return 1;
}

static int record_valid(const bdi_provider_record *r,const char *source,const char *product)
{
    if (!r || !bounded_text(r->record_id,128u) || !bounded_text(r->candidate_id,128u) ||
        !class_char(r->hardware_class) ||
        !digest_valid(r->source_sha256) || !same(r->source_sha256,source) ||
        !bounded_text(r->product,64u) || !same(r->product,product) ||
        !bounded_text(r->route,128u) || !bounded_text(r->route_compatible,128u) ||
        !digest_valid(r->source_revision_sha256) || !digest_valid(r->stock_kernel_sha256) ||
        !digest_valid(r->bridge_kernel_sha256) || !digest_valid(r->nand_dt_sha256) ||
        !bounded_text(r->profile_set_id,96u)||!bounded_text(r->candidate_set_id,96u)||
        !r->nand_profiles||!r->nand_profile_count||r->nand_profile_count>64u ||
        !digest_valid(r->provider_sha256) || !r->provider_bytes || r->provider_bytes>32u*1024u*1024u ||
        !digest_valid(r->payload_sha256) ||
        r->payload_bytes!=BDI_PAYLOAD_BYTES || !variant_paths_valid(r)) return 0;
    for(size_t i=0;i<r->nand_profile_count;i++){
        if(!profile_valid(&r->nand_profiles[i])||
           !(r->nand_profiles[i].catalogue_eligible&class_catalogue_bit(r->hardware_class)))return 0;
        for(size_t j=0;j<i;j++)if(same(r->nand_profiles[i].profile_id,r->nand_profiles[j].profile_id))return 0;
    }
    return 1;
}

static int token_count(const uint8_t *p,size_t n,const char *wanted)
{
    size_t at=0,z=strlen(wanted);int count=0;
    while(at<n){size_t end=at;while(end<n&&p[end])end++;
        if(end==n||end==at)return -1;
        if(end-at==z&&!memcmp(p+at,wanted,z))count++;
        at=end+1;
    }
    return count;
}

static int compatible_property_valid(const uint8_t *p,size_t n)
{
    size_t at=0;unsigned tokens=0;
    if(!p||!n||n>BDI_COMPATIBLE_MAX||p[n-1]!=0)return 0;
    while(at<n){size_t end=at;while(end<n&&p[end])end++;
        if(end==n||end==at||++tokens>64u)return 0;
        for(size_t prior=0;prior<at;){size_t prior_end=prior;while(prior_end<n&&p[prior_end])prior_end++;
            if(prior_end==n)return 0;
            if(prior_end-prior==end-at&&!memcmp(p+prior,p+at,end-at))return 0;
            prior=prior_end+1;
        }
        at=end+1;
    }
    return tokens!=0;
}

static int aliases_overlap(const bdi_nand_profile *a,const bdi_nand_profile *b)
{
    for(size_t i=0;i<a->model_alias_count;i++)
        for(size_t j=0;j<b->model_alias_count;j++)
            if(same(a->model_aliases[i],b->model_aliases[j]))return 1;
    return 0;
}

/* Profile IDs are labels, not uniqueness evidence. Compare the actual runtime
 * NAND parameter group and any shared exact alias so changing record/provider/
 * profile IDs or splitting an alias list cannot duplicate applicability for
 * the same board+candidate+source tuple. */
static int same_nand_parameter_group(const bdi_nand_profile *a,const bdi_nand_profile *b)
{
    return a->jedec_bytes_observed==b->jedec_bytes_observed&&
        a->manufacturer_id==b->manufacturer_id&&a->chip_id==b->chip_id&&
        a->capacity_mib==b->capacity_mib&&a->page_bytes==b->page_bytes&&
        a->erase_bytes==b->erase_bytes&&a->raw_spare_bytes==b->raw_spare_bytes&&
        a->mtd_oob_bytes==b->mtd_oob_bytes&&a->ecc_bits==b->ecc_bits&&
        aliases_overlap(a,b);
}

static int bundle_valid(const bdi_provider_record *records,size_t count,
                        const char *source,const char *product)
{
    if(!records||!count||count>BDI_PROVIDER_ROWS_MAX||!digest_valid(source)||!bounded_text(product,64u))return 0;
    for(size_t i=0;i<count;i++){
        if(!record_valid(&records[i],source,product))return 0;
        for(size_t j=0;j<i;j++){
            const bdi_provider_record *a=&records[i],*b=&records[j];
            if(same(a->record_id,b->record_id)||
               same(a->provider_path,b->provider_path)||
               same(a->provider_path,b->payload_path)||same(a->payload_path,b->provider_path))return 0;
            if(same(a->payload_path,b->payload_path)&&
               (!same(a->payload_sha256,b->payload_sha256)||a->payload_bytes!=b->payload_bytes))return 0;
            if(same(a->source_sha256,b->source_sha256)&&
               same(a->hardware_class,b->hardware_class)&&same(a->candidate_id,b->candidate_id)){
                for(size_t x=0;x<a->nand_profile_count;x++)for(size_t y=0;y<b->nand_profile_count;y++)
                    if(same_nand_parameter_group(&a->nand_profiles[x],&b->nand_profiles[y]))return 0;
            }
        }
    }
    return 1;
}

static int profile_matches(const bdi_nand_profile *p,const bdi_facts *f)
{
    int alias=0;
    for(size_t i=0;i<p->model_alias_count;i++)if(same(f->nand_model,p->model_aliases[i]))alias++;
    return alias==1&&f->jedec_bytes_observed==p->jedec_bytes_observed&&
        f->manufacturer_id==p->manufacturer_id&&f->chip_id==p->chip_id&&
        f->capacity_mib==p->capacity_mib&&f->page_bytes==p->page_bytes&&
        f->erase_bytes==p->erase_bytes&&f->raw_spare_bytes==p->raw_spare_bytes&&
        f->mtd_oob_bytes==p->mtd_oob_bytes&&f->ecc_bits==p->ecc_bits;
}

static unsigned class_catalogue_bit(const char *hardware_class)
{ return class_char(hardware_class)?(1u<<(unsigned)(hardware_class[0]-'A')):0u; }

const bdi_provider_record *bdi_select_provider(
    const bdi_provider_record *records,size_t count,const char *source,
    const char *product,const bdi_facts *f,const bdi_nand_profile **matched_profile,
    bdi_result *result)
{
    const bdi_provider_record *found=NULL;const bdi_nand_profile *found_profile=NULL;
    unsigned matches=0,distinct_routes=0;
    if(matched_profile)*matched_profile=NULL;
    if(result)*result=BDI_BAD_BUNDLE;
    if(!bundle_valid(records,count,source,product))return NULL;
    if(!f||!f->have_compatible||!f->have_nand_model||!f->have_jedec||
       !f->have_geometry||!f->have_mtd_format){
        if(result)*result=BDI_FACTS_MISSING;
        return NULL;
    }
    if(!compatible_property_valid(f->compatible,f->compatible_bytes)){
        if(result)*result=BDI_FACTS_CONFLICT;
        return NULL;
    }
    for(size_t i=0;i<count;i++){
        int token=token_count(f->compatible,f->compatible_bytes,records[i].route_compatible);
        if(token<0||token>1){if(result)*result=BDI_FACTS_CONFLICT;return NULL;}
        if(token==1){
            unsigned prior=0;
            for(size_t j=0;j<i;j++)if(same(records[j].route_compatible,records[i].route_compatible)&&
                token_count(f->compatible,f->compatible_bytes,records[j].route_compatible)==1)prior=1;
            if(!prior&&++distinct_routes>1){if(result)*result=BDI_FACTS_CONFLICT;return NULL;}
        }
    }
    for(size_t i=0;i<count;i++){
        const bdi_provider_record *r=&records[i];
        int compatible=token_count(f->compatible,f->compatible_bytes,r->route_compatible);
        if(compatible<0||compatible>1){if(result)*result=BDI_FACTS_CONFLICT;return NULL;}
        if(compatible!=1)continue;
        for(size_t k=0;k<r->nand_profile_count;k++)if(
            (r->nand_profiles[k].catalogue_eligible&class_catalogue_bit(r->hardware_class))&&
            profile_matches(&r->nand_profiles[k],f)){
            found=r;found_profile=&r->nand_profiles[k];matches++;
        }
    }
    if(matches==0){if(result)*result=BDI_NO_MATCH;return NULL;}
    if(matches!=1){if(result)*result=BDI_AMBIGUOUS;return NULL;}
    if(matched_profile)*matched_profile=found_profile;
    if(result)*result=BDI_OK;
    return found;
}

static int open_beneath(int rootfd,const char *path)
{
    int dir=-1,next=-1;const char *p;
    if(rootfd<0||!path||path[0]!='/'||!path[1]){errno=EINVAL;return -1;}
    dir=dup(rootfd);if(dir<0)return -1;
    p=path+1;
    for(;;){const char *slash=strchr(p,'/');size_t n=slash?(size_t)(slash-p):strlen(p);
        if(!n||n>NAME_MAX||(n==1&&p[0]=='.')||(n==2&&p[0]=='.'&&p[1]=='.')){errno=EINVAL;goto fail;}
        char part[NAME_MAX+1];memcpy(part,p,n);part[n]=0;
        if(slash){next=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(next<0)goto fail;
            close(dir);dir=next;next=-1;p=slash+1;continue;}
        int fd=openat(dir,part,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        close(dir);return fd;
    }
fail:
    if(next>=0)close(next);
    if(dir>=0)close(dir);
    return -1;
}

int bdi_open_verified_at(int rootfd,const char *path,const char *sha256,
                         uint64_t bytes,int executable,bdi_result *result)
{
    struct stat st,after;bi_sha256_ctx c;uint8_t digest[32],buf[16384];char hex[65];uint64_t total=0;
    if(result)*result=BDI_BAD_FILE;
    if(!digest_valid(sha256)||!bytes||bytes>UINT32_MAX||executable<0||executable>1){if(result)*result=BDI_BAD_PATH;return -1;}
    int fd=open_beneath(rootfd,path);if(fd<0){if(result)*result=BDI_BAD_PATH;return -1;}
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=
#ifdef BDI_TEST
       geteuid()||
#else
       0||
#endif
       st.st_nlink!=1||(st.st_mode&06222u)||st.st_size<0||(uint64_t)st.st_size!=bytes||
       (executable&&!(st.st_mode&0111u))){close(fd);if(result)*result=BDI_BAD_FILE;return -1;}
    bi_sha256_init(&c);
    for(;;){ssize_t n=read(fd,buf,sizeof(buf));if(n<0&&errno==EINTR)continue;if(n<0){close(fd);if(result)*result=BDI_BAD_FILE;return -1;}if(!n)break;
        if((uint64_t)n>bytes-total){close(fd);if(result)*result=BDI_BAD_FILE;return -1;}total+=(uint64_t)n;bi_sha256_update(&c,buf,(size_t)n);}
    if(total!=bytes||fstat(fd,&after)||st.st_dev!=after.st_dev||st.st_ino!=after.st_ino||st.st_size!=after.st_size){close(fd);if(result)*result=BDI_BAD_FILE;return -1;}
    bi_sha256_final(&c,digest);bi_sha256_hex(digest,hex);
    if(strcmp(hex,sha256)){close(fd);if(result)*result=BDI_BAD_HASH;return -1;}
    if(lseek(fd,0,SEEK_SET)<0){close(fd);if(result)*result=BDI_BAD_FILE;return -1;}
    if(result)*result=BDI_OK;
    return fd;
}

int bdi_resolve_mtd0_sysfs(const char *root,char *resolved,size_t cap)
{
    char devices[PATH_MAX],linkpath[PATH_MAX],device_root[PATH_MAX],target[PATH_MAX];
    struct stat target_st,link_st,lst;int n;
    if(!root||!resolved||cap==0)return -1;
    n=snprintf(devices,sizeof(devices),"%s/sys/devices",root);
    if(n<0||(size_t)n>=sizeof(devices))return -1;
    n=snprintf(linkpath,sizeof(linkpath),"%s/sys/class/mtd/mtd0",root);
    if(n<0||(size_t)n>=sizeof(linkpath))return -1;
    if(lstat(linkpath,&lst)||!S_ISLNK(lst.st_mode)||
       !realpath(devices,device_root)||!realpath(linkpath,target))return -1;
    size_t prefix=strlen(device_root),len=strlen(target);
    if(len<=prefix||strncmp(target,device_root,prefix)||target[prefix]!='/'||len<9||
       strcmp(target+len-9,"/mtd/mtd0"))return -1;
    if(stat(target,&target_st)||!S_ISDIR(target_st.st_mode)||stat(linkpath,&link_st)||
       target_st.st_dev!=link_st.st_dev||target_st.st_ino!=link_st.st_ino)return -1;
    if(len>=cap)return -1;
    memcpy(resolved,target,len+1);return 0;
}

const char *bdi_result_name(bdi_result r)
{
    static const char *const names[]={"ok","invalid provider bundle","required runtime facts missing",
        "invalid device-tree compatible","invalid kernel NAND identity","invalid MTD sysfs values",
        "invalid MTD character-device binding","invalid /proc/mtd mapping",
        "runtime facts conflict or repeat","no provider profile matched","no full OEM candidate contract passed","multiple OEM candidate contracts passed","multiple provider profiles matched",
        "resource path rejected","resource file invalid","resource hash mismatch","provider exec failed"};
    return (unsigned)r<sizeof(names)/sizeof(names[0])?names[r]:"dispatcher error";
}

int bdi_parse_compatible(const uint8_t *p,size_t n,bdi_facts *f)
{
    if(!f||!compatible_property_valid(p,n))return -1;
    memcpy(f->compatible,p,n);f->compatible_bytes=n;f->have_compatible=1;return 0;
}

static int parse_decimal(const char *s,unsigned *out)
{
    unsigned value=0,digits=0;const unsigned char *p=(const unsigned char *)s;
    if(!s||!out)return -1;
    while(*p>='0'&&*p<='9'){
        unsigned digit=(unsigned)(*p++-'0');
        if(value>(UINT_MAX-digit)/10u)return -1;
        value=value*10u+digit;digits++;
    }
    if(!digits)return -1;
    while(*p=='\n'||*p=='\r'||*p==' '||*p=='\t')p++;
    if(*p)return -1;
    *out=value;return 0;
}

static int trim_ascii(const char *s,char *out,size_t cap)
{
    size_t n;
    if(!s||!out||!cap)return -1;
    n=strlen(s);while(n&&(s[n-1]=='\n'||s[n-1]=='\r'||s[n-1]==' '||s[n-1]=='\t'))n--;
    if(!n||n>=cap)return -1;
    for(size_t i=0;i<n;i++)if((unsigned char)s[i]<0x20u||(unsigned char)s[i]==0x7fu)return -1;
    memcpy(out,s,n);out[n]=0;return 0;
}

static int parse_dev_number(const char *s,unsigned *major_no,unsigned *minor_no)
{
    const char *colon;char left[32],right[32];size_t n;
    if(!s||(colon=strchr(s,':'))==NULL||strchr(colon+1,':'))return -1;
    n=(size_t)(colon-s);if(!n||n>=sizeof(left)||strlen(colon+1)>=sizeof(right))return -1;
    memcpy(left,s,n);left[n]=0;strcpy(right,colon+1);
    return parse_decimal(left,major_no)||parse_decimal(right,minor_no)?-1:0;
}

int bdi_parse_mtd_sysfs(const char *type,const char *offset,const char *size,
                        const char *erase,const char *page,const char *oob,
                        const char *ecc,const char *dev,bdi_mtd_sysfs_values *v)
{
    char normalized[16];bdi_mtd_sysfs_values x={0};
    if(!v||trim_ascii(type,normalized,sizeof(normalized))||strcmp(normalized,"nand")||
       (offset&&parse_decimal(offset,&x.offset))||parse_decimal(size,&x.size)||!x.size||
       parse_decimal(erase,&x.erase_bytes)||!x.erase_bytes||x.size%x.erase_bytes||
       parse_decimal(page,&x.page_bytes)||!x.page_bytes||x.erase_bytes%x.page_bytes||
       parse_decimal(oob,&x.oob_bytes)||!x.oob_bytes||parse_decimal(ecc,&x.ecc_bits)||!x.ecc_bits||
       parse_dev_number(dev,&x.dev_major,&x.dev_minor)||x.offset!=0)return -1;
    *v=x;return 0;
}

int bdi_parse_mtd_sysfs_optional_offset(int offset_status,const char *offset,
                        const char *type,const char *size,const char *erase,
                        const char *page,const char *oob,const char *ecc,
                        const char *dev,bdi_mtd_sysfs_values *v)
{
    if(offset_status==0)return offset?bdi_parse_mtd_sysfs(type,offset,size,erase,page,oob,ecc,dev,v):-1;
    if(offset_status==1)return offset? -1 : bdi_parse_mtd_sysfs(type,NULL,size,erase,page,oob,ecc,dev,v);
    return -1;
}

int bdi_parse_kernel_identity(const char *p,size_t n,bdi_facts *f)
{
    char *copy;unsigned have_model=0,have_id=0,have_geom=0;
    if(!p||!n||!f||memchr(p,0,n))return -1;
    copy=malloc(n+1);if(!copy)return -1;
    memcpy(copy,p,n);copy[n]=0;
    for(char *line=copy;line&&*line;){char *next=strchr(line,'\n');if(next)*next++=0;
        line[strcspn(line,"\r")]=0;const char *q=strstr(line,"nand: device found, Manufacturer ID:");
        if(q){unsigned long m=0,c=0;int used=0;if(have_id++||sscanf(q,"nand: device found, Manufacturer ID: 0x%lx, Chip ID: 0x%lx%n",&m,&c,&used)!=2||!used||q[used]||m>255||c>255){free(copy);return -1;}f->manufacturer_id=(unsigned)m;f->chip_id=(unsigned)c;}
        q=strstr(line,"nand: ");
        if(q&&strstr(q," SPI NAND ")){char vendor[32]={0},model[BDI_MODEL_MAX]={0};if(have_model++||sscanf(q,"nand: %31s %63s",vendor,model)!=2||!model[0]){free(copy);return -1;}snprintf(f->nand_model,sizeof(f->nand_model),"%s",model);}
        q=strstr(line,"nand: ");
        if(q&&strstr(q,"erase size:")&&strstr(q,"page size:")&&strstr(q,"OOB size:")){
            unsigned cap=0,erase=0,page=0,oob=0;char cell[32]={0};int used=0;
            if(have_geom++||sscanf(q,"nand: %u MiB, %31[^,], erase size: %u KiB, page size: %u, OOB size: %u%n",&cap,cell,&erase,&page,&oob,&used)!=5||!used||q[used]||strcmp(cell,"SLC")||!cap||!erase||!page||!oob||erase>UINT_MAX/1024u){free(copy);return -1;}
            f->capacity_mib=cap;f->erase_bytes=erase*1024u;f->page_bytes=page;f->raw_spare_bytes=oob;
        }
        line=next;
    }
    free(copy);if(have_id!=1||have_model!=1||have_geom!=1)return -1;
    f->jedec_bytes_observed=2u;f->have_jedec=1;f->have_nand_model=1;f->have_geometry=1;return 0;
}

int bdi_parse_proc_mtd0(const char *p,size_t n,unsigned expected_size,unsigned expected_erase)
{
    char data[8192];unsigned rows=0;
    if(!p||n>=sizeof(data)||memchr(p,0,n))return -1;
    memcpy(data,p,n);data[n]=0;
    char *save=NULL;
    for(char *line=strtok_r(data,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
        unsigned idx=0;unsigned long long size=0,erase=0;char name[128]={0};int used=0;
        if(strncmp(line,"mtd",3))continue;
        if(sscanf(line,"mtd%u: %llx %llx \"%127[^\"]\"%n",&idx,&size,&erase,name,&used)!=4){
            if(!strncmp(line,"mtd0:",5))return -1;
            continue;
        }
        if(line[used]){const char *q=line+used;while(*q==' '||*q=='\t'||*q=='\r')q++;if(*q)return -1;}
        if(idx==0){rows++;if(rows!=1||size!=expected_size||erase!=expected_erase||!name[0])return -1;}
    }
    return rows==1?0:-1;
}

#ifndef BDI_TEST

static int read_live_path(const char *path,uint8_t *out,size_t cap,size_t *used)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;size_t n=0;
    while(n<cap){ssize_t z=read(fd,out+n,cap-n);if(z<0&&errno==EINTR)continue;if(z<0){close(fd);return -1;}if(!z)break;n+=(size_t)z;}
    close(fd);if(n==cap)return -1;*used=n;return 0;
}

static int read_sysfs_attr(int dirfd,const char *name,char *out,size_t cap)
{
    errno=0;
    int fd=openat(dirfd,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    size_t n=0;while(n+1<cap){ssize_t z=read(fd,out+n,cap-1-n);if(z<0&&errno==EINTR)continue;if(z<0){close(fd);return -1;}if(!z)break;n+=(size_t)z;}
    if(n+1==cap||memchr(out,0,n)){close(fd);return -1;}out[n]=0;close(fd);return 0;
}

static int read_sysfs_attr_optional(int dirfd,const char *name,char *out,size_t cap)
{
    if(read_sysfs_attr(dirfd,name,out,cap)==0)return 0;
    return errno==ENOENT?1:-1;
}

static bdi_result collect_mtd0_format(bdi_facts *f)
{
    char resolved[PATH_MAX],type[128],offset[128],size[128],erase[128],page[128],oob[128],ecc[128],dev[64],proc[8192];
    size_t proc_bytes=0;int offset_status; bdi_mtd_sysfs_values v;
    if(bdi_resolve_mtd0_sysfs("/",resolved,sizeof(resolved)))return BDI_FACTS_MTD_SYSFS_INVALID;
    int dir=open(resolved,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(dir<0)return BDI_FACTS_MTD_SYSFS_INVALID;
    offset_status=read_sysfs_attr_optional(dir,"offset",offset,sizeof(offset));
    if(offset_status<0||offset_status>1||read_sysfs_attr(dir,"type",type,sizeof(type))||
       read_sysfs_attr(dir,"size",size,sizeof(size))||read_sysfs_attr(dir,"erasesize",erase,sizeof(erase))||
       read_sysfs_attr(dir,"writesize",page,sizeof(page))||read_sysfs_attr(dir,"oobsize",oob,sizeof(oob))||
       read_sysfs_attr(dir,"ecc_strength",ecc,sizeof(ecc))||read_sysfs_attr(dir,"dev",dev,sizeof(dev))||
       bdi_parse_mtd_sysfs_optional_offset(offset_status,
            offset_status==0?offset:NULL,type,size,erase,page,oob,ecc,dev,&v)){
        close(dir);return BDI_FACTS_MTD_SYSFS_INVALID;
    }
    close(dir);
    /* Linux 4.4 exports offset only for partitions. Its absence means mtd0 is
     * the master device, whose physical offset is necessarily zero. */
    if(v.offset!=0||(uint64_t)v.size>(uint64_t)f->capacity_mib*1024u*1024u||
       v.page_bytes!=f->page_bytes||v.erase_bytes!=f->erase_bytes||
       v.oob_bytes!=f->raw_spare_bytes)return BDI_FACTS_MTD_SYSFS_INVALID;
    if(read_live_path("/proc/mtd",(uint8_t*)proc,sizeof(proc)-1,&proc_bytes))return BDI_FACTS_PROC_MTD_INVALID;
    proc[proc_bytes]=0;
    if(bdi_parse_proc_mtd0(proc,proc_bytes,v.size,v.erase_bytes))return BDI_FACTS_PROC_MTD_INVALID;
    struct stat devst;
    if(stat("/dev/mtd0",&devst)||!S_ISCHR(devst.st_mode)||major(devst.st_rdev)!=v.dev_major||minor(devst.st_rdev)!=v.dev_minor)
        return BDI_FACTS_MTD_DEVICE_INVALID;
    f->mtd_oob_bytes=v.oob_bytes;f->ecc_bits=v.ecc_bits;f->have_mtd_format=1;return BDI_OK;
}

static bdi_result read_live_facts(bdi_facts *f)
{
    uint8_t bytes[BDI_COMPATIBLE_MAX],log[BDI_KERNEL_LOG_MAX];size_t n=0;long cap,got;
    memset(f,0,sizeof(*f));
    if(read_live_path("/sys/firmware/devicetree/base/compatible",bytes,sizeof(bytes),&n))return BDI_FACTS_MISSING;
    if(bdi_parse_compatible(bytes,n,f))return BDI_FACTS_COMPAT_INVALID;
    cap=syscall(SYS_syslog,10,NULL,0);if(cap<=0||cap>(long)sizeof(log))return BDI_FACTS_MISSING;
    got=syscall(SYS_syslog,3,log,(int)cap);if(got<=0||got>cap)return BDI_FACTS_MISSING;
    if(bdi_parse_kernel_identity((const char*)log,(size_t)got,f))return BDI_FACTS_KERNEL_ID_INVALID;
    return collect_mtd0_format(f);
}
#endif

static int identity_text(const char *s)
{
    if(!s||!*s)return 0;
    for(const unsigned char*p=(const unsigned char*)s;*p;p++)
        if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||
             *p=='_'||*p=='-'||*p=='.'||*p=='@'||*p==':'||*p==','||*p=='+'))return 0;
    return 1;
}

static int json_has_string(const char *json,const char *key,const char *value)
{
    char needle[512];
    if(!identity_text(key)||!identity_text(value))return 0;
    int n=snprintf(needle,sizeof(needle),"\"%s\":\"%s\"",key,value);
    return n>0&&(size_t)n<sizeof(needle)&&strstr(json,needle)!=NULL;
}

/* Flat, bounded inspect protocol. Reject duplicates, nesting and trailing
 * data before inspecting identity values; diagnostic text grants no rights. */
static const char *inspect_json_string_end(const char *p)
{
    if(*p++!='"')return NULL;
    for(;*p;p++){
        unsigned char c=(unsigned char)*p;
        if(c=='"')return p+1;
        if(c<0x20u)return NULL;
        if(c=='\\'){
            c=(unsigned char)*++p;
            if(c=='u'){
                for(unsigned i=0;i<4;i++){c=(unsigned char)*++p;
                    if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return NULL;}
            }else if(!c||!strchr("\"\\/bfnrt",c))return NULL;
        }
    }
    return NULL;
}
static const char *inspect_json_space(const char *p)
{while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;return p;}
static int inspect_report_format_valid(const char *json)
{
    static const char *const keys[]={"install_ready","source_sha256","product","route","hardware_class","candidate_id","return_ready","rejected_check","reason"};
    unsigned seen=0;const char *p=inspect_json_space(json);
    if(*p++!='{')return 0;
    for(;;){
        p=inspect_json_space(p);if(*p!='"')return 0;
        const char *end=inspect_json_string_end(p);if(!end)return 0;
        unsigned k;for(k=0;k<sizeof(keys)/sizeof(keys[0]);k++)
            if((size_t)(end-p)==strlen(keys[k])+2u&&!memcmp(p+1,keys[k],strlen(keys[k])))break;
        if(k==sizeof(keys)/sizeof(keys[0])||(seen&(1u<<k)))return 0;
        seen|=1u<<k;p=inspect_json_space(end);if(*p++!=':')return 0;p=inspect_json_space(p);
        if(k==0||k==6){if(!strncmp(p,"true",4))p+=4;else if(!strncmp(p,"false",5))p+=5;else return 0;}
        else{p=inspect_json_string_end(p);if(!p)return 0;}
        p=inspect_json_space(p);if(*p=='}'){p=inspect_json_space(p+1);return !*p&&(seen&63u)==63u;}
        if(*p++!=',')return 0;
    }
}

int bdi_inspect_report_matches(const char *json,const bdi_provider_record *r)
{
    return json&&inspect_report_format_valid(json)&&strstr(json,"\"install_ready\":true")&&
        json_has_string(json,"source_sha256",r->source_sha256)&&
        json_has_string(json,"product",r->product)&&
        json_has_string(json,"route",r->route)&&
        json_has_string(json,"hardware_class",r->hardware_class)&&
        json_has_string(json,"candidate_id",r->candidate_id);
}

const bdi_provider_record *bdi_select_inspected_provider(
    const bdi_provider_record *records,size_t count,const char *source,const char *product,
    const bdi_facts *facts,const int *ready,const bdi_nand_profile **matched_profile,
    bdi_result *result)
{
    const bdi_provider_record *selected=NULL;const bdi_nand_profile *selected_profile=NULL;
    unsigned eligible=0,passing=0;
    if(matched_profile)*matched_profile=NULL;
    if(!ready){if(result)*result=BDI_BAD_BUNDLE;return NULL;}
    if(!bdi_select_provider(records,count,source,product,facts,NULL,result)&&
       result&&*result!=BDI_AMBIGUOUS)return NULL;
    for(size_t i=0;i<count;i++){
        const bdi_provider_record *r=&records[i];
        if(token_count(facts->compatible,facts->compatible_bytes,r->route_compatible)!=1)continue;
        const bdi_nand_profile *profile=NULL;unsigned nprofiles=0;
        for(size_t k=0;k<r->nand_profile_count;k++)if(
            (r->nand_profiles[k].catalogue_eligible&class_catalogue_bit(r->hardware_class))&&
            profile_matches(&r->nand_profiles[k],facts)){profile=&r->nand_profiles[k];nprofiles++;}
        if(!nprofiles)continue;
        if(nprofiles!=1){if(result)*result=BDI_AMBIGUOUS;return NULL;}
        eligible++;
        if(ready[i]<0){if(result)*result=BDI_EXEC_FAILED;return NULL;}
        if(ready[i]==1){selected=r;selected_profile=profile;passing++;}
    }
    if(!eligible){if(result)*result=BDI_NO_MATCH;return NULL;}
    if(!passing){if(result)*result=BDI_CANDIDATE_UNAVAILABLE;return NULL;}
    if(passing!=1){if(result)*result=BDI_CANDIDATE_AMBIGUOUS;return NULL;}
    if(matched_profile)*matched_profile=selected_profile;
    if(result)*result=BDI_OK;
    return selected;
}

#if !defined(BDI_TEST) || defined(BDI_INSPECT_TEST)
/* Diagnostics never grant readiness. Preserve only bounded text from the
 * verified provider and only when its exact compiled identity matches. */
static char g_candidate_rejection[257],g_rejected_candidate_revision[65];
static unsigned g_rejected_candidates;
static int inspect_json_text(const char *json,const char *key,char *out,size_t cap)
{
    char needle[96];size_t used=0;
    int n=snprintf(needle,sizeof(needle),"\"%s\":\"",key);
    if(n<0||(size_t)n>=sizeof(needle)||!cap)return -1;
    const char *p=strstr(json,needle);if(!p)return -1;p+=n;
    while(*p&&*p!='"'){
        unsigned char c=(unsigned char)*p++;
        if(c=='\\'){c=(unsigned char)*p++;if(c!='"'&&c!='\\'&&c!='/')return -1;}
        if(c<0x20||c==0x7f||used+1>=cap)return -1;
        out[used++]=(char)c;
    }
    if(*p!='"')return -1;
    out[used]=0;return 0;
}
static void remember_inspect_rejection(const char *json,const bdi_provider_record *r)
{
    char check[65],reason[257],detail[257];
    if(!json_has_string(json,"source_sha256",r->source_sha256)||
       !json_has_string(json,"product",r->product)||!json_has_string(json,"route",r->route)||
       !json_has_string(json,"hardware_class",r->hardware_class)||
       !json_has_string(json,"candidate_id",r->candidate_id)||
       inspect_json_text(json,"rejected_check",check,sizeof(check))||
       inspect_json_text(json,"reason",reason,sizeof(reason))||!check[0]||!reason[0])return;
    for(const char *p=check;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'))return;
    int n=snprintf(detail,sizeof(detail),"%s/%s: %s",r->hardware_class,check,reason);
    if(n<0)return;
    if((size_t)n>=sizeof(detail))snprintf(detail,sizeof(detail),"%s/%s",r->hardware_class,check);
    size_t used=strlen(g_candidate_rejection),add=strlen(detail);
    if(!used)memcpy(g_candidate_rejection,detail,add+1);
    else if(used+2+add<sizeof(g_candidate_rejection)){
        memcpy(g_candidate_rejection+used,"; ",2);memcpy(g_candidate_rejection+used+2,detail,add+1);
    }
    g_rejected_candidates++;
    if(g_rejected_candidates==1)snprintf(g_rejected_candidate_revision,sizeof(g_rejected_candidate_revision),"%s",r->source_revision_sha256);
    else g_rejected_candidate_revision[0]=0;
}
/* Execution metadata is diagnostic only. It never changes provider identity
 * binding or turns a failed/rejected inspection into install readiness. */
static struct {
    char outcome[32],last_stage[65],last_status[32];
    unsigned long long elapsed_ms;
    unsigned budget_ms;
    int system_errno,exit_code,term_signal,stderr_truncated,child_reaped;
    size_t stdout_bytes,stderr_bytes;
} g_inspect_diag;
static char g_inspect_detail[2048],g_inspect_revision[65];

static int inspect_token(const char *s)
{
    if(!s[0])return 0;
    for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='_'))return 0;
    return 1;
}
struct inspect_progress {char line[192];size_t used;int discard;};
static void inspect_progress_feed(struct inspect_progress *p,const char *data,size_t size)
{
    for(size_t i=0;i<size;i++){
        unsigned char c=(unsigned char)data[i];
        if(c=='\n'){
            if(!p->discard){
                char stage[65],status[32],extra;
                p->line[p->used]=0;
                if(sscanf(p->line,"BI_INSPECT_STAGE %64s %31s %c",stage,status,&extra)==2&&
                   inspect_token(stage)&&inspect_token(status)){
                    snprintf(g_inspect_diag.last_stage,sizeof(g_inspect_diag.last_stage),"%s",stage);
                    snprintf(g_inspect_diag.last_status,sizeof(g_inspect_diag.last_status),"%s",status);
                }
            }
            p->used=0;p->discard=0;
        }else if(c<0x20u||c>0x7eu||p->used+1>=sizeof(p->line))p->discard=1;
        else if(!p->discard)p->line[p->used++]=(char)c;
    }
}
static int inspect_finish(const bdi_provider_record *r,const struct timespec *start,
                          const char *outcome,int result)
{
    struct timespec now;
    if(start&&clock_gettime(CLOCK_MONOTONIC,&now)==0){
        long long ns=(long long)(now.tv_sec-start->tv_sec)*1000000000LL+now.tv_nsec-start->tv_nsec;
        if(ns>0)g_inspect_diag.elapsed_ms=(unsigned long long)ns/1000000ULL;
    }
    snprintf(g_inspect_diag.outcome,sizeof(g_inspect_diag.outcome),"%s",outcome);
    snprintf(g_inspect_revision,sizeof(g_inspect_revision),"%s",r->source_revision_sha256?r->source_revision_sha256:"");
    snprintf(g_inspect_detail,sizeof(g_inspect_detail),
        "tool_revision=R6-preflight-v1;outcome=%s;elapsed_ms=%llu;budget_ms=%u;errno=%d;exit_code=%d;signal=%d;last_stage=%s;last_status=%s;stdout_bytes=%zu;stderr_bytes=%zu;stderr_truncated=%d;child_reaped=%d;candidate=%s;provider=%s;provider_sha256=%s;source_revision_sha256=%s",
        g_inspect_diag.outcome,g_inspect_diag.elapsed_ms,g_inspect_diag.budget_ms,
        g_inspect_diag.system_errno,g_inspect_diag.exit_code,g_inspect_diag.term_signal,
        g_inspect_diag.last_stage,g_inspect_diag.last_status,g_inspect_diag.stdout_bytes,
        g_inspect_diag.stderr_bytes,g_inspect_diag.stderr_truncated,g_inspect_diag.child_reaped,
        r->candidate_id?r->candidate_id:"",r->provider_path?r->provider_path:"",
        r->provider_sha256?r->provider_sha256:"",r->source_revision_sha256?r->source_revision_sha256:"");
    return result;
}
static int inspect_negative_report_matches(const char *json,const bdi_provider_record *r)
{
    return json&&inspect_report_format_valid(json)&&strstr(json,"\"install_ready\":false")&&
        !strstr(json,"\"install_ready\":true")&&
        json_has_string(json,"source_sha256",r->source_sha256)&&
        json_has_string(json,"product",r->product)&&json_has_string(json,"route",r->route)&&
        json_has_string(json,"hardware_class",r->hardware_class)&&
        json_has_string(json,"candidate_id",r->candidate_id);
}
/* Exec only the hash-pinned variant provider's inspect-only CLI. Both time
 * limits share one budget. The child alarm is a backstop for parent failure,
 * deliberately later than the monotonic deadline, never a second short cap. */
static int inspect_provider(int provider,const bdi_provider_record *r)
{
    int out[2]={-1,-1},err[2]={-1,-1},execerr[2]={-1,-1};
    int status=0,child_done=0,wait_valid=0,eof[2]={0,0};pid_t pid=-1;
    char output[8192];size_t used=0;struct inspect_progress progress={{0},0,0};
    struct timespec start,deadline,now;const char *failure=NULL;
    memset(&g_inspect_diag,0,sizeof(g_inspect_diag));g_inspect_detail[0]=0;g_inspect_revision[0]=0;
    g_inspect_diag.exit_code=-1;g_inspect_diag.budget_ms=BDI_INSPECT_TIMEOUT_MS;
    snprintf(g_inspect_diag.last_stage,sizeof(g_inspect_diag.last_stage),"launch");
    if(clock_gettime(CLOCK_MONOTONIC,&start)){
        g_inspect_diag.system_errno=errno;return inspect_finish(r,NULL,"clock_error",-1);
    }
    deadline=start;deadline.tv_sec+=BDI_INSPECT_TIMEOUT_MS/1000;
    deadline.tv_nsec+=(long)(BDI_INSPECT_TIMEOUT_MS%1000)*1000000L;
    if(deadline.tv_nsec>=1000000000L){deadline.tv_sec++;deadline.tv_nsec-=1000000000L;}
    if(pipe2(out,O_CLOEXEC)||pipe2(err,O_CLOEXEC)||pipe2(execerr,O_CLOEXEC)){
        failure="pipe_error";g_inspect_diag.system_errno=errno;goto done;
    }
    pid=fork();if(pid<0){failure="fork_error";g_inspect_diag.system_errno=errno;goto done;}
    if(pid==0){
        close(out[0]);close(err[0]);close(execerr[0]);
        int event[2]={1,0};
        if(dup2(out[1],STDOUT_FILENO)<0||dup2(err[1],STDERR_FILENO)<0||
           fcntl(provider,F_SETFD,0)<0){event[1]=errno;(void)write(execerr[1],event,sizeof(event));_exit(126);}
        close(out[1]);close(err[1]);
        alarm((BDI_INSPECT_TIMEOUT_MS+999u)/1000u+2u);
        char *const argv[]={(char*)r->provider_path,(char*)"--bridge-dispatch-inspect",NULL};
        char *const envp[]={(char*)"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
            (char*)"BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1",
            (char*)"BRIDGE_RUNTIME_MODE=RUNTIME_GATED",
            (char*)"BRIDGE_DISPATCH_INSPECT=1",NULL};
        fexecve(provider,argv,envp);event[0]=2;event[1]=errno;
        (void)write(execerr[1],event,sizeof(event));_exit(127);
    }
    close(out[1]);out[1]=-1;close(err[1]);err[1]=-1;close(execerr[1]);execerr[1]=-1;
    {int fds[3]={out[0],err[0],execerr[0]};for(unsigned i=0;i<3;i++){
        int flags=fcntl(fds[i],F_GETFL);
        if(flags<0||fcntl(fds[i],F_SETFL,flags|O_NONBLOCK)<0){failure="setup_error";g_inspect_diag.system_errno=errno;goto done;}
    }}
    for(;;){
        if(clock_gettime(CLOCK_MONOTONIC,&now)){failure="clock_error";g_inspect_diag.system_errno=errno;break;}
        if(now.tv_sec>deadline.tv_sec||(now.tv_sec==deadline.tv_sec&&now.tv_nsec>=deadline.tv_nsec)){
            failure="timeout";break;
        }
        /* At most 8KiB per stream per turn: noisy children cannot starve the
         * deadline or waitpid. Never publish arbitrary stderr or raw output. */
        int fds[2]={out[0],err[0]};
        for(unsigned stream=0;stream<2&&!failure;stream++)for(unsigned chunk=0;chunk<8&&!eof[stream];chunk++){
            char buf[1024];ssize_t n=read(fds[stream],buf,sizeof(buf));
            if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)break;failure="read_error";g_inspect_diag.system_errno=errno;break;}
            if(!n){eof[stream]=1;break;}
            if(!stream){g_inspect_diag.stdout_bytes+=(size_t)n;
                if((size_t)n>sizeof(output)-1-used){failure="output_overflow";break;}
                memcpy(output+used,buf,(size_t)n);used+=(size_t)n;
            }else{
                g_inspect_diag.stderr_bytes+=(size_t)n;
                inspect_progress_feed(&progress,buf,(size_t)n);
                if(g_inspect_diag.stderr_bytes>65536u)g_inspect_diag.stderr_truncated=1;
            }
        }
        if(failure)break;
        if(!child_done){pid_t w=waitpid(pid,&status,WNOHANG);
            if(w==pid){child_done=1;wait_valid=1;g_inspect_diag.child_reaped=1;}
            else if(w<0&&errno!=EINTR){failure="wait_error";g_inspect_diag.system_errno=errno;child_done=1;break;}}
        if(child_done&&eof[0]&&eof[1])break;
        long long ns=(long long)(deadline.tv_sec-now.tv_sec)*1000000000LL+deadline.tv_nsec-now.tv_nsec;
        int ms=(int)((ns+999999LL)/1000000LL);if(ms>100)ms=100;
        struct pollfd polls[2]={{eof[0]?-1:out[0],POLLIN|POLLHUP,0},{eof[1]?-1:err[0],POLLIN|POLLHUP,0}};
        int pr=poll(polls,2,ms);
        if(pr<0&&errno!=EINTR){failure="poll_error";g_inspect_diag.system_errno=errno;break;}
    }
done:
    if(pid>0&&!child_done){
        (void)kill(pid,SIGKILL);pid_t w;do{w=waitpid(pid,&status,0);}while(w<0&&errno==EINTR);
        if(w==pid){wait_valid=1;g_inspect_diag.child_reaped=1;}
        else if(!failure){failure="wait_error";g_inspect_diag.system_errno=errno;}
    }
    if(wait_valid){if(WIFEXITED(status))g_inspect_diag.exit_code=WEXITSTATUS(status);
        if(WIFSIGNALED(status))g_inspect_diag.term_signal=WTERMSIG(status);}
    if(pid>0&&execerr[0]>=0){int event[2];ssize_t n=read(execerr[0],event,sizeof(event));
        if(n==(ssize_t)sizeof(event)){g_inspect_diag.system_errno=event[1];if(!failure)failure=event[0]==2?"exec_error":"setup_error";}}
    {int fds[6]={out[0],out[1],err[0],err[1],execerr[0],execerr[1]};
        for(unsigned i=0;i<6;i++)if(fds[i]>=0)close(fds[i]);}
    if(failure)return inspect_finish(r,&start,failure,-1);
    if(!wait_valid)return inspect_finish(r,&start,"wait_error",-1);
    if(!WIFEXITED(status))return inspect_finish(r,&start,"signal",-1);
    if(memchr(output,0,used))return inspect_finish(r,&start,"report_invalid",-1);
    output[used]=0;
    if(WEXITSTATUS(status)==1){
        if(!inspect_negative_report_matches(output,r))return inspect_finish(r,&start,"report_invalid",-1);
        remember_inspect_rejection(output,r);return inspect_finish(r,&start,"rejected",0);
    }
    if(WEXITSTATUS(status)!=0)return inspect_finish(r,&start,"exit_error",-1);
    if(!bdi_inspect_report_matches(output,r)||strstr(output,"\"install_ready\":false"))
        return inspect_finish(r,&start,"report_invalid",-1);
    return inspect_finish(r,&start,"ready",1);
}
#endif

#ifndef BDI_TEST
/* Verify every artifact and run its full read-only contract probe before
 * choosing. Same route and NAND tuples can resolve only through source-bound
 * SBL/APPSBL/layout qualification; multiple passing candidates stay blocked. */
static const bdi_provider_record *preflight_candidate_contracts(int rootfd,
        const bdi_provider_record *records,size_t count,const char *source,
        const char *product,const bdi_facts *facts,const bdi_nand_profile **matched_profile,
        bdi_result *result)
{
    int probes[BDI_PROVIDER_ROWS_MAX]={0};char passing_detail[2048]="",passing_revision[65]="";
    g_candidate_rejection[0]=0;g_rejected_candidate_revision[0]=0;g_rejected_candidates=0;
    if(matched_profile)*matched_profile=NULL;
    if(bdi_select_provider(records,count,source,product,facts,NULL,result)==NULL&&
       result&&*result!=BDI_AMBIGUOUS)return NULL;
    for(size_t i=0;i<count;i++){
        const bdi_provider_record *r=&records[i];
        int token=token_count(facts->compatible,facts->compatible_bytes,r->route_compatible);
        if(token<0||token>1){if(result)*result=BDI_FACTS_CONFLICT;return NULL;}
        if(token!=1)continue;
        unsigned profile_matches_count=0;
        for(size_t k=0;k<r->nand_profile_count;k++)if(
            (r->nand_profiles[k].catalogue_eligible&class_catalogue_bit(r->hardware_class))&&
            profile_matches(&r->nand_profiles[k],facts))profile_matches_count++;
        if(!profile_matches_count)continue;
        if(profile_matches_count!=1){if(result)*result=BDI_AMBIGUOUS;return NULL;}
        bdi_result artifact_result;int provider=bdi_open_verified_at(rootfd,r->provider_path,
            r->provider_sha256,r->provider_bytes,1,&artifact_result);
        if(provider<0){if(result)*result=artifact_result;return NULL;}
        int payload=bdi_open_verified_at(rootfd,r->payload_path,r->payload_sha256,
            r->payload_bytes,0,&artifact_result);
        if(payload<0){close(provider);if(result)*result=artifact_result;return NULL;}
        close(payload);
        int inspected=inspect_provider(provider,r);close(provider);
        if(inspected<0){if(result)*result=BDI_EXEC_FAILED;return NULL;}
        probes[i]=inspected;
        if(inspected==1){snprintf(passing_detail,sizeof(passing_detail),"%s",g_inspect_detail);
            snprintf(passing_revision,sizeof(passing_revision),"%s",g_inspect_revision);}
    }
    const bdi_provider_record *selected=bdi_select_inspected_provider(records,count,source,product,facts,probes,
                                         matched_profile,result);
    if(selected){snprintf(g_inspect_detail,sizeof(g_inspect_detail),"%s",passing_detail);
        snprintf(g_inspect_revision,sizeof(g_inspect_revision),"%s",passing_revision);}
    return selected;
}
#endif

#ifndef BDI_TEST
static const char *blocked_reason(bdi_result r)
{
    switch(r){case BDI_FACTS_MISSING:return "无法读取完整设备信息，安装已关闭。";
    case BDI_FACTS_COMPAT_INVALID:return "设备树路由信息格式异常，安装已关闭。";
    case BDI_FACTS_KERNEL_ID_INVALID:return "内核未提供唯一有效的 NAND 型号与几何信息，安装已关闭。";
    case BDI_FACTS_MTD_SYSFS_INVALID:return "MTD 类型、偏移或几何信息无法核对，安装已关闭。";
    case BDI_FACTS_MTD_DEVICE_INVALID:return "MTD 设备号与内核设备映射不一致，安装已关闭。";
    case BDI_FACTS_PROC_MTD_INVALID:return "内核 MTD 分区表与 sysfs 信息不一致，安装已关闭。";
    case BDI_FACTS_CONFLICT:return "设备信息有冲突，无法唯一识别，安装已关闭。";
    case BDI_NO_MATCH:return "此设备不在当前原厂版本的适配范围内，安装已关闭。";
    case BDI_CANDIDATE_UNAVAILABLE:return "NAND 与路由配置命中候选，但来源、SBL、APPSBL 或分区布局完整只读合同未通过；安装已关闭。";
    case BDI_CANDIDATE_AMBIGUOUS:return "多个候选的完整原厂来源、SBL、APPSBL 与分区布局只读合同均通过；运行时缺少可区分板型的属性，安装已关闭。";
    case BDI_AMBIGUOUS:return "多个完整原厂来源、SBL、APPSBL 与分区布局只读合同同时匹配；当前设备没有提供额外运行时板型标识，安装已关闭。";
    case BDI_BAD_HASH:return "内置候选文件校验失败，安装已关闭。";
    case BDI_EXEC_FAILED:return "候选程序验证或启动失败，安装已关闭；请下载详细诊断。";
    case BDI_BAD_FILE:case BDI_BAD_PATH:return "内置候选文件不可安全读取，安装已关闭。";
    default:return "设备配置或内置文件验证失败，安装已关闭。";}
}

static int write_all(int fd,const char *p,size_t n)
{while(n){ssize_t z=write(fd,p,n);if(z<0&&errno==EINTR)continue;if(z<=0)return -1;p+=z;n-=(size_t)z;}return 0;}

static void reply(int fd,int code,const char *status,const char *type,const char *body,size_t n,int head)
{
    char h[256];int z=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",code,status,type,(unsigned long)n);
    if(z>0&&(size_t)z<sizeof(h)){(void)write_all(fd,h,(size_t)z);if(!head)(void)write_all(fd,body,n);}
}

static void reply_download(int fd,const char *body,size_t n,int head)
{
    char h[384];int z=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Disposition: attachment; filename=\"bootbridge-diagnostic.json\"\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",(unsigned long)n);
    if(z>0&&(size_t)z<sizeof(h)){(void)write_all(fd,h,(size_t)z);if(!head)(void)write_all(fd,body,n);}
}

static const char *blocked_code(bdi_result r)
{
    switch(r){
    case BDI_FACTS_MISSING:return "runtime_facts_missing";
    case BDI_FACTS_COMPAT_INVALID:return "runtime_compatible_invalid";
    case BDI_FACTS_KERNEL_ID_INVALID:return "runtime_nand_identity_invalid";
    case BDI_FACTS_MTD_SYSFS_INVALID:return "runtime_mtd_sysfs_invalid";
    case BDI_FACTS_MTD_DEVICE_INVALID:return "runtime_mtd_device_mismatch";
    case BDI_FACTS_PROC_MTD_INVALID:return "runtime_proc_mtd_mismatch";
    case BDI_FACTS_CONFLICT:return "runtime_facts_conflict";
    case BDI_NO_MATCH:return "no_provider_match";
    case BDI_CANDIDATE_UNAVAILABLE:return "candidate_oem_contract_unavailable";
    case BDI_CANDIDATE_AMBIGUOUS:return "candidate_contract_ambiguous";
    case BDI_AMBIGUOUS:return "provider_match_ambiguous";
    case BDI_BAD_BUNDLE:return "provider_bundle_invalid";
    case BDI_BAD_HASH:return "resource_hash_mismatch";
    case BDI_BAD_FILE:return "resource_file_invalid";
    case BDI_BAD_PATH:return "resource_path_invalid";
    case BDI_EXEC_FAILED:return "provider_exec_failed";
    default:return "dispatch_blocked";
    }
}

static int html_escape(char *out,size_t cap,const char *in)
{
    size_t n=0;
    for(const unsigned char *p=(const unsigned char*)in;*p;p++){
        const char *rep=NULL;char one[2]={(char)*p,0};
        switch(*p){case '&':rep="&amp;";break;case '<':rep="&lt;";break;case '>':rep="&gt;";break;case '\"':rep="&quot;";break;case '\'':rep="&#39;";break;}
        const char *add=rep?rep:one;size_t z=strlen(add);if(z>=cap-n)return -1;memcpy(out+n,add,z);n+=z;
    }
    if(n>=cap)return -1;
    out[n]=0;
    return (int)n;
}

static int json_escape(char *out,size_t cap,const char *in)
{
    static const char hex[]="0123456789abcdef";size_t n=0;
    for(const unsigned char *p=(const unsigned char*)in;*p;p++){
        char enc[7];const char *s=enc;size_t z=0;
        if(*p=='\"'||*p=='\\'){enc[0]='\\';enc[1]=(char)*p;z=2;}
        else if(*p<0x20){enc[0]='\\';enc[1]='u';enc[2]='0';enc[3]='0';enc[4]=hex[*p>>4];enc[5]=hex[*p&15];z=6;}
        else{enc[0]=(char)*p;z=1;}
        if(z>=cap-n)return -1;
        memcpy(out+n,s,z);n+=z;
    }
    if(n>=cap)return -1;
    out[n]=0;
    return (int)n;
}

static void serve_blocked(const char *code,const char *reason,const char *return_error)
{
    char body[BDI_HTTP_BODY_MAX],json[8192],safe_detail[4096],safe_code[256],safe_reason[768],safe_ret[768],html_reason[768],html_ret[768];int n;
    if(html_escape(html_reason,sizeof(html_reason),reason)<0||
       html_escape(html_ret,sizeof(html_ret),return_error)<0||
       json_escape(safe_code,sizeof(safe_code),code)<0||
       json_escape(safe_reason,sizeof(safe_reason),reason)<0||
       json_escape(safe_ret,sizeof(safe_ret),return_error)<0||
       json_escape(safe_detail,sizeof(safe_detail),g_inspect_detail)<0)return;
    n=snprintf(body,sizeof(body),"<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>暂不可用</title><style>body{font:18px sans-serif;max-width:42em;margin:3em auto;padding:0 1em;color:#222}main{border:1px solid #bbb;border-radius:8px;padding:1.5em}h1{font-size:1.4em}</style><main><h1>无法确认适用的设备配置</h1><p>%s</p><p><button disabled>返回原厂暂不可用</button>：%s</p><p><a href=\"/diagnostic.json\" download>下载诊断文件</a></p><p>此只读页面没有执行安装。</p></main></html>\n",html_reason,html_ret);
    if(n<0||(size_t)n>=sizeof(body))return;
    n=snprintf(json,sizeof(json),"{\"install_ready\":false,\"return_ready\":false,\"nand_profile\":{\"id\":\"\",\"model\":\"\"},\"source\":{\"product\":\"\",\"route\":\"\",\"source_sha256\":\"\"},\"dispatch\":{\"status\":\"blocked\",\"code\":\"%s\",\"reason\":\"%s\",\"return_reason\":\"%s\",\"execution_detail\":\"%s\",\"tool_revision\":\"R6-preflight-v1\"}}\n",safe_code,safe_reason,safe_ret,safe_detail);
    if(n<0||(size_t)n>=sizeof(json))return;
    int server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return;
    int one=1;(void)setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_port=htons(BDI_HTTP_PORT);
    if(inet_pton(AF_INET,"192.168.10.1",&addr.sin_addr)!=1||bind(server,(struct sockaddr*)&addr,sizeof(addr))||listen(server,8)){close(server);return;}
    signal(SIGPIPE,SIG_IGN);
    for(;;){int c=accept(server,NULL,NULL);if(c<0){if(errno==EINTR)continue;continue;}
        char req[1024];ssize_t nr;do{nr=read(c,req,sizeof(req)-1);}while(nr<0&&errno==EINTR);
        if(nr<=0){close(c);continue;}req[nr]=0;
        char method[8]={0},path[128]={0},version[16]={0};
        if(sscanf(req,"%7s %127s %15s",method,path,version)!=3){reply(c,400,"Bad Request","text/plain; charset=utf-8","Bad Request\n",12,0);close(c);continue;}
        int head=!strcmp(method,"HEAD");int get=!strcmp(method,"GET");
        if(!get&&!head)reply(c,405,"Method Not Allowed","text/plain; charset=utf-8","只读页面\n",12,0);
        else if(!strcmp(path,"/")||!strcmp(path,"/index.html"))reply(c,200,"OK","text/html; charset=utf-8",body,(size_t)strlen(body),head);
        else if(!strcmp(path,"/api/check")||!strcmp(path,"/api/status")||!strcmp(path,"/report.json"))reply(c,200,"OK","application/json; charset=utf-8",json,(size_t)strlen(json),head);
        else if(!strcmp(path,"/diagnostic.json"))reply_download(c,json,(size_t)strlen(json),head);
        else reply(c,404,"Not Found","text/plain; charset=utf-8","Not Found\n",10,head);
        close(c);
    }
}

static int exact_pid1_arguments(int argc,char **argv)
{
    return argc==7&&same(argv[1],"-b")&&same(argv[2],"192.168.10.1")&&
        same(argv[3],"-p")&&same(argv[4],"80")&&same(argv[5],"-w")&&same(argv[6],"/www");
}

int main(int argc,char **argv)
{
    const char *verified=getenv("BRIDGE_INIT_VERIFIED"),*mode=getenv("BRIDGE_RUNTIME_MODE");
    bdi_result result=BDI_BAD_BUNDLE;const bdi_provider_record *record=NULL;int rootfd=-1,provider=-1,payload=-1;
    char profile_env[160],source_env[128],route_env[160],product_env[96],
         candidate_env[160],class_env[64],execution_env[2112],return_error[128]="原厂返回服务未能启动。";
    const char *why="设备配置或内置文件验证失败，安装已关闭。",*code="dispatch_blocked";
    rootfd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    bdi_facts facts;
    if(rootfd<0){result=BDI_FACTS_MISSING;why=blocked_reason(result);code=blocked_code(result);goto blocked;}
    if(!exact_pid1_arguments(argc,argv)||!same(verified,"H3C-BOOTBRIDGE-RUNTIME-v1")||
       !same(mode,"RUNTIME_GATED")){result=BDI_BAD_BUNDLE;why="启动环境未通过只读校验，安装已关闭。";code="runtime_guard";goto blocked;}
    if(BI_PROVIDER_BUNDLE_ABI_VERSION!=BDI_BUNDLE_VERSION||
       BI_PROVIDER_COUNT!=sizeof(bootbridge_provider_records)/sizeof(bootbridge_provider_records[0])||
       !bundle_valid(bootbridge_provider_records,BI_PROVIDER_COUNT,
                     BI_PROVIDER_SOURCE_SHA256,BI_PROVIDER_PRODUCT)){
        result=BDI_BAD_BUNDLE;why="桥接包内的设备配置表不完整，安装已关闭。";code="provider_bundle_invalid";goto blocked;}
    result=read_live_facts(&facts);
    if(result!=BDI_OK){why=blocked_reason(result);code=blocked_code(result);goto blocked;}
    const bdi_nand_profile *matched_profile=NULL;
    record=preflight_candidate_contracts(rootfd,bootbridge_provider_records,
        BI_PROVIDER_COUNT,BI_PROVIDER_SOURCE_SHA256,BI_PROVIDER_PRODUCT,&facts,
        &matched_profile,&result);
    if(!record){why=result==BDI_CANDIDATE_UNAVAILABLE&&g_candidate_rejection[0]?
        g_candidate_rejection:blocked_reason(result);code=blocked_code(result);goto blocked;}
    if(snprintf(profile_env,sizeof(profile_env),"BRIDGE_NAND_PROFILE_ID=%s",matched_profile->profile_id)<0||
       snprintf(source_env,sizeof(source_env),"BRIDGE_SOURCE_SHA256=%s",record->source_sha256)<0||
       snprintf(route_env,sizeof(route_env),"BRIDGE_SOURCE_ROUTE=%s",record->route)<0||
       snprintf(product_env,sizeof(product_env),"BRIDGE_SOURCE_PRODUCT=%s",record->product)<0||
       snprintf(candidate_env,sizeof(candidate_env),"BRIDGE_CANDIDATE_ID=%s",record->candidate_id)<0||
       snprintf(class_env,sizeof(class_env),"BRIDGE_HARDWARE_CLASS=%s",record->hardware_class)<0||
       strlen(profile_env)>=sizeof(profile_env)||strlen(source_env)>=sizeof(source_env)||
       strlen(route_env)>=sizeof(route_env)||strlen(product_env)>=sizeof(product_env)||
       strlen(candidate_env)>=sizeof(candidate_env)||strlen(class_env)>=sizeof(class_env)){
        result=BDI_BAD_BUNDLE;why=blocked_reason(result);code=blocked_code(result);goto blocked;
    }
    provider=bdi_open_verified_at(rootfd,record->provider_path,record->provider_sha256,
        record->provider_bytes,1,&result);
    if(provider<0){why=blocked_reason(result);code=blocked_code(result);goto blocked;}
    payload=bdi_open_verified_at(rootfd,record->payload_path,record->payload_sha256,
        record->payload_bytes,0,&result);
    if(payload<0){why=blocked_reason(result);code=blocked_code(result);goto blocked;}
    close(payload);payload=-1;
    if(fcntl(provider,F_SETFD,0)<0){int saved_errno=errno;
        memset(&g_inspect_diag,0,sizeof(g_inspect_diag));g_inspect_diag.exit_code=-1;
        g_inspect_diag.system_errno=saved_errno;
        snprintf(g_inspect_diag.last_stage,sizeof(g_inspect_diag.last_stage),"service_fd");
        (void)inspect_finish(record,NULL,"setup_error",-1);
        result=BDI_EXEC_FAILED;why=blocked_reason(result);code=blocked_code(result);goto blocked;}
    snprintf(execution_env,sizeof(execution_env),"BRIDGE_DISPATCH_EXECUTION_DETAIL=%s",g_inspect_detail);
    char *provider_argv[]={ (char *)record->provider_path,"-b","192.168.10.1","-p","80","-w","/www",NULL};
    char *provider_env[]={"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
        "BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1",
        "BRIDGE_RUNTIME_MODE=RUNTIME_GATED",profile_env,source_env,route_env,product_env,
        candidate_env,class_env,execution_env,NULL};
    fexecve(provider,provider_argv,provider_env);
    {int saved_errno=errno;memset(&g_inspect_diag,0,sizeof(g_inspect_diag));
        g_inspect_diag.exit_code=-1;g_inspect_diag.system_errno=saved_errno;}
    snprintf(g_inspect_diag.last_stage,sizeof(g_inspect_diag.last_stage),"service_exec");
    (void)inspect_finish(record,NULL,"exec_error",-1);
    result=BDI_EXEC_FAILED;why=blocked_reason(result);code=blocked_code(result);
blocked:
    if(payload>=0)close(payload);
    if(provider>=0)close(provider);
    if(code[0]=='d'&&same(code,"dispatch_blocked"))code=blocked_code(result);
#ifndef BDI_TEST
    if(rootfd>=0){errno=0;if(bi_oem_return_exec_verified_detail(rootfd,code,why,record?record->source_revision_sha256:
        (result==BDI_CANDIDATE_UNAVAILABLE?g_rejected_candidate_revision:
         result==BDI_EXEC_FAILED&&g_inspect_detail[0]?g_inspect_revision:""),g_inspect_detail)<0)
        snprintf(return_error,sizeof(return_error),"返回服务验证或启动失败（errno %d）。",errno);}
    else snprintf(return_error,sizeof(return_error),"无法打开只读根目录以验证返回服务。");
#endif
    if(rootfd>=0)close(rootfd);
    serve_blocked(code,why,return_error);return 1;
}

#endif
