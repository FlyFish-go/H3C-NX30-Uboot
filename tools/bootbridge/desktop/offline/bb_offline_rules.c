#define _CRT_SECURE_NO_WARNINGS
#include "bb_offline_rules.h"
#include "../src/bridge_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/*
 * bb_catalog.h above supplies fallbacks for builds that carry no generated
 * catalogue. This unit always compiles one, so drop those fallbacks and let
 * the generated header supply the real release identity. Without this the
 * fallbacks win, and the binary reports the placeholder set instead of the
 * one its adjacent BootBridge.rules.dat was generated for.
 */
#undef BB_RELEASE_SET_ID
#undef BB_ENGINE_REVISION
static unsigned char bb_offline_data[];
#include "bb_offline_catalog_generated.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef BB_OFFLINE_DATA_SIZE
#error generated offline catalog header is required
#endif
#if BB_OFFLINE_DATA_SIZE == 0 || BB_OFFLINE_DATA_SIZE > 67108864ULL
#error offline sidecar size is outside the supported limit
#endif

static unsigned char bb_offline_data[BB_OFFLINE_DATA_SIZE];
static int bb_offline_state;
static bb_bundle_catalog bb_loaded_catalog;

static void report(wchar_t *message, size_t cap, const wchar_t *s)
{
    size_t i;
    if (!message || !cap) return;
    for (i = 0; i + 1 < cap && s[i]; ++i) message[i] = s[i];
    message[i] = 0;
}

#if defined(_WIN32)
static int read_sidecar(void)
{
    wchar_t module[MAX_PATH], path[MAX_PATH];
    DWORD n, attr; size_t i, dir;
    HANDLE h; LARGE_INTEGER size; DWORD got;
    n = GetModuleFileNameW(NULL, module, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    dir = n;
    while (dir && module[dir-1] != L'\\' && module[dir-1] != L'/') --dir;
    if (!dir || dir + 21 > MAX_PATH) return 0;
    for (i=0;i<dir;i++) path[i]=module[i];
    wcscpy(path+dir,L"BootBridge.rules.dat");
    attr=GetFileAttributesW(path);
    if (attr==INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
    h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (h==INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(h,&size) || size.QuadPart!=(LONGLONG)BB_OFFLINE_DATA_SIZE) { CloseHandle(h); return 0; }
    if (!ReadFile(h,bb_offline_data,(DWORD)BB_OFFLINE_DATA_SIZE,&got,NULL) || got!=(DWORD)BB_OFFLINE_DATA_SIZE) { CloseHandle(h); return 0; }
    { unsigned char extra; if (ReadFile(h,&extra,1,&got,NULL) && got!=0) { CloseHandle(h); return 0; } }
    CloseHandle(h); return 1;
}
#elif defined(BB_OFFLINE_TEST)
static int read_sidecar(void)
{
    const char *path=getenv("BB_OFFLINE_TEST_DATA"); FILE *f; long n;
    if (!path || !*path || !(f=fopen(path,"rb"))) return 0;
    if (fseek(f,0,SEEK_END) || (n=ftell(f))<0 || (uint64_t)n!=BB_OFFLINE_DATA_SIZE || fseek(f,0,SEEK_SET)) { fclose(f); return 0; }
    if (fread(bb_offline_data,1,(size_t)BB_OFFLINE_DATA_SIZE,f)!=(size_t)BB_OFFLINE_DATA_SIZE || fgetc(f)!=EOF || ferror(f)) { fclose(f); return 0; }
    fclose(f); return 1;
}
#else
static int read_sidecar(void) { return 0; }
#endif

static int initialize(void)
{
    bi_sha256_ctx ctx; unsigned char digest[32];
    if (!read_sidecar()) return 0;
    bi_sha256_init(&ctx); bi_sha256_update(&ctx,bb_offline_data,(size_t)BB_OFFLINE_DATA_SIZE); bi_sha256_final(&ctx,digest);
    if (memcmp(digest,bb_offline_expected_sha256,32)) { memset(bb_offline_data,0,sizeof(bb_offline_data)); return 0; }
    bb_loaded_catalog.schema=BB_BUNDLE_CATALOG_SCHEMA;
    bb_loaded_catalog.release_set=BB_RELEASE_SET_ID;
    bb_loaded_catalog.bundles=bb_offline_bundles;
    bb_loaded_catalog.bundle_count=BB_OFFLINE_BUNDLE_COUNT;
    return 1;
}

#if defined(_WIN32)
static BOOL CALLBACK bb_offline_init_once(PINIT_ONCE once,PVOID parameter,PVOID *context)
{
    static BOOL ok;
    (void)once; (void)parameter; (void)context;
    ok=initialize()?TRUE:FALSE;
    /* The callback's return means initialization completed; the result is
     * stored separately because InitOnce must also cache a failed validation. */
    bb_offline_state=ok?1:-1;
    return TRUE;
}
#endif

int bb_offline_rules_load_verify(bb_bundle_catalog *out_catalog,wchar_t *message,size_t capacity)
{
    if (!out_catalog) { report(message,capacity,L"Invalid catalog output."); return -1; }
#if defined(_WIN32)
    /* GUI initializes this once at startup. InitOnce also makes repeated or
     * concurrent calls safe while preserving immutable data lifetime. */
    { static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
      InitOnceExecuteOnce(&once,bb_offline_init_once,NULL,NULL);
      if (bb_offline_state<0) { report(message,capacity,L"BootBridge.rules.dat is missing, invalid, or does not match this application."); return -1; } }
#else
    if (bb_offline_state==0) bb_offline_state=initialize()?1:-1;
    if (bb_offline_state<0) { report(message,capacity,L"BootBridge.rules.dat is missing, invalid, or does not match this application."); return -1; }
#endif
    *out_catalog=bb_loaded_catalog;
    report(message,capacity,L"Offline rules verified.");
    return 0;
}

const char *bb_offline_rules_release_set(void)
{
    return BB_RELEASE_SET_ID;
}
