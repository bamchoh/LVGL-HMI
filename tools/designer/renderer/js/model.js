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
    st.path = null; st.dir = null; st.archive = false; st.name = '';
    st.project = this.blankProject(title, w, h);
    st.screens = [{ file: 'screens/main.json', data: this.blankScreen('main') }];
    st.cur = 0; st.sel = []; st.dirty = true; st.undo = []; st.redo = [];
  },

  // The project can be a folder (path = .../project.json, dir = that folder) or one file
  // (path = .../name.hmlproj, dir = the working folder it is unpacked into). Everything that
  // needs files on disk - Run, Preview, Deploy, images - uses dir; path is what the user sees.
  runPath() { return this.st.dir ? this.st.dir.replace(/[\\/]+$/, '') + '/project.json' : null; },
  // r = { path, dir, project: text, screens: [{ file, text }], archive, name } from the host
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
    st.archive = !!r.archive; st.name = r.name || U.basename(r.dir || '');
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

  // The tags a tag field offers: the project's (with their type), then those the runtime makes.
  tagChoices() {
    const out = ((this.st.project && this.st.project.tags) || []).filter(t => t && t.name).map(t => ({ name: t.name, note: t.type || '' }));
    if (this.usersOn()) for (const [n, ty] of [['sys.user.name', 'string'], ['sys.user.level', 'int'], ['sys.user.level_name', 'string'], ['sys.user.logged_in', 'bool']]) out.push({ name: n, note: ty + ' (system)' });
    return out;
  },

  // Snap while dragging. box: the dragged widget(s) { x, y, w, h } where they would be drawn;
  // others: the rectangles to line up with (siblings, and the parent as { x: 0, y: 0, w, h }).
  // Returns { dx, dy, gx, gy }: the correction, and where the guide lines are (null = none).
  snap(box, others, tol) {
    const best = (lines, cands) => {
      let r = null;
      for (const l of lines) for (const c of cands) { const d = c - l; if (Math.abs(d) <= tol && (!r || Math.abs(d) < Math.abs(r.d))) r = { d, at: c }; }
      return r;
    };
    const xs = [], ys = [];
    for (const o of others) { xs.push(o.x, o.x + o.w / 2, o.x + o.w); ys.push(o.y, o.y + o.h / 2, o.y + o.h); }
    const bx = best([box.x, box.x + box.w / 2, box.x + box.w], xs), by = best([box.y, box.y + box.h / 2, box.y + box.h], ys);
    return { dx: bx ? Math.round(bx.d) : 0, dy: by ? Math.round(by.d) : 0, gx: bx ? bx.at : null, gy: by ? by.at : null };
  },

  // ---- several widgets selected at once ----------------------------------
  // st.sel is the selection as before (one path). st.more holds further widgets of the same
  // parent that are selected with it (Ctrl / Shift + click). Everything that works on
  // "the selection" uses selection(): the paths, st.sel first.
  selection() {
    const st = this.st;
    if (!st.sel || !st.sel.length || !this.get(st.sel)) return [];
    const parent = U.pathStr(st.sel.slice(0, -1)), out = [st.sel];
    for (const p of st.more || []) {
      if (p.length === st.sel.length && U.pathStr(p.slice(0, -1)) === parent && this.get(p) && !out.some(o => U.samePath(o, p))) out.push(p);
    }
    return out;
  },
  // Ctrl + click: add the widget to the selection or take it out again. A widget of another
  // parent starts a new selection.
  toggleSelect(path) {
    const st = this.st;
    if (!path || !path.length) return;
    if (!st.sel || !st.sel.length || U.pathStr(path.slice(0, -1)) !== U.pathStr(st.sel.slice(0, -1))) { st.sel = path; st.more = []; return; }
    const all = this.selection(), i = all.findIndex(p => U.samePath(p, path));
    if (i < 0) { st.more = (st.more || []).concat([path]); return; }
    if (all.length === 1) return;                               // the last one stays selected
    all.splice(i, 1);
    st.sel = all[0]; st.more = all.slice(1);
  },
  selectSiblings() {                                             // Ctrl+A: everything in the container of the selection
    const st = this.st, cp = st.sel && st.sel.length ? st.sel.slice(0, -1) : [], c = this.get(cp);
    const n = c && c.children ? c.children.length : 0;
    if (!n) return;
    st.sel = cp.concat(0); st.more = [];
    for (let i = 1; i < n; i++) st.more.push(cp.concat(i));
  },
  deleteSelection() {
    const sels = this.selection();
    if (!sels.length) return;
    this.mutate(() => {
      const parent = this.parent(sels[0]);
      for (const i of sels.map(p => p[p.length - 1]).sort((a, b) => b - a)) parent.children.splice(i, 1);
      this.st.sel = sels[0].slice(0, -1); this.st.more = [];
    });
  },
  duplicateSelection() {
    const sels = this.selection();
    if (!sels.length) return;
    this.mutate(() => {
      const cp = sels[0].slice(0, -1), parent = this.get(cp), idx = sels.map(p => p[p.length - 1]).sort((a, b) => a - b);
      const at = idx[idx.length - 1] + 1;
      const copies = idx.map(i => { const c = U.clone(parent.children[i]); if (typeof c.x === 'number') c.x += 10; if (typeof c.y === 'number') c.y += 10; return c; });
      parent.children.splice(at, 0, ...copies);
      this.st.sel = cp.concat(at); this.st.more = copies.slice(1).map((c, k) => cp.concat(at + 1 + k));
    });
  },
  nudgeSelection(dx, dy) {
    const sels = this.selection();
    if (!sels.length) return;
    const parent = this.parent(sels[0]);
    if (parent && parent.flex) return;                          // placed by the layout
    this.mutate(() => { for (const p of sels) { const w = this.get(p); w.x = U.num(w.x, 0) + dx; w.y = U.num(w.y, 0) + dy; } });
  },

  // Line up or space out the selected widgets. rects: [{ path, x, y, w, h }] as they are drawn
  // (the first one is the widget selected first). kind:
  //   left right top bottom hcenter vcenter   against the box around all of them
  //   hdist vdist                              equal gaps between the outermost two
  //   samew sameh                              the size of the first one
  arrange(kind, rects) {
    if (!rects || rects.length < 2) return;
    const minX = Math.min(...rects.map(r => r.x)), maxX = Math.max(...rects.map(r => r.x + r.w));
    const minY = Math.min(...rects.map(r => r.y)), maxY = Math.max(...rects.map(r => r.y + r.h));
    const move = new Map(), size = new Map();
    const shift = (r, dx, dy) => move.set(r, [dx, dy]);
    for (const r of rects) {
      if (kind === 'left') shift(r, minX - r.x, 0);
      else if (kind === 'right') shift(r, maxX - (r.x + r.w), 0);
      else if (kind === 'top') shift(r, 0, minY - r.y);
      else if (kind === 'bottom') shift(r, 0, maxY - (r.y + r.h));
      else if (kind === 'hcenter') shift(r, Math.round((minX + maxX) / 2 - (r.x + r.w / 2)), 0);
      else if (kind === 'vcenter') shift(r, 0, Math.round((minY + maxY) / 2 - (r.y + r.h / 2)));
      else if (kind === 'samew' && r.w !== rects[0].w) size.set(r, ['w', rects[0].w]);
      else if (kind === 'sameh' && r.h !== rects[0].h) size.set(r, ['h', rects[0].h]);
    }
    if ((kind === 'hdist' || kind === 'vdist') && rects.length > 2) {
      const h = kind === 'hdist';
      const sorted = rects.slice().sort((a, b) => (h ? a.x - b.x : a.y - b.y));
      const total = sorted.reduce((s, r) => s + (h ? r.w : r.h), 0);
      const gap = ((h ? maxX - minX : maxY - minY) - total) / (sorted.length - 1);
      let pos = h ? minX : minY;
      for (const r of sorted) {
        const d = Math.round(pos - (h ? r.x : r.y));
        shift(r, h ? d : 0, h ? 0 : d);
        pos += (h ? r.w : r.h) + gap;
      }
    }
    if (![...move.values()].some(m => m[0] || m[1]) && !size.size) return;
    this.mutate(() => {
      for (const [r, m] of move) { const w = this.get(r.path); if (m[0]) w.x = U.num(w.x, 0) + m[0]; if (m[1]) w.y = U.num(w.y, 0) + m[1]; }
      for (const [r, s] of size) this.get(r.path)[s[0]] = s[1];
    });
  },

  // ---- copy and paste -----------------------------------------------------
  // The copied widgets as plain data, in their order on the screen.
  copySelection() {
    const sels = this.selection();
    return sels.slice().sort((a, b) => a[a.length - 1] - b[b.length - 1]).map(p => U.clone(this.get(p)));
  },
  // Paste into the selected container, else next to the selected widget, else onto the
  // screen. Works across screens (the clipboard is plain data). The pasted widgets are selected.
  paste(widgets) {
    if (!widgets || !widgets.length || !this.screen()) return;
    this.mutate(() => {
      const cp = this.containerPathFor(this.st.sel), parent = this.get(cp);
      parent.children = parent.children || [];
      const first = parent.children.length;
      for (const src of widgets) {
        const w = U.clone(src);
        // not exactly on top of a widget that is already there (pasting where it was copied)
        while (!parent.flex && parent.children.some(c => U.num(c.x, 0) === U.num(w.x, 0) && U.num(c.y, 0) === U.num(w.y, 0) && (c.align || '') === (w.align || ''))) {
          w.x = U.num(w.x, 0) + 10; w.y = U.num(w.y, 0) + 10;
        }
        parent.children.push(w);
      }
      this.st.sel = cp.concat(first);
      this.st.more = widgets.slice(1).map((w, k) => cp.concat(first + 1 + k));
    });
  },

  // ---- problems -------------------------------------------------------------
  // Things in the project that will not work on the device: names that do not exist, and the
  // like. Each entry: { text, screen, path } for a widget or { text, view, index } for a page.
  problems() {
    const st = this.st, p = st.project;
    if (!p) return [];
    const out = [];
    const tags = new Set(this.tagNames()), screens = new Set(st.screens.map(s => s.data.name)), scripts = new Set(this.scriptNames());
    const okTag = n => tags.has(n) || /^sys\./.test(n);          // sys.*: made by the runtime (user, system settings)
    const usersOn = this.usersOn();

    // tags
    const seenTag = new Set();
    (p.tags || []).forEach((t, i) => {
      if (!t.name) out.push({ text: 'a tag has no name', view: 'tags', index: i });
      else if (seenTag.has(t.name)) out.push({ text: 'tag "' + t.name + '" is defined twice', view: 'tags', index: i });
      seenTag.add(t.name);
    });
    // screens
    if (p.start_screen && !screens.has(p.start_screen)) out.push({ text: 'start screen "' + p.start_screen + '" does not exist', view: 'project' });
    const seenScr = new Set();
    st.screens.forEach((s, si) => {
      if (seenScr.has(s.data.name)) out.push({ text: 'screen "' + s.data.name + '" is defined twice', screen: si, path: [] });
      seenScr.add(s.data.name);
      const tagOf = spec => (typeof spec === 'string' ? spec : spec && typeof spec === 'object' ? spec.tag : undefined);
      const visit = (w, path) => {
        const at = { screen: si, path };
        const need = (name, what) => { if (name !== undefined && name !== '' && !okTag(name)) out.push(Object.assign({ text: 'unknown tag "' + name + '"' + (what ? ' (' + what + ')' : '') }, at)); };
        if (typeof w.bind === 'string') need(w.bind, 'bind');
        else if (w.bind && typeof w.bind === 'object') {
          for (const [kind, spec] of Object.entries(w.bind)) {
            need(tagOf(spec), kind);
            if (spec && typeof spec === 'object') for (const op of ['eq', 'ne', 'gt', 'lt', 'ge', 'le']) if (spec[op] && typeof spec[op] === 'object') need(spec[op].tag, kind);
          }
        }
        for (const [k, v] of Object.entries(w)) {
          if (!/^on_/.test(k) || !v) continue;
          for (const a of Array.isArray(v) ? v : [v]) {
            if (!a || typeof a !== 'object') continue;
            if (['set', 'toggle', 'inc', 'dec'].includes(a.action)) need(a.tag, a.action);
            if (a.action === 'goto' && a.screen && !screens.has(a.screen) && !/^__/.test(a.screen)) out.push(Object.assign({ text: 'unknown screen "' + a.screen + '" (goto)' }, at));
            if (a.action === 'script' && a.name && !scripts.has(a.name)) out.push(Object.assign({ text: 'unknown script "' + a.name + '"' }, at));
            if ((a.action === 'login' || a.action === 'logout') && !usersOn) out.push(Object.assign({ text: 'action "' + a.action + '" needs user management (Users tab)' }, at));
          }
        }
        for (const s2 of Array.isArray(w.series) ? w.series : []) need(s2 && s2.tag, 'series');
        if (U.num(w.level, 0) > 0 && !usersOn) out.push(Object.assign({ text: 'a level is set, but user management is off (Users tab)' }, at));
        (w.children || []).forEach((c, i) => visit(c, path.concat(i)));
      };
      visit(s.data, []);
    });
    // drivers
    (p.drivers || []).forEach((d, i) => {
      const at = { view: 'drivers', index: i }, name = d.name || d.type || 'driver';
      for (const row of [].concat(Array.isArray(d.map) ? d.map : [], Array.isArray(d.items) ? d.items : [])) {
        if (row && row.tag !== undefined && !okTag(row.tag)) out.push(Object.assign({ text: name + ': unknown tag "' + row.tag + '"' }, at));
      }
      if (d.status_tag && !okTag(d.status_tag)) out.push(Object.assign({ text: name + ': unknown tag "' + d.status_tag + '" (status_tag)' }, at));
      if ((d.type === 'modbus_rtu' || d.type === 'modbus_ascii') && !d.device && !d.host) out.push(Object.assign({ text: name + ': no serial port (device) is set' }, at));
    });
    // scripts
    (p.scripts || []).forEach((s, i) => {
      if (s.trigger === 'tag' && s.tag && !okTag(s.tag)) out.push({ text: 'script "' + (s.name || '') + '": unknown tag "' + s.tag + '"', view: 'scripts', index: i });
      if (/^screen_/.test(s.trigger || '') && s.screen && !screens.has(s.screen)) out.push({ text: 'script "' + (s.name || '') + '": unknown screen "' + s.screen + '"', view: 'scripts', index: i });
    });
    // logs
    const logTag = (t, where) => { if (t && !okTag(t)) out.push({ text: where + ': unknown tag "' + t + '"', view: 'logs' }); };
    if (this.logOn('data_log')) for (const g of p.data_log.groups || []) { for (const t of g.tags || []) logTag(t, 'data log "' + (g.name || '') + '"'); logTag(g.trigger_tag, 'data log "' + (g.name || '') + '"'); }
    if (this.logOn('alarms')) for (const a of p.alarms.items || []) { logTag(a.tag, 'alarm "' + (a.name || '') + '"'); logTag(a.value_tag, 'alarm "' + (a.name || '') + '"'); }
    if (this.logOn('operation_log') && !usersOn) logTag(p.operation_log.user_tag, 'operation log');
    // users
    for (const w of this.userProblems()) out.push({ text: w, view: 'users' });
    return out;
  },

  // ---- undo / redo -------------------------------------------------------
  snapshot() { const st = this.st; return JSON.stringify({ p: st.project, s: st.screens, c: st.cur, sel: st.sel, more: st.more || [] }); },
  restore(snap) {
    const o = JSON.parse(snap), st = this.st;
    st.project = o.p; st.screens = o.s; st.cur = Math.min(o.c, st.screens.length - 1); st.sel = o.sel; st.more = o.more || [];
  },
  pushUndo() { const st = this.st; st.undo.push(this.snapshot()); if (st.undo.length > 100) st.undo.shift(); st.redo = []; },
  mutate(fn) {
    this.pushUndo();
    const st = this.st, sel = st.sel, more = st.more;
    const r = fn();
    // an operation that selects something else ends a selection of several widgets
    if (st.more === more && !U.samePath(st.sel, sel)) st.more = [];
    st.dirty = true; App.refresh(); return r;
  },
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

  // pos: { x, y } inside the container (a widget dropped there); else a free spot is chosen
  addWidget(type, atPath, pos) {
    return this.mutate(() => {
      const cp = this.containerPathFor(atPath);
      const parent = this.get(cp);
      parent.children = parent.children || [];
      const w = this.newWidget(type);
      if (pos && !parent.flex) { delete w.align; w.x = Math.max(0, Math.round(pos.x)); w.y = Math.max(0, Math.round(pos.y)); }
      else if (!parent.flex && w.align === undefined && w.x === undefined) {
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
  selectScreen(idx) { this.st.cur = idx; this.st.sel = []; this.st.more = []; App.refresh(); },

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

  // Apply one of SCHEMA.displayPresets: the size, and the touch correction that goes with it.
  applyDisplayPreset(preset) {
    this.mutate(() => {
      const d = this.st.project.display = this.st.project.display || {};
      d.width = preset.width; d.height = preset.height;
      if (preset.touch) d.touch = U.clone(preset.touch); else delete d.touch;
    });
  },
  // One of the four touch calibration values (min_x, min_y, max_x, max_y); the entry goes
  // away when all four are empty.
  setTouchCalibration(index, value) {
    this.mutate(() => {
      const d = this.st.project.display = this.st.project.display || {};
      const t = d.touch = (d.touch && typeof d.touch === 'object') ? d.touch : {};
      const c = Array.isArray(t.calibration) && t.calibration.length === 4 ? t.calibration.slice() : [0, 0, 4095, 4095];
      c[index] = value;
      if (c.some(v => v === undefined || v === null || v === '' || isNaN(Number(v)))) delete t.calibration;
      else t.calibration = c.map(Number);
      if (!Object.keys(t).length) delete d.touch;
    });
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
