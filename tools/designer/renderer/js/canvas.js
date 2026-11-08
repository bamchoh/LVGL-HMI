// Approximate rendering of a screen with HTML/CSS/SVG, selection, drag to move, drag to resize.
// The real look comes from the Preview button (hml_runtime renders the screen headless).
'use strict';

const Canvas = {
  root: null, wrap: null, drag: null,

  init() {
    this.root = document.getElementById('canvas');
    this.wrap = document.getElementById('canvas-wrap');
    this.root.addEventListener('mousedown', e => this.onDown(e));
    this.wrap.addEventListener('mousedown', e => { if (e.target === this.wrap || e.target === this.wrap.firstElementChild) App.select(null); });
    document.addEventListener('mousemove', e => this.onMove(e));
    document.addEventListener('mouseup', e => this.onUp(e));
  },

  theme() {
    const t = (M.st.project && M.st.project.theme) || {};
    const dark = t.dark !== false;
    return {
      dark,
      primary: U.cssColor(t.primary, '#2196f3'),
      secondary: U.cssColor(t.secondary, '#ff9800'),
      font: U.num(t.font, 16),
      scr: dark ? '#15171a' : '#eaeff3',
      card: dark ? '#282b30' : '#ffffff',
      text: dark ? '#e0e3e8' : '#2c3137',
      grey: dark ? '#3a3e45' : '#dfe4e8',
      border: dark ? '#44484f' : '#d0d7dd',
      muted: dark ? '#8a93a0' : '#6b7580',
    };
  },

  render() {
    const root = this.root, scr = M.screen();
    root.innerHTML = '';
    if (!scr) { root.style.display = 'none'; return; }
    root.style.display = '';
    const { w: W, h: H } = M.display(), th = this.theme(), z = M.st.zoom;
    root.style.width = W + 'px'; root.style.height = H + 'px';
    root.style.transform = 'scale(' + z + ')';
    root.parentElement.style.width = Math.round(W * z) + 'px';
    root.parentElement.style.height = Math.round(H * z) + 'px';
    root.style.setProperty('--lv-primary', th.primary);
    root.style.setProperty('--lv-secondary', th.secondary);
    root.style.setProperty('--lv-text', th.text);
    root.style.setProperty('--lv-card', th.card);
    root.style.setProperty('--lv-grey', th.grey);
    root.style.setProperty('--lv-border', th.border);
    root.style.setProperty('--lv-muted', th.muted);
    root.style.fontSize = th.font + 'px';
    root.style.color = th.text;
    root.style.background = th.scr;
    root.dataset.path = '';
    root.className = 'lv lv-screen' + (U.samePath(M.st.sel, []) ? ' hd-selected' : '');
    const content = this.container(root, scr, th, 0);
    (scr.children || []).forEach((c, i) => content.append(this.build(c, [i], th, scr.flex ? scr : null)));
  },

  // ---- style helpers -----------------------------------------------------
  style(w) { return (w && w.style && typeof w.style === 'object') ? w.style : {}; },
  pad(st, def) {
    const p = U.num(st.pad, def);
    return { l: U.num(st.pad_left, p), r: U.num(st.pad_right, p), t: U.num(st.pad_top, p), b: U.num(st.pad_bottom, p),
             row: U.num(st.pad_row, U.num(st.pad_gap, 10)), col: U.num(st.pad_column, U.num(st.pad_gap, 10)) };
  },
  applyStyle(el, st, th, opt) {
    opt = opt || {};
    const s = el.style;
    if (st.bg_color !== undefined) s.background = U.cssColor(st.bg_color, '');
    if (st.bg_grad_color !== undefined) s.background = 'linear-gradient(' + (st.bg_grad_dir === 'hor' ? 'to right' : 'to bottom') + ',' + U.cssColor(st.bg_color, th.card) + ',' + U.cssColor(st.bg_grad_color, th.card) + ')';
    if (st.bg_opa !== undefined && U.num(st.bg_opa, 100) === 0) s.background = 'transparent';
    if (st.radius !== undefined) s.borderRadius = U.num(st.radius, 0) + 'px';
    if (st.border_width !== undefined) s.borderWidth = U.num(st.border_width, 0) + 'px';
    if (st.border_color !== undefined) s.borderColor = U.cssColor(st.border_color, th.border);
    if (st.border_width !== undefined || st.border_color !== undefined) s.borderStyle = 'solid';
    if (st.outline_width) s.outline = U.num(st.outline_width, 0) + 'px solid ' + U.cssColor(st.outline_color, th.primary);
    if (st.text_color !== undefined) s.color = U.cssColor(st.text_color, th.text);
    if (st.font !== undefined) s.fontSize = U.num(st.font, th.font) + 'px';
    if (st.text_align) s.textAlign = st.text_align;
    if (st.opa !== undefined) s.opacity = U.num(st.opa, 100) / 100;
    if (st.shadow_width) s.boxShadow = '0 0 ' + U.num(st.shadow_width, 0) + 'px ' + U.cssColor(st.shadow_color, '#000');
    if (st.text_letter_space) s.letterSpacing = st.text_letter_space + 'px';
    if (opt.pad !== undefined) {
      const p = this.pad(st, opt.pad);
      s.padding = p.t + 'px ' + p.r + 'px ' + p.b + 'px ' + p.l + 'px';
    }
  },

  // ---- placement ---------------------------------------------------------
  defaultSize(type) {
    switch (type) {
      case 'panel': case 'container': case 'obj': return [130, 130];
      case 'slider': case 'bar': return [260, 13];
      case 'arc': case 'spinner': return [130, 130];
      case 'switch': return [52, 26];
      case 'led': return [26, 26];
      case 'chart': return [390, 260];
      case 'dropdown': return [130, null];
      case 'textarea': return [130, 130];
      case 'numeric_display': return [120, null];
      case 'text_display': case 'password_display': return [160, null];
      case 'keyboard': return ['100%', '50%'];
      default: return [null, null];
    }
  },
  place(el, w, parentFlex) {
    const s = el.style;
    const [dw, dh] = this.defaultSize(w.type);
    const cw = U.cssSize(w.w !== undefined ? w.w : dw), ch = U.cssSize(w.h !== undefined ? w.h : dh);
    if (cw) s.width = cw;
    if (ch) s.height = ch;
    if (parentFlex) {
      s.position = 'relative';
      if (w.flex_grow) s.flexGrow = w.flex_grow;
      return;
    }
    s.position = 'absolute';
    const x = U.num(w.x, 0), y = U.num(w.y, 0);
    const a = String(w.align || 'top_left').toLowerCase();
    const h = a.includes('left') ? 'l' : a.includes('right') ? 'r' : (a === 'center' || a.endsWith('_mid') || a === 'top' || a === 'bottom') ? 'c' : 'l';
    const v = a.startsWith('top') ? 't' : a.startsWith('bottom') ? 'b' : (a === 'center' || a.includes('_mid') || a === 'left' || a === 'right') ? 'm' : 't';
    let tx = '0', ty = '0';
    if (h === 'l') s.left = x + 'px';
    else if (h === 'r') s.right = (-x) + 'px';
    else { s.left = 'calc(50% + ' + x + 'px)'; tx = '-50%'; }
    if (v === 't') s.top = y + 'px';
    else if (v === 'b') s.bottom = (-y) + 'px';
    else { s.top = 'calc(50% + ' + y + 'px)'; ty = '-50%'; }
    if (tx !== '0' || ty !== '0') s.transform = 'translate(' + tx + ',' + ty + ')';
  },

  // content box of a container (children are positioned relative to the padding edge in LVGL)
  container(el, w, th, defPad) {
    const st = this.style(w), p = this.pad(st, defPad);
    el.style.padding = p.t + 'px ' + p.r + 'px ' + p.b + 'px ' + p.l + 'px';
    const c = U.el('div', { class: 'lv-content' });
    if (w.flex) {
      const f = String(w.flex).toLowerCase();
      c.style.display = 'flex';
      c.style.flexDirection = f.startsWith('column') ? (f.endsWith('reverse') ? 'column-reverse' : 'column') : (f.endsWith('reverse') ? 'row-reverse' : 'row');
      c.style.flexWrap = f.includes('wrap') ? 'wrap' : 'nowrap';
      const map = { start: 'flex-start', center: 'center', end: 'flex-end', space_between: 'space-between', space_around: 'space-around', space_evenly: 'space-evenly' };
      c.style.justifyContent = map[w.flex_main] || 'flex-start';
      c.style.alignItems = map[w.flex_cross] || 'flex-start';
      c.style.alignContent = map[w.flex_track] || 'flex-start';
      c.style.rowGap = p.row + 'px'; c.style.columnGap = p.col + 'px';
    }
    el.append(c);
    return c;
  },

  // Evaluate "visible" / "hidden" bindings against the tags' initial values, so that e.g. the
  // three NORMAL / WARNING / ALARM labels of a demo do not all show at once.
  tagInit(name) {
    const t = ((M.st.project && M.st.project.tags) || []).find(x => x.name === name);
    if (!t) return undefined;
    if (t.init === undefined) return t.type === 'string' ? '' : t.type === 'bool' ? false : 0;
    return t.init;
  },
  condValue(cfg) {
    const tag = typeof cfg === 'string' ? cfg : cfg && cfg.tag;
    const v = this.tagInit(tag);
    if (v === undefined) return true;
    let r;
    const c = typeof cfg === 'object' && cfg ? cfg : {};
    const num = x => typeof x === 'boolean' ? (x ? 1 : 0) : typeof x === 'string' ? (parseFloat(x) || 0) : x;
    if (c.eq !== undefined) r = String(v) === String(c.eq) || num(v) === num(c.eq);
    else if (c.ne !== undefined) r = !(String(v) === String(c.ne) || num(v) === num(c.ne));
    else if (c.gt !== undefined) r = num(v) > num(c.gt);
    else if (c.lt !== undefined) r = num(v) < num(c.lt);
    else if (c.ge !== undefined) r = num(v) >= num(c.ge);
    else if (c.le !== undefined) r = num(v) <= num(c.le);
    else r = typeof v === 'string' ? v !== '' : !!num(v);
    return c.invert ? !r : r;
  },
  condVisible(w) {
    const b = w.bind;
    if (!b || typeof b !== 'object') return true;
    if (b.visible !== undefined && !this.condValue(b.visible)) return false;
    if (b.hidden !== undefined && this.condValue(b.hidden)) return false;
    return true;
  },

  // ---- widgets -----------------------------------------------------------
  build(w, path, th, parentFlex) {
    const type = w.type || 'panel';
    const el = U.el('div', { class: 'lv lv-' + type, dataset: { path: U.pathStr(path) } });
    if (U.samePath(M.st.sel, path)) el.classList.add('hd-selected');
    if (w.hidden) el.classList.add('lv-hidden');
    if (w.disabled) el.classList.add('lv-disabled');
    if (!this.condVisible(w)) el.classList.add('lv-cond-hidden');
    this.place(el, w, parentFlex);
    const st = this.style(w);
    const fn = this['w_' + type] || this.w_panel;
    fn.call(this, el, w, st, th, path);
    this.applyStyle(el, st, th, {});
    if (U.samePath(M.st.sel, path) && !parentFlex) el.append(U.el('div', { class: 'hd-rs', title: 'drag to resize' }));
    return el;
  },

  w_panel(el, w, st, th, path) {
    el.classList.add('lv-card');
    const c = this.container(el, w, th, 14);
    (w.children || []).forEach((ch, i) => c.append(this.build(ch, path.concat(i), th, w.flex ? w : null)));
  },
  w_container(el, w, st, th, path) { this.w_panel(el, w, st, th, path); },
  w_obj(el, w, st, th, path) { this.w_panel(el, w, st, th, path); },

  labelText(w) {
    if (w.text !== undefined && w.text !== null && !w.bind) return String(w.text);
    const b = w.bind;
    if (typeof b === 'string') return '{' + b + '}';
    if (b && typeof b === 'object' && b.text !== undefined) {
      const t = b.text;
      if (typeof t === 'string') return '{' + t + '}';
      if (t && t.format) return t.format.replace(/%%/g, '\u0000').replace(/%[-+ 0#]*\d*(?:\.\d+)?(hh|h|ll|l|L|z)?[diouxX]/g, '0')
        .replace(/%[-+ 0#]*\d*(?:\.\d+)?(hh|h|ll|l|L|z)?[fFeEgG]/g, '0.0').replace(/%[-+ 0#]*\d*(?:\.\d+)?s/g, '{' + (t.tag || '') + '}').replace(/\u0000/g, '%');
      if (t && t.tag) return '{' + t.tag + '}';
    }
    return w.text !== undefined ? String(w.text) : 'Label';
  },
  w_label(el, w, st) {
    el.textContent = this.labelText(w);
    el.style.whiteSpace = (w.w !== undefined && w.long_mode !== 'scroll' && w.long_mode !== 'circular') ? 'pre-wrap' : 'pre';
    if (w.long_mode === 'dots' || w.long_mode === 'clip') { el.style.overflow = 'hidden'; el.style.textOverflow = w.long_mode === 'dots' ? 'ellipsis' : 'clip'; el.style.whiteSpace = 'nowrap'; }
  },
  w_button(el, w, st, th) {
    const lbl = U.el('span', { class: 'lv-btn-label' }, w.text !== undefined ? String(w.text) : (w.bind ? this.labelText(w) : ''));
    if (w.style_label) this.applyStyle(lbl, w.style_label, th, {});
    el.append(lbl);
    if (w.checked) el.classList.add('lv-checked');
  },
  w_switch(el, w) { el.append(U.el('div', { class: 'lv-knob' })); if (w.checked) el.classList.add('lv-checked'); },
  w_checkbox(el, w) {
    el.append(U.el('div', { class: 'lv-box' + (w.checked ? ' lv-checked' : '') }, w.checked ? '✓' : ''), U.el('span', null, w.text !== undefined ? String(w.text) : 'Check'));
  },
  ratio(w) { const mn = U.num(w.min, 0), mx = U.num(w.max, 100); const v = U.num(w.value, mn + (mx - mn) * 0.4); return mx === mn ? 0 : Math.max(0, Math.min(1, (v - mn) / (mx - mn))); },
  w_bar(el, w, st, th) {
    const ind = U.el('div', { class: 'lv-ind' }); ind.style.width = (this.ratio(w) * 100) + '%';
    if (w.style_indicator) this.applyStyle(ind, w.style_indicator, th, {});
    el.append(ind);
  },
  w_slider(el, w, st, th) {
    const r = this.ratio(w);
    const ind = U.el('div', { class: 'lv-ind' }); ind.style.width = (r * 100) + '%';
    const knob = U.el('div', { class: 'lv-knob' }); knob.style.left = 'calc(' + (r * 100) + '% - 10px)';
    if (w.style_indicator) this.applyStyle(ind, w.style_indicator, th, {});
    if (w.style_knob) this.applyStyle(knob, w.style_knob, th, {});
    el.append(ind, knob);
  },
  arcPath(cx, cy, r, a0, a1) {
    const rad = d => d * Math.PI / 180;
    let sweep = ((a1 - a0) % 360 + 360) % 360; if (sweep === 0 && a1 !== a0) sweep = 360;
    if (sweep >= 360) sweep = 359.99;
    const x0 = cx + r * Math.cos(rad(a0)), y0 = cy + r * Math.sin(rad(a0));
    const x1 = cx + r * Math.cos(rad(a0 + sweep)), y1 = cy + r * Math.sin(rad(a0 + sweep));
    return 'M' + x0.toFixed(2) + ' ' + y0.toFixed(2) + ' A' + r + ' ' + r + ' 0 ' + (sweep > 180 ? 1 : 0) + ' 1 ' + x1.toFixed(2) + ' ' + y1.toFixed(2);
  },
  w_arc(el, w, st, th) {
    const a0 = U.num(w.start_angle, 135), a1 = U.num(w.end_angle, 45), rot = U.num(w.rotation, 0);
    const width = U.num(st.arc_width, 10);
    const indSt = w.style_indicator || {};
    const indW = U.num(indSt.arc_width, width);
    const svg = U.svg('svg', { viewBox: '0 0 100 100', preserveAspectRatio: 'none', class: 'lv-svg' });
    const r = 50 - Math.max(width, indW) / 2 * (100 / Math.max(U.num(w.w, 130), 1));
    const sweep = ((a1 - a0) % 360 + 360) % 360 || 360;
    svg.append(U.svg('path', { d: this.arcPath(50, 50, r, a0 + rot, a0 + rot + sweep), fill: 'none', stroke: U.cssColor(st.arc_color, th.grey), 'stroke-width': width, 'vector-effect': 'non-scaling-stroke', 'stroke-linecap': 'round' }));
    svg.append(U.svg('path', { d: this.arcPath(50, 50, r, a0 + rot, a0 + rot + sweep * this.ratio(w)), fill: 'none', stroke: U.cssColor(indSt.arc_color, th.primary), 'stroke-width': indW, 'vector-effect': 'non-scaling-stroke', 'stroke-linecap': 'round' }));
    el.append(svg);
    if (w.adjustable !== false) {
      const ang = (a0 + rot + sweep * this.ratio(w)) * Math.PI / 180;
      const knob = U.el('div', { class: 'lv-knob' });
      knob.style.left = 'calc(' + (50 + r * Math.cos(ang)) + '% - 9px)'; knob.style.top = 'calc(' + (50 + r * Math.sin(ang)) + '% - 9px)';
      if (w.style_knob) this.applyStyle(knob, w.style_knob, th, {});
      el.append(knob);
    }
  },
  w_led(el, w, st, th) {
    const c = U.cssColor(w.color, th.primary);
    el.style.background = c; el.style.boxShadow = '0 0 8px ' + c;
    if (w.on === false || (w.on === undefined && w.bind)) { el.style.opacity = 0.35; el.style.boxShadow = 'none'; }
  },
  w_dropdown(el, w) {
    const opts = Array.isArray(w.options) ? w.options : String(w.options || '').split('\n');
    el.append(U.el('span', { class: 'lv-grow' }, opts[U.num(w.selected, 0)] || ''), U.el('span', null, '▾'));
  },
  w_roller(el, w, st, th) {
    const opts = Array.isArray(w.options) ? w.options : String(w.options || '').split('\n');
    const rows = Math.max(1, U.num(w.visible_rows, 3)), sel = U.num(w.selected, 0);
    for (let i = 0; i < rows; i++) {
      const idx = sel - Math.floor(rows / 2) + i;
      const row = U.el('div', { class: 'lv-row' + (idx === sel ? ' lv-sel' : '') }, opts[((idx % opts.length) + opts.length) % opts.length] || '');
      if (idx === sel && w.style_selected) this.applyStyle(row, w.style_selected, th, {});
      el.append(row);
    }
  },
  w_textarea(el, w) {
    const txt = w.text ? String(w.text) : '';
    el.append(U.el('span', { class: txt ? '' : 'lv-placeholder' }, txt || w.placeholder || ''));
    if (w.one_line) el.classList.add('lv-oneline');
  },
  // data displays (textarea based): one line, dashed border while display-only
  bindTag(w, kind) {
    const b = w.bind;
    if (typeof b === 'string') return b;
    if (b && typeof b === 'object' && b[kind] !== undefined) return typeof b[kind] === 'string' ? b[kind] : (b[kind].tag || '');
    return '';
  },
  fmtSample(fmt) {
    return String(fmt).replace(/%%/g, '\u0000').replace(/%[-+ 0#]*\d*(?:\.\d+)?(hh|h|ll|l|L|z)?[diouxX]/g, '0')
      .replace(/%[-+ 0#]*\d*(?:\.(\d+))?(hh|h|ll|l|L|z)?[fFeEgG]/g, (m, prec) => (0).toFixed(prec === undefined ? 1 : Number(prec)))
      .replace(/%[-+ 0#]*\d*(?:\.\d+)?s/g, '0').replace(/\u0000/g, '%');
  },
  dataDisplay(el, w, body, input) {
    el.classList.add('lv-textarea', 'lv-oneline', 'lv-data');
    if (!input) el.classList.add('lv-readonly');
    el.append(body);
  },
  w_numeric_display(el, w) {
    const fmt = w.format || (w.decimals !== undefined ? '%.' + w.decimals + 'f' : '%s');
    const tag = this.bindTag(w, 'value');
    const text = (tag ? this.fmtSample(fmt) : (w.text || this.fmtSample(fmt))) + (w.unit || '');
    this.dataDisplay(el, w, U.el('span', { class: 'lv-grow lv-num', title: tag ? '{' + tag + '}' : '' }, text), !!w.input);
  },
  w_text_display(el, w) {
    const tag = this.bindTag(w, 'text');
    const txt = w.text ? String(w.text) : (tag ? '{' + tag + '}' : '');
    this.dataDisplay(el, w, U.el('span', { class: 'lv-grow' + (txt ? '' : ' lv-placeholder') }, txt || w.placeholder || ''), !!w.input);
  },
  w_password_display(el, w) {
    const n = Math.min(U.num(w.max_length, 6) || 6, 12);
    this.dataDisplay(el, w, U.el('span', { class: 'lv-grow' }, '\u2022'.repeat(n)), w.input !== false);
  },
  w_image(el, w) {
    if (w.src && M.st.dir) {
      const img = U.el('img', { src: U.fileUrl(U.joinPath(M.st.dir, w.src)), draggable: false });
      if (w.zoom) img.style.transform = 'scale(' + w.zoom + ')';
      if (w.rotation) img.style.transform = (img.style.transform || '') + ' rotate(' + w.rotation + 'deg)';
      el.append(img);
    } else el.append(U.el('div', { class: 'lv-img-ph' }, w.src || 'image'));
  },
  w_chart(el, w, st, th) {
    const svg = U.svg('svg', { viewBox: '0 0 100 100', preserveAspectRatio: 'none', class: 'lv-svg' });
    const line = U.cssColor(st.line_color, th.border);
    const hd = U.num(w.hdiv, 3), vd = U.num(w.vdiv, 5);
    for (let i = 0; i < hd; i++) { const y = hd === 1 ? 50 : (i / (hd - 1)) * 100; svg.append(U.svg('line', { x1: 0, y1: y, x2: 100, y2: y, stroke: line, 'stroke-width': 1, 'vector-effect': 'non-scaling-stroke' })); }
    for (let i = 0; i < vd; i++) { const x = vd === 1 ? 50 : (i / (vd - 1)) * 100; svg.append(U.svg('line', { x1: x, y1: 0, x2: x, y2: 100, stroke: line, 'stroke-width': 1, 'vector-effect': 'non-scaling-stroke' })); }
    (w.series || []).forEach((s, k) => {
      const pts = [];
      for (let i = 0; i <= 20; i++) { const x = i * 5; const y = 50 - 30 * Math.sin((i / 20) * Math.PI * 2 + k * 1.3) * (1 - k * 0.3); pts.push(x + ',' + y.toFixed(1)); }
      const col = U.cssColor(s.color, k ? th.secondary : th.primary);
      if (w.chart_type === 'bar') pts.forEach((p, i) => { if (i % 2) return; const [x, y] = p.split(','); svg.append(U.svg('rect', { x: +x - 1.5 + k * 1.5, y, width: 1.5, height: 100 - y, fill: col })); });
      else svg.append(U.svg('polyline', { points: pts.join(' '), fill: 'none', stroke: col, 'stroke-width': 2, 'vector-effect': 'non-scaling-stroke' }));
    });
    el.append(svg);
  },
  w_spinner(el, w, st, th) {
    const svg = U.svg('svg', { viewBox: '0 0 100 100', class: 'lv-svg' });
    const width = U.num(st.arc_width, 10);
    svg.append(U.svg('circle', { cx: 50, cy: 50, r: 45, fill: 'none', stroke: U.cssColor(st.arc_color, th.grey), 'stroke-width': width }));
    svg.append(U.svg('path', { d: this.arcPath(50, 50, 45, 0, U.num(w.angle, 60)), fill: 'none', stroke: U.cssColor((w.style_indicator || {}).arc_color, th.primary), 'stroke-width': width, 'stroke-linecap': 'round' }));
    el.append(svg);
  },
  w_keyboard(el) {
    for (const row of ['1234567890', 'qwertyuiop', 'asdfghjkl', 'zxcvbnm']) {
      const r = U.el('div', { class: 'lv-kbrow' });
      for (const ch of row) r.append(U.el('div', { class: 'lv-key' }, ch));
      el.append(r);
    }
    el.append(U.el('div', { class: 'lv-kbrow' }, U.el('div', { class: 'lv-key lv-key-wide' }, 'space'), U.el('div', { class: 'lv-key' }, '✓')));
  },
  w_line(el, w, st, th) {
    const pts = Array.isArray(w.points) ? w.points.filter(p => Array.isArray(p) && p.length >= 2) : [];
    const maxX = Math.max(1, ...pts.map(p => +p[0])), maxY = Math.max(1, ...pts.map(p => +p[1]));
    const lw = U.num(st.line_width, 2);
    el.style.width = (maxX + lw) + 'px'; el.style.height = (maxY + lw) + 'px';
    const svg = U.svg('svg', { viewBox: '0 0 ' + (maxX + lw) + ' ' + (maxY + lw), class: 'lv-svg' });
    svg.append(U.svg('polyline', { points: pts.map(p => (+p[0] + lw / 2) + ',' + (+p[1] + lw / 2)).join(' '), fill: 'none', stroke: U.cssColor(st.line_color, th.primary), 'stroke-width': lw, 'stroke-linecap': st.line_rounded ? 'round' : 'butt' }));
    el.append(svg);
  },

  // ---- mouse -------------------------------------------------------------
  widgetAt(target) { const el = target.closest ? target.closest('.lv') : null; return el ? U.pathArr(el.dataset.path) : null; },
  onDown(e) {
    if (e.button !== 0) return;
    const z = M.st.zoom;
    if (e.target.classList.contains('hd-rs')) {
      const el = e.target.parentElement, path = U.pathArr(el.dataset.path), w = M.get(path);
      this.drag = { mode: 'resize', path, w, sx: e.clientX, sy: e.clientY, w0: el.offsetWidth, h0: el.offsetHeight, pushed: false };
      e.preventDefault(); e.stopPropagation(); return;
    }
    const path = this.widgetAt(e.target);
    if (!path) return;
    e.stopPropagation();
    App.select(path);
    if (!path.length) return;
    const parent = M.parent(path), w = M.get(path);
    if (!w || (parent && parent.flex)) return;
    this.drag = { mode: 'move', path, w, sx: e.clientX, sy: e.clientY, x0: U.num(w.x, 0), y0: U.num(w.y, 0), pushed: false, moved: false };
    e.preventDefault();
  },
  onMove(e) {
    const d = this.drag; if (!d) return;
    const z = M.st.zoom, dx = Math.round((e.clientX - d.sx) / z), dy = Math.round((e.clientY - d.sy) / z);
    if (!dx && !dy) return;
    if (!d.pushed) { M.pushUndo(); d.pushed = true; }
    d.moved = true;
    if (d.mode === 'move') {
      if (U.num(d.w.x, 0) !== d.x0 + dx) d.w.x = d.x0 + dx;
      if (U.num(d.w.y, 0) !== d.y0 + dy) d.w.y = d.y0 + dy;
      if (d.w.x === 0 && d.w.align === undefined) delete d.w.x;
      if (d.w.y === 0 && d.w.align === undefined) delete d.w.y;
    } else {
      d.w.w = Math.max(4, d.w0 + dx); d.w.h = Math.max(4, d.h0 + dy);
    }
    M.st.dirty = true;
    if (!d.raf) d.raf = requestAnimationFrame(() => { d.raf = 0; this.render(); App.status(); });
  },
  onUp() {
    const d = this.drag; if (!d) return;
    this.drag = null;
    if (d.moved) App.refresh();
  },
};
