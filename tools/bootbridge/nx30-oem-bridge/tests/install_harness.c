#define _POSIX_C_SOURCE 200809L
#include "../src/bridge_install.h"
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned poll_iterations=6000; /* 60 seconds per bounded stage */
static int status(char *b,size_t n){return bi_status_json(b,n);}
static void pause_ms(long ms){struct timespec t={ms/1000,(ms%1000)*1000000L};nanosleep(&t,NULL);}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const char*budget=getenv("BRIDGE_TEST_TIMEOUT_SECONDS");
    if(budget&&*budget){char*end=NULL;errno=0;unsigned long seconds=strtoul(budget,&end,10);if(errno||end==budget||*end||seconds<1||seconds>180)return 38;poll_iterations=(unsigned)seconds*100u;}

    const char *root=argv[1],*mode=argv[2];char b[16384],token[80],err[256];
    if(!strcmp(mode,"dispatch_status"))bi_set_dispatch_diagnostic("candidate_unavailable",
        "provider rejected its own contract","0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    if(!strcmp(mode,"dispatch_status_detail")){
        bi_set_dispatch_diagnostic("provider_exec_failed",
            "inspector timed out; see diagnostic",
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        if(bi_set_dispatch_execution_detail("tool_revision=R6-preflight-v1;outcome=timeout;elapsed_ms=180000;last_stage=nand;note=escaped \"quote\""))return 40;
        if(bi_set_dispatch_execution_detail("bad\nline")==0)return 41;
    }
    if(!strcmp(mode,"inspect"))bi_set_inspection_mode(1);
    bi_init(root);if(status(b,sizeof(b)))return 3;
    if(!strcmp(mode,"inspect")){if(bi_dispatch_inspect_json(b,sizeof(b)))return 39;puts(b);return 0;}
    if(!strcmp(mode,"status")){puts(b);return 0;}
    if(!strcmp(mode,"dispatch_status")||!strcmp(mode,"dispatch_status_detail")){puts(b);return 0;}
    const char *p=strstr(b,"\"token\":\"");if(!p)return 4;p+=9;const char*e=strchr(p,'"');if(!e||(size_t)(e-p)>=sizeof(token))return 5;memcpy(token,p,(size_t)(e-p));token[e-p]=0;
    if(!strcmp(mode,"idempotent_reboot")){
        puts(b);if(bi_oem_reboot_start(token,err,sizeof(err))){fprintf(stderr,"reboot: %s\n",err);return 14;}
        for(unsigned i=0;i<poll_iterations;i++){if(status(b,sizeof(b)))return 15;if(strstr(b,"reboot_simulated"))break;bi_reap();pause_ms(10);}
        puts(b);return 0;
    }
    if(!strcmp(mode,"generic_reboot")){int rc=bi_reboot_start(token,err,sizeof(err));printf("generic_reboot_rc=%d\n",rc);puts(err);return rc==0?25:0;}
    if(!strcmp(mode,"reboot_before")){int rc=bi_oem_reboot_start(token,err,sizeof(err));printf("reboot_rc=%d\n",rc);puts(err);return rc==0?6:0;}
    if(!strcmp(mode,"stale")){
        char path[4096];snprintf(path,sizeof(path),"%s/dev/mtd5",root);FILE*f=fopen(path,"r+b");if(!f)return 7;if(fseek(f,32,SEEK_SET)||fputc(0x11,f)==EOF||fclose(f))return 8;
    }
    if(!strcmp(mode,"stale_map")){
        char path[4096];snprintf(path,sizeof(path),"%s/sys/class/mtd/mtd6/dev",root);FILE*f=fopen(path,"w");if(!f)return 16;if(fputs("91:6\n",f)<0||fclose(f))return 17;
    }
#ifdef BRIDGE_INSTALL_TEST
    if(!strcmp(mode,"stale_active_version")){
        /* Synthetic fault injection after preflight, before commit. This write
         * belongs to the harness, never to the production installer. */
        char source[4096],target[4096];unsigned char block[4096];size_t n;
        snprintf(source,sizeof(source),"%s/etc/bridge/test-newer-appsbl.bin",root);
        snprintf(target,sizeof(target),"%s/dev/mtd7",root);
        FILE *in=fopen(source,"rb"),*out=fopen(target,"wb");
        if(!in||!out)return 20;
        while((n=fread(block,1,sizeof(block),in))!=0)if(fwrite(block,1,n,out)!=n)return 21;
        int bad=ferror(in);if(fclose(in))bad=1;if(fclose(out))bad=1;if(bad)return 22;
    }
#endif
    if(!strcmp(mode,"nand_noise_recheck")||!strcmp(mode,"nand_identity_recheck")){
        char path[4096];snprintf(path,sizeof(path),"%s/proc/nand-probe-log",root);FILE*f=fopen(path,"a");if(!f)return 18;
        const char *noise="[    1.234567] bridge: ordinary runtime message\n";
        const char *conflict="[    1.234568] nand: device found, Manufacturer ID: 0xc8, Chip ID: 0x41\n";
        int rc=fputs(!strcmp(mode,"nand_noise_recheck")?noise:conflict,f)<0||fclose(f);if(rc)return 19;
    }
    if(!strcmp(mode,"stale_oem")||!strcmp(mode,"stale_oem_map")||!strcmp(mode,"stale_root_witness")){
        char path[4096];const char*rel=!strcmp(mode,"stale_oem")?"dev/mtd15":!strcmp(mode,"stale_oem_map")?"sys/class/mtd/mtd15/offset":"sys/class/ubi/ubi0/mtd_num";
        snprintf(path,sizeof(path),"%s/%s",root,rel);FILE*f=fopen(path,!strcmp(mode,"stale_oem")?"r+b":"w");if(!f)return 26;
        if(!strcmp(mode,"stale_oem")){if(fseek(f,5*131072+4096,SEEK_SET)||fputc(0x13,f)==EOF)return 27;}
        else if(fputs(!strcmp(mode,"stale_oem_map")?"0xa00000\n":"15\n",f)<0)return 28;
        if(fclose(f))return 29;
    }
    int returning=!strncmp(mode,"return",6);
    if(returning?bi_oem_return_start(token,err,sizeof(err)):bi_start(token,err,sizeof(err))){fprintf(stderr,"start: %s\n",err);return 9;}
    int terminal=0,continued=0;

    for(unsigned i=0;i<poll_iterations;i++){
        if(status(b,sizeof(b)))return 10;
        if(!continued&&(!strcmp(mode,"pause_before_oem_change")||!strcmp(mode,"pause_after_bc1_oem_change"))&&
           (strstr(b,"oem_before_commit_gate")||strstr(b,"oem_after_backup_gate"))){
            int stopped;pid_t child=waitpid(-1,&stopped,WUNTRACED);if(child<=0||!WIFSTOPPED(stopped))return 30;
            char path[4096];snprintf(path,sizeof(path),"%s/dev/mtd15",root);FILE*f=fopen(path,"r+b");if(!f||fseek(f,5*131072+4096,SEEK_SET)||fputc(0x13,f)==EOF||fclose(f))return 31;
            if(kill(child,SIGCONT))return 32;
            continued=1;
        }
        if(strstr(b,"\"phase\":\"failed\"")||strstr(b,"\"phase\":\"blocked\"")||
           (strstr(b,"\"phase\":\"complete\"")&&(!returning||strstr(b,"oem_return_reboot_simulated")))){terminal=1;break;}
        bi_reap();pause_ms(10);
    }
    if(!terminal){fprintf(stderr,"install timeout: %s\n",b);return 11;}
    puts(b);
    if(returning){int rc=bi_oem_return_start(token,err,sizeof(err));printf("return_replay_rc=%d\n",rc);return rc==0?33:0;}
    if(!strcmp(mode,"pre_reboot_oem_change")||!strcmp(mode,"pre_reboot_bc_change")){
        char path[4096];snprintf(path,sizeof(path),"%s/dev/mtd%s",root,!strcmp(mode,"pre_reboot_oem_change")?"15":"2");FILE*f=fopen(path,"r+b");if(!f)return 34;
        if(fseek(f,!strcmp(mode,"pre_reboot_oem_change")?5*131072+4096:4,SEEK_SET)||fputc(0x13,f)==EOF||fclose(f))return 35;
        int rc=bi_oem_reboot_start(token,err,sizeof(err));printf("oem_reboot_rc=%d\n",rc);if(status(b,sizeof(b)))return 36;puts(b);return rc==0?37:0;
    }
    if(!strcmp(mode,"replay")||!strcmp(mode,"stale_active_version")){int rc=bi_start(token,err,sizeof(err));printf("replay_rc=%d\n",rc);}
    if((!strcmp(mode,"install")||!strcmp(mode,"replay"))&&strstr(b,"\"phase\":\"complete\"")){
        int rc=bi_oem_reboot_start(token,err,sizeof(err));printf("reboot_rc=%d\n",rc);if(rc)return 12;
        for(unsigned i=0;i<poll_iterations;i++){if(status(b,sizeof(b)))return 13;if(strstr(b,"reboot_simulated"))break;bi_reap();pause_ms(10);}
        puts(b);
    }
    return 0;
}
