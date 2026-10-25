/* BootBridge web: write the rule pack into a rebuilt base image.
 * Browser port of build/inject_pack.py: fill the reserved LEB data areas,
 * recompute the FIT crc32/sha1 over the UBI data, fix the H3C header word sum.
 * SPDX-License-Identifier: GPL-2.0-or-later */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.BBInject = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  var CRC = (function () {
    var t = new Int32Array(256), c, n, k;
    for (n = 0; n < 256; n++) {
      c = n;
      for (k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c;
    }
    return t;
  })();

  function crc32(u8) {
    var c = -1, i;
    for (i = 0; i < u8.length; i++) c = CRC[(c ^ u8[i]) & 0xff] ^ (c >>> 8);
    return (c ^ -1) >>> 0;
  }

  /* SHA-1, pure JS: the page also runs from file:// where WebCrypto may be absent. */
  function sha1(u8) {
    var h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476, h4 = 0xc3d2e1f0;
    var len = u8.length, blocks = ((len + 8) >> 6) + 1, w = new Int32Array(80), i, j, t, a, b, c, d, e;
    var buf = new Uint8Array(blocks * 64);
    buf.set(u8);
    buf[len] = 0x80;
    var bits = len * 8;
    buf[buf.length - 4] = bits >>> 24; buf[buf.length - 3] = bits >>> 16;
    buf[buf.length - 2] = bits >>> 8; buf[buf.length - 1] = bits;
    buf[buf.length - 5] = Math.floor(len / 0x20000000);
    for (i = 0; i < buf.length; i += 64) {
      for (j = 0; j < 16; j++)
        w[j] = buf[i + 4 * j] << 24 | buf[i + 4 * j + 1] << 16 | buf[i + 4 * j + 2] << 8 | buf[i + 4 * j + 3];
      for (j = 16; j < 80; j++) { t = w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16]; w[j] = t << 1 | t >>> 31; }
      a = h0; b = h1; c = h2; d = h3; e = h4;
      for (j = 0; j < 80; j++) {
        if (j < 20) t = (b & c | ~b & d) + 0x5a827999;
        else if (j < 40) t = (b ^ c ^ d) + 0x6ed9eba1;
        else if (j < 60) t = (b & c | b & d | c & d) + 0x8f1bbcdc;
        else t = (b ^ c ^ d) + 0xca62c1d6;
        t = (a << 5 | a >>> 27) + t + e + w[j] | 0;
        e = d; d = c; c = b << 30 | b >>> 2; b = a; a = t;
      }
      h0 = h0 + a | 0; h1 = h1 + b | 0; h2 = h2 + c | 0; h3 = h3 + d | 0; h4 = h4 + e | 0;
    }
    var out = new Uint8Array(20), hs = [h0, h1, h2, h3, h4];
    for (i = 0; i < 5; i++) { out[4 * i] = hs[i] >>> 24; out[4 * i + 1] = hs[i] >>> 16; out[4 * i + 2] = hs[i] >>> 8; out[4 * i + 3] = hs[i]; }
    return out;
  }

  function be32(u8, off, v) {
    u8[off] = v >>> 24; u8[off + 1] = v >>> 16; u8[off + 2] = v >>> 8; u8[off + 3] = v;
  }

  function fail(code) { var e = new Error(code); e.code = code; return e; }

  /* layout: bb-layout-v1 from build/build_bridge.py. Modifies `base` in place. */
  function apply(base, layout, pack) {
    var p = layout.pack, f = layout.fit, h = layout.header, pos = 0, i, n, sum, data, crc, sha;
    if (!(base instanceof Uint8Array) || base.length !== layout.base.bytes) throw fail('inject-base');
    if (!(pack instanceof Uint8Array) || pack.length > p.capacity || pack.length < 64 ||
        pack[0] !== 0x42 || pack[1] !== 0x42 || pack[2] !== 0x50 || pack[3] !== 0x4b) throw fail('inject-pack');
    for (i = 0; i < p.slots.length && pos < pack.length; i++) {
      n = Math.min(p.slot_bytes, pack.length - pos);
      base.set(pack.subarray(pos, pos + n), p.slots[i]);
      pos += n;
    }
    if (pos !== pack.length) throw fail('inject-pack');
    data = base.subarray(f.data_offset, f.data_offset + f.data_len);
    crc = crc32(data);
    be32(base, f.crc32_offset, crc);
    sha = sha1(data);
    base.set(sha, f.sha1_offset);
    be32(base, h.checksum_offset, 0);
    sum = 0;
    for (i = h.sum_from; i + 3 < base.length; i += 4)
      sum = (sum + (base[i] << 24 | base[i + 1] << 16 | base[i + 2] << 8 | base[i + 3])) >>> 0;
    be32(base, h.checksum_offset, (0xffffffff - sum) >>> 0);
    return base;
  }

  return { apply: apply, crc32: crc32, sha1: sha1 };
});
