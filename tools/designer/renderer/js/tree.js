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
    const selected = U.samePath(M.st.sel, path);
    const row = U.el('div', {
      class: 'tr-row' + (selected ? ' sel' : '') + (w.hidden ? ' hidden-w' : ''),
      dataset: { path: U.pathStr(path) },
      onclick: e => { e.stopPropagation(); App.select(path); },
      ondblclick: e => { e.stopPropagation(); if (!isRoot) Props.focusFirst(); },
    }, U.el('span', { class: 'tr-icon' }, this.icon(w, isRoot)), U.el('span', { class: 'tr-label' }, this.label(w, isRoot)));
    li.append(row);
    if (w.children && w.children.length) {
      const ul = U.el('ul');
      w.children.forEach((c, i) => ul.append(this.node(c, path.concat(i), false)));
      li.append(ul);
    }
    return li;
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
      default: return '□';
    }
  },
};
