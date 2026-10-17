/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __H3C_BOOT_POLICY_H__
#define __H3C_BOOT_POLICY_H__
enum h3c_boot_board { H3C_BOOT_A, H3C_BOOT_B, H3C_BOOT_C, H3C_BOOT_D };
enum h3c_reset_state { H3C_RESET_RELEASED, H3C_RESET_HELD, H3C_RESET_UNKNOWN };
enum h3c_boot_action { H3C_BOOT_LEGACY, H3C_BOOT_SELECTED, H3C_BOOT_WEB };
struct h3c_boot_inputs {
 int release_system;
 int forced_web;
 int format_ready;
 enum h3c_reset_state reset;
};
struct h3c_reset_io {
 void *context;
 int (*read)(void *context, unsigned int gpio);
 void (*delay)(void *context);
};
/* Five consecutive samples, bounded to twenty; invalid reads fail closed. */
enum h3c_reset_state h3c_boot_sample_reset(enum h3c_boot_board board,
 const struct h3c_reset_io *io);
int h3c_boot_reset_gpio(enum h3c_boot_board board);
enum h3c_boot_action h3c_boot_decide(const struct h3c_boot_inputs *inputs);
int h3c_boot_load_failed(int result);
#ifndef H3C_BOOT_POLICY_HOST_TEST
void h3c_release_autoboot(void);
void h3c_boot_recovery(void);
#endif
#endif
