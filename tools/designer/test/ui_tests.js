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

  reset();
  setLang(lang);
  return results;
})()
