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
  deploy: o => host.call('hml_deploy', o),
  deployCheck: o => host.call('hml_deployCheck', o),
  serialPorts: o => host.call('hml_serialPorts', o || {}),
  folderInfo: dir => host.call('hml_folderInfo', dir || ''),
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
    q('b-deploy').onclick = () => this.deployDialog();
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
    await this.loadLocal();
    this.refresh();
    setTimeout(() => this.zoomFit(), 50);
  },
  async save(as) {
    const st = M.st;
    if (!st.project) return false;
    if (!st.path || as) {
      let p = await host.saveDialog('project.json', st.dir || '');
      if (!p) return false;
      p = await this.ownFolder(p);
      if (!p) return false;
      st.path = p; st.dir = U.dirname(p);
    }
    const r = await host.writeFiles(st.dir, M.filesToWrite());
    if (!r || !r.ok) { this.toast('save failed: ' + (r && r.error), 6000); return false; }
    st.dirty = false;
    if (as) await this.loadLocal();
    this.refresh();
    this.toast('saved ' + st.path, 1500);
    return true;
  },
  // A project is its folder (project.json, screens/, assets/), and the folder's name is the
  // project's name on the target. Saving into a folder that already holds other things mixes
  // the two up: offer a folder of its own. Returns the path to save to, or null (cancelled).
  async ownFolder(path) {
    const dir = U.dirname(path);
    const info = await host.folderInfo(dir);
    if (!info || !info.ok || !info.others) return path;
    const base = (String((M.st.project && M.st.project.title) || '').replace(/[^A-Za-z0-9_.-]+/g, '-').replace(/^[-.]+|[-.]+$/g, '') || 'hmi-project').toLowerCase();
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
  // ---- this PC's own settings for the project (project.local.json next to it) -----------
  // Things that differ per machine, like the name of a serial port: used when the project runs
  // here (Run / Preview), never deployed. The target has its own such file.
  local: {},
  async loadLocal() {
    this.local = {};
    if (!M.st.path) return;
    const r = await host.localSettings(M.st.path);
    if (r && r.ok && r.settings) this.local = r.settings;
  },
  localDriverSetting(i, name) {
    const d = this.local && this.local.drivers && this.local.drivers[M.driverKey(i)];
    return d && d[name] !== undefined ? d[name] : '';
  },
  async setLocalDriverSetting(i, name, value) {
    if (!M.st.path) { this.toast('save the project first'); return; }
    const next = M.withLocalDriverSetting(this.local, M.driverKey(i), name, value);
    const r = await host.localSettings(M.st.path, next);
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
    const r = await host.serialPorts(dep ? Object.assign({}, dep, { project: M.st.path }) : {});
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
      last = await host.deployCheck(Object.assign({}, v, { project: st.path }));
      renderCheck();
    };
    const name = () => (st.path ? U.basename(U.dirname(st.path)) : '(unsaved)');
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
      const r = await host.deploy(Object.assign({}, v, { project: st.path, runtime: v.sendRuntime ? v.runtime : '', clearLocal: f.clearLocal.checked }));
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
    if (ctrl && /^[123456]$/.test(e.key)) { e.preventDefault(); Panels.showView(['screen', 'tags', 'drivers', 'scripts', 'logs', 'users'][Number(e.key) - 1]); return; }
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
