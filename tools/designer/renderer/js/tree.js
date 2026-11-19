// Widget tree of the current screen. Global `Tree`.
'use strict';

const Tree = {
  el: null,
  init() { this.el = document.getElementById('tree'); },

  render() {
    this.el.innerHTML = '';
    const scr = M.screen();
    if (!scr) return;
    const ul = U.el('ul', { class: 'tree' });
    ul.append(this.node(scr, [], true));
    this.el.append(ul);
    const sel = this.el.querySelector('.tr-row.sel');
    if (sel) sel.scrollIntoView({ block: 'nearest' });
  },

  label(w, isRoot) {
    if (isRoot) return 'screen “' + (w.name || '') + '”';
    const t = w.type || 'panel';
    if (w.text !== undefined && w.text !== '') return t + ' “' + String(w.text).slice(0, 24) + '”';
    if (typeof w.bind === 'string') return t + ' ⇄ ' + w.bind;
    if (w.bind && typeof w.bind === 'object') {
      const first = Object.values(w.bind)[0];
      const tag = typeof first === 'string' ? first : (first && first.tag) || '';
      if (tag) return t + ' ⇄ ' + tag;
    }
    if (w.src) return t + ' ' + U.basename(w.src);
    if (w.children) return t + ' (' + w.children.length + ')';
    return t;
  },

  node(w, path, isRoot) {
    const li = U.el('li');
    const selected = U.samePath(M.st.sel, path) || (!isRoot && M.selection().some(p => U.samePath(p, path)));
    const row = U.el('div', {
      class: 'tr-row' + (selected ? ' sel' : '') + (w.hidden ? ' hidden-w' : ''),
      dataset: { path: U.pathStr(path) },
      draggable: !isRoot,
      ondragstart: e => {
        // one of several selected rows: they all move
        const sels = M.selection();
        Tree.dragPaths = sels.some(p => U.samePath(p, path)) ? sels : [path];
        e.dataTransfer.setData('text/plain', 'widget'); e.dataTransfer.effectAllowed = 'move'; e.stopPropagation();
      },
      ondragend: () => { Tree.dragPaths = null; Tree.mark(null); },
      ondragover: e => { const z = Tree.zone(e, row, w, isRoot); if (!z) return; e.preventDefault(); e.dataTransfer.dropEffect = 'move'; Tree.mark(row, z); },
      ondragleave: () => Tree.mark(null),
      ondrop: e => {
        const z = Tree.zone(e, row, w, isRoot), paths = Tree.dragPaths;
        Tree.dragPaths = null; Tree.mark(null);
        if (!z || !paths) return;
        e.preventDefault();
        if (z === 'into') M.moveWidgets(paths, path);
        else M.moveWidgets(paths, path.slice(0, -1), path[path.length - 1] + (z === 'after' ? 1 : 0));
      },
      oncontextmenu: e => { e.preventDefault(); e.stopPropagation(); App.widgetMenu(e, path); },
      onclick: e => {
        e.stopPropagation();
        if ((e.ctrlKey || e.shiftKey || e.metaKey) && !isRoot) { M.toggleSelect(path); App.selectionChanged(); }
        else App.select(path);
      },
      ondblclick: e => { e.stopPropagation(); if (!isRoot) Props.focusFirst(); },
    }, U.el('span', { class: 'tr-icon' }, this.icon(w, isRoot)), U.el('span', { class: 'tr-label' }, this.label(w, isRoot)),
      M.flag(w, 'locked') ? U.el('span', { class: 'tr-mark', title: 'Locked on the canvas' }, '🔒') : null,
      M.flag(w, 'hidden') ? U.el('span', { class: 'tr-mark', title: 'Hidden on the canvas (designer only)' }, '🚫') : null);
    li.append(row);
    if (w.children && w.children.length) {
      const ul = U.el('ul');
      w.children.forEach((c, i) => ul.append(this.node(c, path.concat(i), false)));
      li.append(ul);
    }
    return li;
  },

  // ---- drag and drop: reorder, and move into / out of panels
  dragPaths: null,
  // where a drop on this row would go: 'before' / 'after' the widget, or 'into' it (a panel)
  zone(e, row, w, isRoot) {
    if (!this.dragPaths) return null;
    if (isRoot) return 'into';
    const r = row.getBoundingClientRect(), f = (e.clientY - r.top) / Math.max(1, r.height), box = M.isContainer(w);
    return box ? (f < 0.25 ? 'before' : f > 0.75 ? 'after' : 'into') : (f < 0.5 ? 'before' : 'after');
  },
  mark(row, z) {
    for (const r of this.el.querySelectorAll('.drop-before, .drop-after, .drop-into')) r.classList.remove('drop-before', 'drop-after', 'drop-into');
    if (row && z) row.classList.add('drop-' + z);
  },

  icon(w, isRoot) {
    if (isRoot) return '▣';
    switch (w.type) {
      case 'label': return 'T';
      case 'button': return '▭';
      case 'switch': return '⏻';
      case 'checkbox': return '☑';
      case 'slider': case 'bar': return '─';
      case 'arc': case 'spinner': return '◔';
      case 'led': return '●';
      case 'dropdown': return '▾';
      case 'roller': return '☰';
      case 'textarea': return '✎';
      case 'numeric_display': return '#';
      case 'text_display': return '"';
      case 'password_display': return '*';
      case 'image': return '▨';
      case 'chart': return '↗';
      case 'keyboard': return '⌨';
      case 'line': return '╱';
      case 'recipe_table': return '☷';
      case 'alarm_list': return '⚠';
      case 'alarm_banner': return '⚑';
      default: return '□';
    }
  },
};
