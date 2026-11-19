// UI tests of the designer: evaluated inside the running designer page by ui_test.py (which
// opens a throw-away copy of projects/demo). Returns [{ name, ok, msg }].
// Everything goes through real DOM events where the page listens for them, so that the wiring
// (mouse, keyboard, buttons) is tested and not only the model (see model.test.js for that).
(async () => {
  const results = [];
  const frame = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
  const eq = (a, b, what) => { if (JSON.stringify(a) !== JSON.stringify(b)) throw new Error((what || 'value') + ': ' + JSON.stringify(a) + ' != ' + JSON.stringify(b)); };
  const ok = (v, what) => { if (!v) throw new Error(what || 'expected a true value'); };
  const el = path => Canvas.root.querySelector('[data-path="' + path + '"]');
  const mouse = (target, type, x, y, more) => target.dispatchEvent(new MouseEvent(type, Object.assign({ bubbles: true, cancelable: true, button: 0, clientX: x, clientY: y }, more || {})));
  const clickAt = (path, more) => { const r = el(path).getBoundingClientRect(); mouse(el(path), 'mousedown', r.left + 4, r.top + 4, more); mouse(document, 'mouseup', r.left + 4, r.top + 4, more); };
  const key = (k, more) => document.dispatchEvent(new KeyboardEvent('keydown', Object.assign({ key: k, bubbles: true, cancelable: true }, more || {})));
  const closeModal = () => { document.getElementById('modal').hidden = true; };
  const reset = () => { closeModal(); while (M.st.undo.length) M.undo(); M.st.cur = 0; M.st.sel = []; M.st.more = []; Panels.showView('screen'); App.doRefresh(); };
  const test = async (name, fn) => {
    try { reset(); await fn(); results.push({ name, ok: true }); }
    catch (e) { results.push({ name, ok: false, msg: String(e && e.message || e) }); }
  };
  const lang = I18N.lang;
  const setLang = l => { I18N.set(l); App.staticTexts(); App.doRefresh(); };    // (not saved: the select's handler is not used)

  setLang('ja');
  await test('language: toolbar, tabs and property names follow the language', async () => {
    eq(document.getElementById('b-save').textContent, '保存');
    App.select([1]);
    ok([...document.querySelectorAll('#props label, #props .row > *')].some(e => e.textContent === 'X 位置'), 'the property x is named in Japanese');
    setLang('en');
    eq(document.getElementById('b-save').textContent, 'Save');
    ok(![...document.querySelectorAll('#props *')].some(e => e.childElementCount === 0 && e.textContent === 'X 位置'), 'English: the key itself');
    setLang('ja');
  });

  await test('Ctrl+click selects several widgets; the align buttons line them up; one undo', async () => {
    clickAt('1'); clickAt('2', { ctrlKey: true }); clickAt('3', { ctrlKey: true });
    eq(M.selection(), [[1], [2], [3]]);
    eq(Canvas.root.querySelectorAll('.hd-selected').length, 3, 'outlined widgets');
    const btns = document.querySelectorAll('#props button.arr');
    eq(btns.length, 10, 'align buttons');
    M.get([2]).y = M.get([2]).y + 40; App.doRefresh();                    // (not an undo step)
    const y1 = M.get([1]).y;
    document.querySelectorAll('#props button.arr')[3].click();            // align top edges
    await frame();
    eq([M.get([1]).y, M.get([2]).y, M.get([3]).y], [y1, y1, y1], 'top edges');
    eq(M.st.undo.length, 1, 'undo steps');
    clickAt('2', { ctrlKey: true });                                       // take one out again
    eq(M.selection().length, 2);
    clickAt('1');                                                          // a plain click: just that one
    eq(M.selection(), [[1]]);
  });

  await test('dragging one of several selected widgets moves them all', async () => {
    clickAt('1'); clickAt('2', { ctrlKey: true });
    const before = [[1], [2], [3]].map(p => [M.get(p).x || 0, M.get(p).y || 0]);
    const r = el('2').getBoundingClientRect(), z = M.st.zoom;
    mouse(el('2'), 'mousedown', r.left + 6, r.top + 6);
    mouse(document, 'mousemove', r.left + 6 + 31 * z, r.top + 6 + 23 * z, { altKey: true });   // Alt: no snapping
    mouse(document, 'mouseup', r.left + 6 + 31 * z, r.top + 6 + 23 * z);
    await frame();
    const after = [[1], [2], [3]].map(p => [M.get(p).x || 0, M.get(p).y || 0]);
    eq(after, [[before[0][0] + 31, before[0][1] + 23], [before[1][0] + 31, before[1][1] + 23], before[2]]);
    eq(M.st.undo.length, 1, 'undo steps');
    eq(M.selection().length, 2, 'still selected');
  });

  await test('a drag snaps to the edge of a neighbour and shows guide lines', async () => {
    clickAt('2');
    const x0 = M.get([2]).x || 0, y0 = M.get([2]).y || 0, z = M.st.zoom;
    const r = el('2').getBoundingClientRect();
    mouse(el('2'), 'mousedown', r.left + 6, r.top + 6);
    mouse(document, 'mousemove', r.left + 6 + 3 * z, r.top + 6 + 60 * z);  // 3 px off: back onto the line
    await frame();
    eq(M.get([2]).x || 0, x0, 'x snapped back');
    ok(Canvas.root.querySelectorAll('.hd-guide').length >= 1, 'a guide line is shown');
    mouse(document, 'mouseup', 0, 0);
    await frame();
    eq(Canvas.root.querySelectorAll('.hd-guide').length, 0, 'guides are gone after the drag');
    ok((M.get([2]).y || 0) !== y0, 'moved down');
  });

  await test('keyboard: copy, paste onto another screen, arrows, duplicate, delete', async () => {
    ok(M.st.screens.length > 1, 'the demo has several screens');
    clickAt('1'); clickAt('2', { ctrlKey: true });
    key('c', { ctrlKey: true });
    eq(App.clip.length, 2, 'clipboard');
    M.selectScreen(1); App.doRefresh();
    const n = (M.screen().children || []).length;
    key('v', { ctrlKey: true });
    eq(M.screen().children.length, n + 2, 'pasted onto the other screen');
    eq(M.selection().length, 2, 'the pasted widgets are selected');
    const x = M.selected().x || 0;
    key('ArrowRight', { shiftKey: true });
    eq(M.selected().x, x + 10, 'moved by the arrow key');
    key('d', { ctrlKey: true });
    eq(M.screen().children.length, n + 4, 'duplicated');
    key('Delete');
    eq(M.screen().children.length, n + 2, 'deleted');
    key('a', { ctrlKey: true });
    eq(M.selection().length, n + 2, 'Ctrl+A');
  });

  await test('a widget dropped from the palette lands where the mouse is', async () => {
    ok(document.querySelector('#tab-widgets button.pal').draggable, 'palette buttons can be dragged');
    const r = Canvas.root.getBoundingClientRect(), z = M.st.zoom, n = M.screen().children.length;
    Canvas.dragType = 'led';
    Canvas.onDrop({ target: Canvas.root, clientX: r.left + 300 * z, clientY: r.top + 200 * z, preventDefault() {} });
    await frame();
    const w = M.screen().children[n];
    eq(w.type, 'led');
    ok(Math.abs(w.x - 300) <= 60 && Math.abs(w.y - 200) <= 60, 'near the mouse: ' + w.x + ',' + w.y);
    eq(M.st.undo.length, 1, 'undo steps');
  });

  await test('problems: counted in the status bar, listed, and a click goes there', async () => {
    eq(App.problems.length, 0, 'the demo project has no problems');
    M.mutate(() => { M.get([1]).children.push({ type: 'label', text: 'x', bind: { text: 'no_such_tag' }, on_click: [{ action: 'goto', screen: 'missing' }] }); });
    App.doRefresh();
    const st = document.getElementById('status-prob');
    ok(/2/.test(st.textContent), 'status bar: ' + st.textContent);
    Panels.showView('tags');
    st.click();
    const items = document.querySelectorAll('#modal .prob-list .item');
    eq(items.length, 2, 'listed problems');
    ok(/no_such_tag/.test(items[0].textContent), items[0].textContent);
    items[0].click();
    eq(Panels.view, 'screen');
    eq(M.st.sel, [1, M.get([1]).children.length - 1], 'the widget is selected');
    ok(document.getElementById('modal').hidden, 'the dialog closed');
  });

  await test('choices show their meaning, tag fields offer the tags, dialogs open', async () => {
    App.select([1]);
    ok([...document.querySelectorAll('#props select option')].some(o => o.value === 'center' && /中央/.test(o.textContent)), 'align: center has a Japanese name');
    const dl = [...document.querySelectorAll('#dl-tags option')];
    eq(dl.length, M.tagChoices().length, 'tag list');
    ok(dl[0].label, 'the tag list shows the type');
    key('F1');
    eq(document.querySelectorAll('#modal table.keys tr').length, App.SHORTCUTS.length, 'shortcut list');
    closeModal();
    document.getElementById('b-recent').click();
    ok(document.querySelectorAll('#modal .recent .item').length >= 1, 'recent projects');
  });

  await test('no interface text is left untranslated on the main pages', async () => {
    I18N.collect = new Set();
    for (const v of ['tags', 'drivers', 'scripts', 'logs', 'users', 'screen']) { Panels.showView(v); App.doRefresh(); }
    for (const t of ['screens', 'project', 'widgets']) Panels.show(t);
    App.select([1]); clickAt('2', { ctrlKey: true });
    const left = [...I18N.collect].filter(s => /^[A-Z][a-z]+ [a-z]+ [a-z ]+/.test(s));     // sentences in English
    I18N.collect = null;
    eq(left, [], 'untranslated');
  });

  await test('screen tabs: opening, switching with the keyboard, closing', async () => {
    const bar = document.getElementById('screen-tabs');
    const names = () => [...bar.querySelectorAll('.stab')].map(t => t.firstChild.textContent.replace('● ', ''));
    App.tabs = [M.st.screens[0].file]; App.doRefresh();
    eq(names(), [M.st.screens[0].data.name]);
    const more = bar.querySelector('select.stab-more');
    more.value = M.st.screens[1].file; more.dispatchEvent(new Event('change'));
    App.doRefresh();
    eq(names(), [M.st.screens[0].data.name, M.st.screens[1].data.name]);
    eq(M.st.cur, 1);
    key('Tab', { ctrlKey: true }); App.doRefresh();
    eq(M.st.cur, 0, 'Ctrl+Tab goes round');
    bar.querySelectorAll('.stab')[1].click(); App.doRefresh();
    eq(M.st.cur, 1, 'a click on a tab');
    bar.querySelector('.stab.active .x').click(); App.doRefresh();
    eq([names().length, M.st.cur], [1, 0], 'closed: the other tab is shown');
    eq(M.st.screens.length > 1, true, 'the screen is still in the project');
    Panels.showView('tags'); App.doRefresh();
    ok(bar.hidden, 'no tabs on the other pages');
  });

  await test('a rectangle dragged on the background selects the widgets it touches', async () => {
    const a = el('1').getBoundingClientRect(), b = el('2').getBoundingClientRect();
    const x0 = a.left + 10, y0 = a.bottom + 3;                              // on the background, just below the first panel
    ok(document.elementFromPoint(x0, y0) === Canvas.root || Canvas.root.contains(document.elementFromPoint(x0, y0)), 'the start is on the canvas');
    const start = document.elementFromPoint(x0, y0);
    mouse(start, 'mousedown', x0, y0);
    mouse(document, 'mousemove', b.left + 20, a.top + 20);
    eq(document.querySelectorAll('.hd-band').length, 1, 'the rectangle is drawn');
    mouse(document, 'mouseup', b.left + 20, a.top + 20);
    eq(document.querySelectorAll('.hd-band').length, 0);
    eq(M.selection(), [[1], [2]]);
    eq(M.st.undo.length, 0, 'selecting is not an undo step');
  });

  await test('right click: menu with copy / paste / order', async () => {
    const r = el('2').getBoundingClientRect();
    el('2').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: r.left + 5, clientY: r.top + 5 }));
    eq(M.selection(), [[2]], 'the widget under the mouse is selected');
    const items = [...document.querySelectorAll('#ctx-menu .mi')];
    ok(items.length >= 8, 'menu items');
    const n = M.screen().children.length, w = M.get([2]);
    items.find(i => /最前面/.test(i.textContent)).click();                   // bring to front
    ok(!document.getElementById('ctx-menu'), 'the menu closed');
    eq(M.screen().children[n - 1] === w, true, 'now the last one');
    el(String(n - 1)).dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 10, clientY: 10 }));
    key('Escape');
    ok(!document.getElementById('ctx-menu'), 'Esc closes the menu');
  });

  await test('tree: a row dropped on a panel moves the widget into it', async () => {
    const row = p => document.querySelector('#tree .tr-row[data-path="' + p + '"]');
    const dt = new DataTransfer();
    const n1 = M.get([1]).children.length, w = M.get([2]);
    row('2').dispatchEvent(new DragEvent('dragstart', { bubbles: true, dataTransfer: dt }));
    const r = row('1').getBoundingClientRect();
    row('1').dispatchEvent(new DragEvent('dragover', { bubbles: true, cancelable: true, dataTransfer: dt, clientX: r.left + 20, clientY: r.top + r.height / 2 }));
    ok(row('1').classList.contains('drop-into'), 'the panel is marked');
    row('1').dispatchEvent(new DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer: dt, clientX: r.left + 20, clientY: r.top + r.height / 2 }));
    await frame();
    eq(M.get([1]).children.length, n1 + 1);
    eq(M.get([1]).children[n1] === w, true, 'moved into the panel');
    eq(M.st.undo.length, 1, 'undo steps');
  });

  await test('tags page: where a tag is used, and renaming updates the uses', async () => {
    Panels.showView('tags'); App.doRefresh();
    const name = M.st.project.tags[0].name, uses = M.tagUses().get(name).length;
    ok(uses > 0, 'the first tag of the demo is used');
    const rowEl = document.querySelector('#view-tags table.grid tr:nth-child(2)');
    eq(rowEl.querySelector('td.uses button').textContent, String(uses));
    rowEl.querySelector('td.uses button').click();
    eq(document.querySelectorAll('#modal .prob-list .item').length, uses, 'listed uses');
    closeModal();
    const inp = rowEl.querySelector('input[type=text]');
    inp.value = name + '_renamed'; inp.dispatchEvent(new Event('change'));
    App.doRefresh();
    eq(M.tagUses().get(name + '_renamed').length, uses, 'the uses follow');
    eq(App.problems.length, 0, 'nothing is left pointing at the old name');
    eq(M.st.undo.length, 1, 'undo steps');
  });

  await test('style copy / paste, lock and hide on the canvas', async () => {
    clickAt('1');
    const st = JSON.stringify(M.styleOf(M.get([1])));
    key('c', { ctrlKey: true, altKey: true });
    clickAt('2'); clickAt('3', { ctrlKey: true });
    M.get([2]).style = { bg_color: '#ff0000' }; M.get([2]).style_pressed = { bg_color: '#00ff00' };
    key('v', { ctrlKey: true, altKey: true });
    eq([JSON.stringify(M.styleOf(M.get([2]))), JSON.stringify(M.styleOf(M.get([3])))], [st, st], 'both have the copied style, and only that');
    clickAt('1');
    M.setFlag('locked', true); App.doRefresh();
    ok(el('1').classList.contains('hd-locked'), 'marked as locked');
    eq(getComputedStyle(el('1')).pointerEvents, 'none', 'clicks go through');
    ok(document.querySelector('#tree .tr-row[data-path="1"] .tr-mark'), 'the tree shows the lock');
    App.select([2]); M.setFlag('hidden', true); App.select([]); App.doRefresh();
    eq(getComputedStyle(el('2')).display, 'none', 'not drawn');
    M.clearFlag(); App.doRefresh();
    ok(getComputedStyle(el('2')).display !== 'none' && !el('1').classList.contains('hd-locked'), 'all back');
    eq(M.get([1]).designer, undefined, 'no mark is left in the data');
  });

  await test('tags: export and import through the dialog; unsaved work is kept aside', async () => {
    Panels.showView('tags'); App.doRefresh();
    const n = M.st.project.tags.length;
    [...document.querySelectorAll('#view-tags .page-head button')].find(b => /取り込み/.test(b.textContent)).click();
    const ta = document.querySelector('#modal textarea.csv');
    eq(ta.value.trim().split('\n').length, n + 1, 'one line per tag and a heading');
    const btn = t => [...document.querySelectorAll('#modal button')].find(b => b.textContent === t);
    ta.value = 'new_one\tbool\ttrue\nbad name,int';
    btn('取り込む').click();
    eq(M.st.project.tags.length, n, 'an error: nothing is imported');
    ok(document.querySelector('#modal .warn'), 'the error is shown');
    ta.value = 'new_one\tbool\ttrue\n' + M.st.project.tags[0].name + ',float,5,0,100';
    btn('取り込む').click();
    eq(M.st.project.tags.length, n + 1);
    eq([M.st.project.tags[n], M.st.project.tags[0].max], [{ name: 'new_one', type: 'bool', init: true }, 100]);
    eq(M.st.undo.length, 1, 'undo steps');
    App.autosave(true);
    const rec = JSON.parse(localStorage.getItem(App.recKey(M.st.path)));
    eq(rec.project.tags.length, n + 1, 'the safety copy has the change');
    // as after a crash: the project is opened again, and the unsaved work is offered
    const ask = App.ask, file = M.st.path;
    try {
      let asked = 0;
      App.ask = () => { asked++; return true; };
      await App.open(file);
      eq([asked, M.st.project.tags.length, M.st.dirty], [1, n + 1, true], 'restored');
      App.ask = () => { asked++; return false; };
      await App.open(file);
      eq([asked, M.st.project.tags.length, M.st.dirty], [2, n, false], 'declined: the saved project');
      eq(localStorage.getItem(App.recKey(file)), null, 'declined: the safety copy is gone');
    } finally { App.ask = ask; }
    M.st.dirty = false; App.autosave(true);
    eq(localStorage.getItem(App.recKey(M.st.path)), null, 'nothing unsaved: no safety copy');
  });

  await test('recipes: the page edits books and values, the recipe table shows the book', async () => {
    key('7', { ctrlKey: true }); App.doRefresh();
    eq(Panels.view, 'recipes');
    const page = document.getElementById('view-recipes');
    ok(!page.hidden, 'page shown');
    ok(page.textContent.includes('レシピブック'), 'in Japanese');
    const n = M.recipeBooks().length;
    [...page.querySelectorAll('button')].find(b => b.textContent.includes('レシピブックを追加')).click(); App.doRefresh();
    eq(M.recipeBooks().length, n + 1);
    const bi = n, b = M.recipeBooks()[bi];
    [...page.querySelectorAll('button')].filter(x => x.textContent.includes('レシピを追加')).pop().click(); App.doRefresh();
    eq(b.recipes.length, 1);
    const cell = document.querySelector('#view-recipes input[data-fk="rec.' + bi + '.r0.' + b.items[0] + '"]');
    ok(cell, 'value cell');
    cell.value = '7'; cell.dispatchEvent(new Event('change')); App.doRefresh();
    const t0 = M.st.project.tags.find(t => t.name === b.items[0]);
    eq(b.recipes[0].values[b.items[0]], t0.type === 'string' ? '7' : t0.type === 'bool' ? false : 7);
    Panels.showView('screen'); App.doRefresh();
    App.addWidget('recipe_table'); await frame();
    const sel = [...document.querySelectorAll('#props select')].find(s => [...s.options].some(o => o.value === b.name));
    ok(sel, 'book select');
    sel.value = b.name; sel.dispatchEvent(new Event('change')); App.doRefresh(); await frame();
    const el2 = Canvas.root.querySelector('[data-path="' + U.pathStr(M.st.sel) + '"]');
    ok(el2.classList.contains('lv-recipe_table') && el2.textContent.includes(b.recipes[0].name), 'preview shows the recipes');
    eq(App.problems.filter(p => /recipe/i.test(p.text)).length, 0, 'no recipe problems');
  });

  await test('drivers: an SLMP driver can be added and its map shows address problems', async () => {
    Panels.showView('drivers'); App.doRefresh();
    const sel = [...document.querySelectorAll('#view-drivers select')].find(s => [...s.options].some(o => o.value === 'slmp'));
    ok(sel, 'slmp is offered');
    ok([...sel.options].find(o => o.value === 'slmp').textContent.includes('三菱電機'), 'named in Japanese');
    sel.value = 'slmp';
    [...document.querySelectorAll('#view-drivers button')].find(b => b.textContent.includes('追加')).click();
    App.doRefresh();
    const d = M.st.project.drivers[M.st.project.drivers.length - 1];
    eq([d.type, d.port, d.frame], ['slmp', 5007, '3e']);
    ok(document.querySelector('#view-drivers').textContent.includes('デバイスの割り付け'), 'map table');
    M.mutate(() => { d.map.push({ tag: M.st.project.tags[0].name, addr: 'D1A' }); }); App.doRefresh();
    ok(App.problems.some(p => /D1A/.test(p.text)), 'a wrong address is a problem');
    ok(document.querySelector('#view-drivers input[placeholder="D100 / M0 / X1F / D10.3"]'), 'the address field shows examples');
  });

  await test('alarm widgets: in the palette, drawn with the alarms of the project, settings on the Logs page', async () => {
    const pal = [...document.querySelectorAll('#tab-widgets button.pal')].map(b => b.textContent);
    ok(pal.some(t => /アラーム一覧/.test(t)) && pal.some(t => /アラームバナー/.test(t)), 'palette: ' + pal.join(','));
    App.addWidget('alarm_list'); await frame();
    const p = M.st.sel, el2 = Canvas.root.querySelector('[data-path="' + U.pathStr(p) + '"]');
    ok(el2 && el2.classList.contains('lv-alarm_list'), 'drawn');
    const first = M.st.project.alarms.items[0].message;
    ok(el2.textContent.includes(first), 'the alarm "' + first + '" of the project is shown as a sample');
    ok(el2.querySelector('.lv-al-unacked') && el2.querySelector('.lv-al-cleared'), 'states are shown');
    const mode = [...document.querySelectorAll('#props select')].find(s => [...s.options].some(o => o.value === 'history'));
    ok(mode, 'mode select');
    mode.value = 'history'; mode.dispatchEvent(new Event('change')); App.doRefresh(); await frame();
    ok(Canvas.root.querySelector('[data-path="' + U.pathStr(p) + '"] .lv-al-foot'), 'history has page buttons');
    eq(M.get(p).mode, 'history');
    App.addWidget('alarm_banner'); await frame();
    ok(Canvas.root.querySelector('.lv-alarm_banner').textContent.includes(first), 'banner');
    eq(App.problems.length, 0, 'no problems');
    Panels.showView('logs'); App.doRefresh();
    ok(document.querySelector('[data-fk="log.alarms.ack_level"]'), 'ack_level field');
    ok([...document.querySelectorAll('#view-logs th')].some(t => /確認が必要/.test(t.textContent)), 'ack column');
    M.st.project.alarms.ack_level = 2; App.doRefresh();
    ok(!App.problems.some(x => /ack_level/.test(x.text)), 'the demo has user management: fine');
    const users = M.st.project.users; delete M.st.project.users; App.doRefresh();
    ok(App.problems.some(x => /ack_level/.test(x.text)), 'a level without user management is a problem');
    M.st.project.users = users; delete M.st.project.alarms.ack_level;
  });

  await test('trend: in the palette, group chosen from the data log, preview with the legend', async () => {
    ok([...document.querySelectorAll('#tab-widgets button.pal')].some(b => /トレンド/.test(b.textContent)), 'palette');
    App.addWidget('trend'); await frame();
    const sel = [...document.querySelectorAll('#props select')].find(s => [...s.options].some(o => o.value === 'process'));
    ok(sel, 'group select offers the data log groups');
    sel.value = 'process'; sel.dispatchEvent(new Event('change')); App.doRefresh(); await frame();
    const el2 = Canvas.root.querySelector('[data-path="' + U.pathStr(M.st.sel) + '"]');
    ok(el2.classList.contains('lv-trend') && el2.querySelectorAll('path').length === 5, 'one line per tag of the group');
    ok(el2.textContent.includes('temp') && el2.textContent.includes('1 h'), 'legend and window');
    eq(App.problems.filter(p => /trend/.test(p.text)).length, 0, 'no problems');
    M.mutate(() => { M.selected().tags = ['run']; }); App.doRefresh();
    ok(App.problems.some(p => /does not record "run"/.test(p.text)), 'a tag the group does not record');
  });

  reset();
  M.st.dirty = false; App.autosave(true);
  setLang(lang);
  return results;
})()
