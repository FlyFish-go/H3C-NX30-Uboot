// End-to-end UI test of a built site with Playwright + Chromium.
// usage: node e2e.js SITE_DIR HTTP_BASE INPUTS_DIR SHOTS_DIR [--offline]
// Uses the real H3C OSS for the RC3000 direct download unless --offline.
'use strict';
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const [SITE, BASE, INPUTS, SHOTS] = process.argv.slice(2, 6).map((p, i) => (i === 1 ? p : path.resolve(p)));
const OFFLINE = process.argv.includes('--offline');
const CAT = require(path.join(SITE, 'assets/catalog.js'));
const sha = (p) => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const pin = (model) => CAT.bundles.find((b) => b.model === model).outSha;
// official inputs are located by size + SHA-256, whatever their file names are
const input = (model) => {
  const g = CAT.guide.find((x) => x.id === model);
  for (const f of fs.readdirSync(INPUTS)) { const p = path.join(INPUTS, f); const st = fs.statSync(p); if (st.isFile() && st.size === g.bytes && sha(p) === g.sha) return p; }
  throw new Error('official input for ' + model + ' not found in ' + INPUTS);
};
let pass = 0, fail = 0;
const ok = (name, cond, extra) => { cond ? pass++ : fail++; console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra ? ' — ' + extra : '')); };
fs.mkdirSync(SHOTS, { recursive: true });

async function newPage(browser, opts) {
  const ctx = await browser.newContext(Object.assign({ acceptDownloads: true, viewport: { width: 1440, height: 900 }, locale: 'zh-CN' }, opts || {}));
  const page = await ctx.newPage();
  page.problems = [];
  page.on('console', (m) => { if (m.type() === 'error' || /Content Security Policy/i.test(m.text())) page.problems.push(m.text()); });
  page.on('pageerror', (e) => page.problems.push('pageerror: ' + e.message));
  return { ctx, page };
}
const next = (page) => page.click('[data-act="next"]');

(async () => {
  const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || '/usr/local/bin/chromium' });

  /* ---- A: http, RC3000 direct download -> generate ---- */
  {
    const { ctx, page } = await newPage(browser);
    await ctx.grantPermissions(['clipboard-read', 'clipboard-write'], { origin: BASE });
    await page.goto(BASE + '/index.html');
    ok('boot ok (http)', await page.isHidden('#boot-fail'));
    await page.screenshot({ path: path.join(SHOTS, '01-step1.png') });
    await page.click('[data-model="RC3000"]');
    await next(page);
    if (!OFFLINE) {
      await page.click('[data-act="dl"]');
      await page.waitForSelector('.result.ok, .result.err', { timeout: 240000 });
      ok('RC3000 direct download verified', await page.isVisible('.result.ok'), await page.textContent('.result'));
      await page.screenshot({ path: path.join(SHOTS, '02-step2-downloaded.png') });
      const [d] = await Promise.all([page.waitForEvent('download'), page.click('[data-act="save-fw"]')]);
      ok('save official firmware', d.suggestedFilename() === 'RC3000V100R013L01.bin' && sha(await d.path()) === CAT.guide.find((g) => g.id === 'RC3000').sha);
    } else {
      await page.setInputFiles('#picker', input('RC3000'));
      await page.waitForSelector('.result.ok', { timeout: 60000 });
    }
    await next(page);
    ok('step3 next disabled before confirm', await page.isDisabled('[data-act="next"]'));
    await page.click('[data-act="toggle-up"]');
    await page.screenshot({ path: path.join(SHOTS, '03-step3.png') });
    await next(page);
    await page.screenshot({ path: path.join(SHOTS, '04-step4-ready.png') });
    const [d2] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('[data-act="gen"]')]);
    const f2 = await d2.path();
    ok('bridge generated over http', d2.suggestedFilename() === 'RC3000-RC3000V100R013L01-BootBridge.bin' && sha(f2) === pin('RC3000'), d2.suggestedFilename());
    await page.waitForSelector('.result.ok');
    await page.screenshot({ path: path.join(SHOTS, '05-step4-done.png') });
    await page.click('[data-copy="outsha"]');
    ok('copy output sha', (await page.evaluate(() => navigator.clipboard.readText())) === pin('RC3000'));
    await next(page); await page.screenshot({ path: path.join(SHOTS, '06-step5.png') });
    await next(page); await page.screenshot({ path: path.join(SHOTS, '07-step6.png') });
    ok('no finish button on step 6', (await page.$('[data-act="next"]')) === null);
    ok('step 6 recovery note (10.1 + DHCP)', await page.$eval('[data-k="s6rec"]', (e) => /192\.168\.10\.1/.test(e.textContent) && /192\.168\.10\.x/.test(e.textContent)).catch(() => false));
    ok('no console/CSP problems (A)', page.problems.length === 0, page.problems.join(' | '));
    await ctx.close();
  }

  /* ---- B/C/D: drop auto-switch, bad file, NX30 SN flow ---- */
  {
    const { ctx, page } = await newPage(browser);
    await ctx.grantPermissions(['clipboard-read', 'clipboard-write'], { origin: BASE });
    await page.goto(BASE + '/index.html');
    await next(page); // NX30 is first
    await page.setInputFiles('#picker', input('RT3000'));
    await page.waitForSelector('.result.ok', { timeout: 60000 });
    ok('drop RT3000 auto-switches model', (await page.evaluate(() => window.__bb.state.model)) === 'RT3000');
    await page.click('[data-act="reset-file"]');
    const junk = path.join(SHOTS, 'junk.bin'); fs.writeFileSync(junk, crypto.randomBytes(1000));
    await page.setInputFiles('#picker', junk);
    await page.waitForSelector('.result.warn');
    ok('unknown file rejected', await page.isVisible('.result.warn'));
    await page.screenshot({ path: path.join(SHOTS, '08-bad-file.png') });
    await page.click('[data-act="reset-file"]');
    await page.click('[data-go="1"]'); await page.click('[data-model="NX30"]'); await next(page);
    await page.fill('#sn', 'abc');
    ok('short SN keeps button disabled', await page.isDisabled('#sncheck'));
    await page.fill('#sn', 'test 0000 0000 0000 0000');
    ok('20-char SN enables button', await page.isEnabled('#sncheck'));
    if (!OFFLINE) {
      await page.click('#sncheck');
      await page.waitForSelector('.status:not(.busy)', { timeout: 30000 });
      ok('real H3C: synthetic SN -> unknown', /不认识/.test(await page.textContent('.status')), await page.textContent('.status'));
      await page.screenshot({ path: path.join(SHOTS, '09-nx30-unknown.png') });
      await page.click('#sn'); await page.fill('#sn', 'TEST0000000000000000');
    }
    const seen = [];
    await page.route('https://magic.h3c.com/**', async (route) => {
      const req = route.request();
      const cors = { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': 'content-type', 'Access-Control-Allow-Methods': 'POST' };
      if (req.method() === 'OPTIONS') return route.fulfill({ status: 204, headers: cors });
      const b = JSON.parse(req.postData()); seen.push(b);
      return route.fulfill({ status: 200, headers: Object.assign({ 'Content-Type': 'application/json' }, cors), body: JSON.stringify({ retCode: 0, gwBestVer: b.gwVer === 'NX30V100R011' ? 'NX30V100R012' : 'NX30V100R012' }) });
    });
    await page.click('#sncheck');
    await page.waitForSelector('#cmdtext', { timeout: 15000 });
    ok('mock H3C ok -> command shown', /TEST0000000000000000/.test(await page.textContent('#cmdtext')));
    ok('precheck sent normalized SN', seen.length >= 1 && seen[0].gwSn === 'TEST0000000000000000');
    await page.click('[data-tab="ps51"]');
    await page.click('[data-copy="cmd"]');
    const clip = await page.evaluate(() => navigator.clipboard.readText());
    ok('copied PowerShell command', clip.startsWith('& {') && clip.includes("$sn='TEST0000000000000000'"));
    await page.screenshot({ path: path.join(SHOTS, '10-nx30-command-win.png'), fullPage: true });
    await page.click('[data-tab="sh"]');
    ok('sh tab', (await page.textContent('#cmdtext')).includes("sn='TEST0000000000000000'"));
    const diag = await page.evaluate(() => window.__bb.diag());
    ok('diagnostics exclude SN', !diag.includes('TEST0000'), diag.split('\n').slice(-2).join(' / '));
    await page.click('[data-act="open-modal"]');
    await page.click('[data-set="theme"][data-val="dark"]');
    await page.screenshot({ path: path.join(SHOTS, '11-settings-dark.png') });
    await page.keyboard.press('Escape');
    await page.click('[data-set="lang"][data-val="en"]');
    await page.screenshot({ path: path.join(SHOTS, '12-nx30-en-dark.png'), fullPage: true });
    ok('no console/CSP problems (B-D)', page.problems.filter((p) => !/magic\.h3c\.com|net::ERR/.test(p)).length === 0, page.problems.join(' | '));
    await ctx.close();
  }

  /* ---- E: file:// (script-tag data loading) ---- */
  {
    const { ctx, page } = await newPage(browser);
    await page.goto('file://' + path.join(SITE, 'index.html'));
    ok('boot ok (file://)', await page.isHidden('#boot-fail'));
    await page.setInputFiles('#picker', input('RW3000'));
    await page.waitForSelector('.result.ok', { timeout: 60000 });
    await page.click('[data-go="4"]');
    const [d] = await Promise.all([page.waitForEvent('download', { timeout: 120000 }), page.click('[data-act="gen"]')]);
    ok('bridge generated from file://', sha(await d.path()) === pin('RW3000'));
    ok('no console/CSP problems (file://)', page.problems.length === 0, page.problems.join(' | '));
    await ctx.close();
  }

  /* ---- F: mobile layout + broken data ---- */
  {
    const { ctx, page } = await newPage(browser, { viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
    await page.goto(BASE + '/index.html');
    await page.screenshot({ path: path.join(SHOTS, '13-mobile-step1.png'), fullPage: true });
    await page.route('**/data/*.js', (r) => r.fulfill({ status: 200, contentType: 'text/javascript', body: 'BBData.add("x","");' }));
    await page.setInputFiles('#picker', input('RC3000'));
    await page.waitForSelector('.result.ok', { timeout: 60000 });
    await page.click('[data-go="4"]'); await page.click('[data-act="gen"]');
    await page.waitForSelector('.result.err', { timeout: 60000 });
    ok('corrupt rule data refused', /规则数据/.test(await page.textContent('.result.err')), await page.textContent('.result.err b'));
    await page.screenshot({ path: path.join(SHOTS, '14-mobile-rules-bad.png'), fullPage: true });
    await ctx.close();
  }
  await browser.close();
  console.log(`e2e: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
