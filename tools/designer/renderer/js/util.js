// Small DOM / JSON helpers shared by the designer modules (classic scripts, global `U`).
'use strict';

const U = {
  // el('div', {class:'x', onclick: fn, title:'..'}, child, 'text', ...)
  el(tag, attrs, ...children) {
    const e = document.createElement(tag);
    if (attrs) {
      for (const [k, v] of Object.entries(attrs)) {
        if (v === undefined || v === null || v === false) continue;
        if (k === 'class') e.className = v;
        else if (k === 'style' && typeof v === 'object') Object.assign(e.style, v);
        else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v);
        else if (k === 'dataset') Object.assign(e.dataset, v);
        else if (k in e && k !== 'list') { try { e[k] = v; } catch (_) { e.setAttribute(k, v); } }
        else e.setAttribute(k, v);
      }
    }
    for (const c of children.flat()) {
      if (c === undefined || c === null || c === false) continue;
      e.append(c instanceof Node ? c : document.createTextNode(String(c)));
    }
    return e;
  },

  svg(tag, attrs, ...children) {
    const e = document.createElementNS('http://www.w3.org/2000/svg', tag);
    if (attrs) for (const [k, v] of Object.entries(attrs)) if (v !== undefined && v !== null) e.setAttribute(k, v);
    for (const c of children.flat()) if (c) e.append(c);
    return e;
  },

  clone(o) { return o === undefined ? undefined : JSON.parse(JSON.stringify(o)); },

  // JSON with // and /* */ comments (the runtime accepts them too)
  parseLooseJson(text) {
    if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1);
    try { return JSON.parse(text); } catch (_) { /* fall through */ }
    let out = '', i = 0, inStr = false;
    while (i < text.length) {
      const c = text[i], n = text[i + 1];
      if (inStr) {
        out += c;
        if (c === '\\') { out += n || ''; i += 2; continue; }
        if (c === '"') inStr = false;
        i++; continue;
      }
      if (c === '"') { inStr = true; out += c; i++; continue; }
      if (c === '/' && n === '/') { while (i < text.length && text[i] !== '\n') i++; continue; }
      if (c === '/' && n === '*') { i += 2; while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) i++; i += 2; continue; }
      out += c; i++;
    }
    return JSON.parse(out);
  },

  toJson(obj) { return JSON.stringify(obj, null, 2) + '\n'; },

  PALETTE: {
    red: '#F44336', pink: '#E91E63', purple: '#9C27B0', deep_purple: '#673AB7', indigo: '#3F51B5',
    blue: '#2196F3', light_blue: '#03A9F4', cyan: '#00BCD4', teal: '#009688', green: '#4CAF50',
    light_green: '#8BC34A', lime: '#CDDC39', yellow: '#FFEB3B', amber: '#FFC107', orange: '#FF9800',
    deep_orange: '#FF5722', brown: '#795548', grey: '#9E9E9E', gray: '#9E9E9E', blue_grey: '#607D8B',
    white: '#FFFFFF', black: '#000000',
  },

  cssColor(v, fallback) {
    if (v === undefined || v === null || v === '') return fallback;
    if (typeof v === 'number') return '#' + v.toString(16).padStart(6, '0');
    const s = String(v).trim();
    if (s[0] === '#') return s;
    if (/^[0-9a-fA-F]{6}$/.test(s)) return '#' + s;
    if (/^0x[0-9a-fA-F]{6}$/.test(s)) return '#' + s.slice(2);
    return U.PALETTE[s.toLowerCase()] || fallback;
  },

  // value → "#rrggbb" for <input type=color>
  hex6(v, fallback) {
    const c = U.cssColor(v, fallback) || '#000000';
    if (/^#[0-9a-fA-F]{3}$/.test(c)) return '#' + c[1] + c[1] + c[2] + c[2] + c[3] + c[3];
    return c.slice(0, 7);
  },

  cssSize(v) {
    if (v === undefined || v === null || v === '') return null;
    if (typeof v === 'number') return v + 'px';
    const s = String(v).trim().toLowerCase();
    if (s === 'content') return 'auto';
    if (s === 'full') return '100%';
    if (s.endsWith('%')) return s;
    const n = parseFloat(s);
    return isNaN(n) ? null : n + 'px';
  },

  num(v, d) { const n = typeof v === 'number' ? v : parseFloat(v); return isNaN(n) ? d : n; },

  // "0.2.1" <-> [0,2,1]
  pathStr(p) { return p ? p.join('.') : ''; },
  pathArr(s) { return s === '' || s == null ? [] : String(s).split('.').map(Number); },
  samePath(a, b) { return !!a && !!b && a.length === b.length && a.every((v, i) => v === b[i]); },

  basename(p) { return String(p).replace(/\\/g, '/').split('/').pop(); },
  dirname(p) { const s = String(p).replace(/\\/g, '/'); const i = s.lastIndexOf('/'); return i < 0 ? '.' : s.slice(0, i); },
  joinPath(a, b) { return String(a).replace(/[\\/]+$/, '') + '/' + String(b).replace(/^[\\/]+/, ''); },
  fileUrl(p) { let s = String(p).replace(/\\/g, '/'); if (!s.startsWith('/')) s = '/' + s; return 'file://' + encodeURI(s); },

  esc(s) { return String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); },

  // keydown handler for code textareas: Tab inserts two spaces instead of leaving the field
  tabKey(e) {
    if (e.key !== 'Tab' || e.shiftKey || e.ctrlKey || e.altKey) return;
    e.preventDefault();
    const t = e.target, a = t.selectionStart, b = t.selectionEnd;
    t.value = t.value.slice(0, a) + '  ' + t.value.slice(b);
    t.selectionStart = t.selectionEnd = a + 2;
    t.dispatchEvent(new Event('input', { bubbles: true }));
  },

  debounce(fn, ms) { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; },

  // SHA-256 of a string (UTF-8), as lower-case hex. Used for the PIN hashes of the users; the
  // runtime computes the same (src/users.cpp).
  sha256hex(text) {
    const K = [0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
      0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
      0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
      0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
      0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
      0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2];
    const bytes = [];
    for (const ch of unescape(encodeURIComponent(String(text)))) bytes.push(ch.charCodeAt(0));
    const bits = bytes.length * 8;
    bytes.push(0x80);
    while (bytes.length % 64 !== 56) bytes.push(0);
    for (let i = 7; i >= 0; i--) bytes.push(i >= 4 ? Math.floor(bits / 2 ** 32) >>> ((i - 4) * 8) & 0xff : bits >>> (i * 8) & 0xff);
    const h = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
    const rotr = (x, n) => (x >>> n) | (x << (32 - n));
    const w = new Array(64);
    for (let off = 0; off < bytes.length; off += 64) {
      for (let i = 0; i < 16; i++) w[i] = (bytes[off + i * 4] << 24) | (bytes[off + i * 4 + 1] << 16) | (bytes[off + i * 4 + 2] << 8) | bytes[off + i * 4 + 3];
      for (let i = 16; i < 64; i++) {
        const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
        const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
      }
      let [a, b, c, d, e, f, g, hh] = h;
      for (let i = 0; i < 64; i++) {
        const t1 = (hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i]) | 0;
        const t2 = ((rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) | 0;
        hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
      }
      [a, b, c, d, e, f, g, hh].forEach((v, i) => { h[i] = (h[i] + v) | 0; });
    }
    return h.map(v => (v >>> 0).toString(16).padStart(8, '0')).join('');
  },
};
