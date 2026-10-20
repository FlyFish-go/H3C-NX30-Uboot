/* Entirely invented fixtures. No packages, backups, devices or network input. */
#include "bridge_oem_slot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PEB 131072u
#define LEB 126976u
#define TOTAL (320u*PEB)
static unsigned checks;
#define CHECK(x) do { checks++; if(!(x)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static void put(uint8_t*p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static uint32_t crc(const uint8_t*p,size_t n){uint32_t c=0xffffffffu;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0);}return c;}

static void record(uint8_t*r,const char*n,unsigned type,unsigned reserve,unsigned flag){size_t z=strlen(n);memset(r,0,172);if(z){put(r,reserve);put(r+4,1);r[12]=(uint8_t)type;r[15]=(uint8_t)z;memcpy(r+16,n,z);r[144]=(uint8_t)flag;}put(r+168,crc(r,168));}
static void block(uint8_t*b,unsigned vol,unsigned ln,unsigned type,const void*data,size_t size){uint8_t*v=b+2048;memset(b,255,PEB);memset(b,0,64);memcpy(b,"UBI#\1",5);put(b+16,2048);put(b+20,4096);put(b+24,7);put(b+60,crc(b,60));memset(v,0,64);memcpy(v,"UBI!\1",5);v[5]=(uint8_t)type;v[7]=(vol==0x7fffefff)?5:0;put(v+8,vol);put(v+12,ln);if(type==2){put(v+20,(uint32_t)size);put(v+24,1);put(v+32,crc(data,size));}put(v+60,crc(v,60));memcpy(b+4096,data,size);}
typedef struct {uint8_t*image;uint64_t bytes;unsigned calls;int io,mutate;} source;
static int rd(void*ctx,uint64_t off,void*out,size_t n){source*s=ctx;CHECK(n==PEB&&off%PEB==0&&off<=TOTAL&&n<=TOTAL-off);s->calls++;s->bytes+=n;if(s->io&&off==3u*PEB)return -1;memcpy(out,s->image+(size_t)off,n);if(s->mutate&&s->calls>320&&off==2u*PEB)((uint8_t*)out)[4096]^=1;return 0;}
static void tables(uint8_t*image,const uint8_t*t){block(image,0x7fffefff,0,1,t,(128u*172u));block(image+PEB,0x7fffefff,1,1,t,(128u*172u));}


static void make_rule(bos_pool_rule*r,const bos_measurement*m){memset(r,0,sizeof(*r));r->content.geometry=m->geometry;memset(r->content.candidate_sha256,3,32);memcpy(r->content.volume_table_sha256,m->volume_table_sha256,32);memcpy(r->content.immutable,m->immutable,sizeof(r->content.immutable));r->content_group=10;r->exit_policy=BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0;r->product=BOS_PRODUCT_NX30;r->alias=1;r->table_policy=BOS_TABLE_RAW_EXACT;}
static bos_code measure(source*s,const bos_geometry*g,bos_measurement*m){s->calls=0;s->bytes=0;return bos_measure(rd,s,g,m);}
int main(void){
 uint8_t*image=malloc(TOTAL),*payload=malloc(LEB),*t=malloc(128u*172u),*post=malloc(128u*172u),candidate[32],revision[32];
 bos_pool_rule*pool=calloc(BOS_POOL_MAX,sizeof(*pool));bos_measurement before,now;bos_pool_match match;
 bos_geometry g={0x900000,TOTAL,PEB,2048,64,4};source s={0};
 CHECK(image&&payload&&t&&post&&pool);s.image=image;memset(image,255,TOTAL);memset(candidate,3,32);memset(revision,4,32);
 for(unsigned i=0;i<128;i++)record(t+i*172,"",0,0,0);
 record(t,"kernel",2,8,0);record(t+172,"wifi_fw",2,8,0);record(t+344,"bt_fw",2,8,0);record(t+516,"ubi_rootfs",1,8,0);record(t+688,"rootfs_data",1,1,1);tables(image,t);
 for(unsigned i=0;i<4;i++){memset(payload,(int)i+31,LEB);block(image+(i+2)*PEB,i,0,i==3?1:2,payload,i==3?LEB:99+i);}
 CHECK(measure(&s,&g,&before)==BOS_OK);CHECK(s.calls==324&&s.bytes<=80u*1024u*1024u);make_rule(pool,&before);
 CHECK(bos_pool_select(&before,pool,1,candidate,revision,&match)==BOS_OK&&match.logical_content_verified);
 CHECK(!memcmp(match.candidate_sha256,candidate,32)&&!memcmp(match.pool_revision_sha256,revision,32));
 /* Runtime UBI autoresize may update only the rootfs_data table row. Revert
  * those two exact fields and CRC, then match the compiled source-table hash. */
 memcpy(post,t,128u*172u);put(post+4*172,56);post[4*172+144]=0;put(post+4*172+168,crc(post+4*172,168));tables(image,post);
 CHECK(measure(&s,&g,&now)==BOS_OK&&now.normalized_volume_table_valid==1);
 CHECK(memcmp(now.volume_table_sha256,before.volume_table_sha256,32));
 CHECK(!memcmp(now.normalized_volume_table_sha256,before.volume_table_sha256,32));
 pool[0].table_policy=BOS_TABLE_POSTATTACH_AUTORESIZE;
 CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_OK&&match.logical_content_verified);
 pool[0].table_policy=BOS_TABLE_RAW_EXACT;
 CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_NO_MATCH);
 /* Any other volume-table row mutation remains outside the normalization. */
 put(post+3*172,9);put(post+3*172+168,crc(post+3*172,168));tables(image,post);
 CHECK(measure(&s,&g,&now)==BOS_OK&&now.normalized_volume_table_valid==1);
 pool[0].table_policy=BOS_TABLE_POSTATTACH_AUTORESIZE;
 CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_NO_MATCH);
 pool[0].table_policy=BOS_TABLE_RAW_EXACT;
 memcpy(post,t,128u*172u);put(post+4*172,56);post[4*172+144]=1;put(post+4*172+168,crc(post+4*172,168));tables(image,post);
 CHECK(measure(&s,&g,&now)==BOS_OK&&now.normalized_volume_table_valid==0);
 pool[0].table_policy=BOS_TABLE_POSTATTACH_AUTORESIZE;
 CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_NO_MATCH);
 pool[0].table_policy=BOS_TABLE_RAW_EXACT;
 memcpy(post,t,128u*172u);put(post+4*172,0);put(post+4*172+168,crc(post+4*172,168));tables(image,post);
 CHECK(measure(&s,&g,&now)!=BOS_OK);
 memcpy(post,t,128u*172u);tables(image,post);
 CHECK(measure(&s,&g,&now)==BOS_OK&&bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_OK);
 pool[0].exit_policy=BOS_EXIT_UNAVAILABLE;CHECK(bos_pool_select(&before,pool,1,candidate,revision,&match)==BOS_OK&&match.exit_policy==0&&match.logical_content_verified);pool[0].exit_policy=BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0;
 /* Physical position is not logical stream order. */
 memcpy(image+319*PEB,image+2*PEB,PEB);memcpy(image+2*PEB,image+5*PEB,PEB);memcpy(image+5*PEB,image+319*PEB,PEB);memset(image+319*PEB,255,PEB);
 CHECK(measure(&s,&g,&now)==BOS_OK);CHECK(memcmp(now.snapshot_sha256,before.snapshot_sha256,32));CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_OK);
 memcpy(image+319*PEB,image+2*PEB,PEB);memcpy(image+2*PEB,image+5*PEB,PEB);memcpy(image+5*PEB,image+319*PEB,PEB);memset(image+319*PEB,255,PEB);
 for(unsigned i=1;i<64;i++){pool[i]=pool[0];pool[i].alias=i+1;pool[i].content.immutable[0].sha256[0]^=(uint8_t)i;}
 {unsigned calls=s.calls;CHECK(bos_pool_select(&before,pool,6,candidate,revision,&match)==BOS_OK);CHECK(bos_pool_select(&before,pool,64,candidate,revision,&match)==BOS_OK);CHECK(s.calls==calls);}
 /* Exact content in the LAST rule must select it, never first geometry match. */
 pool[63]=pool[0];pool[63].alias=64;pool[63].content_group=64;pool[0].content.immutable[0].sha256[0]^=99;
 CHECK(bos_pool_select(&before,pool,64,candidate,revision,&match)==BOS_OK&&match.content_group==64);make_rule(pool,&before);
 pool[1]=pool[0];pool[1].alias=2;CHECK(bos_pool_select(&before,pool,2,candidate,revision,&match)==BOS_OK&&match.aliases==2);
 pool[1].content_group++;CHECK(bos_pool_select(&before,pool,2,candidate,revision,&match)==BOS_AMBIGUOUS&&!match.logical_content_verified);pool[1]=pool[0];pool[1].alias=2;pool[1].exit_policy++;CHECK(bos_pool_select(&before,pool,2,candidate,revision,&match)==BOS_AMBIGUOUS);
 pool[1]=pool[0];pool[1].alias=2;pool[1].product++;CHECK(bos_pool_select(&before,pool,2,candidate,revision,&match)==BOS_AMBIGUOUS);
 pool[1]=pool[0];CHECK(bos_pool_select(&before,pool,2,candidate,revision,&match)==BOS_UNTRUSTED_BINDING);
 for(unsigned i=0;i<9;i++){pool[i]=pool[0];pool[i].alias=i+1;}CHECK(bos_pool_select(&before,pool,9,candidate,revision,&match)==BOS_LIMIT);
 CHECK(bos_pool_select(&before,pool,65,candidate,revision,&match)==BOS_ARGUMENT);
 candidate[0]^=1;CHECK(bos_pool_select(&before,pool,1,candidate,revision,&match)==BOS_NO_MATCH);candidate[0]^=1;
 memset(revision,0,32);CHECK(bos_pool_select(&before,pool,1,candidate,revision,&match)==BOS_UNTRUSTED_BINDING);memset(revision,4,32);
 pool[0].content.geometry.physical_offset=0x3100000;CHECK(bos_pool_select(&before,pool,1,candidate,revision,&match)==BOS_NO_MATCH);pool[0].content.geometry=g;
 /* Valid changed table cannot become trusted through an unavailable policy. */
 put(t+4*172,9);put(t+4*172+168,crc(t+4*172,168));tables(image,t);CHECK(measure(&s,&g,&now)==BOS_OK);
 CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_NO_MATCH);pool[0].table_policy=BOS_TABLE_AUTORESIZE_UNAVAILABLE;
 CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_POLICY_UNKNOWN&&!match.logical_content_verified);
 CHECK(bos_pool_select(&before,pool,1,candidate,revision,&match)==BOS_OK);pool[0].table_policy=BOS_TABLE_RAW_EXACT;
 /* Mutable sparse payload changes full snapshot, while exact content stays equal. */
 memcpy(t,image+4096,128u*172u);memset(payload,42,LEB);block(image+6*PEB,4,8,1,payload,LEB);CHECK(measure(&s,&g,&now)==BOS_OK&&now.rootfs_data_lebs==1);
 make_rule(pool,&now);CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_OK);
 image[6*PEB+4100]^=1;CHECK(measure(&s,&g,&before)==BOS_OK);CHECK(memcmp(before.snapshot_sha256,now.snapshot_sha256,32));CHECK(bos_pool_select(&before,pool,1,candidate,revision,&match)==BOS_OK);
 s.mutate=1;CHECK(measure(&s,&g,&now)==BOS_CHANGED);s.mutate=0;
 image[2*PEB+4100]^=1;CHECK(measure(&s,&g,&now)!=BOS_OK&&!now.scanned_pebs);image[2*PEB+4100]^=1;
 memcpy(image+7*PEB,image+2*PEB,PEB);CHECK(measure(&s,&g,&now)!=BOS_OK);memset(image+7*PEB,255,PEB);
 image[5*PEB+4100]^=1;CHECK(measure(&s,&g,&now)==BOS_OK);CHECK(bos_pool_select(&now,pool,1,candidate,revision,&match)==BOS_NO_MATCH);image[5*PEB+4100]^=1;
 image[PEB+4096]^=1;CHECK(measure(&s,&g,&now)!=BOS_OK);image[PEB+4096]^=1;
 t[168]^=1;tables(image,t);CHECK(measure(&s,&g,&now)!=BOS_OK);t[168]^=1;tables(image,t);
 s.io=1;CHECK(measure(&s,&g,&now)==BOS_IO);s.io=0;
 g.physical_offset=0;CHECK(measure(&s,&g,&now)!=BOS_OK);
 free(image);free(payload);free(t);free(post);free(pool);printf("PASS production pool C %u checks; whole-stream SHA; bounded post-attach normalization\n",checks);return 0;
}
