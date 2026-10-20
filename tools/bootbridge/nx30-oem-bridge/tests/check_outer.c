#define _GNU_SOURCE

#include <errno.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/*
 * Synthetic process context for dlopen only. Export the two absolute symbols
 * at the addresses used by original R012 webs. Their ELF sizes are unspecified
 * (zero), so the 2 MiB anonymous mappings use only the observed address spacing;
 * they are not asserted OEM object sizes or live shared-memory contents.
 * Nothing here replaces a checker, product, version, or authentication API.
 */
__asm__(
    ".globl g_szCfgShareBuf\n"
    ".set g_szCfgShareBuf, 0x50000000\n"
    ".globl g_szProductConfigShareBuf\n"
    ".set g_szProductConfigShareBuf, 0x50200000\n"
);

static int map_synthetic_shared_buffers(void)
{
    const uintptr_t addresses[] = {0x50000000u, 0x50200000u};
    const size_t length = 0x200000u;
    size_t i;

    for (i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        void *requested = (void *)addresses[i];
        void *mapped = mmap(requested, length, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                            -1, 0);
        if (mapped == MAP_FAILED) {
            fprintf(stderr, "synthetic mmap at 0x%08lx failed: %s\n",
                    (unsigned long)addresses[i], strerror(errno));
            return -1;
        }
        if (mapped != requested) {
            fprintf(stderr, "synthetic mmap returned unexpected address %p\n", mapped);
            (void)munmap(mapped, length);
            return -1;
        }
    }
    return 0;
}

typedef int (*check_firmware_fn)(const char *path);

int main(int argc, char **argv)
{
    void *handle;
    void *symbol;
    check_firmware_fn check_firmware = NULL;
    const char *error;
    int result;

    if (argc != 2) {
        fprintf(stderr, "usage: %s PACKAGE_PATH\n", argv[0]);
        return 64;
    }

    if (map_synthetic_shared_buffers() != 0)
        return 73;

    dlerror();
    handle = dlopen("/lib/libmwopenrpc.so", RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        error = dlerror();
        fprintf(stderr, "dlopen failed: %s\n", error != NULL ? error : "unknown error");
        return 70;
    }

    dlerror();
    symbol = dlsym(handle, "MW_APP_CheckFirmware");
    error = dlerror();
    if (error != NULL || symbol == NULL) {
        fprintf(stderr, "dlsym failed: %s\n", error != NULL ? error : "symbol is null");
        (void)dlclose(handle);
        return 71;
    }
    if (sizeof(check_firmware) != sizeof(symbol)) {
        fprintf(stderr, "function and object pointer sizes differ\n");
        (void)dlclose(handle);
        return 72;
    }
    memcpy(&check_firmware, &symbol, sizeof(check_firmware));

    result = check_firmware(argv[1]);
    printf("MW_APP_CheckFirmware rc=%d\n", result);
    (void)fflush(stdout);
    (void)dlclose(handle);
    return 0;
}
