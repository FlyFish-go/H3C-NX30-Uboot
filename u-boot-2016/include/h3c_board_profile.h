/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __H3C_BOARD_PROFILE_H__
#define __H3C_BOARD_PROFILE_H__
/* Hardware pinmux, DDR fallback and PHY topology remain in each board DTS. */
/* Pin assignment is board configuration; the button endpoint is shared. */
#define H3C_RESET_GPIO 34
#define H3C_WPS_GPIO 38
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
#define H3C_BOARD_LABEL "H3C HW-B"
#define H3C_BOARD_FAMILY "HW-B"
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
#define H3C_BOARD_LABEL "H3C HW-A"
#define H3C_BOARD_FAMILY "HW-A"
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_D)
#define H3C_BOARD_LABEL "H3C HW-D / NX30"
#define H3C_BOARD_FAMILY "HW-D"
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
#define H3C_BOARD_LABEL "H3C HW-C"
#define H3C_BOARD_FAMILY "HW-C"
#else
#define H3C_BOARD_LABEL "IPQ5018"
#define H3C_BOARD_FAMILY "IPQ5018"
#endif
#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || (defined(CONFIG_H3C_HW_D_RAM_READONLY) && !defined(CONFIG_H3C_HW_D_PRODUCTION_PORT))
#define H3C_BOARD_RAM_READONLY 1
#else
#define H3C_BOARD_RAM_READONLY 0
#endif
#endif
