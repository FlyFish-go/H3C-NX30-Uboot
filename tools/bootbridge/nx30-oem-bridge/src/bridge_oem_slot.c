#include "bridge_oem_slot.h"
#include "bridge_sha256.h"
#include <stdlib.h>
#include <string.h>
#define PEB 131072u
#define LEB 126976u
#define COUNT 320u
#define TABLE (128u*172u)
#define LAYOUT 0x7fffefffu
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put_be32(uint8_t*p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=0xffffffffu;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0);}return c;}
static int fill(const uint8_t*p,size_t n,uint8_t b){while(n--)if(*p++!=b)return 0;return 1;}
static void sha(const void*p,size_t n,uint8_t d[32]){bi_sha256_ctx c;bi_sha256_init(&c);bi_sha256_update(&c,p,n);bi_sha256_final(&c,d);}
static int geo_equal(const bos_geometry*a,const bos_geometry*b){return a->physical_offset==b->physical_offset&&a->size_bytes==b->size_bytes&&a->erase_bytes==b->erase_bytes&&a->page_bytes==b->page_bytes&&a->oob_bytes==b->oob_bytes&&a->ecc_bits==b->ecc_bits;}
typedef struct {int peb;uint32_t size,num,type;uint8_t digest[32];} entry;
/* Accept only two bounded copy contexts: the exact observed layout-table
 * record, and a UBI dynamic-volume copy with a valid data CRC. Duplicate LEB
 * resolution remains unsupported; each (volume,lnum) must be unique. */
static int observed_copy_valid(const uint8_t*v,const uint8_t*peb,uint32_t id,
                               uint32_t ln,uint32_t size,uint32_t used){
    const uint8_t*data=peb+4096;
    if(v[6]!=1||be32(v+28)!=0||used!=0)return 0;
    if(id==LAYOUT){
        if(v[5]!=1||v[7]!=5||ln>1||size!=22528||
           crc(data,size)!=be32(v+32)||!fill(data+TABLE,size-TABLE,255)||
           !fill(data+size,PEB-4096-size,255))return 0;
        return 1;
    }
    if(id==4){
        /* UBI v4.4 uses data_size/data_crc for any wear-level copy of a
         * dynamic LEB; the LEB number is not fixed. Padding after data_size
         * is not constrained by the VID format (and may contain old bytes). */
        if(v[5]!=1||v[7]!=0||!size||size>LEB||
           crc(data,size)!=be32(v+32))return 0;
        return 1;
    }
    return 0;
}
bos_code bos_verify(bos_read_at rd,void*ctx,const bos_geometry*g,const bos_manifest*m,const uint8_t candidate[32],bos_result*r){
    static const char*names[5]={"kernel","wifi_fw","bt_fw","ubi_rootfs","rootfs_data"};
    entry (*map)[COUNT]=NULL;uint8_t *buf=NULL,*table=NULL,d[32];uint32_t reserve[5]={0},types[5]={0};
    unsigned layouts=0,layoutmask=0;uint32_t seq=0;int have_seq=0;bos_code rc=BOS_ARGUMENT;bi_sha256_ctx raw;
    (void)bi_sha256_hex; /* shared SHA header also exposes optional formatter */
    if(!r)return BOS_ARGUMENT;
    memset(r,0,sizeof(*r));r->failure_peb=UINT32_MAX;
    if(!rd||!g||!m||!candidate)goto done;
    rc=BOS_UNTRUSTED_BINDING;
    if(fill(candidate,32,0)||memcmp(candidate,m->candidate_sha256,32)||fill(m->volume_table_sha256,32,0))goto done;
    for(unsigned i=0;i<4;i++)if(!m->immutable[i].size_bytes||fill(m->immutable[i].sha256,32,0))goto done;
    rc=BOS_UNSUPPORTED;
    if(!geo_equal(g,&m->geometry)||g->size_bytes!=(uint64_t)PEB*COUNT||g->erase_bytes!=PEB||g->page_bytes!=2048||
       (g->physical_offset!=0x900000u&&g->physical_offset!=0x3100000u)||
       !((g->oob_bytes==64&&g->ecc_bits==4)||(g->oob_bytes==80&&g->ecc_bits==8)||
         (g->oob_bytes==128&&(g->ecc_bits==4||g->ecc_bits==8))))goto done;
    map=calloc(5,sizeof(*map));buf=malloc(PEB);table=malloc(TABLE);rc=BOS_IO;
    if(!map||!buf||!table)goto done;
    for(unsigned v=0;v<5;v++)for(unsigned l=0;l<COUNT;l++)map[v][l].peb=-1;
    bi_sha256_init(&raw);
    for(unsigned p=0;p<COUNT;p++){
        r->failure_peb=p;rc=BOS_IO;if(rd(ctx,(uint64_t)p*PEB,buf,PEB))goto done;
        bi_sha256_update(&raw,buf,PEB);r->scanned_pebs++;
        if(fill(buf,PEB,255))continue;
        rc=BOS_EC;if(memcmp(buf,"UBI#",4)||buf[4]!=1||!fill(buf+5,3,0)||!fill(buf+28,32,0)||crc(buf,60)!=be32(buf+60)||be32(buf+16)!=2048||be32(buf+20)!=4096)goto done;
        if(have_seq&&seq!=be32(buf+24))goto done;
        seq=be32(buf+24);have_seq=1;
        uint8_t*v=buf+2048;
        if(fill(v,64,255)){if(!fill(v,PEB-2048,255))goto done;continue;}
        rc=BOS_VID;if(memcmp(v,"UBI!",4)||v[4]!=1||(v[5]!=1&&v[5]!=2)||v[6]>1||crc(v,60)!=be32(v+60)||!fill(v+16,4,0)||be32(v+28)||!fill(v+36,4,0)||!fill(v+48,12,0))goto done;
        uint32_t id=be32(v+8),ln=be32(v+12),sz=be32(v+20),num=be32(v+24);
        if(id==LAYOUT){
            rc=BOS_TABLE;if(v[5]!=1||v[7]!=5||ln>1||(layoutmask&(1u<<ln))||
                (v[6] ? !observed_copy_valid(v,buf,id,ln,sz,num) : (sz||num||be32(v+32))))goto done;
            if(layouts&&memcmp(table,buf+4096,TABLE))goto done;
            memcpy(table,buf+4096,TABLE);layouts++;layoutmask|=1u<<ln;continue;
        }
        rc=BOS_VOLUME;if(id>=5||ln>=COUNT||map[id][ln].peb>=0)goto done;
        if(v[6]){if(!observed_copy_valid(v,buf,id,ln,sz,num))goto done;}
        else if(v[7])goto done;
        if(v[5]==2){rc=BOS_STATIC_CRC;if(!sz||sz>LEB||!num||num>COUNT||ln>=num||crc(buf+4096,sz)!=be32(v+32))goto done;}
        else if(!v[6]){rc=BOS_UNSUPPORTED;if(sz||num||be32(v+32))goto done;sz=LEB;}
        else sz=LEB;
        entry*e=&map[id][ln];e->peb=(int)p;e->size=sz;e->num=num;e->type=v[5];sha(buf,PEB,e->digest);
    }
    bi_sha256_final(&raw,r->snapshot_sha256);r->failure_peb=UINT32_MAX;rc=BOS_TABLE;
    if(layouts!=2||layoutmask!=3)goto done;
    sha(table,TABLE,d);if(memcmp(d,m->volume_table_sha256,32))goto done;
    for(unsigned j=0;j<128;j++){
        const uint8_t*t=table+j*172;uint32_t n=be32(t);if(crc(t,168)!=be32(t+168))goto done;
        if(j>=5){if(!fill(t,168,0))goto done;continue;}
        size_t z=strlen(names[j]);
        if(!n||n>COUNT||be32(t+4)!=1||be32(t+8)||t[13]||t[14]||t[15]!=z||memcmp(t+16,names[j],z)||!fill(t+16+z,128-z,0)||t[144]>1||!fill(t+145,23,0)||(t[12]!=1&&t[12]!=2))goto done;
        reserve[j]=n;types[j]=t[12];
    }
    if(types[4]!=1)goto done;
    {uint32_t total=2;for(unsigned i=0;i<5;i++)total+=reserve[i];if(total>COUNT)goto done;}
    for(unsigned id=0;id<5;id++){
        unsigned count=0;uint64_t bytes=0;bi_sha256_ctx stream;bi_sha256_init(&stream);
        for(unsigned ln=0;ln<COUNT;ln++)if(map[id][ln].peb>=0)count++;
        rc=BOS_VOLUME;if(id<4&&(!count||types[id]!=m->immutable[id].type))goto done;
        for(unsigned ln=0;ln<COUNT;ln++){
            entry*e=&map[id][ln];if(e->peb<0){if(id<4&&ln<count)goto done;continue;}
            rc=BOS_VOLUME;if(ln>=reserve[id]||e->type!=types[id]||(types[id]==2&&(e->num!=count||(ln+1<count&&e->size!=LEB))))goto done;
            if(id==4){r->rootfs_data_lebs++;continue;}
            r->failure_peb=(uint32_t)e->peb;rc=BOS_IO;if(rd(ctx,(uint64_t)e->peb*PEB,buf,PEB))goto done;
            sha(buf,PEB,d);rc=BOS_CHANGED;if(memcmp(d,e->digest,32))goto done;
            bi_sha256_update(&stream,buf+4096,e->size);bytes+=e->size;
        }
        if(id<4){bi_sha256_final(&stream,d);rc=BOS_DIGEST;if(bytes!=m->immutable[id].size_bytes||memcmp(d,m->immutable[id].sha256,32))goto done;}
    }
    rc=BOS_OK;r->logical_content_verified=1;r->failure_peb=UINT32_MAX;
done:
    r->code=rc;free(map);free(buf);free(table);return rc;
}

/* V2 measurement shares v1 strict format; no manifest-dependent reads. */
typedef char bos_bounded_workspace[(5u*COUNT*sizeof(entry)+PEB+TABLE<=256u*1024u)?1:-1];
bos_code bos_measure(bos_read_at rd,void*ctx,const bos_geometry*g,bos_measurement*r){
    static const char*names[5]={"kernel","wifi_fw","bt_fw","ubi_rootfs","rootfs_data"};
    entry (*map)[COUNT]=NULL;uint8_t *buf=NULL,*table=NULL,d[32];uint32_t reserve[5]={0},types[5]={0};
    unsigned layouts=0,layoutmask=0;uint32_t seq=0;int have_seq=0;bos_code rc=BOS_ARGUMENT;bi_sha256_ctx raw;
    (void)bi_sha256_hex;
    if(!r)return BOS_ARGUMENT;
    memset(r,0,sizeof(*r));
    if(!rd||!g)return BOS_ARGUMENT;
    if(g->size_bytes!=(uint64_t)PEB*COUNT||g->erase_bytes!=PEB||g->page_bytes!=2048||
       (g->physical_offset!=0x900000u&&g->physical_offset!=0x3100000u)||
       !((g->oob_bytes==64&&g->ecc_bits==4)||(g->oob_bytes==80&&g->ecc_bits==8)||
         (g->oob_bytes==128&&(g->ecc_bits==4||g->ecc_bits==8))))return BOS_UNSUPPORTED;
    r->geometry=*g;
    map=calloc(5,sizeof(*map));buf=malloc(PEB);table=malloc(TABLE);rc=BOS_IO;
    if(!map||!buf||!table)goto done;
    for(unsigned v=0;v<5;v++)for(unsigned l=0;l<COUNT;l++)map[v][l].peb=-1;
    bi_sha256_init(&raw);
    for(unsigned p=0;p<COUNT;p++){
        rc=BOS_IO;if(rd(ctx,(uint64_t)p*PEB,buf,PEB))goto done;
        bi_sha256_update(&raw,buf,PEB);r->scanned_pebs++;
        if(fill(buf,PEB,255))continue;
        rc=BOS_UNSUPPORTED;if(memcmp(buf,"UBI#",4)||buf[4]!=1||!fill(buf+5,3,0)||!fill(buf+28,32,0)||crc(buf,60)!=be32(buf+60)||be32(buf+16)!=2048||be32(buf+20)!=4096)goto done;
        if(have_seq&&seq!=be32(buf+24))goto done;
        seq=be32(buf+24);have_seq=1;
        uint8_t*v=buf+2048;
        if(fill(v,64,255)){if(!fill(v,PEB-2048,255))goto done;continue;}
        rc=BOS_UNSUPPORTED;if(memcmp(v,"UBI!",4)||v[4]!=1||(v[5]!=1&&v[5]!=2)||v[6]>1||crc(v,60)!=be32(v+60)||!fill(v+16,4,0)||be32(v+28)||!fill(v+36,4,0)||!fill(v+48,12,0))goto done;
        uint32_t id=be32(v+8),ln=be32(v+12),sz=be32(v+20),num=be32(v+24);
        if(id==LAYOUT){
            rc=BOS_UNSUPPORTED;if(v[5]!=1||v[7]!=5||ln>1||(layoutmask&(1u<<ln))||
                (v[6] ? !observed_copy_valid(v,buf,id,ln,sz,num) : (sz||num||be32(v+32))))goto done;
            if(layouts&&memcmp(table,buf+4096,TABLE))goto done;
            memcpy(table,buf+4096,TABLE);layouts++;layoutmask|=1u<<ln;continue;
        }
        rc=BOS_UNSUPPORTED;if(id>=5||ln>=COUNT||map[id][ln].peb>=0)goto done;
        if(v[6]){if(!observed_copy_valid(v,buf,id,ln,sz,num))goto done;}
        else if(v[7])goto done;
        if(v[5]==2){rc=BOS_UNSUPPORTED;if(!sz||sz>LEB||!num||num>COUNT||ln>=num||crc(buf+4096,sz)!=be32(v+32))goto done;}
        else if(!v[6]){rc=BOS_UNSUPPORTED;if(sz||num||be32(v+32))goto done;sz=LEB;}
        else sz=LEB;
        entry*e=&map[id][ln];e->peb=(int)p;e->size=sz;e->num=num;e->type=v[5];sha(buf,PEB,e->digest);
    }
    bi_sha256_final(&raw,r->snapshot_sha256);rc=BOS_UNSUPPORTED;
    if(layouts!=2||layoutmask!=3)goto done;
    sha(table,TABLE,r->volume_table_sha256);
    for(unsigned j=0;j<128;j++){
        const uint8_t*t=table+j*172;uint32_t n=be32(t);if(crc(t,168)!=be32(t+168))goto done;
        if(j>=5){if(!fill(t,168,0))goto done;continue;}
        size_t z=strlen(names[j]);
        if(!n||n>COUNT||be32(t+4)!=1||be32(t+8)||t[13]||t[14]||t[15]!=z||memcmp(t+16,names[j],z)||!fill(t+16+z,128-z,0)||t[144]>1||!fill(t+145,23,0)||(t[12]!=1&&t[12]!=2))goto done;
        reserve[j]=n;types[j]=t[12];
    }
    if(types[4]!=1)goto done;
    {uint32_t total=2;for(unsigned i=0;i<5;i++)total+=reserve[i];if(total>COUNT)goto done;}
    /* Exact post-attach autoresize transform: only the rootfs_data table row
     * may change from its source defaults (reserved=1, autoresize=1) to the
     * attached form (reserved>=1, autoresize=0). Other rows and the full table
     * remain bound by their CRCs and the normalized whole-table SHA. */
    {
        uint8_t *row=table+4u*172u;
        uint32_t flags=row[144];
        if(types[4]==1u&&reserve[4]>=1u&&flags==0u){
            put_be32(row,1u);
            row[144]=1u;
            put_be32(row+168,crc(row,168));
            sha(table,TABLE,r->normalized_volume_table_sha256);
            r->normalized_volume_table_valid=1u;
        }
    }
    for(unsigned id=0;id<5;id++){
        unsigned count=0;uint64_t bytes=0;bi_sha256_ctx stream;bi_sha256_init(&stream);
        for(unsigned ln=0;ln<COUNT;ln++)if(map[id][ln].peb>=0)count++;
        rc=BOS_UNSUPPORTED;if(id<4&&!count)goto done;
        for(unsigned ln=0;ln<COUNT;ln++){
            entry*e=&map[id][ln];if(e->peb<0){if(id<4&&ln<count)goto done;continue;}
            rc=BOS_UNSUPPORTED;if(ln>=reserve[id]||e->type!=types[id]||(types[id]==2&&(e->num!=count||(ln+1<count&&e->size!=LEB))))goto done;
            if(id==4){r->rootfs_data_lebs++;continue;}
            rc=BOS_IO;if(rd(ctx,(uint64_t)e->peb*PEB,buf,PEB))goto done;
            sha(buf,PEB,d);rc=BOS_CHANGED;if(memcmp(d,e->digest,32))goto done;
            bi_sha256_update(&stream,buf+4096,e->size);bytes+=e->size;
        }
        if(id<4){bi_sha256_final(&stream,r->immutable[id].sha256);r->immutable[id].size_bytes=bytes;r->immutable[id].type=types[id];}
    }
    rc=BOS_OK;
done:
    if(rc!=BOS_OK)memset(r,0,sizeof(*r));
    free(map);free(buf);free(table);return rc;
}

static int bos_volume_equal(const bos_volume*a,const bos_volume*b){return a->size_bytes==b->size_bytes&&a->type==b->type&&!memcmp(a->sha256,b->sha256,32);}
bos_code bos_pool_select(const bos_measurement*m,const bos_pool_rule*pool,size_t n,const uint8_t candidate[32],const uint8_t revision[32],bos_pool_match*out){
    bos_pool_match found;int unknown=0;memset(&found,0,sizeof(found));
    if(!out)return BOS_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!m||!pool||!n||n>BOS_POOL_MAX||!candidate||!revision)return BOS_ARGUMENT;
    if(fill(candidate,32,0)||fill(revision,32,0))return BOS_UNTRUSTED_BINDING;
    for(size_t i=0;i<n;i++){
        const bos_pool_rule*r=&pool[i];const bos_manifest*c=&r->content;int same=1;
        if(!r->content_group||!r->product||!r->alias||
           (r->table_policy!=BOS_TABLE_RAW_EXACT&&r->table_policy!=BOS_TABLE_AUTORESIZE_UNAVAILABLE&&
            r->table_policy!=BOS_TABLE_POSTATTACH_AUTORESIZE)||
           fill(c->candidate_sha256,32,0)||fill(c->volume_table_sha256,32,0))return BOS_UNTRUSTED_BINDING;
        for(unsigned v=0;v<4;v++)if(!c->immutable[v].size_bytes||c->immutable[v].size_bytes>(uint64_t)PEB*COUNT||
           (c->immutable[v].type!=1&&c->immutable[v].type!=2)||fill(c->immutable[v].sha256,32,0))return BOS_UNTRUSTED_BINDING;
        for(size_t j=0;j<i;j++)if(r->alias==pool[j].alias&&geo_equal(&c->geometry,&pool[j].content.geometry))return BOS_UNTRUSTED_BINDING;
        if(memcmp(c->candidate_sha256,candidate,32)||!geo_equal(&m->geometry,&c->geometry))continue;
        for(unsigned v=0;v<4;v++)if(!bos_volume_equal(&m->immutable[v],&c->immutable[v]))same=0;
        if(!same)continue;
        if(r->table_policy==BOS_TABLE_POSTATTACH_AUTORESIZE){
            if(!m->normalized_volume_table_valid||
               memcmp(m->normalized_volume_table_sha256,c->volume_table_sha256,32))continue;
        }else if(memcmp(m->volume_table_sha256,c->volume_table_sha256,32)){
            if(r->table_policy==BOS_TABLE_AUTORESIZE_UNAVAILABLE)unknown=1;
            continue;
        }
        if(found.aliases&&(found.content_group!=r->content_group||found.exit_policy!=r->exit_policy||found.product!=r->product))return BOS_AMBIGUOUS;
        found.content_group=r->content_group;found.exit_policy=r->exit_policy;found.product=r->product;
        if(++found.aliases>BOS_ALIAS_MAX)return BOS_LIMIT;
    }
    if(unknown)return BOS_POLICY_UNKNOWN;
    if(!found.aliases)return BOS_NO_MATCH;
    memcpy(found.candidate_sha256,candidate,32);memcpy(found.pool_revision_sha256,revision,32);
    found.logical_content_verified=1;*out=found;return BOS_OK;
}
