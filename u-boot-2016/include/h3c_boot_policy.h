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
/* Persist a physical default rootfs slot (0 = lower extent) through the
 * rescue page's BOOTCONFIG transaction. 0 changed, 2 already default,
 * 1 refused before any write, negative after a partial write. */
int rt3_boot_default_set_physical(int selector, char name[16]);
/* RESET blink count seen by the startup countdown (0 = none). */
void h3c_boot_request_blinks(int blinks);
/* Map a blink count to an action; pure, host-tested. */
enum h3c_blink_action { H3C_BLINK_DEFAULT, H3C_BLINK_SLOT, H3C_BLINK_WEB };
enum h3c_blink_action h3c_boot_blink_action(int blinks, int dual_layout, int single_layout);
#ifndef H3C_BOOT_POLICY_HOST_TEST
void h3c_release_autoboot(void);
void h3c_boot_recovery(void);
#endif
#endif
