#ifndef RT3_HOST_LIBFDT_H
#define RT3_HOST_LIBFDT_H
#include <stdint.h>
struct fdt_header { uint32_t words[10]; };
#define FDT_ERR_NOTFOUND 1
int fdt_check_header(const void *fdt);
static inline uint32_t fdt_totalsize(const void *fdt)
{ return __builtin_bswap32(((const struct fdt_header *)fdt)->words[1]); }
int fdt_path_offset(const void *fdt, const char *path);
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp);
#endif
