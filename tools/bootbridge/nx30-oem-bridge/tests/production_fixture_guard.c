#include "../src/bridge_install.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc,char **argv){char b[16384],dir[160],path[192];if(argc!=2)return 2;bi_init(argv[1]);if(bi_status_json(b,sizeof(b)))return 3;puts(b);const char*p=strstr(b,"\"session\":\"");if(p){p+=11;const char*e=strchr(p,'"');if(e&&(size_t)(e-p)<64){snprintf(dir,sizeof(dir),"/tmp/nx30-bridge-install-%.*s",(int)(e-p),p);snprintf(path,sizeof(path),"%s/status.json",dir);unlink(path);rmdir(dir);}}return strstr(b,"\"phase\":\"blocked\"")&&strstr(b,"拒绝 fixture_root")?0:4;}
