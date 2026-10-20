/* Cross-check harness: compiles the desktop engine (desktop/src/bb_export.c) with
 * a generated catalog header and exports one bundle exactly like the desktop app.
 * usage: c_catalog_harness DAT BUNDLE_ID INPUT OUTPUT  -> prints bb_status
 * build: cc -O2 -I desktop/src -DBB_CATALOG_HEADER='"path/to/header.h"' ... */
#include "bb_export.h"
#include "bb_export_plan.h"
#include <stdio.h>
#include <string.h>
bb_result bb_export_plan_bin(const char*,const char*,const bb_plan_recipe*,size_t,const char*);
typedef enum { BB_BOARD_HW_A, BB_BOARD_HW_B, BB_BOARD_HW_C, BB_BOARD_HW_D } bb_board;
typedef enum { BB_MODEL_RC3000, BB_MODEL_RT3000, BB_MODEL_RW3000, BB_MODEL_NX30 } bb_model;
typedef struct { const char *id; bb_model model; const char *display, *version, *release; unsigned engine;
                 bb_plan_recipe *plan; const bb_board *providers; unsigned provider_count; } bb_bundle_rule;
static unsigned char bb_data_buf[64u << 20];
#define bb_offline_data bb_data_buf
#include BB_CATALOG_HEADER
int main(int argc, char **argv) {
  if (argc != 5) return 2;
  FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
  size_t n = fread(bb_data_buf, 1, sizeof bb_data_buf, f); fclose(f);
  if (n != BB_OFFLINE_DATA_SIZE) { puts("dat-size"); return 3; }
  for (size_t i = 0; i < BB_OFFLINE_BUNDLE_COUNT; i++)
    if (!strcmp(bb_offline_bundles[i].id, argv[2])) {
      bb_result r = bb_export_plan_bin(argv[3], argv[4], bb_offline_bundles[i].plan, 1, argv[2]);
      printf("%d\n", (int)r.status); return 0;
    }
  puts("no-bundle"); return 4;
}
