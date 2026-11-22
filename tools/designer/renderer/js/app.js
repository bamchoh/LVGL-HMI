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
  readProject: (p, workingCopy) => host.call('hml_readProject', p, !!workingCopy),
  projectStamp: (dir, files) => host.call('hml_projectStamp', dir || '', files || []),
  openFolder: dir => host.call('hml_openFolder', dir || ''),
  installSkill: () => host.call('hml_installSkill'),
  addFont: dir => host.call('hml_addFont', dir || ''),
  addIcons: dir => host.call('hml_addIcons', dir || ''),
  writeFiles: (dir, files) => host.call('hml_writeFiles', dir, files),
  openDialog: (kind, dir) => host.call('hml_openDialog', kind, dir || ''),
  saveDialog: (name, dir) => host.call('hml_saveDialog', name, dir || ''),
  pickImage: dir => host.call('hml_pickImage', dir || ''),
  preview: (p, s) => host.call('hml_preview', p, s),
  launch: p => host.call('hml_launch', p),
  runLog: from => host.call('hml_runLog', from || 0),
  runStop: () => host.call('hml_runStop'),
  deploy: o => host.call('hml_deploy', o),
  deployCheck: o => host.call('hml_deployCheck', o),
  serialPorts: o => host.call('hml_serialPorts', o || {}),
  folderInfo: dir => host.call('hml_folderInfo', dir || ''),
  archiveDir: (file, clean) => host.call('hml_archiveDir', file, !!clean),
  packProject: (dir, file) => host.call('hml_packProject', dir, file),
  copyProjectFiles: (from, to) => host.call('hml_copyProjectFiles', from || '', to),
  localSettings: (p, o) => host.call('hml_localSettings', p || '', o || null),
  keyInfo: o => host.call('hml_keyInfo', o),
  keySend: o => host.call('hml_keySend', o),
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
    q('b-recent').onclick = () => this.recentDialog();
    q('b-keys').onclick = () => this.shortcutsDialog();
    q('canvas-wrap').addEventListener('dragover', e => Canvas.onDragOver(e));
    q('canvas-wrap').addEventListener('drop', e => Canvas.onDrop(e));
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
    q('runlog-stop').onclick = () => this.runStop();
    q('runlog-close').onclick = () => this.runLogShow(false);
    q('runlog-copy').onclick = () => { navigator.clipboard.writeText(q('runlog-text').textContent).then(() => this.toast('Copied.'), () => this.toast('Could not copy.')); };
    q('status-run').onclick = () => this.runLogShow(q('runlog').hidden);
    q('b-deploy').onclick = () => this.deployDialog();
    q('b-ai').onclick = () => this.aiDialog();
    // files changed from outside (an AI assistant, a text editor): looked for when the window
    // gets the focus back, and every few seconds while it has it
    window.addEventListener('focus', () => this.checkOutside());
    setInterval(() => { if (document.hasFocus()) this.checkOutside(); }, 3000);
    q('sel-screen').onchange = e => { Panels.showView('screen'); M.selectScreen(Number(e.target.value)); };
    q('t-dup').onclick = () => M.duplicateSelection();
    q('t-del').onclick = () => M.deleteSelection();
    q('status-prob').onclick = () => this.problemsDialog();
    q('b-replace').onclick = () => this.replaceDialog();
    q('t-up').onclick = () => M.moveWidget(M.st.sel, -1);
    q('t-down').onclick = () => M.moveWidget(M.st.sel, 1);
    q('t-in').onclick = () => M.reparentWidget(M.st.sel, 'in');
    q('t-out').onclick = () => M.reparentWidget(M.st.sel, 'out');
    document.addEventListener('keydown', e => this.onKey(e));
    window.addEventListener('resize', U.debounce(() => this.status(), 200));

    this.info = (await host.info()) || {};
    // the language of the designer: what was chosen before, else the system's
    const saved = (await host.settings()) || {};
    I18N.init(saved.lang);
    this.recent = Array.isArray(saved.recent) ? saved.recent.filter(p => typeof p === 'string') : [];
    // (what the settings file holds is cleaned like a library from outside: somebody may have edited it)
    this.library = M.libraryImport(JSON.stringify(Array.isArray(saved.library) ? saved.library : []), []).parts;
    const langSel = q('sel-lang');
    for (const [code, name] of Object.entries(I18N.LANGS)) langSel.append(U.el('option', { value: code, selected: code === I18N.lang }, name));
    langSel.onchange = () => { I18N.set(langSel.value); host.settings({ lang: I18N.lang }); this.staticTexts(); this.doRefresh(); };
    this.staticTexts();
    if (this.info.argvProject) await this.open(this.info.argvProject);
    else { M.newProject('New HMI', 800, 480); M.st.dirty = false; this.offerRecovery(); }
    this.refresh();
    if (!this.info.runtime) this.toast('hml_runtime not found next to the designer: Preview / Run are unavailable');
  },

  // The texts that are part of the page itself (toolbar, tabs): shown in the chosen language.
  staticTexts() {
    for (const b of document.querySelectorAll('#toolbar button, #left-tabs button, #center-tabs button, #runlog-head button, #runlog-head b')) {
      if (b.dataset.i18n === undefined) b.dataset.i18n = b.textContent;
      if (/[A-Za-z]/.test(b.dataset.i18n)) b.textContent = I18N.t(b.dataset.i18n);       // (not the arrow / symbol buttons)
    }
    for (const b of document.querySelectorAll('#toolbar [title], #center-tabs [title], #tree-tools [title], #runlog-head [title]')) {
      if (b.dataset.i18nTitle === undefined) b.dataset.i18nTitle = b.title;
      b.title = I18N.t(b.dataset.i18nTitle);
    }
    this.runLogState();
    const scr = document.querySelector('#toolbar label');                                 // "Screen <select>"
    if (scr && scr.firstChild && scr.firstChild.nodeType === 3) {
      if (scr.dataset.i18n === undefined) scr.dataset.i18n = scr.firstChild.textContent;
      scr.firstChild.textContent = I18N.t(scr.dataset.i18n.trim()) + ' ';
    }
  },

  // ---- refresh -----------------------------------------------------------
  refresh() {
    if (this.refreshTimer) return;
    this.refreshTimer = setTimeout(() => { this.refreshTimer = 0; this.doRefresh(); }, 0);
  },
  doRefresh() {
    const ae = document.activeElement, fk = ae && ae.dataset ? ae.dataset.fk : null;
    this.problems = M.problems();
    this.autosave();
    this.renderTabs();
    Canvas.render(); Tree.render(); Props.render(); Panels.render(); this.toolbar(); this.status();
    if (fk) { const e = document.querySelector('[data-fk="' + CSS.escape(fk) + '"]'); if (e) e.focus(); }
  },
  select(path) { M.st.sel = path; M.st.more = []; this.selectionChanged(); },
  selectionChanged() { Canvas.render(); Tree.render(); Props.render(); this.status(); },

  // ---- copy and paste (also from one screen to another) -------------------
  clip: null,
  copy() {
    const ws = M.copySelection();
    if (!ws.length) return false;
    this.clip = ws;
    this.toast(I18N.f('{0} widget(s) copied', ws.length), 1200);
    return true;
  },
  cut() { if (this.copy()) M.deleteSelection(); },
  paste() { if (this.clip) M.paste(this.clip); },

  // ---- screen tabs: the screens being worked on, above the canvas -----------
  tabs: [],                                         // the files of the open screens
  renderTabs() {
    const bar = document.getElementById('screen-tabs'), st = M.st;
    bar.hidden = Panels.view !== 'screen' || !st.screens.length;
    bar.innerHTML = '';
    if (bar.hidden) return;
    const files = st.screens.map(s => s.file), cur = st.screens[st.cur];
    this.tabs = this.tabs.filter(f => files.includes(f));
    if (cur && !this.tabs.includes(cur.file)) this.tabs.push(cur.file);
    for (const f of this.tabs) {
      const i = files.indexOf(f), s = st.screens[i];
      bar.append(U.el('div', { class: 'stab' + (i === st.cur ? ' active' : ''), title: s.file,
        onmousedown: e => { if (e.button === 1) { e.preventDefault(); this.closeTab(f); } },
        onclick: () => { if (i !== M.st.cur) M.selectScreen(i); } },
        U.el('span', null, (st.project.start_screen === s.data.name ? '● ' : '') + s.data.name),
        this.tabs.length > 1 ? U.el('span', { class: 'x', title: 'Close the tab (the screen stays in the project)', onclick: e => { e.stopPropagation(); this.closeTab(f); } }, '×') : null));
    }
    const rest = st.screens.filter(s => !this.tabs.includes(s.file));
    if (rest.length) {
      const sel = U.el('select', { class: 'stab-more', title: 'Open another screen in a tab', onchange: e => { const i = files.indexOf(e.target.value); if (i >= 0) M.selectScreen(i); } },
        U.el('option', { value: '' }, '+'), ...rest.map(s => U.el('option', { value: s.file }, s.data.name)));
      bar.append(sel);
    }
  },
  closeTab(file) {
    if (this.tabs.length < 2) return;
    const at = this.tabs.indexOf(file), cur = M.st.screens[M.st.cur];
    this.tabs.splice(at, 1);
    if (cur && cur.file === file) { const next = this.tabs[Math.min(at, this.tabs.length - 1)]; M.selectScreen(M.st.screens.findIndex(s => s.file === next)); }
    else this.renderTabs();
  },
  nextTab(dir) {
    const cur = M.st.screens[M.st.cur];
    if (!cur || this.tabs.length < 2) return;
    const next = this.tabs[(this.tabs.indexOf(cur.file) + dir + this.tabs.length) % this.tabs.length];
    M.selectScreen(M.st.screens.findIndex(s => s.file === next));
  },

  // ---- context menu -------------------------------------------------------------
  // items: [{ label, key, onclick, disabled }] or null for a separator
  menu(x, y, items) {
    this.closeMenu();
    const m = U.el('div', { id: 'ctx-menu' });
    for (const it of items) {
      if (!it) { m.append(U.el('div', { class: 'sep' })); continue; }
      m.append(U.el('div', { class: 'mi' + (it.disabled ? ' off' : ''), onclick: () => { if (it.disabled) return; this.closeMenu(); it.onclick(); } },
        U.el('span', null, I18N.t(it.label)), U.el('span', { class: 'k' }, it.key || '')));
    }
    document.body.append(m);
    m.style.left = Math.max(0, Math.min(x, window.innerWidth - m.offsetWidth - 4)) + 'px';
    m.style.top = Math.max(0, Math.min(y, window.innerHeight - m.offsetHeight - 4)) + 'px';
    setTimeout(() => document.addEventListener('mousedown', this._menuOff = e => { if (!m.contains(e.target)) this.closeMenu(); }), 0);
  },
  closeMenu() {
    const m = document.getElementById('ctx-menu');
    if (m) m.remove();
    if (this._menuOff) { document.removeEventListener('mousedown', this._menuOff); this._menuOff = null; }
  },
  widgetMenu(e, path) {
    // a right click on something that is not selected selects it first
    if (!M.selection().some(p => U.samePath(p, path)) && !(path.length === 0 && U.samePath(M.st.sel, []))) this.select(path);
    const n = M.selection().length, none = n === 0;
    this.menu(e.clientX, e.clientY, [
      { label: 'Cut', key: 'Ctrl+X', disabled: none, onclick: () => this.cut() },
      { label: 'Copy', key: 'Ctrl+C', disabled: none, onclick: () => this.copy() },
      { label: 'Paste', key: 'Ctrl+V', disabled: !this.clip, onclick: () => this.paste() },
      { label: 'Duplicate', key: 'Ctrl+D', disabled: none, onclick: () => M.duplicateSelection() },
      { label: 'Delete', key: 'Del', disabled: none, onclick: () => M.deleteSelection() },
      null,
      { label: 'Copy style', key: 'Ctrl+Alt+C', disabled: none, onclick: () => this.copyStyle() },
      { label: 'Paste style', key: 'Ctrl+Alt+V', disabled: none || !this.styleClip, onclick: () => M.pasteStyle(this.styleClip) },
      null,
      M.flag(M.selected(), 'locked') ? { label: 'Unlock', disabled: none, onclick: () => M.setFlag('locked', false) } : { label: 'Lock (cannot be clicked on the canvas)', disabled: none, onclick: () => M.setFlag('locked', true) },
      M.flag(M.selected(), 'hidden') ? { label: 'Show on the canvas', disabled: none, onclick: () => M.setFlag('hidden', false) } : { label: 'Hide on the canvas (designer only)', disabled: none, onclick: () => M.setFlag('hidden', true) },
      { label: 'Show all / unlock all', disabled: !M.anyFlag('hidden') && !M.anyFlag('locked'), onclick: () => M.clearFlag() },
      null,
      { label: 'Bring to front', disabled: none, onclick: () => M.orderSelection('front') },
      { label: 'Send to back', disabled: none, onclick: () => M.orderSelection('back') },
      null,
      { label: 'Select all in this panel', key: 'Ctrl+A', onclick: () => { M.selectSiblings(); this.selectionChanged(); } },
    ]);
  },

  styleClip: null,
  copyStyle() { const w = M.selected(); if (w && M.st.sel.length) { this.styleClip = M.styleOf(w); this.toast('Style copied', 1200); } },

  // ---- texts to and from a spreadsheet (translators work in one) ---------------------
  textsCsvDialog() {
    const ta = U.el('textarea', { class: 'csv', spellcheck: false, value: M.textsCsv() });
    const out = U.el('div');
    const body = U.el('div', null, U.el('div', { class: 'hint' }, 'One text per line: id, then one column per language (the first line names them). Copy this into a spreadsheet for the translators, or paste their lines here (commas or tabs) and press Import: the texts are replaced, new languages are added.'), ta, out);
    this.modal({ title: 'Texts: import / export', body, buttons: [
      { label: 'Copy', onclick: () => { ta.select(); try { document.execCommand('copy'); } catch (_) {} return false; } },
      { label: 'Close' },
      { label: 'Import', primary: true, onclick: () => {
        const r = M.importTextsCsv(ta.value);
        out.innerHTML = '';
        if (r.errors.length) { out.append(U.el('div', { class: 'warn' }, I18N.t('Nothing was imported:')), U.el('pre', null, r.errors.join('\n'))); return false; }
        this.toast(I18N.f('{0} text(s) imported', r.count), 2500);
      } }] });
  },

  // ---- recipes to and from a spreadsheet ----------------------------------------
  recipesCsvDialog(bi) {
    const b = M.recipeBooks()[bi];
    if (!b) return;
    const ta = U.el('textarea', { class: 'csv', spellcheck: false, value: M.recipesCsv(bi) });
    const out = U.el('div');
    const body = U.el('div', null, U.el('div', { class: 'hint' }, 'One recipe per line: name, then one value per item. Copy this into a spreadsheet, or paste lines from one here (commas or tabs) and press Import: the recipes of the book are replaced. Cells that a spreadsheet would read as a formula get a leading apostrophe.'), ta, out);
    this.modal({ title: I18N.f('Recipes "{0}": import / export', b.name), body, buttons: [
      { label: 'Copy', onclick: () => { ta.select(); try { document.execCommand('copy'); } catch (_) {} return false; } },
      { label: 'Close' },
      { label: 'Import', primary: true, onclick: () => {
        const r = M.importRecipesCsv(bi, ta.value);
        out.innerHTML = '';
        if (r.errors.length) { out.append(U.el('div', { class: 'warn' }, I18N.t('Nothing was imported:')), U.el('pre', null, r.errors.join('\n'))); return false; }
        this.toast(I18N.f('{0} recipe(s) imported', r.count), 2500);
      } }] });
  },

  // ---- tags to and from a spreadsheet -------------------------------------------
  tagsCsvDialog() {
    const ta = U.el('textarea', { class: 'csv', spellcheck: false, value: M.exportTags() });
    const out = U.el('div');
    const body = U.el('div', null,
      U.el('div', { class: 'hint' }, 'One tag per line: name, type, init, min, max. Copy this into a spreadsheet, or paste lines from one here (commas or tabs) and press Import. A tag that already exists is updated; nothing is deleted.'),
      ta, out);
    this.modal({ title: 'Tags: import / export', body, buttons: [
      { label: 'Copy', onclick: () => { ta.select(); try { document.execCommand('copy'); } catch (_) {} return false; } },
      { label: 'Close' },
      { label: 'Import', primary: true, onclick: () => {
        const r = M.importTags(ta.value);
        out.innerHTML = '';
        if (r.errors.length) { out.append(U.el('div', { class: 'warn' }, I18N.t('Nothing was imported:')), U.el('pre', null, r.errors.join('\n'))); return false; }
        this.toast(I18N.f('{0} tag(s) added, {1} updated', r.added, r.updated), 2500);
      } }] });
  },

  // ---- unsaved work is kept aside, in case the designer is closed or crashes ------
  recKey(path) { return 'hml.recover:' + String(path || '').replace(/\\/g, '/').toLowerCase(); },
  autosave(now) {
    clearTimeout(this._autoTimer);
    const run = () => {
      try {
        const st = M.st, key = this.recKey(st.path);
        if (this._recKey && this._recKey !== key) localStorage.removeItem(this._recKey);
        this._recKey = key;
        if (st.dirty && st.project) localStorage.setItem(key, JSON.stringify({ time: Date.now(), project: st.project, screens: st.screens }));
        else localStorage.removeItem(key);
      } catch (_) { /* no storage, or full: there is just no safety copy */ }
    };
    if (now) run(); else this._autoTimer = setTimeout(run, 1500);
  },
  // called after a project was opened (or at start, without one): offer what was not saved
  offerRecovery() {
    let rec = null;
    const key = this.recKey(M.st.path);
    try { rec = JSON.parse(localStorage.getItem(key) || 'null'); } catch (_) { rec = null; }
    if (!rec || !rec.project || !Array.isArray(rec.screens) || !rec.screens.length) return;
    if (this.ask(I18N.f('Changes that were not saved were found ({0}). Restore them?', new Date(rec.time).toLocaleString()))) {
      const st = M.st;
      st.project = rec.project; st.screens = rec.screens; st.cur = 0; st.sel = []; st.more = []; st.undo = []; st.redo = []; st.dirty = true;
    } else { try { localStorage.removeItem(key); } catch (_) {} }
  },

  // ---- tags: rename with everything that uses the tag, and where it is used ------
  renameTag(i, name) {
    const n = M.renameTag(i, name);
    if (n) this.toast(I18N.f('{0} place(s) that use the tag were changed with it', n), 2500);
  },
  usesDialog(name) {
    const uses = M.tagUses().get(name) || [];
    const body = U.el('div', { class: 'prob-list' });
    let close = null;
    for (const u of uses) {
      body.append(U.el('div', { class: 'item', onclick: () => { close(); this.gotoProblem(u.loc); } },
        U.el('div', null, this.whereText(u.loc)), U.el('div', { class: 'where' }, I18N.label(u.what))));
    }
    close = this.modal({ title: I18N.f('Tag "{0}" is used here ({1})', name, uses.length), body, buttons: [{ label: 'Close' }] });
  },
  // a place in the project, in words: Screen "main" › label "x" / Drivers
  whereText(pr) {
    const scr = pr.screen !== undefined ? M.st.screens[pr.screen] : null;
    if (!scr) return I18N.t({ tags: 'Tags', drivers: 'Drivers', scripts: 'Scripts', logs: 'Logs', users: 'Users', recipes: 'Recipes', texts: 'Texts', project: 'Project' }[pr.view] || '')
      + (pr.view === 'drivers' && pr.index !== undefined ? ' › ' + ((M.st.project.drivers[pr.index] || {}).name || (M.st.project.drivers[pr.index] || {}).type || '')
        : pr.view === 'scripts' && pr.index !== undefined ? ' › ' + ((M.st.project.scripts[pr.index] || {}).name || '') : '');
    let w = scr.data; for (const i of pr.path) w = (w.children || [])[i];
    return I18N.t('Screen') + ' "' + scr.data.name + '"' + (pr.path.length && w ? ' › ' + Tree.label(w, false) : '');
  },

  // ---- the library of parts (kept in the settings of the designer) --------------
  library: [],
  async saveLibrary() { await host.settings({ library: this.library }); },
  async addToLibrary() {
    if (!M.selection().length) { this.toast('Select the widgets to keep first.', 2500); return; }
    const first = M.selected() || {};
    const name = await this.prompt('Name of the part', U.safeName(first.id || first.text || first.type || 'part', 'part'));
    if (!name) return;
    const part = M.libraryPartFromSelection(name);
    if (!part) { this.toast('This cannot be kept as a part.', 2500); return; }
    const i = this.library.findIndex(p => p.name === part.name);
    if (i >= 0) { if (!this.ask(I18N.f('The library has a part "{0}". Replace it?', part.name))) return; this.library[i] = part; }
    else if (this.library.length >= M.LIBRARY_MAX) { this.toast('The library is full.', 2500); return; }
    else this.library.push(part);
    await this.saveLibrary();
    this.toast(I18N.f('Part "{0}" kept in the library ({1} widget(s))', part.name, part.widgets.length), 2500);
    this.doRefresh();
  },
  useLibraryPart(i) {
    const part = this.library[i];
    if (!part || !M.screen()) return;
    Panels.showView('screen');
    const n = M.pastePart(part);
    if (n) this.toast(I18N.f('{0} widget(s) added from "{1}"', n, part.name), 1500);
  },
  async removeLibraryPart(i) {
    const part = this.library[i];
    if (!part || !this.ask(I18N.f('Remove the part "{0}" from the library?', part.name))) return;
    this.library.splice(i, 1);
    await this.saveLibrary();
    this.doRefresh();
  },
  libraryDialog() {
    const ta = U.el('textarea', { id: 'lib-text', rows: 16, spellcheck: false, style: { width: '640px', maxWidth: '80vw' }, value: M.libraryExport(this.library) });
    const out = U.el('div', { id: 'lib-out' });
    const body = U.el('div', null,
      U.el('div', { class: 'hint' }, 'The library as text. Copy it into a file to pass it on; paste the text of a library here and press Import to add its parts (a part with the same name is replaced). It is read as data only.'),
      ta, out);
    this.modal({ title: 'Library: import / export', body, buttons: [
      { label: 'Copy', onclick: () => { ta.select(); try { document.execCommand('copy'); } catch (_) {} return false; } },
      { label: 'Close' },
      { label: 'Import', primary: true, onclick: async () => {
        const r = M.libraryImport(ta.value, this.library);
        out.innerHTML = '';
        if (!r.added && !r.replaced) { out.append(U.el('div', { class: 'warn' }, I18N.t('Nothing was imported:')), U.el('pre', null, (r.errors.length ? r.errors : ['no parts']).join('\n'))); return false; }
        this.library = r.parts;
        await this.saveLibrary();
        this.doRefresh();
        const more = r.errors.concat(r.dropped.map(d => 'left out - ' + d));
        if (more.length) { out.append(U.el('div', { class: 'warn' }, I18N.f('{0} part(s) added, {1} replaced. Not everything was taken:', r.added, r.replaced)), U.el('pre', null, more.slice(0, 50).join('\n'))); return false; }
        this.toast(I18N.f('{0} part(s) added, {1} replaced', r.added, r.replaced), 2500);
      } }] });
  },

  // ---- find and replace: tag names and texts ---------------------------------
  // The list under the fields is what will change; nothing changes before "Replace".
  replaceDialog(preset) {
    if (!M.st.project) return;
    const opt = Object.assign({ what: 'tags', find: '', replace: '', whole: false, matchCase: true, scope: 'screen', createTags: false, definitions: false }, preset);
    const sel = (id, options, v) => { const s = U.el('select', { id }); for (const [val, text] of options) s.append(U.el('option', { value: val, selected: val === v }, I18N.t(text))); return s; };
    const chk = (id, text, v) => { const box = U.el('input', { type: 'checkbox', id, checked: !!v }); const l = U.el('label', { class: 'rep-chk' }, box, ' ' + I18N.t(text)); l.box = box; return l; };
    const what = sel('rep-what', [['tags', 'Tag names'], ['texts', 'Texts']], opt.what);
    const scope = sel('rep-scope', [['screen', 'This screen'], ['screens', 'All screens'], ['project', 'The whole project']], opt.scope);
    const find = U.el('input', { type: 'text', id: 'rep-find', value: opt.find, placeholder: 'Find' });
    const repl = U.el('input', { type: 'text', id: 'rep-with', value: opt.replace, placeholder: 'Replace with' });
    const whole = chk('rep-whole', 'Only the whole name / text', opt.whole), mcase = chk('rep-case', 'Match case', opt.matchCase);
    const create = chk('rep-create', 'Make the tags the new names need (copies of the old ones)', opt.createTags);
    const defs = chk('rep-defs', 'Rename in the tag list too', opt.definitions);
    const count = U.el('div', { id: 'rep-count', class: 'rep-count' }), list = U.el('div', { id: 'rep-list', class: 'prob-list' });
    const read = () => ({ what: what.value, find: find.value, replace: repl.value, scope: scope.value, whole: whole.box.checked, matchCase: mcase.box.checked,
                          createTags: what.value === 'tags' && create.box.checked, definitions: what.value === 'tags' && scope.value === 'project' && defs.box.checked });
    const show = () => {
      const o = read();
      create.hidden = o.what !== 'tags';
      defs.hidden = o.what !== 'tags' || o.scope !== 'project';
      const plan = M.replacePlan(o);
      count.textContent = !o.find ? I18N.t('Type what to find. The places that will change are listed here first.') : I18N.f('{0} place(s) will change', plan.length);
      list.innerHTML = '';
      // (what the user wrote is shown as text, never as markup)
      for (const c of plan.slice(0, 300))
        list.append(U.el('div', { class: 'item rep-item' }, U.el('div', null, U.el('span', { class: 'rep-old' }, c.before), ' → ', U.el('span', { class: 'rep-new' }, c.after)),
          U.el('div', { class: 'where' }, this.whereText(c.loc) + (c.what ? ' · ' + I18N.label(c.what) : ''))));
      if (plan.length > 300) list.append(U.el('div', { class: 'rep-count' }, I18N.f('... and {0} more', plan.length - 300)));
    };
    for (const e of [what, scope, find, repl]) { e.oninput = show; e.onchange = show; }
    for (const e of [whole, mcase, create, defs]) e.box.onchange = show;
    const body = U.el('div', { class: 'replace-dlg' }, U.el('div', { class: 'rep-line' }, what, scope), U.el('div', { class: 'rep-line' }, find, U.el('span', null, '→'), repl),
      U.el('div', { class: 'rep-line' }, whole, mcase), create, defs, count, list);
    this.modal({ title: 'Find and replace', body, buttons: [{ label: 'Close' }, { label: 'Replace', primary: true, onclick: () => {
      const o = read();
      if (!M.replacePlan(o).length) { this.toast('Nothing to replace.', 2000); return false; }
      const r = M.replaceApply(o);
      setTimeout(() => this.replaceResult(r), 0);          // (after this dialog is closed)
    } }] });
    show();
  },
  // What a replacement did. New problems are shown at once, with the way back.
  replaceResult(r) {
    const msg = I18N.f('{0} place(s) changed', r.changed) + (r.created ? ' · ' + I18N.f('{0} tag(s) made', r.created) : '') + (r.skipped ? ' · ' + I18N.f('{0} left as they were', r.skipped) : '');
    if (!r.newProblems.length) { this.toast(msg, 4000); return; }
    const body = U.el('div', null, U.el('div', { class: 'rep-count' }, msg), U.el('div', { class: 'prob-list', id: 'rep-problems' },
      ...r.newProblems.slice(0, 200).map(pr => U.el('div', { class: 'item' }, U.el('div', null, I18N.t(pr.text)), U.el('div', { class: 'where' }, this.whereText(pr))))));
    this.modal({ title: I18N.f('Replaced - but there are {0} new problem(s)', r.newProblems.length), body,
                 buttons: [{ label: 'Undo the replacement', onclick: () => { M.undo(); this.toast('Undone.', 2000); } }, { label: 'Keep it', primary: true }] });
  },
  // The whole project for a display of another size.
  resizeDialog() {
    if (!M.st.project) return;
    const d = M.display();
    const w = U.el('input', { type: 'number', id: 'size-w', value: d.w, min: 64, max: 8192 }), h = U.el('input', { type: 'number', id: 'size-h', value: d.h, min: 64, max: 8192 });
    const body = U.el('div', null, U.el('div', { class: 'rep-line' }, w, U.el('span', null, '×'), h),
      U.el('div', { class: 'hint' }, 'Every screen is scaled: positions, sizes, spacings; fonts become the nearest size there is. Sizes in % stay. Look at the screens afterwards - texts may need another font. One step that can be undone.'));
    this.modal({ title: I18N.f('Convert the project to another display size (now {0} × {1})', d.w, d.h), body, buttons: [{ label: 'Cancel' }, { label: 'Convert', primary: true, onclick: () => {
      if (!M.resizeProject(Number(w.value), Number(h.value))) { this.toast('Give another size (64 to 8192).', 2500); return false; }
      this.toast(I18N.f('{0} screen(s) converted', M.st.screens.length), 3000);
    } }] });
  },

  // ---- problems: what will not work on the device --------------------------
  problems: [],
  problemsDialog() {
    const list = this.problems;
    if (!list.length) return;
    const body = U.el('div', { class: 'prob-list' });
    let close = null;
    for (const pr of list) {
      const where = this.whereText(pr);
      body.append(U.el('div', { class: 'item', onclick: () => { close(); this.gotoProblem(pr); } },
        U.el('div', null, I18N.t(pr.text)), U.el('div', { class: 'where' }, where)));
    }
    close = this.modal({ title: I18N.f('Problems ({0})', list.length), body, buttons: [{ label: 'Close' }] });
  },
  gotoProblem(pr) {
    if (pr.screen !== undefined) {
      Panels.showView('screen');
      M.st.cur = pr.screen; M.st.sel = pr.path; M.st.more = [];
    } else if (pr.view === 'project') {
      Panels.showView('screen'); Panels.show('project');
    } else {
      if (pr.view === 'drivers' && pr.index !== undefined) Panels.driverSel = pr.index;
      if (pr.view === 'scripts' && pr.index !== undefined) Panels.scriptSel = pr.index;
      Panels.showView(pr.view);
    }
    this.doRefresh();
  },
  addWidget(type) { Panels.showView('screen'); M.addWidget(type, M.st.sel); },

  toolbar() {
    const st = M.st, sel = document.getElementById('sel-screen');
    sel.innerHTML = '';
    st.screens.forEach((s, i) => sel.append(U.el('option', { value: i, selected: i === st.cur }, s.data.name)));
    document.getElementById('b-undo').disabled = !st.undo.length;
    document.getElementById('b-redo').disabled = !st.redo.length;
    document.getElementById('zoom-label').textContent = Math.round(st.zoom * 100) + '%';
    const name = !st.path ? '(unsaved)' : st.archive ? U.basename(st.path) : U.basename(U.dirname(st.path)) + '/project.json';
    const title = (st.dirty ? '● ' : '') + (st.project ? st.project.title || '' : '') + '  —  ' + name;
    document.getElementById('title-label').textContent = title;
    host.setTitle('Tsukuraku Designer — ' + title);
    const noRt = !this.info.runtime;
    document.getElementById('b-preview').disabled = noRt; document.getElementById('b-run').disabled = noRt;
  },
  status(msg) {
    const el = document.getElementById('status-text');
    if (msg) { el.textContent = msg; return; }
    const prob = document.getElementById('status-prob'), n = this.problems.length;
    prob.textContent = n ? '⚠ ' + I18N.f('{0} problem(s)', n) : I18N.t('no problems');
    prob.className = n ? '' : 'none';
    prob.title = n ? I18N.t('Show what will not work on the device') : '';
    const w = M.selected(), d = M.display(), many = M.selection().length;
    let s = d.w + '×' + d.h;
    if (many > 1) s += '  |  ' + I18N.f('{0} widgets selected', many);
    else if (w) {
      if (M.st.sel.length) s += '  |  ' + (w.type || 'panel') + ' #' + U.pathStr(M.st.sel) + '  x=' + U.num(w.x, 0) + ' y=' + U.num(w.y, 0) + (w.w !== undefined ? ' w=' + w.w : '') + (w.h !== undefined ? ' h=' + w.h : '') + (w.align ? ' align=' + w.align : '');
      else s += '  |  screen "' + (w.name || '') + '"';
    }
    if (M.st.path) s += '  |  ' + M.st.path;
    el.textContent = s;
  },
  toast(msg, ms) {
    const t = document.getElementById('toast');
    t.textContent = I18N.t(msg); t.hidden = false;
    clearTimeout(this._toast); this._toast = setTimeout(() => { t.hidden = true; }, ms || 3500);
  },

  // ---- modal -------------------------------------------------------------
  modal(opt) {
    const m = document.getElementById('modal');
    document.getElementById('modal-title').textContent = I18N.t(opt.title || '');
    const body = document.getElementById('modal-body'); body.innerHTML = '';
    if (opt.body) body.append(opt.body);
    const btns = document.getElementById('modal-buttons'); btns.innerHTML = '';
    const close = () => { m.hidden = true; };
    for (const b of opt.buttons || [{ label: 'Close' }]) {
      btns.append(U.el('button', { class: b.primary ? 'primary' : '', dataset: { label: b.label }, onclick: async () => { if (!b.onclick || (await b.onclick()) !== false) close(); } }, I18N.t(b.label)));
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
  // a yes / no question, in the chosen language
  ask(question) { return confirm(I18N.t(question)); },
  confirmDiscard() {
    if (!M.st.dirty) return true;
    if (!this.ask('Discard unsaved changes?')) return false;
    M.st.dirty = false; this.autosave(true);                   // thrown away on purpose: no safety copy
    return true;
  },

  newProjectDialog() {
    if (!this.confirmDiscard()) return;
    const title = U.el('input', { type: 'text', value: 'My HMI' });
    const w = U.el('input', { type: 'number', value: 800 }), h = U.el('input', { type: 'number', value: 480 });
    // a preset also brings the touch correction of that display (kept while the size stays as it set it)
    let chosen = null;
    const presets = U.el('div', { class: 'row' }, U.el('label', null, I18N.t('presets')),
      U.el('select', { onchange: e => { chosen = SCHEMA.displayPresets[Number(e.target.value)] || null; if (chosen) { w.value = chosen.width; h.value = chosen.height; } } },
        U.el('option', { value: '' }, I18N.t('(choose to apply)')), ...SCHEMA.displayPresets.map((p, i) => U.el('option', { value: i }, p.name))));
    const body = U.el('div', null, U.el('div', { class: 'row' }, U.el('label', null, I18N.label('title')), title),
      U.el('div', { class: 'row' }, U.el('label', null, I18N.t('width × height')), w, U.el('span', null, '×'), h), presets,
      U.el('div', { class: 'hint' }, 'You will be asked where to save the project: as one file (name.hmlproj, easy to carry around), or as a folder (choose "Project folder" in the dialog: project.json with screens/ and assets/ next to it).'));
    // Japanese (and other non-Latin) text needs a font in the project: offered here, on by default
    // when the designer itself is shown in Japanese
    const jp = U.el('input', { type: 'checkbox', checked: I18N.lang === 'ja' && !!(this.info && this.info.fontsDir), disabled: !(this.info && this.info.fontsDir), dataset: { fk: 'new.font' } });
    body.append(U.el('div', { class: 'row' }, U.el('label', null, 'Japanese font'), jp,
      U.el('span', { class: 'dim' }, 'put Noto Sans JP into the project (about 1.2 MB). Without a font Japanese text is shown as empty boxes')));
    this.modal({ title: 'New project', body, buttons: [{ label: 'Cancel' }, { label: 'Create', primary: true, onclick: async () => {
      M.newProject(title.value.trim() || 'New HMI', Number(w.value) || 800, Number(h.value) || 480);
      if (chosen && chosen.touch && Number(w.value) === chosen.width && Number(h.value) === chosen.height) M.st.project.display.touch = U.clone(chosen.touch);
      this.refresh();
      if ((await this.save(true)) && jp.checked && (await this.addBundledFont())) await this.save(false);
    } }] });
  },

  async openDialog() {
    if (!this.confirmDiscard()) return;
    const p = await host.openDialog('project', M.st.path ? U.dirname(M.st.path) : '');
    if (p) await this.open(p);
  },
  async open(path) {
    const r = await host.readProject(path);
    if (!r || !r.ok) { this.toast('open failed: ' + (r && r.error)); return; }
    try { M.loadFromHost(r); } catch (e) { this.toast('parse error: ' + e.message, 6000); return; }
    await this.loadLocal();
    this.remember(path);
    this.offerRecovery();
    this.refresh();
    await this.takeStamp();
    setTimeout(() => this.zoomFit(), 50);
  },

  // ---- changes made to the project's files from outside -------------------------------
  stamp: null, checking: null,
  async readStamp() {
    const st = M.st;
    if (!st.path || !st.dir) return null;
    const r = await host.projectStamp(st.dir, st.screens.map(s => s.file));
    return r && r.ok ? r.stamp : null;
  },
  async takeStamp() { this.stamp = await this.readStamp(); },
  // (one check at a time: a second caller gets the one that is running)
  checkOutside() {
    if (!this.checking) this.checking = this.checkOutsideNow().finally(() => { this.checking = null; });
    return this.checking;
  },
  async checkOutsideNow() {
    if (this.stamp === null || !M.st.path || !document.getElementById('modal').hidden) return;
    {
      const now = await this.readStamp();
      if (now === null || now === this.stamp) return;
      this.stamp = now;                                   // asked once per change
      if (!M.st.dirty) { await this.reloadFromDisk(); return; }
      this.modal({ title: 'The project was changed outside the designer',
        body: U.el('div', { class: 'hint' }, 'Its files were changed by another program, and there are changes here that are not saved. Loading the files discards the changes made here; keeping them means the next Save overwrites the files.'),
        buttons: [{ label: 'Keep my changes' }, { label: 'Load the files', primary: true, onclick: () => { this.reloadFromDisk(); } }] });
    }
  },
  // Read the files again, staying on the same screen. A project file (.hmlproj) is read from its
  // working folder and left unsaved: Save puts the outside changes into the file.
  async reloadFromDisk() {
    const st = M.st, cur = st.screens[st.cur] && st.screens[st.cur].data.name, view = Panels.view, wasArchive = st.archive;
    const r = await host.readProject(st.path, true);
    if (!r || !r.ok) { this.toast('reload failed: ' + (r && r.error), 6000); return; }
    try { M.loadFromHost(r); } catch (e) { this.toast('The files were changed, but cannot be read: ' + e.message, 8000); return; }
    const i = M.st.screens.findIndex(s => s.data.name === cur);
    if (i >= 0) M.st.cur = i;
    if (wasArchive) M.st.dirty = true;
    await this.loadLocal();
    this.refresh();
    if (view && Panels.showView) Panels.showView(view);
    await this.takeStamp();
    this.toast('The project was changed outside the designer: loaded again', 4000);
  },

  // Put the Japanese font that comes with the designer into the project (assets/fonts/) and
  // make it the project's font. The project must have a folder: it is saved first.
  async addBundledFont() {
    const st = M.st;
    if (!st.project) return false;
    if (!st.path && !(await this.save(false))) return false;
    const r = await host.addFont(st.dir);
    if (!r || !r.ok) { this.toast('failed: ' + (r && r.error), 6000); return false; }
    M.mutate(() => { st.project.font = Object.assign({}, st.project.font, { file: r.rel }); });
    this.toast(I18N.f('font added: {0}', r.rel), 4000);
    return true;
  },

  // Put the preset icons that come with the designer into the project (assets/icons/<size>/<name>.png).
  async addBundledIcons() {
    const st = M.st;
    if (!st.project) return false;
    if (!st.path && !(await this.save(false))) return false;
    const r = await host.addIcons(st.dir);
    if (!r || !r.ok) { this.toast('failed: ' + (r && r.error), 6000); return false; }
    this.toast(I18N.f('icons added: {0}', r.rel + '/<size>/<name>.png'), 4000);
    return true;
  },

  // ---- "AI": what an AI assistant needs to make screens for this project ---------------
  aiPrompt() {
    const st = M.st, i = this.info || {};
    const ja = I18N.lang === 'ja';
    const skill = i.skillDir ? i.skillDir.replace(/\\/g, '/') + '/SKILL.md' : '(skill folder not found)';
    const lines = ja ? [
      'HML（LVGL HMI）の画面を作ってください。',
      'まず次の手引きを読み、それに従ってください: ' + skill,
      'プロジェクトのフォルダー: ' + (st.dir || '（プロジェクトを保存してから、もう一度このダイアログを開いてください）'),
      'ランタイム: ' + (i.runtime || '(hml_runtime が見つかりません)'),
      '日本語フォント（プロジェクトにないときに入れるもの）: ' + (i.fontsDir || '(見つかりません)'),
      'アイコン（プロジェクトにないときに入れるもの）: ' + (i.iconsDir || '(見つかりません)'),
      '',
      '作りたい画面: （ここに書く。例: 上に今のアラーム、下に温度のトレンド、右に今の値を大きく。メイン画面から行けるように）',
    ] : [
      'Please make a screen for an HML (LVGL HMI) project.',
      'First read this guide and follow it: ' + skill,
      'Project folder: ' + (st.dir || '(save the project first, then open this dialog again)'),
      'Runtime: ' + (i.runtime || '(hml_runtime not found)'),
      'Japanese font (to put into the project when it has none): ' + (i.fontsDir || '(not found)'),
      'Icons (to put into the project when it has none): ' + (i.iconsDir || '(not found)'),
      '',
      'The screen I want: (write it here, e.g. current alarms on top, a temperature trend below, large values on the right; reachable from the main screen)',
    ];
    return lines.join('\n');
  },
  aiDialog() {
    const i = this.info || {}, st = M.st;
    const text = U.el('textarea', { rows: 8, class: 'mono', style: { width: '100%' }, spellcheck: false, value: this.aiPrompt(), dataset: { fk: 'ai.prompt' } });
    const body = U.el('div', { class: 'deploy' },
      U.el('div', { class: 'hint' }, 'An AI assistant that can read and write files on this PC (Claude Code, the Claude desktop app ...) can make screens for this project. Give it the text below: it names the guide (skill) that comes with the designer, the project folder and the runtime it checks its work with. The runtime check opens no window and contacts no device.'),
      text,
      U.el('div', { class: 'row' },
        U.el('button', { class: 'mini', onclick: async () => { text.select(); try { await navigator.clipboard.writeText(text.value); } catch (e) { document.execCommand('copy'); } this.toast('copied', 1200); } }, 'Copy'),
        U.el('button', { class: 'mini', disabled: !i.skillDir, onclick: () => host.openFolder(i.skillDir) }, 'Open the skill folder'),
        U.el('button', { class: 'mini', disabled: !i.skillDir, onclick: async () => { const r = await host.installSkill(); this.toast(r && r.ok ? I18N.f('installed in {0}', r.path) : 'failed: ' + (r && r.error), 5000); } }, 'Install the skill for Claude Code')),
      U.el('div', { class: 'hint' }, st.dirty ? 'Save first: the assistant reads the files, not what is open here.' : 'While the assistant works, keep the designer open: when it changes the files, the designer loads them again (it asks first if there are unsaved changes here).'),
      st.archive ? U.el('div', { class: 'hint' }, 'This project is one file (.hmlproj): the assistant works in its working folder shown above. Press Save afterwards to put its changes into the file.') : null);
    this.modal({ title: 'Make screens with an AI assistant', body, buttons: [{ label: 'Close' }] });
  },
  // ---- recently used projects (kept in the designer's settings on this PC)
  recent: [],
  remember(path) {
    if (!path) return;
    const same = p => p.replace(/\\/g, '/').toLowerCase() === path.replace(/\\/g, '/').toLowerCase();
    this.recent = [path].concat(this.recent.filter(p => !same(p))).slice(0, 10);
    host.settings({ recent: this.recent });
  },
  SHORTCUTS: [
    ['Ctrl+N / Ctrl+O / Ctrl+S', 'New / open / save (Ctrl+Shift+S: save as)'], ['Ctrl+Z / Ctrl+Y', 'Undo / redo'],
    ['Ctrl+1 … Ctrl+8', 'Screen editor, tags, drivers, scripts, logs, users, recipes, texts'], ['Ctrl+Tab / Ctrl+Shift+Tab', 'Next / previous screen tab'],
    ['Drag on the background', 'Select the widgets inside the rectangle'], ['Right click', 'Menu: copy, paste, order …'],
    ['Ctrl+click, Shift+click', 'Add a widget to the selection, or take it out'], ['Ctrl+A', 'Select everything in the same panel'],
    ['Ctrl+C / Ctrl+X / Ctrl+V', 'Copy / cut / paste widgets (also onto another screen)'], ['Ctrl+Alt+C / Ctrl+Alt+V', 'Copy / paste the style (look) of a widget'], ['Ctrl+D', 'Duplicate'], ['Del', 'Delete'],
    ['← ↑ → ↓', 'Move by 1 px (with Shift: 10 px)'], ['Alt + drag', 'Move without snapping to other widgets'], ['Esc', 'Clear the selection / close a dialog'],
    ['F1', 'This list'],
  ],
  shortcutsDialog() {
    const tb = U.el('table', { class: 'keys' });
    for (const [k, what] of this.SHORTCUTS) tb.append(U.el('tr', null, U.el('td', { class: 'k' }, k), U.el('td', null, I18N.t(what))));
    this.modal({ title: 'Keyboard shortcuts', body: tb, buttons: [{ label: 'Close' }] });
  },
  recentDialog() {
    const body = U.el('div', { class: 'recent' });
    let close = null;
    if (!this.recent.length) body.append(U.el('div', { class: 'hint' }, 'No project was opened yet.'));
    for (const p of this.recent) {
      body.append(U.el('div', { class: 'item', onclick: async () => { if (!this.confirmDiscard()) return; close(); await this.open(p); } },
        U.el('div', null, U.basename(/project\.json$/i.test(p) ? U.dirname(p) : p)), U.el('div', { class: 'where' }, p)));
    }
    close = this.modal({ title: 'Recent projects', body, buttons: [{ label: 'Close' }] });
  },
  async save(as) {
    const st = M.st;
    if (!st.project) return false;
    if (!st.path || as) {
      // one file by default (<name>.hmlproj); a project that is a folder stays one unless the
      // other form is picked in the dialog
      const suggested = (st.path && !st.archive) ? 'project.json' : (st.path ? U.basename(st.path) : U.safeName(st.project.title, 'project') + '.hmlproj');
      let p = await host.saveDialog(suggested, st.path ? U.dirname(st.path) : '');
      if (!p) return false;
      const from = st.dir;
      let dir, name;
      if (U.isProjectFile(p)) {
        const a = await host.archiveDir(p, true);                 // its working folder on this PC
        if (!a || !a.ok) { this.toast('save failed: ' + (a && a.error), 6000); return false; }
        dir = a.dir; name = a.name;
      } else {
        p = await this.ownFolder(p);
        if (!p) return false;
        dir = U.dirname(p); name = U.basename(dir);
      }
      // the project moves: its images and other files come along
      if (from) {
        const c = await host.copyProjectFiles(from, dir);
        if (!c || !c.ok) { this.toast('save failed: ' + (c && c.error), 6000); return false; }
      }
      st.path = p; st.dir = dir; st.archive = U.isProjectFile(p); st.name = name;
    }
    const r = await host.writeFiles(st.dir, M.filesToWrite());
    if (!r || !r.ok) { this.toast('save failed: ' + (r && r.error), 6000); return false; }
    if (st.archive) {
      const z = await host.packProject(st.dir, st.path);
      if (!z || !z.ok) { this.toast('save failed: ' + (z && z.error), 6000); return false; }
    }
    st.dirty = false;
    if (as) await this.loadLocal();
    this.refresh();
    await this.takeStamp();
    this.toast('saved ' + st.path, 1500);
    this.remember(st.path);
    this.autosave(true);
    return true;
  },
  // A project is its folder (project.json, screens/, assets/), and the folder's name is the
  // project's name on the target. Saving into a folder that already holds other things mixes
  // the two up: offer a folder of its own. Returns the path to save to, or null (cancelled).
  async ownFolder(path) {
    const dir = U.dirname(path);
    const info = await host.folderInfo(dir);
    if (!info || !info.ok || !info.others) return path;
    const base = U.safeName(M.st.project && M.st.project.title, 'hmi-project');
    const name = U.el('input', { type: 'text', value: base, spellcheck: false });
    const body = U.el('div', { class: 'deploy' },
      U.el('div', { class: 'warn' }, 'This folder already contains ' + info.others + ' other item(s): ' + info.names.join(', ') + (info.others > info.names.length ? ', …' : '')),
      U.el('div', { class: 'hint' }, 'A project should have a folder of its own: project.json, screens/ and assets/ are created in it, and its name is the name of the project on the target.'),
      U.el('div', { class: 'row' }, U.el('label', null, 'new folder'), name),
      U.el('div', { class: 'hint' }, 'Letters, digits, . _ - only (it is also used as the folder name on the target).'));
    return new Promise(resolve => {
      let done = false;
      const finish = v => { if (!done) { done = true; resolve(v); } };
      this.modal({ title: 'Save the project in its own folder?', body, buttons: [
        { label: 'Cancel', onclick: () => finish(null) },
        { label: 'Save here anyway', onclick: () => finish(path) },
        { label: 'Create the folder', primary: true, onclick: () => {
          const n = name.value.trim();
          if (!/^[A-Za-z0-9_][A-Za-z0-9_.-]*$/.test(n)) { this.toast('folder name: letters, digits, . _ - (not starting with . or -)'); return false; }
          finish(dir.replace(/[\\/]+$/, '') + '/' + n + '/project.json');
        } }] });
    });
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
    const r = await host.preview(M.runPath(), scr.name);
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
  // ---- this PC's own settings for the project (project.local.json next to it) -----------
  // Things that differ per machine, like the name of a serial port: used when the project runs
  // here (Run / Preview), never deployed. The target has its own such file.
  local: {},
  async loadLocal() {
    this.local = {};
    if (!M.st.path) return;
    const r = await host.localSettings(M.runPath());
    if (r && r.ok && r.settings) this.local = r.settings;
  },
  localDriverSetting(i, name) {
    const d = this.local && this.local.drivers && this.local.drivers[M.driverKey(i)];
    return d && d[name] !== undefined ? d[name] : '';
  },
  async setLocalDriverSetting(i, name, value) {
    if (!M.st.path) { this.toast('save the project first'); return; }
    const next = M.withLocalDriverSetting(this.local, M.driverKey(i), name, value);
    const r = await host.localSettings(M.runPath(), next);
    if (!r || !r.ok) { this.toast('cannot save the local settings: ' + (r && r.error), 6000); return; }
    this.local = r.settings || {};
    this.refresh();
  },
  // Pick a serial port: those of the target (asked over SSH with the Deploy connection settings)
  // go into the project, those of this PC into this PC's local settings.
  async serialPortDialog(i) {
    const d = M.st.project.drivers[i];
    const saved = (await host.settings()) || {};
    const dep = (saved.deploy && M.st.path && saved.deploy[M.st.path]) || null;
    const body = U.el('div', { class: 'deploy' }, U.el('div', { class: 'hint' }, 'Looking for serial ports …'));
    const close = this.modal({ title: 'Serial ports', body, buttons: [{ label: 'Close' }] });
    const r = await host.serialPorts(dep ? Object.assign({}, dep, { project: M.runPath(), name: M.st.name }) : {});
    body.innerHTML = '';
    const list = (ports, onPick) => {
      if (!ports.length) return U.el('div', { class: 'hint' }, 'No serial port was found.');
      const tbl = U.el('table', { class: 'grid' });
      for (const p of ports) {
        const pick = name => U.el('button', { class: 'mini', onclick: () => { onPick(name); close(); } }, name);
        tbl.append(U.el('tr', null, U.el('td', null, pick(p.device)), U.el('td', null, p.description || ''),
          U.el('td', null, p.stable ? pick(p.stable) : '')));
      }
      return tbl;
    };
    body.append(U.el('h4', null, 'On the target' + (r && r.targetName ? ' (' + r.targetName + ')' : '')),
      U.el('div', { class: 'hint' }, 'Goes into the project ("device"): the name the port has where the project will run.'));
    if (r && Array.isArray(r.target)) {
      body.append(list(r.target, name => M.mutate(() => { d.device = name; })));
      if (r.target.some(p => p.stable)) body.append(U.el('div', { class: 'hint' }, 'The long name on the right stays the same when adapters are plugged in another order; /dev/ttyUSB0 may become /dev/ttyUSB1.'));
    }
    else body.append(U.el('div', { class: r && r.targetError ? 'warn' : 'hint' }, r && r.targetError ? 'Cannot ask the target: ' + r.targetError
      : 'The target is not known yet: enter its address once in the Deploy dialog (and deploy), then its ports are listed here.'));
    body.append(U.el('h4', null, 'On this PC'),
      U.el('div', { class: 'hint' }, 'For running the project here (Run / Preview). Stored on this PC only (project.local.json), not deployed.'),
      list((r && r.local) || [], name => this.setLocalDriverSetting(i, 'device', name)));
  },

  // ---- deploy to a target over SSH ----------------------------------------
  // The connection settings are kept per project in the designer's own settings file (they
  // describe this PC and its network, not the project); '*' holds the last ones used.
  async deployDialog() {
    const st = M.st;
    if (!st.project) return;
    const saved = (await host.settings()) || {};
    const all = (saved.deploy && typeof saved.deploy === 'object' && !Array.isArray(saved.deploy)) ? saved.deploy : {};
    const rts = this.info.targetRuntimes || [];
    const found = rts.find(p => /aarch64|arm/i.test(p)) || rts[0] || '';   // a Raspberry Pi build first
    const d = Object.assign({ host: '', user: '', port: 22, key: '', remoteDir: 'hml', runtime: '', sendRuntime: false, restart: true, activate: true },
      all['*'] || {}, (st.path && all[st.path]) || {});
    const text = (v, ph, cls) => U.el('input', { type: 'text', value: v || '', placeholder: ph || '', class: cls || '', spellcheck: false });
    const check = v => U.el('input', { type: 'checkbox', checked: !!v });
    const f = {
      host: text(d.host, '192.168.0.10 or raspberrypi.local'), user: text(d.user, 'pi'),
      port: U.el('input', { type: 'number', value: d.port || 22, min: 1, max: 65535, class: 'short' }),
      key: text(d.key, 'default key (~/.ssh/id_*)'), remoteDir: text(d.remoteDir, 'hml'),
      sendRuntime: check(d.sendRuntime), runtime: text(d.runtime || found, 'hml_runtime built for the target (Linux)'), restart: check(d.restart !== false),
      activate: check(d.activate !== false),
    };
    const row = (label, ...c) => U.el('div', { class: 'row' }, U.el('label', null, label), ...c);
    const browse = (kind, inp) => U.el('button', { class: 'mini', onclick: async () => { const p = await host.openDialog(kind, U.dirname(inp.value || '')); if (p) { inp.value = p; update(); } } }, '…');
    f.clearLocal = check(false);                     // never remembered: it removes settings made on the target
    const dest = U.el('div', { class: 'hint' });
    const out = U.el('div');
    const info = U.el('div', { class: 'check' });   // what the target has now (Check)
    let last = null;                                 // the last check result
    const show = v => (typeof v === 'string' ? JSON.stringify(v) : v === null || v === undefined ? '(not set)' : String(v));
    // The settings saved on the target (system settings screens -> project.local.json) win over
    // project.json there: show them next to what the project says, before anything is sent.
    const renderCheck = () => {
      info.innerHTML = '';
      const r = last;
      if (!r) return;
      if (!r.ok) { info.append(U.el('div', { class: 'warn' }, 'Check failed: ' + (r.error || 'unknown error'))); return; }
      const state = !r.exists ? 'This project has not been deployed to ' + r.target + ' yet.'
        : !r.running ? 'hml_runtime is not running on the target.'
        : r.runsThis ? 'The target is running this project.'
        : 'The target is running another project: ' + (r.runningProject || r.running)
          + (f.activate.checked ? '. This deploy switches it to this project.' : '. "run on target" is not ticked: it will keep running that one.');
      info.append(U.el('div', { class: r.exists && r.running && !r.runsThis && !f.activate.checked ? 'warn' : 'hint' }, state));
      if (!r.exists) return;
      const diff = r.local.filter(x => !x.same), same = r.local.length - diff.length;
      info.append(U.el('h4', null, 'Local settings on the target'));
      if (!r.local.length) info.append(U.el('div', { class: 'hint' }, 'None: every driver setting comes from this project.'));
      else {
        if (diff.length) {
          info.append(U.el('div', { class: f.clearLocal.checked ? 'hint' : 'warn' }, f.clearLocal.checked
            ? 'These will be removed by this deploy; the project\'s values take effect.'
            : 'These were changed on the target and stay in effect after a deploy (the project\'s values are not used):'));
          info.append(U.el('table', { class: f.clearLocal.checked ? 'gone' : '' },
            U.el('tr', null, U.el('th', null, 'driver'), U.el('th', null, 'setting'), U.el('th', null, 'on the target'), U.el('th', null, 'in this project')),
            ...diff.map(x => U.el('tr', null, U.el('td', null, x.driver), U.el('td', null, x.key),
              U.el('td', { class: 'val diff' }, show(x.target)), U.el('td', { class: 'val' }, x.unknown ? '(no such driver)' : show(x.project))))));
        }
        if (same) info.append(U.el('div', { class: 'hint' }, (diff.length ? 'Also ' : '') + same + ' setting(s) saved on the target with the same value as this project'
          + (diff.length ? '.' : ': nothing differs, but they would not follow a change made here.')));
      }
      const fl = r.files, n = fl.changed.length + fl.added.length;
      info.append(U.el('h4', null, 'Files'));
      if (!n && !fl.extra.length) info.append(U.el('div', { class: 'hint' }, 'The target has the same files as this project' + (st.dirty ? ' (as last saved).' : '.')));
      else {
        const list = (title, names) => names.length ? [U.el('div', { class: 'hint' }, title), U.el('ul', null, ...names.slice(0, 12).map(x => U.el('li', null, x)),
          names.length > 12 ? U.el('li', null, '… ' + (names.length - 12) + ' more') : null)] : [];
        info.append(...list('Changed (' + fl.changed.length + '):', fl.changed), ...list('New (' + fl.added.length + '):', fl.added),
          ...list('Only on the target, not removed by a deploy (' + fl.extra.length + '):', fl.extra));
      }
      if (r.localUsers) info.append(U.el('div', { class: f.clearLocal.checked ? 'hint' : 'warn' }, f.clearLocal.checked
        ? 'The ' + r.localUsers + ' user(s) edited on the target will be removed by this deploy; the project\'s users take effect.'
        : 'Users were edited on the target (' + r.localUsers + ' user(s)): they stay in effect after a deploy, the project\'s user list is not used there.'));
      if (st.dirty) info.append(U.el('div', { class: 'hint' }, 'Unsaved changes are not part of this comparison (Deploy saves first).'));
    };
    const runCheck = async () => {
      const v = values();
      if (!v.host || !v.user || !st.path) { last = null; renderCheck(); return; }
      info.innerHTML = ''; info.append(U.el('div', { class: 'hint' }, 'Checking ' + v.user + '@' + v.host + ' …'));
      last = await host.deployCheck(Object.assign({}, v, { project: M.runPath(), name: st.name }));
      renderCheck();
    };
    const name = () => (st.path ? (st.name || U.basename(U.dirname(st.path))) : '(unsaved)');
    const update = () => {
      const dir = (f.remoteDir.value.trim() || 'hml').replace(/\/+$/, '');
      dest.textContent = 'Project → ' + (f.user.value.trim() || 'user') + '@' + (f.host.value.trim() || 'host') + ':' + dir + '/projects/' + name() + '/'
        + (f.sendRuntime.checked ? ',  runtime → ' + dir + '/hml_runtime' : '');
      f.runtime.disabled = !f.sendRuntime.checked;
    };
    for (const k of Object.keys(f)) f[k].addEventListener('input', update);
    const body = U.el('div', { class: 'deploy' },
      row('target (host)', f.host), row('user', f.user), row('port', f.port),
      row('private key', f.key, browse('key', f.key),
        U.el('button', { class: 'mini', title: 'Register this key on the target (the target\'s screen must be in key management mode: System > Keys)', onclick: () => registerKey() }, 'Register…')),
      row('remote folder', f.remoteDir),
      row('send runtime', f.sendRuntime, f.runtime, browse('targetRuntime', f.runtime)),
      row('run on target', f.activate, U.el('span', { class: 'dim' }, 'make this the project the target shows (it keeps doing so after a reboot)')),
      row('restart runtime', f.restart, U.el('span', { class: 'dim' }, 'restart hml_runtime on the target after installing')),
      row('local settings', f.clearLocal, U.el('span', { class: 'dim' }, 'remove the settings changed on the target (project.local.json)')),
      dest,
      U.el('div', { class: 'hint' }, 'Uses ssh / scp with key authentication (no password prompt). logs/ on the target are kept.'),
      out, info);
    f.clearLocal.addEventListener('input', renderCheck);
    f.activate.addEventListener('input', renderCheck);
    update();
    const values = () => ({
      host: f.host.value.trim(), user: f.user.value.trim(), port: Number(f.port.value) || 22, key: f.key.value.trim(),
      remoteDir: f.remoteDir.value.trim() || 'hml', runtime: f.runtime.value.trim(), sendRuntime: f.sendRuntime.checked, restart: f.restart.checked,
      activate: f.activate.checked,
    });
    let busy = false;
    // Register the key on a target that does not know it yet (a new PC, a lost key): the target
    // accepts keys only while its screen is in key management mode, and its operator confirms.
    const registerKey = async () => {
      if (busy) return;
      const v = values();
      out.innerHTML = '';
      if (!v.host) { out.append(U.el('div', { class: 'warn' }, 'Enter the target (host) first.')); return; }
      busy = true;
      const k = await host.keyInfo({ key: v.key });
      if (!k || !k.ok) { busy = false; out.append(U.el('div', { class: 'warn' }, 'Register key: ' + ((k && k.error) || 'failed'))); return; }
      if (k.key && k.key !== v.key) { f.key.value = k.key; update(); }
      const keyPort = Number(st.project && st.project.key_management && st.project.key_management.port) || 4855;
      if (k.created) out.append(U.el('div', { class: 'hint' }, 'A new key was created: ' + k.key));
      out.append(
        U.el('div', { class: 'hint' }, 'Sending the key to ' + v.host + ' (port ' + keyPort + '). On the target\'s screen open System > Keys, turn Key management on, and accept the key if it shows this fingerprint:'),
        U.el('pre', null, k.fingerprint),
        U.el('div', { class: 'hint' }, 'Waiting for the answer on the target …'));
      const r = await host.keySend({ host: v.host, keyPort, pub: k.pub, fingerprint: k.fingerprint });
      busy = false;
      out.lastChild.remove();
      if (r && r.ok) out.append(U.el('div', { class: 'ok' }, r.result === 'already' ? 'The target already has this key.' : 'The key was accepted on the target.'));
      else out.append(U.el('div', { class: 'warn' }, 'Register key: ' + ((r && r.error) || 'failed')));
      if (r && r.ok) runCheck();
    };
    runCheck();                                      // with remembered settings: look at the target right away
    this.modal({ title: 'Deploy to target', body, buttons: [{ label: 'Close', onclick: () => !busy },
      { label: 'Check', onclick: async () => { if (!busy) { busy = true; out.innerHTML = ''; await runCheck(); busy = false; } return false; } },
      { label: 'Deploy', primary: true, onclick: async () => {
      if (busy) return false;
      const v = values();
      out.innerHTML = '';
      if (!v.host || !v.user) { out.append(U.el('div', { class: 'warn' }, 'Enter the target (host) and the user.')); return false; }
      if (!(await this.save(false))) return false;
      update();
      all['*'] = v; all[st.path] = v;
      host.settings({ deploy: all });
      busy = true;
      info.innerHTML = '';
      out.append(U.el('div', { class: 'hint' }, 'Deploying to ' + v.user + '@' + v.host + ' …'));
      const r = await host.deploy(Object.assign({}, v, { project: M.runPath(), name: st.name, runtime: v.sendRuntime ? v.runtime : '', clearLocal: f.clearLocal.checked }));
      if (r && r.ok) { f.clearLocal.checked = false; await runCheck(); }   // what the target has now
      busy = false;
      out.innerHTML = '';
      if (r && r.ok) out.append(U.el('div', { class: r.warning ? 'warn' : 'ok' }, r.warning ? 'Deployed, but: ' + r.warning : 'Deployed.'));
      else out.append(U.el('div', { class: 'warn' }, 'Deploy failed: ' + ((r && r.error) || 'unknown error')));
      if (r && r.log) out.append(U.el('pre', null, r.log));
      return false;
    } }] });
  },
  async run() {
    if (!(await this.save(false))) return;
    const r = await host.launch(M.runPath());       // (a runtime started before is closed first)
    if (!r || !r.ok) { this.toast('run failed: ' + (r && r.error)); return; }
    this.runLogStart(r.run);
  },

  // ---- log of the runtime that Run started --------------------------------
  // The host keeps what the runtime prints; the page asks for the new lines while it runs.
  runLog: { run: 0, next: 0, running: false, code: 0, timer: 0, busy: false },
  runLogStart(run) {
    const L = this.runLog;
    L.run = run; L.next = 0; L.running = true; L.code = 0;
    document.getElementById('runlog-text').textContent = '';
    this.runLogShow(true);
    this.runLogState();
    clearInterval(L.timer);
    L.timer = setInterval(() => this.runLogPoll(), 300);
  },
  runLogShow(on) {
    document.getElementById('runlog').hidden = !on;
    if (on) { const t = document.getElementById('runlog-text'); t.scrollTop = t.scrollHeight; }
  },
  async runLogPoll() {
    const L = this.runLog;
    if (L.busy) return;
    L.busy = true;
    const r = await host.runLog(L.next);
    L.busy = false;
    if (!r || !r.ok || r.run !== L.run) return;      // (another Run started meanwhile)
    if (r.text) this.runLogAppend(r.text);
    L.next = r.next;
    if (L.running !== r.running || L.code !== r.code) { L.running = r.running; L.code = r.code; this.runLogState(); }
    if (!r.running) { clearInterval(L.timer); L.timer = 0; }
  },
  runLogAppend(text) {
    const box = document.getElementById('runlog-text');
    const atEnd = box.scrollTop + box.clientHeight >= box.scrollHeight - 4;
    const frag = document.createDocumentFragment();
    for (const raw of text.replace(/\r/g, '').replace(/\n$/, '').split('\n')) {
      const line = raw.replace(/\x1b\[[0-9;]*m/g, '');                       // (colours of the OPC UA library)
      const cls = /\bERROR\b|\[Error\]|\berror\//.test(line) ? 'err' : /\bWARN\b|\[Warn\]|\bwarn\//.test(line) ? 'warn' : '';
      const d = document.createElement('div');
      if (cls) d.className = cls;
      d.textContent = line || ' ';
      frag.append(d);
    }
    box.append(frag);
    while (box.childElementCount > 5000) box.firstChild.remove();
    if (atEnd) box.scrollTop = box.scrollHeight;
  },
  runLogState() {
    const L = this.runLog, st = document.getElementById('runlog-state'), sb = document.getElementById('status-run');
    if (!st || !sb) return;
    const text = !L.run ? '' : L.running ? I18N.t('running') : I18N.f('ended (exit code {0})', L.code);
    st.textContent = text;
    sb.hidden = !L.run;
    sb.textContent = (L.running ? '▶ ' : '■ ') + I18N.t('Runtime') + ': ' + text;
    sb.className = L.running ? 'on' : 'dim';
    sb.title = I18N.t('Show or hide the log of the runtime');
    document.getElementById('runlog-stop').disabled = !L.running;
  },
  async runStop() {
    await host.runStop();
    this.runLogPoll();
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
    if (e.key === 'Escape' && document.getElementById('ctx-menu')) { this.closeMenu(); return; }
    if (ctrl && (e.key === 'Tab' || e.key === 'PageDown' || e.key === 'PageUp') && Panels.view === 'screen') {
      e.preventDefault(); this.nextTab(e.key === 'PageUp' || (e.key === 'Tab' && e.shiftKey) ? -1 : 1); return;
    }
    if (e.key === 'F1') { e.preventDefault(); this.shortcutsDialog(); return; }
    if (ctrl && e.key.toLowerCase() === 'n') { e.preventDefault(); this.newProjectDialog(); return; }
    if (ctrl && /^[12345678]$/.test(e.key)) { e.preventDefault(); Panels.showView(['screen', 'tags', 'drivers', 'scripts', 'logs', 'users', 'recipes', 'texts'][Number(e.key) - 1]); return; }
    if (ctrl && e.key.toLowerCase() === 'z' && !inInput) { e.preventDefault(); if (e.shiftKey) M.redo(); else M.undo(); return; }
    if (ctrl && e.key.toLowerCase() === 'y' && !inInput) { e.preventDefault(); M.redo(); return; }
    if (!document.getElementById('modal').hidden) { if (e.key === 'Escape') document.getElementById('modal').hidden = true; return; }
    if (inInput || Panels.view !== 'screen') return;   // widget shortcuts only act on the canvas
    const sel = M.st.sel, key = e.key.toLowerCase();
    if (ctrl && e.altKey && key === 'c') { e.preventDefault(); this.copyStyle(); return; }
    if (ctrl && e.altKey && key === 'v') { e.preventDefault(); if (this.styleClip) M.pasteStyle(this.styleClip); return; }
    if (ctrl && key === 'd') { e.preventDefault(); M.duplicateSelection(); return; }
    if (ctrl && key === 'a') { e.preventDefault(); M.selectSiblings(); this.selectionChanged(); return; }
    if (ctrl && (key === 'c' || key === 'x')) {
      if (String(window.getSelection() || '')) return;            // text is marked somewhere: the normal copy
      e.preventDefault(); if (key === 'c') this.copy(); else this.cut(); return;
    }
    if (ctrl && key === 'v') { e.preventDefault(); this.paste(); return; }
    if (e.key === 'Delete' || e.key === 'Backspace') { if (sel && sel.length) { e.preventDefault(); M.deleteSelection(); } return; }
    if (e.key === 'Escape') { this.select(null); return; }
    if (/^Arrow/.test(e.key) && sel && sel.length) {
      e.preventDefault();
      const step = e.shiftKey ? 10 : 1;
      M.nudgeSelection(e.key === 'ArrowLeft' ? -step : e.key === 'ArrowRight' ? step : 0, e.key === 'ArrowUp' ? -step : e.key === 'ArrowDown' ? step : 0);
    }
  },
};

window.addEventListener('DOMContentLoaded', () => App.init());
