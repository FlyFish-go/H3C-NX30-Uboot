/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_HTTP_ROUTE_CORE_H__
#define __NX30_D_HTTP_ROUTE_CORE_H__
/* GET views/read-only status; POST is limited to the existing service APIs.
 * Caller supplies mutually exclusive method flags and a normalized URI. */
static inline int nx30_d_http_route_allowed(int is_get, int is_post, const char *uri)
{
 static const char *const get_routes[] = {"/","/index.html","/cgi-bin/luci","/cgi-bin/luci/","/main.js","/style.css","/backup.html","/env.html","/network.html","/settings.html","/reboot.html","/syslog.html","/webterm.html","/mac.html","/firmware.html","/uboot.html","/art.html","/cdt.html","/ptable.html","/simg.html","/mibib.html","/initramfs.html","/version","/sysinfo","/nand/probe","/service/status","/service/boot/default/failure","/service/env","/service/network/info","/service/slots","/service/logs","/service/buttons","/boot/slots","/syslog/poll"};
 static const char *const post_routes[] = {"/service/env/prepare","/service/env/commit","/service/env/abort","/service/network/ram","/service/backup","/service/console","/service/led","/service/boot/slot","/service/boot/default/prepare","/service/boot/default/commit","/service/boot/default/abort","/service/firmware/validate","/service/firmware/write","/service/uboot/validate","/service/uboot/write","/service/reboot"};
 const char *const *routes; unsigned int count, i;
 if (!uri || (is_get && is_post) || (!is_get && !is_post)) return 0;
 routes = is_get ? get_routes : post_routes;
 count = is_get ? sizeof(get_routes)/sizeof(get_routes[0]) : sizeof(post_routes)/sizeof(post_routes[0]);
 for (i=0; i<count; i++) if (!strcmp(uri, routes[i])) return 1;
 return 0;
}
#endif
