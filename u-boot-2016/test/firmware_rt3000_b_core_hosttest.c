/* Host regression for the same target-range and transaction core used by B. */
#define RT3_FW_HOST_TEST 1
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "../include/rt3000_b_fw_core.h"

#define PEB 16
#define LIMIT 344
struct mock {
	uint8_t flash[LIMIT][PEB];
	int bad[LIMIT];
	int open, close_count, erase_count, write_count, read_count;
	int fail_erase, fail_write, corrupt_read;
	uint64_t target, extent;
};
static int block_index(uint64_t off) { return (int)(off / PEB); }
static int is_bad(void *ctx, uint64_t off) { struct mock *m=ctx; return m->bad[block_index(off)]; }
static int erase(void *ctx, uint64_t off, uint32_t len) {
	struct mock *m=ctx; assert(m->open); assert(off>=m->target && off+len<=m->target+m->extent);
	m->erase_count++; if (m->fail_erase) return -EIO;
	memset(m->flash[block_index(off)],0xff,PEB); return 0;
}
static int write_peb(void *ctx, uint64_t off, const uint8_t *b, uint32_t len,uint32_t *done) {
	struct mock *m=ctx; assert(m->open); assert(off>=m->target && off+len<=m->target+m->extent);
	m->write_count++; if (m->fail_write) { *done=0; return -EIO; }
	memcpy(m->flash[block_index(off)],b,len); *done=len; return 0;
}
static int read_peb(void *ctx,uint64_t off,uint8_t *b,uint32_t len,uint32_t *done) {
	struct mock *m=ctx; m->read_count++; memcpy(b,m->flash[block_index(off)],len); *done=len;
	if (m->corrupt_read && m->read_count==m->corrupt_read) b[0]^=1;
	return 0;
}
static int open_window(void *ctx,uint64_t off,uint64_t size) {
	struct mock *m=ctx; assert(!m->open); m->open=1;m->target=off;m->extent=size;return 0;
}
static void close_window(void *ctx) { struct mock *m=ctx; assert(m->open);m->open=0;m->close_count++; }
static void init(struct mock *m) { memset(m,0,sizeof(*m));memset(m->flash,0xa5,sizeof(m->flash)); }

int main(void)
{
	struct mock m; uint32_t offsets[LIMIT]; uint8_t image[3*PEB], verify[PEB];
	uint8_t full_image[320*PEB]; unsigned int i;
	struct rt3_fw_io io={&m,is_bad,erase,write_peb,read_peb,open_window,close_window};
	static int identity1, identity2;
	memset(image,0x5a,sizeof(image));
	assert(rt3_fw_core_range_allows(1,&identity1,&identity1,100,200,120,20,0,0));
	assert(!rt3_fw_core_range_allows(0,&identity1,&identity1,100,200,120,20,0,0));
	assert(!rt3_fw_core_range_allows(1,&identity1,&identity2,100,200,120,20,0,0));
	assert(!rt3_fw_core_range_allows(1,&identity1,&identity1,100,200,120,20,1,0));
	assert(!rt3_fw_core_range_allows(1,&identity1,&identity1,100,200,120,20,0,1));
	assert(!rt3_fw_core_range_allows(1,&identity1,&identity1,100,200,190,20,0,0));
	{ uint8_t hash_a[4]={1,2,3,4}, hash_b[4]={1,2,3,4}, hash_changed[4]={1,2,3,5};
	  assert(rt3_fw_core_request_matches(7,7,"rootfs_1","rootfs_1","dual","dual",9,9,hash_a,hash_b,4));
	  assert(!rt3_fw_core_request_matches(7,8,"rootfs_1","rootfs_1","dual","dual",9,9,hash_a,hash_b,4));
	  assert(!rt3_fw_core_request_matches(7,7,"rootfs_1","rootfs","dual","dual",9,9,hash_a,hash_b,4));
	  assert(!rt3_fw_core_request_matches(7,7,"rootfs_1","rootfs_1","dual","single",9,9,hash_a,hash_b,4));
	  assert(!rt3_fw_core_request_matches(7,7,"rootfs_1","rootfs_1","dual","dual",9,10,hash_a,hash_b,4));
	  assert(!rt3_fw_core_request_matches(7,7,"rootfs_1","rootfs_1","dual","dual",9,9,hash_a,hash_changed,4));
	  assert(!rt3_fw_core_input_size_ok(0,100,100));
	  assert(!rt3_fw_core_input_size_ok(101,100,200));
	  assert(!rt3_fw_core_input_size_ok(101,200,100));
	  assert(rt3_fw_core_input_size_ok(100,100,100)); }
	assert(rt3_fw_core_capacity_ok(12,4,6,6));
	assert(!rt3_fw_core_capacity_ok(9,4,6,6));
	assert(rt3_fw_core_capacity_ok(320,320,296,24));
	assert(rt3_fw_core_capacity_ok(320,296,296,24));
	assert(rt3_fw_core_capacity_ok(640,640,616,24));
	assert(rt3_fw_core_capacity_ok(640,616,616,24));
	assert(!rt3_fw_core_capacity_ok(319,320,296,24));
	assert(!rt3_fw_core_capacity_ok(319,296,296,24));
	assert(!rt3_fw_core_capacity_ok(320,0,296,24));
	assert(!rt3_fw_core_capacity_ok(320,1,UINT32_MAX,1));

	/* Successful write erases selected good PEBs, preserves bad PEB and tail. */
	init(&m);m.bad[2]=1;
	assert(rt3_fw_core_transaction(&io,PEB,7*PEB,image,sizeof(image),PEB,3,5,1,offsets,LIMIT,verify)==0);
	assert(!m.open && m.close_count==1 && m.erase_count==6 && m.write_count==3);
	assert(m.bad[2] && !memcmp(m.flash[1],image,PEB) && !memcmp(m.flash[3],image+PEB,PEB));
	assert(m.flash[5][0]==0xff && m.flash[7][PEB-1]==0xff);

	/* Protected target bounds, capacity failure and malformed sizes fail before opening. */
	init(&m);assert(rt3_fw_core_transaction(&io,PEB,7*PEB,image,sizeof(image),PEB,3,5,6,offsets,LIMIT,verify)==-ENOSPC);assert(!m.open&&!m.erase_count);
	init(&m);assert(rt3_fw_core_transaction(&io,PEB,LIMIT*PEB+PEB,image,sizeof(image),PEB,3,5,1,offsets,LIMIT,verify)==-EINVAL);assert(!m.open);
	init(&m);assert(rt3_fw_core_transaction(&io,PEB,7*PEB,image,sizeof(image)-1,PEB,3,5,1,offsets,LIMIT,verify)==-EINVAL);assert(!m.open);
	init(&m);assert(rt3_fw_core_transaction(&io,PEB+1,7*PEB,image,sizeof(image),PEB,3,5,1,offsets,LIMIT,verify)==-EINVAL);assert(!m.open);

	/* All post-open failure paths close the temporary writer window. */
	init(&m);m.fail_erase=1;assert(rt3_fw_core_transaction(&io,PEB,7*PEB,image,sizeof(image),PEB,3,5,1,offsets,LIMIT,verify)==-EIO);assert(!m.open&&m.close_count==1&&m.write_count==0);
	init(&m);m.fail_write=1;assert(rt3_fw_core_transaction(&io,PEB,7*PEB,image,sizeof(image),PEB,3,5,1,offsets,LIMIT,verify)==-EIO);assert(!m.open&&m.close_count==1);
	init(&m);m.corrupt_read=1;assert(rt3_fw_core_transaction(&io,PEB,7*PEB,image,sizeof(image),PEB,3,5,1,offsets,LIMIT,verify)==-EIO);assert(!m.open&&m.close_count==1);

	/* A complete equal-length image may use its embedded EC-only PEBs for
	 * UBI free space; no second image-sized reserve is charged. */
	for(i=0;i<sizeof(full_image);i++) full_image[i]=(uint8_t)(i*37U+(i/PEB));
	init(&m);
	assert(rt3_fw_core_transaction(&io,0,320*PEB,full_image,sizeof(full_image),
				       PEB,320,296,24,offsets,LIMIT,verify)==0);
	assert(!m.open&&m.close_count==1&&m.erase_count==320&&m.write_count==320);
	for(i=0;i<320;i++) assert(!memcmp(m.flash[i],full_image+i*PEB,PEB));

	/* One bad PEB makes the intact equal-length input itself not fit. */
	init(&m);m.bad[319]=1;
	assert(rt3_fw_core_transaction(&io,0,320*PEB,full_image,sizeof(full_image),
				       PEB,320,296,24,offsets,LIMIT,verify)==-ENOSPC);
	assert(!m.open&&!m.erase_count&&!m.write_count);

	/* A bad PEB can also leave the image fitting while reserved+margin does not. */
	init(&m);m.bad[319]=1;
	assert(rt3_fw_core_transaction(&io,0,320*PEB,full_image,296*PEB,
				       PEB,296,296,24,offsets,LIMIT,verify)==-ENOSPC);
	assert(!m.open&&!m.erase_count&&!m.write_count);
	return 0;
}
