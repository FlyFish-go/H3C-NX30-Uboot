/* SPDX-License-Identifier: GPL-2.0+ */
#include <h3c_boot_policy.h>

int h3c_boot_reset_gpio(enum h3c_boot_board board)
{
 switch (board) {
 case H3C_BOOT_A: case H3C_BOOT_C: return 27;
 case H3C_BOOT_B: case H3C_BOOT_D: return 34;
 default: return -1;
 }
}

int h3c_boot_mesh_gpio(enum h3c_boot_board board)
{
 switch (board) {
 case H3C_BOOT_A: case H3C_BOOT_B: case H3C_BOOT_C: case H3C_BOOT_D: return 38;
 default: return -1;
 }
}

enum h3c_reset_state h3c_boot_sample_reset(enum h3c_boot_board board,
 const struct h3c_reset_io *io)
{
 int gpio = h3c_boot_reset_gpio(board), last = -1;
 unsigned int i, stable = 0;
 if (gpio < 0 || !io || !io->read || !io->delay)
  return H3C_RESET_UNKNOWN;
 for (i = 0; i < 20; ++i) {
  int value = io->read(io->context, (unsigned int)gpio);
  if (value != 0 && value != 1)
   return H3C_RESET_UNKNOWN;
  stable = value == last ? stable + 1 : 1;
  last = value;
  if (stable == 5)
   return value ? H3C_RESET_RELEASED : H3C_RESET_HELD;
  if (i != 19)
   io->delay(io->context);
 }
 return H3C_RESET_UNKNOWN;
}

enum h3c_boot_action h3c_boot_decide(const struct h3c_boot_inputs *in)
{
 if (!in)
  return H3C_BOOT_WEB;
 if (in->forced_web)
  return H3C_BOOT_WEB;
 if (!in->release_system)
  return H3C_BOOT_LEGACY;
 if (!in->format_ready || in->reset != H3C_RESET_RELEASED)
  return H3C_BOOT_WEB;
 return H3C_BOOT_SELECTED;
}

enum h3c_blink_action h3c_boot_blink_action(int blinks, int dual_layout,
 int single_layout)
{
 if (blinks <= 0)
  return H3C_BLINK_DEFAULT;
 if (blinks >= 3 || (!dual_layout && !single_layout))
  return H3C_BLINK_WEB;
 if (dual_layout)
  return H3C_BLINK_SLOT;
 /* Single large rootfs: slot 1 is the only system; slot 2 does not exist. */
 return blinks == 1 ? H3C_BLINK_DEFAULT : H3C_BLINK_WEB;
}

int h3c_boot_load_failed(int result)
{
 /* boot_os success is 0; successful Linux OS_GO does not return. */
 return result != 0;
}
