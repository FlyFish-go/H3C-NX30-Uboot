/* SPDX-License-Identifier: GPL-2.0+ */
/* Release-only boot control: RAM settings and GPIO input configuration only. */
#include <common.h>
#include <command.h>
#include <asm/io.h>
#include <asm/arch-qca-common/gpio.h>
#include <dt-bindings/qcom/gpio-ipq5018.h>
#include <h3c_boot_policy.h>
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
#include <h3c_nand_ecc.h>
#endif

static int recovery_active;
static int release_dispatch_active;
static int release_dispatch_recovered;

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
 if (h3c_boot_decide(&in) == H3C_BOOT_SELECTED) {
  /* Exact audited default-slot command; never imported bootcmd/altbootcmd.
   * do_bootipq owns errors and recovery. Do not retry or enter HTTP twice. */
  int result;
  release_dispatch_active = 1;
  release_dispatch_recovered = 0;
  result = run_command("bootipq", 0);
  if (h3c_boot_load_failed(result) && !release_dispatch_recovered)
   h3c_boot_recovery();
  release_dispatch_active = 0;
  release_dispatch_recovered = 0;
 } else {
  h3c_boot_recovery();
 }
}
