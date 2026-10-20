#ifdef CONFIG_H3C_HW_D_RAM_AUTOECC
#include <nx30_nor_autoecc.h>
#endif
/*
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
#include <h3c_nand_ecc.h>
#endif
#include <autoboot.h>
#if defined(CONFIG_H3C_RELEASE_SYSTEM_DEFAULT) || \
    defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || \
    defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || \
    defined(CONFIG_H3C_HW_A_RAM_READONLY) || \
    defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || \
    defined(CONFIG_H3C_HW_C_RAM_READONLY) || \
    defined(CONFIG_H3C_HW_D_NOR_RESCUE)
#include <h3c_boot_policy.h>
#endif
#include <bootretry.h>
#ifdef CONFIG_H3C_HW_A_PRODUCTION_PORT
extern int h3c_hw_a_reset_held_3s(void);
#endif
#include <cli.h>
#include <console.h>
#include <fdtdec.h>
#include <menu.h>
#include <post.h>
#include <u-boot/sha256.h>
#include <asm/arch-qca-common/qca_common.h>
#if defined(CONFIG_NET_ABORT)
#include <net_abort.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

extern int do_dumpqca_minimal_data(const char *offset);

#define MAX_DELAY_STOP_STR 32

#ifndef DEBUG_BOOTKEYS
#define DEBUG_BOOTKEYS 0
#endif
#define debug_bootkeys(fmt, args...)		\
	debug_cond(DEBUG_BOOTKEYS, fmt, ##args)

/* Stored value of bootdelay, used by autoboot_command() */
static int stored_bootdelay;

#if defined(CONFIG_AUTOBOOT_KEYED)
#if defined(CONFIG_AUTOBOOT_STOP_STR_SHA256)

/*
 * Use a "constant-length" time compare function for this
 * hash compare:
 *
 * https://crackstation.net/hashing-security.htm
 */
static int slow_equals(u8 *a, u8 *b, int len)
{
	int diff = 0;
	int i;

	for (i = 0; i < len; i++)
		diff |= a[i] ^ b[i];

	return diff == 0;
}

static int passwd_abort(uint64_t etime)
{
	const char *sha_env_str = getenv("bootstopkeysha256");
	u8 sha_env[SHA256_SUM_LEN];
	u8 sha[SHA256_SUM_LEN];
	char presskey[MAX_DELAY_STOP_STR];
	const char *algo_name = "sha256";
	u_int presskey_len = 0;
	int abort = 0;
	int size;
	int ret;

	if (sha_env_str == NULL)
		sha_env_str = CONFIG_AUTOBOOT_STOP_STR_SHA256;

	/*
	 * Generate the binary value from the environment hash value
	 * so that we can compare this value with the computed hash
	 * from the user input
	 */
	ret = hash_parse_string(algo_name, sha_env_str, sha_env);
	if (ret) {
		printf("Hash %s not supported!\n", algo_name);
		return 0;
	}

	/*
	 * We don't know how long the stop-string is, so we need to
	 * generate the sha256 hash upon each input character and
	 * compare the value with the one saved in the environment
	 */
	do {
		if (tstc()) {
			/* Check for input string overflow */
			if (presskey_len >= MAX_DELAY_STOP_STR)
				return 0;

			presskey[presskey_len++] = getc();

			/* Calculate sha256 upon each new char */
			hash_block(algo_name, (const void *)presskey,
				   presskey_len, sha, &size);

			/* And check if sha matches saved value in env */
			if (slow_equals(sha, sha_env, SHA256_SUM_LEN))
				abort = 1;
		}
	} while (!abort && get_ticks() <= etime);

	return abort;
}
#else
static int passwd_abort(uint64_t etime)
{
	int abort = 0;
	struct {
		char *str;
		u_int len;
		int retry;
	}
	delaykey[] = {
		{ .str = getenv("bootdelaykey"),  .retry = 1 },
		{ .str = getenv("bootstopkey"),   .retry = 0 },
	};

	char presskey[MAX_DELAY_STOP_STR];
	u_int presskey_len = 0;
	u_int presskey_max = 0;
	u_int i;

#  ifdef CONFIG_AUTOBOOT_DELAY_STR
	if (delaykey[0].str == NULL)
		delaykey[0].str = CONFIG_AUTOBOOT_DELAY_STR;
#  endif
#  ifdef CONFIG_AUTOBOOT_STOP_STR
	if (delaykey[1].str == NULL)
		delaykey[1].str = CONFIG_AUTOBOOT_STOP_STR;
#  endif

	for (i = 0; i < sizeof(delaykey) / sizeof(delaykey[0]); i++) {
		delaykey[i].len = delaykey[i].str == NULL ?
				    0 : strlen(delaykey[i].str);
		delaykey[i].len = delaykey[i].len > MAX_DELAY_STOP_STR ?
				    MAX_DELAY_STOP_STR : delaykey[i].len;

		presskey_max = presskey_max > delaykey[i].len ?
				    presskey_max : delaykey[i].len;

		debug_bootkeys("%s key:<%s>\n",
			       delaykey[i].retry ? "delay" : "stop",
			       delaykey[i].str ? delaykey[i].str : "NULL");
	}

	/* In order to keep up with incoming data, check timeout only
	 * when catch up.
	 */
	do {
		if (tstc()) {
			if (presskey_len < presskey_max) {
				presskey[presskey_len++] = getc();
			} else {
				for (i = 0; i < presskey_max - 1; i++)
					presskey[i] = presskey[i + 1];

				presskey[i] = getc();
			}
		}

		for (i = 0; i < sizeof(delaykey) / sizeof(delaykey[0]); i++) {
			if (delaykey[i].len > 0 &&
			    presskey_len >= delaykey[i].len &&
				memcmp(presskey + presskey_len -
					delaykey[i].len, delaykey[i].str,
					delaykey[i].len) == 0) {
					debug_bootkeys("got %skey\n",
						delaykey[i].retry ? "delay" :
						"stop");

				/* don't retry auto boot */
				if (!delaykey[i].retry)
					bootretry_dont_retry();
				abort = 1;
			}
		}
	} while (!abort && get_ticks() <= etime);

	return abort;
}
#endif

/***************************************************************************
 * Watch for 'delay' seconds for autoboot stop or autoboot delay string.
 * returns: 0 -  no key string, allow autoboot 1 - got key string, abort
 */
static int abortboot_keyed(int bootdelay)
{
	int abort;
	uint64_t etime = endtick(bootdelay);

#ifndef CONFIG_ZERO_BOOTDELAY_CHECK
	if (bootdelay == 0)
		return 0;
#endif

#  ifdef CONFIG_AUTOBOOT_PROMPT
	/*
	 * CONFIG_AUTOBOOT_PROMPT includes the %d for all boards.
	 * To print the bootdelay value upon bootup.
	 */
	printf(CONFIG_AUTOBOOT_PROMPT, bootdelay);
#  endif

	abort = passwd_abort(etime);
	if (!abort)
		debug_bootkeys("key timeout\n");

#ifdef CONFIG_SILENT_CONSOLE
	if (abort)
		gd->flags &= ~GD_FLG_SILENT;
#endif

	return abort;
}

# else	/* !defined(CONFIG_AUTOBOOT_KEYED) */

#ifdef CONFIG_MENUKEY
static int menukey;
#endif

static int abortboot_normal(int bootdelay)
{
	int abort = 0;
	unsigned long ts;

#if defined(CONFIG_NET_ABORT)
	bool net_abort_enabled = net_abort_prepare();
#else
	bool net_abort_enabled = false;
#endif

#ifdef CONFIG_MENUPROMPT
	printf(CONFIG_MENUPROMPT);
#else
	if (bootdelay >= 0)
		printf("Hit any key%s to stop autoboot: %-2d",
			net_abort_enabled ? " or use net abort tool" : "", bootdelay);
#endif

#if defined CONFIG_ZERO_BOOTDELAY_CHECK
	/*
	 * Check if key already pressed
	 * Don't check if bootdelay < 0
	 */
	if (bootdelay >= 0) {
		if (tstc()) {	/* we got a key press	*/
			(void) getc();  /* consume input	*/
			puts("\b\b0 ");
			abort = 1;	/* don't auto boot	*/
		}
	}
#endif

	while ((bootdelay > 0) && (!abort)) {
		--bootdelay;
		/* delay 1000 ms */
		ts = get_timer(0);
		do {
			if (tstc()) {	/* we got a key press	*/
				abort  = 1;	/* don't auto boot	*/
				bootdelay = 0;	/* no more delay	*/
# ifdef CONFIG_MENUKEY
				menukey = getc();
# else
				(void) getc();  /* consume input	*/
# endif
				break;
			}
#if defined(CONFIG_NET_ABORT)
			if (net_abort_enabled) {
				eth_rx();
				if (net_abort_detected()) {
					abort  = 1;	/* don't auto boot	*/
					bootdelay = 0;	/* no more delay	*/
					break;
				}
			}
#endif
			udelay(10000);
		} while (!abort && get_timer(ts) < 1000);

		printf("\b\b%-2d", bootdelay);
	}

	putc('\n');

#if defined(CONFIG_NET_ABORT)
	if (net_abort_enabled)
		net_abort_finish();
#endif

#ifdef CONFIG_SILENT_CONSOLE
	if (abort)
		gd->flags &= ~GD_FLG_SILENT;
#endif
	return abort;
}
# endif	/* CONFIG_AUTOBOOT_KEYED */

static int abortboot(int bootdelay)
{
#ifdef CONFIG_AUTOBOOT_KEYED
	return abortboot_keyed(bootdelay);
#else
	return abortboot_normal(bootdelay);
#endif
}

static void process_fdt_options(const void *blob)
{
#if defined(CONFIG_OF_CONTROL) && !defined(CONFIG_REDUCE_FOOTPRINT)
	ulong addr;

	/* Add an env variable to point to a kernel payload, if available */
	addr = fdtdec_get_config_int(gd->fdt_blob, "kernel-offset", 0);
	if (addr)
		setenv_addr("kernaddr", (void *)(CONFIG_SYS_TEXT_BASE + addr));

	/* Add an env variable to point to a root disk, if available */
	addr = fdtdec_get_config_int(gd->fdt_blob, "rootdisk-offset", 0);
	if (addr)
		setenv_addr("rootaddr", (void *)(CONFIG_SYS_TEXT_BASE + addr));
#endif /* CONFIG_OF_CONTROL */
}

const char *bootdelay_process(void)
{
	char *s;
	int bootdelay;
#ifdef CONFIG_BOOTCOUNT_LIMIT
	unsigned long bootcount = 0;
	unsigned long bootlimit = 0;
#endif /* CONFIG_BOOTCOUNT_LIMIT */

#ifdef CONFIG_BOOTCOUNT_LIMIT
	bootcount = bootcount_load();
	bootcount++;
	bootcount_store(bootcount);
	setenv_ulong("bootcount", bootcount);
	bootlimit = getenv_ulong("bootlimit", 10, 0);
#endif /* CONFIG_BOOTCOUNT_LIMIT */

	s = getenv("bootdelay");
	bootdelay = s ? (int)simple_strtol(s, NULL, 10) : CONFIG_BOOTDELAY;

#if defined(CONFIG_OF_CONTROL) && !defined(CONFIG_REDUCE_FOOTPRINT)
	bootdelay = fdtdec_get_config_int(gd->fdt_blob, "bootdelay",
			bootdelay);
#endif

	debug("### main_loop entered: bootdelay=%d\n\n", bootdelay);

#if defined(CONFIG_MENU_SHOW)
	bootdelay = menu_show(bootdelay);
#endif
	bootretry_init_cmd_timeout();

#ifdef CONFIG_POST
	if (gd->flags & GD_FLG_POSTFAIL) {
		s = getenv("failbootcmd");
	} else
#endif /* CONFIG_POST */
#ifdef CONFIG_BOOTCOUNT_LIMIT
	if (bootlimit && (bootcount > bootlimit)) {
		printf("Warning: Bootlimit (%u) exceeded. Using altbootcmd.\n",
		       (unsigned)bootlimit);
		s = getenv("altbootcmd");
	} else
#endif /* CONFIG_BOOTCOUNT_LIMIT */
		s = getenv("bootcmd");

	process_fdt_options(gd->fdt_blob);
	stored_bootdelay = bootdelay;

	return s;
}

__weak int apps_iscrashed(void)
{
	return 0;
}

__weak int apps_iscrashed_crashdump_disabled(void)
{
	return 0;
}

__weak void indicate_sdx_device(void) {}

void autoboot_command(const char *s)
{
#ifdef CONFIG_H3C_RELEASE_SYSTEM_DEFAULT
	/* Dedicated release path ignores all imported command strings. */
	/* Keep the standard UART stop window without consulting ENV bootcmd. */
	if (abortboot(3))
		return;
	h3c_release_autoboot();
	return;
#endif
#ifdef CONFIG_H3C_HW_D_NOR_RESCUE
	/* Ignore any stored bootcmd and keep the recovery service reachable. */
	if (setenv("ethprime", "eth1") ||
	    setenv("ethact", "eth1") ||
	    setenv("ipaddr", "192.168.10.1") ||
	    setenv("netmask", "255.255.255.0") ||
	    setenv("serverip", "192.168.10.2"))
		puts("NOR rescue: failed to set LAN recovery defaults\n");
	s = "httpd";
	stored_bootdelay = 0;
#elif defined(CONFIG_H3C_DEFAULT_WEB)
#ifdef CONFIG_TARGET_IPQ5018_H3C_HW_D
    /* Bridge installer profile: fixed RAM-only LAN defaults. */
    if (setenv("ethprime", "eth1") || setenv("ethact", "eth1") ||
        setenv("ethrotate", "no") || setenv("ipaddr", "192.168.10.1") ||
        setenv("netmask", "255.255.255.0") || setenv("serverip", "192.168.10.2")) {
        puts("Bridge LAN setup failed; staying at CLI\n");
        return;
    }
#endif
#ifdef CONFIG_H3C_HW_D_RAM_AUTOECC
    nx30_nor_nand_autoecc_cli();
#endif
    s = "httpd";
    stored_bootdelay = 0;
#elif defined(CONFIG_H3C_HW_D_RAM_WEB)
	/* Enter the read-only maintenance preview on the verified D LAN port. */
	if (setenv("ethprime", "eth1") || setenv("ethact", "eth1") || setenv("ethrotate", "no"))
		puts("NX30 D RAM web preview: cannot select eth1\n");
#ifdef CONFIG_H3C_HW_D_RAM_AUTOECC
	/* A failed or ambiguous probe keeps the read-only web/serial rescue. */
	nx30_nor_nand_autoecc_cli();
#endif
	s = "httpd";
	stored_bootdelay = 0;
#elif defined(CONFIG_H3C_HW_D_RAM_READONLY)
	/* Ignore imported bootcmd and leave this bounded diagnostic at CLI. */
	s = "echo NX30 D RAM diagnostic ready";
	stored_bootdelay = 0;
#endif
#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
	/*
	 * Ignore OEM persistent bootcmd in RAM validation. A/C pin only the
	 * network values here; port selection stays with each board.
	 */
	if (setenv("ipaddr", "192.168.10.1") ||
	    setenv("netmask", "255.255.255.0") ||
	    setenv("serverip", "192.168.10.2"))
		puts("H3C A/C: failed to set RAM recovery LAN defaults\n");
	s = "httpd";
	stored_bootdelay = CONFIG_BOOTDELAY;
#elif defined(CONFIG_H3C_HW_A_PRODUCTION_PORT)
	/*
	 * HW-A production always enters HTTP recovery. The OEM environment
	 * survives installation with ipaddr=192.168.10.10, so the recovery LAN
	 * is re-asserted in RAM only (never saved) before httpd starts.
	 */
	if (setenv("ipaddr", "192.168.10.1") ||
	    setenv("netmask", "255.255.255.0") ||
	    setenv("serverip", "192.168.10.2"))
		puts("HW-A: failed to set RAM recovery LAN defaults\n");
	s = "httpd";
	stored_bootdelay = 0;
#endif
#if defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT)
	/* C's three LAN ports share the RTL8367S CPU link on GMAC1. */
	if (setenv("ethact", "eth1") ||
	    setenv("ipaddr", "192.168.10.1") ||
	    setenv("netmask", "255.255.255.0") ||
	    setenv("serverip", "192.168.10.2")) {
		puts("HW-C: unable to select LAN for web recovery\n");
		return;
	}
#endif
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
    if (!h3c_nand_ecc_ready()) {
        puts("H3C NAND: format unconfirmed; entering recovery web\n");
        if (setenv("ethact", "eth1"))
            return;
        s = "httpd";
        stored_bootdelay = 0;
    }
#endif
	debug("### main_loop: bootcmd=\"%s\"\n", s ? s : "<UNDEFINED>");

#if defined(CONFIG_QCA_APPSBL_DLOAD) && !defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) && !defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
	/*
	 * If kernel has crashed in previous boot,
	 * jump to crash dump collection.
	 */
	if (apps_iscrashed()) {
		printf("Crashdump magic found, initializing dump activity..\n");
		indicate_sdx_device();
		s = getenv("dump_to_flash");
		if (!s) {
			s = getenv("dump_minimal");
			if (s) {
				if (strncmp(s, "1", sizeof("1"))) {
					printf("\nError: Invalid variable dump_minimal \n");
					crashdump_exit();
				}
			}
		}
		if (s) {
			do_dumpqca_minimal_data(s);
			crashdump_exit();
		}
		else
			dump_func(FULL_DUMP);
		return;
	}
#endif

#if !defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) && !defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
	if (apps_iscrashed_crashdump_disabled()) {
		printf("Crashdump disabled, resetting the board..\n");
		reset_board();
	}
#endif

	if (stored_bootdelay != -1 && s && !abortboot(stored_bootdelay)) {
#if defined(CONFIG_AUTOBOOT_KEYED) && !defined(CONFIG_AUTOBOOT_KEYED_CTRLC)
		int prev = disable_ctrlc(1);	/* disable Control C checking */
#endif

		run_command_list(s, -1, 0);

#if defined(CONFIG_AUTOBOOT_KEYED) && !defined(CONFIG_AUTOBOOT_KEYED_CTRLC)
		disable_ctrlc(prev);	/* restore Control C checking */
#endif
#if !defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) && \
	!defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) && \
	!defined(CONFIG_TARGET_IPQ5018_H3C_HW_C) && \
	!defined(CONFIG_H3C_HW_D_RAM_READONLY)
		const char *cmd = "bootipq";
		if (!s || strncmp(s, cmd, strlen(cmd))) {
			printf("Failed to run `%s`, try `%s`\n", s, cmd);
			run_command(cmd, 0);
		}
#else
		/* HW-B bootipq handles its own web recovery on failure.
		 * Do not retry a returned explicit boot command with another slot.
		 */
#endif
	}

#ifdef CONFIG_MENUKEY
	if (menukey == CONFIG_MENUKEY) {
		s = getenv("menucmd");
		if (s)
			run_command_list(s, -1, 0);
	}
#endif /* CONFIG_MENUKEY */

#if !defined(CONFIG_HTTPD) && defined(CONFIG_IPQ_ETH_INIT_DEFER)
	puts("\nNet:   ");
	eth_initialize();
#endif
#ifdef CONFIG_IPQ_PCI_INIT_DEFER
	pci_init();
#endif
}
