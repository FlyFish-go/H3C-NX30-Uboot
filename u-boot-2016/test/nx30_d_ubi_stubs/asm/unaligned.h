#ifndef HOST_UNALIGNED_H
#define HOST_UNALIGNED_H
#include <stdint.h>
static inline uint32_t get_unaligned_be32(const void *v) { const unsigned char *p=v; return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
#endif
