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
    out.push({ name: 'sys.screen.off', note: 'bool (system)' }, { name: 'sys.lang', note: 'string (system)' });
    if (this.st.project && this.st.project.sound) out.push({ name: 'sys.sound.mute', note: 'bool (system)' }, { name: 'sys.sound.volume', note: 'int (system)' }, { name: 'sys.sound.alarm', note: 'bool (system)' });
    if (this.usersOn()) for (const [n, ty] of [['sys.user.name', 'string'], ['sys.user.level', 'int'], ['sys.user.level_name', 'string'], ['sys.user.logged_in', 'bool']]) out.push({ name: n, note: ty + ' (system)' });
    for (const [n, ty] of [['sys.alarm.active', 'int'], ['sys.alarm.unacked', 'int'], ['sys.alarm.latest', 'string'], ['sys.alarm.latest_severity', 'string']]) out.push({ name: n, note: ty + ' (system)' });
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

  // ---- where tags are used ---------------------------------------------------
  // Calls cb(name, set, loc, what, soft) for every place that names a tag. set(newName) changes
  // it there; loc is { screen, path } or { view, index }; what says which property. soft: found
  // in Lua code (tag.name) - renamed with the tag, but not reported when the tag is unknown.
  eachTagRef(cb) {
    const st = this.st, p = st.project;
    if (!p) return;
    const prop = (o, k, loc, what) => { if (o && typeof o[k] === 'string' && o[k] !== '') cb(o[k], v => { o[k] = v; }, loc, what || k, false); };
    const list = (arr, loc, what) => { if (Array.isArray(arr)) arr.forEach((v, i) => { if (typeof v === 'string' && v) cb(v, n => { arr[i] = n; }, loc, what, false); }); };
    // a condition: "tag" or { "tag": ..., "gt": { "tag": ... } }
    const cond = (o, k, loc, what, soft) => {
      const c = o && o[k];
      const one = (h, key) => { if (h && typeof h[key] === 'string' && h[key] !== '') cb(h[key], v => { h[key] = v; }, loc, what, soft); };
      if (typeof c === 'string') one(o, k);
      else if (c && typeof c === 'object') { one(c, 'tag'); for (const op of ['eq', 'ne', 'gt', 'lt', 'ge', 'le']) if (c[op] && typeof c[op] === 'object') one(c[op], 'tag'); }
    };
    const lua = (o, k, loc) => {
      if (!o) return;
      if (Array.isArray(o[k])) { o[k].forEach((line, i) => lua(o[k], i, loc)); return; }     // code as a list of lines
      if (typeof o[k] !== 'string') return;
      for (const name of new Set([...o[k].matchAll(/\btag\.([A-Za-z_][A-Za-z0-9_]*)/g)].map(m => m[1]))) {
        cb(name, n => { if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(n)) o[k] = o[k].replace(new RegExp('\\btag\\.' + name + '\\b', 'g'), 'tag.' + n); }, loc, 'lua', true);
      }
    };
    st.screens.forEach((s, si) => {
      const visit = (w, path) => {
        const loc = { screen: si, path };
        if (typeof w.bind === 'string') prop(w, 'bind', loc, 'bind');
        else if (w.bind && typeof w.bind === 'object') {
          for (const kind of Object.keys(w.bind)) {
            const spec = w.bind[kind];
            if (typeof spec === 'string') prop(w.bind, kind, loc, kind);
            else if (spec && typeof spec === 'object') {
              prop(spec, 'tag', loc, kind);
              for (const op of ['eq', 'ne', 'gt', 'lt', 'ge', 'le']) if (spec[op] && typeof spec[op] === 'object') prop(spec[op], 'tag', loc, kind);
            }
          }
        }
        for (const k of Object.keys(w)) {
          if (!/^on_/.test(k) || !w[k]) continue;
          for (const act of Array.isArray(w[k]) ? w[k] : [w[k]]) {
            if (!act || typeof act !== 'object') continue;
            if (['set', 'toggle', 'inc', 'dec'].includes(act.action)) prop(act, 'tag', loc, act.action);
            if (act.action === 'script') lua(act, 'code', loc);
          }
        }
        // "animate": { "when": ... } (an unknown tag there is reported with the other animate problems)
        for (const a of Array.isArray(w.animate) ? w.animate : [w.animate]) if (a && typeof a === 'object') cond(a, 'when', loc, 'animate', true);
        for (const s2 of Array.isArray(w.series) ? w.series : []) prop(s2, 'tag', loc, 'series');
        if (w.type === 'trend' && Array.isArray(w.tags)) w.tags.forEach((x, i) => {
          if (typeof x === 'string') { if (x) cb(x, n => { w.tags[i] = n; }, loc, 'trend', false); }
          else prop(x, 'tag', loc, 'trend');
        });
        // the cells of a table that follow a tag; the tag a log view filters by
        if (w.type === 'table' && Array.isArray(w.rows)) for (const row of w.rows) if (Array.isArray(row)) for (const cell of row) if (cell && typeof cell === 'object' && !Array.isArray(cell)) prop(cell, 'tag', loc, 'table');
        if (w.type === 'log_view') prop(w, 'filter_tag', loc, 'filter');
        if (w.type === 'xy_chart') { for (const k of ['x_tag', 'y_tag', 'clear_tag']) prop(w, k, loc, 'xy_chart'); for (const s3 of Array.isArray(w.series) ? w.series : []) if (s3 && typeof s3 === 'object') { prop(s3, 'x_tag', loc, 'xy_chart'); prop(s3, 'y_tag', loc, 'xy_chart'); } }
        (w.children || []).forEach((c, i) => visit(c, path.concat(i)));
      };
      visit(s.data, []);
    });
    // transfers: from, to, and the tags of the condition
    (Array.isArray(p.transfers) ? p.transfers : []).forEach((t, i) => {
      if (!t || typeof t !== 'object') return;
      const loc = { view: 'project', index: i };
      prop(t, 'from', loc, 'transfer');
      prop(t, 'to', loc, 'transfer');
      cond(t, 'when', loc, 'transfer', false);
    });
    // what a schedule does: the actions of a screen
    (Array.isArray(p.schedules) ? p.schedules : []).forEach((sc, i) => {
      if (!sc || typeof sc !== 'object') return;
      const loc = { view: 'project', index: i };
      for (const act of Array.isArray(sc.do) ? sc.do : [sc.do]) {
        if (!act || typeof act !== 'object') continue;
        if (['set', 'toggle', 'inc', 'dec'].includes(act.action)) prop(act, 'tag', loc, 'schedule');
        if (act.action === 'script') lua(act, 'code', loc);
      }
    });
    (p.drivers || []).forEach((d, i) => {
      const loc = { view: 'drivers', index: i };
      for (const [k, v] of Object.entries(d || {})) {
        if (!Array.isArray(v)) continue;
        if (k === 'tags' || k === 'publish' || k === 'subscribe') list(v, loc, k);
        for (const row of v) if (row && typeof row === 'object') prop(row, 'tag', loc, k);
      }
      prop(d, 'status_tag', loc);
    });
    (p.scripts || []).forEach((sc, i) => {
      const loc = { view: 'scripts', index: i };
      if (!sc) return;
      list(sc.tags, loc, 'trigger'); prop(sc, 'tag', loc, 'trigger'); lua(sc, 'code', loc);
    });
    const logs = { view: 'logs' };
    for (const g of (p.data_log && p.data_log.groups) || []) { if (!g) continue; list(g.tags, logs, 'data_log'); prop(g, 'trigger_tag', logs); }
    for (const al of (p.alarms && p.alarms.items) || []) { if (!al) continue; prop(al, 'tag', logs, 'alarms'); prop(al, 'value_tag', logs); }
    if (p.operation_log) prop(p.operation_log, 'user_tag', logs);
    this.recipeBooks().forEach((b, bi) => {
      const loc = { view: 'recipes', index: bi };
      list(b.items, loc, 'items');
      // the values of the recipes are keyed by tag name: a rename moves them
      for (const r of Array.isArray(b.recipes) ? b.recipes : []) {
        const vals = r && r.values;
        if (!vals || typeof vals !== 'object') continue;
        for (const k of Object.keys(vals)) cb(k, n => { if (n !== k && !(n in vals)) { vals[n] = vals[k]; delete vals[k]; } }, loc, 'recipe', true);
      }
    });
  },
  // name -> [{ loc, what }]
  tagUses() {
    const m = new Map();
    this.eachTagRef((name, set, loc, what) => { if (!m.has(name)) m.set(name, []); m.get(name).push({ loc, what }); });
    return m;
  },
  // Give a tag another name; everything that names it follows. Returns the number of places changed.
  renameTag(i, name) {
    const tags = this.st.project.tags, t = tags[i];
    name = String(name || '').trim();
    if (!t || t.name === name) return 0;
    let n = 0;
    this.mutate(() => {
      const old = t.name;
      t.name = name;
      // (not when another tag has the old name too, or the new name is already taken: then the
      // references are ambiguous and stay as they are)
      if (!old || !name || tags.some(x => x !== t && (x.name === old || x.name === name))) return;
      this.eachTagRef((ref, set) => { if (ref === old) { set(name); n++; } });
    });
    return n;
  },

  // ---- find and replace: tag names and texts, over a screen or the whole project ----
  // opt: { what: 'tags' | 'texts', find, replace, whole, matchCase, scope: 'screen' | 'screens' | 'project',
  //        definitions (tags, scope project: the names in the tag list too), createTags }
  // -> the places that would change: [{ loc, what, before, after, set }]. Nothing is changed.
  replacePlan(opt) {
    const st = this.st, out = [];
    const find = String(opt.find === undefined || opt.find === null ? '' : opt.find), repl = String(opt.replace === undefined || opt.replace === null ? '' : opt.replace);
    if (!st.project || !find) return out;
    const cs = opt.matchCase !== false, whole = !!opt.whole, scope = opt.scope || 'project';
    const inScope = loc => scope === 'project' || (loc && loc.screen !== undefined && (scope === 'screens' || loc.screen === st.cur));
    const lf = find.toLowerCase();
    const change = s => {
      if (whole) return (cs ? s === find : s.toLowerCase() === lf) ? repl : s;
      if (cs) return s.split(find).join(repl);
      let r = '', i = 0;
      const ls = s.toLowerCase();
      for (;;) { const k = ls.indexOf(lf, i); if (k < 0) { r += s.slice(i); break; } r += s.slice(i, k) + repl; i = k + find.length; }
      return r;
    };
    const add = (before, set, loc, what, lua) => {
      if (typeof before !== 'string' || !inScope(loc)) return;
      const after = change(before);
      if (after !== before) out.push({ loc, what, before, after, set, lua: !!lua });
    };
    if (opt.what === 'texts') {
      const table = st.project.texts && typeof st.project.texts === 'object' ? st.project.texts : null;
      // ("@id" is a name, not a text: the text itself is in the table)
      this.eachTextRef((s, set, loc) => { if (table && s[0] === '@' && s[1] !== '@') return; add(s, set, loc, 'text'); });
      if (table && scope === 'project')
        for (const id of Object.keys(table)) {
          const row = table[id];
          if (!row || typeof row !== 'object') continue;
          for (const lang of Object.keys(row)) add(row[lang], v => { row[lang] = v; }, { view: 'texts' }, id + ' / ' + lang);
        }
    } else {
      this.eachTagRef((name, set, loc, what) => add(name, set, loc, what, what === 'lua'));
      if (opt.definitions && scope === 'project') (st.project.tags || []).forEach((t, i) => { if (t) add(t.name, v => { t.name = v; }, { view: 'tags', index: i }, 'tag', false); });
    }
    return out;
  },
  // Do it, as one step that can be undone. -> { changed, created, skipped, newProblems }
  replaceApply(opt) {
    const count = list => { const m = new Map(); for (const p of list) m.set(p.text, (m.get(p.text) || 0) + 1); return m; };
    const before = count(this.problems());
    let changed = 0, created = 0, skipped = 0;
    this.mutate(() => {
      const st = this.st, tags = st.project.tags || (st.project.tags = []);
      const plan = this.replacePlan(opt);
      const isTags = opt.what !== 'texts';
      // a tag definition does not take a name another tag has (or gets)
      const taken = new Set(tags.map(t => t && t.name));
      const todo = [];
      for (const c of plan) {
        if (isTags && c.loc.view === 'tags' && c.what === 'tag') {
          if (!c.after || taken.has(c.after)) { skipped++; continue; }
          taken.delete(c.before); taken.add(c.after);
        }
        if (isTags && c.lua && !/^[A-Za-z_][A-Za-z0-9_]*$/.test(c.after)) { skipped++; continue; }      // (not a name Lua can write as tag.x)
        todo.push(c);
      }
      // the tags the new names need: copies of the ones the old names had
      if (isTags && opt.createTags) {
        const byName = new Map(tags.map(t => [t && t.name, t]));
        for (const c of todo) {
          if (c.loc.view === 'tags' || !c.after || byName.has(c.after) || /^sys\./.test(c.after)) continue;
          const src = byName.get(c.before);
          if (!src) continue;
          const copy = Object.assign(U.clone(src), { name: c.after });
          tags.splice(tags.indexOf(src) + 1, 0, copy);
          byName.set(c.after, copy);
          created++;
        }
      }
      // Lua code: a name is replaced wherever it stands in the code, so a name that is the result of
      // one replacement must not be the subject of a later one (x1 -> x11 before x11 -> x111 would
      // change both to x111): those whose new name nobody else waits for go first.
      const rest = todo.filter(c => c.lua), plain = todo.filter(c => !c.lua);
      for (const c of plain) { c.set(c.after); changed++; }
      while (rest.length) {
        let k = rest.findIndex(c => !rest.some(o => o !== c && o.before === c.after));
        if (k < 0) k = 0;                                   // (a ring of names: no order is right)
        const c = rest.splice(k, 1)[0];
        c.set(c.after); changed++;
      }
    });
    const fresh = [];
    for (const p of this.problems()) { const n = before.get(p.text) || 0; if (n) before.set(p.text, n - 1); else fresh.push(p); }
    return { changed, created, skipped, newProblems: fresh };
  },

  // ---- screens: a copy, another size ----------------------------------------
  // A copy of a screen, placed after it and shown. -> its index, or -1
  duplicateScreen(idx, name) {
    const st = this.st, src = st.screens[idx];
    if (!src) return -1;
    const safe = s => String(s || '').trim().replace(/[^A-Za-z0-9_\-]/g, '_');
    const base = safe(name) || safe(src.data.name + '_copy') || 'screen';
    const names = new Set(this.screenNames()), files = new Set(st.screens.map(s => s.file));
    let n = base;
    for (let i = 2; names.has(n) || files.has('screens/' + n + '.json'); i++) n = base + i;
    this.mutate(() => {
      const data = U.clone(src.data);
      data.name = n;
      st.screens.splice(idx + 1, 0, { file: 'screens/' + n + '.json', data });
      st.cur = idx + 1;
      st.sel = [];
    });
    return idx + 1;
  },
  // Positions, sizes, fonts and spacings of one widget tree for a display sx times as wide and sy
  // times as high. Percentages and "content" stay; a font becomes the nearest size there is.
  scaleWidget(w, sx, sy) {
    const s = Math.min(sx, sy), fonts = SCHEMA.fonts;
    const px = (v, f) => (typeof v === 'number' && isFinite(v) ? Math.round(v * f) : v);
    const visit = o => {
      if (!o || typeof o !== 'object') return;
      o.x = px(o.x, sx); o.y = px(o.y, sy); o.w = px(o.w, sx); o.h = px(o.h, sy);
      for (const k of ['x', 'y', 'w', 'h']) if (o[k] === undefined) delete o[k];
      if (Array.isArray(o.points)) o.points = o.points.map(p => (Array.isArray(p) && p.length >= 2 ? [px(p[0], sx), px(p[1], sy)].concat(p.slice(2)) : p));
      for (const k of Object.keys(o)) {
        if (!/^style/.test(k) || !o[k] || typeof o[k] !== 'object' || Array.isArray(o[k])) continue;
        const st = o[k];
        if (typeof st.font === 'number') { const want = st.font * s; st.font = fonts.reduce((a, b) => (Math.abs(b - want) < Math.abs(a - want) ? b : a), fonts[0]); }
        for (const sk of Object.keys(st)) {
          if (typeof st[sk] !== 'number') continue;
          if (/^pad(_|$)|^margin(_|$)|^radius$/.test(sk)) st[sk] = Math.max(st[sk] > 0 ? 1 : 0, Math.round(st[sk] * (/_left$|_right$|_column$/.test(sk) ? sx : /_top$|_bottom$|_row$/.test(sk) ? sy : s)));
        }
      }
      if (Array.isArray(o.children)) o.children.forEach(visit);
    };
    visit(w);
    return w;
  },
  // The whole project for another display: every screen is scaled, the display gets the new size.
  resizeProject(width, height) {
    const st = this.st, d = this.display();
    width = Math.round(Number(width)); height = Math.round(Number(height));
    if (!st.project || !(width >= 64 && width <= 8192 && height >= 64 && height <= 8192)) return false;
    if (width === d.w && height === d.h) return false;
    this.mutate(() => {
      const sx = width / d.w, sy = height / d.h;
      for (const s of st.screens) this.scaleWidget(s.data, sx, sy);
      st.project.display = Object.assign({}, st.project.display, { width, height });
    });
    return true;
  },

  // ---- the library: groups of widgets kept for use in any project ---------------
  // A part is { name, widgets: [...] }: plain data, kept in the settings of the designer (not in
  // the project: the runtime and older designers never see it). What comes from outside - a file
  // somebody sent - goes through libraryClean(): nothing but data gets in.
  LIBRARY_MAX: 200, LIBRARY_WIDGETS_MAX: 2000,
  // true for a path that leaves the project folder (absolute, a drive, "..", a share)
  pathLeavesProject(p) {
    const s = String(p).replace(/\\/g, '/');
    return /^\//.test(s) || /^[A-Za-z]:/.test(s) || /^[a-z][a-z0-9+.-]*:/i.test(s) || s.split('/').some(part => part === '..');
  },
  // -> { part, dropped: [what was left out] }, or { part: null, error }
  libraryClean(raw) {
    const dropped = [];
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return { part: null, error: 'a part is { "name": ..., "widgets": [...] }' };
    const name = String(raw.name === undefined || raw.name === null ? '' : raw.name).replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, 64);
    if (!name) return { part: null, error: 'a part has no name' };
    if (!Array.isArray(raw.widgets) || !raw.widgets.length) return { part: null, error: 'part "' + name + '" has no widgets' };
    let count = 0, tooMany = false;
    const value = (v, depth) => {
      if (v === null || typeof v === 'boolean') return v;
      if (typeof v === 'number') return isFinite(v) ? v : undefined;
      if (typeof v === 'string') return v.length > 20000 ? v.slice(0, 20000) : v;
      if (typeof v !== 'object' || depth > 24) return undefined;          // (functions, symbols, what is nested too deep)
      if (Array.isArray(v)) return v.slice(0, 5000).map(x => value(x, depth + 1)).filter(x => x !== undefined);
      const out = {};
      for (const k of Object.keys(v)) {
        if (k === '__proto__' || k === 'constructor' || k === 'prototype') { dropped.push('key ' + k); continue; }
        if (k === 'src' && typeof v[k] === 'string' && this.pathLeavesProject(v[k])) { dropped.push('image ' + v[k].slice(0, 80)); continue; }
        const x = value(v[k], depth + 1);
        if (x !== undefined) out[k] = x;
      }
      return out;
    };
    const widget = (w, depth) => {
      if (!w || typeof w !== 'object' || Array.isArray(w) || typeof w.type !== 'string' || !w.type || depth > 20) { dropped.push('something that is no widget'); return undefined; }
      if (++count > this.LIBRARY_WIDGETS_MAX) { tooMany = true; return undefined; }
      const kids = Array.isArray(w.children) ? w.children.map(c => widget(c, depth + 1)).filter(Boolean) : undefined;
      const out = value(Object.assign({}, w, { children: undefined }), 0) || {};
      delete out.children;
      if (kids && kids.length) out.children = kids;
      return out;
    };
    const widgets = raw.widgets.map(w => widget(w, 0)).filter(Boolean);
    if (tooMany) return { part: null, error: 'part "' + name + '" has more than ' + this.LIBRARY_WIDGETS_MAX + ' widgets' };
    if (!widgets.length) return { part: null, error: 'part "' + name + '" has no widgets' };
    return { part: { name, widgets }, dropped };
  },
  // The selected widgets as a part: copies, moved so that the group begins at 0, 0.
  libraryPartFromSelection(name) {
    const ws = this.copySelection();
    if (!ws.length) return null;
    const free = ws.filter(w => !w.align || w.align === 'top_left');
    if (free.length === ws.length) {
      const x0 = Math.min(...ws.map(w => U.num(w.x, 0))), y0 = Math.min(...ws.map(w => U.num(w.y, 0)));
      for (const w of ws) { w.x = U.num(w.x, 0) - x0; w.y = U.num(w.y, 0) - y0; }
    }
    return this.libraryClean({ name, widgets: ws }).part;
  },
  libraryExport(parts) { return U.toJson({ hml_library: 1, parts: parts || [] }); },
  // The text of a library file into a list of parts. A part with a name that is there already
  // takes its place. Read as JSON and nothing else. -> { parts, added, replaced, errors, dropped }
  libraryImport(text, existing) {
    const parts = (existing || []).slice(), errors = [], dropped = [];
    let added = 0, replaced = 0, data;
    try { data = JSON.parse(String(text).replace(/^﻿/, '')); } catch (e) { return { parts, added, replaced, dropped, errors: ['not a library file: ' + e.message] }; }
    const list = Array.isArray(data) ? data : data && Array.isArray(data.parts) ? data.parts : null;
    if (!list) return { parts, added, replaced, dropped, errors: ['not a library file: no "parts"'] };
    for (const raw of list) {
      const r = this.libraryClean(raw);
      if (!r.part) { errors.push(r.error); continue; }
      for (const d of r.dropped) dropped.push(r.part.name + ': ' + d);
      const i = parts.findIndex(p => p.name === r.part.name);
      if (i >= 0) { parts[i] = r.part; replaced++; }
      else if (parts.length >= this.LIBRARY_MAX) errors.push('the library is full (' + this.LIBRARY_MAX + ' parts): "' + r.part.name + '" is left out');
      else { parts.push(r.part); added++; }
    }
    return { parts, added, replaced, errors, dropped };
  },
  // Put a part where a paste would go. -> the number of widgets
  pastePart(part) {
    const r = this.libraryClean(part);
    if (!r.part || !this.screen()) return 0;
    this.paste(r.part.widgets);
    return r.part.widgets.length;
  },

  // ---- templates: a screen built on another one ("template": "<screen>") ----------
  // The screens a screen is built on, the outermost first, and why the chain ends early ('' = it does not).
  templateChain(scr) {
    const layers = [], seen = [scr && scr.name];
    let problem = '', next = scr && scr.template;
    if (next !== undefined && (typeof next !== 'string' || !next)) return { layers, problem: 'template: the name of a screen' };
    while (next) {
      const t = this.st.screens.find(s => s.data.name === next);
      if (next === seen[0] && seen.length === 1) { problem = 'a screen is not its own template'; break; }
      if (!t) { problem = 'unknown template "' + next + '"'; break; }
      if (seen.includes(next)) { problem = 'the templates form a ring (' + next + ')'; break; }
      if (t.data.popup) { problem = 'the template "' + next + '" is a popup'; break; }
      if (seen.length > 8) { problem = 'more than 8 templates on top of each other'; break; }
      seen.push(next);
      layers.unshift(t.data);
      next = typeof t.data.template === 'string' ? t.data.template : '';
    }
    return { layers, problem };
  },
  // the names in "${name}" of the texts of the templates of a screen (without the built-in "screen")
  templatePlaceholders(scr) {
    const out = new Set();
    const visit = v => {
      if (typeof v === 'string') { for (const m of v.matchAll(/\$\{([^}]*)\}/g)) out.add(m[1]); }
      else if (v && typeof v === 'object') for (const x of Array.isArray(v) ? v : Object.values(v)) visit(x);
    };
    for (const layer of this.templateChain(scr).layers) visit(layer.children);
    out.delete('screen');
    return [...out];
  },
  // a widget tree of a template with its "${name}" filled by what the screen gives
  templateFill(v, scr) {
    const args = { screen: scr.name };
    if (scr.template_args && typeof scr.template_args === 'object' && !Array.isArray(scr.template_args))
      for (const [k, x] of Object.entries(scr.template_args)) if (['string', 'number', 'boolean'].includes(typeof x)) args[k] = String(x);
    const fill = x => {
      if (typeof x === 'string') return x.includes('${') ? x.replace(/\$\{([^}]*)\}/g, (m, k) => (Object.prototype.hasOwnProperty.call(args, k) ? args[k] : '')) : x;
      if (Array.isArray(x)) return x.map(fill);
      if (x && typeof x === 'object') { const o = {}; for (const k of Object.keys(x)) o[k] = fill(x[k]); return o; }
      return x;
    };
    return fill(v);
  },
  templateProblems() {
    const out = [];
    this.st.screens.forEach((s, si) => {
      const scr = s.data, loc = { screen: si, path: [] }, say = text => out.push(Object.assign({ text: 'screen "' + scr.name + '": ' + text }, loc));
      const args = scr.template_args;
      if (args !== undefined && scr.template === undefined) say('template_args without a template');
      if (args !== undefined && (!args || typeof args !== 'object' || Array.isArray(args))) say('template_args is { "name": value }');
      if (scr.template === undefined) return;
      const chain = this.templateChain(scr);
      if (chain.problem) { say(chain.problem); return; }
      const given = args && typeof args === 'object' && !Array.isArray(args) ? args : {}, wanted = this.templatePlaceholders(scr);
      for (const k of Object.keys(given)) {
        if (!['string', 'number', 'boolean'].includes(typeof given[k])) say('template_args: "' + k + '" is a text, a number or true / false');
        if (!wanted.includes(k) && k !== 'screen') say('template_args: "' + k + '" is not used by the template');
      }
      for (const k of wanted) if (!(k in given)) say('the template has ${' + k + '}: give it under "template_args" (it is shown as nothing)');
    });
    return out;
  },

  // ---- style: copy the look of one widget onto others ------------------------
  styleOf(w) { const o = {}; for (const k of Object.keys(w || {})) if (/^style/.test(k)) o[k] = U.clone(w[k]); return o; },
  pasteStyle(styles) {
    const sels = this.selection();
    if (!styles || !sels.length) return;
    this.mutate(() => {
      for (const p of sels) {
        const w = this.get(p);
        for (const k of Object.keys(w)) if (/^style/.test(k)) delete w[k];
        Object.assign(w, U.clone(styles));
      }
    });
  },

  // ---- designer-only marks on a widget: "designer": { "locked": true, "hidden": true } ----
  // locked: cannot be clicked or moved on the canvas (a background). hidden: not drawn on the
  // canvas, to reach what is below. The runtime does not look at them.
  flag(w, k) { return !!(w && w.designer && w.designer[k]); },
  setFlag(k, on) {
    const sels = this.selection();
    if (!sels.length) return;
    this.mutate(() => {
      for (const p of sels) {
        const w = this.get(p);
        if (on) { w.designer = w.designer || {}; w.designer[k] = true; }
        else if (w.designer) { delete w.designer[k]; if (!Object.keys(w.designer).length) delete w.designer; }
      }
    });
  },
  anyFlag(k) { let f = false; const walk = w => { if (this.flag(w, k)) f = true; (w.children || []).forEach(walk); }; if (this.screen()) walk(this.screen()); return f; },
  clearFlag(k) {
    this.mutate(() => {
      const walk = w => { if (w.designer) { if (k) delete w.designer[k]; if (!k || !Object.keys(w.designer).length) delete w.designer; } (w.children || []).forEach(walk); };   // no k: all marks
      walk(this.screen());
    });
  },

  // ---- tags as a table of text (to and from a spreadsheet) ----------------------
  exportTags() {
    const cell = v => { const s = v === undefined || v === null ? '' : String(v); return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s; };
    return ['name,type,init,min,max'].concat(((this.st.project && this.st.project.tags) || []).map(t => [t.name, t.type, t.init, t.min, t.max].map(cell).join(','))).join('\n') + '\n';
  },
  // Lines of name, type, init, min, max - separated by commas or tabs (pasted from a
  // spreadsheet); a first line "name,..." is a heading. A tag that exists is updated.
  // Returns { added, updated, errors: ['line 3: ...'] }; nothing is changed when there are errors.
  importTags(text) {
    const res = { added: 0, updated: 0, errors: [] }, rows = [];
    String(text || '').split(/\r?\n/).forEach((line, li) => {
      if (!line.trim()) return;
      const cells = (line.includes('\t') ? line.split('\t') : line.split(',')).map(c => c.trim().replace(/^"(.*)"$/, '$1').replace(/""/g, '"'));
      if (!rows.length && !res.errors.length && /^name$/i.test(cells[0])) return;
      const [name, type0, init, min, max] = cells, type = (type0 || 'int').toLowerCase();
      if (!/^[A-Za-z_][A-Za-z0-9_.]*$/.test(name || '')) { res.errors.push('line ' + (li + 1) + ': "' + (name || '') + '" is not a tag name'); return; }
      if (!SCHEMA.tagTypes.includes(type)) { res.errors.push('line ' + (li + 1) + ': unknown type "' + type0 + '" (' + SCHEMA.tagTypes.join(', ') + ')'); return; }
      const t = { name, type };
      if (init !== undefined && init !== '') t.init = type === 'string' ? init : type === 'bool' ? /^(true|1|on)$/i.test(init) : Number(init);
      if (min !== undefined && min !== '') t.min = Number(min);
      if (max !== undefined && max !== '') t.max = Number(max);
      if ([t.min, t.max].concat(type === 'int' || type === 'float' ? [t.init] : []).some(v => v !== undefined && Number.isNaN(v))) { res.errors.push('line ' + (li + 1) + ': "' + name + '": not a number'); return; }
      rows.push(t);
    });
    if (res.errors.length || !rows.length) return res;
    this.mutate(() => {
      const tags = this.st.project.tags = this.st.project.tags || [];
      for (const t of rows) {
        const old = tags.find(x => x.name === t.name);
        if (old) { for (const k of ['type', 'init', 'min', 'max']) { if (t[k] === undefined) delete old[k]; else old[k] = t[k]; } res.updated++; }
        else { tags.push(t); res.added++; }
      }
    });
    return res;
  },

  // ---- languages and texts ("@id") ---------------------------------------------------
  languages() {
    const p = this.st.project || {};
    if (Array.isArray(p.languages) && p.languages.length) return p.languages.filter(l => typeof l === 'string');
    const out = [];
    for (const t of Object.values(p.texts || {})) for (const l of Object.keys(t || {})) if (!out.includes(l)) out.push(l);
    return out;
  },
  // the language the canvas shows (the designer's choice, else the project's start language)
  previewLanguage() {
    const langs = this.languages(), p = this.st.project || {};
    if (this.st.previewLang && langs.includes(this.st.previewLang)) return this.st.previewLang;
    return langs.includes(p.language) ? p.language : langs[0];
  },
  // "@id" -> the text in the preview language, as the runtime does (texts.cpp)
  // ---- the cells of a "table" widget as one line of text (the grid of the cell editor) ----
  //   a text                    -> "Loader"            (a number stays a number: 5, 2.5)
  //   { tag }                   -> "{count}"
  //   { tag, format }           -> "{temp|%.1f C}"
  //   { tag, map }              -> "{state|0=Stop,1=Run,*=?}"      (* is "default")
  //   { tag, format, map }      -> "{state|%d|0=Stop,1=Run}"
  // A text that itself starts with "{" is written with a backslash in front: "\{not a tag}".
  cellToText(c) {
    if (c === undefined || c === null) return '';
    if (typeof c === 'string') return /^\\*\{/.test(c) ? '\\' + c : c;
    if (typeof c !== 'object') return String(c);
    let s = '{' + (c.tag === undefined ? '' : c.tag);
    if (c.format !== undefined) s += '|' + c.format;
    if (c.map && typeof c.map === 'object') s += '|' + Object.entries(c.map).map(([k, v]) => (k === 'default' ? '*' : k) + '=' + v).join(',');
    return s + '}';
  },
  textToCell(t) {
    const s = String(t === undefined || t === null ? '' : t);
    if (/^\\+\{/.test(s)) return s.slice(1);
    const m = /^\{([^|{}]*)((?:\|[^|{}]*){0,2})\}$/.exec(s.trim());
    if (!m) { const n = Number(s); return s.trim() !== '' && String(n) === s.trim() ? n : s; }
    const cell = { tag: m[1].trim() };
    for (const part of m[2] ? m[2].slice(1).split('|') : []) {
      if (/^[^%=]+=/.test(part)) {
        cell.map = {};
        for (const kv of part.split(',')) { const i = kv.indexOf('='); if (i > 0) cell.map[kv.slice(0, i).trim() === '*' ? 'default' : kv.slice(0, i).trim()] = kv.slice(i + 1); }
      } else if (part !== '') cell.format = part;
    }
    return cell;
  },
  // Can the grid show this cell and give back the very same cell? (A map whose texts hold , | { } or =,
  // or keys the grid does not know: such a cell is left as it is and edited as JSON.)
  cellEditable(c) {
    try {
      const back = this.textToCell(this.cellToText(c)), orig = c === undefined || c === null ? '' : c;
      if (typeof back !== 'object' && typeof orig !== 'object') return String(back) === String(orig);     // ("5" and 5 show the same)
      return JSON.stringify(back) === JSON.stringify(orig);
    } catch (e) { return false; }
  },

  resolveText(s) {
    if (typeof s !== 'string' || s.length < 2 || s[0] !== '@') return s;
    if (s[1] === '@') return s.slice(1);
    const t = ((this.st.project || {}).texts || {})[s.slice(1)];
    if (!t || typeof t !== 'object') return s;
    const lang = this.previewLanguage(), first = this.languages()[0];
    return t[lang] || (first && t[first]) || Object.values(t)[0] || s;
  },
  textIds() { return Object.keys((this.st.project || {}).texts || {}); },
  addText(id) {
    id = String(id || '').trim();
    const p = this.st.project;
    if (!/^[A-Za-z_][A-Za-z0-9_.-]{0,63}$/.test(id) || (p.texts && p.texts[id])) return false;
    this.mutate(() => { p.texts = p.texts || {}; p.texts[id] = {}; });
    return true;
  },
  // rename an id: every "@old" in the screens, alarm messages and widget labels follows
  renameText(oldId, id) {
    id = String(id || '').trim();
    const p = this.st.project, texts = p.texts || {};
    if (oldId === id) return true;
    if (!/^[A-Za-z_][A-Za-z0-9_.-]{0,63}$/.test(id) || texts[id] || !texts[oldId]) return false;
    this.mutate(() => {
      const t = {};
      for (const [k, v] of Object.entries(texts)) t[k === oldId ? id : k] = v;      // (keeps the order)
      p.texts = t;
      this.eachTextRef((s, set) => { if (s === '@' + oldId) set('@' + id); });
    });
    return true;
  },
  // every place a text may be "@id": cb(text, set, loc)
  eachTextRef(cb) {
    const st = this.st, p = st.project || {};
    const str = (o, k, loc) => { if (o && typeof o[k] === 'string') cb(o[k], v => { o[k] = v; }, loc); };
    st.screens.forEach((s, si) => {
      const visit = (w, path) => {
        const loc = { screen: si, path };
        str(w, 'text', loc); str(w, 'placeholder', loc);
        if (Array.isArray(w.options)) w.options.forEach((o, i) => { if (typeof o === 'string') cb(o, v => { w.options[i] = v; }, loc); });
        if (w.labels && typeof w.labels === 'object') for (const k of Object.keys(w.labels)) str(w.labels, k, loc);
        (w.children || []).forEach((c, i) => visit(c, path.concat(i)));
      };
      visit(s.data, []);
    });
    for (const a of (p.alarms && p.alarms.items) || []) str(a, 'message', { view: 'logs' });
  },
  textProblems() {
    const out = [], p = this.st.project || {}, texts = p.texts || {}, langs = this.languages();
    for (const [id, t] of Object.entries(texts)) {
      const missing = langs.filter(l => !t || typeof t[l] !== 'string' || t[l] === '');
      if (missing.length) out.push({ text: 'text "' + id + '" has no ' + missing.join(', '), view: 'texts' });
      // a translation much longer than the first language may not fit where the first one does
      const base = t && t[langs[0]];
      if (base) for (const l of langs.slice(1)) {
        const v = t[l];
        if (typeof v === 'string' && U.textWidth(v) > U.textWidth(base) * 1.5 + 6 /* (room: short words differ a lot) */) out.push({ text: 'text "' + id + '" in ' + l + ' is much longer than in ' + langs[0] + ': it may not fit', view: 'texts' });
      }
    }
    this.eachTextRef((s, set, loc) => {
      if (typeof s === 'string' && /^@[A-Za-z_][A-Za-z0-9_.-]*$/.test(s) && !texts[s.slice(1)]) out.push(Object.assign({ text: 'no text "' + s.slice(1) + '" (shown as "' + s + '")' }, loc));
    });
    if (p.language && langs.length && !langs.includes(p.language)) out.push({ text: 'the start language "' + p.language + '" is not one of the languages', view: 'texts' });
    return out;
  },
  textsCsv() {
    const langs = this.languages(), texts = (this.st.project || {}).texts || {};
    const cell = v => { let s = v === undefined || v === null ? '' : String(v); if (/^[=+\-@\t\r]/.test(s) && !/^[+-]?\d+(\.\d+)?$/.test(s)) s = "'" + s; return /[",\n\r]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s; };
    return [['id'].concat(langs).map(cell).join(',')].concat(Object.entries(texts).map(([id, t]) => [id].concat(langs.map(l => (t || {})[l])).map(cell).join(','))).join('\n') + '\n';
  },
  // replace the texts with a table (first line: id,<language>...); new languages are added. { count, errors }
  importTextsCsv(text) {
    const res = { count: 0, errors: [] }, rows = U.parseTable(text);
    if (!rows.length || rows[0][0] !== 'id') { res.errors.push('the first line must be: id,<language>,<language>...'); return res; }
    const langs = rows[0].slice(1).map(s => s.trim());
    for (const l of langs) if (!/^[A-Za-z0-9_-]{1,16}$/.test(l)) res.errors.push('"' + l + '" is not a language code (letters, digits, - and _)');
    const texts = {};
    rows.slice(1).forEach((r, i) => {
      const id = String(r[0] || '').trim();
      if (!/^[A-Za-z_][A-Za-z0-9_.-]{0,63}$/.test(id)) { res.errors.push('line ' + (i + 2) + ': "' + id + '" is not an id'); return; }
      if (texts[id]) { res.errors.push('line ' + (i + 2) + ': "' + id + '" twice'); return; }
      const t = {};
      langs.forEach((l, k) => { let v = r[k + 1]; if (v === undefined || v === '') return; if (v[0] === "'") v = v.slice(1); t[l] = v; });
      texts[id] = t;
    });
    if (res.errors.length) return res;
    this.mutate(() => {
      const p = this.st.project;
      p.texts = texts;
      const all = this.languages().concat(langs.filter(l => !this.languages().includes(l)));
      p.languages = Array.from(new Set((Array.isArray(p.languages) ? p.languages : []).concat(all)));
    });
    res.count = Object.keys(texts).length;
    return res;
  },

  // ---- recipes ---------------------------------------------------------------------
  recipeBooks() { const r = this.st.project && this.st.project.recipes; return r && Array.isArray(r.books) ? r.books : []; },
  validRecipeName(n) { return typeof n === 'string' && n.length >= 1 && n.length <= 32 && !/[\x00-\x1f\x7f]/.test(n) && n.trim() === n; },
  validBookName(n) { return this.validRecipeName(n) && !/[\/\\:*?"<>|.]/.test(n); },
  addRecipeBook() {
    const p = this.st.project, books = this.recipeBooks();
    let n = 1;
    while (books.some(b => b.name === 'book' + n)) n++;
    const name = 'book' + n;
    this.mutate(() => {
      p.recipes = p.recipes || {};
      p.recipes.books = p.recipes.books || [];
      p.recipes.books.push({ name, items: this.tagNames().slice(0, 3), recipes: [] });
    });
    return name;
  },
  renameRecipeBook(i, name) {
    name = String(name || '').trim();
    const books = this.recipeBooks(), b = books[i];
    if (!b || !this.validBookName(name) || books.some((x, j) => j !== i && x.name === name)) return false;
    const old = b.name;
    this.mutate(() => {
      b.name = name;
      // recipe tables on the screens follow
      const walk = w => { if (w.type === 'recipe_table' && w.book === old) w.book = name; (w.children || []).forEach(walk); };
      this.st.screens.forEach(s => walk(s.data));
    });
    return true;
  },
  addRecipe(bi) {
    const b = this.recipeBooks()[bi];
    if (!b) return;
    b.recipes = b.recipes || [];
    let n = b.recipes.length + 1;
    while (b.recipes.some(r => r.name === 'Recipe ' + n)) n++;
    this.mutate(() => b.recipes.push({ name: 'Recipe ' + n, values: {} }));
  },
  // text from a cell: as the tag's type ('' removes the value)
  setRecipeValue(bi, ri, tag, text) {
    const b = this.recipeBooks()[bi], r = b && b.recipes[ri];
    if (!r) return;
    const t = ((this.st.project.tags) || []).find(x => x.name === tag), s = String(text).trim();
    this.mutate(() => {
      r.values = r.values || {};
      if (s === '') { delete r.values[tag]; return; }
      if (t && t.type === 'string') r.values[tag] = s;
      else if (t && t.type === 'bool') r.values[tag] = /^(true|1|on)$/i.test(s);
      else r.values[tag] = Number.isFinite(Number(s)) ? Number(s) : s;      // (kept as typed: the problems list says what is wrong)
    });
  },
  // what the runtime would refuse
  recipeProblems() {
    const out = [], tags = (this.st.project && this.st.project.tags) || [];
    const books = this.recipeBooks(), seen = new Set();
    books.forEach((b, bi) => {
      const at = { view: 'recipes', index: bi }, bn = b.name || '';
      if (!this.validBookName(bn)) out.push(Object.assign({ text: 'recipe book "' + bn + '": 1-32 characters, no / \\ : * ? " < > | .' }, at));
      if (seen.has(bn)) out.push(Object.assign({ text: 'recipe book "' + bn + '" is defined twice' }, at));
      seen.add(bn);
      const names = new Set();
      for (const r of Array.isArray(b.recipes) ? b.recipes : []) {
        if (!this.validRecipeName(r.name)) out.push(Object.assign({ text: 'recipe book "' + bn + '": a recipe name is 1 to 32 characters' }, at));
        else if (names.has(r.name)) out.push(Object.assign({ text: 'recipe book "' + bn + '": recipe "' + r.name + '" is defined twice' }, at));
        names.add(r.name);
        for (const [k, v] of Object.entries(r.values || {})) {
          const t = tags.find(x => x.name === k);
          if (!t) continue;
          if (t.type !== 'string' && t.type !== 'bool' && typeof v !== 'number') out.push(Object.assign({ text: 'recipe "' + r.name + '": ' + k + ' is not a number' }, at));
          else if (typeof v === 'number' && ((t.min !== undefined && v < t.min) || (t.max !== undefined && v > t.max)))
            out.push(Object.assign({ text: 'recipe "' + r.name + '": ' + k + ' = ' + v + ' is outside ' + (t.min === undefined ? '' : t.min) + '..' + (t.max === undefined ? '' : t.max) }, at));
        }
      }
    });
    const level = this.st.project && this.st.project.recipes && U.num(this.st.project.recipes.level, 0);
    if (level > 0 && !this.usersOn()) out.push({ text: 'recipes: a level is set, but user management is off (Users tab)', view: 'recipes' });
    // trends need a data log group that exists, and tags that it records
    const groups = (this.st.project && this.st.project.data_log && this.st.project.data_log.groups) || [];
    this.st.screens.forEach((s, si) => {
      const walk = (w, path) => {
        if (w.type === 'trend') {
          const g = groups.find(x => x.name === w.group);
          if (!g) out.push({ text: 'trend: no data log group "' + (w.group || '') + '"', screen: si, path });
          else for (const t of Array.isArray(w.tags) ? w.tags : []) {
            const n = typeof t === 'string' ? t : t && t.tag;
            if (n && !(g.tags || []).includes(n)) out.push({ text: 'trend: data log group "' + g.name + '" does not record "' + n + '"', screen: si, path });
          }
        }
        (w.children || []).forEach((c, i) => walk(c, path.concat(i)));
      };
      walk(s.data, []);
    });
    // recipe tables need a book that exists
    this.st.screens.forEach((s, si) => {
      const walk = (w, path) => {
        if (w.type === 'recipe_table' && !books.some(b => b.name === w.book)) out.push({ text: 'recipe table: no recipe book "' + (w.book || '') + '"', screen: si, path });
        (w.children || []).forEach((c, i) => walk(c, path.concat(i)));
      };
      walk(s.data, []);
    });
    return out;
  },
  recipesCsv(bi) {
    const b = this.recipeBooks()[bi];
    if (!b) return '';
    const cell = v => { let s = v === undefined || v === null ? '' : String(v); if (/^[=+\-@\t\r]/.test(s) && !/^[+-]?\d+(\.\d+)?([eE][+-]?\d+)?$/.test(s)) s = "'" + s; return /[",\n\r]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s; };
    const items = b.items || [];
    return [['name'].concat(items).map(cell).join(',')].concat((b.recipes || []).map(r => [r.name].concat(items.map(t => (r.values || {})[t])).map(cell).join(','))).join('\n') + '\n';
  },
  // replace the recipes of a book with lines of a table (first line: name,<tag>...). { count, errors }
  importRecipesCsv(bi, text) {
    const b = this.recipeBooks()[bi], res = { count: 0, errors: [] };
    if (!b) return res;
    const rows = [];
    let row = [], cell = '', q = false;
    const src = String(text || '').replace(/^\uFEFF/, '');
    for (let i = 0; i < src.length; i++) {
      const c = src[i];
      if (q) { if (c === '"' && src[i + 1] === '"') { cell += '"'; i++; } else if (c === '"') q = false; else cell += c; continue; }
      if (c === '"') q = true;
      else if (c === ',' || c === '\t') { row.push(cell); cell = ''; }
      else if (c === '\r') continue;
      else if (c === '\n') { row.push(cell); if (row.some(x => x !== '')) rows.push(row); row = []; cell = ''; }
      else cell += c;
    }
    row.push(cell); if (row.some(x => x !== '')) rows.push(row);
    if (!rows.length || rows[0][0] !== 'name') { res.errors.push('the first line must be: name,<tag>,<tag>...'); return res; }
    const cols = rows[0].slice(1);
    for (const c of cols) if (!(b.items || []).includes(c)) res.errors.push('column "' + c + '" is not an item of the book');
    const list = [], names = new Set();
    rows.slice(1).forEach((r, i) => {
      const name = String(r[0] || '').replace(/^'/, '');
      if (!this.validRecipeName(name)) res.errors.push('line ' + (i + 2) + ': a recipe name is 1 to 32 characters');
      else if (names.has(name)) res.errors.push('line ' + (i + 2) + ': recipe "' + name + '" twice');
      names.add(name);
      const values = {};
      cols.forEach((c, ci) => {
        let v = r[ci + 1];
        if (v === undefined || v === '') return;
        v = v.replace(/^'/, '');
        const t = ((this.st.project.tags) || []).find(x => x.name === c);
        if (t && t.type === 'string') values[c] = v;
        else if (t && t.type === 'bool') values[c] = /^(true|1|on)$/i.test(v);
        else if (Number.isFinite(Number(v))) values[c] = Number(v);
        else res.errors.push('line ' + (i + 2) + ': ' + c + ' "' + v + '" is not a number');
      });
      list.push({ name, values });
    });
    if (res.errors.length) return res;
    this.mutate(() => { b.recipes = list; });
    res.count = list.length;
    return res;
  },

  // ---- moving widgets in the tree ----------------------------------------------
  pathOf(obj) {
    let found = null;
    const walk = (w, path) => { if (found) return; if (w === obj) { found = path; return; } (w.children || []).forEach((c, i) => walk(c, path.concat(i))); };
    if (this.screen()) walk(this.screen(), []);
    return found;
  },
  // Move widgets (of one parent) so that they become children of the widget at destPath, at
  // index (undefined = at the end). False when that is not possible (into itself, not a container).
  moveWidgets(paths, destPath, index) {
    if (!paths || !paths.length) return false;
    const src = this.get(paths[0].slice(0, -1)), dest = this.get(destPath);
    if (!src || !dest || !(this.isContainer(dest) || !destPath.length)) return false;
    const idx = paths.map(p => p[p.length - 1]).sort((x, y) => x - y), moved = idx.map(i => src.children[i]);
    if (moved.some(w => !w)) return false;
    const inside = (w, target) => w === target || (w.children || []).some(c => inside(c, target));
    if (moved.some(w => inside(w, dest))) return false;
    this.mutate(() => {
      dest.children = dest.children || [];
      let at = index === undefined ? dest.children.length : Math.max(0, Math.min(index, dest.children.length));
      if (src === dest) at -= idx.filter(i => i < at).length;
      for (const i of idx.slice().reverse()) src.children.splice(i, 1);
      dest.children.splice(at, 0, ...moved);
      const dp = this.pathOf(dest);
      this.st.sel = dp.concat(at); this.st.more = moved.slice(1).map((w, k) => dp.concat(at + 1 + k));
    });
    return true;
  },
  // 'front': drawn on top of its siblings (last), 'back': below them (first)
  orderSelection(where) {
    const sels = this.selection();
    if (!sels.length) return;
    const cp = sels[0].slice(0, -1);
    this.moveWidgets(sels, cp, where === 'front' ? undefined : 0);
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

    const seenTag = new Set();
    (p.tags || []).forEach((t, i) => {
      if (!t.name) out.push({ text: 'a tag has no name', view: 'tags', index: i });
      else if (seenTag.has(t.name)) out.push({ text: 'tag "' + t.name + '" is defined twice', view: 'tags', index: i });
      seenTag.add(t.name);
    });
    this.eachTagRef((name, set, loc, what, soft) => { if (!soft && !okTag(name)) out.push(Object.assign({ text: 'unknown tag "' + name + '" (' + what + ')' }, loc)); });
    // mail: a server, addresses that are addresses; no password in the project file
    if (p.mail !== undefined) {
      const loc = { view: 'project' }, m = p.mail, say = text => out.push(Object.assign({ text: 'mail: ' + text }, loc));
      const addr = a => typeof a === 'string' && a.length <= 254 && /^[^\s<>,;:"()\[\]\\@\x00-\x20\x7f-\uffff]+@[^\s<>,;:"()\[\]\\@\x00-\x20\x7f-\uffff]+$/.test(a) && !/\.\.|@\.|\.$/.test(a);
      if (!m || typeof m !== 'object' || Array.isArray(m)) say('an object is needed');
      else {
        if (!m.host || typeof m.host !== 'string') say('no mail server (host)');
        else if (!/^[A-Za-z0-9.\-:\[\]]{1,253}$/.test(m.host)) say('"host" is not the name of a server');
        if (m.security !== undefined && !['starttls', 'tls', 'none'].includes(m.security)) say('"security" is starttls, tls or none');
        if (m.port !== undefined && !(Number.isInteger(m.port) && m.port >= 1 && m.port <= 65535)) say('"port" is 1 to 65535');
        if (!addr(m.from)) say('"from" is the mail address of the sender');
        const to = Array.isArray(m.to) ? m.to : typeof m.to === 'string' ? m.to.split(/[\s,;]+/).filter(Boolean) : null;
        if (!to || to.some(a => !addr(a)) || to.length > 20) say('"to" is a mail address, or a list of them (20 at most)');
        else if (!to.length) say('no recipient (to)');
        if (typeof m.password === 'string' && m.password) say('the password is in the project file: everybody who gets the project can read it. Set it on the device instead (System > Mail)');
        if (m.security === 'none') say('"security": "none": the mails can be read on the network');
        if (m.verify === false) say('"verify": false: the certificate of the server is not checked');
      }
    }
    // transfers: tags, a trigger that is one, no rings
    const xfers = Array.isArray(p.transfers) ? p.transfers : [], xferNames = new Set(), xferNext = new Map();
    for (const t of xfers) if (t && typeof t === 'object' && typeof t.from === 'string' && typeof t.to === 'string') { if (!xferNext.has(t.from)) xferNext.set(t.from, []); xferNext.get(t.from).push(t.to); }
    const xferRing = t => { const seen = new Set(), todo = [t.to]; while (todo.length) { const g = todo.pop(); if (g === t.from) return true; if (seen.has(g)) continue; seen.add(g); for (const n of xferNext.get(g) || []) todo.push(n); } return false; };
    if (p.transfers !== undefined && !Array.isArray(p.transfers)) out.push({ text: 'transfers: a list is needed', view: 'project' });
    xfers.forEach((t, i) => {
      const loc = { view: 'project', index: i }, n = t && t.name ? '"' + t.name + '"' : '#' + (i + 1), bad = text => out.push(Object.assign({ text: 'transfer ' + n + ': ' + text }, loc));
      if (!t || typeof t !== 'object' || Array.isArray(t)) { out.push(Object.assign({ text: 'transfer ' + n + ' is not a transfer' }, loc)); return; }
      if (!t.name) out.push(Object.assign({ text: 'a transfer has no name' }, loc));
      else if (xferNames.has(t.name)) bad('the name is used twice');
      xferNames.add(t.name);
      if (typeof t.from !== 'string' || !t.from) bad('no tag to take the value from');
      if (typeof t.to !== 'string' || !t.to) bad('no tag to write the value to');
      if (typeof t.from !== 'string' || typeof t.to !== 'string' || !t.from || !t.to) return;
      if (t.from === t.to) bad('"from" and "to" are the same tag');
      else if (/^sys\./.test(t.to)) bad('"to" is a tag of the runtime (sys.*): a transfer does not write those');
      else if (xferRing(t)) bad('part of a ring of transfers (' + t.from + ' -> ' + t.to + ' -> ... -> ' + t.from + '): not run');
      if (t.trigger !== undefined && t.trigger !== 'change' && t.trigger !== 'cyclic') bad('"trigger" is "change" or "cyclic"');
      if (t.every_ms !== undefined && !(typeof t.every_ms === 'number' && t.every_ms >= 100 && t.every_ms <= 86400000)) bad('"every_ms" is 100 to 86400000');
      if (t.every_ms !== undefined && t.trigger !== 'cyclic') bad('"every_ms" has no meaning without the trigger "cyclic"');
      for (const k of ['scale', 'offset']) if (t[k] !== undefined && !(typeof t[k] === 'number' && isFinite(t[k]))) bad('"' + k + '" is a number');
      if (t.out_of_range !== undefined && t.out_of_range !== 'skip' && t.out_of_range !== 'clamp') bad('"out_of_range" is "skip" or "clamp"');
      if (t.when !== undefined && !(typeof t.when === 'string' && t.when) && !(t.when && typeof t.when === 'object' && !Array.isArray(t.when) && typeof t.when.tag === 'string' && t.when.tag &&
          Object.keys(t.when).filter(k => k !== 'tag').length <= 1 && Object.keys(t.when).every(k => ['tag', 'eq', 'ne', 'gt', 'lt', 'ge', 'le'].includes(k)))) bad('"when" is a tag, or { "tag": ..., "eq": ... } (eq ne gt lt ge le)');
    });
    for (const pr of this.templateProblems()) out.push(pr);
    // sound: an output that exists, sounds that can be played
    if (p.sound !== undefined) {
      const loc = { view: 'project' }, so = p.sound;
      if (!so || typeof so !== 'object' || Array.isArray(so)) out.push(Object.assign({ text: 'sound: an object is needed' }, loc));
      else {
        if (so.output !== undefined && !['auto', 'audio', 'gpio', 'none'].includes(so.output)) out.push(Object.assign({ text: 'sound: output "' + so.output + '" is not auto, audio, gpio or none' }, loc));
        if (so.output === 'gpio' && so.gpio === undefined) out.push(Object.assign({ text: 'sound: output "gpio" needs the GPIO line of the buzzer' }, loc));
        if (so.gpio !== undefined && !(Number.isInteger(so.gpio) && so.gpio >= 0 && so.gpio <= 511)) out.push(Object.assign({ text: 'sound: gpio is the number of a GPIO line (0..511)' }, loc));
        if (so.volume !== undefined && !(typeof so.volume === 'number' && so.volume >= 0 && so.volume <= 100)) out.push(Object.assign({ text: 'sound: volume is 0 to 100' }, loc));
        if (so.alarm_every_s !== undefined && !(typeof so.alarm_every_s === 'number' && so.alarm_every_s >= 0)) out.push(Object.assign({ text: 'sound: alarm_every_s is a number of seconds (0 = once)' }, loc));
        for (const k of ['touch', 'alarm']) {
          const v = so[k];
          if (v === undefined || typeof v === 'boolean' || v === '' || ['click', 'beep', 'alarm'].includes(v)) continue;
          const n = typeof v === 'string' ? v.replace(/\\/g, '/') : '';
          if (!n || n.length > 200 || /^\/|:|[\x00-\x1f]/.test(n) || n.split('/').some(part => part === '..' || part === '.') || !/.\.wav$/i.test(n))
            out.push(Object.assign({ text: 'sound: ' + k + ' "' + v + '" is no built-in sound (click, beep, alarm) and no .wav file inside the project folder' }, loc));
        }
      }
    }
    // schedules: a name, a time that is one, days that are days, something to do
    const schedNames = new Set(), dayNames = ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'daily', 'weekdays', 'weekend', 'all'];
    (Array.isArray(p.schedules) ? p.schedules : []).forEach((sc, i) => {
      const loc = { view: 'project', index: i }, n = sc && sc.name ? '"' + sc.name + '"' : '#' + (i + 1);
      if (!sc || typeof sc !== 'object') { out.push(Object.assign({ text: 'schedule ' + n + ' is not a schedule' }, loc)); return; }
      if (!sc.name) out.push(Object.assign({ text: 'a schedule has no name' }, loc));
      else if (schedNames.has(sc.name)) out.push(Object.assign({ text: 'schedule ' + n + ' is defined twice' }, loc));
      schedNames.add(sc.name);
      const at = String(sc.at === undefined ? '' : sc.at).trim(), daily = /^([01]\d|2[0-3]):[0-5]\d$/.test(at), once = /^\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])[ T]([01]\d|2[0-3]):[0-5]\d$/.test(at);
      if (!daily && !once) out.push(Object.assign({ text: 'schedule ' + n + ': "at" is HH:MM, or YYYY-MM-DD HH:MM for once' }, loc));
      const days = sc.days === undefined ? [] : Array.isArray(sc.days) ? sc.days : String(sc.days).split(/[ ,]+/).filter(Boolean);
      const bad = days.filter(d => !dayNames.includes(String(d).toLowerCase()));
      if (bad.length) out.push(Object.assign({ text: 'schedule ' + n + ': "' + bad[0] + '" is not a day (mon tue wed thu fri sat sun)' }, loc));
      if (sc.days !== undefined && !days.length) out.push(Object.assign({ text: 'schedule ' + n + ': no day is named' }, loc));
      const acts = Array.isArray(sc.do) ? sc.do : sc.do === undefined ? [] : [sc.do];
      if (!acts.length || acts.some(a => !a || typeof a !== 'object' || !SCHEMA.actions[a.action])) out.push(Object.assign({ text: 'schedule ' + n + ': "do" is an action of a screen, or a list of them' }, loc));
      for (const a of acts) {
        if (a && (a.action === 'goto' || a.action === 'popup') && !screens.has(a.screen)) out.push(Object.assign({ text: 'schedule ' + n + ': unknown screen "' + a.screen + '"' }, loc));
        if (a && a.action === 'script' && a.name && !scripts.has(a.name)) out.push(Object.assign({ text: 'schedule ' + n + ': unknown script "' + a.name + '"' }, loc));
      }
    });

    if (p.start_screen && !screens.has(p.start_screen)) out.push({ text: 'start screen "' + p.start_screen + '" does not exist', view: 'project' });
    const startPopup = st.screens.find(x => x.data.name === p.start_screen && x.data.popup);
    if (startPopup) out.push({ text: 'the start screen "' + p.start_screen + '" is a popup', view: 'project' });
    const seenScr = new Set();
    st.screens.forEach((s, si) => {
      if (seenScr.has(s.data.name)) out.push({ text: 'screen "' + s.data.name + '" is defined twice', screen: si, path: [] });
      seenScr.add(s.data.name);
      const visit = (w, path) => {
        const at = { screen: si, path };
        for (const [k, v] of Object.entries(w)) {
          if (!/^on_/.test(k) || !v) continue;
          for (const a of Array.isArray(v) ? v : [v]) {
            if (!a || typeof a !== 'object') continue;
            if (a.action === 'goto' && a.screen && !screens.has(a.screen) && !/^__/.test(a.screen)) out.push(Object.assign({ text: 'unknown screen "' + a.screen + '" (goto)' }, at));
            if ((a.action === 'popup' || a.action === 'close') && a.screen) {
              const target = st.screens.find(x => x.data.name === a.screen);
              if (!target) out.push(Object.assign({ text: 'unknown screen "' + a.screen + '" (' + a.action + ')' }, at));
              else if (!target.data.popup) out.push(Object.assign({ text: 'screen "' + a.screen + '" is not a popup (' + a.action + ')' }, at));
            }
            if (a.action === 'popup' && !a.screen) out.push(Object.assign({ text: 'popup without a screen' }, at));
            if (a.action === 'script' && a.name && !scripts.has(a.name)) out.push(Object.assign({ text: 'unknown script "' + a.name + '"' }, at));
            if ((a.action === 'login' || a.action === 'logout') && !usersOn) out.push(Object.assign({ text: 'action "' + a.action + '" needs user management (Users tab)' }, at));
          }
        }
        if (U.num(w.level, 0) > 0 && !usersOn) out.push(Object.assign({ text: 'a level is set, but user management is off (Users tab)' }, at));
        for (const t of this.animateProblems(w, tags)) out.push(Object.assign({ text: 'animate: ' + t }, at));
        (w.children || []).forEach((c, i) => visit(c, path.concat(i)));
      };
      visit(s.data, []);
    });
    (p.drivers || []).forEach((d, i) => {
      if ((d.type === 'modbus_rtu' || d.type === 'modbus_ascii') && !d.device && !d.host) out.push({ text: (d.name || d.type) + ': no serial port (device) is set', view: 'drivers', index: i });
      if (d.type === 'opcua_client') {
        const n = d.name || d.type, at = { view: 'drivers', index: i };
        if (!/^opc\.tcp:\/\//.test(d.endpoint || '')) out.push(Object.assign({ text: n + ': the endpoint is not opc.tcp://host:port' }, at));
        if (d.security === 'none') out.push(Object.assign({ text: n + ': no security: values can be read and changed on the network' }, at));
        else if (d.verify === false) out.push(Object.assign({ text: n + ': the server\'s certificate is not checked (verify off)' }, at));
        else if (!d.server_cert) out.push(Object.assign({ text: n + ': no server certificate to trust (server_cert); put one in pki/trusted/ on the device, or it does not connect' }, at));
        if (d.password) out.push(Object.assign({ text: n + ': the password is in project.json; better enter it on the device (System > Driver)' }, at));
        if (!!d.cert_file !== !!d.key_file) out.push(Object.assign({ text: n + ': cert_file and key_file go together' }, at));
      }
      if (d.type === 'mqtt') {
        const n = d.name || d.type, at = { view: 'drivers', index: i };
        if (!d.host) out.push(Object.assign({ text: n + ': no host is set' }, at));
        if (d.tls === false) out.push(Object.assign({ text: n + ': no TLS: user name, password and values can be read on the network' }, at));
        else if (d.verify === false) out.push(Object.assign({ text: n + ': the broker\'s certificate is not checked (verify off)' }, at));
        if (d.password) out.push(Object.assign({ text: n + ': the password is in project.json; better enter it on the device (System > Driver)' }, at));
        for (const key of ['publish', 'subscribe']) {
          for (const row of Array.isArray(d[key]) ? d[key] : []) {
            const t = (typeof row === 'string' ? '' : (row && row.topic) || '');
            if (/[+#]/.test(t)) out.push(Object.assign({ text: n + ': "' + t + '": wildcards (+ #) are not allowed' }, at));
          }
        }
      }
      if (d.type === 'fins') {
        if (!d.host) out.push({ text: (d.name || d.type) + ': no host is set', view: 'drivers', index: i });
        for (const row of Array.isArray(d.map) ? d.map : []) {
          if (row && row.addr !== undefined && !U.finsAddress(row.addr)) out.push({ text: (d.name || d.type) + ': "' + row.addr + '" is not a device address', view: 'drivers', index: i });
        }
      }
      if (d.type === 'slmp') {
        if (!d.host) out.push({ text: (d.name || d.type) + ': no host is set', view: 'drivers', index: i });
        for (const row of Array.isArray(d.map) ? d.map : []) {
          if (row && row.addr !== undefined && !U.slmpAddress(row.addr)) out.push({ text: (d.name || d.type) + ': "' + row.addr + '" is not a device address', view: 'drivers', index: i });
        }
      }
    });
    (p.scripts || []).forEach((s, i) => {
      if (/^screen_/.test(s.trigger || '') && s.screen && !screens.has(s.screen)) out.push({ text: 'script "' + (s.name || '') + '": unknown screen "' + s.screen + '"', view: 'scripts', index: i });
    });
    if (p.alarms && U.num(p.alarms.ack_level, 0) > 0 && !usersOn) out.push({ text: 'alarms: ack_level is set, but user management is off (Users tab)', view: 'logs' });
    // Japanese, Chinese, Korean text and no font for it: the built-in font has Latin letters only
    if (!(p.font && p.font.file)) {
      const cjk = /[\u3000-\u30ff\u3400-\u9fff\uac00-\ud7af\uff00-\uffef]/;
      const where = cjk.test(JSON.stringify(p.texts || {})) ? 'the texts' : st.screens.some(s => cjk.test(JSON.stringify(s.data))) ? 'a screen'
                  : cjk.test(JSON.stringify(p.alarms || {})) ? 'the alarms' : '';
      if (where) out.push({ text: 'Japanese text is used (' + where + '), but the project has no font for it: it is shown as empty boxes. Project tab > Font', view: 'screen' });
    }
    for (const w of this.userProblems()) out.push({ text: w, view: 'users' });
    for (const r of this.recipeProblems()) out.push(r);
    for (const t of this.textProblems()) out.push(t);
    return out;
  },

  // What is wrong in a widget's "animate" (the same checks as the runtime's --check).
  animateProblems(w, tags) {
    const out = [], A = SCHEMA.animate;
    if (w.animate === undefined) return out;
    for (let a of Array.isArray(w.animate) ? w.animate : [w.animate]) {
      if (typeof a === 'string') a = { preset: a };
      if (!a || typeof a !== 'object' || Array.isArray(a)) { out.push('an animation is an object'); continue; }
      for (const k of Object.keys(a)) if (!A.keys.includes(k)) out.push('unknown key "' + k + '"');
      if (a.preset !== undefined && !A.presets.includes(a.preset)) { out.push('unknown preset "' + a.preset + '"'); continue; }
      const prop = a.prop !== undefined ? a.prop : { blink: 'opa', pulse: 'opa', spin: 'rotation' }[a.preset];
      if (prop === undefined) { out.push('no "prop" (or "preset")'); continue; }
      if (!A.props.includes(prop)) { out.push('unknown prop "' + prop + '"'); continue; }
      if (a.preset === undefined && prop !== 'frames' && a.to === undefined) out.push('"' + prop + '" needs "to"');
      if (prop === 'frames') {
        if (w.type !== 'image') out.push('"frames" is for image widgets');
        if (!Array.isArray(a.files) || a.files.length < 2) out.push('"frames" needs "files" with two pictures or more');
      }
      if (a.path !== undefined && !A.paths.includes(a.path)) out.push('unknown path "' + a.path + '"');
      const flash = ['opa', 'bg_color', 'text_color'].includes(prop), time = a.time === undefined ? (a.preset === 'blink' ? 500 : a.preset === 'pulse' ? 700 : 1000) : Number(a.time);
      const playback = a.playback !== undefined ? !!a.playback : (a.preset === 'blink' || a.preset === 'pulse');
      if (flash && time < (playback ? 167 : 334)) out.push(time + ' ms flashes faster than 3 times a second (the runtime slows it down)');
      if (a.when !== undefined) {
        const t = typeof a.when === 'string' ? a.when : (a.when && a.when.tag);
        if (!t) out.push('"when" names no tag');
        else if (!tags.has(t) && !/^sys\./.test(t)) out.push('unknown tag "' + t + '" (when)');
      }
    }
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
  // (template: the name of a screen the new one is built on)
  addScreen(name, template) {
    name = String(name || '').trim().replace(/[^A-Za-z0-9_\-]/g, '_');
    if (!name || this.screenNames().includes(name)) return false;
    this.mutate(() => {
      const data = this.blankScreen(name);
      if (template && this.screenNames().includes(template)) {
        // (what the template says about the screen itself counts unless the screen says it too: say nothing)
        delete data.style; delete data.scrollable;
        data.template = template;
      }
      this.st.screens.push({ file: 'screens/' + name + '.json', data });
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
      for (const o of this.st.screens) if (o.data.template === old) o.data.template = name;
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
