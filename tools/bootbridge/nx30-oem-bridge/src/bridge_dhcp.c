#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Small, RAM-only DHCP service for the isolated NX30 maintenance LAN. */
#define DHCP_PORT 67
#define CLIENT_PORT 68
#define DHCP_FIXED 240
#define DHCP_PACKET_MAX 1500
#define POOL_FIRST 100
#define POOL_COUNT 50
#define LEASE_SECONDS 1800
#define OFFER_SECONDS 60

enum { DHCP_DISCOVER=1, DHCP_OFFER=2, DHCP_REQUEST=3, DHCP_DECLINE=4,
       DHCP_ACK=5, DHCP_NAK=6, DHCP_RELEASE=7, DHCP_INFORM=8 };
enum { LEASE_FREE=0, LEASE_OFFERED=1, LEASE_BOUND=2 };

typedef struct {
    unsigned char state, mac[6];
    time_t lease_until, offer_until;
} lease;
typedef struct {
    int have_type, have_server, have_requested;
    unsigned char type;
    unsigned char server[4], requested[4];
} options;
static lease leases[POOL_COUNT];
static const unsigned char server_ip[4]={192,168,10,1};
static const unsigned char netmask[4]={255,255,255,0};

static int same_mac(const unsigned char *a,const unsigned char *b){return memcmp(a,b,6)==0;}
static int ip_in_pool(const unsigned char ip[4]){
    return ip[0]==192&&ip[1]==168&&ip[2]==10&&ip[3]>=POOL_FIRST&&ip[3]<POOL_FIRST+POOL_COUNT;
}
static void pool_ip(int slot,unsigned char ip[4]){ip[0]=192;ip[1]=168;ip[2]=10;ip[3]=(unsigned char)(POOL_FIRST+slot);}
static int slot_for_ip(const unsigned char ip[4]){return ip_in_pool(ip)?(int)ip[3]-POOL_FIRST:-1;}
static int option_seen_set(int *seen,const unsigned char *old,const unsigned char *now,size_t n){
    if(*seen)return memcmp(old,now,n)==0;
    *seen=1;return 1;
}
static int parse_options(const unsigned char *p,size_t n,options *o){
    size_t i=DHCP_FIXED; memset(o,0,sizeof(*o));
    while(i<n){unsigned code=p[i++]; if(code==0)continue; if(code==255)return 1; if(i>=n)return 0;
        size_t len=p[i++]; if(len>n-i)return 0;
        if(code==53){if(len!=1||!option_seen_set(&o->have_type,&o->type,p+i,1))return 0;o->type=p[i];}
        else if(code==54){if(len!=4||!option_seen_set(&o->have_server,o->server,p+i,4))return 0;memcpy(o->server,p+i,4);}
        else if(code==50){if(len!=4||!option_seen_set(&o->have_requested,o->requested,p+i,4))return 0;memcpy(o->requested,p+i,4);}
        i+=len;
    }
    return 0; /* DHCP option stream must include END. */
}
static int request_valid(const unsigned char *p,size_t n,options *o){
    static const unsigned char cookie[4]={99,130,83,99};
    if(n<DHCP_FIXED||p[0]!=1||p[1]!=1||p[2]!=6||memcmp(p+236,cookie,4)!=0)return 0;
    if(p[24]||p[25]||p[26]||p[27])return 0; /* No DHCP relay support. */
    if(!parse_options(p,n,o)||!o->have_type)return 0;
    if(o->type!=DHCP_DISCOVER&&o->type!=DHCP_REQUEST)return 0;
    if(p[12]||p[13]||p[14]||p[15])return 0; /* Initial allocation only. */
    return 1;
}
static void sweep_expired(time_t now){
    for(int i=0;i<POOL_COUNT;i++){
        lease *l=&leases[i];
        if(l->state==LEASE_OFFERED&&l->offer_until<=now)memset(l,0,sizeof(*l));
        else if(l->state==LEASE_BOUND){
            if(l->lease_until<=now&&l->offer_until>now){l->state=LEASE_OFFERED;l->lease_until=0;}
            else if(l->lease_until<=now)memset(l,0,sizeof(*l));
            else if(l->offer_until<=now)l->offer_until=0;
        }
    }
}
static int lease_for_mac(const unsigned char *mac,time_t now){
    sweep_expired(now);
    for(int i=0;i<POOL_COUNT;i++)if(leases[i].state!=LEASE_FREE&&same_mac(leases[i].mac,mac))return i;
    return -1;
}
static int allocate_slot(const unsigned char *mac,const unsigned char *hint,time_t now){
    int slot=lease_for_mac(mac,now);
    if(slot>=0){
        leases[slot].offer_until=now+OFFER_SECONDS;
        if(leases[slot].state!=LEASE_BOUND)leases[slot].state=LEASE_OFFERED;
        return slot;
    }
    if(hint&&ip_in_pool(hint)){
        int requested=slot_for_ip(hint);
        if(leases[requested].state==LEASE_FREE){slot=requested;goto reserve;}
    }
    for(int i=0;i<POOL_COUNT;i++)if(leases[i].state==LEASE_FREE){slot=i;goto reserve;}
    return -1;
reserve:
    leases[slot].state=LEASE_OFFERED;memcpy(leases[slot].mac,mac,6);leases[slot].offer_until=now+OFFER_SECONDS;
    return slot;
}
static size_t append_option(unsigned char *out,size_t at,unsigned code,const unsigned char *v,size_t len){
    if(at+2+len>DHCP_PACKET_MAX)return 0;
    out[at++]=(unsigned char)code;out[at++]=(unsigned char)len;memcpy(out+at,v,len);return at+len;
}
static size_t make_reply(const unsigned char *request,size_t request_len,unsigned type,const unsigned char yiaddr[4],unsigned char *out){
    (void)request_len;memset(out,0,DHCP_PACKET_MAX);out[0]=2;out[1]=1;out[2]=6;
    memcpy(out+4,request+4,4);memcpy(out+10,request+10,2);memcpy(out+16,yiaddr,4);memcpy(out+20,server_ip,4);memcpy(out+28,request+28,16);
    out[236]=99;out[237]=130;out[238]=83;out[239]=99;
    size_t at=240;unsigned char t=(unsigned char)type;unsigned char lease_time[4]={0,0,7,8};
    at=append_option(out,at,53,&t,1);if(!at)return 0;
    at=append_option(out,at,54,server_ip,4);if(!at)return 0;
    if(type==DHCP_OFFER||type==DHCP_ACK){
        at=append_option(out,at,51,lease_time,4);if(!at)return 0;
        at=append_option(out,at,1,netmask,4);if(!at)return 0;
    }
    if(at>=DHCP_PACKET_MAX)return 0;
    out[at++]=255;return at;
}
/* Returns 1 with reply, 0 when ignored/malformed, -1 when internal capacity is exhausted. */
static int process_packet(const unsigned char *in,size_t in_len,unsigned char *out,size_t *out_len,time_t now){
    options o;int slot;unsigned char zero[4]={0,0,0,0};
    *out_len=0;if(!request_valid(in,in_len,&o))return 0;
    if(o.have_server&&memcmp(o.server,server_ip,4)!=0)return 0;
    const unsigned char *mac=in+28;
    if(o.type==DHCP_DISCOVER){
        slot=allocate_slot(mac,o.have_requested?o.requested:NULL,now);if(slot<0)return -1;
        unsigned char yi[4];pool_ip(slot,yi);*out_len=make_reply(in,in_len,DHCP_OFFER,yi,out);return *out_len?1:-1;
    }
    if(!o.have_requested){return 0;}
    if(!ip_in_pool(o.requested)){
        *out_len=make_reply(in,in_len,DHCP_NAK,zero,out);return *out_len?1:-1;
    }
    slot=lease_for_mac(mac,now);
    if(slot<0||slot!=slot_for_ip(o.requested)||
       !((leases[slot].state==LEASE_OFFERED&&leases[slot].offer_until>now)||
         (leases[slot].state==LEASE_BOUND&&(leases[slot].offer_until>now||leases[slot].lease_until>now)))){
        *out_len=make_reply(in,in_len,DHCP_NAK,zero,out);return *out_len?1:-1;
    }
    leases[slot].state=LEASE_BOUND;leases[slot].lease_until=now+LEASE_SECONDS;leases[slot].offer_until=0;
    *out_len=make_reply(in,in_len,DHCP_ACK,o.requested,out);return *out_len?1:-1;
}
static time_t monotonic_seconds(void){
    struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)<0)return 0;return ts.tv_sec;
}
static int response_option(const unsigned char *p,size_t n,unsigned code,unsigned char *v,size_t want){
    if(n<DHCP_FIXED||p[0]!=2||memcmp(p+236,"\x63\x82\x53\x63",4)!=0)return 0;
    size_t i=DHCP_FIXED;
    while(i<n){unsigned c=p[i++];if(c==0)continue;if(c==255)return 0;if(i>=n)return 0;size_t len=p[i++];if(len>n-i)return 0;if(c==code){if(len!=want)return 0;memcpy(v,p+i,want);return 1;}i+=len;}
    return 0;
}
static void build_input(unsigned char *p,size_t *n,const unsigned char mac[6],unsigned type,const unsigned char *requested,const unsigned char *server){
    memset(p,0,DHCP_PACKET_MAX);p[0]=1;p[1]=1;p[2]=6;p[4]=0x12;p[5]=0x34;p[6]=0x56;p[7]=0x78;memcpy(p+28,mac,6);
    p[236]=99;p[237]=130;p[238]=83;p[239]=99;size_t at=240;
    unsigned char t=(unsigned char)type;at=append_option(p,at,53,&t,1);
    if(requested)at=append_option(p,at,50,requested,4);
    if(server)at=append_option(p,at,54,server,4);
    p[at++]=255;*n=at;
}
static int selftest(void){
    unsigned char in[DHCP_PACKET_MAX],out[DHCP_PACKET_MAX],mac[6]={2,1,2,3,4,5},requested[4],type;
    size_t n,on;int rc;memset(leases,0,sizeof(leases));
    build_input(in,&n,mac,DHCP_DISCOVER,NULL,NULL);rc=process_packet(in,n,out,&on,1000);
    if(rc!=1||response_option(out,on,53,&type,1)!=1||type!=DHCP_OFFER||memcmp(out+16,"\xc0\xa8\x0a\x64",4))return 1;
    memcpy(requested,out+16,4);build_input(in,&n,mac,DHCP_REQUEST,requested,server_ip);rc=process_packet(in,n,out,&on,1001);
    if(rc!=1||response_option(out,on,53,&type,1)!=1||type!=DHCP_ACK||memcmp(out+16,requested,4))return 2;
    time_t first_lease_expiry=leases[0].lease_until;
    build_input(in,&n,mac,DHCP_REQUEST,requested,server_ip);rc=process_packet(in,n,out,&on,1002);
    if(rc!=1||response_option(out,on,53,&type,1)!=1||type!=DHCP_ACK||leases[0].lease_until!=1002+LEASE_SECONDS)return 5;
    build_input(in,&n,mac,DHCP_DISCOVER,NULL,NULL);rc=process_packet(in,n,out,&on,1003);
    if(rc!=1||leases[0].state!=LEASE_BOUND||leases[0].lease_until!=1002+LEASE_SECONDS||leases[0].offer_until!=1003+OFFER_SECONDS)return 6;
    /* All expired slots are swept even when the requested MAC is found early. */
    leases[1].state=LEASE_OFFERED;leases[1].mac[0]=9;leases[1].offer_until=1004;
    if(lease_for_mac(mac,1004)!=0||leases[1].state!=LEASE_FREE)return 7;
    build_input(in,&n,mac,DHCP_REQUEST,requested,(const unsigned char[]){192,168,124,2});rc=process_packet(in,n,out,&on,1002);
    if(rc!=0)return 3;
    build_input(in,&n,mac,DHCP_DISCOVER,NULL,NULL);in[239]=0;rc=process_packet(in,n,out,&on,1003);
    if(rc!=0)return 4;
    if(first_lease_expiry!=1001+LEASE_SECONDS)return 8;
    printf("selftest PASS: OFFER, matching and retransmitted ACK, bound DISCOVER lease retention, expiry sweep, foreign-server and malformed-cookie ignore\n");return 0;
}
static int read_packet(const char *path,unsigned char *buf,size_t cap,size_t *len){
    FILE *f=fopen(path,"rb");if(!f)return 0;*len=fread(buf,1,cap,f);int bad=ferror(f)||!feof(f);fclose(f);return !bad;
}
static int write_packet(const char *path,const unsigned char *buf,size_t len){
    FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(buf,1,len,f)==len&&!ferror(f);if(fclose(f)!=0)ok=0;return ok;
}
static int valid_iface(const char *s){
    size_t n=strlen(s);if(n==0||n>=IF_NAMESIZE||s[0]=='-')return 0;
    for(size_t i=0;i<n;i++)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'||s[i]=='.'))return 0;
    return 1;
}
static int serve(const char *iface,const char *ip_text){
    struct in_addr parsed;if(!valid_iface(iface)||inet_pton(AF_INET,ip_text,&parsed)!=1||memcmp(&parsed,server_ip,4)!=0){fprintf(stderr,"bridge-dhcp: only a valid interface and server 192.168.10.1 are allowed\n");return 2;}
    if(getuid()!=0){fprintf(stderr,"bridge-dhcp: root is required to bind UDP 67 to the selected interface\n");return 1;}
    int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0){perror("bridge-dhcp: socket");return 1;}
    if(setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,iface,(socklen_t)(strlen(iface)+1))<0){perror("bridge-dhcp: SO_BINDTODEVICE");close(fd);return 1;}
    int yes=1;if(setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes))<0){perror("bridge-dhcp: SO_BROADCAST");close(fd);return 1;}
    struct sockaddr_in local;memset(&local,0,sizeof(local));local.sin_family=AF_INET;local.sin_port=htons(DHCP_PORT);local.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(fd,(struct sockaddr*)&local,sizeof(local))<0){perror("bridge-dhcp: bind UDP 67");close(fd);return 1;}
    memset(leases,0,sizeof(leases));
    for(;;){unsigned char in[DHCP_PACKET_MAX],out[DHCP_PACKET_MAX];struct sockaddr_in peer;socklen_t peer_len=sizeof(peer);
        ssize_t n=recvfrom(fd,in,sizeof(in),0,(struct sockaddr*)&peer,&peer_len);if(n<0){if(errno==EINTR)continue;perror("bridge-dhcp: recvfrom");close(fd);return 1;}
        time_t now=monotonic_seconds();if(now<=0)continue;
        size_t on=0;int rc=process_packet(in,(size_t)n,out,&on,now);if(rc<=0)continue;
        struct sockaddr_in dest;memset(&dest,0,sizeof(dest));dest.sin_family=AF_INET;dest.sin_port=htons(CLIENT_PORT);dest.sin_addr.s_addr=htonl(INADDR_BROADCAST);
        if(sendto(fd,out,on,0,(struct sockaddr*)&dest,sizeof(dest))<0&&errno!=EINTR)perror("bridge-dhcp: sendto");
    }
}
int main(int argc,char **argv){
    const char *iface="eth1",*server="192.168.10.1";
    if(argc==2&&!strcmp(argv[1],"--selftest"))return selftest();
    if(argc==4&&!strcmp(argv[1],"--decode-test")){
        unsigned char in[DHCP_PACKET_MAX],out[DHCP_PACKET_MAX];size_t n,on;memset(leases,0,sizeof(leases));
        if(!read_packet(argv[2],in,sizeof(in),&n)){fprintf(stderr,"cannot read bounded fixture packet\n");return 2;}
        int rc=process_packet(in,n,out,&on,1000);if(rc<0){fprintf(stderr,"fixture allocator full or reply failure\n");return 1;}
        if(rc==0)on=0;
        if(!write_packet(argv[3],out,on)){fprintf(stderr,"cannot write fixture reply\n");return 2;}
        return rc==0?3:0;
    }
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-i")&&i+1<argc)iface=argv[++i];
        else if(!strcmp(argv[i],"-s")&&i+1<argc)server=argv[++i];
        else if(!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help")){printf("usage: %s [-i interface] [-s 192.168.10.1]\n       %s --selftest\n       %s --decode-test input.bin output.bin\n",argv[0],argv[0],argv[0]);return 0;}
        else {fprintf(stderr,"unknown or incomplete argument\n");return 2;}
    }
    return serve(iface,server);
}
