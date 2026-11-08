// Application wiring: toolbar, keyboard, host bridge, dialogs. Global `App`, `host`.
'use strict';

// Functions the C++ host binds into the page (window.hml_*). Each returns a Promise.
const host = {
  call(name, ...args) {
    const f = window[name];
    if (typeof f !== 'function') return Promise.resolve({ ok: false, error: 'host function missing: ' + name });
    return f(...args).catch(e => ({ ok: false, error: String(e) }));
  },
  info: () => host.call('hml_info'),
  readProject: p => host.call('hml_readProject', p),
  writeFiles: (dir, files) => host.call('hml_writeFiles', dir, files),
  openDialog: (kind, dir) => host.call('hml_openDialog', kind, dir || ''),
  saveDialog: (name, dir) => host.call('hml_saveDialog', name, dir || ''),
  pickImage: dir => host.call('hml_pickImage', dir || ''),
  preview: (p, s) => host.call('hml_preview', p, s),
  launch: p => host.call('hml_launch', p),
  setTitle: t => host.call('hml_setTitle', t),
  settings: o => host.call('hml_settings', o || null),
};

const App = {
  info: {}, refreshTimer: 0,

  async init() {
    Canvas.init(); Tree.init(); Props.init(); Panels.init();
    const q = id => document.getElementById(id);
    q('b-new').onclick = () => this.newProjectDialog();
    q('b-open').onclick = () => this.openDialog();
    q('b-save').onclick = () => this.save(false);
    q('b-saveas').onclick = () => this.save(true);
    q('b-undo').onclick = () => M.undo();
    q('b-redo').onclick = () => M.redo();
    q('b-zoom-in').onclick = () => this.zoom(M.st.zoom * 1.25);
    q('b-zoom-out').onclick = () => this.zoom(M.st.zoom / 1.25);
    q('b-zoom-fit').onclick = () => this.zoomFit();
    q('b-json').onclick = () => this.jsonDialog();
    q('b-preview').onclick = () => this.preview();
    q('b-run').onclick = () => this.run();
    q('sel-screen').onchange = e => { Panels.showView('screen'); M.selectScreen(Number(e.target.value)); };
    q('t-dup').onclick = () => M.duplicateWidget(M.st.sel);
    q('t-del').onclick = () => M.deleteWidget(M.st.sel);
    q('t-up').onclick = () => M.moveWidget(M.st.sel, -1);
    q('t-down').onclick = () => M.moveWidget(M.st.sel, 1);
    q('t-in').onclick = () => M.reparentWidget(M.st.sel, 'in');
    q('t-out').onclick = () => M.reparentWidget(M.st.sel, 'out');
    document.addEventListener('keydown', e => this.onKey(e));
    window.addEventListener('resize', U.debounce(() => this.status(), 200));

    this.info = (await host.info()) || {};
    if (this.info.argvProject) await this.open(this.info.argvProject);
    else { M.newProject('New HMI', 800, 480); M.st.dirty = false; }
    this.refresh();
    if (!this.info.runtime) this.toast('hml_runtime not found next to the designer: Preview / Run are unavailable');
  },

  // ---- refresh -----------------------------------------------------------
  refresh() {
    if (this.refreshTimer) return;
    this.refreshTimer = setTimeout(() => { this.refreshTimer = 0; this.doRefresh(); }, 0);
  },
  doRefresh() {
    const ae = document.activeElement, fk = ae && ae.dataset ? ae.dataset.fk : null;
    Canvas.render(); Tree.render(); Props.render(); Panels.render(); this.toolbar(); this.status();
    if (fk) { const e = document.querySelector('[data-fk="' + CSS.escape(fk) + '"]'); if (e) e.focus(); }
  },
  select(path) { M.st.sel = path; Canvas.render(); Tree.render(); Props.render(); this.status(); },
  addWidget(type) { Panels.showView('screen'); M.addWidget(type, M.st.sel); },

  toolbar() {
    const st = M.st, sel = document.getElementById('sel-screen');
    sel.innerHTML = '';
    st.screens.forEach((s, i) => sel.append(U.el('option', { value: i, selected: i === st.cur }, s.data.name)));
    document.getElementById('b-undo').disabled = !st.undo.length;
    document.getElementById('b-redo').disabled = !st.redo.length;
    document.getElementById('zoom-label').textContent = Math.round(st.zoom * 100) + '%';
    const name = st.path ? U.basename(U.dirname(st.path)) + '/project.json' : '(unsaved)';
    const title = (st.dirty ? '● ' : '') + (st.project ? st.project.title || '' : '') + '  —  ' + name;
    document.getElementById('title-label').textContent = title;
    host.setTitle('HML Designer — ' + title);
    const noRt = !this.info.runtime;
    document.getElementById('b-preview').disabled = noRt; document.getElementById('b-run').disabled = noRt;
  },
  status(msg) {
    const el = document.getElementById('status');
    if (msg) { el.textContent = msg; return; }
    const w = M.selected(), d = M.display();
    let s = d.w + '×' + d.h;
    if (w) {
      if (M.st.sel.length) s += '  |  ' + (w.type || 'panel') + ' #' + U.pathStr(M.st.sel) + '  x=' + U.num(w.x, 0) + ' y=' + U.num(w.y, 0) + (w.w !== undefined ? ' w=' + w.w : '') + (w.h !== undefined ? ' h=' + w.h : '') + (w.align ? ' align=' + w.align : '');
      else s += '  |  screen "' + (w.name || '') + '"';
    }
    if (M.st.path) s += '  |  ' + M.st.path;
    el.textContent = s;
  },
  toast(msg, ms) {
    const t = document.getElementById('toast');
    t.textContent = msg; t.hidden = false;
    clearTimeout(this._toast); this._toast = setTimeout(() => { t.hidden = true; }, ms || 3500);
  },

  // ---- modal -------------------------------------------------------------
  modal(opt) {
    const m = document.getElementById('modal');
    document.getElementById('modal-title').textContent = opt.title || '';
    const body = document.getElementById('modal-body'); body.innerHTML = '';
    if (opt.body) body.append(opt.body);
    const btns = document.getElementById('modal-buttons'); btns.innerHTML = '';
    const close = () => { m.hidden = true; };
    for (const b of opt.buttons || [{ label: 'Close' }]) {
      btns.append(U.el('button', { class: b.primary ? 'primary' : '', onclick: async () => { if (!b.onclick || (await b.onclick()) !== false) close(); } }, b.label));
    }
    m.hidden = false;
    const first = body.querySelector('input,textarea,select'); if (first) { first.focus(); if (first.select) first.select(); }
    return close;
  },
  prompt(title, def) {
    return new Promise(resolve => {
      const inp = U.el('input', { type: 'text', value: def || '', style: { width: '100%' }, onkeydown: e => { if (e.key === 'Enter') { ok(); } } });
      let done = false;
      const ok = () => { if (done) return; done = true; close(); resolve(inp.value.trim() || null); };
      const close = this.modal({ title, body: inp, buttons: [{ label: 'Cancel', onclick: () => { if (!done) { done = true; resolve(null); } } }, { label: 'OK', primary: true, onclick: () => { ok(); return false; } }] });
    });
  },

  // ---- project files -----------------------------------------------------
  confirmDiscard() { return !M.st.dirty || confirm('Discard unsaved changes?'); },

  newProjectDialog() {
    if (!this.confirmDiscard()) return;
    const title = U.el('input', { type: 'text', value: 'My HMI' });
    const w = U.el('input', { type: 'number', value: 800 }), h = U.el('input', { type: 'number', value: 480 });
    const presets = U.el('div', { class: 'row' }, U.el('label', null, 'presets'),
      ...[[480, 320, '3.5" LCD'], [800, 480, '7" LCD'], [1024, 600, '10" LCD'], [1280, 720, 'HD']].map(p => U.el('button', { class: 'mini', onclick: () => { w.value = p[0]; h.value = p[1]; } }, p[0] + '×' + p[1] + ' ' + p[2])));
    const body = U.el('div', null, U.el('div', { class: 'row' }, U.el('label', null, 'title'), title),
      U.el('div', { class: 'row' }, U.el('label', null, 'width × height'), w, U.el('span', null, '×'), h), presets,
      U.el('div', { class: 'hint' }, 'You will be asked where to save project.json (use an empty folder: screens/ and assets/ are created next to it).'));
    this.modal({ title: 'New project', body, buttons: [{ label: 'Cancel' }, { label: 'Create', primary: true, onclick: async () => {
      M.newProject(title.value.trim() || 'New HMI', Number(w.value) || 800, Number(h.value) || 480);
      this.refresh();
      await this.save(true);
    } }] });
  },

  async openDialog() {
    if (!this.confirmDiscard()) return;
    const p = await host.openDialog('project', M.st.dir || '');
    if (p) await this.open(p);
  },
  async open(path) {
    const r = await host.readProject(path);
    if (!r || !r.ok) { this.toast('open failed: ' + (r && r.error)); return; }
    try { M.loadFromHost(r); } catch (e) { this.toast('parse error: ' + e.message, 6000); return; }
    this.refresh();
    setTimeout(() => this.zoomFit(), 50);
  },
  async save(as) {
    const st = M.st;
    if (!st.project) return false;
    if (!st.path || as) {
      const p = await host.saveDialog('project.json', st.dir || '');
      if (!p) return false;
      st.path = p; st.dir = U.dirname(p);
    }
    const r = await host.writeFiles(st.dir, M.filesToWrite());
    if (!r || !r.ok) { this.toast('save failed: ' + (r && r.error), 6000); return false; }
    st.dirty = false;
    this.refresh();
    this.toast('saved ' + st.path, 1500);
    return true;
  },
  async pickImage() {
    if (!M.st.dir) { this.toast('save the project first (images are copied into its assets/ folder)'); return null; }
    const r = await host.pickImage(M.st.dir);
    return r && r.ok ? r.rel : null;
  },

  // ---- preview / run -----------------------------------------------------
  async preview() {
    if (!(await this.save(false))) return;
    const scr = M.screen(); if (!scr) return;
    const body = U.el('div', null, U.el('div', { class: 'hint' }, 'Rendering with hml_runtime…'));
    const close = this.modal({ title: 'Preview: ' + scr.name, body, buttons: [{ label: 'Close' }] });
    const r = await host.preview(M.st.path, scr.name);
    body.innerHTML = '';
    if (r && r.ok && r.image) {
      const img = U.el('img', { class: 'preview', src: r.image });
      img.style.width = Math.round(M.display().w * Math.min(1, (window.innerWidth * 0.85) / M.display().w)) + 'px';
      body.append(img);
      if (r.log && /WARN|ERROR/.test(r.log)) body.append(U.el('pre', null, r.log.split('\n').filter(l => /WARN|ERROR/.test(l)).join('\n')));
    } else {
      body.append(U.el('div', { class: 'warn' }, 'Preview failed: ' + ((r && r.error) || 'unknown error')), U.el('pre', null, (r && r.log) || ''));
    }
  },
  async run() {
    if (!(await this.save(false))) return;
    const r = await host.launch(M.st.path);
    if (!r || !r.ok) this.toast('run failed: ' + (r && r.error));
  },

  // ---- json --------------------------------------------------------------
  jsonDialog() {
    const scr = M.screen(); if (!scr) return;
    const ta = U.el('textarea', { class: 'json mono', value: JSON.stringify(scr, null, 2) });
    this.modal({ title: 'Screen JSON: ' + M.st.screens[M.st.cur].file, body: ta, buttons: [{ label: 'Cancel' }, { label: 'Apply', primary: true, onclick: () => {
      try { const o = U.parseLooseJson(ta.value); if (!o.children) o.children = []; if (!o.name) o.name = scr.name; M.mutate(() => { M.st.screens[M.st.cur].data = o; M.st.sel = []; }); }
      catch (e) { this.toast('JSON: ' + e.message, 5000); return false; }
    } }] });
  },

  // ---- zoom --------------------------------------------------------------
  zoom(z) { M.st.zoom = Math.max(0.2, Math.min(4, z)); Canvas.render(); this.toolbar(); },
  zoomFit() {
    const wrap = document.getElementById('canvas-wrap'), d = M.display();
    if (wrap.hidden) return;   // Tags / Drivers view is showing: nothing to fit
    const z = Math.min((wrap.clientWidth - 60) / d.w, (wrap.clientHeight - 60) / d.h);
    this.zoom(Math.max(0.2, Math.min(2, Math.floor(z * 20) / 20)));
  },

  // ---- keyboard ----------------------------------------------------------
  onKey(e) {
    const inInput = /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName) || e.target.isContentEditable;
    const ctrl = e.ctrlKey || e.metaKey;
    if (ctrl && e.key.toLowerCase() === 's') { e.preventDefault(); this.save(e.shiftKey); return; }
    if (ctrl && e.key.toLowerCase() === 'o') { e.preventDefault(); this.openDialog(); return; }
    if (ctrl && e.key.toLowerCase() === 'n') { e.preventDefault(); this.newProjectDialog(); return; }
    if (ctrl && /^[123]$/.test(e.key)) { e.preventDefault(); Panels.showView(['screen', 'tags', 'drivers'][Number(e.key) - 1]); return; }
    if (ctrl && e.key.toLowerCase() === 'z' && !inInput) { e.preventDefault(); if (e.shiftKey) M.redo(); else M.undo(); return; }
    if (ctrl && e.key.toLowerCase() === 'y' && !inInput) { e.preventDefault(); M.redo(); return; }
    if (!document.getElementById('modal').hidden) { if (e.key === 'Escape') document.getElementById('modal').hidden = true; return; }
    if (inInput || Panels.view !== 'screen') return;   // widget shortcuts only act on the canvas
    const sel = M.st.sel, w = M.selected();
    if (ctrl && e.key.toLowerCase() === 'd') { e.preventDefault(); M.duplicateWidget(sel); return; }
    if (e.key === 'Delete' || e.key === 'Backspace') { if (sel && sel.length) { e.preventDefault(); M.deleteWidget(sel); } return; }
    if (e.key === 'Escape') { this.select(null); return; }
    if (/^Arrow/.test(e.key) && w && sel && sel.length) {
      const parent = M.parent(sel); if (parent && parent.flex) return;
      e.preventDefault();
      const step = e.shiftKey ? 10 : 1;
      M.mutate(() => {
        if (e.key === 'ArrowLeft') w.x = U.num(w.x, 0) - step;
        if (e.key === 'ArrowRight') w.x = U.num(w.x, 0) + step;
        if (e.key === 'ArrowUp') w.y = U.num(w.y, 0) - step;
        if (e.key === 'ArrowDown') w.y = U.num(w.y, 0) + step;
      });
    }
  },
};

window.addEventListener('DOMContentLoaded', () => App.init());
