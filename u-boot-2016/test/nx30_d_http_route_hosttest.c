#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <nx30_d_http_route_core.h>
int main(void) {
 assert(nx30_d_http_route_allowed(1,0,"/main.js"));
 assert(nx30_d_http_route_allowed(1,0,"/service/boot/default/failure"));
 assert(!nx30_d_http_route_allowed(0,1,"/service/boot/default/failure"));
 assert(!nx30_d_http_route_allowed(1,0,"/service/boot/default/failure-extra"));
 assert(nx30_d_http_route_allowed(1,0,"/style.css"));
 assert(nx30_d_http_route_allowed(1,0,"/backup.html"));
 assert(nx30_d_http_route_allowed(1,0,"/syslog/poll"));
 assert(!nx30_d_http_route_allowed(0,1,"/backup.html"));
 assert(!nx30_d_http_route_allowed(0,1,"/main.js"));
 assert(!nx30_d_http_route_allowed(1,0,"/service/env/commit"));
 assert(nx30_d_http_route_allowed(0,1,"/service/env/commit"));
 assert(!nx30_d_http_route_allowed(0,1,"/env/set"));
 assert(!nx30_d_http_route_allowed(0,1,"/webterm/exec"));
 assert(!nx30_d_http_route_allowed(0,1,"/upgrade"));
 assert(!nx30_d_http_route_allowed(0,1,"/art"));
 assert(!nx30_d_http_route_allowed(1,0,"/main.js/../service/env/commit"));
 assert(!nx30_d_http_route_allowed(1,0,"/main.js-extra"));
 assert(!nx30_d_http_route_allowed(0,0,"/"));
 assert(!nx30_d_http_route_allowed(1,1,"/"));
 assert(!nx30_d_http_route_allowed(1,0,NULL));
 puts("PASS_ORIGINAL_UI_GET_VIEWS_EXISTING_POST_BOUNDARIES");
 return 0;
}
