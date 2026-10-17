#!/usr/bin/env python3
"""Keep the NX30 NOR AUTOECC browser request aligned with the HTTP handler."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HTTPD = (ROOT / 'net/httpd.c').read_text()
CONTROLLER = (ROOT / 'failsafe/fsdata/nor_rescue.js').read_text()
HANDLER = (ROOT / 'failsafe/modules/nor_nand_autoecc.c').read_text()

# The browser sends a bodyless POST; retries add only the retry header.
probe_fetch = re.search(
    r"const options = \{method:'POST'\};(?P<retry>.*?)"
    r"fetch\('/nand/probe', options\)", CONTROLLER, re.S)
if not probe_fetch:
    raise SystemExit('AUTOECC browser must POST /nand/probe')
if 'body:' in probe_fetch.group(0):
    raise SystemExit('AUTOECC probe must not send a POST body')
if "'X-NX30-NAND-Probe-Retry':'1'" not in probe_fetch.group('retry'):
    raise SystemExit('retry must remain a header-only request')

# The HTTP length gate permits exactly the zero-length probe exception, while
# the receive path rejects an accidental inline body on that route.
if not re.search(
    r"!httpd_upload_length_allowed\(pdata->uri, parsed_length\).*?"
    r"!strcmp\(pdata->uri, \"/nand/probe\"\).*?parsed_length == 0",
    HTTPD, re.S):
    raise SystemExit('HTTP length gate must allow only empty /nand/probe POST')
if not re.search(
    r"!strcmp\(pdata->uri, \"/nand/probe\"\).*?"
    r"pdata->payload_size == 0 && pdata->bufsize != hdr_size",
    HTTPD, re.S):
    raise SystemExit('HTTP receive path must reject a body on empty /nand/probe POST')
if '"/nand/probe"' not in HTTPD or 'req->method == HTTP_POST' not in HTTPD:
    raise SystemExit('NOR HTTP route allowlist must retain POST /nand/probe')

# Verify the registered handler accepts an empty POST and declares JSON.
if 'httpd_register_uri_handler(inst, "/nand/probe", &nor_nand_probe_handler, NULL)' not in HANDLER:
    raise SystemExit('AUTOECC handler must be registered at /nand/probe')
if 'response->info.content_type = "application/json";' not in HANDLER:
    raise SystemExit('AUTOECC handler must return application/json')
if '!request || request->method != HTTP_POST || request->form.count' not in HANDLER:
    raise SystemExit('AUTOECC handler must require a bodyless POST')

# Surface HTTP text errors instead of throwing a JSON parser SyntaxError.
if 'const body = await response.text();' not in CONTROLLER or 'JSON.parse(body)' not in CONTROLLER:
    raise SystemExit('probe response must parse text with an explicit JSON fallback')
if 'HTTP ${response.status} 返回非 JSON' not in CONTROLLER:
    raise SystemExit('non-JSON HTTP responses must retain status and server detail')

print('NOR AUTOECC route / POST body / application-json contract: PASS')
