// Differential test: web engine (site/assets/engine.js) vs the desktop C engine
// (desktop/src/bb_export.c) on random valid and deliberately broken plans.
// usage: node engine-vs-c.test.js C_HARNESS_BINARY [cases]
'use strict';
const fs = require('fs'), os = require('os'), path = require('path'), crypto = require('crypto');
const { execFileSync } = require('child_process');
const E = require('../site/assets/engine.js');
const BIN = process.argv[2], CASES = +(process.argv[3] || 300);
const T = fs.mkdtempSync(path.join(os.tmpdir(), 'bbdiff-'));
const sha = (u) => crypto.createHash('sha256').update(u).digest('hex');
const R = (n) => Math.floor(Math.random() * n);
const C_STATUS = { 0: 'ok', 2: 'invalid-plan', 5: 'input-mismatch', 8: 'output-mismatch' };

function randomPlan() {
  const inSize = 1 + R(1 << (8 + R(10)));
  const input = crypto.randomBytes(inSize);
  const ops = [], chunks = [], outParts = [];
  const nops = 1 + R(40);
  for (let i = 0; i < nops; i++) {
    const kind = [1, 2, 3][R(3)];
    if (kind === 2) { const sz = 1 + R(3000); const b = crypto.randomBytes(sz); ops.push([2, 0, sz]); chunks.push(b); outParts.push(b); continue; }
    const off = R(inSize), sz = 1 + R(inSize - off);
    const seg = input.subarray(off, off + sz);
    if (kind === 1) { ops.push([1, off, sz]); outParts.push(seg); }
    else { const b = crypto.randomBytes(sz); ops.push([3, off, sz]); chunks.push(b); outParts.push(Buffer.from(seg.map((x, j) => x ^ b[j]))); }
  }
  const data = Buffer.concat(chunks), out = Buffer.concat(outParts);
  return { b: { inSize, inSha: sha(input), outSize: out.length, outSha: sha(out), ops, data: { size: data.length, sha: sha(data) } }, input, data, out };
}
const MUTATIONS = [
  ['none', () => {}],
  ['bad input sha', (c) => { c.b.inSha = sha('x'); }],
  ['bad output sha', (c) => { c.b.outSha = sha('y'); }],
  ['zero input sha', (c) => { c.b.inSha = '0'.repeat(64); }],
  ['zero output sha', (c) => { c.b.outSha = '0'.repeat(64); }],
  ['out size +1', (c) => { c.b.outSize += 1; }],
  ['op zero size', (c) => { const i = R(c.b.ops.length); c.fix = i; c.b.ops[i] = [c.b.ops[i][0], c.b.ops[i][1], 0]; }],
  ['copy past end', (c) => { const i = c.b.ops.findIndex((o) => o[0] !== 2); if (i < 0) return; c.b.ops[i] = [c.b.ops[i][0], c.b.inSize - c.b.ops[i][2] + 1, c.b.ops[i][2]]; }],
  ['literal offset', (c) => { const i = c.b.ops.findIndex((o) => o[0] === 2); if (i < 0) return; c.b.ops[i] = [2, 1, c.b.ops[i][2]]; }],
  ['unknown kind', (c) => { const i = R(c.b.ops.length); c.b.ops[i] = [4, c.b.ops[i][1], c.b.ops[i][2]]; }],
  ['wrong input file', (c) => { c.input = Buffer.from(c.input); c.input[R(c.input.length)] ^= 1; }],
  ['short input file', (c) => { c.input = c.input.subarray(0, c.input.length - 1); }],
];

(async () => {
  let pass = 0, fail = 0, okCount = 0;
  for (let n = 0; n < CASES; n++) {
    const c = randomPlan(), [name, mut] = MUTATIONS[n % MUTATIONS.length];
    mut(c);
    if (!c.input.length) continue;
    const kinds = c.b.ops.map((o) => o[0]);
    const pf = path.join(T, 'plan.txt'), df = path.join(T, 'data.bin'), inf = path.join(T, 'in.bin'), of = path.join(T, 'out' + n + '.bin');
    fs.writeFileSync(pf, `${c.b.inSize} ${c.b.inSha} ${c.b.outSize} ${c.b.outSha} ${c.b.ops.length}\n` +
      c.b.ops.map((o) => `${o[0]} ${o[1]} ${o[2]} ${o[0] === 1 ? 0 : 1}`).join('\n') + '\n');
    fs.writeFileSync(df, c.data); fs.writeFileSync(inf, c.input);
    const cs = C_STATUS[execFileSync(BIN, [pf, df, inf, of]).toString().trim()] || 'c-other';
    let js, bytes = null;
    try { const r = await E.build(c.b, new Uint8Array(c.input), new Uint8Array(c.data), { pureJs: n % 2 === 0 }); js = 'ok'; bytes = Buffer.from(r.bytes); }
    catch (e) { js = e.code || String(e); }
    let same = cs === js;
    if (same && js === 'ok') { same = fs.readFileSync(of).equals(bytes) && bytes.equals(c.out); okCount++; }
    if (fs.existsSync(of)) fs.unlinkSync(of);
    if (same) pass++; else { fail++; console.log(`MISMATCH case ${n} (${name}) C=${cs} JS=${js} kinds=${kinds.join('')}`); }
  }
  console.log(`engine-vs-c: ${pass} agree (${okCount} successful builds byte-identical), ${fail} differ`);
  fs.rmSync(T, { recursive: true, force: true });
  process.exit(fail ? 1 : 0);
})();
