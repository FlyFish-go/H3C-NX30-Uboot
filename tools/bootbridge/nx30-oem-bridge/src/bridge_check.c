#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "bridge_install.h"

/* Offline, read-only inspection service. It never opens /dev or invokes a shell. */
#define REQ_MAX 8192
#define JSON_MAX 32768
#define FILE_MAX 16384
#define MTD_MAX 48

static char fixture_root[512] = "/";
static char web_dir[512] = "web";
static char bind_addr[64] = "192.168.10.1";
static int listen_port = 8080;
static int fixture_override;

typedef struct { char data[JSON_MAX]; size_t len; int failed; } jbuf;
typedef struct {
    int index;
    unsigned long long size;
    char name[96], type[32];
    long writesize, erasesize, ecc_strength, ecc_failures;
    long long offset;
    int have_writesize, have_erasesize, have_ecc, have_failures, have_offset;
} mtd;
static mtd mtds[MTD_MAX];
static size_t nmtd;

static void jout_n(jbuf *b, const char *s, size_t n) {
    if (b->failed || n >= sizeof(b->data) - b->len) { b->failed = 1; return; }
    memcpy(b->data + b->len, s, n); b->len += n; b->data[b->len] = 0;
}
static void jout(jbuf *b, const char *s) { jout_n(b,s,strlen(s)); }
static size_t valid_utf8_sequence(const unsigned char *p,size_t remaining){
    if(remaining>=2&&p[0]>=0xc2&&p[0]<=0xdf&&(p[1]&0xc0)==0x80)return 2;
    if(remaining>=3&&p[0]>=0xe0&&p[0]<=0xef&&(p[2]&0xc0)==0x80){
        if(p[0]==0xe0&&p[1]>=0xa0&&p[1]<=0xbf)return 3;
        if(p[0]==0xed&&p[1]>=0x80&&p[1]<=0x9f)return 3;
        if(p[0]!=0xe0&&p[0]!=0xed&&(p[1]&0xc0)==0x80)return 3;
    }
    if(remaining>=4&&p[0]>=0xf0&&p[0]<=0xf4&&(p[2]&0xc0)==0x80&&(p[3]&0xc0)==0x80){
        if(p[0]==0xf0&&p[1]>=0x90&&p[1]<=0xbf)return 4;
        if(p[0]==0xf4&&p[1]>=0x80&&p[1]<=0x8f)return 4;
        if(p[0]!=0xf0&&p[0]!=0xf4&&(p[1]&0xc0)==0x80)return 4;
    }
    return 0;
}
static void jstr(jbuf *b, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    size_t i=0,n=strlen((const char *)p);
    jout(b, "\"");
    while(i<n&&!b->failed){
        unsigned char ch=p[i];
        char esc[7];
        switch (ch) {
        case '"': jout(b, "\\\""); ++i; break;
        case '\\': jout(b, "\\\\"); ++i; break;
        case '\b': jout(b, "\\b"); ++i; break;
        case '\f': jout(b, "\\f"); ++i; break;
        case '\n': jout(b, "\\n"); ++i; break;
        case '\r': jout(b, "\\r"); ++i; break;
        case '\t': jout(b, "\\t"); ++i; break;
        default:
            if (ch < 0x20) { snprintf(esc, sizeof(esc), "\\u%04x", ch); jout(b, esc); ++i; }
            else if(ch<0x80){char c[2]={(char)ch,0};jout(b,c);++i;}
            else {size_t seq=valid_utf8_sequence(p+i,n-i);if(seq){jout_n(b,(const char *)p+i,seq);i+=seq;}else{snprintf(esc,sizeof(esc),"\\u%04x",ch);jout(b,esc);++i;}}
        }
    }
    jout(b, "\"");
}
static void jkeystr(jbuf *b, const char *key, const char *value) {
    jstr(b, key); jout(b, ":"); jstr(b, value);
}
static void jkeynum(jbuf *b, const char *key, long long v) {
    char n[64]; snprintf(n, sizeof(n), "%lld", v); jstr(b, key); jout(b, ":"); jout(b, n);
}
static void jcomma(jbuf *b, int *first) { if (!*first) jout(b, ","); *first = 0; }

static int rooted_path(char *out, size_t cap, const char *rel) {
    int n;
    if (strcmp(fixture_root, "/") == 0) n = snprintf(out, cap, "/%s", rel);
    else n = snprintf(out, cap, "%s/%s", fixture_root, rel);
    return n >= 0 && (size_t)n < cap;
}
static int read_rel(const char *rel, char *out, size_t cap) {
    char path[1024]; FILE *f; size_t n;
    if (!rooted_path(path, sizeof(path), rel) || cap < 2) return 0;
    f = fopen(path, "rb"); if (!f) return 0;
    n = fread(out, 1, cap - 1, f); out[n] = 0;
    if (ferror(f)) { fclose(f); return 0; }
    fclose(f); return 1;
}
static long parse_long(const char *s, int base, int *ok) {
    char *end = NULL; errno = 0; long v = strtol(s, &end, base);
    while (end && isspace((unsigned char)*end)) ++end;
    *ok = end != s && end && *end == 0 && errno == 0; return *ok ? v : 0;
}
static void attr_read(mtd *m, const char *attr, long *v, int *have) {
    char rel[160], buf[128]; int ok;
    snprintf(rel, sizeof(rel), "sys/class/mtd/mtd%d/%s", m->index, attr);
    if (read_rel(rel, buf, sizeof(buf))) { *v = parse_long(buf, 0, &ok); *have = ok; }
}
static void load_mtd(void) {
    char buf[FILE_MAX], *line;
    nmtd = 0;
    if (!read_rel("proc/mtd", buf, sizeof(buf))) return;
    line = strchr(buf, '\n'); if (!line) return; ++line;
    while (*line && nmtd < MTD_MAX) {
        char *end = strchr(line, '\n'); if (end) *end = 0;
        unsigned idx, sz, er;
        char name[96] = {0};
        if (sscanf(line, " mtd%u: %x %x \"%95[^\"]\"", &idx, &sz, &er, name) == 4) {
            mtd *m = &mtds[nmtd++]; memset(m, 0, sizeof(*m));
            m->index = (int)idx; m->size = sz;
            snprintf(m->name, sizeof(m->name), "%s", name);
            char rel[128], value[128];
            snprintf(rel, sizeof(rel), "sys/class/mtd/mtd%u/name", idx);
            if (read_rel(rel, value, sizeof(value)) && value[0]) {
                value[strcspn(value, "\r\n")] = 0;
                if (value[0]) snprintf(m->name, sizeof(m->name), "%.95s", value);
            }
            snprintf(rel, sizeof(rel), "sys/class/mtd/mtd%u/type", idx);
            if (read_rel(rel, value, sizeof(value))) {
                value[strcspn(value, "\r\n")] = 0;
                snprintf(m->type, sizeof(m->type), "%.31s", value);
            }
            else snprintf(m->type, sizeof(m->type), "unknown");
            attr_read(m, "writesize", &m->writesize, &m->have_writesize);
            attr_read(m, "erasesize", &m->erasesize, &m->have_erasesize);
            attr_read(m, "ecc_strength", &m->ecc_strength, &m->have_ecc);
            attr_read(m, "ecc_failures", &m->ecc_failures, &m->have_failures);
            long off = 0; int have = 0;
            attr_read(m, "offset", &off, &have);
            m->offset = off; m->have_offset = have;
        }
        if (!end) break;
        line = end + 1;
    }
}
static int has_ci(const char *s, const char *needle) {
    size_t n = strlen(needle);
    for (; *s; ++s) if (strncasecmp(s, needle, n) == 0) return 1;
    return 0;
}
static void check_item(jbuf *b, int *first, const char *name, const char *state, const char *detail) {
    jcomma(b, first); jout(b, "{"); jkeystr(b, "name", name); jout(b, ",");
    jkeystr(b, "status", state); jout(b, ","); jkeystr(b, "detail", detail); jout(b, "}");
}
/* Returns 1 for one value, 0 when absent, and -1 for conflicting duplicates. */
static int parse_cmdline_value(const char *cmd, const char *key, char *out, size_t cap) {
    size_t k = strlen(key); const char *p = cmd; int found = 0;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) ++p;
        const char *e = p; while (*e && !isspace((unsigned char)*e)) ++e;
        size_t n = (size_t)(e-p);
        if (n > k && !strncmp(p, key, k) && p[k] == '=') {
            size_t v = n-k-1; char candidate[160];
            if (cap > sizeof(candidate)) cap = sizeof(candidate);
            if (v >= cap) v = cap-1;
            memcpy(candidate, p+k+1, v); candidate[v] = 0;
            if (found && strcmp(out, candidate) != 0) { out[0]=0; return -1; }
            if (!found) { memcpy(out, candidate, v+1); found=1; }
        }
        p = e;
    }
    if (!found) out[0] = 0;
    return found;
}
static int name_is(const char *observed, const char *bare, const char *prefixed) {
    return !strcasecmp(observed, bare) || !strcasecmp(observed, prefixed);
}
static void count_category(int kind, int *count, int *bad_size) {
    size_t i; *count = 0; *bad_size = 0;
    for (i=0;i<nmtd;i++) {
        const char *n=mtds[i].name; int match=0; unsigned long long expected=0;
        if (kind==0 && (name_is(n,"APPSBL","0:APPSBL") || name_is(n,"APPSBL_1","0:APPSBL_1"))) { match=1; expected=0x140000; }
        if (kind==1 && (name_is(n,"BOOTCONFIG","0:BOOTCONFIG") || name_is(n,"BOOTCONFIG1","0:BOOTCONFIG1"))) { match=1; expected=0x40000; }
        if (kind==2 && name_is(n,"ART","0:ART")) { match=1; expected=0x100000; }
        if (kind==3 && (name_is(n,"rootfs","rootfs") || name_is(n,"rootfs_1","rootfs_1"))) { match=1; expected=0x2800000; }
        if (match) { ++*count; if (mtds[i].size != expected) *bad_size=1; }
    }
}
static void build_report(jbuf *b, int install_available) {
    char cmd[4096]="", mem[4096]="", compatible[512]="", model[256]="", kernel[128]="";
    char val[160]; struct utsname un; long ram_kb=0; int first, i, ok;
    if (!read_rel("proc/cmdline", cmd, sizeof(cmd))) cmd[0]=0;
    read_rel("proc/meminfo", mem, sizeof(mem));
    read_rel("proc/device-tree/model", model, sizeof(model));
    read_rel("proc/device-tree/compatible", compatible, sizeof(compatible));
    if(read_rel("proc/sys/kernel/osrelease",kernel,sizeof(kernel)))kernel[strcspn(kernel,"\r\n")]=0;
    if(!kernel[0]){uname(&un);snprintf(kernel,sizeof(kernel),"%.127s",un.release);}
    if (!model[0]) read_rel("sys/firmware/devicetree/base/model", model, sizeof(model));
    if (!compatible[0]) read_rel("sys/firmware/devicetree/base/compatible", compatible, sizeof(compatible));
    for (i=0;mem[i];i++) if (!strncmp(mem+i,"MemTotal:",9)) { sscanf(mem+i+9," %ld",&ram_kb); break; }
    load_mtd();
    jout(b,"{"); jkeystr(b,"profile","H3C-BOOTBRIDGE-MULTICONFIG-V1"); jout(b,",");
    jkeystr(b,"hardware_classification","UNKNOWN"); jout(b,",");
    jout(b, install_available ? "\"install_available\":true," : "\"install_available\":false,");
    jkeystr(b,"identity_limit","观察结果仅用于离线核对；兼容信息不能证明实体型号，也不构成安装许可。"); jout(b,",");
    jout(b,"\"observed\":{"); jkeystr(b,"kernel",kernel); jout(b,",");
    jkeynum(b,"ram_total_kib",ram_kb); jout(b,",");
    jkeystr(b,"device_tree_model",model); jout(b,","); jkeystr(b,"device_tree_compatible",compatible); jout(b,",");
    jout(b,"\"boot_parameters\":{");
    const char *keys[]={"machid","nandspare","nandeccbits","cnss2.skip_radio_bmap"};
    const char *outs[]={"machid","nandspare","nandeccbits","skip_radio_bmap"};
    for (i=0;i<4;i++) { if(i)jout(b,","); parse_cmdline_value(cmd,keys[i],val,sizeof(val)); jkeystr(b,outs[i],val); }
    jout(b,"},\"partitions\":[");
    for (i=0;i<(int)nmtd;i++) {
        mtd *m=&mtds[i]; if(i)jout(b,","); jout(b,"{");
        jkeynum(b,"index",m->index); jout(b,","); jkeystr(b,"name",m->name); jout(b,",");
        jkeynum(b,"size_bytes",(long long)m->size); jout(b,","); jkeystr(b,"type",m->type); jout(b,",");
        jkeynum(b,"writesize",m->have_writesize?m->writesize:-1); jout(b,",");
        jkeynum(b,"erasesize",m->have_erasesize?m->erasesize:-1); jout(b,",");
        jkeynum(b,"offset_bytes",m->have_offset?m->offset:-1); jout(b,",");
        jkeynum(b,"ecc_strength",m->have_ecc?m->ecc_strength:-1); jout(b,",");
        jkeynum(b,"ecc_failures",m->have_failures?m->ecc_failures:-1); jout(b,"}");
    }
    jout(b,"]},\"checks\":["); first=1;
    int machid_found=parse_cmdline_value(cmd,"machid",val,sizeof(val));
    const char *machid_text=val;
    if (machid_found > 0 && !strncmp(val,"0x",2)) machid_text=val+2;
    long machid=parse_long(machid_text,16,&ok);
    if (machid_found <= 0) ok=0;
    check_item(b,&first,"machid",!ok?"unknown":(machid==0x8040000?"consistent":"mismatch"),machid_found<0?"conflicting duplicate values":"8040000 hexadecimal expected");
    int skip_found=parse_cmdline_value(cmd,"cnss2.skip_radio_bmap",val,sizeof(val));
    check_item(b,&first,"cnss2.skip_radio_bmap",skip_found==1?"consistent":"unknown",skip_found<0?"启动参数存在冲突副本":(skip_found==0?"启动参数未提供":"读取到唯一参数值"));
    int any_nand=0, any_nor=0, geo4=0, geo_bad=0, geo_unknown=0;
    for(i=0;i<(int)nmtd;i++) {
        mtd *m=&mtds[i];
        if(has_ci(m->type,"nand")) { any_nand=1; if(m->have_writesize&&m->have_erasesize&&m->have_ecc) { if(m->writesize==2048&&m->erasesize==131072&&m->ecc_strength==4)geo4++; else geo_bad++; } else geo_unknown++; }
        if(has_ci(m->type,"nor")) any_nor=1;
    }
    check_item(b,&first,"flash_type",any_nor?"mismatch":(any_nand?"consistent":"unknown"),any_nor?"检测到 NOR；此检查配置要求 NAND":(any_nand?"检测到 NAND":"类型信息不可用"));
    const char *geo_state = geo_bad ? "mismatch" : ((!any_nand || geo_unknown || !geo4) ? "unknown" : "consistent");
    const char *geo_detail = geo_bad ? "NAND 几何或 ECC 与检查配置不符" : (geo_state[0] == 'c' ? "NAND 报告写入 2048、擦除 131072、ECC 4" : "缺少可读取的 NAND 几何或 ECC 信息");
    check_item(b,&first,"nand_geometry_ecc4",geo_state,geo_detail);
    const char *cn[]={"APPSBL","BOOTCONFIG","ART","system"}; int expected_counts[]={2,2,1,2};
    for(i=0;i<4;i++) { int count,bad; count_category(i,&count,&bad); char detail[128]; snprintf(detail,sizeof(detail),"观测到 %d 项，预期 %d 项且大小相符",count,expected_counts[i]);
        check_item(b,&first,cn[i],count==0?"unknown":(bad||count!=expected_counts[i]?"mismatch":"consistent"),detail); }
    jout(b,"]}");
}

static void write_all(int fd, const void *data, size_t len) {
    const char *p = (const char *)data;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        p += n; len -= (size_t)n;
    }
}
static void respond(int fd, int code, const char *reason, const char *type, const char *body, size_t len, int head) {
    char hdr[512]; int n=snprintf(hdr,sizeof(hdr),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nCache-Control: no-store\r\n\r\n",code,reason,type,len);
    if(n>0 && (size_t)n<sizeof(hdr)) write_all(fd,hdr,(size_t)n);
    if(!head&&body&&len) write_all(fd,body,len);
}
static int send_file(int fd, const char *path, int head) {
    struct stat st;
    int in=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(in<0)return 0;
    if(fstat(in,&st)<0||!S_ISREG(st.st_mode)||st.st_size<45LL*1024*1024||st.st_size>45LL*1024*1024+128*1024){close(in);return 0;}
    {unsigned char signature[4];if(pread(in,signature,sizeof(signature),0)!=4||memcmp(signature,"PK\003\004",4)){close(in);return 0;}}
    char hdr[512];
    int n=snprintf(hdr,sizeof(hdr),"HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: %lld\r\nContent-Disposition: attachment; filename=\"bootbridge-recovery-backup.zip\"\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nCache-Control: no-store\r\n\r\n",(long long)st.st_size);
    if(n<=0||(size_t)n>=sizeof(hdr)){close(in);return 0;}
    write_all(fd,hdr,(size_t)n);
    if(!head){
        char buf[32768]; off_t left=st.st_size;
        while(left>0){size_t want=left>(off_t)sizeof(buf)?sizeof(buf):(size_t)left;ssize_t got=read(in,buf,want);
            if(got<0&&errno==EINTR) continue;
            if(got<=0){close(in);return 1;} /* header already sent; terminate the truncated response */
            write_all(fd,buf,(size_t)got);left-=got;}
    }
    close(in);return 1;
}
static int unsafe_path(const char *p) {
    if(strstr(p,"..")||strchr(p,'\\')||strchr(p,'%')) return 1;
    return 0;
}
static int serve_asset(int fd,const char *file,const char *mime,int head) {
    char path[1024],buf[8192],hdr[512];struct stat st;
    int z=snprintf(path,sizeof(path),"%s/%s",web_dir,file); if(z<0||(size_t)z>=sizeof(path))return 0;
    int in=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(in<0)return 0;
    /* Assets are read-only regular files. Do not silently truncate CSS/JS at
       FILE_MAX (the separate proc observation bound), or allocate their size. */
    if(fstat(in,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>1024*1024){close(in);return 0;}
    z=snprintf(hdr,sizeof(hdr),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nCache-Control: no-store\r\n\r\n",mime,(long long)st.st_size);
    if(z<0||(size_t)z>=sizeof(hdr)){close(in);return 0;}write_all(fd,hdr,(size_t)z);
    if(!head){off_t remaining=st.st_size;while(remaining>0){size_t want=remaining>(off_t)sizeof(buf)?sizeof(buf):(size_t)remaining;ssize_t got=read(in,buf,want);if(got<0&&errno==EINTR)continue;if(got<=0)break;write_all(fd,buf,(size_t)got);remaining-=got;}}
    close(in);return 1;
}
static void bad_request(int fd,int code,const char *reason){respond(fd,code,reason,"text/plain","Request rejected\n",17,0);}
static int token_valid(const char *s) { size_t n=strlen(s);if(n!=32&&n!=64)return 0;for(size_t i=0;i<n;i++)if(!isxdigit((unsigned char)s[i]))return 0;return 1; }
static int parse_token_body(char *body,size_t n,char token[65]) {
    if(n<7||n>=96||memcmp(body,"token=",6)!=0)return 0;
    size_t len=n-6;if(len>=65)return 0;memcpy(token,body+6,len);token[len]=0;return token_valid(token);
}
static int parse_return_body(char *body,size_t n,char token[65]) {
    static const char suffix[]="&confirm=return-oem";
    if(n<=sizeof(suffix)-1||n>=128||memcmp(body,"token=",6)!=0||
       memcmp(body+n-(sizeof(suffix)-1),suffix,sizeof(suffix)-1)!=0)return 0;
    size_t token_len=n-6-(sizeof(suffix)-1);
    if(token_len>=65)return 0;
    memcpy(token,body+6,token_len);token[token_len]=0;
    return token_valid(token);
}
static int parse_decimal_length(const char *v,size_t *out) {
    if(!*v)return 0;
    size_t n=0;
    for(;*v;v++){if(*v<'0'||*v>'9'||n>128)return 0;n=n*10+(size_t)(*v-'0');}
    if(n>128)return 0;
    *out=n;return 1;
}
static void trim_value(char *v){size_t n=strlen(v);while(n&&(v[n-1]==' '||v[n-1]=='\t'))v[--n]=0;while(*v==' '||*v=='\t')memmove(v,v+1,strlen(v));}
static int host_allowed(const char *host) {
    if(!strcmp(bind_addr,"192.168.10.1")&&listen_port==80)
        return !strcmp(host,"192.168.10.1")||!strcmp(host,"192.168.10.1:80");
    if(fixture_override&&!strcmp(bind_addr,"127.0.0.1")){
        char expected[64];int n=snprintf(expected,sizeof(expected),"127.0.0.1:%d",listen_port);
        return n>0&&(size_t)n<sizeof(expected)&&!strcmp(host,expected);
    }
    return 0;
}
static int same_origin(const char *host,const char *origin) {
    char expected[320];int n=snprintf(expected,sizeof(expected),"http://%s",host);
    return host_allowed(host)&&n>0&&(size_t)n<sizeof(expected)&&strcasecmp(expected,origin)==0;
}
static int valid_header_name(const char *s){if(!*s)return 0;for(;*s;s++)if(!(isalnum((unsigned char)*s)||strchr("!#$%&'*+-.^_`|~",*s)))return 0;return 1;}
static void client(int fd) {
    char req[REQ_MAX+1];size_t used=0,head_end=0,body_len=0;int complete=0,have_cl=0,have_host=0,have_origin=0,have_type=0,have_te=0;
    char method[16],path[256],host[256]={0},origin[320]={0},content_type[96]={0};size_t content_length=0;
    struct timeval rtimeout={1,0},stimeout={60,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&rtimeout,sizeof(rtimeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&stimeout,sizeof(stimeout));
    while(used<REQ_MAX){ssize_t n=recv(fd,req+used,REQ_MAX-used,0);if(n<0&&errno==EINTR)continue;if(n<=0)break;if(memchr(req+used,0,(size_t)n)){bad_request(fd,400,"Bad Request");close(fd);return;}used+=(size_t)n;req[used]=0;char *sep=strstr(req,"\r\n\r\n");if(sep){head_end=(size_t)(sep-req)+4;complete=1;break;}}
    if(!complete){bad_request(fd,used==REQ_MAX?431:400,used==REQ_MAX?"Request Header Fields Too Large":"Bad Request");close(fd);return;}
    char *line_end=strstr(req,"\r\n");if(!line_end){bad_request(fd,400,"Bad Request");close(fd);return;}*line_end=0;
    char *sp1=strchr(req,' '),*sp2=sp1?strchr(sp1+1,' '):NULL;if(!sp1||!sp2||strchr(sp2+1,' ')||sp1==req||sp2==sp1+1||!sp2[1]){bad_request(fd,400,"Bad Request");close(fd);return;}
    size_t ml=(size_t)(sp1-req),pl=(size_t)(sp2-sp1-1),vl=strlen(sp2+1);if(pl>=sizeof(path)){respond(fd,414,"URI Too Long","text/plain",NULL,0,0);close(fd);return;}if(ml>=sizeof(method)||vl!=8||(strcmp(sp2+1,"HTTP/1.0")&&strcmp(sp2+1,"HTTP/1.1"))){bad_request(fd,400,"Bad Request");close(fd);return;}
    for(size_t i=0;i<ml;i++)if(!isupper((unsigned char)req[i])){bad_request(fd,400,"Bad Request");close(fd);return;}
    memcpy(method,req,ml);method[ml]=0;memcpy(path,sp1+1,pl);path[pl]=0;
    size_t pos=(size_t)(line_end-req)+2;
    while(pos+2<=head_end){char *e=strstr(req+pos,"\r\n");if(!e)break;if(e==req+pos)break;*e=0;char *colon=strchr(req+pos,':');if(!colon||colon==req+pos){bad_request(fd,400,"Bad Request");close(fd);return;}*colon=0;if(!valid_header_name(req+pos)){bad_request(fd,400,"Bad Request");close(fd);return;}char *v=colon+1;trim_value(v);for(const unsigned char *q=(const unsigned char*)v;*q;q++)if((*q<0x20&&*q!='\t')||*q==0x7f){bad_request(fd,400,"Bad Request");close(fd);return;}
        if(!strcasecmp(req+pos,"Host")){if(have_host++||!*v||strlen(v)>=sizeof(host)||strchr(v,',')||strchr(v,'@')){bad_request(fd,400,"Bad Request");close(fd);return;}strcpy(host,v);}
        else if(!strcasecmp(req+pos,"Origin")){if(have_origin++||strlen(v)>=sizeof(origin)){bad_request(fd,400,"Bad Request");close(fd);return;}strcpy(origin,v);}
        else if(!strcasecmp(req+pos,"Content-Length")){if(have_cl++||!parse_decimal_length(v,&content_length)){bad_request(fd,400,"Bad Request");close(fd);return;}}
        else if(!strcasecmp(req+pos,"Transfer-Encoding")){have_te=1;}
        else if(!strcasecmp(req+pos,"Content-Type")){if(have_type++||strlen(v)>=sizeof(content_type)){bad_request(fd,400,"Bad Request");close(fd);return;}strcpy(content_type,v);}
        pos=(size_t)(e-req)+2;
    }
    if(have_te||have_host>1||(!strcmp(sp2+1,"HTTP/1.1")&&!have_host)){bad_request(fd,400,"Bad Request");close(fd);return;}
    size_t initial=used-head_end;if(initial>sizeof(req)-head_end){bad_request(fd,400,"Bad Request");close(fd);return;}
    int is_post=!strcmp(method,"POST"),is_get=!strcmp(method,"GET"),is_head=!strcmp(method,"HEAD");
    if(!is_post&&!is_get&&!is_head){respond(fd,405,"Method Not Allowed","text/plain","Method Not Allowed\n",19,0);close(fd);return;}
    if(is_post&&strcmp(path,"/api/install")&&strcmp(path,"/api/reboot")&&strcmp(path,"/api/return-oem")&&strcmp(path,"/api/reboot-oem")){
        if(!strcmp(path,"/api/check")||!strcmp(path,"/api/status")||!strcmp(path,"/report.json"))respond(fd,405,"Method Not Allowed","text/plain","Method Not Allowed\n",19,0);
        else respond(fd,404,"Not Found","text/plain","Not Found\n",10,0);
        close(fd);return;
    }
    if(is_post){
        if(!have_origin||!have_host||!same_origin(host,origin)){bad_request(fd,403,"Forbidden");close(fd);return;}
        if(!have_cl||content_length==0||content_length>128||have_type!=1||strcasecmp(content_type,"application/x-www-form-urlencoded")){bad_request(fd,400,"Bad Request");close(fd);return;}
    }
    else if((have_cl&&content_length!=0)||initial!=0){bad_request(fd,400,"Bad Request");close(fd);return;}
    char body[129];if(is_post){if(initial>content_length){bad_request(fd,400,"Bad Request");close(fd);return;}body_len=initial;if(body_len)memcpy(body,req+head_end,body_len);while(body_len<content_length){ssize_t n=recv(fd,body+body_len,content_length-body_len,0);if(n<0&&errno==EINTR)continue;if(n<=0){bad_request(fd,400,"Bad Request");close(fd);return;}body_len+=(size_t)n;}body[body_len]=0;}
    if(unsafe_path(path)){bad_request(fd,400,"Bad Request");close(fd);return;}char *query=strchr(path,'?');int has_query=query!=NULL;if(query)*query=0;
    if(is_post){if(has_query){respond(fd,404,"Not Found","text/plain","Not Found\n",10,0);close(fd);return;}
        char token[65],error[256]={0};
        int parsed=!strcmp(path,"/api/return-oem")?parse_return_body(body,body_len,token):parse_token_body(body,body_len,token);
        if(!parsed){bad_request(fd,400,"Bad Request");close(fd);return;}
        int rc;
        if(!strcmp(path,"/api/install"))rc=bi_start(token,error,sizeof(error));
        else if(!strcmp(path,"/api/return-oem"))rc=bi_oem_return_start(token,error,sizeof(error));
        else if(!strcmp(path,"/api/reboot-oem"))rc=bi_oem_reboot_start(token,error,sizeof(error));
        else rc=bi_reboot_start(token,error,sizeof(error));
        if(rc==0)respond(fd,202,"Accepted","application/json; charset=utf-8","{\"accepted\":true}",17,0);
        else {const char *msg="{\"accepted\":false,\"error\":\"request rejected\"}";respond(fd,409,"Conflict","application/json; charset=utf-8",msg,strlen(msg),0);}close(fd);return;
    }
    int head=is_head;
    if(!strcmp(path,"/api/status")&&(is_get||is_head)){char status[JSON_MAX];if(bi_status_json(status,sizeof(status))!=0)respond(fd,500,"Internal Server Error","application/json; charset=utf-8","{}",2,head);else respond(fd,200,"OK","application/json; charset=utf-8",status,strlen(status),head);}
    else if(!strcmp(path,"/backup.zip")&&(is_get||is_head)){char backup[1024];int avail=bi_backup_path(backup,sizeof(backup));if(avail==1){if(!send_file(fd,backup,head))respond(fd,404,"Not Found","text/plain","Not Found\n",10,head);}else respond(fd,404,"Not Found","text/plain","Not Found\n",10,head);}
    else if(!strcmp(path,"/")||!strcmp(path,"/index.html")||
       !strcmp(path,"/mobile.asp")||!strcmp(path,"/router_password_mobile.asp")||!strcmp(path,"/index.asp")||
       !strcmp(path,"/login.asp")||!strcmp(path,"/device_firmware_upgrade.asp")||
       !strcmp(path,"/device_fireware_progress.asp")||!strcmp(path,"/do_cmd.asp")) {
        if(!serve_asset(fd,"index.html","text/html; charset=utf-8",head))respond(fd,404,"Not Found","text/plain",NULL,0,head);
    }
    else if(!strcmp(path,"/style.css")) { if(!serve_asset(fd,"style.css","text/css; charset=utf-8",head))respond(fd,404,"Not Found","text/plain",NULL,0,head); }
    else if(!strcmp(path,"/main.js")) { if(!serve_asset(fd,"main.js","application/javascript; charset=utf-8",head))respond(fd,404,"Not Found","text/plain",NULL,0,head); }
    else if((!strcmp(path,"/api/check")||!strcmp(path,"/report.json"))&&(is_get||is_head)){char core[JSON_MAX]={0};int ready=0;if(bi_status_json(core,sizeof(core))==0){char *p=strstr(core,"\"install_ready\"");if(p){p=strchr(p,':');if(p){++p;while(*p==' '||*p=='\t')++p;ready=!strncmp(p,"true",4);}}}jbuf b={{0},0,0};build_report(&b,ready);if(!b.failed&&b.len&&b.data[b.len-1]=='}'){b.data[--b.len]=0;jout(&b,",\"wizard\":");jout(&b,core[0]?core:"{}");jout(&b,"}");}if(b.failed)respond(fd,500,"Internal Server Error","application/json","{}",2,head);else respond(fd,200,"OK","application/json; charset=utf-8",b.data,b.len,head);}
    else if(!strcmp(path,"/api/check")&&!is_get&&!is_head){respond(fd,405,"Method Not Allowed","text/plain","Method Not Allowed\n",19,head);}
    else respond(fd,404,"Not Found","text/plain","Not Found\n",10,head);
    close(fd);
}
static int parse_port(const char *s) { char *e; long v=strtol(s,&e,10); if(!*s||*e||v<1||v>65535)return -1; return (int)v; }
int main(int argc,char **argv) {
    int opt;
    if(argc==2&&!strcmp(argv[1],"--bridge-dispatch-inspect")){
        char result[8192];
        bi_set_inspection_mode(1);
        bi_init(NULL);
        if(bi_dispatch_inspect_json(result,sizeof(result))!=0||strlen(result)>8192)return 2;
        if(fwrite(result,1,strlen(result),stdout)!=strlen(result)||fflush(stdout))return 2;
        return strstr(result,"\"install_ready\":true")?0:1;
    }
    const char *dispatch_code=NULL,*dispatch_reason=NULL,*dispatch_candidate_revision=NULL,*execution_detail="";
    if((argc==12||argc==14)&&!strcmp(argv[1],"--dispatch-reason")&&getenv("BRIDGE_RETURN_FALLBACK")&&
       !strcmp(getenv("BRIDGE_RETURN_FALLBACK"),"1")){
        dispatch_code=argv[2];dispatch_reason=argv[3];dispatch_candidate_revision=argv[5];
        if(!*dispatch_code||strlen(dispatch_code)>=65||strlen(dispatch_reason)>=257)return 2;
        for(const unsigned char*p=(const unsigned char*)dispatch_code;*p;p++)
            if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return 2;
        for(const unsigned char*p=(const unsigned char*)dispatch_reason;*p;p++)if(*p<0x20||*p==0x7f)return 2;
        if(strcmp(argv[4],"--dispatch-candidate-source-revision")||
           (*dispatch_candidate_revision&&strlen(dispatch_candidate_revision)!=64))return 2;
        for(const char*p=dispatch_candidate_revision;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')))return 2;
        int first_option=6;
        if(argc==14){
            if(strcmp(argv[6],"--dispatch-execution-detail")||strlen(argv[7])>=2048)return 2;
            execution_detail=argv[7];
            for(const unsigned char*p=(const unsigned char*)execution_detail;*p;p++)if(*p<0x20u||*p>0x7eu)return 2;
            first_option=8;
        }
        if(strcmp(argv[first_option],"-b")||strcmp(argv[first_option+1],"192.168.10.1")||strcmp(argv[first_option+2],"-p")||
           strcmp(argv[first_option+3],"80")||strcmp(argv[first_option+4],"-w")||strcmp(argv[first_option+5],"www"))return 2;
        optind=first_option;
    }
    while((opt=getopt(argc,argv,"r:b:p:w:h"))!=-1) {
        switch(opt) {
        case 'r': if(strlen(optarg)>=sizeof(fixture_root))return 2; snprintf(fixture_root,sizeof(fixture_root),"%s",optarg);fixture_override=1; break;
        case 'b': if(strlen(optarg)>=sizeof(bind_addr))return 2; snprintf(bind_addr,sizeof(bind_addr),"%s",optarg); break;
        case 'p': listen_port=parse_port(optarg); if(listen_port<0)return 2; break;
        case 'w': if(strlen(optarg)>=sizeof(web_dir))return 2; snprintf(web_dir,sizeof(web_dir),"%s",optarg); break;
        case 'h': printf("usage: %s [-r fixture-root] [-b bind-address] [-p port] [-w webdir]\n",argv[0]); return 0;
        default: return 2;
        }
    }
    if(strcmp(bind_addr,"192.168.10.1")&&
       (strcmp(bind_addr,"127.0.0.1")||!fixture_override)){
        fprintf(stderr,"only the maintenance LAN or fixture loopback bind address is allowed\n");return 2;
    }
    if(!dispatch_code){const char *detail=getenv("BRIDGE_DISPATCH_EXECUTION_DETAIL");
        if(detail&&bi_set_dispatch_execution_detail(detail))return 2;}
    if(dispatch_code){bi_set_dispatch_diagnostic(dispatch_code,dispatch_reason,dispatch_candidate_revision);
        if(bi_set_dispatch_execution_detail(execution_detail))return 2;}
    bi_init(fixture_override ? fixture_root : NULL);
    signal(SIGPIPE,SIG_IGN);
    int s=socket(AF_INET,SOCK_STREAM,0); if(s<0){perror("socket");return 1;}
    int one=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((unsigned short)listen_port);
    if(inet_pton(AF_INET,bind_addr,&a.sin_addr)!=1){fprintf(stderr,"invalid IPv4 bind address\n");close(s);return 2;}
    if(bind(s,(struct sockaddr*)&a,sizeof(a))<0||listen(s,8)<0){perror("bind/listen");close(s);return 1;}
    for(;;){
        bi_reap();
        fd_set rfds;FD_ZERO(&rfds);FD_SET(s,&rfds);struct timeval tick={1,0};
        int ready=select(s+1,&rfds,NULL,NULL,&tick);
        if(ready<0){if(errno==EINTR)continue;perror("select");break;}
        if(ready==0)continue;
        int c=accept(s,NULL,NULL);if(c<0){if(errno==EINTR)continue;perror("accept");break;}client(c);
    }
    close(s); return 0;
}
