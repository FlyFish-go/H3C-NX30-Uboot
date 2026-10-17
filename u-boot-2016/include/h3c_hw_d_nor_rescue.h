/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __H3C_HW_D_NOR_RESCUE_H
#define __H3C_HW_D_NOR_RESCUE_H

#define H3C_HW_D_NOR_RESCUE_MARKER "NX30-HW-D-SPI-NOR-RESCUE-V1"
/* Defined once by the startup path and shared with the package validator. */
extern const char h3c_hw_d_nor_rescue_marker[];
#define H3C_HW_D_NOR_RESCUE_MARKER_SIZE \
	(sizeof(H3C_HW_D_NOR_RESCUE_MARKER) - 1)

#endif
