/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BRIDGE_RETURN_ENV_POLICY_H
#define BRIDGE_RETURN_ENV_POLICY_H
#include <string.h>
/* Exact defaults independently extracted from each source's APPSBL. The NX
 * BDF variant is additionally bound to the reviewed R012/mp02.1 observation.
 * Syntax validity alone does not establish a boot contract. */
static int br_return_bootargs_allowed(const char *source, const char *route,
                                      const char *value)
{
    static const char *const sources[] = {
        "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44",
        "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b",
        "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5",
        "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"
    };
    unsigned known = 0;
    if (!source || !route || !value) return 0;
    if (strcmp(route, "config@mp02.1") && strcmp(route, "config@mp03.1") &&
        strcmp(route, "config@mp03.1-c2")) return 0;
    for (unsigned i = 0; i < sizeof(sources)/sizeof(sources[0]); i++)
        if (!strcmp(source, sources[i])) known = 1;
    if (!known) return 0;
    if (!strcmp(value, "console=ttyMSM0,115200n8") ||
        !strcmp(value, "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"))
        return 1;
    return !strcmp(source, sources[3]) && !strcmp(route, "config@mp02.1") &&
        !strcmp(value, "console=ttyMSM0,115200n8 cnss2.bdf_integrated=0x10 cnss2.bdf_pci0=0x60 cnss2.bdf_pci1=0x60 cnss2.skip_radio_bmap=4 loglevel=3");
}
#endif
