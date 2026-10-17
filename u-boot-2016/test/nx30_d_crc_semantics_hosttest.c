#include <stdint.h>
typedef unsigned int uint;
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <u-boot/crc.h>
#ifdef crc32
#error "linux/crc32.h macro must not pollute the production U-Boot CRC API"
#endif
int main(int argc,char **argv)
{
 static const unsigned char check[]="123456789";
 FILE *f; long n; unsigned char *data;
 assert(crc32(0,check,9)==0xcbf43926U);
 assert(crc32_no_comp(0,check,9)==0x2dfd2d88U);
 assert(argc==2); f=fopen(argv[1],"rb"); assert(f);
 assert(!fseek(f,0,SEEK_END)); n=ftell(f); assert(n==24772608L); rewind(f);
 data=malloc((size_t)n); assert(data); assert(fread(data,1,(size_t)n,f)==(size_t)n); fclose(f);
 assert(crc32(0,data,(unsigned int)n)==0x601d8883U);
 assert(crc32_no_comp(0,data,(unsigned int)n)==0x456b1cb3U);
 free(data); puts("NX30 CRC semantics PASS: standard and UBI no-complement separated"); return 0;
}
