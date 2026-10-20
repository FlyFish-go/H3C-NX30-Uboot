#ifndef NX30_BRIDGE_SHA256_H
#define NX30_BRIDGE_SHA256_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct { uint32_t h[8]; uint64_t bits; uint8_t block[64]; size_t used; } bi_sha256_ctx;
static const uint32_t bi_sha256_k[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static uint32_t bi_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32-n)); }
static void bi_sha256_block(bi_sha256_ctx *c, const uint8_t *p) {
  uint32_t w[64],a,b,d,e,f,g,h,x,cc; unsigned i;
  for(i=0;i<16;i++) w[i]=((uint32_t)p[4*i]<<24)|((uint32_t)p[4*i+1]<<16)|((uint32_t)p[4*i+2]<<8)|p[4*i+3];
  for(i=16;i<64;i++){uint32_t s0=bi_rotr(w[i-15],7)^bi_rotr(w[i-15],18)^(w[i-15]>>3),s1=bi_rotr(w[i-2],17)^bi_rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
  a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
  for(i=0;i<64;i++){uint32_t s1=bi_rotr(e,6)^bi_rotr(e,11)^bi_rotr(e,25),ch=(e&f)^(~e&g),t1=h+s1+ch+bi_sha256_k[i]+w[i],s0=bi_rotr(a,2)^bi_rotr(a,13)^bi_rotr(a,22),maj=(a&b)^(a&cc)^(b&cc),t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;}
  x=a;c->h[0]+=x;x=b;c->h[1]+=x;x=cc;c->h[2]+=x;x=d;c->h[3]+=x;x=e;c->h[4]+=x;x=f;c->h[5]+=x;x=g;c->h[6]+=x;x=h;c->h[7]+=x;
}
static void bi_sha256_init(bi_sha256_ctx *c) { static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memcpy(c->h,iv,sizeof(iv));c->bits=0;c->used=0; }
static void bi_sha256_update(bi_sha256_ctx *c,const void *data,size_t n){const uint8_t*p=data;c->bits+=(uint64_t)n*8;while(n){size_t z=64-c->used;if(z>n)z=n;memcpy(c->block+c->used,p,z);c->used+=z;p+=z;n-=z;if(c->used==64){bi_sha256_block(c,c->block);c->used=0;}}}
static void bi_sha256_final(bi_sha256_ctx *c,uint8_t out[32]){uint64_t bits=c->bits;unsigned i;c->block[c->used++]=0x80;if(c->used>56){memset(c->block+c->used,0,64-c->used);bi_sha256_block(c,c->block);c->used=0;}memset(c->block+c->used,0,56-c->used);for(i=0;i<8;i++)c->block[63-i]=(uint8_t)(bits>>(8*i));bi_sha256_block(c,c->block);for(i=0;i<8;i++){out[4*i]=(uint8_t)(c->h[i]>>24);out[4*i+1]=(uint8_t)(c->h[i]>>16);out[4*i+2]=(uint8_t)(c->h[i]>>8);out[4*i+3]=(uint8_t)c->h[i];}}
static void bi_sha256_hex(const uint8_t d[32],char out[65]){static const char x[]="0123456789abcdef";size_t i;for(i=0;i<32;i++){out[2*i]=x[d[i]>>4];out[2*i+1]=x[d[i]&15];}out[64]=0;}

#endif
