/* SPDX-License-Identifier: GPL-2.0+ */
/* Release-only boot control: RAM settings and GPIO input configuration only. */
#include <common.h>
#include <command.h>
#include <asm/io.h>
#include <asm/arch-qca-common/gpio.h>
#include <dt-bindings/qcom/gpio-ipq5018.h>
#include <h3c_boot_policy.h>
#ifdef CONFIG_H3C_IPQ5018_COMMON
#include <rt3000_b_layout.h>
#endif
#ifdef CONFIG_H3C_HW_D_RAM_WEB
#include <ipq_led.h>
#endif
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
#include <h3c_nand_ecc.h>
#endif

static int recovery_active;
static int release_dispatch_active;
static int release_dispatch_recovered;
static int requested_blinks;

void h3c_boot_request_blinks(int blinks)
{
 requested_blinks = blinks;
}

/* Build the boot command for a RESET blink request. Returns 0 with cmd set,
 * or -1 when the request must end in web recovery. */
static int release_blink_command(int blinks, char *cmd, size_t size)
{
#ifdef CONFIG_H3C_IPQ5018_COMMON
 struct rt3_layout layout;
 const struct rt3_partition *part;
 char name[16];
 int dual = 0, single = 0, r;
 if (!rt3_layout_read(&layout)) {
  dual = layout.mode == RT3_LAYOUT_DUAL;
  single = layout.mode == RT3_LAYOUT_SINGLE;
 }
 switch (h3c_boot_blink_action(blinks, dual, single)) {
 case H3C_BLINK_DEFAULT:
  if (single)
   puts("H3C RESET: single-rootfs layout; booting the only system\n");
  snprintf(cmd, size, "%s", "bootipq");
  return 0;
 case H3C_BLINK_WEB:
  if (single)
   puts("H3C RESET: single-rootfs layout has no slot 2; enter web recovery\n");
  else if (!dual)
   puts("H3C RESET: unsupported rootfs layout; enter web recovery\n");
  return -1;
 default:
  break;
 }
 if (rt3_layout_default_target(&layout, blinks - 1, &part)) {
  puts("H3C RESET: physical slot not resolvable; enter web recovery\n");
  return -1;
 }
 printf("H3C RESET: physical slot %d = %s at 0x%llx\n", blinks, part->name,
        (unsigned long long)part->offset);
 r = rt3_boot_default_set_physical(blinks - 1, name);
 if (r < 0) {
  puts("H3C RESET: BOOTCONFIG write incomplete; enter web recovery\n");
  return -1;
 }
 if (r == 0)
  printf("H3C RESET: default is now physical slot %d\n", blinks);
 else if (r == 2)
  printf("H3C RESET: physical slot %d was already the default\n", blinks);
 else
  puts("H3C RESET: default not changed (refused before any write); booting this slot once\n");
 snprintf(cmd, size, "bootipq %s", part->name);
 return 0;
#else
 (void)blinks; (void)size;
 snprintf(cmd, size, "%s", "bootipq");
 return 0;
#endif
}

static int recovery_lan(void)
{
 return setenv("ethprime", "eth1") || setenv("ethact", "eth1") ||
  setenv("ethrotate", "no") || setenv("ipaddr", "192.168.10.1") ||
  setenv("netmask", "255.255.255.0") || setenv("serverip", "192.168.10.2");
}

void h3c_boot_recovery(void)
{
 /* One synchronous owner: nested entry cannot start another HTTP service.
  * No ENV marker can claim that a failed load was handled. */
 if (release_dispatch_active)
  release_dispatch_recovered = 1;
 if (recovery_active)
  return;
 recovery_active = 1;
 if (recovery_lan())
  puts("H3C: recovery LAN setup failed; remaining at CLI\n");
 else
  run_command("httpd", 0);
 recovery_active = 0;
}

static enum h3c_boot_board release_board(void)
{
#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
 return H3C_BOOT_A;
#elif defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
 return H3C_BOOT_B;
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
 return H3C_BOOT_C;
#else
 return H3C_BOOT_D;
#endif
}

static int reset_read(void *context, unsigned int gpio)
{
 (void)context;
 return gpio_get_value(gpio);
}

static void reset_delay(void *context)
{
 (void)context;
 udelay(10000);
}

static enum h3c_reset_state release_reset(enum h3c_boot_board board)
{
 const u32 mask = 3U | (15U << 2) | (7U << 6) | (1U << 9);
 const u32 input = GPIO_PULL_UP | (GPIO_INPUT << 2) |
  (GPIO_8MA << 6) | (GPIO_OE_DISABLE << 9);
 struct h3c_reset_io io = { NULL, reset_read, reset_delay };
 int gpio = h3c_boot_reset_gpio(board);
 void *config;
 u32 value;
 if (gpio < 0)
  return H3C_RESET_UNKNOWN;
 config = (void *)(uintptr_t)GPIO_CONFIG_ADDR(gpio);
 value = readl(config);
 /* Do not write the GPIO output latch while configuring its input. */
 writel((value & ~mask) | input, config);
 if ((readl(config) & mask) != input)
  return H3C_RESET_UNKNOWN;
 return h3c_boot_sample_reset(board, &io);
}

#ifdef CONFIG_H3C_HW_D_RAM_WEB
/*
 * HW-D has no early RESET countdown (do_httpd_check is compiled out), so the
 * release path runs the same flash selector as A/B/C: one short red flash per
 * second while RESET stays held; release after 1 or 2 flashes selects that
 * physical slot, holding through the third flash enters web rescue.
 */
static int release_reset_blinks(enum h3c_boot_board board)
{
 int gpio = h3c_boot_reset_gpio(board), blinks = 0, released = 0, i;
 ulong t0, t;
 if (gpio < 0)
  return 3;
 led_all_off();
 puts("RESET held: release after 1 flash = slot 1, 2 flashes = slot 2, hold for 3 = web failsafe\n");
 t0 = get_timer(0);
 /* Two consecutive released samples (10 ms apart) end the hold. */
 while (released < 2) {
  t = get_timer(t0);
  if (t >= (ulong)(blinks + 1) * 1000UL) {
   blinks++;
   led_on("power_led");
   printf("RESET flash %d\n", blinks);
   if (blinks >= 3)
    break;
  } else if (blinks && t >= (ulong)blinks * 1000UL + 250UL) {
   led_off("power_led");
  }
  released = gpio_get_value(gpio) ? released + 1 : 0;
  udelay(10000);
 }
 if (blinks >= 3) {
  led_on("power_led");
  return 3;
 }
 led_all_off();
 led_on("system_led");
 for (i = 0; i < blinks; i++) {
  led_off("system_led"); mdelay(200);
  led_on("system_led"); mdelay(200);
 }
 if (blinks)
  printf("RESET released after %d flash(es): physical slot %d requested\n",
         blinks, blinks);
 return blinks;
}
#endif

void h3c_release_autoboot(void)
{
 struct h3c_boot_inputs in = { 1, 0, 1, H3C_RESET_UNKNOWN };
#if defined(CONFIG_H3C_HW_D_NOR_RESCUE) || \
 defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || \
 defined(CONFIG_H3C_HW_D_SERVICE_RAM) || defined(CONFIG_H3C_RAM_RESCUE) || \
 (defined(CONFIG_H3C_HW_D_RAM_READONLY) && !defined(CONFIG_H3C_HW_D_PRODUCTION_PORT))
 in.forced_web = 1;
#endif
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
 in.format_ready = h3c_nand_ecc_ready();
#endif
 if (!in.forced_web)
  in.reset = release_reset(release_board());
#ifdef CONFIG_H3C_HW_D_RAM_WEB
 if (in.reset == H3C_RESET_HELD) {
  int blinks = release_reset_blinks(release_board());
  if (blinks < 3) {
   in.reset = H3C_RESET_RELEASED;
   requested_blinks = blinks;
  }
 }
#endif
 if (h3c_boot_decide(&in) == H3C_BOOT_SELECTED) {
  /* Exact audited default-slot command; never imported bootcmd/altbootcmd.
   * do_bootipq owns errors and recovery. Do not retry or enter HTTP twice. */
  int result;
  char cmd[32] = "bootipq";
  int blinks = requested_blinks;
  requested_blinks = 0;
  if (blinks && release_blink_command(blinks, cmd, sizeof(cmd))) {
   h3c_boot_recovery();
   return;
  }
  release_dispatch_active = 1;
  release_dispatch_recovered = 0;
  result = run_command(cmd, 0);
  if (h3c_boot_load_failed(result) && !release_dispatch_recovered)
   h3c_boot_recovery();
  release_dispatch_active = 0;
  release_dispatch_recovered = 0;
 } else {
  h3c_boot_recovery();
 }
}
