/*
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

/* #define	DEBUG	*/

#include <common.h>
#include <autoboot.h>
#include <cli.h>
#include <console.h>
#include <version.h>
#ifdef CONFIG_H3C_HW_D_NOR_RESCUE
#include <h3c_hw_d_nor_rescue.h>
const char h3c_hw_d_nor_rescue_marker[] = H3C_HW_D_NOR_RESCUE_MARKER;
#endif
#if defined(CONFIG_HTTPD)
#include <asm/arch-qca-common/gpio.h>
#include <ipq_api.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

/*
 * Board-specific Platform code can reimplement show_boot_progress () if needed
 */
__weak void show_boot_progress(int val) {}

#ifndef CONFIG_REDUCE_FOOTPRINT
static void modem_init(void)
{
#ifdef CONFIG_MODEM_SUPPORT
	debug("DEBUG: main_loop:   gd->do_mdm_init=%lu\n", gd->do_mdm_init);
	if (gd->do_mdm_init) {
		char *str = getenv("mdm_cmd");

		setenv("preboot", str);  /* set or delete definition */
		mdm_init(); /* wait for modem connection */
	}
#endif  /* CONFIG_MODEM_SUPPORT */
}

#if !defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) && !defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
static void run_preboot_environment_command(void)
{
#ifdef CONFIG_PREBOOT
	char *p;

	p = getenv("preboot");
	if (p != NULL) {
# ifdef CONFIG_AUTOBOOT_KEYED
		int prev = disable_ctrlc(1);	/* disable Control C checking */
# endif

		run_command_list(p, -1, 0);

# ifdef CONFIG_AUTOBOOT_KEYED
		disable_ctrlc(prev);	/* restore Control C checking */
# endif
	}
#endif /* CONFIG_PREBOOT */
}
#endif /* neither HW-A nor HW-C */
#endif

/* We come here after U-Boot is initialised and ready to process commands */
void main_loop(void)
{
	const char *s = NULL;

	bootstage_mark_name(BOOTSTAGE_ID_MAIN_LOOP, "main_loop");
#ifdef CONFIG_H3C_HW_D_NOR_RESCUE
	puts(h3c_hw_d_nor_rescue_marker);
	putc('\n');
#endif

#ifndef CONFIG_SYS_GENERIC_BOARD
	puts("Warning: Your board does not use generic board. Please read\n");
	puts("doc/README.generic-board and take action. Boards not\n");
	puts("upgraded by the late 2014 may break or be removed.\n");
#endif

#ifndef CONFIG_REDUCE_FOOTPRINT
	modem_init();
#ifdef CONFIG_VERSION_VARIABLE
	setenv("ver", version_string);  /* set version variable */
#endif /* CONFIG_VERSION_VARIABLE */
#endif

	cli_init();

#ifdef CONFIG_H3C_HW_D_NOR_RESCUE
	/*
	 * Do not enter the generic preboot/autoboot path: old SPI ENV values,
	 * bootconfig, key detection, and board HTTPD checks can select NAND paths.
	 * Establish a RAM-only recovery environment and start the HTTP server now.
	 */
	setenv("preboot", NULL);
	setenv("bootcmd", "httpd");
	setenv("bootdelay", "0");
	setenv("bootargs", "");
	setenv("ethprime", "eth1");
	setenv("ethact", "eth1");
	setenv("ipaddr", "192.168.10.1");
	setenv("netmask", "255.255.255.0");
	setenv("serverip", "192.168.10.2");
	ipq_gpio_init();
	puts("NOR rescue: forced eth1 HTTP recovery at 192.168.10.1\n");
	run_command("httpd", 0);
	cli_loop();
	return;
#endif

#if !defined(CONFIG_REDUCE_FOOTPRINT) && !defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) && !defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
	run_preboot_environment_command();
#endif

#if defined(CONFIG_UPDATE_TFTP)
	update_tftp(0UL, NULL, NULL);
#endif /* CONFIG_UPDATE_TFTP */

#if defined(CONFIG_HTTPD)
	reload_mibib_from_flash_and_set_default_flash_type_in_9008_mode();
	do_network_check();
	do_httpd_check();
#endif

	s = bootdelay_process();
#ifndef CONFIG_REDUCE_FOOTPRINT
	if (cli_process_fdt(&s))
		cli_secure_boot_cmd(s);
#endif

	autoboot_command(s);

	cli_loop();
}
