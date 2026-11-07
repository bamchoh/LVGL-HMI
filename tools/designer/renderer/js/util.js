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

  debounce(fn, ms) { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; },
};
