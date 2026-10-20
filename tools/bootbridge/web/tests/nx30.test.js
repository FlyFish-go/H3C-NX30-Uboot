// Unit tests for site/assets/nx30.js with a scripted fake of H3C's version API.
'use strict';
const N = require('../site/assets/nx30.js');
const C = require('./_load-commands.js');
let pass = 0, fail = 0;
const eq = (name, a, b) => { const ok = JSON.stringify(a) === JSON.stringify(b); ok ? pass++ : fail++; if (!ok) console.log('FAIL', name, JSON.stringify(a), '!=', JSON.stringify(b)); };
const fake = (...replies) => { const seen = []; const f = async (url, init) => { seen.push(JSON.parse(init.body)); const r = replies.shift();
  if (r instanceof Error) throw r; if (typeof r === 'number') return { ok: false, status: r, json: async () => ({}) };
  return { ok: true, status: 200, json: async () => { if (r === 'garbage') throw new SyntaxError('x'); return r; } }; }; f.seen = seen; return f; };
(async () => {
  const sn = N.normalize(' test 0000-0000 0000 0000 ');
  eq('normalize', sn, 'TEST0000000000000000');
  eq('group', N.group(sn), 'TEST 0000 0000 0000 0000');
  eq('check ok', N.check(sn).ok, true);
  eq('check short', N.check('ABC'), { ok: false, reason: 'length', length: 3 });
  eq('check chars', N.check('TEST_000000000000000').reason, 'chars');
  eq('check lower is normalized', N.check(N.normalize('abcd0000000000000000')).ok, true);
  let f = fake({ retCode: 0, gwBestVer: 'NX30V100R011' }, { retCode: 0, gwBestVer: 'NX30V100R012' });
  eq('ok after R011 step', await N.precheck(sn, { fetchImpl: f }), { state: 'ok' });
  eq('asked R011 then R012', f.seen.map((b) => b.gwVer), ['NX30V100R011', 'NX30V100R012']);
  eq('sn sent verbatim', f.seen[0].gwSn, sn);
  eq('ok first', await N.precheck(sn, { fetchImpl: fake({ retCode: 0, gwBestVer: 'NX30V100R012' }) }), { state: 'ok' });
  eq('unknown', await N.precheck(sn, { fetchImpl: fake({ retCode: 24 }) }), { state: 'unknown-sn' });
  eq('format', await N.precheck(sn, { fetchImpl: fake({ retCode: 20 }) }), { state: 'bad-format' });
  eq('other', await N.precheck(sn, { fetchImpl: fake({ retCode: 0, gwBestVer: 'NX30V100R013' }, { retCode: 0, gwBestVer: 'NX30V100R013' }) }), { state: 'other-version', version: 'NX30V100R013' });
  eq('rejected', await N.precheck(sn, { fetchImpl: fake({ retCode: 7 }, { retCode: 7 }) }), { state: 'rejected', code: 7 });
  eq('http', await N.precheck(sn, { fetchImpl: fake(503) }), { state: 'http', status: 503 });
  eq('garbage', await N.precheck(sn, { fetchImpl: fake('garbage') }), { state: 'bad-response' });
  eq('network', (await N.precheck(sn, { fetchImpl: fake(new TypeError('Failed to fetch')) })).state, 'network');
  eq('bad sn never sent', await N.precheck('X', { fetchImpl: () => { throw new Error('must not call'); } }), { state: 'bad-format' });
  const hang = () => new Promise((_, rej) => {});
  const slow = (url, init) => new Promise((_, rej) => init.signal.addEventListener('abort', () => { const e = new Error('aborted'); e.name = 'AbortError'; rej(e); }));
  eq('timeout', await N.precheck(sn, { fetchImpl: slow, timeoutMs: 50 }), { state: 'network', detail: 'timeout' });
  for (const key of ['ps51', 'sh']) {
    const cmd = N.command(C[key], sn);
    eq(key + ' sn embedded once', cmd.split(sn).length, 2);
    eq(key + ' no placeholder left', cmd.includes('__SN__'), false);
  }
  let threw = false; try { N.command(C.ps51, "AAAA'; rm -rf ~; '"); } catch { threw = true; }
  eq('command refuses unvalidated SN', threw, true);
  eq('platform win', N.platform({ platform: 'Win32', userAgent: '' }), 'ps51');
  eq('platform mac', N.platform({ platform: 'MacIntel', userAgent: '' }), 'sh');
  console.log(`nx30: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
