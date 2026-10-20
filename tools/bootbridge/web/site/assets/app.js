/* BootBridge web edition: six-step wizard (UI follows design/desktop-offline-ui).
 * Everything runs locally; the only network requests are the official firmware
 * download (h3c-magic OSS), the NX30 SN pre-check (magic.h3c.com) and this
 * site's own rule data.  SPDX-License-Identifier: GPL-2.0-or-later */
(function () {
  'use strict';
  var CAT = window.BB_CATALOG, STR = window.BB_STRINGS, CMDS = window.BB_COMMANDS, E = window.BBEngine, NX = window.BBNx30;
  var root = document.getElementById('root');

  function bootFail(detail) {
    var b = document.getElementById('boot-fail'), d = document.getElementById('boot-detail');
    if (root) root.innerHTML = '';
    if (b) b.hidden = false;
    if (d) d.textContent = detail || '';
  }
  (function features() {
    var miss = [];
    if (!CAT || !STR || !CMDS || !E || !NX) miss.push('assets');
    if (typeof Promise !== 'function') miss.push('Promise');
    if (typeof Uint8Array !== 'function') miss.push('Uint8Array');
    if (typeof Blob !== 'function' || !window.URL || !URL.createObjectURL) miss.push('Blob');
    if (typeof FileReader !== 'function') miss.push('FileReader');
    if (typeof atob !== 'function') miss.push('atob');
    if (miss.length) { bootFail('missing: ' + miss.join(', ')); throw new Error('BootBridge: unsupported browser'); }
  })();

  /* ---------------- icons ---------------- */
  var ICON = {
    check: '<path d="M5 12.5l4.2 4.2L19 7"/>',
    router: '<rect x="3" y="13" width="18" height="7" rx="2"/><path d="M7 16.5h.01M11 16.5h.01"/><path d="M8 9.5a6 6 0 0 1 8 0M5.5 7a9.5 9.5 0 0 1 13 0"/>',
    download: '<path d="M12 4v11M7.5 10.5L12 15l4.5-4.5"/><path d="M5 19h14"/>',
    upload: '<path d="M12 15V4M7.5 8.5L12 4l4.5 4.5"/><path d="M5 19h14"/>',
    file: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>',
    copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h8"/>',
    ext: '<path d="M14 4h6v6M20 4l-9 9"/><path d="M18 14v4a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    alert: '<path d="M12 3.5L2.5 20h19z"/><path d="M12 10v4.5M12 17.5h.01"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/>',
    shield: '<path d="M12 3l7 3v6c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6z"/><path d="M9 12l2 2 4-4"/>',
    chev: '<path d="M9 6l6 6-6 6"/>',
    x: '<path d="M6 6l12 12M18 6L6 18"/>',
    arrowR: '<path d="M5 12h14M13 6l6 6-6 6"/>',
    arrowL: '<path d="M19 12H5M11 6l-6 6 6 6"/>',
    globe: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/>',
    link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1"/><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
    refresh: '<path d="M20 11a8 8 0 0 0-14.6-4.5L4 8M4 4v4h4"/><path d="M4 13a8 8 0 0 0 14.6 4.5L20 16M20 20v-4h-4"/>',
    term: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 9l3 3-3 3M12 15h5"/>'
  };
  function ic(n, cls) { return '<svg class="' + (cls || 'i') + '" viewBox="0 0 24 24" aria-hidden="true">' + ICON[n] + '</svg>'; }
  var LOGO = '<svg width="26" height="26" viewBox="0 0 26 26" aria-hidden="true"><rect width="26" height="26" rx="7" fill="var(--teal)"/><path d="M5.5 17.5h15M8 17.5v-4.2a5 5 0 0 1 10 0v4.2" stroke="var(--on-teal)" stroke-width="2" fill="none" stroke-linecap="round"/><circle cx="13" cy="8.2" r="1.4" fill="var(--on-teal)"/></svg>';

  /* ---------------- helpers ---------------- */
  function esc(s) { return String(s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function fmt(s, v) { return String(s).replace(/\{(\w+)\}/g, function (m, k) { return v && v[k] != null ? v[k] : m; }); }
  function store(k, v) { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; } return null; }
  function L() { return STR[S.lang]; }
  function guideById(id) { for (var i = 0; i < CAT.guide.length; i++) if (CAT.guide[i].id === id) return CAT.guide[i]; return null; }
  function G() { return guideById(S.model); }
  function mname(g) { return g.alias ? (S.lang === 'zh' ? g.primary + '（' + g.alias + '）' : g.primary + ' (' + g.alias + ')') : g.primary; }
  function bundleFor(g) { for (var i = 0; i < CAT.bundles.length; i++) { var b = CAT.bundles[i]; if (b.model === g.id && b.inSha === g.sha && b.inSize === g.bytes) return b; } return null; }
  function outName(g) { return g.primary + '-' + g.version + '-BootBridge.bin'; }
  function mb(n) { return (n / 1048576).toFixed(1); }
  function fmtSize(n) { return S.lang === 'zh' ? mb(n) + ' MB（' + n.toLocaleString('en-US') + ' 字节）' : mb(n) + ' MB (' + n.toLocaleString('en-US') + ' bytes)'; }
  function shortSha(s) { return s.slice(0, 16) + '…' + s.slice(-8); }
  function err(code, extra) { var e = new Error(code); e.code = code; if (extra != null) e.detail = extra; return e; }
  function busy() { return S.file.st === 'hashing' || S.file.st === 'downloading' || S.gen.st === 'loading' || S.gen.st === 'running'; }

  /* ---------------- state ---------------- */
  var langPref = store('bb.lang'), themePref = store('bb.theme');
  var S = {
    step: 1,
    lang: langPref === 'zh' || langPref === 'en' ? langPref : (/^zh/i.test(navigator.language || 'zh') ? 'zh' : 'en'),
    theme: themePref === 'light' || themePref === 'dark' ? themePref : 'auto',
    model: CAT.guide[0].id, modal: false, toast: '',
    rules: 'ok', rulesWhy: '',
    file: { st: 'empty' }, upgraded: false,
    gen: { st: 'idle' },
    sn: { v: '', st: 'idle', r: null },
    tab: NX.platform(navigator)
  };
  var dl = null; /* AbortController of the running official download */

  (function verifyCatalog() {
    try {
      if (!CAT.meta || CAT.meta.schema !== 'bootbridge-web-catalog-v1') throw err('schema');
      CAT.bundles.forEach(function (b) {
        if (!E.validPlan(b) || E.dataSize(b) !== b.data.size) throw err('plan ' + b.id);
        var g = guideById(b.model);
        if (!g || g.bytes !== b.inSize || g.sha !== b.inSha || g.version !== b.version) throw err('guide ' + b.id);
      });
      CAT.guide.forEach(function (g) { if (!bundleFor(g)) throw err('no bundle for ' + g.id); });
    } catch (e) { S.rules = 'bad'; S.rulesWhy = e.message; }
  })();

  /* ---------------- clipboard, toast, save ---------------- */
  var toastTimer = null;
  function toast(msg) {
    S.toast = msg; render();
    clearTimeout(toastTimer); toastTimer = setTimeout(function () { S.toast = ''; render(); }, 1800);
  }
  function copyText(text) {
    function legacy() {
      var ta = document.createElement('textarea'); ta.value = text; ta.setAttribute('readonly', '');
      ta.className = 'sr'; document.body.appendChild(ta); ta.select();
      var ok = false; try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
      ta.remove(); toast(ok ? L().copied : L().copyFail);
    }
    if (navigator.clipboard && window.isSecureContext) navigator.clipboard.writeText(text).then(function () { toast(L().copied); }, legacy);
    else legacy();
  }
  function saveBytes(bytes, name) {
    var url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
    var a = document.createElement('a'); a.href = url; a.download = name; a.rel = 'noopener'; a.className = 'sr';
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(function () { URL.revokeObjectURL(url); }, 120000);
  }
  function readFile(file) {
    if (file.arrayBuffer) return file.arrayBuffer().then(function (b) { return new Uint8Array(b); });
    return new Promise(function (res, rej) {
      var r = new FileReader(); r.onload = function () { res(new Uint8Array(r.result)); }; r.onerror = function () { rej(r.error); };
      r.readAsArrayBuffer(file);
    });
  }

  /* ---------------- progress (updated in place, no re-render) ---------------- */
  function setProgress(frac, text) {
    S.prog = frac; S.progText = text;
    paintProgress();
  }
  function paintProgress() {
    var bar = document.getElementById('pbar'), tx = document.getElementById('ptx');
    if (bar) {
      var indet = S.prog == null;
      bar.parentNode.classList.toggle('indet', indet);
      bar.style.width = indet ? '' : Math.max(0, Math.min(100, Math.round(S.prog * 100))) + '%';
    }
    if (tx) tx.textContent = S.progText || '';
  }

  /* ---------------- official firmware: local file ---------------- */
  var SIZES = {}; CAT.guide.forEach(function (g) { SIZES[g.bytes] = 1; });
  var MAX_FILE = 64 << 20;
  function resetFile() {
    if (dl) { try { dl.abort(); } catch (e) { /* ignore */ } dl = null; }
    S.file = { st: 'empty' }; S.gen = { st: 'idle' };
  }
  function handleFile(file) {
    if (busy()) return;
    S.step = 2; S.gen = { st: 'idle' };
    S.file = { st: 'hashing', name: file.name, size: file.size };
    setProgress(null, ''); render();
    if (file.size > MAX_FILE || !file.size) { S.file = { st: 'bad', name: file.name, size: file.size, why: L().tooBig }; render(); return; }
    readFile(file).then(function (u8) {
      S.progText = ''; return E.sha256(u8, { onProgress: function (f) { setProgress(f, Math.round(f * 100) + '%'); } }).then(function (sha) {
        return accept(u8, sha, file.name, 'local');
      });
    }).catch(function (e) {
      S.file = (e && e.name === 'RangeError') ? { st: 'error', title: L().memT, msg: L().memS } : { st: 'error', title: L().readErrT, msg: L().readErrS };
      render();
    });
  }
  function accept(u8, sha, name, source) {
    var id = E.identify(CAT, u8.length, sha);
    if (id.ambiguous) { S.file = { st: 'error', title: L().badTitle, msg: L().ambiguous, name: name, size: u8.length, sha: sha }; render(); return; }
    if (!id.guide || !id.bundle) { S.file = { st: 'bad', name: name, size: u8.length, sha: sha }; render(); return; }
    var switched = S.model !== id.guide.id;
    S.model = id.guide.id;
    S.file = { st: 'matched', name: name, size: u8.length, sha: sha, bytes: u8, model: id.guide.id, source: source, saved: false };
    if (switched) toast(fmt(L().autoSwitched, { m: esc(mname(id.guide)) })); else render();
  }

  /* ---------------- official firmware: direct download (public URL models) ---------------- */
  function download() {
    var g = G(); if (!g.url || busy()) return;
    var ctl = typeof AbortController === 'function' ? new AbortController() : null; dl = ctl;
    S.gen = { st: 'idle' };
    S.file = { st: 'downloading', name: g.version + '.bin', size: g.bytes };
    setProgress(0, '0.0 / ' + mb(g.bytes) + ' MB'); render();
    var buf, pos = 0, last = 0;
    function prog() { var now = Date.now(); if (now - last > 120 || pos === g.bytes) { last = now; setProgress(pos / g.bytes, mb(pos) + ' / ' + mb(g.bytes) + ' MB'); } }
    fetch(g.url, { mode: 'cors', credentials: 'omit', referrerPolicy: 'no-referrer', redirect: 'error', signal: ctl ? ctl.signal : undefined })
      .then(function (res) {
        if (!res.ok) throw err('http', res.status);
        try { buf = new Uint8Array(g.bytes); } catch (e) { throw err('memory'); }
        if (!res.body || !res.body.getReader) {
          return res.arrayBuffer().then(function (ab) { if (ab.byteLength !== g.bytes) throw err('size'); buf.set(new Uint8Array(ab)); pos = g.bytes; });
        }
        var rd = res.body.getReader();
        return (function pump() {
          return rd.read().then(function (r) {
            if (r.done) return;
            if (pos + r.value.length > g.bytes) { try { rd.cancel(); } catch (e) { /* ignore */ } throw err('size'); }
            buf.set(r.value, pos); pos += r.value.length; prog();
            return pump();
          });
        })();
      })
      .then(function () {
        if (pos !== g.bytes) throw err('size');
        setProgress(null, L().hashing);
        return E.sha256(buf);
      })
      .then(function (sha) {
        dl = null;
        if (sha !== g.sha) throw err('hash');
        accept(buf, sha, g.version + '.bin', 'download');
      })
      .catch(function (e) {
        dl = null;
        if (S.file.st !== 'downloading') return;           /* reset elsewhere */
        if (e && e.name === 'AbortError') { S.file = { st: 'empty' }; toast(L().fetchCancelled); return; }
        var code = e && e.code, msg;
        if (code === 'http') msg = fmt(L().eHttp, { f: 'HTTP ' + esc(e.detail) });
        else if (code === 'size') msg = L().eSize;
        else if (code === 'hash') msg = L().eHash;
        else if (code === 'memory') msg = L().memS;
        else msg = L().eNet;
        S.file = { st: 'error', title: L().fetchFailT, msg: msg, hint: L().fetchFailHint };
        render();
      });
  }

  /* ---------------- NX30 SN pre-check ---------------- */
  function snValue() { return NX.normalize(S.sn.v); }
  function precheck() {
    var sn = snValue(); if (!NX.check(sn).ok || S.sn.st === 'checking') return;
    S.sn.st = 'checking'; S.sn.r = null; render();
    NX.precheck(sn).then(function (r) {
      if (snValue() !== sn) return;
      S.sn.st = r.state; S.sn.r = r; render();
    });
  }
  function commandsAllowed() { return S.sn.st === 'ok' || S.sn.st === 'network' || S.sn.st === 'http'; }

  /* ---------------- rule data + generation ---------------- */
  var dataCache = {}, dataWait = {};
  window.BBData = { add: function (id, b64) { var w = dataWait[id]; if (w) { delete dataWait[id]; w(b64); } } };
  function viaScript(b) {
    return new Promise(function (res, rej) {
      var s = document.createElement('script'), t = setTimeout(function () { delete dataWait[b.id]; rej(err('data-load', 'timeout')); }, 180000);
      dataWait[b.id] = function (b64) { clearTimeout(t); res(b64); };
      s.src = b.data.file; s.async = true;
      s.onerror = function () { clearTimeout(t); delete dataWait[b.id]; rej(err('data-load', 'script')); };
      s.onload = function () { s.remove(); };
      document.head.appendChild(s);
    });
  }
  function viaFetch(b, onProg) {
    var prefix = 'BBData.add("' + b.id + '","', total = prefix.length + b.data.b64 + 4;
    return fetch(b.data.file, { credentials: 'same-origin' }).then(function (res) {
      if (!res.ok) throw err('data-load', 'HTTP ' + res.status);
      if (!res.body || !res.body.getReader || typeof TextDecoder !== 'function') return res.text();
      var rd = res.body.getReader(), parts = [], got = 0, dec = new TextDecoder('ascii');
      return (function pump() {
        return rd.read().then(function (r) {
          if (r.done) { parts.push(dec.decode()); return parts.join(''); }
          got += r.value.length; parts.push(dec.decode(r.value, { stream: true })); onProg(Math.min(1, got / total), got);
          return pump();
        });
      })();
    }).then(function (text) {
      if (text.indexOf(prefix) !== 0) throw err('data-mismatch', 'prefix');
      var end = text.lastIndexOf('");');
      if (end < prefix.length) throw err('data-mismatch', 'suffix');
      return text.slice(prefix.length, end);
    });
  }
  function loadData(b, onProg) {
    if (dataCache[b.id]) return Promise.resolve(dataCache[b.id]);
    var http = /^https?:$/.test(location.protocol) && typeof fetch === 'function';
    var p = http ? viaFetch(b, onProg).catch(function (e) { if (e && e.code === 'data-mismatch') throw e; return viaScript(b); }) : viaScript(b);
    return p.then(function (b64) {
      if (b64.length !== b.data.b64) throw err('data-mismatch', 'length');
      var u8; try { u8 = E.fromBase64(b64); } catch (e) { throw err('data-mismatch', 'base64'); }
      if (u8.length !== b.data.size) throw err('data-mismatch', 'size');
      dataCache[b.id] = u8; return u8;
    });
  }
  function generate() {
    if (busy() || S.file.st !== 'matched' || S.rules !== 'ok') return;
    var g = guideById(S.file.model), b = bundleFor(g), input = S.file.bytes, total = b.data.b64;
    S.gen = { st: 'loading' }; setProgress(dataCache[b.id] ? 1 : 0, ''); render();
    loadData(b, function (f, got) { setProgress(f, mb(got * 3 / 4) + ' / ' + mb(total * 3 / 4) + ' MB'); })
      .then(function (data) {
        S.gen = { st: 'running', stage: 'check' }; setProgress(0.08, ''); render();
        return E.build(b, input, data, {
          onStage: function (st) {
            S.gen.stage = st;
            var lbl = document.getElementById('gstage');
            if (lbl) lbl.textContent = st === 'verify' ? L().verifying : L().generating;
            setProgress(st === 'check' ? 0.08 : st === 'apply' ? 0.45 : 0.8, '');
          }
        });
      })
      .then(function (r) {
        if (S.file.bytes !== input) return;
        S.gen = { st: 'done', bytes: r.bytes, sha: r.sha, name: outName(g) };
        render();
        saveBytes(r.bytes, outName(g));
      })
      .catch(function (e) {
        if (S.file.bytes !== input) return;
        var c = (e && e.code) || (e && e.name === 'RangeError' ? 'memory' : 'other');
        S.gen = { st: 'error', code: c, detail: (e && (e.detail || e.message)) || '' };
        render();
      });
  }
  function genError() {
    var c = S.gen.code, l = L();
    if (c === 'data-load') return [l.rulesLoadT, l.rulesLoadS];
    if (c === 'data-mismatch' || c === 'invalid-plan') return [l.rulesBadT, l.rulesBadS];
    if (c === 'input-mismatch') return [l.inputBadT, l.inputBadS];
    if (c === 'output-mismatch') return [l.outBadT, l.outBadS];
    if (c === 'memory') return [l.memT, l.memS];
    return [l.genFailT, esc(S.gen.detail || c)];
  }

  /* ---------------- diagnostics (never contains the SN) ---------------- */
  function diag() {
    var m = CAT.meta, f = S.file, lines = [
      'BootBridge Web ' + m.web + (m.label ? ' (' + m.label + ')' : ''),
      'release: ' + m.release + ' · engine ' + m.engine + ' · dat ' + m.datSha.slice(0, 16),
      'rules: ' + S.rules + (S.rulesWhy ? ' (' + S.rulesWhy + ')' : ''),
      'page: ' + location.protocol + ' secure=' + !!window.isSecureContext + ' subtle=' + E.hasSubtle(),
      'browser: ' + navigator.userAgent,
      'lang: ' + S.lang + ' theme: ' + S.theme + ' step: ' + S.step + ' model: ' + S.model,
      'file: ' + f.st + (f.size != null ? ' size=' + f.size : '') + (f.sha ? ' sha=' + f.sha : '') + (f.source ? ' source=' + f.source : ''),
      'gen: ' + S.gen.st + (S.gen.code ? ' code=' + S.gen.code + ' ' + (S.gen.detail || '') : '') + (S.gen.sha ? ' out=' + S.gen.sha : ''),
      'nx30 precheck: ' + S.sn.st + (S.sn.r && S.sn.r.code != null ? ' retCode=' + S.sn.r.code : '') + (S.sn.r && S.sn.r.detail ? ' ' + S.sn.r.detail : ''),
      'SN: not included'
    ];
    return lines.join('\n');
  }

  /* ---------------- views ---------------- */
  function stepDone(n) {
    if (n === 1) return S.step > 1;
    if (n === 2) return S.file.st === 'matched';
    if (n === 3) return S.upgraded;
    if (n === 4) return S.gen.st === 'done';
    if (n === 5) return S.step > 5;
    return false;
  }
  function stepper() {
    var l = L();
    return '<nav class="stepper" aria-label="steps"><div class="wrap"><ol class="hsteps">' + l.steps.map(function (s, i) {
      var n = i + 1, a = S.step === n, d = !a && stepDone(n);
      return '<li class="' + (a ? 'cur' : '') + (n <= S.step ? ' reached' : '') + '"><button class="hs' + (a ? ' active' : '') + (d ? ' done' : '') + '" data-go="' + n + '"' + (a ? ' aria-current="step"' : '') + ' title="' + esc(s[0]) + '"><span class="dot">' + (d ? ic('check') : n) + '</span><span class="tx"><b>' + s[0] + '</b><span>' + s[1] + '</span></span></button></li>';
    }).join('') + '</ol></div></nav>';
  }
  function aside() {
    var l = L(), g = G(), f = S.file, gs = S.gen;
    function chk(on, t, sub) { return '<li' + (on ? ' class="on"' : '') + '><span class="cd">' + (on ? ic('check') : '') + '</span><span class="ct"><b>' + t + '</b><span>' + sub + '</span></span></li>'; }
    var fw = f.st === 'matched' ? esc(f.name) : f.st === 'downloading' ? l.stDl : f.st === 'hashing' ? l.stHash : l.stFwTodo;
    var br = gs.st === 'done' ? esc(gs.name) : gs.st === 'loading' || gs.st === 'running' ? l.stBrBusy : l.stBrTodo;
    return '<aside class="aside" aria-label="' + esc(l.sumT) + '">' +
      '<div class="box"><h2>' + l.sumT + '</h2>' +
      '<div class="sumrow"><span class="k">' + l.sumModel + '</span><span class="v">' + esc(mname(g)) + '</span></div>' +
      '<div class="sumrow"><span class="k">' + l.sumVer + '</span><span class="v mono">' + esc(g.version) + '</span></div>' +
      '<ul class="checks">' + chk(stepDone(2), l.sumFw, fw) + chk(stepDone(3), l.sumUp, S.upgraded ? fmt(l.stUpOk, { v: esc(g.version) }) : l.stUpTodo) + chk(stepDone(4), l.sumBridge, br) + '</ul></div>' +
      '<div class="box"><h2>' + l.trustT + '</h2><div class="trust">' +
      (S.rules === 'ok' ? '<div class="row"><span class="gd"></span><span>' + l.rulesOk + '<br><span class="muted">' + l.rulesOff + '</span></span></div>'
        : '<div class="row bad">' + ic('alert') + '<span>' + l.rulesBadT + '</span></div>') +
      '<div class="row">' + ic('shield') + '<span>' + l.privacyLine + '</span></div>' +
      '<div class="row">' + ic('info') + '<span>' + l.diagNoSn + '</span></div></div></div>' +
      '<div class="box"><h2>' + l.helpT + '</h2><div class="trust"><span>' + l.diagS + '</span></div><button class="btn" data-copy="diag">' + ic('copy') + l.copyDiag + '</button></div>' +
      '</aside>';
  }
  function pageFoot() {
    var l = L(), m = CAT.meta;
    return '<footer class="pagefoot"><div class="wrap"><span>' + l.title + ' ' + fmt(l.version, { v: esc(m.web) }) + '</span><span>' + l.release + ' <span class="mono">' + esc(m.release) + '</span></span><span class="sp"></span>' +
      '<a href="SHA256SUMS" target="_blank" rel="noopener">SHA256SUMS</a>' + (m.repo ? '<a href="' + esc(m.repo) + '" target="_blank" rel="noopener noreferrer">' + l.source + '</a>' : '') +
      '<button class="btn link" data-act="open-modal">' + l.settings + '</button></div></footer>';
  }
  function shaDd(sha, key) { return '<dd><span class="mono" title="' + sha + '">' + shortSha(sha) + '</span><button class="btn link" data-copy="' + key + '" aria-label="copy">' + ic('copy') + '</button></dd>'; }
  function olist(items, vars) {
    return '<ol class="olist">' + items.map(function (it, i) {
      var tx = it[1].replace('{v}', '<code>' + esc(vars.v || '') + '</code>').replace('{f}', '<code>' + esc(vars.f || '') + '</code>').replace('192.168.10.1', '<code>192.168.10.1</code>');
      return '<li><span class="n">' + (i + 1) + '</span><span class="tx"><b>' + it[0] + '</b><span>' + tx + '</span>' + (vars.extra && vars.extra[i] || '') + '</span></li>';
    }).join('') + '</ol>';
  }
  function head(n, t, lead) { var l = L(); return '<div class="eyebrow">' + fmt(l.stepOf, { n: n }) + '</div><h1>' + t + '</h1><p class="lead">' + lead + '</p>'; }

  function step1() {
    var l = L();
    return head(1, l.s1t, l.s1l) + '<div class="mgrid" role="radiogroup">' + CAT.guide.map(function (g) {
      var sel = S.model === g.id;
      return '<button class="mcard' + (sel ? ' sel' : '') + '" data-model="' + esc(g.id) + '" role="radio" aria-checked="' + sel + '"><span class="ic">' + ic('router') + '</span><span class="tx"><b>' + esc(g.primary) + (g.alias ? '<em class="alias">' + esc(g.alias) + '</em>' : '') + '</b><span>' + fmt(l.needVer, { v: esc(g.version) }) + '</span></span><span class="radio"></span></button>';
    }).join('') + '</div><div class="fine">' + ic('info') + '<span>' + l.s1fine + '</span></div>';
  }

  function fileZone() {
    var l = L(), g = G(), f = S.file;
    if (f.st === 'empty') return '<button class="drop" data-act="pick" id="drop"><span class="big">' + ic('upload') + '</span><b>' + l.dropB + '</b><span>' + l.dropS + '</span><small>' + l.dropN + '</small></button>';
    if (f.st === 'hashing' || f.st === 'downloading') {
      var d = f.st === 'downloading';
      return '<div class="card mt"><div class="filerow"><span class="fileic">' + ic(d ? 'download' : 'file') + '</span><span class="tx"><b>' + esc(f.name) + '</b><span>' + (d ? l.downloading : l.hashing) + '</span></span>' + (d ? '<button class="btn" data-act="cancel">' + l.cancel + '</button>' : '') + '</div><div class="prog"><i id="pbar"></i></div><div class="progtx"><span>' + (d ? l.dlSrc : l.dropN) + '</span><span id="ptx"></span></div></div>';
    }
    if (f.st === 'matched') {
      var fg = guideById(f.model), dlSrc = f.source === 'download';
      return '<div class="result ok mt"><div class="head"><span class="badge">' + ic('check') + '</span><div class="grow"><b>' + fmt(l.okTitle, { m: esc(mname(fg)), v: esc(fg.version) }) + '</b><p>' + (dlSrc ? l.okSubDl : l.okSub) + '</p></div>' + (dlSrc ? '' : '<button class="btn" data-act="reset-file">' + l.another + '</button>') + '</div>' +
        '<div class="rmeta">' + ic('file') + '<span>' + esc(f.name) + '</span><span class="sep">·</span><span>' + l.sameAs + '</span></div>' +
        (dlSrc ? '<div class="acts"><button class="btn' + (f.saved ? '' : ' primary') + '" data-act="save-fw">' + ic('download') + l.saveFw + '</button><button class="btn" data-act="reset-file">' + l.another + '</button></div>' : '') + '</div>';
    }
    if (f.st === 'bad') {
      return '<div class="result warn mt"><div class="head"><span class="badge">' + ic('alert') + '</span><div class="grow"><b>' + l.badTitle + '</b><p>' + (f.why || l.badSub) + '</p></div></div>' +
        '<div class="rmeta">' + ic('file') + '<span>' + esc(f.name) + '</span><span class="sep">·</span><span>' + mb(f.size) + ' MB</span>' + (f.sha ? '<span class="sep">·</span><span class="mono" title="' + f.sha + '">' + shortSha(f.sha) + '</span>' : '') + '</div>' +
        '<div class="acts"><button class="btn primary" data-act="reset-file">' + l.another + '</button><button class="btn" data-copy="diag">' + ic('copy') + l.copyDiag + '</button></div></div>';
    }
    if (f.st === 'error') {
      return '<div class="result err mt"><div class="head"><span class="badge">' + ic('alert') + '</span><div class="grow"><b>' + f.title + '</b><p>' + f.msg + (f.hint ? '<br>' + f.hint : '') + '</p></div></div>' +
        '<div class="acts">' + (G().url && f.hint ? '<button class="btn primary" data-act="dl">' + ic('refresh') + l.retry + '</button>' : '') + '<button class="btn' + (G().url && f.hint ? '' : ' primary') + '" data-act="reset-file">' + l.another + '</button><button class="btn" data-copy="diag">' + ic('copy') + l.copyDiag + '</button></div></div>';
    }
    return '';
  }

  function snPanel() {
    var l = L(), sn = snValue(), c = NX.check(sn), st = S.sn.st, r = S.sn.r || {}, g = G(), h = '';
    var locked = st === 'checking' || commandsAllowed();
    h += '<p class="how">' + l.fetchNote + '</p><div class="snbox">';
    if (!locked) {
      var bad = sn.length > 0 && (!c.ok && (c.reason === 'chars' || sn.length > 20));
      h += '<label for="sn">' + l.snLabel + '</label><div class="snrow"><input id="sn" class="snin' + (bad ? ' bad' : '') + '" type="text" inputmode="text" autocomplete="off" autocapitalize="characters" spellcheck="false" maxlength="40" placeholder="' + esc(l.snPh) + '" value="' + esc(S.sn.v) + '">' +
        '<button class="btn primary lg" data-act="sn-check" id="sncheck"' + (c.ok ? '' : ' disabled') + '>' + l.snCheck + '</button></div>' +
        '<div class="snmeta"><span id="snhint" class="' + (bad ? 'bad' : c.ok ? 'good' : '') + '">' + snHint(sn, c) + '</span><span id="sncount">' + fmt(l.snCount, { n: sn.length }) + '</span></div>';
    } else {
      h += '<label>' + l.snLabel + '</label><div class="snfixed"><span class="mono">' + esc(NX.group(sn)) + '</span>' + (st === 'checking' ? '' : '<button class="btn link" data-act="sn-edit">' + l.snEdit + '</button>') + '</div>';
    }
    h += '</div><div class="privacy">' + ic('shield') + '<span>' + l.snPrivacy + '</span></div>';
    if (st === 'checking') h += '<div class="status busy"><span class="spin"></span><span>' + l.snChecking + '</span></div>';
    else if (st === 'ok') h += '<div class="status ok">' + ic('check') + '<span>' + fmt(l.snOk, { v: esc(g.version) }) + '</span></div>';
    else if (st === 'network') h += '<div class="status warn">' + ic('alert') + '<span>' + fmt(l.snNet, { f: esc(r.detail || '') }) + '</span></div>';
    else if (st === 'http') h += '<div class="status warn">' + ic('alert') + '<span>' + fmt(l.snHttp, { f: esc(r.status) }) + '</span></div>';
    else if (st === 'unknown-sn') h += '<div class="status err">' + ic('alert') + '<span>' + l.snUnknown + '</span></div>';
    else if (st === 'bad-format') h += '<div class="status err">' + ic('alert') + '<span>' + l.snFormat + '</span></div>';
    else if (st === 'other-version') h += '<div class="status warn">' + ic('alert') + '<span>' + fmt(l.snOther, { f: esc(r.version || '?'), v: esc(g.version) }) + '</span></div>';
    else if (st === 'rejected') h += '<div class="status err">' + ic('alert') + '<span>' + fmt(l.snRejected, { f: esc(r.code == null ? '?' : r.code) }) + '</span></div>';
    else if (st === 'bad-response') h += '<div class="status err">' + ic('alert') + '<span>' + l.snBad + '</span></div>';
    if (commandsAllowed()) {
      var key = S.tab === 'ps51' ? 'ps51' : 'sh', cmd = NX.command(CMDS[key], sn);
      h += '<div class="divider"></div><h3>' + ic('term') + ' ' + l.cmdT + '</h3>' +
        '<div class="tabs" role="tablist"><button role="tab" data-tab="ps51" aria-selected="' + (key === 'ps51') + '">' + l.cmdWin + '</button><button role="tab" data-tab="sh" aria-selected="' + (key === 'sh') + '">' + l.cmdSh + '</button></div>' +
        '<p class="how">' + (key === 'ps51' ? l.cmdWinHow : l.cmdShHow) + '</p>' +
        '<div class="cmd"><pre id="cmdtext" tabindex="0">' + esc(cmd) + '</pre></div>' +
        '<div class="cmdbar"><button class="btn primary" data-copy="cmd">' + ic('copy') + l.cmdCopy + '</button></div>' +
        '<div class="fine">' + ic('shield') + '<span>' + l.cmdSafe + '</span></div>' +
        '<p class="after">' + fmt(l.cmdAfter, { f: esc(g.version + '.bin') }) + '</p>';
    }
    return h;
  }
  function snHint(sn, c) {
    var l = L();
    if (!sn.length) return '';
    if (c.ok) return '✓ ' + esc(NX.group(sn));
    if (c.reason === 'chars') return l.snChars;
    return fmt(l.snLen, { n: sn.length });
  }

  function step2() {
    var l = L(), g = G(), dlBusy = S.file.st === 'downloading';
    var h = head(2, l.s2t, fmt(l.s2l, { m: esc(mname(g)) }));
    h += '<div class="card"><h3>' + l.target + ' · ' + esc(mname(g)) + '</h3><div class="meta"><div><span class="k">' + l.kVer + '</span><span class="v">' + esc(g.version) + '</span></div><div><span class="k">' + l.kSize + '</span><span class="v">' + mb(g.bytes) + ' MB</span></div><div><span class="k">' + l.kSha + '</span><span class="v"><span class="mono" title="' + g.sha + '">' + shortSha(g.sha) + '</span><button class="btn link" data-copy="sha" aria-label="copy">' + ic('copy') + '</button></span></div></div><div class="divider"></div>';
    if (g.url) {
      h += '<div class="linkrow"><div class="url">' + ic('globe') + '<span class="mono" title="' + esc(g.url) + '">' + esc(g.url) + '</span></div>' +
        '<button class="btn' + (S.file.st === 'matched' ? '' : ' primary') + '" data-act="dl"' + (busy() ? ' disabled' : '') + '>' + ic('download') + l.directDl + '</button>' +
        '<a class="btn" href="' + esc(g.url) + '" target="_blank" rel="noopener noreferrer">' + ic('ext') + l.openBrowser + '</a>' +
        '<button class="btn" data-copy="link" aria-label="' + esc(l.copyLink) + '">' + ic('copy') + '</button></div>' +
        (dlBusy || S.file.st === 'error' ? '<div class="fine">' + ic('info') + '<span>' + l.dlOpenHint + '</span></div>' : '');
    } else {
      h += snPanel();
    }
    h += '</div>' + fileZone();
    return h;
  }

  function step3() {
    var l = L(), g = G();
    return head(3, l.s3t, l.s3l) + '<div class="callout amber">' + ic('alert') + '<span>' + fmt(l.s3warn, { v: esc(g.version) }) + '</span></div><div class="card">' + olist(l.s3steps, { v: g.version }) + '</div>';
  }

  function step4() {
    var l = L(), f = S.file, gs = S.gen, body;
    if (S.rules !== 'ok') {
      body = '<div class="result err"><div class="head"><span class="badge">' + ic('alert') + '</span><div class="grow"><b>' + l.rulesBadT + '</b><p>' + l.rulesBadS + '</p></div></div><div class="acts"><button class="btn" data-copy="diag">' + ic('copy') + l.copyDiag + '</button></div></div>';
    } else if (f.st !== 'matched') {
      body = '<div class="card empty"><div class="big">' + ic('file') + '</div><b>' + l.noFileT + '</b><p>' + l.noFileP + '</p><button class="btn primary" data-go="2">' + ic('arrowL') + l.goStep2 + '</button></div>';
    } else {
      var g = guideById(f.model), name = outName(g);
      if (gs.st === 'loading' || gs.st === 'running') {
        var stage = gs.st === 'loading' ? fmt(l.loadingRules, { m: esc(mname(g)) }) : (gs.stage === 'verify' ? l.verifying : l.generating);
        body = '<div class="card"><div class="filerow"><span class="fileic">' + ic('refresh') + '</span><span class="tx"><b>' + esc(name) + '</b><span id="gstage">' + stage + '</span></span></div><div class="prog"><i id="pbar"></i></div><div class="progtx"><span>' + l.noClose + '</span><span id="ptx"></span></div></div>';
      } else if (gs.st === 'done') {
        body = '<div class="result ok"><div class="head"><span class="badge">' + ic('check') + '</span><div class="grow"><b>' + l.doneT + '</b><p>' + l.doneS + '</p></div></div>' +
          '<dl class="kv"><dt>' + l.kOut + '</dt><dd><span class="mono" title="' + esc(gs.name) + '">' + esc(gs.name) + '</span><button class="btn link" data-copy="outname" aria-label="copy">' + ic('copy') + '</button></dd><dt>' + l.kSha + '</dt>' + shaDd(gs.sha, 'outsha') + '</dl>' +
          '<div class="acts"><button class="btn primary" data-act="save-out">' + ic('download') + l.saveBridge + '</button></div></div>' +
          '<div class="soft-note">' + ic('info') + '<span>' + l.noUpgrade + '</span></div>';
      } else if (gs.st === 'error') {
        var e = genError();
        body = '<div class="result err"><div class="head"><span class="badge">' + ic('alert') + '</span><div class="grow"><b>' + e[0] + '</b><p>' + e[1] + '</p></div></div><div class="acts"><button class="btn primary" data-act="gen">' + ic('refresh') + l.retry + '</button><button class="btn" data-copy="diag">' + ic('copy') + l.copyDiag + '</button></div></div>';
      } else {
        body = '<div class="card"><div class="filerow"><span class="fileic">' + ic('file') + '</span><span class="tx"><b>' + esc(f.name) + '</b><span>' + esc(mname(g)) + ' · ' + esc(g.version) + ' · <span class="ok-ink">✓ ' + l.verified + '</span></span></span><button class="btn link" data-go="2">' + l.change + '</button></div>' +
          '<div class="lbl">' + l.outFile + '</div><div class="out"><div class="path"><span class="mono">' + esc(name) + '</span></div></div><div class="fine">' + ic('download') + '<span>' + l.outNote + '</span></div></div>' +
          '<div class="row-end"><button class="btn primary lg" data-act="gen">' + l.gen + '</button><div class="fine">' + ic('shield') + '<span>' + l.genFine + '</span></div></div>';
      }
    }
    return head(4, l.s4t, l.s4l) + body;
  }

  function step5() {
    var l = L(), g = S.file.st === 'matched' ? guideById(S.file.model) : G();
    return head(5, l.s5t, l.s5l) + '<div class="card">' + olist(l.s5steps, { f: outName(g) }) + '</div><div class="callout info">' + ic('info') + '<span>' + l.s5info + '</span></div>';
  }

  function step6() {
    var l = L(), g = G(), maint = 'http://192.168.10.1/';
    var link = '<span class="linkrow"><span class="url">' + ic('link') + '<span class="mono">' + maint + '</span></span><a class="btn primary" href="' + maint + '" target="_blank" rel="noopener noreferrer">' + ic('ext') + l.openBrowser + '</a><button class="btn" data-copy="maint" aria-label="copy">' + ic('copy') + '</button></span>';
    return head(6, l.s6t, l.s6l) + '<div class="callout teal">' + ic('shield') + '<span>' + l.s6backup + '</span></div>' +
      '<div class="card">' + olist(l.s6steps, { extra: [link] }) + '</div>' +
      '<details class="faq"><summary>' + ic('alert') + '<span>' + l.faqQ + '</span>' + ic('chev', 'i chev') + '</summary><div class="body">' + fmt(l.faqA, { v: esc(g.version) }) + '<ol>' + l.faqSteps.map(function (s) { return '<li>' + s + '</li>'; }).join('') + '</ol></div></details>' +
      '<div class="callout info" data-k="s6rec">' + ic('info') + '<span><b>' + l.s6recT + '</b> ' + l.s6rec.replace('192.168.10.1', '<code>192.168.10.1</code>') + '</span></div>' +
      '<div class="soft-note">' + ic('info') + '<span>' + l.noUpgrade + '</span></div>';
  }

  function panelFoot() {
    var l = L(), hint = '', nextDis = false;
    if (S.step === 3) {
      nextDis = !S.upgraded;
      hint = '<span class="check' + (S.upgraded ? ' on' : '') + '" role="checkbox" tabindex="0" aria-checked="' + S.upgraded + '" data-act="toggle-up"><span class="box">' + ic('check') + '</span><span>' + fmt(l.s3check, { v: esc(G().version) }) + '</span></span>';
    }
    if (S.step === 4 && busy()) nextDis = true;
    return '<div class="panel-foot"><span class="hint">' + hint + '</span><span class="sp"></span>' +
      (S.step > 1 ? '<button class="btn" data-act="prev">' + ic('arrowL') + l.prev + '</button>' : '') +
      (S.step < 6 ? '<button class="btn' + (S.step === 4 && S.gen.st !== 'done' ? '' : ' primary') + '" data-act="next"' + (nextDis ? ' disabled' : '') + '>' + l.next + ic('arrowR') + '</button>' : '') + '</div>';
  }

  function modal() {
    if (!S.modal) return '';
    var l = L(), m = CAT.meta;
    function seg(k, opts) { return '<div class="seg">' + opts.map(function (o) { return '<button data-set="' + k + '" data-val="' + o[0] + '" aria-pressed="' + (S[k] === o[0]) + '">' + o[1] + '</button>'; }).join('') + '</div>'; }
    var links = (m.repo ? '<a class="btn" href="' + esc(m.repo) + '" target="_blank" rel="noopener noreferrer">' + ic('ext') + l.source + '</a>' : '') +
      '<a class="btn" href="SHA256SUMS" target="_blank" rel="noopener">' + ic('file') + 'SHA256SUMS</a>';
    return '<div class="scrim" data-act="close-modal"><div class="modal" role="dialog" aria-modal="true" aria-label="' + esc(l.settings) + '" data-stop="1">' +
      '<div class="mh"><b>' + l.settings + '</b><button class="iconbtn" data-act="close-modal" aria-label="close">' + ic('x') + '</button></div><div class="mb">' +
      '<div class="msec mgrid2"><div><h4>' + l.mLang + '</h4>' + seg('lang', [['zh', '中文'], ['en', 'English']]) + '</div><div><h4>' + l.mTheme + '</h4>' + seg('theme', [['auto', l.themeAuto], ['light', l.themeLight], ['dark', l.themeDark]]) + '</div></div>' +
      '<div class="msec"><h4>' + l.mUpdate + '</h4><div class="offline-box">' + l.offlineUpd + '</div></div>' +
      '<div class="msec"><h4>' + l.mDiag + '</h4><div class="mrow"><div class="l"><span>' + l.diagS + ' · ' + l.diagNoSn + '</span></div><button class="btn" data-copy="diag">' + ic('copy') + l.copyDiag + '</button></div></div>' +
      '<div class="msec"><h4>' + l.mAbout + '</h4><div class="about">' + fmt(l.about, { v: esc(m.web), e: esc(m.engine) }) + '</div>' +
      '<dl class="kv compact"><dt>' + l.release + '</dt><dd><span class="mono">' + esc(m.release) + '</span></dd>' + (m.label ? '<dt>Build</dt><dd><span class="mono">' + esc(m.label) + '</span></dd>' : '') + '</dl>' +
      '<div class="about-links">' + links + '</div><div class="fine">' + ic('info') + '<span>' + l.sumNote + '</span></div></div>' +
      '</div></div></div>';
  }

  var rendered = 0;
  function render() {
    var l = L(), d = document.documentElement;
    d.setAttribute('data-theme', S.theme); d.lang = S.lang === 'zh' ? 'zh-CN' : 'en';
    document.title = 'BootBridge ' + l.edition;
    var content = [null, step1, step2, step3, step4, step5, step6][S.step]();
    var keep = rendered === S.step ? window.pageYOffset : -1;
    var ae = document.activeElement, focusId = ae && ae.id, selS = null, selE = null;
    if (focusId === 'sn') { selS = ae.selectionStart; selE = ae.selectionEnd; }
    root.innerHTML = '<div class="page">' +
      '<header class="topbar"><div class="wrap"><div class="brand">' + LOGO + '<span class="name">' + l.title + '</span><span class="tag">' + l.edition + '</span></div>' +
      '<div class="right"><div class="seg" role="group" aria-label="language"><button data-set="lang" data-val="zh" aria-pressed="' + (S.lang === 'zh') + '">中文</button><button data-set="lang" data-val="en" aria-pressed="' + (S.lang === 'en') + '">EN</button></div>' +
      '<button class="iconbtn" data-act="open-modal" title="' + esc(l.settings) + '" aria-label="' + esc(l.settings) + '">' + ic('gear') + '</button></div></div></header>' +
      stepper() +
      '<main class="wrap layout"><section class="panel"><div class="panel-body" data-step="' + S.step + '">' + content + '</div>' + panelFoot() + '</section>' + aside() + '</main>' +
      pageFoot() + '</div>' + modal() + (S.toast ? '<div class="toast" role="status">' + ic('check') + S.toast + '</div>' : '') +
      '<input type="file" id="picker" class="hidden-input" accept=".bin,application/octet-stream">';
    rendered = S.step;
    if (focusId) { var el = document.getElementById(focusId); if (el) { try { el.focus({ preventScroll: true }); } catch (e) { el.focus(); } if (selS != null && el.setSelectionRange) try { el.setSelectionRange(selS, selE); } catch (e) { /* ignore */ } } }
    if (keep >= 0 && Math.abs(window.pageYOffset - keep) > 1) window.scrollTo(0, keep);
    paintProgress();
  }

  /* ---------------- events ---------------- */
  function go(n) { if (n >= 1 && n <= 6) { S.step = n; render(); window.scrollTo(0, 0); } }
  function copyKey(k) {
    var g = G(), t = '';
    if (k === 'sha') t = g.sha; else if (k === 'link') t = g.url; else if (k === 'maint') t = 'http://192.168.10.1/';
    else if (k === 'outsha') t = S.gen.sha || ''; else if (k === 'outname') t = S.gen.name || '';
    else if (k === 'diag') t = diag();
    else if (k === 'cmd') t = NX.command(CMDS[S.tab === 'ps51' ? 'ps51' : 'sh'], snValue());
    if (t) copyText(t);
  }
  root.addEventListener('click', function (ev) {
    var t = ev.target.closest('[data-act],[data-go],[data-model],[data-set],[data-copy],[data-tab]');
    if (!t) return;
    var act = t.getAttribute('data-act');
    if (t.hasAttribute('data-go')) return go(+t.getAttribute('data-go'));
    if (t.hasAttribute('data-model')) {
      var id = t.getAttribute('data-model');
      if (id !== S.model) { S.model = id; if (S.file.st === 'matched' && S.file.model !== id || S.file.st === 'downloading') resetFile(); }
      return render();
    }
    if (t.hasAttribute('data-set')) {
      var k = t.getAttribute('data-set'), v = t.getAttribute('data-val');
      S[k] = v; store(k === 'lang' ? 'bb.lang' : 'bb.theme', v); return render();
    }
    if (t.hasAttribute('data-copy')) return copyKey(t.getAttribute('data-copy'));
    if (t.hasAttribute('data-tab')) { S.tab = t.getAttribute('data-tab'); return render(); }
    switch (act) {
      case 'prev': return go(S.step - 1);
      case 'next': return go(S.step + 1);
      case 'pick': { var p = document.getElementById('picker'); if (p) p.click(); return; }
      case 'reset-file': resetFile(); return render();
      case 'cancel': if (dl) dl.abort(); return;
      case 'dl': return download();
      case 'save-fw': if (S.file.bytes) { saveBytes(S.file.bytes, S.file.name); S.file.saved = true; toast(fmt(L().savedAs, { f: esc(S.file.name) })); } return;
      case 'save-out': if (S.gen.bytes) saveBytes(S.gen.bytes, S.gen.name); return;
      case 'gen': return generate();
      case 'toggle-up': S.upgraded = !S.upgraded; return render();
      case 'sn-check': return precheck();
      case 'sn-edit': S.sn.st = 'idle'; S.sn.r = null; render(); var i = document.getElementById('sn'); if (i) i.focus(); return;
      case 'open-modal': S.modal = true; return render();
      case 'close-modal': if (ev.target === t || t.tagName === 'BUTTON') { S.modal = false; render(); } return;
    }
  });
  root.addEventListener('keydown', function (ev) {
    var t = ev.target;
    if (t.getAttribute && t.getAttribute('data-act') === 'toggle-up' && (ev.key === ' ' || ev.key === 'Enter')) { ev.preventDefault(); S.upgraded = !S.upgraded; render(); var c = root.querySelector('[data-act="toggle-up"]'); if (c) c.focus(); }
    if (t.id === 'sn' && ev.key === 'Enter') { ev.preventDefault(); precheck(); }
  });
  document.addEventListener('keydown', function (ev) { if (ev.key === 'Escape' && S.modal) { S.modal = false; render(); } });
  root.addEventListener('input', function (ev) {
    if (ev.target.id !== 'sn') return;
    S.sn.v = ev.target.value; if (S.sn.st !== 'idle' && S.sn.st !== 'checking') { S.sn.st = 'idle'; S.sn.r = null; render(); return; }
    var sn = snValue(), c = NX.check(sn), bad = sn.length > 0 && (!c.ok && (c.reason === 'chars' || sn.length > 20));
    ev.target.classList.toggle('bad', bad);
    var h = document.getElementById('snhint'), n = document.getElementById('sncount'), b = document.getElementById('sncheck');
    if (h) { h.innerHTML = snHint(sn, c); h.className = bad ? 'bad' : c.ok ? 'good' : ''; }
    if (n) n.textContent = fmt(L().snCount, { n: sn.length });
    if (b) b.disabled = !c.ok;
  });
  root.addEventListener('change', function (ev) {
    if (ev.target.id !== 'picker') return;
    var fs = ev.target.files; if (!fs || !fs.length) return;
    if (fs.length > 1) return toast(L().oneFile);
    handleFile(fs[0]);
  });
  var dragDepth = 0;
  function hasFiles(ev) { var t = ev.dataTransfer && ev.dataTransfer.types; return t && Array.prototype.indexOf.call(t, 'Files') >= 0; }
  window.addEventListener('dragenter', function (ev) { if (!hasFiles(ev)) return; dragDepth++; var d = document.getElementById('drop'); if (d) d.classList.add('over'); });
  window.addEventListener('dragleave', function () { dragDepth = Math.max(0, dragDepth - 1); if (!dragDepth) { var d = document.getElementById('drop'); if (d) d.classList.remove('over'); } });
  window.addEventListener('dragover', function (ev) { if (hasFiles(ev)) { ev.preventDefault(); ev.dataTransfer.dropEffect = busy() ? 'none' : 'copy'; } });
  window.addEventListener('drop', function (ev) {
    if (!hasFiles(ev)) return;
    ev.preventDefault(); dragDepth = 0;
    var fs = ev.dataTransfer.files; if (!fs || !fs.length || busy()) return;
    if (fs.length > 1) return toast(L().oneFile);
    handleFile(fs[0]);
  });
  window.addEventListener('beforeunload', function (ev) { if (busy()) { ev.preventDefault(); ev.returnValue = ''; } });

  try { render(); } catch (e) { bootFail(String(e && e.message || e)); throw e; }
  var bf = document.getElementById('boot-fail'); if (bf) bf.hidden = true;
  window.__bb = { state: S, diag: diag }; /* for automated tests and support */
})();
