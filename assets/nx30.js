/* NX30 / RX3000: the reviewed version (NX30V100R012) has no public link.  The page
 * checks the SN with H3C's version API (browser-readable), then builds a download
 * command with the SN embedded; the user's own computer asks H3C for the short-lived
 * signed link (that API is not readable from a browser), downloads and verifies.
 * SPDX-License-Identifier: GPL-2.0-or-later */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.BBNx30 = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  var BEST_API = 'https://magic.h3c.com/smarthomeback/rest/ihomers/getGwBestVerNew';
  var TARGET = 'NX30V100R012', ASK = ['NX30V100R011', TARGET];

  function normalize(raw) { return String(raw == null ? '' : raw).replace(/[\s-]+/g, '').toUpperCase(); }
  /* H3C: retCode 20 unless the SN is exactly 20 characters of [0-9A-Z]. */
  function check(sn) {
    if (!/^[0-9A-Z]*$/.test(sn)) return { ok: false, reason: 'chars', length: sn.length };
    if (sn.length !== 20) return { ok: false, reason: 'length', length: sn.length };
    return { ok: true, length: 20 };
  }
  function group(sn) { return sn.replace(/(.{4})(?=.)/g, '$1 '); }

  function post(fetchImpl, body, timeoutMs) {
    var ctl = typeof AbortController === 'function' ? new AbortController() : null, timer;
    var p = fetchImpl(BEST_API, {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
      body: JSON.stringify(body), credentials: 'omit', referrerPolicy: 'no-referrer', cache: 'no-store',
      mode: 'cors', signal: ctl ? ctl.signal : undefined
    });
    if (ctl) timer = setTimeout(function () { ctl.abort(); }, timeoutMs);
    return p.then(function (r) { clearTimeout(timer); return r; }, function (e) { clearTimeout(timer); throw e; });
  }

  /* -> {state:'ok'|'unknown-sn'|'bad-format'|'other-version'(version)|'rejected'(code)|'network'(detail)|'http'(status)|'bad-response'} */
  function precheck(sn, opt) {
    opt = opt || {};
    var fetchImpl = opt.fetchImpl || (typeof fetch === 'function' ? fetch.bind(null) : null);
    var timeoutMs = opt.timeoutMs || 15000, i = 0, last = { state: 'rejected', code: null };
    if (!check(sn).ok) return Promise.resolve({ state: 'bad-format' });
    if (!fetchImpl) return Promise.resolve({ state: 'network', detail: 'no fetch' });
    function next() {
      if (i >= ASK.length) return Promise.resolve(last);
      var gwVer = ASK[i++];
      return post(fetchImpl, { gwVer: gwVer, gwSn: sn }, timeoutMs).then(function (res) {
        if (!res.ok) return { state: 'http', status: res.status };
        return res.json().then(function (j) {
          var code = Number(j && j.retCode), ver = j && j.gwBestVer != null ? String(j.gwBestVer) : '';
          if (code === 0 && ver === TARGET) return { state: 'ok' };
          if (code === 24) return { state: 'unknown-sn' };
          if (code === 20 || code === 18) return { state: 'bad-format' };
          last = code === 0 ? { state: 'other-version', version: ver.slice(0, 40) } : { state: 'rejected', code: isNaN(code) ? null : code };
          return next();
        }, function () { return { state: 'bad-response' }; });
      }, function (e) { return { state: 'network', detail: e && e.name === 'AbortError' ? 'timeout' : String(e && e.message || e).slice(0, 80) }; });
    }
    return next();
  }

  /* Only a validated SN ([0-9A-Z]{20}) is ever substituted into a command. */
  function command(template, sn) {
    if (!check(sn).ok) throw new Error('bad SN');
    if (template.split('__SN__').length !== 2) throw new Error('bad template');
    return template.replace('__SN__', sn);
  }
  function platform(nav) {
    var s = ((nav && (nav.userAgentData && nav.userAgentData.platform || nav.platform)) || '') + ' ' + (nav && nav.userAgent || '');
    return /Win/i.test(s) ? 'ps51' : 'sh';
  }

  return { TARGET: TARGET, BEST_API: BEST_API, normalize: normalize, check: check, group: group,
           precheck: precheck, command: command, platform: platform };
});
