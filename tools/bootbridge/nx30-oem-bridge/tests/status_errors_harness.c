#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <unistd.h>
static int force_reboot_exit_failure;
static void test_exit(int code) __attribute__((noreturn));
#define _exit test_exit
#include "../src/bridge_install.c"
#undef _exit
static void test_exit(int code){_Exit(force_reboot_exit_failure ? 1 : code);}
static unsigned poll_iterations=6000;
static void poll_worker(void){for(unsigned i=0;i<poll_iterations&&g_child>0;i++){bi_reap();struct timespec t={0,10000000};nanosleep(&t,NULL);}}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const char*budget=getenv("BRIDGE_TEST_TIMEOUT_SECONDS");
    if(budget&&*budget){char*end=NULL;errno=0;unsigned long seconds=strtoul(budget,&end,10);if(errno||end==budget||*end||seconds<1||seconds>180)return 38;poll_iterations=(unsigned)seconds*100u;}
    char state[STATUS_CAP],error[256];bi_init(argv[1]);
    if(strcmp(g_phase,"ready")&&strcmp(g_phase,"complete"))return 3;
    if(!strcmp(argv[2],"accepted_failure")){
        char failpath[PATH_MAX];if(snprintf(failpath,sizeof(failpath),"%s.new",g_status_file)>=(int)sizeof(failpath)||mkdir(failpath,0700))return 4;
        int rc=bi_start(g_token,error,sizeof(error));printf("start_rc=%d\n",rc);
        if(bi_status_json(state,sizeof(state)))return 5;
    puts(state);
        if(rmdir(failpath))return 6;
        if(bi_status_json(state,sizeof(state)))return 7;
    puts(state);
        printf("replay_rc=%d\n",bi_start(g_token,error,sizeof(error)));
        printf("reboot_rc=%d\n",bi_oem_reboot_start(g_token,error,sizeof(error)));
        return 0;
    }
    if(!strcmp(g_phase,"ready")){if(bi_start(g_token,error,sizeof(error)))return 8;poll_worker();if(strcmp(g_phase,"complete"))return 9;}
    if(bi_status_json(state,sizeof(state)))return 10;
    puts(state);
    force_reboot_exit_failure=1;
    if(bi_oem_reboot_start(g_token,error,sizeof(error)))return 11;
    printf("repeat_reboot_rc=%d\n",bi_oem_reboot_start(g_token,error,sizeof(error)));
    poll_worker();if(bi_status_json(state,sizeof(state)))return 12;
    puts(state);
    printf("reinstall_rc=%d\n",bi_start(g_token,error,sizeof(error)));
    printf("retry_reboot_rc=%d\n",bi_oem_reboot_start(g_token,error,sizeof(error)));
    return 0;
}
