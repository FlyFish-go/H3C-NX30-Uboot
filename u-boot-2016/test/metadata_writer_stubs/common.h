#ifndef MD_HOST_COMMON_H
#define MD_HOST_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 95
#endif
#ifndef EXDEV
#define EXDEV 18
#endif
#ifndef ENODEV
#define ENODEV 19
#endif
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef unsigned long ulong;
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define ARCH_DMA_MINALIGN 64
#define MTD_NANDFLASH 4
#define CHUNKSZ_SHA256 (64*1024)
#define memchr_inv(p,v,n) host_memchr_inv((p),(v),(n))
static inline const void *host_memchr_inv(const void*p,int v,size_t n){const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i]!=(unsigned char)v)return b+i;return NULL;}
#endif
