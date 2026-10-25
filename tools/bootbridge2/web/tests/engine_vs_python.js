// SPDX-License-Identifier: GPL-2.0-or-later
// Rebuild every bundle with the browser engine + injector (in Node) and compare
// with the image build/inject_pack.py produced.
//   node engine_vs_python.js SITE_DIR OFFICIAL_DIR BUILDS_DIR
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const [site, inputs, builds] = process.argv.slice(2);
const ctx = { window: {}, self: {} };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(site, 'assets/catalog.js'), 'utf8'), ctx);
const E = require(path.resolve(site, 'assets/engine.js'));
const INJ = require(path.resolve(site, 'assets/inject.js'));
const CAT = ctx.window.BB_CATALOG;

function dataOf(file) {
  const t = fs.readFileSync(path.join(site, file), 'utf8');
  const m = /^BBData\.add\("[^"]+","([A-Za-z0-9+/=]*)"\);\n$/.exec(t);
  if (!m) throw new Error('bad data script ' + file);
  return E.fromBase64(m[1]);
}

(async () => {
  const pack = dataOf(CAT.pack.data.file);
  let failed = 0;
  for (const b of CAT.bundles) {
    const src = path.join(inputs, b.id + '-original.bin');
    if (!fs.existsSync(src)) { console.log('SKIP', b.id); continue; }
    const input = new Uint8Array(fs.readFileSync(src));
    const r = await E.build(b, input, dataOf(b.data.file), { pureJs: true });
    const out = INJ.apply(r.bytes, b.layout, pack);
    const sha = await E.sha256(out, { pureJs: true });
    const ref = fs.readFileSync(path.join(builds, b.id, 'bridge.bin'));
    const same = Buffer.compare(Buffer.from(out), ref) === 0;
    console.log(same ? 'OK  ' : 'FAIL', b.id, sha);
    if (!same) failed++;
  }
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
