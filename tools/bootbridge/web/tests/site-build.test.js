// Rebuilds every bundle whose official input is available from a *built* site
// (assets/catalog.js + data/*.js) with the web engine (WebCrypto and pure-JS
// SHA paths), checks SHA256SUMS and tamper refusals, and optionally compares the
// bytes with the desktop C engine run on the same header + sidecar.
// usage: node site-build.test.js SITE_DIR INPUTS_DIR [C_CATALOG_HARNESS DAT]
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto'), vm = require('vm');
const [SITE, INPUTS, CBIN, DAT] = process.argv.slice(2).map((p) => p && path.resolve(p));
const os = require('os'), { execFileSync } = require('child_process');
const COUT = CBIN ? fs.mkdtempSync(path.join(os.tmpdir(), 'bb-cout-')) : null;
const E = require(path.join(SITE, 'assets/engine.js'));
const CAT = require(path.join(SITE, 'assets/catalog.js'));
const sha = (u) => crypto.createHash('sha256').update(u).digest('hex');
const pool = {};
const SIZES = new Set(CAT.bundles.map((b) => b.inSize));
for (const f of fs.readdirSync(INPUTS)) { const p = path.join(INPUTS, f); const st = fs.statSync(p); if (st.isFile() && SIZES.has(st.size)) pool[sha(fs.readFileSync(p))] = p; }
function loadData(b) {
  let got = null; const ctx = { BBData: { add: (id, b64) => { if (id === b.id) got = b64; } } };
  vm.runInNewContext(fs.readFileSync(path.join(SITE, b.data.file), 'utf8'), ctx);
  return E.fromBase64(got);
}
(async () => {
  let fail = 0, built = 0;
  for (const line of fs.readFileSync(path.join(SITE, 'SHA256SUMS'), 'utf8').trim().split('\n')) {
    const [h, rel] = line.split(/\s+/); if (sha(fs.readFileSync(path.join(SITE, rel))) !== h) { console.log('SUM FAIL', rel); fail++; }
  }
  for (const b of CAT.bundles) {
    if (!E.validPlan(b)) { console.log('INVALID', b.id); fail++; continue; }
    const id = E.identify(CAT, b.inSize, b.inSha);
    if (id.bundle !== b || !id.guide) { console.log('IDENTIFY FAIL', b.id); fail++; }
    const src = pool[b.inSha]; if (!src) { console.log(`${b.id.padEnd(22)} SKIP (no official input)`); continue; }
    const input = new Uint8Array(fs.readFileSync(src)), data = loadData(b);
    if (CBIN) {
      const st = execFileSync(CBIN, [DAT, b.id, src, path.join(COUT, b.id + '.bin')]).toString().trim();
      if (st !== '0') { console.log(`${b.id} C engine status ${st}`); fail++; continue; }
    }
    for (const pureJs of [false, true]) {
      const t = Date.now();
      const r = await E.build(b, input, data, { pureJs });
      let ok = r.sha === b.outSha && r.bytes.length === b.outSize, note = '';
      if (COUT) { const c = fs.readFileSync(path.join(COUT, b.id + '.bin')); ok = ok && Buffer.from(r.bytes).equals(c); note = ' == C engine output'; }
      console.log(`${b.id.padEnd(22)} ${pureJs ? 'pure-js' : 'subtle '} ${ok ? 'OK' : 'FAIL'} ${Date.now() - t} ms${ok ? note : ''}`);
      if (!ok) fail++; else built++;
    }
    const bad = new Uint8Array(input); bad[12345] ^= 1;
    try { await E.build(b, bad, data); console.log('TAMPER ACCEPTED'); fail++; } catch (e) { if (e.code !== 'input-mismatch') { fail++; console.log('wrong code', e.code); } }
    const badd = new Uint8Array(data); badd[7] ^= 1;
    try { await E.build(b, input, badd); console.log('DATA TAMPER ACCEPTED'); fail++; } catch (e) { if (e.code !== 'data-mismatch') { fail++; console.log('wrong code', e.code); } }
    const wrongOut = Object.assign({}, b, { outSha: 'f'.repeat(64) });
    try { await E.build(wrongOut, input, data); console.log('OUTPUT PIN IGNORED'); fail++; } catch (e) { if (e.code !== 'output-mismatch') { fail++; console.log('wrong code', e.code); } }
  }
  if (COUT) fs.rmSync(COUT, { recursive: true, force: true });
  console.log(`site-build: ${built} builds verified, ${fail} failures`);
  process.exit(fail ? 1 : 0);
})();
