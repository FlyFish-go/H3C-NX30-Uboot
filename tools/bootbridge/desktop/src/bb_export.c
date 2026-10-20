#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif
#include "bb_export.h"
#include "bb_export_plan.h"
#include "bridge_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

static int pathsep(char c) { return c=='/' || c=='\\'; }
int bb_windows_path_allowed(const char *p) {
    size_t i, start=0, n;
    if(!p || !(n=strlen(p))) return 0;
    if(n>=3 && pathsep(p[0]) && pathsep(p[1]) &&
        (p[2]=='.' || p[2]=='?')) return 0;
    for(i=0;i<n;i++) {
        unsigned char c=(unsigned char)p[i];
        if(c==':') {
            if(i!=1 || !((p[0]>='A' && p[0]<='Z') || (p[0]>='a' && p[0]<='z')) ||
                i+1>=n || !pathsep(p[i+1])) return 0;
        }
        if(c<32 || c=='"' || c=='<' || c=='>' || c=='|' || c=='*' || c=='?') return 0;
        if(pathsep(p[i])) {
            size_t len=i-start;
            if(len && (p[i-1]==' ' || p[i-1]=='.') &&
                !(len==1 && p[start]=='.') &&
                !(len==2 && p[start]=='.' && p[start+1]=='.')) return 0;
            start=i+1;
        }
    }
    return p[n-1]!=' ' && p[n-1]!='.';
}
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
typedef HANDLE file_t;
#define BAD_FILE INVALID_HANDLE_VALUE
static wchar_t *wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *p;
    if (!n) return NULL;
    p = (wchar_t *)malloc((size_t)n * sizeof(*p));
    if (p && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n)) { free(p); p = NULL; }
    return p;
}
static int syserr(void) { return (int)GetLastError(); }
static file_t openfile(const char *s, int create) {
    wchar_t *p; HANDLE h; BY_HANDLE_FILE_INFORMATION info; DWORD attr;
    if (!bb_windows_path_allowed(s)) { SetLastError(ERROR_INVALID_NAME); return BAD_FILE; }
    p = wide(s);
    if (!p) return BAD_FILE;
    /* Refuse device/extended namespaces. Ordinary UTF-8 DOS and UNC paths only. */
    if (p[0] == L'\\' && p[1] == L'\\' &&
        (p[2] == L'.' || p[2] == L'?')) {
        free(p); SetLastError(ERROR_INVALID_NAME); return BAD_FILE;
    }
    if (!create) {
        attr = GetFileAttributesW(p);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            free(p); SetLastError(ERROR_INVALID_DATA); return BAD_FILE;
        }
    }
    h = CreateFileW(p, create ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                    FILE_SHARE_READ, NULL, create ? CREATE_NEW : OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    free(p);
    if (h != BAD_FILE && (GetFileType(h) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))) {
        CloseHandle(h); SetLastError(ERROR_INVALID_DATA); return BAD_FILE;
    }
    return h;
}
static int exists(const char *s) { wchar_t *p = wide(s); DWORD a; if (!p) return 1; a = GetFileAttributesW(p); free(p); return a != INVALID_FILE_ATTRIBUTES; }
static int rd(file_t f, void *b, size_t n) { DWORD k; return ReadFile(f,b,(DWORD)n,&k,NULL) ? (int)k : -1; }
static int wr(file_t f, const void *b, size_t n) { DWORD k; return WriteFile(f,b,(DWORD)n,&k,NULL) ? (int)k : -1; }
static int rewindfile(file_t f) { LARGE_INTEGER z; z.QuadPart = 0; return SetFilePointerEx(f,z,NULL,FILE_BEGIN) != 0; }
static int seekfile(file_t f,uint64_t offset) { LARGE_INTEGER z; z.QuadPart=(LONGLONG)offset; return SetFilePointerEx(f,z,NULL,FILE_BEGIN)!=0; }
static int syncfile(file_t f) { return FlushFileBuffers(f) != 0; }
static int closefile(file_t f) { return CloseHandle(f) != 0; }
static void removefile(const char *s) { wchar_t *p = wide(s); if (p) { DeleteFileW(p); free(p); } }
static int commit(const char *a, const char *b) { wchar_t *x = wide(a), *y = wide(b); int ok = x && y && MoveFileExW(x,y,0); free(x); free(y); return ok; }
static unsigned long processid(void) { return GetCurrentProcessId(); }
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
typedef int file_t;
#define BAD_FILE (-1)
static int syserr(void) { return errno; }
static file_t openfile(const char *s, int create) {
    struct stat st; int f;
    if (!create && (lstat(s,&st) || !S_ISREG(st.st_mode))) { errno = EINVAL; return BAD_FILE; }
    f = open(s, create ? O_CREAT | O_EXCL | O_RDWR : O_RDONLY | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (f >= 0 && (fstat(f,&st) || !S_ISREG(st.st_mode))) { close(f); errno = EINVAL; return BAD_FILE; }
    return f;
}
static int exists(const char *s) { struct stat st; return lstat(s,&st) == 0 || errno != ENOENT; }
static int rd(file_t f, void *b, size_t n) { ssize_t k; do { k=read(f,b,n); } while(k<0 && errno==EINTR); return (int)k; }
static int wr(file_t f, const void *b, size_t n) { ssize_t k; do { k=write(f,b,n); } while(k<0 && errno==EINTR); return (int)k; }
static int rewindfile(file_t f) { return lseek(f,0,SEEK_SET) == 0; }
static int seekfile(file_t f,uint64_t offset) { return lseek(f,(off_t)offset,SEEK_SET)==(off_t)offset; }
static int syncfile(file_t f) { return fsync(f) == 0; }
static int closefile(file_t f) { return close(f) == 0; }
static void removefile(const char *s) { (void)unlink(s); }
/* link is an atomic no-replace commit on the same filesystem. */
static int commit(const char *a, const char *b) { return link(a,b) == 0; }
static unsigned long processid(void) { return (unsigned long)getpid(); }
#endif

static int binpath(const char *p) {
    size_t n;
    if (!p || (n=strlen(p)) < 4) return 0;
    return p[n-4]=='.' && (p[n-3]=='b'||p[n-3]=='B') &&
           (p[n-2]=='i'||p[n-2]=='I') && (p[n-1]=='n'||p[n-1]=='N');
}
static int valid(const bb_recipe *r) {
    uint64_t end=0, size=r->input_size; size_t i;
    /* SHA-256 length encoding is limited to fewer than 2^64 bits. */
    if (r->input_size > UINT64_MAX/8 || r->output_size > UINT64_MAX/8 ||
        (r->span_count && !r->spans)) return 0;
    for(i=0;i<r->span_count;i++) {
        const bb_span *s=&r->spans[i];
        if (s->offset < end || s->offset > r->input_size ||
            s->remove_size > r->input_size-s->offset || (s->bytes_size && !s->bytes)) return 0;
        end=s->offset+s->remove_size;
        if (size < s->remove_size) return 0;
        size-=s->remove_size;
        if ((uint64_t)s->bytes_size > UINT64_MAX-size) return 0;
        size+=(uint64_t)s->bytes_size;
    }
    return size == r->output_size;
}
static int hashfile(file_t f, uint64_t *size, unsigned char digest[32]) {
    unsigned char b[65536]; bi_sha256_ctx h; int n; *size=0; bi_sha256_init(&h);
    while((n=rd(f,b,sizeof(b)))>0) {
        if (*size > UINT64_MAX/8-(uint64_t)n) return 0;
        *size+=(uint64_t)n; bi_sha256_update(&h,b,(size_t)n);
    }
    if(n<0) return 0;
    bi_sha256_final(&h,digest); return 1;
}
static int writeall(file_t f, const unsigned char *b, size_t n, bb_result *res) {
    while(n) { size_t z=n>65536 ? 65536:n; int k=wr(f,b,z); if(k<=0) return 0; b+=k; n-=(size_t)k; res->bytes_written+=(uint64_t)k; }
    return 1;
}
/* Copy and hash even bytes discarded by replacement. */
static int segment(file_t in, file_t out, uint64_t n, int copy, bi_sha256_ctx *h, bb_result *res) {
    unsigned char b[65536];
    while(n) { size_t z=n>sizeof(b)?sizeof(b):(size_t)n; int k=rd(in,b,z); if(k<=0) return 0;
        bi_sha256_update(h,b,(size_t)k); if(copy && !writeall(out,b,(size_t)k,res)) return 0; n-=(uint64_t)k; }
    return 1;
}
static int nonzero_digest(const unsigned char *p){unsigned x=0;size_t i;for(i=0;i<32;i++)x|=p[i];return x!=0;}
static int valid_plan(const bb_recipe*r,const bb_plan_op*ops,size_t count){
    uint64_t total=0;size_t i;
    if(!r->input_size||!r->output_size||r->input_size>UINT64_MAX/8||
       r->output_size>UINT64_MAX/8||!nonzero_digest(r->input_sha256)||
       !nonzero_digest(r->output_sha256)||!ops||!count||count>BB_PLAN_MAX_OPERATIONS)return 0;
    for(i=0;i<count;i++){
        const bb_plan_op*p=&ops[i];
        if(!p->size||p->size>r->output_size-total)return 0;
        if(p->kind==BB_PLAN_COPY||p->kind==BB_PLAN_XOR){
            if(p->input_offset>r->input_size||p->size>r->input_size-p->input_offset)return 0;
            if(p->kind==BB_PLAN_COPY&&p->bytes)return 0;
        }else if(p->kind==BB_PLAN_LITERAL){if(p->input_offset)return 0;}
        else return 0;
        if(p->kind!=BB_PLAN_COPY&&(!p->bytes||p->size>SIZE_MAX))return 0;
        total+=p->size;
    }
    return total==r->output_size;
}
static int emit_plan(file_t in,file_t out,const bb_plan_op*ops,size_t count,bb_result*res){
    unsigned char buffer[65536];size_t i;
    for(i=0;i<count;i++){
        const bb_plan_op*p=&ops[i];uint64_t done=0;
        if(p->kind==BB_PLAN_LITERAL){if(!writeall(out,p->bytes,(size_t)p->size,res))return 0;continue;}
        if(!seekfile(in,p->input_offset))return 0;
        while(done<p->size){
            uint64_t left=p->size-done;size_t take=left>sizeof(buffer)?sizeof(buffer):(size_t)left;
            int got=rd(in,buffer,take);size_t j;
            if(got<=0)return 0;
            if(p->kind==BB_PLAN_XOR)for(j=0;j<(size_t)got;j++)buffer[j]^=p->bytes[(size_t)done+j];
            if(!writeall(out,buffer,(size_t)got,res))return 0;
            done+=(uint64_t)got;
        }
    }
    return 1;
}
static bb_result export_bound(const char *input,const char *output,const bb_recipe*r,
                               const bb_plan_op*ops,size_t op_count,int plan){
    bb_result res={BB_INVALID_RECIPE,0,0};size_t i;
    file_t in=BAD_FILE,out=BAD_FILE; char *temp=NULL; int owned=0;
    uint64_t size,pos=0; unsigned char digest[32], extra; bi_sha256_ctx h;
    if(plan?!valid_plan(r,ops,op_count):!valid(r))return res;
    res.status=BB_INVALID_PATH; if(!input || !*input || !binpath(output)) return res;
#ifdef _WIN32
    if(!bb_windows_path_allowed(input) || !bb_windows_path_allowed(output)) return res;
#endif
    res.status=BB_OUTPUT_EXISTS; if(exists(output)) return res;
    res.status=BB_INPUT_ERROR; in=openfile(input,0); if(in==BAD_FILE) goto done;
    if(!hashfile(in,&size,digest)) goto done;
    res.status=BB_INPUT_MISMATCH;
    if(size!=r->input_size || memcmp(digest,r->input_sha256,32)) goto done;
    res.status=BB_IO_ERROR; if(!rewindfile(in)) goto done;
    if(strlen(output)>SIZE_MAX-80) goto done;
    temp=(char *)malloc(strlen(output)+80); if(!temp) goto done;
    for(i=0;i<100;i++) {
        (void)snprintf(temp,strlen(output)+80,"%s.bbtmp-%lu-%lu",output,processid(),(unsigned long)i);
        out=openfile(temp,1); if(out!=BAD_FILE) { owned=1; break; }
    }
    if(out==BAD_FILE) goto done;
    if(plan){
        if(!emit_plan(in,out,ops,op_count,&res)||!rewindfile(in)||!hashfile(in,&size,digest))goto done;
        res.status=BB_INPUT_MISMATCH;
        if(size!=r->input_size||memcmp(digest,r->input_sha256,32))goto done;
    }else{
      bi_sha256_init(&h);
      for(i=0;i<r->span_count;i++) {
        const bb_span *s=&r->spans[i];
        if(!segment(in,out,s->offset-pos,1,&h,&res) ||
           !segment(in,out,s->remove_size,0,&h,&res) ||
           !writeall(out,s->bytes,s->bytes_size,&res)) goto done;
        pos=s->offset+s->remove_size;
      }
      if(!segment(in,out,r->input_size-pos,1,&h,&res)) goto done;
      bi_sha256_final(&h,digest);
      res.status=BB_INPUT_MISMATCH;
      if(rd(in,&extra,1)!=0 || memcmp(digest,r->input_sha256,32)) goto done;
    }
    res.status=BB_IO_ERROR;
    if(!syncfile(out) || !rewindfile(out) || !hashfile(out,&size,digest)) goto done;
    res.status=BB_OUTPUT_MISMATCH;
    if(size!=r->output_size || memcmp(digest,r->output_sha256,32)) goto done;
    res.status=BB_IO_ERROR;
    if(!closefile(out)) { out=BAD_FILE; goto done; } out=BAD_FILE;
    if(!commit(temp,output)) { if(exists(output)) res.status=BB_OUTPUT_EXISTS; goto done; }
    res.status=BB_OK;
 done:
    if(res.status==BB_IO_ERROR || res.status==BB_INPUT_ERROR) res.system_error=syserr();
    if(out!=BAD_FILE) (void)closefile(out);
    if(in!=BAD_FILE) (void)closefile(in);
    if(owned) removefile(temp);
    free(temp); return res;
}
bb_result bb_export_bin(const char *input,const char *output,const bb_recipe*registry,size_t count,const char*id){
    bb_result res={BB_UNKNOWN_RECIPE,0,0};const bb_recipe*r=NULL;size_t i;
    if(!registry||!count||!id)return res;
    for(i=0;i<count;i++)if(registry[i].id&&!strcmp(registry[i].id,id)){
        if(r){res.status=BB_INVALID_RECIPE;return res;}r=&registry[i];
    }
    return r?export_bound(input,output,r,NULL,0,0):res;
}
bb_result bb_export_plan_bin(const char*input,const char*output,const bb_plan_recipe*registry,size_t count,const char*id){
    bb_result res={BB_UNKNOWN_RECIPE,0,0};const bb_plan_recipe*p=NULL;bb_recipe r;size_t i;
    if(!registry||!count||!id)return res;
    for(i=0;i<count;i++)if(registry[i].id&&!strcmp(registry[i].id,id)){
        if(p){res.status=BB_INVALID_RECIPE;return res;}p=&registry[i];
    }
    if(!p)return res;
    memset(&r,0,sizeof(r));r.id=p->id;r.input_size=p->input_size;r.output_size=p->output_size;
    memcpy(r.input_sha256,p->input_sha256,32);memcpy(r.output_sha256,p->output_sha256,32);
    return export_bound(input,output,&r,p->operations,p->operation_count,1);
}
