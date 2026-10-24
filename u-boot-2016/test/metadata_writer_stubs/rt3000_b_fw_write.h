#include <linux/mtd/mtd.h>
int rt3_metadata_write_window_open(struct mtd_info *,const char *,uint64_t,uint64_t);
void rt3_fw_write_window_close(void);
void rt3_flash_require_reboot(void);
int rt3_flash_reboot_required(void);
