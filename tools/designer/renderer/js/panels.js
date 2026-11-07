// Left side panels: widget palette, screens, tags, drivers, project settings. Global `Panels`.
'use strict';

const Panels = {
  tab: 'widgets', driverSel: 0,

  init() {
    document.querySelectorAll('#left-tabs button').forEach(b => b.addEventListener('click', () => this.show(b.dataset.tab)));
    this.show('widgets');
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
      list.append(U.el('div', { class: 'item' + (i === M.st.cur ? ' sel' : ''), onclick: () => M.selectScreen(i) },
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
    const el = document.getElementById('tab-tags'); el.innerHTML = '';
    if (!M.st.project) return;
    const tags = M.st.project.tags = M.st.project.tags || [];
    const tbl = U.el('table', { class: 'grid' });
    tbl.append(U.el('tr', null, ...['name', 'type', 'init', 'min', 'max', ''].map(h => U.el('th', null, h))));
    tags.forEach((t, i) => {
      const fk = 'tag.' + i;
      tbl.append(U.el('tr', null,
        U.el('td', null, U.el('input', { type: 'text', value: t.name || '', dataset: { fk: fk + '.name' }, onchange: e => M.mutate(() => { t.name = e.target.value.trim(); }) })),
        U.el('td', null, Props.editor(t, 'type', { t: 'select', o: SCHEMA.tagTypes }, fk)),
        U.el('td', null, U.el('input', { type: 'text', value: t.init === undefined ? '' : String(t.init), dataset: { fk: fk + '.init' }, onchange: e => M.mutate(() => {
          const s = e.target.value.trim();
          if (s === '') delete t.init;
          else if (t.type === 'string') t.init = s;
          else if (t.type === 'bool') t.init = /^(true|1|on)$/i.test(s);
          else t.init = Number(s);
        }) })),
        U.el('td', null, Props.editor(t, 'min', { t: 'number' }, fk)),
        U.el('td', null, Props.editor(t, 'max', { t: 'number' }, fk)),
        U.el('td', null, U.el('button', { class: 'mini danger', onclick: () => M.removeTag(i) }, '×'))));
    });
    el.append(tbl);
    el.append(U.el('div', { class: 'row' }, U.el('button', { onclick: () => M.addTag() }, '+ Add tag')));
    const dup = tags.map(t => t.name).filter((n, i, a) => n && a.indexOf(n) !== i);
    if (dup.length) el.append(U.el('div', { class: 'warn' }, 'duplicate tag names: ' + [...new Set(dup)].join(', ')));
  },

  renderDrivers() {
    const el = document.getElementById('tab-drivers'); el.innerHTML = '';
    if (!M.st.project) return;
    const drivers = M.st.project.drivers = M.st.project.drivers || [];
    const list = U.el('div', { class: 'list' });
    drivers.forEach((d, i) => {
      list.append(U.el('div', { class: 'item' + (i === this.driverSel ? ' sel' : ''), onclick: () => { this.driverSel = i; this.renderDrivers(); } },
        U.el('input', { type: 'checkbox', checked: d.enabled !== false, title: 'enabled', onclick: e => e.stopPropagation(), onchange: e => M.mutate(() => { if (e.target.checked) delete d.enabled; else d.enabled = false; }) }),
        U.el('span', { class: 'grow' }, d.type || '?', U.el('span', { class: 'dim' }, ' ' + this.driverSummary(d))),
        U.el('button', { class: 'mini danger', onclick: e => { e.stopPropagation(); M.removeDriver(i); } }, '×')));
    });
    el.append(list);
    const addSel = U.el('select');
    for (const [k, v] of Object.entries(SCHEMA.drivers)) addSel.append(U.el('option', { value: k }, v.label + ' (' + k + ')'));
    el.append(U.el('div', { class: 'row' }, addSel, U.el('button', { onclick: () => { M.addDriver(addSel.value); this.driverSel = drivers.length - 1; } }, '+ Add')));

    const d = drivers[this.driverSel];
    if (!d) return;
    const sch = SCHEMA.drivers[d.type], fk = 'drv.' + this.driverSel;
    const body = U.el('div', { class: 'drv-form' });
    if (sch) {
      body.append(...Props.fields(d, sch.fields, fk));
      if (sch.table) body.append(this.tableEditor(d, sch.table, fk));
    } else body.append(U.el('div', { class: 'hint' }, 'Unknown driver type; edit the JSON below.'));
    const raw = U.el('textarea', { rows: 6, class: 'mono', value: JSON.stringify(d, null, 2), onchange: e => {
      try { const o = JSON.parse(e.target.value); M.mutate(() => { drivers[this.driverSel] = o; }); } catch (err) { App.toast('JSON: ' + err.message); }
    } });
    el.append(U.el('h3', null, (sch ? sch.label : d.type)), body, U.el('details', { class: 'sec' }, U.el('summary', null, 'raw JSON'), raw));
  },
  driverSummary(d) {
    if (d.type === 'sim') return (d.items || []).length + ' items';
    if (d.type === 'modbus_tcp') return (d.host || '') + ':' + (d.port || 502) + ', ' + (d.map || []).length + ' regs';
    if (d.type === 'opcua_server') return 'port ' + (d.port || 4840);
    return '';
  },

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
