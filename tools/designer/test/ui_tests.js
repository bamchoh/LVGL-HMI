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

  await test('drivers: an MQTT driver with TLS on, a publish and a subscribe table; no TLS is a problem', async () => {
    Panels.showView('drivers'); App.doRefresh();
    const sel = [...document.querySelectorAll('#view-drivers select')].find(s => [...s.options].some(o => o.value === 'mqtt'));
    ok(sel, 'mqtt is offered');
    sel.value = 'mqtt';
    [...document.querySelectorAll('#view-drivers button')].find(b => b.textContent.includes('追加')).click();
    App.doRefresh();
    const i = M.st.project.drivers.length - 1, d = M.st.project.drivers[i];
    eq([d.type, d.port, d.tls], ['mqtt', 8883, true]);
    const view = document.querySelector('#view-drivers').textContent;
    ok(view.includes('送信（タグの値をブローカーへ）') && view.includes('受信（指令: ここにあるタグだけ書き込まれる）'), 'both tables');
    const tls = document.querySelector('[data-fk="drv.' + i + '.tls"]');
    ok(tls && tls.checked, 'TLS is on');
    eq(App.problems.filter(p => p.index === i && p.view === 'drivers').length, 0, 'no problems with TLS on');
    tls.checked = false; tls.dispatchEvent(new Event('change')); App.doRefresh();
    eq(d.tls, false);
    ok(App.problems.some(p => /TLS/.test(p.text) && p.index === i), 'no TLS is a problem');
    M.mutate(() => { M.st.project.drivers.splice(i, 1); }); App.doRefresh();
  });

  await test('drivers: an OPC UA client is encrypted by default; switching security off is a problem', async () => {
    Panels.showView('drivers'); App.doRefresh();
    const sel = [...document.querySelectorAll('#view-drivers select')].find(s => [...s.options].some(o => o.value === 'opcua_client'));
    ok(sel, 'opcua_client is offered');
    ok([...sel.options].find(o => o.value === 'opcua_client').textContent.includes('OPC UA クライアント'), 'named in Japanese');
    sel.value = 'opcua_client';
    [...document.querySelectorAll('#view-drivers button')].find(b => b.textContent.includes('追加')).click();
    App.doRefresh();
    const i = M.st.project.drivers.length - 1, d = M.st.project.drivers[i];
    eq(d.type, 'opcua_client');
    const security = document.querySelector('[data-fk="drv.' + i + '.security"]');
    ok(security, 'security field');
    ok([...security.options].find(o => o.selected).textContent.includes('署名と暗号化'), 'Sign & Encrypt is the default');
    ok(document.querySelector('#view-drivers').textContent.includes('ノード'), 'node table');
    eq(App.problems.filter(p => p.index === i && p.view === 'drivers').length, 0, 'no problems by default');
    security.value = 'none'; security.dispatchEvent(new Event('change')); App.doRefresh();
    eq(d.security, 'none');
    ok(App.problems.some(p => p.index === i && /no security/.test(p.text)), 'no security is a problem');
    M.mutate(() => { M.st.project.drivers.splice(i, 1); }); App.doRefresh();
  });

  await test('XY chart: in the palette, the numbers at the ends of the axes, a curve per series', async () => {
    const pal = [...document.querySelectorAll('#tab-widgets button.pal')].map(b => b.textContent);
    ok(pal.some(t => /XY グラフ/.test(t)), 'palette: ' + pal.join(','));
    App.addWidget('xy_chart'); await frame();
    const p = M.st.sel, find = () => Canvas.root.querySelector('[data-path="' + U.pathStr(p) + '"]');
    ok(find() && find().classList.contains('lv-xy_chart') && find().querySelector('svg'), 'drawn');
    const ends = () => [...find().querySelectorAll('.lv-xy-end')].map(e => e.textContent).sort().join(',');
    eq(ends(), '0,0,100,100');
    eq(find().querySelectorAll('svg polyline').length, 1);
    const tag = M.st.project.tags[0].name;
    M.mutate(() => { Object.assign(M.get(p), { x_tag: tag, y_tag: tag, x_min: -1, x_max: 1, y_min: 0, y_max: 0.5, decimals: 1, series: [{ x_tag: tag, y_tag: 'no_such_tag' }] }); });
    App.doRefresh(); await frame();
    eq(ends(), '-1.0,0.0,0.5,1.0');
    eq(find().querySelectorAll('svg polyline').length, 2, 'a curve per series');
    ok(M.problems().some(x => /no_such_tag/.test(x.text)), 'an unknown tag of a series is a problem');
    M.mutate(() => { M.get(p).mode = 'dots'; }); App.doRefresh(); await frame();
    ok(find().querySelectorAll('svg circle').length > 10 && !find().querySelector('svg polyline'), 'dots instead of lines');
    ok([...document.querySelectorAll('#props input, #props select')].some(i => i.value === tag), 'the tag of an axis is shown in the properties');
    M.deleteWidget(p); App.doRefresh();
  });

  await test('table: in the palette, its rows and columns, tag cells show the first value of the tag', async () => {
    const pal = [...document.querySelectorAll('#tab-widgets button.pal')].map(b => b.textContent);
    ok(pal.some(t => /^表$|表/.test(t.trim())), 'palette: ' + pal.join(','));
    App.addWidget('table'); await frame();
    const p = M.st.sel, find = () => Canvas.root.querySelector('[data-path="' + U.pathStr(p) + '"]');
    ok(find() && find().classList.contains('lv-table'), 'drawn');
    const grid = () => [...find().querySelectorAll('tr')].map(r => [...r.children].map(c => c.textContent).join('|'));
    eq(grid(), ['Name|Value', 'A|1', 'B|2'], 'the table a new widget starts with');
    const tag = M.st.project.tags.find(t => t.type === 'int' || t.type === 'float') || M.st.project.tags[0];
    M.mutate(() => { Object.assign(M.get(p), { columns: ['What', { title: 'Now', align: 'right', width: 80 }, 'State'],
      rows: [['Speed', { tag: tag.name }, { tag: tag.name, map: { default: 'other' } }], ['Gone', { tag: 'no_such_tag' }], [5]] }); }); App.doRefresh(); await frame();
    const g = grid();
    eq(g[0], 'What|Now|State');
    ok(g[1].startsWith('Speed|') && g[1].endsWith('|other'), 'a tag cell and a map: ' + g[1]);
    eq(g[2], 'Gone|{no_such_tag}|', 'an unknown tag is shown as such');
    eq(g[3], '5||');
    eq(find().querySelectorAll('th')[1].style.textAlign, 'right');
    M.mutate(() => { M.get(p).header = false; }); App.doRefresh(); await frame();
    eq(find().querySelectorAll('th').length, 0, 'no titles');
    ok(M.problems().some(x => /no_such_tag/.test(x.text)), 'the unknown tag is a problem');
    M.deleteWidget(p); App.doRefresh();
  });

  await test('date and time input: in the palette, the rollers of each mode', async () => {
    const pal = [...document.querySelectorAll('#tab-widgets button.pal')].map(b => b.textContent);
    ok(pal.some(t => /日付・時刻の入力/.test(t)), 'palette: ' + pal.join(','));
    App.addWidget('datetime_input'); await frame();
    const p = M.st.sel, find = () => Canvas.root.querySelector('[data-path="' + U.pathStr(p) + '"]');
    ok(find() && find().classList.contains('lv-datetime_input'), 'drawn');
    const rolls = () => find().querySelectorAll('.lv-dt-roll').length;
    eq(rolls(), 5, 'year month day hour minute');
    M.mutate(() => { M.get(p).seconds = true; }); App.doRefresh(); await frame();
    eq(rolls(), 6);
    M.mutate(() => { M.get(p).mode = 'date'; }); App.doRefresh(); await frame();
    eq(rolls(), 3, 'a date has no seconds');
    M.mutate(() => { M.get(p).mode = 'time'; }); App.doRefresh(); await frame();
    eq(rolls(), 3, 'hour minute second');
    ok(/2026/.test(find().textContent) === false, 'no year in time mode');
    M.deleteWidget(p); App.doRefresh();
  });

  await test('meter: in the palette, drawn with its scale, zones and needle; its properties', async () => {
    const pal = [...document.querySelectorAll('#tab-widgets button.pal')].map(b => b.textContent);
    ok(pal.some(t => /メーター/.test(t)), 'palette: ' + pal.join(','));
    App.addWidget('meter'); await frame();
    const p = M.st.sel, find = () => Canvas.root.querySelector('[data-path="' + U.pathStr(p) + '"]');
    let el2 = find();
    ok(el2 && el2.classList.contains('lv-meter') && el2.querySelector('svg'), 'drawn');
    const texts = () => [...find().querySelectorAll('svg text')].map(t => t.textContent);
    ok(texts().includes('0') && texts().includes('100'), 'numbers of the scale: ' + texts().join(','));
    eq(find().querySelectorAll('svg line').length, 21 + 1, '21 ticks and the needle');
    M.mutate(() => { Object.assign(M.get(p), { min: 0, max: 1.5, decimals: 2, unit: 'MPa', ticks: 16, major_every: 3, value: 0.75,
                                              zones: [{ from: 1.2, to: 1.5, color: '#e53935' }] }); }); App.doRefresh(); await frame();
    ok(texts().includes('1.50') && texts().includes('0.75 MPa'), 'decimals and the value with its unit: ' + texts().join(','));
    eq(find().querySelectorAll('svg line').length, 16 + 1);
    ok([...find().querySelectorAll('svg path')].some(x => x.getAttribute('stroke') === '#e53935'), 'the zone is drawn in its colour');
    M.mutate(() => { M.get(p).show_value = false; }); App.doRefresh(); await frame();
    ok(!texts().some(t => /MPa/.test(t)), 'no value when it is switched off');
    M.deleteWidget(p); App.doRefresh();
  });

  await test('log view: in the palette, drawn as a table for each source, its properties', async () => {
    const pal = [...document.querySelectorAll('#tab-widgets button.pal')].map(b => b.textContent);
    ok(pal.some(t => /ログ表示/.test(t)), 'palette: ' + pal.join(','));
    App.addWidget('log_view'); await frame();
    const p = M.st.sel, find = () => Canvas.root.querySelector('[data-path="' + U.pathStr(p) + '"]');
    let el2 = find();
    ok(el2 && el2.classList.contains('lv-log_view'), 'drawn');
    eq(M.get(p).source, 'system');
    ok([...el2.querySelectorAll('th')].map(t => t.textContent).join(',') === 'Time,Level,Source,Message', 'system columns: ' + el2.textContent.slice(0, 80));
    ok(/ERROR/.test(el2.textContent) && /INFO ▾/.test(el2.textContent), 'sample rows and the level dropdown');
    const props = [...document.querySelectorAll('#props select')];
    ok(props.some(s => [...s.options].some(o => o.value === 'operation')), 'source is a select');
    M.mutate(() => { const n = M.get(p); n.source = 'operation'; n.columns = ['time', 'target', 'new']; n.labels = { target: 'What' }; }); App.doRefresh(); await frame();
    el2 = find();
    eq([...el2.querySelectorAll('th')].map(t => t.textContent).join(','), 'Time,What,New');
    ok(!/▾/.test(el2.textContent), 'no level dropdown for the operation log');
    const g = ((M.st.project.data_log || {}).groups || [])[0];
    if (g) {
      M.mutate(() => { const n = M.get(p); n.source = 'data'; n.group = g.name; delete n.columns; delete n.labels; }); App.doRefresh(); await frame();
      el2 = find();
      const want = ['Time'].concat(g.tags.map(t => typeof t === 'string' ? t : t.tag)).join(',');
      eq([...el2.querySelectorAll('th')].map(t => t.textContent).join(','), want);
    }
    M.deleteWidget(p); App.doRefresh();
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

  await test('popup: a screen marked as popup is drawn as a box over the dimmed screen; actions are checked', async () => {
    const i = M.st.screens.findIndex(s => s.data.name === 'temp_detail');
    ok(i >= 0, 'the demo has a popup');
    M.selectScreen(i); App.doRefresh(); await frame();
    ok(Canvas.root.classList.contains('lv-popup-back'), 'dimmed screen');
    const box = Canvas.root.querySelector('.lv-popup-box');
    ok(box && Math.round(box.offsetWidth) === 420, 'box of the popup size');
    App.select([]);
    ok([...document.querySelectorAll('#props input[type=checkbox]')].some(c => c.dataset.fk && /popup$/.test(c.dataset.fk) && c.checked), 'popup checkbox on');
    eq(App.problems.length, 0, 'no problems in the demo');
    M.selectScreen(0); App.doRefresh();
    M.mutate(() => { M.screen().children[0].on_click = { action: 'popup', screen: 'main' }; }); App.doRefresh();
    ok(App.problems.some(p => /is not a popup/.test(p.text)), 'popup to a normal screen is a problem');
  });

  await test('texts: the canvas shows @id in the preview language; the Texts page edits them', async () => {
    App.select([]); App.doRefresh(); await frame();
    ok(Canvas.root.textContent.includes('ポンプ場 1号'), 'Japanese on the canvas');
    key('8', { ctrlKey: true }); App.doRefresh();
    eq(Panels.view, 'texts');
    const page = document.getElementById('view-texts');
    ok(page.querySelectorAll('table.grid tr').length > 10, 'the demo texts are listed');
    const sel = [...page.querySelectorAll('select')].find(s => [...s.options].some(o => o.value === 'en'));
    sel.value = 'en'; sel.dispatchEvent(new Event('change')); App.doRefresh();
    Panels.showView('screen'); App.doRefresh(); await frame();
    ok(Canvas.root.textContent.includes('Pump Station #1'), 'English preview');
    M.st.previewLang = null;
    eq(App.problems.length, 0, 'the demo has no text problems');
    Panels.showView('texts'); App.doRefresh();
    const cell = document.querySelector('#view-texts input[data-fk="txt.title.en"]');
    ok(cell, 'translation cell');
    cell.value = ''; cell.dispatchEvent(new Event('change')); App.doRefresh();
    ok(App.problems.some(p => /title.*has no en/.test(p.text)), 'a missing translation is a problem');
    Panels.showView('screen'); Panels.show('project'); App.doRefresh();
    ok(document.querySelector('[data-fk="proj.font.file"]').value.includes('NotoSansJP'), 'the font file on the Project tab');
    Panels.show('widgets');
  });

  await test('screen saver: set on the Project tab; nothing is written while it is empty', async () => {
    Panels.show('project'); App.doRefresh();
    const f = document.querySelector('[data-fk="proj.ss.after_s"]');
    ok(f, 'after_s field');
    eq(f.value, '600', 'the demo value');
    f.value = '120'; f.dispatchEvent(new Event('change')); App.doRefresh();
    eq(M.st.project.screen_saver.after_s, 120);
    const g = document.querySelector('[data-fk="proj.ss.after_s"]');
    g.value = ''; g.dispatchEvent(new Event('change')); App.doRefresh();
    eq(M.st.project.screen_saver.after_s, undefined, 'emptied');
    ok(M.tagChoices().some(t => t.name === 'sys.screen.off'), 'sys.screen.off in the tag list');
    Panels.show('widgets');
  });

  await test('animation: a ready one is added from the properties; a wrong one is a problem', async () => {
    App.addWidget('label'); await frame();
    const w = M.selected();
    const btn = name => document.querySelector('[data-fk$=".animate.add.' + name + '"]');
    ok(btn('blink') && btn('spin') && btn('move'), 'the buttons');
    btn('blink').click(); App.doRefresh();
    eq(M.selected().animate.preset, 'blink');
    ok(M.selected().animate.when, 'with a tag as its condition');
    btn('move').click(); App.doRefresh();
    ok(Array.isArray(M.selected().animate) && M.selected().animate.length === 2, 'a second one makes a list');
    eq(App.problems.filter(p => /^animate/.test(p.text)).length, 0, 'no problems');
    M.mutate(() => { M.selected().animate = { prop: 'colour', to: 1 }; }); App.doRefresh();
    ok(App.problems.some(p => /unknown prop "colour"/.test(p.text)), 'a wrong prop is a problem');
    void w;
  });

  await test('font: the Japanese font that comes with the designer is put into the project', async () => {
    ok(App.info.fontsDir, 'the host found the fonts');
    const before = M.st.project.font;
    M.mutate(() => { delete M.st.project.font; }); Panels.show('project'); App.doRefresh();
    ok(App.problems.some(p => /no font for it/.test(p.text)), 'the demo has Japanese texts: a problem without a font');
    const btn = document.querySelector('[data-fk$=".font.add"]');
    ok(btn && !btn.disabled, 'the button');
    await App.addBundledFont(); App.doRefresh();
    eq(M.st.project.font.file, 'assets/fonts/NotoSansJP-subset.otf');
    ok(!App.problems.some(p => /no font for it/.test(p.text)), 'the problem is gone');
    const s = await host.projectStamp(M.st.dir, ['assets/fonts/NotoSansJP-subset.otf', 'assets/fonts/OFL.txt']);
    ok(!/NotoSansJP-subset\.otf:-;/.test(s.stamp) && !/OFL\.txt:-;/.test(s.stamp), 'the font and its license are in the project: ' + s.stamp);
    M.mutate(() => { M.st.project.font = before; });
    Panels.show('widgets');
  });

  await test('AI: the dialog gives the skill, the project folder and the runtime', async () => {
    document.getElementById('b-ai').click();
    const t = document.querySelector('[data-fk="ai.prompt"]');
    ok(t, 'the text for the assistant');
    ok(/hml-screens\/SKILL\.md/.test(t.value), 'names the skill: ' + t.value);
    ok(t.value.includes(M.st.dir), 'names the project folder');
    ok(/hml_runtime/.test(t.value), 'names the runtime');
    ok(App.info.skillDir, 'the host found the skill folder');
    [...document.querySelectorAll('#modal-buttons button')].pop().click();
  });

  await test('files changed outside: loaded again; with unsaved changes the user is asked', async () => {
    reset();
    M.st.dirty = false;
    await App.takeStamp();
    const st = M.st, file = st.screens[st.cur].file, name = st.screens[st.cur].data.name;
    const original = JSON.stringify(st.screens[st.cur].data, null, 2);
    const withLabel = text => { const d = JSON.parse(original); d.children.push({ type: 'label', x: 1, y: 1, text }); return JSON.stringify(d, null, 2); };
    const has = text => M.st.screens[M.st.cur].data.children.some(c => c.text === text);
    // (the designer also checks by itself every few seconds: wait for whichever check sees the change)
    const until = async cond => { for (let i = 0; i < 60 && !cond(); i++) { await App.checkOutside(); await new Promise(r => setTimeout(r, 50)); } };

    // another program writes the screen file: nothing unsaved here, so it is simply loaded
    await host.writeFiles(st.dir, [{ rel: file, text: withLabel('from outside 1') }]);
    await until(() => has('from outside 1'));
    ok(has('from outside 1'), 'the change is in the model');
    eq(M.st.screens[M.st.cur].data.name, name, 'still on the same screen');
    eq(M.st.dirty, false, 'nothing to save');
    await App.checkOutside();
    ok(document.getElementById('modal').hidden, 'not asked again without a new change');

    // with unsaved changes: asked; keeping them leaves the model alone
    M.mutate(() => { M.st.project.title = 'edited here'; });
    await host.writeFiles(M.st.dir, [{ rel: file, text: withLabel('from outside 2') }]);
    await until(() => !document.getElementById('modal').hidden);
    ok(!document.getElementById('modal').hidden, 'asked');
    const btn = label => [...document.querySelectorAll('#modal-buttons button')].find(b => b.dataset.label === label);
    ok(btn('Keep my changes') && btn('Load the files'), 'both choices');
    btn('Keep my changes').click();
    ok(!has('from outside 2') && M.st.project.title === 'edited here', 'kept');

    // ... and loading the files discards them
    await host.writeFiles(M.st.dir, [{ rel: file, text: withLabel('from outside 3') }]);
    await until(() => !document.getElementById('modal').hidden);
    btn('Load the files').click();
    for (let i = 0; i < 50 && !has('from outside 3'); i++) await new Promise(r => setTimeout(r, 50));
    ok(has('from outside 3'), 'loaded');
    ok(M.st.project.title !== 'edited here', 'the change made here is gone');

    // a file that cannot be read keeps what is open
    const before = App.stamp;
    await host.writeFiles(M.st.dir, [{ rel: file, text: '{ broken' }]);
    await until(() => App.stamp !== before);
    ok(has('from outside 3'), 'the broken file is not taken');

    await host.writeFiles(M.st.dir, [{ rel: file, text: original }]);
    await until(() => !has('from outside 3'));
    ok(!has('from outside 3'), 'back to the original');
  });

  await test('Run: the log of the runtime is shown in the designer, Stop ends the runtime', async () => {
    const wait = async (fn, what) => { for (let i = 0; i < 200; i++) { if (fn()) return; await new Promise(r => setTimeout(r, 100)); } throw new Error('timeout: ' + what); };
    const box = document.getElementById('runlog-text'), panel = document.getElementById('runlog');
    ok(panel.hidden && document.getElementById('status-run').hidden, 'nothing before the first Run');
    document.getElementById('b-run').click();
    await wait(() => !panel.hidden, 'the panel opens');
    await wait(() => /running \(/.test(box.textContent), 'the log arrives while the runtime runs');
    ok(/project '.*' loaded/.test(box.textContent), 'the start of the log is there');
    ok(App.runLog.running, 'running');
    eq(document.getElementById('runlog-state').textContent, '実行中');
    ok(!document.getElementById('status-run').hidden, 'status bar entry');
    document.getElementById('runlog-close').click();
    ok(panel.hidden, 'hidden');
    document.getElementById('status-run').click();
    ok(!panel.hidden, 'shown again from the status bar');
    document.getElementById('runlog-stop').click();
    await wait(() => !App.runLog.running, 'Stop ends the runtime');
    ok(document.getElementById('runlog-stop').disabled, 'Stop is off');
    ok(/終了しました/.test(document.getElementById('runlog-state').textContent), 'state says ended');
    panel.hidden = true;
  });

  reset();
  M.st.dirty = false; App.autosave(true);
  setLang(lang);
  return results;
})()
