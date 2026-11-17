// Left side panels (widget palette, screens, project settings) and the center views
// (screen canvas / tag list / drivers). Global `Panels`.
'use strict';

const Panels = {
  tab: 'widgets', view: 'screen', driverSel: 0, scriptSel: 0, tagFilter: '',

  init() {
    document.querySelectorAll('#left-tabs button').forEach(b => b.addEventListener('click', () => this.show(b.dataset.tab)));
    document.querySelectorAll('#center-tabs button').forEach(b => b.addEventListener('click', () => this.showView(b.dataset.view)));
    this.show('widgets');
  },
  // center area: 'screen' (canvas), 'tags', 'drivers' or 'scripts'
  showView(v) {
    if (this.view === v) return;
    this.view = v;
    document.querySelectorAll('#center-tabs button').forEach(b => b.classList.toggle('active', b.dataset.view === v));
    document.getElementById('canvas-wrap').hidden = v !== 'screen';
    document.getElementById('view-tags').hidden = v !== 'tags';
    document.getElementById('view-drivers').hidden = v !== 'drivers';
    document.getElementById('view-scripts').hidden = v !== 'scripts';
    document.getElementById('right').hidden = v !== 'screen';   // widget tree / props only matter on the canvas
    App.refresh();
  },
  show(tab) {
    this.tab = tab;
    document.querySelectorAll('#left-tabs button').forEach(b => b.classList.toggle('active', b.dataset.tab === tab));
    document.querySelectorAll('.tab-body').forEach(d => { d.hidden = d.id !== 'tab-' + tab; });
    this.render();
  },

  render() {
    this.renderWidgets();
    this.renderScreens();
    this.renderTags();
    this.renderDrivers();
    this.renderScripts();
    this.renderProject();
    const dl = document.getElementById('dl-tags'); dl.innerHTML = '';
    for (const t of M.tagNames()) dl.append(U.el('option', { value: t }));
  },

  renderWidgets() {
    const el = document.getElementById('tab-widgets'); el.innerHTML = '';
    el.append(U.el('div', { class: 'hint' }, 'Click to add to the selected panel (or the screen).'));
    const grid = U.el('div', { class: 'palette' });
    for (const [type, t] of Object.entries(SCHEMA.types)) {
      grid.append(U.el('button', { class: 'pal', onclick: () => App.addWidget(type), disabled: !M.screen() }, U.el('span', { class: 'tr-icon' }, Tree.icon({ type })), t.label));
    }
    el.append(grid);
  },

  renderScreens() {
    const el = document.getElementById('tab-screens'); el.innerHTML = '';
    if (!M.st.project) return;
    const list = U.el('div', { class: 'list' });
    M.st.screens.forEach((s, i) => {
      const isStart = M.st.project.start_screen === s.data.name;
      list.append(U.el('div', { class: 'item' + (i === M.st.cur ? ' sel' : ''), onclick: () => { Panels.showView('screen'); M.selectScreen(i); } },
        U.el('input', { type: 'radio', name: 'start', checked: isStart, title: 'start screen', onclick: e => { e.stopPropagation(); M.mutate(() => { M.st.project.start_screen = s.data.name; }); } }),
        U.el('span', { class: 'grow' }, s.data.name, U.el('span', { class: 'dim' }, '  ' + s.file)),
        U.el('button', { class: 'mini', title: 'rename', onclick: async e => { e.stopPropagation(); const n = await App.prompt('Screen name', s.data.name); if (n) M.renameScreen(i, n); } }, '✎'),
        U.el('button', { class: 'mini danger', title: 'delete', onclick: e => { e.stopPropagation(); if (confirm('Delete screen "' + s.data.name + '"?')) M.removeScreen(i); } }, '×')));
    });
    el.append(list);
    el.append(U.el('div', { class: 'row' }, U.el('button', { onclick: async () => { const n = await App.prompt('New screen name', 'screen' + (M.st.screens.length + 1)); if (n && !M.addScreen(n)) App.toast('name already used or invalid'); } }, '+ Add screen')));
    el.append(U.el('div', { class: 'hint' }, 'The radio button marks the start screen. Files are screens/<name>.json; renaming keeps the file name.'));
  },

  renderTags() {
    const el = document.getElementById('view-tags'); el.innerHTML = '';
    if (!M.st.project) return;
    const tags = M.st.project.tags = M.st.project.tags || [];
    const q = this.tagFilter.toLowerCase();
    const filter = U.el('input', { type: 'search', class: 'filter', placeholder: 'filter by name', value: this.tagFilter, dataset: { fk: 'tagfilter' },
      oninput: e => { this.tagFilter = e.target.value; this.renderTags(); document.querySelector('[data-fk="tagfilter"]').focus(); } });
    const add = () => { this.tagFilter = ''; M.addTag(); };
    el.append(U.el('div', { class: 'page-head' }, U.el('h3', { style: { margin: 0 } }, 'Tags'), U.el('span', { class: 'dim' }, tags.length + ' tags'),
      U.el('span', { class: 'grow' }), filter, U.el('button', { class: 'primary', onclick: add }, '+ Add tag')));
    const tbl = U.el('table', { class: 'grid' });
    tbl.append(U.el('tr', null, ...['#', 'name', 'type', 'init', 'min', 'max', ''].map(h => U.el('th', null, h))));
    tags.forEach((t, i) => {
      if (q && !(t.name || '').toLowerCase().includes(q)) return;
      const fk = 'tag.' + i;
      tbl.append(U.el('tr', null,
        U.el('td', { class: 'idx' }, String(i + 1)),
        U.el('td', { style: { width: '34%' } }, U.el('input', { type: 'text', value: t.name || '', dataset: { fk: fk + '.name' }, onchange: e => M.mutate(() => { t.name = e.target.value.trim(); }) })),
        U.el('td', { style: { width: '110px' } }, Props.editor(t, 'type', { t: 'select', o: SCHEMA.tagTypes }, fk)),
        U.el('td', null, U.el('input', { type: 'text', value: t.init === undefined ? '' : String(t.init), dataset: { fk: fk + '.init' }, onchange: e => M.mutate(() => {
          const s = e.target.value.trim();
          if (s === '') delete t.init;
          else if (t.type === 'string') t.init = s;
          else if (t.type === 'bool') t.init = /^(true|1|on)$/i.test(s);
          else t.init = Number(s);
        }) })),
        U.el('td', null, Props.editor(t, 'min', { t: 'number' }, fk)),
        U.el('td', null, Props.editor(t, 'max', { t: 'number' }, fk)),
        U.el('td', { class: 'act' }, U.el('button', { class: 'mini danger', title: 'delete', onclick: () => M.removeTag(i) }, '×'))));
    });
    el.append(tbl);
    if (!tags.length) el.append(U.el('div', { class: 'hint' }, 'No tags yet. Tags are the named values that widgets bind to and drivers read / write.'));
    el.append(U.el('div', { class: 'row' }, U.el('button', { onclick: add }, '+ Add tag')));
    const dup = tags.map(t => t.name).filter((n, i, a) => n && a.indexOf(n) !== i);
    if (dup.length) el.append(U.el('div', { class: 'warn' }, 'duplicate tag names: ' + [...new Set(dup)].join(', ')));
  },

  renderDrivers() {
    const page = document.getElementById('view-drivers'); page.innerHTML = '';
    if (!M.st.project) return;
    const drivers = M.st.project.drivers = M.st.project.drivers || [];
    // left: driver list, right: the selected driver's settings
    const side = U.el('div', { class: 'drv-side' }, U.el('h3', { style: { marginTop: 0 } }, 'Drivers'));
    const el = U.el('div', { class: 'drv-main' });
    page.append(side, el);
    const list = U.el('div', { class: 'list' });
    drivers.forEach((d, i) => {
      list.append(U.el('div', { class: 'item' + (i === this.driverSel ? ' sel' : ''), onclick: () => { this.driverSel = i; this.renderDrivers(); } },
        U.el('input', { type: 'checkbox', checked: d.enabled !== false, title: 'enabled', onclick: e => e.stopPropagation(), onchange: e => M.mutate(() => { if (e.target.checked) delete d.enabled; else d.enabled = false; }) }),
        U.el('span', { class: 'grow' }, d.type || '?', U.el('span', { class: 'dim' }, ' ' + this.driverSummary(d))),
        U.el('button', { class: 'mini danger', onclick: e => { e.stopPropagation(); M.removeDriver(i); } }, '×')));
    });
    side.append(list);
    const addSel = U.el('select', { style: { flex: '1' } });
    for (const [k, v] of Object.entries(SCHEMA.drivers)) addSel.append(U.el('option', { value: k }, v.label + ' (' + k + ')'));
    side.append(U.el('div', { class: 'row' }, addSel, U.el('button', { onclick: () => { M.addDriver(addSel.value); this.driverSel = drivers.length - 1; } }, '+ Add')));

    const d = drivers[this.driverSel];
    if (!d) { el.append(U.el('div', { class: 'hint' }, drivers.length ? 'Select a driver on the left.' : 'No drivers. Add one on the left (simulator, Modbus TCP, OPC UA server ...).')); return; }
    const sch = SCHEMA.drivers[d.type], fk = 'drv.' + this.driverSel;
    const body = U.el('div', { class: 'drv-form' });
    if (sch) {
      body.append(U.el('div', { class: 'drv-fields' }, ...Props.fields(d, sch.fields, fk)));
      if (sch.table) body.append(this.tableEditor(d, sch.table, fk));
    } else body.append(U.el('div', { class: 'hint' }, 'Unknown driver type; edit the JSON below.'));
    const raw = U.el('textarea', { rows: 12, class: 'mono', value: JSON.stringify(d, null, 2), onchange: e => {
      try { const o = JSON.parse(e.target.value); M.mutate(() => { drivers[this.driverSel] = o; }); } catch (err) { App.toast('JSON: ' + err.message); }
    } });
    el.append(U.el('h3', { style: { marginTop: '0' } }, (sch ? sch.label : d.type)), body, U.el('details', { class: 'sec' }, U.el('summary', null, 'raw JSON'), raw));
  },
  driverSummary(d) {
    if (d.type === 'sim') return (d.items || []).length + ' items';
    if (d.type === 'modbus_tcp') return (d.host || '') + ':' + (d.port || 502) + ', ' + (d.map || []).length + ' regs';
    if (d.type === 'opcua_server') return 'port ' + (d.port || 4840);
    return '';
  },

  // ---- scripts (Lua): list on the left, the selected script's trigger and code on the right
  scriptSummary(s) {
    const t = s.trigger || 'manual';
    if (t === 'interval') return 'every ' + U.num(s.interval_ms, 1000) + ' ms';
    if (t === 'tag') return 'on ' + ((Array.isArray(s.tags) ? s.tags : [s.tags || s.tag]).filter(Boolean).join(', ') || '(no tag)');
    if (t === 'screen_show' || t === 'screen_hide') return t + ' ' + (s.screen || '(any)');
    return t;
  },
  renderScripts() {
    const page = document.getElementById('view-scripts'); page.innerHTML = '';
    if (!M.st.project) return;
    const scripts = M.st.project.scripts || [];
    if (this.scriptSel >= scripts.length) this.scriptSel = Math.max(0, scripts.length - 1);
    const side = U.el('div', { class: 'drv-side' }, U.el('h3', { style: { marginTop: 0 } }, 'Scripts'));
    const el = U.el('div', { class: 'drv-main script-main' });
    page.append(side, el);
    const list = U.el('div', { class: 'list' });
    scripts.forEach((s, i) => {
      list.append(U.el('div', { class: 'item' + (i === this.scriptSel ? ' sel' : ''), onclick: () => { this.scriptSel = i; this.renderScripts(); } },
        U.el('input', { type: 'checkbox', checked: s.enabled !== false, title: 'enabled', onclick: e => e.stopPropagation(), onchange: e => M.mutate(() => { if (e.target.checked) delete s.enabled; else s.enabled = false; }) }),
        U.el('span', { class: 'grow' }, s.name || '?', U.el('span', { class: 'dim' }, '  ' + this.scriptSummary(s))),
        U.el('button', { class: 'mini danger', title: 'delete', onclick: e => { e.stopPropagation(); if (confirm('Delete script "' + (s.name || '') + '"?')) M.removeScript(i); } }, '×')));
    });
    side.append(list, U.el('div', { class: 'row' }, U.el('button', { class: 'primary', onclick: () => { this.scriptSel = M.addScript(); } }, '+ Add script')));
    side.append(U.el('div', { class: 'hint' }, 'Lua 5.4. All scripts share one Lua state: a function or variable defined by a startup script can be used by the others.'));

    const s = scripts[this.scriptSel];
    if (!s) { el.append(U.el('div', { class: 'hint' }, 'No scripts. Add one on the left; it can run at startup, periodically, when a tag changes, when a screen is shown / hidden, or from a widget event (action "script").')); return; }
    const i = this.scriptSel, fk = 'scr.' + i, trig = s.trigger || 'manual';
    const name = U.el('input', { type: 'text', value: s.name || '', dataset: { fk: fk + '.name' }, onchange: e => { if (!M.renameScript(i, e.target.value)) { App.toast('name is empty or already used'); e.target.value = s.name || ''; } } });
    const trigSel = U.el('select', { dataset: { fk: fk + '.trigger' }, onchange: e => M.setScriptTrigger(i, e.target.value) });
    for (const t of SCHEMA.scriptTriggers) trigSel.append(U.el('option', { value: t, selected: t === trig }, t));
    if (!SCHEMA.scriptTriggers.includes(trig)) trigSel.append(U.el('option', { value: trig, selected: true }, trig));
    const rows = [Props.row('name', name), Props.row('trigger', trigSel, 'when the script runs')];
    if (trig === 'interval') rows.push(Props.row('interval_ms', Props.editor(s, 'interval_ms', { t: 'number' }, fk), 'period in milliseconds (minimum 10)'));
    if (trig === 'tag') rows.push(Props.row('tags', Props.editor(s, 'tags', { t: 'csv' }, fk), 'comma separated tag names; the script runs when any of them changes'));
    if (trig === 'screen_show' || trig === 'screen_hide') rows.push(Props.row('screen', Props.editor(s, 'screen', { t: 'select', o: [''].concat(M.screenNames()), empty: '(any screen)' }, fk)));
    el.append(U.el('div', { class: 'drv-fields' }, ...rows));
    if (trig === 'tag') {
      const unknown = (Array.isArray(s.tags) ? s.tags : []).filter(t => !M.tagNames().includes(t));
      if (unknown.length) el.append(U.el('div', { class: 'warn' }, 'unknown tags: ' + unknown.join(', ')));
    }

    if (typeof s.file === 'string' && s.code === undefined) {
      el.append(U.el('div', { class: 'drv-fields' }, Props.row('file', Props.editor(s, 'file', { t: 'text' }, fk), 'Lua file, relative to project.json')),
        U.el('div', { class: 'hint' }, 'The code of this script is in an external file; edit it with a text editor (F5 in the runtime reloads it).'));
    } else {
      // typing updates the model directly (no re-render, so the caret stays put); one undo step per focus
      let undoPushed = false;
      const code = U.el('textarea', { class: 'mono script-code', spellcheck: false, dataset: { fk: fk + '.code' }, placeholder: '-- Lua\nif tag.temp > tag.temp_hi then\n  tag.alarm = true\nend',
        value: Array.isArray(s.code) ? s.code.join('\n') : (s.code || ''), onkeydown: U.tabKey,
        oninput: e => { if (!undoPushed) { M.pushUndo(); undoPushed = true; } s.code = e.target.value; if (!M.st.dirty) { M.st.dirty = true; App.toolbar(); } } });
      el.append(code);
    }
    el.append(U.el('details', { class: 'sec' }, U.el('summary', null, 'Lua API'), U.el('pre', { class: 'mono script-help' }, this.SCRIPT_HELP)));
  },
  SCRIPT_HELP: [
    'tag.name / tag["sys.name"]      read or write a tag (bool tags are true / false)',
    'goto_screen("name" [, "anim"])  show a screen (anim: none, fade, slide_left, ...); returns false if unknown',
    'back()                          go back to the previous screen',
    'current_screen()                name of the screen being shown',
    'run("script")                   run another project script',
    'call("fn" [, "arg"])            run a named runtime action (same as the "call" action)',
    'log(...) / print(...)           write to the runtime log',
    'millis()                        milliseconds since start',
    'os.time()  os.date("%H:%M:%S")  os.clock()',
    'event.trigger                   "startup" | "interval" | "tag" | "screen_show" | "screen_hide" | "action"',
    'event.tag, event.value          the tag that changed (trigger "tag")',
    'event.screen                    the screen shown / hidden',
    'Libraries: string, table, math, utf8, coroutine. No file or process access.',
    'A script is stopped with an error if it runs longer than 500 ms.',
  ].join('\n'),

  // generic array-of-objects editor (driver items / register map / published tags)
  tableEditor(obj, tdef, fk) {
    if (!Array.isArray(obj[tdef.key])) obj[tdef.key] = [];
    const items = obj[tdef.key];
    // "tags": ["temp", {"tag": "x", "access": "ro"}] - edit everything in object form (the runtime accepts both)
    items.forEach((it, i) => { if (typeof it === 'string') items[i] = { tag: it }; });
    const tbl = U.el('table', { class: 'grid' });
    tbl.append(U.el('tr', null, ...tdef.columns.map(c => U.el('th', null, c.k)), U.el('th')));
    items.forEach((it, i) => {
      const cells = tdef.columns.map(c => U.el('td', null, Props.editor(it, c.k, c, fk + '.' + tdef.key + '.' + i)));
      tbl.append(U.el('tr', null, ...cells, U.el('td', null, U.el('button', { class: 'mini danger', onclick: () => M.mutate(() => items.splice(i, 1)) }, '×'))));
    });
    const add = U.el('button', { class: 'mini', onclick: () => M.mutate(() => {
      const it = {};
      for (const c of tdef.columns) if (c.t === 'tag') it[c.k] = M.tagNames()[0] || ''; else if (c.t === 'select' && c.o && c.o[0]) it[c.k] = c.o[0];
      items.push(it);
    }) }, '+ row');
    return U.el('div', null, U.el('h4', null, tdef.label), tbl, U.el('div', { class: 'row' }, add));
  },

  renderProject() {
    const el = document.getElementById('tab-project'); el.innerHTML = '';
    const p = M.st.project; if (!p) return;
    const d = p.display = p.display || {}, th = p.theme = p.theme || {}, touch = d.touch = d.touch || {};
    const fk = 'proj';
    el.append(Props.section('Project', true,
      Props.row('title', Props.editor(p, 'title', { t: 'text' }, fk)),
      Props.row('start_screen', Props.editor(p, 'start_screen', { t: 'select', o: M.screenNames() }, fk))));
    el.append(Props.section('Display', true,
      ...Props.fields(d, [{ k: 'width', t: 'number' }, { k: 'height', t: 'number' }, { k: 'zoom', t: 'number', hint: 'SDL window zoom (PC only)' },
        { k: 'rotation', t: 'select', o: ['', '0', '90', '180', '270'] }, { k: 'dpi', t: 'number', hint: 'theme scale, default 130' }], fk + '.display')));
    el.append(Props.section('Raspberry Pi (fbdev / drm / evdev)', false,
      ...Props.fields(d, [{ k: 'fbdev', t: 'text', hint: '/dev/fb0' }, { k: 'drm', t: 'text', hint: '/dev/dri/card0' }, { k: 'input', t: 'text', hint: 'empty = auto detect' }], fk + '.display'),
      Props.row('touch swap_axes', Props.editor(touch, 'swap_axes', { t: 'bool' }, fk + '.touch')),
      Props.row('touch calibration', U.el('input', { type: 'text', value: Array.isArray(touch.calibration) ? touch.calibration.join(', ') : '', placeholder: 'min_x, min_y, max_x, max_y', dataset: { fk: fk + '.calib' },
        onchange: e => M.mutate(() => { const a = e.target.value.split(',').map(s => Number(s.trim())); if (a.length === 4 && !a.some(isNaN)) touch.calibration = a; else delete touch.calibration; if (!Object.keys(touch).length) delete d.touch; }) }))));
    el.append(Props.section('Boot splash (Raspberry Pi)', false,
      ...Props.fields(d, [{ k: 'splash', t: 'image' }, { k: 'splash_bg', t: 'color' }], fk + '.display'),
      U.el('div', { class: 'hint' }, 'Shown during boot by hml-splash.service (hml_runtime --splash) until the HMI starts. PNG, centred and shrunk to fit; empty = the project title. Background default: black.')));
    el.append(Props.section('Theme', true,
      ...Props.fields(th, [{ k: 'dark', t: 'bool' }, { k: 'primary', t: 'color' }, { k: 'secondary', t: 'color' }, { k: 'font', t: 'font' }], fk + '.theme')));
    const raw = U.el('textarea', { rows: 10, class: 'mono', value: JSON.stringify(Object.assign({}, p, { screens: M.st.screens.map(s => s.file) }), null, 2), onchange: e => {
      try { const o = JSON.parse(e.target.value); M.mutate(() => { M.st.project = o; }); } catch (err) { App.toast('JSON: ' + err.message); }
    } });
    el.append(U.el('details', { class: 'sec' }, U.el('summary', null, 'project.json (raw)'), raw));
    // the rotation select stores strings; the runtime wants a number
    if (typeof d.rotation === 'string') { d.rotation = Number(d.rotation) || 0; if (!d.rotation) delete d.rotation; }
  },
};
