/* BootBridge web engine: browser port of bb_export_plan_bin() (desktop/src/bb_export.c).
 * Same contract, in memory: the plan is validated first, the input is hashed and
 * must match the pin, COPY/XOR/LITERAL are applied, the input is hashed again,
 * and the complete output must match the pinned size and SHA-256 before it is
 * handed to the caller.  Nothing is uploaded; nothing here touches the network.
 * SPDX-License-Identifier: GPL-2.0-or-later */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.BBEngine = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  var COPY = 1, LITERAL = 2, XOR = 3, MAX_OPS = 262144;
  var MAX_SAFE = 9007199254740991; /* catalog sizes are JS numbers; real ones are < 64 MiB */

  /* ---- SHA-256: WebCrypto when available, otherwise a pure JS implementation ---- */
  var K = new Int32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);

  function Sha256() {
    this.h = new Int32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    this.w = new Int32Array(64); this.buf = new Uint8Array(64); this.used = 0; this.len = 0;
  }
  Sha256.prototype.block = function (p, o) {
    var w = this.w, h = this.h, i, a, b, c, d, e, f, g, hh, t1, t2, x, y;
    for (i = 0; i < 16; i++) w[i] = (p[o + 4 * i] << 24) | (p[o + 4 * i + 1] << 16) | (p[o + 4 * i + 2] << 8) | p[o + 4 * i + 3];
    for (i = 16; i < 64; i++) {
      x = w[i - 15]; y = w[i - 2];
      w[i] = (((x >>> 7) | (x << 25)) ^ ((x >>> 18) | (x << 14)) ^ (x >>> 3)) + w[i - 16] +
             (((y >>> 17) | (y << 15)) ^ ((y >>> 19) | (y << 13)) ^ (y >>> 10)) + w[i - 7] | 0;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
      t1 = hh + (((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7))) + ((e & f) ^ (~e & g)) + K[i] + w[i] | 0;
      t2 = (((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10))) + ((a & b) ^ (a & c) ^ (b & c)) | 0;
      hh = g; g = f; f = e; e = d + t1 | 0; d = c; c = b; b = a; a = t1 + t2 | 0;
    }
    h[0] = h[0] + a | 0; h[1] = h[1] + b | 0; h[2] = h[2] + c | 0; h[3] = h[3] + d | 0;
    h[4] = h[4] + e | 0; h[5] = h[5] + f | 0; h[6] = h[6] + g | 0; h[7] = h[7] + hh | 0;
  };
  Sha256.prototype.update = function (u8) {
    var n = u8.length, o = 0, z;
    this.len += n;
    if (this.used) {
      z = Math.min(64 - this.used, n); this.buf.set(u8.subarray(0, z), this.used); this.used += z; o = z;
      if (this.used < 64) return this;
      this.block(this.buf, 0); this.used = 0;
    }
    for (; o + 64 <= n; o += 64) this.block(u8, o);
    if (o < n) { this.buf.set(u8.subarray(o), 0); this.used = n - o; }
    return this;
  };
  Sha256.prototype.hex = function () {
    var bits = this.len * 8, pad = new Uint8Array(((this.used < 56) ? 56 : 120) - this.used + 8), i, s = '', v;
    pad[0] = 0x80;
    for (i = 0; i < 8; i++) { pad[pad.length - 1 - i] = bits % 256; bits = Math.floor(bits / 256); }
    this.update(pad);
    for (i = 0; i < 8; i++) { v = this.h[i] >>> 0; s += ('0000000' + v.toString(16)).slice(-8); }
    return s;
  };

  function toHex(buf) {
    var u = new Uint8Array(buf), s = '', i;
    for (i = 0; i < u.length; i++) s += (u[i] < 16 ? '0' : '') + u[i].toString(16);
    return s;
  }
  function subtle() {
    var c = (typeof self !== 'undefined' && self.crypto) || (typeof globalThis !== 'undefined' && globalThis.crypto);
    return c && c.subtle && typeof c.subtle.digest === 'function' ? c.subtle : null;
  }
  var tick = function () { return new Promise(function (r) { setTimeout(r, 0); }); };

  /* sha256(u8, {onProgress(fraction), pureJs}) -> Promise<hex> */
  function sha256(u8, opt) {
    opt = opt || {};
    var s = !opt.pureJs && subtle();
    if (s) {
      return s.digest('SHA-256', u8).then(toHex, function () { return sha256(u8, { onProgress: opt.onProgress, pureJs: true }); });
    }
    var h = new Sha256(), step = 4 << 20, o = 0;
    return new Promise(function (resolve, reject) {
      (function next() {
        try {
          var end = Math.min(o + step, u8.length);
          h.update(u8.subarray(o, end)); o = end;
          if (opt.onProgress) opt.onProgress(u8.length ? o / u8.length : 1);
          if (o >= u8.length) resolve(h.hex()); else tick().then(next);
        } catch (e) { reject(e); }
      })();
    });
  }

  /* ---- plan validation: mirror of valid_plan() ---- */
  function isHex32(s) { return typeof s === 'string' && /^[0-9a-f]{64}$/.test(s); }
  function nonzero(s) { return isHex32(s) && /[1-9a-f]/.test(s); }
  function isSize(n) { return typeof n === 'number' && n >= 0 && n <= MAX_SAFE && Math.floor(n) === n; }
  function validPlan(b) {
    var total = 0, i, op, kind, off, size, ops;
    if (!b || !isSize(b.inSize) || !isSize(b.outSize) || !b.inSize || !b.outSize) return false;
    if (!nonzero(b.inSha) || !nonzero(b.outSha)) return false;
    ops = b.ops;
    if (!ops || !ops.length || ops.length > MAX_OPS) return false;
    for (i = 0; i < ops.length; i++) {
      op = ops[i]; kind = op[0]; off = op[1]; size = op[2];
      if (!isSize(off) || !isSize(size)) return false;
      if (!size || size > b.outSize - total) return false;
      if (kind === COPY || kind === XOR) {
        if (off > b.inSize || size > b.inSize - off) return false;
      } else if (kind === LITERAL) {
        if (off) return false;
      } else return false;
      /* COPY never carries bytes; XOR/LITERAL always consume the bundle's data in order. */
      total += size;
    }
    return total === b.outSize;
  }
  function dataSize(b) {
    var n = 0, i;
    for (i = 0; i < b.ops.length; i++) if (b.ops[i][0] !== COPY) n += b.ops[i][2];
    return n;
  }

  /* ---- identification ---- */
  function identify(catalog, size, sha) {
    var guide = null, matches = [], outs = {}, i, g, b;
    for (i = 0; i < catalog.guide.length; i++) {
      g = catalog.guide[i]; if (g.bytes === size && g.sha === sha) guide = g;
    }
    for (i = 0; i < catalog.bundles.length; i++) {
      b = catalog.bundles[i];
      if (b.inSize === size && b.inSha === sha) { matches.push(b); outs[b.outSha] = 1; }
    }
    if (matches.length && Object.keys(outs).length > 1) return { guide: guide, bundle: null, ambiguous: true };
    return { guide: guide, bundle: matches[0] || null, ambiguous: false };
  }

  function fail(code, extra) { var e = new Error(code); e.code = code; if (extra) e.detail = extra; return e; }

  /* build(bundle, input, data, {onStage(stage), pureJs}) -> Promise<{bytes, sha}>
   * stage: 'check' -> 'apply' -> 'verify' */
  function build(b, input, data, opt) {
    opt = opt || {};
    var stage = opt.onStage || function () {}, ho = { pureJs: !!opt.pureJs };
    if (!validPlan(b)) return Promise.reject(fail('invalid-plan'));
    if (!(input instanceof Uint8Array) || !(data instanceof Uint8Array)) return Promise.reject(fail('invalid-plan'));
    stage('check');
    if (input.length !== b.inSize) return Promise.reject(fail('input-mismatch'));
    if (data.length !== dataSize(b) || (b.data && b.data.size !== data.length)) return Promise.reject(fail('data-mismatch'));
    return sha256(input, ho).then(function (h) {
      if (h !== b.inSha) throw fail('input-mismatch');
      return b.data && b.data.sha ? sha256(data, ho) : null;
    }).then(function (h) {
      if (h !== null && h !== b.data.sha) throw fail('data-mismatch');
      stage('apply');
      return tick();
    }).then(function () {
      var out, pos = 0, d = 0, i, j, op, off, size;
      try { out = new Uint8Array(b.outSize); } catch (e) { throw fail('memory'); }
      for (i = 0; i < b.ops.length; i++) {
        op = b.ops[i]; off = op[1]; size = op[2];
        if (op[0] === COPY) out.set(input.subarray(off, off + size), pos);
        else if (op[0] === XOR) { for (j = 0; j < size; j++) out[pos + j] = input[off + j] ^ data[d + j]; d += size; }
        else { out.set(data.subarray(d, d + size), pos); d += size; }
        pos += size;
      }
      if (pos !== b.outSize || d !== data.length) throw fail('invalid-plan');
      stage('verify');
      return sha256(input, ho).then(function (h) {
        if (h !== b.inSha) throw fail('input-mismatch');
        return sha256(out, ho);
      }).then(function (h) {
        if (out.length !== b.outSize || h !== b.outSha) throw fail('output-mismatch');
        return { bytes: out, sha: h };
      });
    });
  }

  /* base64 -> Uint8Array without relying on Buffer */
  function fromBase64(s) {
    var bin = (typeof atob === 'function') ? atob(s) : Buffer.from(s, 'base64').toString('binary');
    var u = new Uint8Array(bin.length), i;
    for (i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
    return u;
  }

  return { COPY: COPY, LITERAL: LITERAL, XOR: XOR, MAX_OPS: MAX_OPS, Sha256: Sha256, sha256: sha256,
           validPlan: validPlan, dataSize: dataSize, identify: identify, build: build, fromBase64: fromBase64,
           hasSubtle: function () { return !!subtle(); } };
});
