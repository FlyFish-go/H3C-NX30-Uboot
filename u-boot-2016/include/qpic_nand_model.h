/* Read-only identity selected by the QPIC serial-NAND ID table. */
#ifndef __QPIC_NAND_MODEL_H
#define __QPIC_NAND_MODEL_H
#if defined(CONFIG_H3C_NAND_ECC_COMPAT) || defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
struct qpic_nand_model_info { const char *name; unsigned char id[3]; };
/* Returns 0 only after the driver matched an actual device ID to its table. */
int qpic_nand_get_model_info(struct qpic_nand_model_info *info);
#endif
#endif
