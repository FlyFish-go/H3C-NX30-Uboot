#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'common/autoboot.c').read_text()
start=source.index('#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)', source.index('void autoboot_command(const char *s)'))
end=source.index('\n\tdebug(\"### main_loop', start)
override=source[start:end]
profiles={
 'c-production':('#define CONFIG_H3C_HW_C_PRODUCTION_PORT 1\n', 'bootipq', 3, 'eth1'),
 'c-readonly':('#define CONFIG_H3C_HW_C_RAM_READONLY 1\n', 'httpd', 3, 'eth1'),
 'a-production':('#define CONFIG_H3C_HW_A_PRODUCTION_PORT 1\n', 'httpd', 0, ''),
}
for profile,(macros, expected_cmd, expected_delay, expected_ethact) in profiles.items():
    harness=r'''#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define setenv mock_setenv
#define CONFIG_BOOTDELAY 3
''' + macros + r'''
static int stored_bootdelay;
static char selected_ethact[16];
static int mock_setenv(const char *key, const char *value) {
    if (!strcmp(key, "ethact")) { snprintf(selected_ethact,sizeof selected_ethact,"%s",value); return 0; }
    return -1;
}
static void apply(const char *input) {
    const char *s=input;
''' + override + r'''
    if (strcmp(s, EXPECTED_CMD) || stored_bootdelay != EXPECTED_DELAY) {
        fprintf(stderr,"override result cmd=%s delay=%d\\n",s,stored_bootdelay); exit(1);
    }
    if (strcmp(selected_ethact, EXPECTED_ETHACT)) {
        fprintf(stderr,"ethact=%s\\n",selected_ethact); exit(1);
    }
}
int main(void) {
    stored_bootdelay=3;
    apply("bootipq");
    return 0;
}
'''.replace('EXPECTED_CMD', '"'+expected_cmd+'"').replace('EXPECTED_DELAY', str(expected_delay)).replace('EXPECTED_ETHACT','"'+expected_ethact+'"')
    with tempfile.TemporaryDirectory(prefix='autoboot-profile-') as td:
        c=Path(td)/'test.c'; exe=Path(td)/'test'; c.write_text(harness)
        subprocess.run(['gcc','-std=gnu99','-Wall','-Wextra','-Werror','-Wno-unused-function',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
print('C production preserves bootipq/bootdelay and eth1; A and C readonly recovery overrides PASS')
