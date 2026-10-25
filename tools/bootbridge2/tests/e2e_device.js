// SPDX-License-Identifier: GPL-2.0-or-later
// Browser test of the device page against a running bridge.
//   node e2e-device.js BRIDGE_URL SHOTS_DIR [--install]
// Without --install it stops before writing anything except the backup.
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');

const [BASE, SHOTS] = process.argv.slice(2);
const INSTALL = process.argv.includes('--install');
let pass = 0, fail = 0;
const ok = (name, cond, extra) => {
  cond ? pass++ : fail++;
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra ? ' — ' + extra : ''));
};
fs.mkdirSync(SHOTS, { recursive: true });

(async () => {
  const launch = process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {};
  if (process.env.BB_BROWSER_PROXY) launch.proxy = { server: process.env.BB_BROWSER_PROXY };
  const browser = await chromium.launch(launch);
  const ctx = await browser.newContext({ acceptDownloads: true, viewport: { width: 1280, height: 900 }, locale: 'zh-CN' });
  const page = await ctx.newPage();
  const problems = [];
  page.on('console', (m) => { if (m.type() === 'error') problems.push(m.text()); });
  page.on('pageerror', (e) => problems.push('pageerror: ' + e.message));
  const text = async (sel) => (await page.textContent(sel)) || '';

  await page.goto(BASE + '/');
  await page.waitForFunction(() => document.getElementById('badge').textContent !== '检查中', null, { timeout: 30000 });
  await page.screenshot({ path: path.join(SHOTS, 'device-1.png'), fullPage: true });
  ok('device recognised', /HW-/.test(await text('#device-name')), await text('#device-name'));
  ok('return button enabled', await page.isEnabled('#return-button'));

  // Return dialog: cancel must not change anything.
  await page.click('#return-button');
  ok('return asks for confirmation', await page.isVisible('#confirm'));
  await page.click('#confirm-no');
  ok('cancel closes the dialog', await page.isHidden('#confirm'));

  // Backup step.
  if ((await text('#main-button')) === '生成备份') {
    await page.click('#main-button');
    await page.waitForFunction(() => document.getElementById('main-button').textContent !== '生成备份',
      null, { timeout: 180000 });
  }
  ok('backup link shown', await page.isVisible('#backup-link'));
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('#backup-link')]);
  const backup = fs.readFileSync(await dl.path());
  ok('backup downloads', dl.suggestedFilename() === 'bootbridge-recovery-backup.zip' &&
     backup.length > (40 << 20), dl.suggestedFilename());
  const backupState = await page.evaluate(() => fetch('/api/state').then(r => r.json()));
  const verified = backupState.backup.ready && backup.length === backupState.backup.bytes &&
    crypto.createHash('sha256').update(backup).digest('hex') === backupState.backup.sha256;
  ok('download matches verified backup size and SHA-256', verified);
  if (!verified) throw new Error('Backup verification failed; installation was not attempted');
  await dl.saveAs(path.join(SHOTS, 'bootbridge-recovery-backup.zip'));
  await page.screenshot({ path: path.join(SHOTS, 'device-2.png'), fullPage: true });
  ok('install offered', (await text('#main-button')) === '安装 U-Boot', await text('#main-button'));

  if (INSTALL) {
    await page.click('#main-button');
    await page.click('#confirm-yes');
    await page.waitForFunction(() => document.getElementById('main-button').textContent === '重启' ||
      !document.getElementById('notice').hidden, null, { timeout: 180000 });
    await page.screenshot({ path: path.join(SHOTS, 'device-3.png'), fullPage: true });
    ok('install finished', (await text('#main-button')) === '重启', await text('#message'));
    if (process.env.BB_EXPECTED_APPSBL_SHA256) {
      const state = await page.evaluate(() => fetch('/api/state').then(r => r.json()));
      ok('both APPSBL copies match pinned candidate',
        state.facts.appsbl_sha256.length === 2 &&
        state.facts.appsbl_sha256.every(h => h === process.env.BB_EXPECTED_APPSBL_SHA256));
    }
  }
  ok('no console errors', problems.length === 0, problems.join(' | '));
  await browser.close();
  console.log(pass + ' passed, ' + fail + ' failed');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
