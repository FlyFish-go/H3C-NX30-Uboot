import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer
class H(BaseHTTPRequestHandler):
    def do_POST(self):
        b = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        sys.stderr.write('REQ %s\n' % json.dumps(b))
        if b['gwVer'] == 'NX30V100R011':
            r = '{"FileSize":"","NextAskTime":0,"RebootTime":"","RebootType":1,"VerURL":"","gwBestVer":"NX30V100R011","retCode":0}'
        else:
            r = '{"FileSize":"","NextAskTime":0,"RebootTime":"","RebootType":1,"VerURL":"http:\\/\\/h3c-magic.oss-cn-hangzhou.aliyuncs.com\\/gateway-version\\/RC3000V100R013L01.bin?Expires=1\\u0026x=1","gwBestVer":"NX30V100R012","retCode":0}'
        d = r.encode(); self.send_response(200); self.send_header('Content-Type','application/json;charset=utf-8'); self.send_header('Content-Length',str(len(d))); self.end_headers(); self.wfile.write(d)
    def log_message(self,*a): pass
HTTPServer(('127.0.0.1',8765),H).serve_forever()
