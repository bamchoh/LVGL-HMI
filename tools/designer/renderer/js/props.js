// Property panel for the selected widget / screen. Global `Props`.
'use strict';

const Props = {
  el: null, styleSel: 'style', TRUE_DEFAULT: ['adjustable', 'clickable', 'scrollable', 'enabled', 'ack', 'blink', 'dim', 'wake_on_alarm', 'tls', 'verify'],

  init() { this.el = document.getElementById('props'); },

  // ---- generic editors ---------------------------------------------------
  // fk = focus key, used by App.refresh() to restore the focus after a rebuild
  setVal(obj, k, v) { M.mutate(() => { if (v === undefined || v === '' || v === null) delete obj[k]; else obj[k] = v; }); },

  // Editing shows on the canvas while typing or dragging in a colour picker, not only when
  // the field is left. The property panel itself is not rebuilt meanwhile (the field would
  // lose the cursor); one undo step covers the whole edit.
  //   live(...)    while the value changes (v undefined = not a usable value yet: ignored)
  //   commit(...)  when the edit is finished
  live(state, obj, k, v) {
    if (v === undefined) return;
    if (!state.on) { M.pushUndo(); state.on = true; M.st.dirty = true; App.toolbar(); }
    if (v === '' || v === null) delete obj[k]; else obj[k] = v;
    Canvas.render();
  },
  commit(state, obj, k, v) {
    if (!state.on) { this.setVal(obj, k, v); return; }
    state.on = false;
    if (v === undefined || v === '' || v === null) delete obj[k]; else obj[k] = v;
    M.st.dirty = true;
    App.refresh();
  },

  editor(obj, k, f, fk) {
    const t = f.t || 'text', v = obj[k];
    const attrs = { dataset: { fk: fk + '.' + k } };
    const st = { on: false };                       // the state of a live edit of this field
    switch (t) {
      case 'number': {
        const num = s => (s === '' ? '' : isNaN(Number(s)) ? undefined : Number(s));
        return U.el('input', Object.assign(attrs, { type: 'number', step: 'any', value: v === undefined ? '' : v,
          oninput: e => this.live(st, obj, k, num(e.target.value)),
          onchange: e => this.commit(st, obj, k, e.target.value === '' ? undefined : Number(e.target.value)) }));
      }
      case 'bool': {
        const dflt = this.TRUE_DEFAULT.includes(k);   // undefined means true for these keys
        return U.el('input', Object.assign(attrs, { type: 'checkbox', checked: v === undefined ? dflt : !!v,
          onchange: e => this.setVal(obj, k, e.target.checked ? (dflt ? undefined : true) : (dflt ? false : undefined)) }));
      }
      case 'select': {
        const o = f.o || [];
        const s = U.el('select', Object.assign(attrs, { onchange: e => this.setVal(obj, k, e.target.value) }));
        for (const opt of o) s.append(U.el('option', { value: opt, selected: String(v === undefined ? '' : v) === String(opt) }, opt === '' ? I18N.t(f.empty || '(default)') : I18N.value(opt)));
        if (v !== undefined && !o.map(String).includes(String(v))) s.append(U.el('option', { value: v, selected: true }, v));
        return s;
      }
      case 'font': return this.editor(obj, k, { t: 'select', o: [''].concat(SCHEMA.fonts) }, fk);
      case 'anim': return this.editor(obj, k, { t: 'select', o: [''].concat(SCHEMA.anims) }, fk);
      case 'screen': return this.editor(obj, k, { t: 'select', o: [''].concat(M.screenNames()) }, fk);
      case 'script': return this.editor(obj, k, { t: 'select', o: [''].concat(M.scriptNames()), empty: '(inline code)' }, fk);
      case 'code': return U.el('textarea', Object.assign(attrs, { rows: 3, class: 'mono code', spellcheck: false, placeholder: 'Lua, e.g.  tag.count = tag.count + 1',
        value: Array.isArray(v) ? v.join('\n') : (v || ''), onkeydown: U.tabKey, onchange: e => this.setVal(obj, k, e.target.value) }));
      case 'loggroup': {
        const p = M.st.project, names = ((p && p.data_log && p.data_log.groups) || []).map(g => g.name).filter(Boolean);
        const s = U.el('select', Object.assign(attrs, { onchange: e => this.setVal(obj, k, e.target.value || undefined) }));
        s.append(U.el('option', { value: '' }, '-'));
        for (const n of names) s.append(U.el('option', { value: n, selected: v === n }, n));
        if (v && !names.includes(v)) s.append(U.el('option', { value: v, selected: true }, v + ' (?)'));
        return s;
      }
      case 'recipebook': {
        const names = M.recipeBooks().map(b => b.name);
        const s = U.el('select', Object.assign(attrs, { onchange: e => this.setVal(obj, k, e.target.value || undefined) }));
        s.append(U.el('option', { value: '' }, '-'));
        for (const n of names) s.append(U.el('option', { value: n, selected: v === n }, n));
        if (v && !names.includes(v)) s.append(U.el('option', { value: v, selected: true }, v + ' (?)'));
        return s;
      }
      case 'tag': return U.el('input', Object.assign(attrs, { type: 'text', list: 'dl-tags', value: v === undefined ? '' : v, placeholder: 'tag',
        onchange: e => this.setVal(obj, k, e.target.value.trim()) }));
      case 'size': {
        const size = s => (s === '' ? '' : /^-?\d+(\.\d+)?$/.test(s) ? Number(s) : /^(\d+(\.\d+)?%|content|full)$/.test(s) ? s : undefined);
        return U.el('input', Object.assign(attrs, { type: 'text', value: v === undefined ? '' : v, placeholder: 'px / 50% / content',
          oninput: e => this.live(st, obj, k, size(e.target.value.trim())),
          onchange: e => { const s = e.target.value.trim(); this.commit(st, obj, k, s === '' ? undefined : /^-?\d+(\.\d+)?$/.test(s) ? Number(s) : s); } }));
      }
      case 'color': {
        const usable = s => (s === '' ? '' : /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.test(s) ? s : undefined);   // names are applied when the field is left
        const txt = U.el('input', Object.assign(attrs, { type: 'text', class: 'color-text', value: v === undefined ? '' : v, placeholder: '#rrggbb / red',
          oninput: e => { const c = usable(e.target.value.trim()); this.live(st, obj, k, c); if (c) sw.value = U.hex6(c, '#808080'); },
          onchange: e => this.commit(st, obj, k, e.target.value.trim()) }));
        const sw = U.el('input', { type: 'color', value: U.hex6(v, '#808080'),
          oninput: e => { txt.value = e.target.value; this.live(st, obj, k, e.target.value); },
          onchange: e => this.commit(st, obj, k, e.target.value) });
        return U.el('span', { class: 'color-ed' }, sw, txt);
      }
      case 'lines': return U.el('textarea', Object.assign(attrs, { rows: 3, value: Array.isArray(v) ? v.join('\n') : (v || ''),
        onchange: e => { const a = e.target.value.split('\n').filter(s => s !== ''); this.setVal(obj, k, a.length ? a : undefined); } }));
      case 'csv': return U.el('input', Object.assign(attrs, { type: 'text', value: Array.isArray(v) ? v.join(', ') : (v || ''), placeholder: 'a, b, c',
        onchange: e => { const a = e.target.value.split(',').map(s => s.trim()).filter(Boolean); this.setVal(obj, k, a.length ? a : undefined); } }));
      case 'points': return U.el('textarea', Object.assign(attrs, { rows: 3, value: Array.isArray(v) ? v.map(p => p.join(',')).join('\n') : '', placeholder: 'x,y per line',
        onchange: e => { const a = e.target.value.split('\n').map(l => l.split(',').map(Number)).filter(p => p.length >= 2 && !p.some(isNaN)); this.setVal(obj, k, a.length ? a : undefined); } }));
      case 'image': {
        const txt = U.el('input', Object.assign(attrs, { type: 'text', value: v === undefined ? '' : v, placeholder: 'assets/img.png', onchange: e => this.setVal(obj, k, e.target.value.trim()) }));
        const btn = U.el('button', { class: 'mini', title: 'choose a PNG (copied into the project)', onclick: async () => { const rel = await App.pickImage(); if (rel) this.setVal(obj, k, rel); } }, '…');
        return U.el('span', { class: 'with-btn' }, txt, btn);
      }
      case 'json': return U.el('textarea', Object.assign(attrs, { rows: 4, value: v === undefined ? '' : JSON.stringify(v),
        onchange: e => { try { this.setVal(obj, k, e.target.value.trim() === '' ? undefined : JSON.parse(e.target.value)); } catch (err) { App.toast('JSON: ' + err.message); } } }));
      default: return U.el('input', Object.assign(attrs, { type: 'text', value: v === undefined ? '' : v, placeholder: f.placeholder ? I18N.t(f.placeholder) : undefined,
        oninput: e => this.live(st, obj, k, e.target.value), onchange: e => this.commit(st, obj, k, e.target.value) }));
    }
  },

  // label: a property key or a fixed text. It is shown by its display name in the chosen
  // language; the tooltip then names the key (what is written in the project files).
  row(label, input, hint) {
    const tip = I18N.named(label) ? label + (hint ? '\n' + I18N.t(hint) : '') : I18N.t(hint || '');
    return U.el('div', { class: 'row', title: tip }, U.el('label', null, I18N.label(label)), input);
  },
  fields(obj, defs, fk) { return defs.map(f => this.row(f.label || f.k, this.editor(obj, f.k, f, fk), f.hint)); },
  section(title, open, ...body) { return U.el('details', { open: open !== false, class: 'sec' }, U.el('summary', null, I18N.t(title)), U.el('div', { class: 'sec-body' }, ...body)); },

  // ---- panel -------------------------------------------------------------
  render() {
    const el = this.el; el.innerHTML = '';
    const path = M.st.sel, w = path ? M.get(path) : null;
    if (!w) { el.append(U.el('div', { class: 'hint' }, I18N.t('Select a widget on the canvas or in the tree.'))); return; }
    const sels = M.selection();
    if (sels.length > 1) { this.renderSeveral(el, sels); return; }
    const isRoot = path.length === 0, fk = 'w:' + U.pathStr(path);
    const common = SCHEMA.common.map(f => f.k === 'align' ? Object.assign({}, f, { o: SCHEMA.aligns }) : f);

    if (isRoot) {
      const name = U.el('input', { type: 'text', value: w.name || '', dataset: { fk: fk + '.name' }, onchange: e => M.renameScreen(M.st.cur, e.target.value) });
      el.append(U.el('div', { class: 'props-head' }, 'Screen'));
      el.append(this.section('Screen', true, this.row('name', name), ...this.fields(w, [{ k: 'scrollable', t: 'bool' },
        { k: 'level', t: 'number', hint: 'minimum user level to open this screen (0 / empty = everybody); the login screen is shown first. Needs users (Users tab)' },
        { k: 'popup', t: 'bool', hint: 'shown over the current screen by the action "popup" (closed by "close", "back" or a screen change), never as a screen of its own' }], fk)));
      if (w.popup) el.append(this.section('Popup', true, ...this.fields(w, [{ k: 'w', t: 'size' }, { k: 'h', t: 'size' }, { k: 'align', t: 'select', o: SCHEMA.aligns },
        { k: 'x', t: 'number' }, { k: 'y', t: 'number' }, { k: 'dim', t: 'bool', hint: 'darken the screen below (it cannot be touched either way)' }], fk)));
      el.append(this.section('Layout (flex)', false, ...this.fields(w, SCHEMA.flex.filter(f => f.k !== 'flex_grow'), fk)));
      el.append(this.styleSection(w, fk));
      return;
    }

    const type = w.type || 'panel', sch = SCHEMA.types[type] || { props: [] };
    el.append(U.el('div', { class: 'props-head' }, U.el('b', null, type), ' ', U.el('span', { class: 'dim' }, '#' + U.pathStr(path))));
    const specific = this.fields(w, sch.props, fk);
    if (sch.series) specific.push(this.seriesEditor(w, fk));
    if (specific.length) el.append(this.section('Widget', true, ...specific));
    el.append(this.section('Position & size', true, ...this.fields(w, common, fk)));
    const layout = this.fields(w, M.isContainer(w) ? SCHEMA.flex : SCHEMA.flex.filter(f => f.k === 'flex_grow'), fk);
    el.append(this.section('Layout', !!w.flex, ...layout));
    el.append(this.section('Flags', false, ...this.fields(w, SCHEMA.flags, fk)));
    el.append(this.section('Access (users)', w.level !== undefined, ...this.fields(w, SCHEMA.access, fk)));
    el.append(this.styleSection(w, fk));
    el.append(this.bindSection(w, fk));
    el.append(this.eventSection(w, fk));
  },

  focusFirst() { const i = this.el.querySelector('input,select,textarea'); if (i) i.focus(); },

  // several widgets selected: line them up, space them out, make them the same size
  renderSeveral(el, sels) {
    const parent = M.parent(sels[0]), flex = !!(parent && parent.flex);
    el.append(U.el('div', { class: 'props-head' }, I18N.f('{0} widgets selected', sels.length)));
    const go = kind => M.arrange(kind, sels.map(p => Object.assign({ path: p }, Canvas.rect(p))).filter(r => r.w !== undefined));
    const btn = (icon, kind, title, min) => U.el('button', { class: 'arr', title, disabled: flex || sels.length < (min || 2), onclick: () => go(kind) }, icon);
    el.append(this.section('Align', true,
      U.el('div', { class: 'arr-row' }, btn('⇤', 'left', 'Align left edges'), btn('⇹', 'hcenter', 'Align centers horizontally'), btn('⇥', 'right', 'Align right edges'),
        btn('⤒', 'top', 'Align top edges'), btn('⇳', 'vcenter', 'Align centers vertically'), btn('⤓', 'bottom', 'Align bottom edges')),
      U.el('div', { class: 'arr-row' }, btn('⋯', 'hdist', 'Space out horizontally (equal gaps)', 3), btn('⋮', 'vdist', 'Space out vertically (equal gaps)', 3),
        btn('↔', 'samew', 'Same width as the first selected'), btn('↕', 'sameh', 'Same height as the first selected')),
      flex ? U.el('div', { class: 'hint' }, 'These widgets are placed by their panel\'s layout (flex); change the order in the tree instead.') : null));
    el.append(U.el('div', { class: 'hint' }, 'Drag one of them to move all. Arrow keys move by 1 px (Shift: 10). Ctrl+C / Ctrl+V copy and paste, Ctrl+D duplicates, Del deletes. Ctrl+click adds or removes a widget.'));
  },

  // ---- style -------------------------------------------------------------
  styleSection(w, fk) {
    const sel = U.el('select', { onchange: e => { this.styleSel = e.target.value; this.render(); } });
    for (const s of SCHEMA.styleSelectors) {
      const has = w[s] && Object.keys(w[s]).length;
      sel.append(U.el('option', { value: s, selected: s === this.styleSel }, s + (has ? ' ●' : '')));
    }
    const cur = this.styleSel, has = !!(w[cur] && typeof w[cur] === 'object');
    // The usual properties are always offered (empty = the theme's value); a value typed into
    // one of them creates the style object, emptying the last one removes it again.
    const st = has ? w[cur] : {};
    const rows = [];
    const shown = SCHEMA.commonStyle.concat(Object.keys(st).filter(k => !SCHEMA.commonStyle.includes(k)));
    for (const k of shown) {
      const kind = SCHEMA.styleKeys[k];
      const f = Array.isArray(kind) ? { t: 'select', o: [''].concat(kind) } : { t: kind || 'text' };
      const set = k in st;
      // edits go through a holder so that the style object only exists while it has something in it
      const holder = new Proxy(st, {
        set: (o, p, val) => { if (!w[cur] || typeof w[cur] !== 'object') w[cur] = o; o[p] = val; return true; },
        deleteProperty: (o, p) => { delete o[p]; if (w[cur] === o && !Object.keys(o).length) delete w[cur]; return true; },
      });
      const del = set ? U.el('button', { class: 'mini danger', title: 'remove', onclick: () => M.mutate(() => { delete holder[k]; }) }, '×') : null;
      rows.push(U.el('div', { class: 'row' + (set ? '' : ' unset'), title: I18N.named(k) ? k : '' }, U.el('label', null, I18N.label(k)), this.editor(holder, k, f, fk + '.' + cur), del));
    }
    const add = U.el('select', { onchange: e => {
      const k = e.target.value; if (!k) return;
      const kind = SCHEMA.styleKeys[k];
      const def = kind === 'color' ? '#ffffff' : kind === 'font' ? 16 : kind === 'bool' ? true : Array.isArray(kind) ? kind[0] : kind === 'size' ? 100 : 0;
      M.mutate(() => { w[cur] = w[cur] || {}; w[cur][k] = def; });
    } }, U.el('option', { value: '' }, I18N.t('+ add property…')));
    for (const k of Object.keys(SCHEMA.styleKeys)) if (!shown.includes(k)) add.append(U.el('option', { value: k }, I18N.named(k) ? I18N.label(k) + '  (' + k + ')' : k));
    return this.section('Style', true, this.row('selector', sel), ...rows, this.row('', add));
  },

  // ---- bind --------------------------------------------------------------
  bindEntries(w) {
    const b = w.bind, nat = SCHEMA.naturalBind[w.type] || 'value';
    if (b === undefined || b === null) return [];
    if (typeof b === 'string') return [{ kind: nat, cfg: { tag: b } }];
    if (typeof b === 'object') return Object.entries(b).map(([kind, v]) => ({ kind, cfg: typeof v === 'string' ? { tag: v } : U.clone(v) || {} }));
    return [];
  },
  writeBind(w, entries) {
    if (!entries.length) { delete w.bind; return; }
    const nat = SCHEMA.naturalBind[w.type] || 'value';
    const simple = e => Object.keys(e.cfg).length === 1 && typeof e.cfg.tag === 'string';
    if (entries.length === 1 && entries[0].kind === nat && simple(entries[0])) { w.bind = entries[0].cfg.tag; return; }
    const o = {};
    for (const e of entries) o[e.kind] = simple(e) ? e.cfg.tag : e.cfg;
    w.bind = o;
  },
  bindSection(w, fk) {
    const entries = this.bindEntries(w), blocks = [];
    const commit = () => M.mutate(() => this.writeBind(w, entries));
    entries.forEach((e, i) => {
      const cfg = e.cfg, bfk = fk + '.bind.' + i;
      const kind = U.el('select', { onchange: ev => { e.kind = ev.target.value; commit(); } });
      for (const k of SCHEMA.bindKinds) kind.append(U.el('option', { value: k, selected: k === e.kind }, I18N.named(k) ? I18N.label(k) + '  (' + k + ')' : I18N.value(k)));
      const tag = U.el('input', { type: 'text', list: 'dl-tags', value: cfg.tag || '', placeholder: 'tag', dataset: { fk: bfk + '.tag' }, onchange: ev => { cfg.tag = ev.target.value.trim(); commit(); } });
      const del = U.el('button', { class: 'mini danger', onclick: () => { entries.splice(i, 1); commit(); } }, '×');
      const head = U.el('div', { class: 'row bind-head' }, kind, tag, del);
      const opt = (label, input) => U.el('div', { class: 'row sub' }, U.el('label', null, label), input);
      const txt = (k, ph) => U.el('input', { type: 'text', value: cfg[k] === undefined ? '' : cfg[k], placeholder: ph || '', dataset: { fk: bfk + '.' + k },
        onchange: ev => { const s = ev.target.value; if (s === '') delete cfg[k]; else cfg[k] = s; commit(); } });
      const num = k => U.el('input', { type: 'number', step: 'any', value: cfg[k] === undefined ? '' : cfg[k], dataset: { fk: bfk + '.' + k },
        onchange: ev => { if (ev.target.value === '') delete cfg[k]; else cfg[k] = Number(ev.target.value); commit(); } });
      const chk = k => U.el('input', { type: 'checkbox', checked: !!cfg[k], onchange: ev => { if (ev.target.checked) cfg[k] = true; else delete cfg[k]; commit(); } });
      const details = [];
      if (e.kind === 'text') details.push(opt('format', txt('format', '%.1f C')));
      if (e.kind === 'value' || e.kind === 'text') details.push(opt('scale', num('scale')), opt('offset', num('offset')));
      if (['visible', 'hidden', 'enabled', 'disabled', 'checked', 'on'].includes(e.kind)) {
        const op = U.el('select', { onchange: ev => { for (const o of SCHEMA.bindOps) if (o) delete cfg[o]; if (ev.target.value) cfg[ev.target.value] = cfg[ev.target.value] === undefined ? 0 : cfg[ev.target.value]; commit(); } });
        let curOp = SCHEMA.bindOps.find(o => o && cfg[o] !== undefined) || '';
        for (const o of SCHEMA.bindOps) op.append(U.el('option', { value: o, selected: o === curOp }, o === '' ? '(truthy)' : o));
        // a constant, or "{tag}" to compare with another tag ({ "tag": "name" } in the JSON)
        const condText = v => (v && typeof v === 'object') ? '{' + (v.tag || '') + '}' : v;
        const val = U.el('input', { type: 'text', value: curOp ? condText(cfg[curOp]) : '', disabled: !curOp, placeholder: 'value or {tag}', dataset: { fk: bfk + '.cond' },
          onchange: ev => { const s = ev.target.value.trim(); const m = /^\{(.+)\}$/.exec(s); cfg[curOp] = m ? { tag: m[1].trim() } : /^-?\d+(\.\d+)?$/.test(s) ? Number(s) : s; commit(); } });
        details.push(opt('condition', U.el('span', { class: 'inline' }, op, val)), opt('invert', chk('invert')));
      }
      if (e.kind === 'bg_color' || e.kind === 'text_color') {
        const map = cfg.map = cfg.map || {};
        const mrows = Object.keys(map).map(key => U.el('div', { class: 'row sub' },
          U.el('input', { type: 'text', class: 'map-key', value: key, dataset: { fk: bfk + '.map.' + key }, onchange: ev => { const nk = ev.target.value.trim(); if (nk && nk !== key) { map[nk] = map[key]; delete map[key]; } commit(); } }),
          U.el('input', { type: 'color', value: U.hex6(map[key], '#808080'), onchange: ev => { map[key] = ev.target.value; commit(); } }),
          U.el('input', { type: 'text', class: 'color-text', value: map[key], dataset: { fk: bfk + '.mapv.' + key }, onchange: ev => { map[key] = ev.target.value.trim(); commit(); } }),
          U.el('button', { class: 'mini danger', onclick: () => { delete map[key]; commit(); } }, '×')));
        const addMap = U.el('button', { class: 'mini', onclick: () => { let n = 0; while (map[String(n)] !== undefined) n++; map[String(n)] = '#ffffff'; commit(); } }, '+ map entry');
        if (e.kind === 'bg_color') details.push(opt('part', (() => { const s = U.el('select', { onchange: ev => { if (ev.target.value) cfg.part = ev.target.value; else delete cfg.part; commit(); } }); for (const p of ['', 'main', 'indicator', 'knob']) s.append(U.el('option', { value: p, selected: (cfg.part || '') === p }, p || '(main)')); return s; })()));
        details.push(U.el('div', { class: 'row sub' }, U.el('label', null, 'map value → color')), ...mrows, U.el('div', { class: 'row sub' }, U.el('label'), addMap));
      }
      details.push(opt('two_way', chk('two_way')));
      blocks.push(U.el('div', { class: 'bind-block' }, head, ...details));
    });
    const add = U.el('button', { class: 'mini', onclick: () => { entries.push({ kind: SCHEMA.naturalBind[w.type] || 'value', cfg: { tag: M.tagNames()[0] || '' } }); commit(); } }, '+ bind');
    return this.section('Bind (tags)', entries.length > 0, ...blocks, U.el('div', { class: 'row' }, U.el('label'), add));
  },

  // ---- events / actions --------------------------------------------------
  eventSection(w, fk) {
    const blocks = [];
    for (const ev of SCHEMA.events) {
      if (w[ev] === undefined) continue;
      const acts = Array.isArray(w[ev]) ? w[ev] : [w[ev]];
      const commit = () => M.mutate(() => { w[ev] = acts; if (!acts.length) delete w[ev]; });
      const rows = acts.map((a, i) => {
        const sel = U.el('select', { onchange: e => { const kind = e.target.value; for (const k of Object.keys(a)) delete a[k]; a.action = kind; commit(); } });
        for (const k of Object.keys(SCHEMA.actions)) sel.append(U.el('option', { value: k, selected: k === a.action }, I18N.action(k)));
        const fieldsEls = (SCHEMA.actions[a.action] || []).map(f => U.el('span', { class: 'inline' }, U.el('span', { class: 'dim' }, f.k), this.editor(a, f.k, f, fk + '.' + ev + '.' + i)));
        const del = U.el('button', { class: 'mini danger', onclick: () => { acts.splice(i, 1); commit(); } }, '×');
        return U.el('div', { class: 'row act' }, sel, ...fieldsEls, del);
      });
      const add = U.el('button', { class: 'mini', onclick: () => { acts.push({ action: 'set', tag: M.tagNames()[0] || '', value: true }); commit(); } }, '+ action');
      const delEv = U.el('button', { class: 'mini danger', title: 'remove event', onclick: () => M.mutate(() => delete w[ev]) }, '×');
      blocks.push(U.el('div', { class: 'ev-block' }, U.el('div', { class: 'row ev-head' }, U.el('b', null, ev), delEv), ...rows, U.el('div', { class: 'row' }, U.el('label'), add)));
    }
    const addEv = U.el('select', { onchange: e => { const ev = e.target.value; if (ev) M.mutate(() => { w[ev] = [{ action: 'set', tag: M.tagNames()[0] || '', value: true }]; }); } }, U.el('option', { value: '' }, '+ add event…'));
    for (const ev of SCHEMA.events) if (w[ev] === undefined) addEv.append(U.el('option', { value: ev }, I18N.value(ev)));
    return this.section('Events / actions', blocks.length > 0, ...blocks, U.el('div', { class: 'row' }, U.el('label'), addEv));
  },

  // ---- chart series ------------------------------------------------------
  seriesEditor(w, fk) {
    const series = w.series = Array.isArray(w.series) ? w.series : [];
    const rows = series.map((s, i) => U.el('div', { class: 'row sub' },
      this.editor(s, 'tag', { t: 'tag' }, fk + '.series.' + i),
      this.editor(s, 'color', { t: 'color' }, fk + '.series.' + i),
      U.el('input', { type: 'number', step: 'any', value: s.scale === undefined ? '' : s.scale, placeholder: 'scale', dataset: { fk: fk + '.series.' + i + '.scale' }, onchange: e => this.setVal(s, 'scale', e.target.value === '' ? undefined : Number(e.target.value)) }),
      U.el('button', { class: 'mini danger', onclick: () => M.mutate(() => series.splice(i, 1)) }, '×')));
    const add = U.el('button', { class: 'mini', onclick: () => M.mutate(() => series.push({ tag: M.tagNames()[0] || '', color: series.length ? '#4caf50' : '#2196f3' })) }, '+ series');
    return U.el('div', null, U.el('div', { class: 'row' }, U.el('label', null, 'series (tag / color / scale)')), ...rows, U.el('div', { class: 'row' }, U.el('label'), add));
  },
};
