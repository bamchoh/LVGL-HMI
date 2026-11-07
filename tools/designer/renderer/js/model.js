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
      // keep goto actions pointing at the renamed screen
      const fix = w => {
        for (const ev of SCHEMA.events) {
          const acts = w[ev]; if (!acts) continue;
          for (const a of Array.isArray(acts) ? acts : [acts]) if (a && a.action === 'goto' && a.screen === old) a.screen = name;
        }
        (w.children || []).forEach(fix);
      };
      this.st.screens.forEach(sc => fix(sc.data));
    });
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
};
