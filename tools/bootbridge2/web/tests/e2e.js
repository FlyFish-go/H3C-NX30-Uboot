// SPDX-License-Identifier: GPL-2.0-or-later
// Browser test of the generator site: pick each model, load its official
// firmware, walk the wizard, generate, and compare the download with the
// image build/inject_pack.py produced.
//   node e2e.js SITE_URL OFFICIAL_DIR BUILDS_DIR SHOTS_DIR
// Needs the playwright module (NODE_PATH) and a Chromium (CHROMIUM or the
// Playwright default).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const [BASE, INPUTS, BUILDS, SHOTS] = process.argv.slice(2);
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
let pass = 0, fail = 0;
const ok = (name, cond, extra) => {
  cond ? pass++ : fail++;
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra ? ' — ' + extra : ''));
};
fs.mkdirSync(SHOTS, { recursive: true });

(async () => {
  const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
  for (const model of ['RC3000', 'RT3000', 'NX30', 'RW3000']) {
    const ctx = await browser.newContext({ acceptDownloads: true, viewport: { width: 1440, height: 900 }, locale: 'zh-CN' });
    const page = await ctx.newPage();
    const problems = [];
    page.on('console', (m) => { if (m.type() === 'error') problems.push(m.text()); });
    page.on('pageerror', (e) => problems.push('pageerror: ' + e.message));
    await page.goto(BASE + '/index.html');
    ok(model + ': page boots', await page.isHidden('#boot-fail'));
    await page.click('[data-model="' + model + '"]');
    await page.click('[data-act="next"]');
    await page.setInputFiles('#picker', path.join(INPUTS, model + '-original.bin'));
    await page.waitForSelector('.result.ok, .result.err', { timeout: 120000 });
    ok(model + ': official firmware accepted', await page.isVisible('.result.ok'));
    await page.click('[data-act="next"]');
    await page.click('[data-act="toggle-up"]');
    await page.click('[data-act="next"]');
    if (model === 'RC3000') await page.screenshot({ path: path.join(SHOTS, 'generate-ready.png') });
    const [dl] = await Promise.all([
      page.waitForEvent('download', { timeout: 300000 }),
      page.click('[data-act="gen"]'),
    ]);
    const got = fs.readFileSync(await dl.path());
    const want = fs.readFileSync(path.join(BUILDS, model, 'bridge.bin'));
    ok(model + ': download equals the reference image', Buffer.compare(got, want) === 0,
       dl.suggestedFilename() + ' ' + sha(got).slice(0, 16));
    if (model === 'RC3000') await page.screenshot({ path: path.join(SHOTS, 'generate-done.png') });
    ok(model + ': no console errors', problems.length === 0, problems.join(' | '));
    await ctx.close();
  }
  await browser.close();
  console.log(pass + ' passed, ' + fail + ' failed');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
