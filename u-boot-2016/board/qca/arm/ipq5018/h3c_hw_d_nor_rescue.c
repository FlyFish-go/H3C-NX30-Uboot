// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#include <command.h>
#include <nx30_nor_nand.h>

/* The NOR web adapter shares the automatic read-only detector with CLI. */
extern int nx30_nor_nand_autoecc_cli(void);

static int do_nor_nand_probe(cmd_tbl_t *cmdtp, int flag, int argc,
                             char * const argv[])
{
    unsigned int bits;
    int ret;
    (void)cmdtp;
    (void)flag;
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "auto")))
        return nx30_nor_nand_autoecc_cli();
    if (argc != 2 || (strcmp(argv[1], "4") && strcmp(argv[1], "8")))
        return CMD_RET_USAGE;
    bits = argv[1][0] - '0';
    ret = nx30_nor_nand_init_once();
    if (!ret)
        ret = nx30_nor_nand_set_read_ecc(bits);
    if (ret) {
        printf("NOR NAND debug mode rejected: %d\n", ret);
        return CMD_RET_FAILURE;
    }
    printf("NAND read-only debug mode ECC%u; existing data format is not validated.\n", bits);
    return CMD_RET_SUCCESS;
}

U_BOOT_CMD(nor_nand_probe, 2, 0, do_nor_nand_probe,
           "automatically inspect NAND system ECC in read-only mode",
           "[auto] - bounded read-only UBI metadata sampling\n"
           "nor_nand_probe 4|8 - advanced debug override, no format validation");
