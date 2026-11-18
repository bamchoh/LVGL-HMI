// Project model: project.json + screens, selection, undo/redo. Global `M`.
'use strict';

const M = {
  st: { path: null, dir: null, project: null, screens: [], cur: 0, sel: null, dirty: false, zoom: 1, undo: [], redo: [] },

  blankProject(title, w, h) {
    return {
      title: title || 'New HMI',
      display: { width: w || 800, height: h || 480 },
      theme: { dark: true, primary: '#2196f3', secondary: '#ff9800', font: 16 },
      start_screen: 'main',
      tags: [],
      drivers: [],
      screens: ['screens/main.json'],
    };
  },
  blankScreen(name) {
    return { name, scrollable: false, style: { pad: 0 }, children: [] };
  },

  newProject(title, w, h) {
    const st = this.st;
    st.path = null; st.dir = null;
    st.project = this.blankProject(title, w, h);
    st.screens = [{ file: 'screens/main.json', data: this.blankScreen('main') }];
    st.cur = 0; st.sel = []; st.dirty = true; st.undo = []; st.redo = [];
  },

  // r = { path, dir, project: text, screens: [{ file, text }] } from the host
  loadFromHost(r) {
    const st = this.st;
    const project = U.parseLooseJson(r.project);
    if (!Array.isArray(project.screens)) project.screens = [];
    const screens = [];
    for (const s of r.screens) {
      let data;
      try { data = s.text ? U.parseLooseJson(s.text) : null; } catch (e) { throw new Error(s.file + ': ' + e.message); }
      if (!data || typeof data !== 'object') data = this.blankScreen(U.basename(s.file).replace(/\.json$/i, ''));
      if (!data.name) data.name = U.basename(s.file).replace(/\.json$/i, '');
      if (!Array.isArray(data.children)) data.children = [];
      screens.push({ file: s.file, data });
    }
    st.path = r.path; st.dir = r.dir; st.project = project; st.screens = screens;
    st.cur = 0; st.sel = []; st.dirty = false; st.undo = []; st.redo = [];
    const start = screens.findIndex(s => s.data.name === project.start_screen);
    if (start >= 0) st.cur = start;
  },

  filesToWrite() {
    const st = this.st;
    st.project.screens = st.screens.map(s => s.file);
    const files = [{ rel: 'project.json', text: U.toJson(st.project) }];
    for (const s of st.screens) files.push({ rel: s.file, text: U.toJson(s.data) });
    return files;
  },

  screen() { const s = this.st.screens[this.st.cur]; return s ? s.data : null; },
  screenNames() { return this.st.screens.map(s => s.data.name); },
  scriptNames() { return ((this.st.project && this.st.project.scripts) || []).map(s => s && s.name).filter(Boolean); },
  tagNames() { return ((this.st.project && this.st.project.tags) || []).map(t => t.name).filter(Boolean); },
  display() { const d = (this.st.project && this.st.project.display) || {}; return { w: U.num(d.width, 800), h: U.num(d.height, 480) }; },

  get(path) {
    let o = this.screen();
    if (!o || !path) return null;
    for (const i of path) { o = (o.children || [])[i]; if (!o) return null; }
    return o;
  },
  parent(path) { return path && path.length ? this.get(path.slice(0, -1)) : null; },
  selected() { return this.st.sel ? this.get(this.st.sel) : null; },
  isContainer(w) { return !!w && (w.type === 'panel' || w.type === 'container' || w.type === 'obj' || w.type === undefined); },

  // ---- undo / redo -------------------------------------------------------
  snapshot() { const st = this.st; return JSON.stringify({ p: st.project, s: st.screens, c: st.cur, sel: st.sel }); },
  restore(snap) {
    const o = JSON.parse(snap), st = this.st;
    st.project = o.p; st.screens = o.s; st.cur = Math.min(o.c, st.screens.length - 1); st.sel = o.sel;
  },
  pushUndo() { const st = this.st; st.undo.push(this.snapshot()); if (st.undo.length > 100) st.undo.shift(); st.redo = []; },
  mutate(fn) { this.pushUndo(); const r = fn(); this.st.dirty = true; App.refresh(); return r; },
  undo() { const st = this.st; if (!st.undo.length) return; st.redo.push(this.snapshot()); this.restore(st.undo.pop()); st.dirty = true; App.refresh(); },
  redo() { const st = this.st; if (!st.redo.length) return; st.undo.push(this.snapshot()); this.restore(st.redo.pop()); st.dirty = true; App.refresh(); },

  // ---- widgets -----------------------------------------------------------
  newWidget(type) { const t = SCHEMA.types[type] || {}; return Object.assign({ type }, U.clone(t.def) || {}); },

  // container that receives new widgets for the current selection
  containerPathFor(path) {
    if (!path) return [];
    if (!path.length) return [];
    const w = this.get(path);
    return this.isContainer(w) ? path : path.slice(0, -1);
  },

  addWidget(type, atPath) {
    return this.mutate(() => {
      const cp = this.containerPathFor(atPath);
      const parent = this.get(cp);
      parent.children = parent.children || [];
      const w = this.newWidget(type);
      if (!parent.flex && w.align === undefined && w.x === undefined) {
        const n = parent.children.length;
        w.x = 10 + (n % 6) * 12; w.y = 10 + (n % 6) * 12;
      }
      parent.children.push(w);
      this.st.sel = cp.concat(parent.children.length - 1);
      return w;
    });
  },

  deleteWidget(path) {
    if (!path || !path.length) return;
    this.mutate(() => {
      const parent = this.parent(path);
      parent.children.splice(path[path.length - 1], 1);
      this.st.sel = path.slice(0, -1);
    });
  },

  duplicateWidget(path) {
    if (!path || !path.length) return;
    this.mutate(() => {
      const parent = this.parent(path), idx = path[path.length - 1];
      const copy = U.clone(parent.children[idx]);
      if (typeof copy.x === 'number') copy.x += 10;
      if (typeof copy.y === 'number') copy.y += 10;
      parent.children.splice(idx + 1, 0, copy);
      this.st.sel = path.slice(0, -1).concat(idx + 1);
    });
  },

  moveWidget(path, delta) {
    if (!path || !path.length) return;
    const parent = this.parent(path), idx = path[path.length - 1], to = idx + delta;
    if (to < 0 || to >= parent.children.length) return;
    this.mutate(() => {
      const [w] = parent.children.splice(idx, 1);
      parent.children.splice(to, 0, w);
      this.st.sel = path.slice(0, -1).concat(to);
    });
  },

  // move the widget into the previous sibling (if it is a container) or out to the grandparent
  reparentWidget(path, dir) {
    if (!path || !path.length) return;
    const parent = this.parent(path), idx = path[path.length - 1];
    if (dir === 'in') {
      const target = parent.children[idx - 1];
      if (!this.isContainer(target)) return;
      this.mutate(() => {
        const [w] = parent.children.splice(idx, 1);
        target.children = target.children || [];
        target.children.push(w);
        this.st.sel = path.slice(0, -1).concat(idx - 1, target.children.length - 1);
      });
    } else {
      if (path.length < 2) return;
      const gp = this.get(path.slice(0, -2)), pidx = path[path.length - 2];
      this.mutate(() => {
        const [w] = parent.children.splice(idx, 1);
        gp.children.splice(pidx + 1, 0, w);
        this.st.sel = path.slice(0, -2).concat(pidx + 1);
      });
    }
  },

  // ---- screens -----------------------------------------------------------
  addScreen(name) {
    name = String(name || '').trim().replace(/[^A-Za-z0-9_\-]/g, '_');
    if (!name || this.screenNames().includes(name)) return false;
    this.mutate(() => {
      this.st.screens.push({ file: 'screens/' + name + '.json', data: this.blankScreen(name) });
      this.st.cur = this.st.screens.length - 1;
      this.st.sel = [];
    });
    return true;
  },
  removeScreen(idx) {
    if (this.st.screens.length <= 1) return;
    this.mutate(() => {
      this.st.screens.splice(idx, 1);
      this.st.cur = Math.max(0, Math.min(this.st.cur, this.st.screens.length - 1));
      this.st.sel = [];
    });
  },
  renameScreen(idx, name) {
    name = String(name || '').trim();
    if (!name) return;
    this.mutate(() => {
      const s = this.st.screens[idx], old = s.data.name;
      s.data.name = name;
      if (this.st.project.start_screen === old) this.st.project.start_screen = name;
      // keep goto actions and screen_show / screen_hide scripts pointing at the renamed screen
      this.eachAction(a => { if (a.action === 'goto' && a.screen === old) a.screen = name; });
      for (const sc of this.st.project.scripts || []) if (sc && sc.screen === old) sc.screen = name;
    });
  },
  // every action object of every widget event on every screen
  eachAction(fn) {
    const walk = w => {
      for (const ev of SCHEMA.events) {
        const acts = w[ev]; if (!acts) continue;
        for (const a of Array.isArray(acts) ? acts : [acts]) if (a && typeof a === 'object') fn(a);
      }
      (w.children || []).forEach(walk);
    };
    this.st.screens.forEach(sc => walk(sc.data));
  },
  selectScreen(idx) { this.st.cur = idx; this.st.sel = []; App.refresh(); },

  // ---- tags / drivers ----------------------------------------------------
  addTag() {
    this.mutate(() => {
      const tags = this.st.project.tags = this.st.project.tags || [];
      let n = tags.length + 1, name;
      do { name = 'tag' + n++; } while (tags.some(t => t.name === name));
      tags.push({ name, type: 'int', init: 0 });
    });
  },
  removeTag(i) { this.mutate(() => this.st.project.tags.splice(i, 1)); },
  addDriver(type) {
    this.mutate(() => {
      const d = this.st.project.drivers = this.st.project.drivers || [];
      d.push(U.clone(SCHEMA.driverDefaults[type]) || { type });
    });
  },
  removeDriver(i) { this.mutate(() => this.st.project.drivers.splice(i, 1)); },
  // How project.local.json names a driver: its "name", else its "type"; "#2", "#3" for repeats
  // (the same rule as driver_keys() in the runtime).
  driverKey(i) {
    const d = (this.st.project && this.st.project.drivers) || [], seen = {};
    let key = '';
    for (let k = 0; k <= i && k < d.length; k++) {
      const base = (d[k] && (typeof d[k].name === 'string' ? d[k].name : typeof d[k].type === 'string' ? d[k].type : '')) || 'driver';
      seen[base] = (seen[base] || 0) + 1;
      key = seen[base] > 1 ? base + '#' + seen[base] : base;
    }
    return key;
  },
  // A copy of local settings (project.local.json) with one driver setting set, or removed when
  // value is empty; objects that become empty are dropped.
  withLocalDriverSetting(local, driverKey, name, value) {
    const out = U.clone(local && typeof local === 'object' ? local : {}) || {};
    const drivers = (out.drivers && typeof out.drivers === 'object') ? out.drivers : (out.drivers = {});
    const d = (drivers[driverKey] && typeof drivers[driverKey] === 'object') ? drivers[driverKey] : (drivers[driverKey] = {});
    if (value === undefined || value === null || value === '') delete d[name]; else d[name] = value;
    if (!Object.keys(d).length) delete drivers[driverKey];
    if (!Object.keys(drivers).length) delete out.drivers;
    return out;
  },

  // ---- users ("users" in project.json): levels, users with a PIN, login behaviour -------
  usersOn() { return this.logOn('users'); },
  setUsers(on) {
    this.mutate(() => {
      const p = this.st.project;
      if (on) {
        if (!p.users || typeof p.users !== 'object') p.users = { levels: ['Operator', 'Maintenance', 'Admin'], list: [] };
        delete p.users.enabled;
      }
      else if (p.users && ((p.users.list || []).length || Object.keys(p.users).some(k => !['enabled', 'levels', 'list'].includes(k)))) p.users.enabled = false;
      else delete p.users;
    });
  },
  userLevels() { const u = this.st.project && this.st.project.users; return (u && Array.isArray(u.levels) && u.levels.length) ? u.levels : ['Operator', 'Maintenance', 'Admin']; },
  validPin(pin) { return /^[0-9]{4,8}$/.test(String(pin)); },
  // "<salt>$<sha256(salt:pin)>": what the runtime reads as "pin_hash"
  hashPin(pin, salt) {
    if (!salt) { salt = ''; for (let i = 0; i < 16; i++) salt += Math.floor(Math.random() * 16).toString(16); }
    return salt + '$' + U.sha256hex(salt + ':' + pin);
  },
  addUser() {
    this.mutate(() => {
      const u = this.st.project.users;
      if (!Array.isArray(u.list)) u.list = [];
      const names = u.list.map(x => x.name);
      let n = 1; while (names.includes('user' + n)) n++;
      u.list.push({ name: 'user' + n, level: 1 });
    });
  },
  setUserPin(i, pin) {
    if (!this.validPin(pin)) return false;
    this.mutate(() => { const x = this.st.project.users.list[i]; x.pin_hash = this.hashPin(pin); delete x.pin; });
    return true;
  },
  removeUser(i) { this.mutate(() => this.st.project.users.list.splice(i, 1)); },
  // problems worth a warning on the Users tab
  userProblems() {
    const u = this.st.project && this.st.project.users;
    if (!u || u.enabled === false) return [];
    const out = [], list = Array.isArray(u.list) ? u.list : [], max = this.userLevels().length;
    const sys = u.system_level === undefined || u.system_level === '' ? max : Number(u.system_level);
    const seen = new Set();
    for (const x of list) {
      const name = String(x.name || '');
      if (!/^[A-Za-z0-9_.-][A-Za-z0-9_. -]{0,15}$/.test(name) || name.endsWith(' ')) out.push('"' + name + '": a name is 1 to 16 letters, digits, _ - . or spaces');
      if (seen.has(name.toLowerCase())) out.push('"' + name + '" is defined twice');
      seen.add(name.toLowerCase());
      if (!x.pin_hash && !this.validPin(x.pin === undefined ? '' : x.pin)) out.push('"' + name + '" has no PIN (4 to 8 digits)');
      if (!(Number(x.level) >= 1 && Number(x.level) <= max)) out.push('"' + name + '": the level must be 1 to ' + max);
    }
    if (!list.length) out.push('no user is defined: nobody can log in');
    else if (sys > 0 && !list.some(x => Number(x.level) >= sys)) out.push('no user has level ' + sys + ': nobody can open the system settings on the device');
    return out;
  },

  // ---- scripts (Lua) -----------------------------------------------------
  // ---- system log ("system_log" in project.json): recorded only while the section exists and is enabled
  systemLogOn() { return this.logOn('system_log'); },
  setSystemLog(on) { this.setLog('system_log', on); },
  // The other logs work the same way; key = 'system_log' | 'data_log' | 'alarms' | 'operation_log'.
  logOn(key) { const lg = this.st.project && this.st.project[key]; return !!lg && typeof lg === 'object' && lg.enabled !== false; },
  setLog(key, on) {
    this.mutate(() => {
      const p = this.st.project;
      const empty = o => !Object.keys(o).some(k => k !== 'enabled' && !(Array.isArray(o[k]) && !o[k].length));
      if (on) { p[key] = (p[key] && typeof p[key] === 'object') ? p[key] : {}; delete p[key].enabled; }
      else if (p[key] && !empty(p[key])) p[key].enabled = false;   // keep the settings
      else delete p[key];
    });
  },

  addScript() {
    return this.mutate(() => {
      const list = this.st.project.scripts = this.st.project.scripts || [];
      let n = list.length + 1, name;
      do { name = 'script' + n++; } while (list.some(s => s && s.name === name));
      list.push({ name, trigger: 'manual', code: '' });
      return list.length - 1;
    });
  },
  removeScript(i) {
    this.mutate(() => {
      const list = this.st.project.scripts;
      list.splice(i, 1);
      if (!list.length) delete this.st.project.scripts;
    });
  },
  // false if the name is empty or already used; script actions that call it follow the rename
  renameScript(i, name) {
    name = String(name || '').trim();
    const list = (this.st.project && this.st.project.scripts) || [], s = list[i];
    if (!s || !name || list.some((o, j) => j !== i && o && o.name === name)) return false;
    if (s.name === name) return true;
    this.mutate(() => {
      const old = s.name;
      s.name = name;
      if (old) this.eachAction(a => { if (a.action === 'script' && a.name === old) a.name = name; });
    });
    return true;
  },
  // the keys each trigger uses; the others are dropped when the trigger changes
  setScriptTrigger(i, trigger) {
    const s = this.st.project.scripts[i];
    this.mutate(() => {
      if (!trigger || trigger === 'manual') delete s.trigger; else s.trigger = trigger;
      if (trigger !== 'interval') delete s.interval_ms; else if (s.interval_ms === undefined) s.interval_ms = 1000;
      if (trigger !== 'tag') { delete s.tags; delete s.tag; } else if (!Array.isArray(s.tags)) s.tags = s.tag ? [s.tag] : [];
      if (trigger === 'tag') delete s.tag;
      if (trigger !== 'screen_show' && trigger !== 'screen_hide') delete s.screen;
    });
  },
};
