#!/usr/bin/env python3
"""Check the deferred NOR rescue controller binds after its DOM exists."""
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HTML = ROOT / 'failsafe/fsdata/nor_rescue.html'
REQUIRED = {'image', 'validate', 'review', 'candidate', 'write', 'status', 'nand-probe', 'nand-retry', 'nand-status'}

class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.deferred = False
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        if tag == 'script' and attrs.get('src') == '/main.js':
            self.deferred = 'defer' in attrs

page = Page()
page.feed(HTML.read_text())
if not page.deferred:
    raise SystemExit('controller script must be deferred until parsing finishes')
if not REQUIRED.issubset(page.ids):
    raise SystemExit('missing DOM ids: ' + ', '.join(sorted(REQUIRED - page.ids)))

print('NOR_RESCUE_UI_DEFERRED_DOM_ORDER_HOSTTEST_PASS')
