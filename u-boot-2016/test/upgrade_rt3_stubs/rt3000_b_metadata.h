#ifndef HOST_METADATA_H
#define HOST_METADATA_H
#include <net/httpd.h>
enum rt3_md_kind { RT3_MD_UBOOT=1, RT3_MD_ART=2, RT3_MD_CDT=3, RT3_MD_MIBIB=4 };
enum rt3_md_target { RT3_MD_TARGET_DEFAULT=0, RT3_MD_TARGET_PRIMARY, RT3_MD_TARGET_SECONDARY };
int rt3_metadata_target_parse(const char *,unsigned int,enum rt3_md_target *);
int rt3_metadata_write_target(enum rt3_md_kind,enum rt3_md_target,const char *,struct httpd_response *);
void rt3_metadata_invalidate(void);
int rt3_metadata_write(enum rt3_md_kind,const char *,struct httpd_response *);
#endif
