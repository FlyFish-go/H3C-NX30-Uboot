#!/usr/bin/env python3
"""Compile the real httpd request-dispatch guard against the actual route table."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output-dir');a=p.parse_args()
out=Path(a.output_dir) if a.output_dir else Path(tempfile.mkdtemp(prefix='nx30-dispatch-'))
out.mkdir(parents=True,exist_ok=True)
source=(root/'net/httpd.c').read_text().split('static int httpd_handle_request',1)[1]
guard=source.split('#if defined(CONFIG_H3C_HW_D_NAND_SERVICE)',1)[1].split('#elif defined(CONFIG_H3C_HW_D_NOR_RESCUE)',1)[0]
guard='#if defined(CONFIG_H3C_HW_D_NAND_SERVICE)'+guard+'\n#endif\n'
code=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <nx30_d_http_route_core.h>
#define CONFIG_H3C_HW_D_NAND_SERVICE 1
enum { HTTP_GET,HTTP_POST,HTTP_RESP_STD };
struct request {int method;void *urih;};
struct response {int status;const char *data;unsigned size;struct {int code,connection_close;const char *content_type;} info;};
struct pdata {const char *uri;struct request request;struct response response;};
static int handler_calls;
static int dispatch(int method,const char *uri) {
 struct pdata storage={0},*pdata=&storage;
 struct request *req=&pdata->request;
 bool parse_post_data_later=true;
 req->method=method;pdata->uri=uri;
'''+guard+r'''
 {handler_calls++;pdata->response.info.code=200;}
 (void)parse_post_data_later;
 return pdata->response.info.code;
}
int main(void) {
 assert(dispatch(HTTP_GET,"/service/boot/default/failure")==200 && handler_calls==1);
 assert(dispatch(HTTP_POST,"/service/boot/default/failure")==403 && handler_calls==1);
 assert(dispatch(HTTP_GET,"/service/boot/default/failure-extra")==403 && handler_calls==1);
 assert(dispatch(HTTP_POST,"/service/backup")==200 && handler_calls==2);
 puts("actual httpd dispatcher: GET failure reaches callback; POST/suffix rejected; backup POST reachable PASS");
 return 0;
}
'''
(out/'failure-dispatch.c').write_text(code)
subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-idirafter',str(root/'include'),str(out/'failure-dispatch.c'),'-o',str(out/'failure-dispatch')],check=True)
subprocess.run([str(out/'failure-dispatch')],check=True)
