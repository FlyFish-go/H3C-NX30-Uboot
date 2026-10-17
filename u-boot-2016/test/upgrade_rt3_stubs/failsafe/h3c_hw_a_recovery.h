#include <common.h>
#include <stddef.h>
int h3c_hw_a_recovery_fit_confirm(const void *,size_t,ulong *,char *,size_t);
int h3c_hw_a_recovery_fit_boot(ulong,const char *);
void h3c_hw_a_recovery_fit_clear(void);

#define H3C_HW_A_RECOVERY_FIT_INVALID_INFO "{\"type\":\"recovery_fit_invalid\"}"
