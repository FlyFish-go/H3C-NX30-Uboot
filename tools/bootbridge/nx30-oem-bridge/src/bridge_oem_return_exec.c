#define _GNU_SOURCE
#include "bridge_oem_return_exec.h"
#include "bridge_sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef BR_RETURN_EXEC_SHA256
#define BR_RETURN_EXEC_SHA256 ""
#endif
#ifndef BR_RETURN_EXEC_BYTES
#define BR_RETURN_EXEC_BYTES 0
#endif


int bi_oem_return_exec_verified_detail(int rootfd, const char *reason_code,
                                const char *reason_text,
                                const char *candidate_source_revision_sha256,
                                const char *execution_detail) {
    int fd=-1, rc=-1; struct stat st; bi_sha256_ctx sha; uint8_t got[32]; char got_hex[65];
    uint8_t buf[16384]; ssize_t n; uint64_t total=0;
    char code[65], reason[257], revision[65], detail[2048];
    if (rootfd<0 || !reason_code || !reason_text || !candidate_source_revision_sha256 ||
        strlen(reason_code)>=sizeof(code) || strlen(reason_text)>=sizeof(reason) ||
        strlen(candidate_source_revision_sha256)>=sizeof(revision) ||
        !execution_detail || strlen(execution_detail)>=sizeof(detail)) {
        errno=EINVAL; return -1;
    }
    if (BR_RETURN_EXEC_BYTES==0 || strlen(BR_RETURN_EXEC_SHA256)!=64) {
        errno=ENOTSUP; return -1;
    }
    for (size_t i=0; reason_code[i]; ++i)
        if (!((reason_code[i]>='a'&&reason_code[i]<='z') ||
              (reason_code[i]>='A'&&reason_code[i]<='Z') ||
              (reason_code[i]>='0'&&reason_code[i]<='9') || reason_code[i]=='_' || reason_code[i]=='-')) {
            errno=EINVAL; return -1;
        }
    if(*candidate_source_revision_sha256){if(strlen(candidate_source_revision_sha256)!=64){errno=EINVAL;return -1;}
        for(size_t i=0;candidate_source_revision_sha256[i];i++)if(!((candidate_source_revision_sha256[i]>='0'&&candidate_source_revision_sha256[i]<='9')||(candidate_source_revision_sha256[i]>='a'&&candidate_source_revision_sha256[i]<='f'))){errno=EINVAL;return -1;}}
    snprintf(code,sizeof(code),"%s",reason_code);
    snprintf(reason,sizeof(reason),"%s",reason_text);
    snprintf(revision,sizeof(revision),"%s",candidate_source_revision_sha256);
    for(const unsigned char *p=(const unsigned char*)execution_detail;*p;p++)
        if(*p<0x20u||*p>0x7eu){errno=EINVAL;return -1;}
    snprintf(detail,sizeof(detail),"%s",execution_detail);
    fd=openat(rootfd,"usr/sbin/bridge-oem-return",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return -1;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||
       (uint64_t)st.st_size!=(uint64_t)BR_RETURN_EXEC_BYTES||
       (st.st_mode&0022)||st.st_uid!=0)goto done;
    bi_sha256_init(&sha);
    for(;;){n=pread(fd,buf,sizeof(buf),(off_t)total);if(n<0&&errno==EINTR)continue;if(n<0)goto done;if(!n)break;
        total+=(uint64_t)n;if(total>(uint64_t)BR_RETURN_EXEC_BYTES)goto done;bi_sha256_update(&sha,buf,(size_t)n);}
    if(total!=(uint64_t)BR_RETURN_EXEC_BYTES)goto done;
    bi_sha256_final(&sha,got);bi_sha256_hex(got,got_hex);if(strcmp(got_hex,BR_RETURN_EXEC_SHA256))goto done;
    if(lseek(fd,0,SEEK_SET)<0)goto done;
    /* Keep the verified descriptor available to an exec interpreter, as the
     * provider dispatcher does. Failure here leaves the helper unexecuted. */
    if(fcntl(fd,F_SETFD,0)<0)goto done;
    { char *const argv[]={(char*)"/usr/sbin/bridge-oem-return",(char*)"--dispatch-reason",code,reason,
        (char*)"--dispatch-candidate-source-revision",revision,(char*)"-b",(char*)"192.168.10.1",(char*)"-p",(char*)"80",(char*)"-w",(char*)"www",NULL};
      char *const extended_argv[]={(char*)"/usr/sbin/bridge-oem-return",(char*)"--dispatch-reason",code,reason,
        (char*)"--dispatch-candidate-source-revision",revision,(char*)"--dispatch-execution-detail",detail,
        (char*)"-b",(char*)"192.168.10.1",(char*)"-p",(char*)"80",(char*)"-w",(char*)"www",NULL};
      char *const envp[]={(char*)"PATH=/bin:/sbin:/usr/bin:/usr/sbin",
        (char*)"BRIDGE_RETURN_FALLBACK=1",
        (char*)"BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1",NULL};
      fexecve(fd,detail[0]?extended_argv:argv,envp); }
done:
    rc=-1; close(fd); return rc;
}

int bi_oem_return_exec_verified(int rootfd, const char *reason_code,
                                const char *reason_text,
                                const char *candidate_source_revision_sha256) {
    return bi_oem_return_exec_verified_detail(rootfd,reason_code,reason_text,
                                             candidate_source_revision_sha256,"");
}
