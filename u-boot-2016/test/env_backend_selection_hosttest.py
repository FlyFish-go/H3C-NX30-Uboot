#!/usr/bin/env python3
"""Compile the real IPQ backend-selection block for production/diagnostics."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'board/qca/arm/common/board_init.c').read_text()
start=s.index('#if (defined(CONFIG_H3C_HW_D_RAM_READONLY)')
end=s.index('#endif /* !CONFIG_TARGET_IPQ5018_H3C_HW_A */',start)+len('#endif /* !CONFIG_TARGET_IPQ5018_H3C_HW_A */')
selection=s[start:end]
pre=r"""
#include <stddef.h>
#include <stdio.h>
#define SMEM_BOOT_SPI_FLASH 6
#define CONFIG_ENV_IS_IN_SPI_FLASH 1
#define CONFIG_ENV_IS_IN_NAND 1
struct { int flash_type; } flash, *sfi=&flash;
int nand_saveenv(void) { return 0; }
int sf_saveenv(void) { return 0; }
int (*saveenv)(void); void *env_ptr, *nand_env_ptr;
char *env_name_spec, *sf_env_name_spec, *nand_env_name_spec;
int main(void) { flash.flash_type=2;
"""
profiles={'Dproduction':(['CONFIG_H3C_HW_D_RAM_READONLY','CONFIG_H3C_HW_D_PRODUCTION_PORT'],'nand_saveenv'),
'Ddiagnostic':(['CONFIG_H3C_HW_D_RAM_READONLY'],'NULL'),'Creadonly':(['CONFIG_H3C_HW_C_RAM_READONLY'],'NULL'),
'Areadonly':(['CONFIG_H3C_HW_A_RAM_READONLY'],'NULL'),'Cproduction':(['CONFIG_H3C_HW_C_PRODUCTION_PORT'],'nand_saveenv'),
'Bproduction':(['CONFIG_TARGET_IPQ5018_H3C_RT3000_B'],'nand_saveenv')}
with tempfile.TemporaryDirectory() as d:
 for name,(defines,expected) in profiles.items():
  c=Path(d)/(name+'.c');exe=Path(d)/name
  c.write_text(pre+selection+chr(10)+'if(saveenv!='+expected+'){return 1;} puts("'+name+' PASS");return 0;}'+chr(10))
  subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',*['-D'+x for x in defines],str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
