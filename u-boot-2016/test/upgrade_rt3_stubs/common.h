#ifndef HOST_COMMON_H
#define HOST_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
typedef unsigned int u32;
typedef unsigned long ulong;
#define ARRAY_SIZE(x) ((int)(sizeof(x)/sizeof((x)[0])))
#define httpd_debug(...) do {} while (0)
static inline size_t strlcpy(char *d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
#endif
