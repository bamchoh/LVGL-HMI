// Approximate rendering of a screen with HTML/CSS/SVG, selection, drag to move, drag to resize.
// The real look comes from the Preview button (hml_runtime renders the screen headless).
'use strict';

const Canvas = {
  root: null, wrap: null, drag: null,

  init() {
    this.root = document.getElementById('canvas');
    this.wrap = document.getElementById('canvas-wrap');
    this.root.addEventListener('mousedown', e => this.onDown(e));
    this.wrap.addEventListener('mousedown', e => {
      if (e.target !== this.wrap && e.target !== this.wrap.firstElementChild) return;
      App.select(null);
      if (e.button === 0) { this.drag = { mode: 'band', sx: e.clientX, sy: e.clientY }; e.preventDefault(); }
    });
    this.root.addEventListener('contextmenu', e => { const path = this.widgetAt(e.target); if (path) { e.preventDefault(); App.widgetMenu(e, path); } });
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
    // the screen's own style (background colour ...) on top of the theme; the element is
    // reused, so what a removed property had set must go first
    for (const k of ['borderWidth', 'borderColor', 'borderStyle', 'borderRadius', 'opacity', 'boxShadow', 'outline', 'textAlign', 'letterSpacing']) root.style[k] = '';
    root.dataset.path = '';
    if (scr.popup) {
      // a popup: its box over the dimmed screen, where the device shows it
      root.className = 'lv lv-screen lv-popup-back' + (scr.dim === false ? ' nodim' : '');
      root.style.padding = '0';
      const box = U.el('div', { class: 'lv lv-panel lv-popup-box' + (U.samePath(M.st.sel, []) ? ' hd-selected' : ''), dataset: { path: '' } });
      const placed = Object.assign({}, scr, { type: 'panel', w: scr.w !== undefined ? scr.w : '60%', h: scr.h !== undefined ? scr.h : '60%' });
      if (placed.align === undefined && placed.x === undefined && placed.y === undefined) placed.align = 'center';
      this.place(box, placed, null);
      box.style.background = th.card;
      if (scr.style && typeof scr.style === 'object') this.applyStyle(box, scr.style, th, {});
      root.append(box);
      const content = this.container(box, scr, th, 12);
      (scr.children || []).forEach((c, i) => content.append(this.build(c, [i], th, scr.flex ? scr : null)));
      return;
    }
    if (scr.style && typeof scr.style === 'object') this.applyStyle(root, scr.style, th, {});
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
    if (st.bg_image && M.st.dir) {
      // over the background colour, centred (or repeated), as the runtime draws it
      s.backgroundImage = 'url("' + U.fileUrl(U.joinPath(M.st.dir, st.bg_image)) + '")';
      s.backgroundRepeat = st.bg_image_tiled ? 'repeat' : 'no-repeat';
      s.backgroundPosition = st.bg_image_tiled ? '0 0' : 'center';
    }
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
      case 'recipe_table': return ['100%', 300];
      case 'trend': return ['100%', 300];
      case 'alarm_list': return ['100%', 240];
      case 'log_view': return ['100%', 300];
      case 'meter': return [200, 200];
      case 'datetime_input': return [null, null];
      case 'alarm_banner': return ['100%', 36];
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
    const rhs = x => (x && typeof x === 'object') ? this.tagInit(x.tag) : x;   // { tag } compares with that tag's init value
    if (c.eq !== undefined) r = String(v) === String(rhs(c.eq)) || num(v) === num(rhs(c.eq));
    else if (c.ne !== undefined) r = !(String(v) === String(rhs(c.ne)) || num(v) === num(rhs(c.ne)));
    else if (c.gt !== undefined) r = num(v) > num(rhs(c.gt));
    else if (c.lt !== undefined) r = num(v) < num(rhs(c.lt));
    else if (c.ge !== undefined) r = num(v) >= num(rhs(c.ge));
    else if (c.le !== undefined) r = num(v) <= num(rhs(c.le));
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
    const also = !U.samePath(M.st.sel, path) && (M.st.more || []).some(p => U.samePath(p, path)) && M.selection().some(p => U.samePath(p, path));
    if (U.samePath(M.st.sel, path)) el.classList.add('hd-selected');
    else if (also) el.classList.add('hd-selected', 'hd-also');
    if (w.hidden) el.classList.add('lv-hidden');
    // designer-only marks: a locked widget lets clicks through, a hidden one is not drawn (unless it is selected)
    if (M.flag(w, 'locked')) el.classList.add('hd-locked');
    if (M.flag(w, 'hidden') && !el.classList.contains('hd-selected')) el.classList.add('hd-ghost');
    if (w.disabled) el.classList.add('lv-disabled');
    if (!this.condVisible(w)) el.classList.add('lv-cond-hidden');
    this.place(el, w, parentFlex);
    const st = this.style(w);
    const fn = this['w_' + type] || this.w_panel;
    fn.call(this, el, w, st, th, path);
    this.applyStyle(el, st, th, {});
    if (U.samePath(M.st.sel, path) && !parentFlex && M.selection().length < 2) el.append(U.el('div', { class: 'hd-rs', title: 'drag to resize' }));
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
    if (w.text !== undefined && w.text !== null && !w.bind) return String(M.resolveText(w.text));
    const b = w.bind;
    if (typeof b === 'string') return '{' + b + '}';
    if (b && typeof b === 'object' && b.text !== undefined) {
      const t = b.text;
      if (typeof t === 'string') return '{' + t + '}';
      if (t && t.format) return t.format.replace(/%%/g, '\u0000').replace(/%[-+ 0#]*\d*(?:\.\d+)?(hh|h|ll|l|L|z)?[diouxX]/g, '0')
        .replace(/%[-+ 0#]*\d*(?:\.\d+)?(hh|h|ll|l|L|z)?[fFeEgG]/g, '0.0').replace(/%[-+ 0#]*\d*(?:\.\d+)?s/g, '{' + (t.tag || '') + '}').replace(/\u0000/g, '%');
      if (t && t.tag) return '{' + t.tag + '}';
    }
    return w.text !== undefined ? String(M.resolveText(w.text)) : 'Label';
  },
  w_label(el, w, st) {
    el.textContent = this.labelText(w);
    el.style.whiteSpace = (w.w !== undefined && w.long_mode !== 'scroll' && w.long_mode !== 'circular') ? 'pre-wrap' : 'pre';
    if (w.long_mode === 'dots' || w.long_mode === 'clip') { el.style.overflow = 'hidden'; el.style.textOverflow = w.long_mode === 'dots' ? 'ellipsis' : 'clip'; el.style.whiteSpace = 'nowrap'; }
  },
  w_button(el, w, st, th) {
    const lbl = U.el('span', { class: 'lv-btn-label' }, w.text !== undefined ? String(M.resolveText(w.text)) : (w.bind ? this.labelText(w) : ''));
    if (w.style_label) this.applyStyle(lbl, w.style_label, th, {});
    el.append(lbl);
    if (w.checked) el.classList.add('lv-checked');
  },
  w_switch(el, w) { el.append(U.el('div', { class: 'lv-knob' })); if (w.checked) el.classList.add('lv-checked'); },
  w_checkbox(el, w) {
    el.append(U.el('div', { class: 'lv-box' + (w.checked ? ' lv-checked' : '') }, w.checked ? '✓' : ''), U.el('span', null, w.text !== undefined ? String(M.resolveText(w.text)) : 'Check'));
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
    const opts = (Array.isArray(w.options) ? w.options : String(w.options || '').split('\n')).map(o => M.resolveText(o));
    el.append(U.el('span', { class: 'lv-grow' }, opts[U.num(w.selected, 0)] || ''), U.el('span', null, '▾'));
  },
  w_roller(el, w, st, th) {
    const opts = (Array.isArray(w.options) ? w.options : String(w.options || '').split('\n')).map(o => M.resolveText(o));
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
      // "fit": the picture fills the widget's own w / h
      if (w.fit === 'contain' || w.fit === 'stretch' || w.fit === 'center') {
        Object.assign(img.style, { width: '100%', height: '100%', objectFit: w.fit === 'contain' ? 'contain' : w.fit === 'stretch' ? 'fill' : 'none' });
      } else if (w.fit === 'tile') {
        Object.assign(el.style, { backgroundImage: 'url("' + img.src + '")', backgroundRepeat: 'repeat' });
        return;
      }
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
    el.append(U.el('div', { class: 'lv-kbrow' }, U.el('div', { class: 'lv-key' }, '×'), U.el('div', { class: 'lv-key lv-key-wide' }, 'space'), U.el('div', { class: 'lv-key' }, '✓')));
  },
  // a trend: sample lines in the colours the device uses, the window and the legend
  TREND_COLORS: ['#2196f3', '#ff9800', '#4caf50', '#e91e63', '#9c27b0', '#009688', '#ffc107', '#f44336'],
  w_trend(el, w) {
    const p = M.st.project, g = ((p && p.data_log && p.data_log.groups) || []).find(x => x.name === w.group);
    const list = (Array.isArray(w.tags) && w.tags.length ? w.tags : (g && g.tags) || []).slice(0, 8)
      .map((t, i) => typeof t === 'string' ? { tag: t, color: this.TREND_COLORS[i % 8] } : { tag: t.tag, color: t.color || this.TREND_COLORS[i % 8] });
    const range = U.num(w.range_s, 3600), span = range >= 86400 ? (range / 86400) + ' d' : range >= 3600 ? (range / 3600) + ' h' : (range / 60) + ' min';
    el.append(U.el('div', null, g ? (w.group + '  ·  ' + span + '  (live)') : "no data log group '" + (w.group || '') + "'"));
    const W = 300, H = 100, svg = U.svg('svg', { class: 'lv-tr-svg', viewBox: '0 0 ' + W + ' ' + H, preserveAspectRatio: 'none' });
    for (let i = 1; i < 5; i++) svg.append(U.svg('line', { x1: 0, x2: W, y1: i * H / 5, y2: i * H / 5, stroke: 'rgba(158,158,158,.25)', 'stroke-width': 0.5 }));
    list.forEach((t, k) => {
      let d = '';
      for (let x = 0; x <= W; x += 5) d += (x ? 'L' : 'M') + x + ',' + (H / 2 + Math.sin(x / 25 + k * 1.7) * (H / 3 - k * 6)).toFixed(1);
      svg.append(U.svg('path', { d, fill: 'none', stroke: t.color, 'stroke-width': 1.5, 'vector-effect': 'non-scaling-stroke' }));
    });
    el.append(svg);
    el.append(U.el('div', { class: 'lv-rt-foot' }, ...['◀', '▶', '−', '+'].map(s => U.el('span', { class: 'lv-al-btn' }, s)),
      U.el('span', { class: 'lv-al-btn', style: { background: '#ff9800', width: 'auto', padding: '0 14px' } }, (w.labels && w.labels.now) || 'Now'),
      ...list.map(t => U.el('span', { style: { color: t.color, alignSelf: 'center' } }, t.tag))));
  },

  // the book's own recipes as the device will show them (current values: from the tags' init)
  w_recipe_table(el, w) {
    const b = M.recipeBooks().find(x => x.name === w.book);
    const list = U.el('div', { class: 'lv-rt-list' }), right = U.el('div', { class: 'lv-rt-right' });
    el.append(list, right);
    if (!b) { right.append(U.el('div', { class: 'lv-al-empty' }, "no recipe book '" + (w.book || '') + "'")); return; }
    const recs = Array.isArray(b.recipes) ? b.recipes : [], sel = recs[0];
    recs.slice(0, 8).forEach((r, i) => list.append(U.el('div', { class: 'lv-rt-item' + (i === 0 ? ' sel' : '') }, r.name)));
    right.append(U.el('div', null, sel ? sel.name : '-'));
    const h = Math.max(24, U.num(w.row_height, 40));
    const table = U.el('div', { class: 'lv-rt-rows' }, U.el('div', { class: 'lv-rt-row head' }, U.el('span', null, 'Item'), U.el('span', null, 'Recipe'), U.el('span', null, 'Current')));
    for (const item of (b.items || []).slice(0, 10)) {
      const t = ((M.st.project && M.st.project.tags) || []).find(x => x.name === item) || {};
      const cur = t.init === undefined ? (t.type === 'string' ? '' : t.type === 'bool' ? 'false' : '0') : String(t.init);
      const val = sel && sel.values && sel.values[item] !== undefined ? String(sel.values[item]) : '-';
      const differs = val !== '-' && val !== cur;
      table.append(U.el('div', { class: 'lv-rt-row', style: { height: h + 'px' } }, U.el('span', null, item),
        U.el('span', { class: differs ? 'diff' : '' }, (differs ? '* ' : '') + val), U.el('span', null, cur)));
    }
    right.append(table, U.el('div', { class: 'lv-rt-foot' }, U.el('span', { class: 'lv-al-btn lv-rt-write' }, 'Write'), U.el('span', { class: 'lv-al-btn' }, 'Read current'),
      U.el('span', { class: 'lv-al-btn' }, 'Save as'), U.el('span', { class: 'lv-al-btn lv-rt-del' }, 'Delete')));
  },

  // ---- alarms: drawn with the project's own alarm definitions as samples
  ALARM_COLORS: { critical: '#f44336', error: '#f44336', alarm: '#f44336', warning: '#ffa000', info: '#2196f3' },
  alarmColor(sev) { return this.ALARM_COLORS[String(sev || '').toLowerCase()] || '#9e9e9e'; },
  alarmSamples() {
    const items = ((M.st.project && M.st.project.alarms && M.st.project.alarms.items) || []).filter(a => a && a.name);
    return items.length ? items : [{ name: 'sample', message: 'Sample alarm', severity: 'warning' }];
  },
  // the rollers the mode has, three rows each, a sample date in the middle row
  w_datetime_input(el, w) {
    const mode = ['date', 'time'].includes(w.mode) ? w.mode : 'datetime', sec = !!w.seconds && mode !== 'date';
    const roll = (width, vals) => U.el('div', { class: 'lv-dt-roll', style: { width: width + 'px', border: '1px solid rgba(128,128,128,.5)', borderRadius: '6px', textAlign: 'center',
      lineHeight: '1.9', overflow: 'hidden' } }, ...vals.map((v, i) => U.el('div', { style: i === 1 ? { background: 'rgba(33,150,243,.45)' } : { opacity: '.55' } }, document.createTextNode(v))));
    const sep = t => U.el('span', null, document.createTextNode(t));
    const parts = [];
    if (mode !== 'time') parts.push(roll(84, ['2025', '2026', '2027']), sep('-'), roll(60, ['10', '11', '12']), sep('-'), roll(60, ['20', '21', '22']));
    if (mode === 'datetime') parts.push(sep('\u00a0'));
    if (mode !== 'date') { parts.push(roll(60, ['12', '13', '14']), sep(':'), roll(60, ['04', '05', '06'])); if (sec) parts.push(sep(':'), roll(60, ['08', '09', '10'])); }
    el.style.display = 'flex'; el.style.alignItems = 'center'; el.style.gap = '4px';
    el.append(...parts);
  },
  // the scale, its numbers and zones as the device draws them; the needle at "value" (or a third of the way)
  w_meter(el, w) {
    let min = U.num(w.min, 0), max = U.num(w.max, 100);
    if (!(max > min)) { min = 0; max = 100; }
    const angle = Math.min(360, Math.max(90, U.num(w.angle, 270))), ticks = Math.min(101, Math.max(2, Math.round(U.num(w.ticks, 21))));
    const major = Math.min(100, Math.max(1, Math.round(U.num(w.major_every, 5)))), dec = Math.min(3, Math.max(0, Math.round(U.num(w.decimals, 0))));
    const value = w.value === undefined ? min + (max - min) / 3 : Math.min(max, Math.max(min, U.num(w.value, min)));
    const NS = 'http://www.w3.org/2000/svg', R = 100, start = 90 + (360 - angle) / 2;
    const mk = (tag, attrs) => { const e = document.createElementNS(NS, tag); for (const k in attrs) e.setAttribute(k, attrs[k]); return e; };
    const at = (v, r) => { const a = (start + angle * (v - min) / (max - min)) * Math.PI / 180; return [R + r * Math.cos(a), R + r * Math.sin(a)]; };
    const arc = (from, to, r) => { const [x0, y0] = at(from, r), [x1, y1] = at(to, r); return 'M' + x0 + ' ' + y0 + ' A' + r + ' ' + r + ' 0 ' + (angle * (to - from) / (max - min) > 180 ? 1 : 0) + ' 1 ' + x1 + ' ' + y1; };
    const svg = mk('svg', { viewBox: '0 0 200 200', width: '100%', height: '100%', preserveAspectRatio: 'xMidYMid meet' });
    svg.style.color = 'inherit';
    svg.append(mk('path', { d: arc(min, max, 96), fill: 'none', stroke: 'currentColor', 'stroke-width': 3 }));
    const zones = Array.isArray(w.zones) ? w.zones : [];
    const zoneOf = v => zones.find(z => z && v >= U.num(z.from, min) && v <= U.num(z.to, max));
    for (const z of zones) {
      if (!z) continue;
      const f = Math.max(min, U.num(z.from, min)), t = Math.min(max, U.num(z.to, max));
      if (t > f) svg.append(mk('path', { d: arc(f, t, 95), fill: 'none', stroke: z.color || '#e53935', 'stroke-width': 6 }));
    }
    for (let i = 0; i < ticks; i++) {
      const v = min + (max - min) * i / (ticks - 1), big = i % major === 0, z = zoneOf(v), col = z ? (z.color || '#e53935') : 'currentColor';
      const [x0, y0] = at(v, 93), [x1, y1] = at(v, big ? 81 : 87);
      svg.append(mk('line', { x1: x0, y1: y0, x2: x1, y2: y1, stroke: col, 'stroke-width': big ? 2 : 1 }));
      if (big) { const [tx, ty] = at(v, 68); const t = mk('text', { x: tx, y: ty, fill: col, 'font-size': 13, 'text-anchor': 'middle', 'dominant-baseline': 'middle' }); t.textContent = v.toFixed(dec); svg.append(t); }
    }
    const nc = w.needle_color || '#f44336', [nx, ny] = at(value, 78);
    svg.append(mk('line', { x1: R, y1: R, x2: nx, y2: ny, stroke: nc, 'stroke-width': 4, 'stroke-linecap': 'round' }), mk('circle', { cx: R, cy: R, r: 7, fill: nc }));
    if (w.show_value !== false) {
      const t = mk('text', { x: R, y: R + (angle > 300 ? 40 : angle <= 200 ? 50 : 80), fill: 'currentColor', 'font-size': 15, 'text-anchor': 'middle', 'dominant-baseline': 'middle' });
      t.textContent = value.toFixed(dec) + (w.unit ? ' ' + w.unit : '');
      svg.append(t);
    }
    el.style.overflow = 'hidden';
    el.append(svg);
  },
  // a few sample rows: what the columns will look like (the real rows come from the log on the device)
  LOG_COLUMNS: { system: ['time', 'level', 'source', 'message'], operation: ['time', 'user', 'kind', 'target', 'old', 'new'] },
  LOG_SAMPLES: {
    system: [['INFO', 'system', 'project loaded'], ['WARN', 'driver', 'modbus: communication error, reconnecting'], ['ERROR', 'script', "script 'calc': attempt to index a nil value"],
             ['INFO', 'driver', 'modbus 192.168.0.10:502 connected']],
    operation: [['op', 'write', 'setpoint', '40', '45'], ['op', 'screen', 'trend', '', ''], ['admin', 'system', 'backup', '', ''], ['op', 'write', 'run', 'false', 'true']],
  },
  w_log_view(el, w) {
    const src = ['system', 'operation', 'data'].includes(w.source) ? w.source : 'system', lab = w.labels || {};
    const p = M.st.project, grp = ((p && p.data_log && p.data_log.groups) || []).find(g => g.name === w.group);
    const all = src === 'data' ? ['time'].concat(((grp && grp.tags) || []).map(t => typeof t === 'string' ? t : (t && t.tag)).filter(Boolean)) : this.LOG_COLUMNS[src];
    const asked = Array.isArray(w.columns) ? w.columns : typeof w.columns === 'string' ? w.columns.split(',').map(x => x.trim()).filter(Boolean) : [];
    const cols = asked.length ? asked : all;
    const title = c => lab[c] || (src === 'data' && c !== 'time' ? c : c.charAt(0).toUpperCase() + c.slice(1));
    const bar = U.el('div', { style: { display: 'flex', alignItems: 'center', gap: '8px', padding: '0 0 6px 0' } });
    if (src === 'system' && w.filter !== false) bar.append(U.el('span', { class: 'lv-al-dd' }, String(w.min_level || 'info').toUpperCase() + ' ▾'));
    const n = Math.max(1, Math.min(U.num(w.rows, 10), 6));
    bar.append(U.el('span', { style: { flex: '1', textAlign: 'right' } }, '1 - ' + n + ' / 128'), U.el('span', { class: 'lv-al-btn' }, '◀'), U.el('span', { class: 'lv-al-btn' }, '▶'));
    const table = U.el('table', { style: { width: '100%', borderCollapse: 'collapse', fontSize: 'inherit' } });
    const cell = (tag, text, color) => U.el(tag, { style: { textAlign: 'left', padding: '5px 6px', border: '1px solid rgba(128,128,128,.35)', whiteSpace: 'nowrap', overflow: 'hidden',
      textOverflow: 'ellipsis', maxWidth: '0', fontWeight: 'normal', color: color || 'inherit', background: tag === 'th' ? 'rgba(33,150,243,.3)' : 'transparent' } }, document.createTextNode(String(text)));   // (as the device shows it: never translated)
    table.append(U.el('tr', null, ...cols.map(c => cell('th', title(c)))));
    for (let i = 0; i < n; i++) {
      const time = '11-21 10:' + String(59 - i * 3).padStart(2, '0') + ':07';
      let row, color = null;
      if (src === 'data') row = cols.map((c, k) => c === 'time' ? time : (20 + ((i * 7 + k * 3) % 50) / 10).toFixed(1));
      else {
        const s = this.LOG_SAMPLES[src][i % 4];
        const byName = src === 'system' ? { time, level: s[0], source: s[1], message: s[2], count: i % 4 === 1 ? 'x3' : '' }
                                        : { time, user: s[0], screen: 'main', kind: s[1], target: s[2], old: s[3], new: s[4] };
        if (src === 'system') color = s[0] === 'ERROR' ? '#ef9a9a' : s[0] === 'WARN' ? '#ffd54f' : null;
        row = cols.map(c => byName[c] === undefined ? '' : byName[c]);
      }
      table.append(U.el('tr', null, ...row.map(t => cell('td', t, color))));
    }
    el.style.display = 'flex'; el.style.flexDirection = 'column'; el.style.padding = '6px'; el.style.overflow = 'hidden';
    el.append(bar, table);
  },
  w_alarm_list(el, w) {
    const history = w.mode === 'history', lab = w.labels || {};
    const cols = Array.isArray(w.columns) && w.columns.length ? w.columns : ['time', 'message', 'state'];
    const h = Math.max(16, U.num(w.row_height, 40));
    const body = U.el('div', { class: 'lv-al-body' });
    const items = this.alarmSamples().filter(a => !w.severity || String(a.severity || 'warning').toLowerCase() === String(w.severity).toLowerCase());
    // (at least three rows, so that the three states - not acknowledged, acknowledged, cleared - are all shown)
    const rows = items.length ? Math.min(Math.max(items.length * (history ? 2 : 1), 3), Math.max(1, U.num(w.rows, history ? 20 : 50)), 6) : 0;
    if (!items.length) body.append(U.el('div', { class: 'lv-al-empty' }, lab.empty || 'No alarms'));
    for (let i = 0; i < rows; i++) {
      const a = items[i % items.length], sev = a.severity || 'warning', c = this.alarmColor(sev);
      // current: the first is not acknowledged, the second acknowledged, the third cleared
      const kind = history ? 'hist' : ['unacked', 'active', 'cleared'][i % 3];
      const row = U.el('div', { class: 'lv-al-row lv-al-' + kind, style: { height: h + 'px', borderLeftColor: c } });
      if (kind === 'unacked') row.style.background = c;
      else if (kind === 'active') row.style.background = c + '66';
      const hh = String(10 + (i % 10)).padStart(2, '0');
      const time = history ? '11-19 ' + hh + ':0' + (i % 6) + ':12' : hh + ':0' + (i % 6) + ':12';
      for (const col of cols) {
        if (col === 'time') row.append(U.el('span', null, time));
        else if (col === 'severity') row.append(U.el('span', null, '⚠ ' + sev));
        else if (col === 'message') row.append(U.el('span', { class: 'lv-grow' }, a.message || a.name));
        else if (col === 'state') row.append(U.el('span', null, history ? (i % 2 ? (lab.cleared_event || 'cleared') : (lab.raised || 'raised'))
          : kind === 'unacked' ? '🔔 ' + (lab.unacked || 'UNACK') : kind === 'cleared' ? '🔔 ' + (lab.cleared || 'CLEARED') : '✓ ' + (lab.active || 'ACTIVE')));
      }
      body.append(row);
    }
    el.append(body);
    if (history) el.append(U.el('div', { class: 'lv-al-foot' }, U.el('span', { class: 'lv-al-btn' }, '◀'), U.el('span', null, '1 / 3'),
      U.el('span', { class: 'lv-al-btn' }, '▶'), w.severity ? null : U.el('span', { class: 'lv-al-dd' }, (lab.all || 'All') + ' ▾')));
  },
  w_alarm_banner(el, w) {
    const a = this.alarmSamples()[0], c = this.alarmColor(a.severity || 'warning');
    el.style.background = c;
    el.append(U.el('span', { class: 'lv-grow' }, '⚠ 10:00:12  ' + (a.message || a.name) + (this.alarmSamples().length > 1 ? '  (+' + (this.alarmSamples().length - 1) + ')' : '')));
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
    // Ctrl / Shift + click: several widgets (of one parent) at once
    if ((e.ctrlKey || e.shiftKey || e.metaKey) && path.length) { M.toggleSelect(path); App.selectionChanged(); e.preventDefault(); return; }
    // a click on one of several selected widgets keeps them selected, so that they can be dragged together
    const sels = M.selection(), together = sels.length > 1 && sels.some(p => U.samePath(p, path));
    if (!together) App.select(path);
    // on the screen's background: drag a rectangle around the widgets to select
    if (!path.length) { this.drag = { mode: 'band', sx: e.clientX, sy: e.clientY }; e.preventDefault(); return; }
    const parent = M.parent(path), w = M.get(path);
    if (!w || (parent && parent.flex)) return;
    if ((together ? sels : [path]).some(p => M.flag(M.get(p), 'locked'))) return;      // locked: stays where it is
    const items = (together ? sels : [path]).map(p => { const it = M.get(p); return { w: it, x0: U.num(it.x, 0), y0: U.num(it.y, 0) }; });
    this.drag = { mode: 'move', path, w, items, together, sx: e.clientX, sy: e.clientY, pushed: false, moved: false, snap: this.snapInfo(together ? sels : [path]) };
    e.preventDefault();
  },
  // What a drag can snap to: the edges and centres of the other widgets in the same parent, and
  // of the parent itself. Measured once, when the drag starts.
  snapInfo(paths) {
    const rects = paths.map(p => this.rect(p)).filter(Boolean);
    const el = this.root.querySelector('[data-path="' + U.pathStr(paths[0]) + '"]'), host = el && el.offsetParent;
    if (!rects.length || !host) return null;
    const x = Math.min(...rects.map(r => r.x)), y = Math.min(...rects.map(r => r.y));
    const box = { x, y, w: Math.max(...rects.map(r => r.x + r.w)) - x, h: Math.max(...rects.map(r => r.y + r.h)) - y };
    const cp = paths[0].slice(0, -1), parent = M.get(cp), others = [{ x: 0, y: 0, w: host.clientWidth, h: host.clientHeight }];
    (parent.children || []).forEach((c, i) => {
      if (paths.some(p => p[p.length - 1] === i)) return;
      const r = this.rect(cp.concat(i)); if (r && r.w && r.h) others.push(r);
    });
    return { box, others };
  },
  guides(d) {
    const el = this.root.querySelector('[data-path="' + U.pathStr(d.path) + '"]'), host = el && el.offsetParent;
    if (!host || !d.guide) return;
    if (d.guide.gx !== null) host.append(U.el('div', { class: 'hd-guide v', style: { left: d.guide.gx + 'px' } }));
    if (d.guide.gy !== null) host.append(U.el('div', { class: 'hd-guide h', style: { top: d.guide.gy + 'px' } }));
  },
  // a widget dragged from the palette is dropped where the mouse is
  dragType: null,
  onDragOver(e) { if (this.dragType && this.widgetAt(e.target)) { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; } },
  onDrop(e) {
    const type = this.dragType; this.dragType = null;
    const at = this.widgetAt(e.target);
    if (!type || !at) return;
    e.preventDefault();
    const cp = M.containerPathFor(at.length ? at : []), w0 = M.get(at);
    const into = at.length && !M.isContainer(w0) ? cp : at;                       // dropped on a widget: next to it
    const host = this.root.querySelector('[data-path="' + U.pathStr(into) + '"]') || this.root;
    const r = host.getBoundingClientRect(), z = M.st.zoom, cs = getComputedStyle(host);
    const def = (SCHEMA.types[type] || {}).def || {};
    const hw = typeof def.w === 'number' ? def.w / 2 : 40, hh = typeof def.h === 'number' ? def.h / 2 : 15;   // the mouse is at its middle
    M.st.sel = into;
    M.addWidget(type, into, { x: (e.clientX - r.left) / z - parseFloat(cs.paddingLeft || 0) - parseFloat(cs.borderLeftWidth || 0) - hw,
      y: (e.clientY - r.top) / z - parseFloat(cs.paddingTop || 0) - parseFloat(cs.borderTopWidth || 0) - hh });
  },
  // where a widget is drawn, in screen pixels relative to its parent's content (for lining up)
  rect(path) {
    const el = this.root.querySelector('[data-path="' + U.pathStr(path) + '"]');
    return el ? { x: el.offsetLeft, y: el.offsetTop, w: el.offsetWidth, h: el.offsetHeight } : null;
  },
  onMove(e) {
    const d = this.drag; if (!d) return;
    if (d.mode === 'band') {
      if (!d.box && Math.abs(e.clientX - d.sx) + Math.abs(e.clientY - d.sy) < 4) return;
      if (!d.box) { d.box = U.el('div', { class: 'hd-band' }); document.body.append(d.box); }
      d.x = Math.min(d.sx, e.clientX); d.y = Math.min(d.sy, e.clientY); d.w = Math.abs(e.clientX - d.sx); d.h = Math.abs(e.clientY - d.sy);
      Object.assign(d.box.style, { left: d.x + 'px', top: d.y + 'px', width: d.w + 'px', height: d.h + 'px' });
      return;
    }
    const z = M.st.zoom;
    let dx = Math.round((e.clientX - d.sx) / z), dy = Math.round((e.clientY - d.sy) / z);
    if (!dx && !dy) return;
    d.guide = null;
    if (d.mode === 'move' && d.snap && !e.altKey) {            // Alt: move freely
      const s = M.snap({ x: d.snap.box.x + dx, y: d.snap.box.y + dy, w: d.snap.box.w, h: d.snap.box.h }, d.snap.others, 5 / z);
      dx += s.dx; dy += s.dy; d.guide = s;
    }
    if (!d.pushed) { M.pushUndo(); d.pushed = true; }
    d.moved = true;
    if (d.mode === 'move') {
      for (const it of d.items) {
        if (U.num(it.w.x, 0) !== it.x0 + dx) it.w.x = it.x0 + dx;
        if (U.num(it.w.y, 0) !== it.y0 + dy) it.w.y = it.y0 + dy;
        if (it.w.x === 0 && it.w.align === undefined) delete it.w.x;
        if (it.w.y === 0 && it.w.align === undefined) delete it.w.y;
      }
    } else {
      d.w.w = Math.max(4, d.w0 + dx); d.w.h = Math.max(4, d.h0 + dy);
    }
    M.st.dirty = true;
    if (!d.raf) d.raf = requestAnimationFrame(() => { d.raf = 0; this.render(); if (this.drag === d) this.guides(d); App.status(); });
  },
  onUp() {
    const d = this.drag; if (!d) return;
    this.drag = null;
    if (d.mode === 'band') {
      if (!d.box) return;
      d.box.remove();
      // the widgets of the screen that the rectangle touches
      const hit = [];
      ((M.screen() && M.screen().children) || []).forEach((c, i) => {
        const el = this.root.querySelector('[data-path="' + i + '"]'); if (!el) return;
        const r = el.getBoundingClientRect();
        if (M.flag(c, 'locked')) return;
        if (r.width && r.height && r.left < d.x + d.w && r.right > d.x && r.top < d.y + d.h && r.bottom > d.y) hit.push([i]);
      });
      if (hit.length) { M.st.sel = hit[0]; M.st.more = hit.slice(1); App.selectionChanged(); }
      return;
    }
    if (d.moved) App.refresh();
    else if (d.together) App.select(d.path);          // a plain click on one of several: just that one
  },
};
