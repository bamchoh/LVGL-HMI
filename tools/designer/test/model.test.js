// Unit tests for the designer's DOM-free modules (util.js, schema.js, model.js).
// Run with:  node --test tools/designer/test/model.test.js   (Node 18+, no dependencies)
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const JS_DIR = path.join(__dirname, '..', 'renderer', 'js');

// Objects created inside the vm context have their own Array/Object prototypes, which
// deepStrictEqual rejects; compare structurally through JSON instead.
const plain = v => (v === undefined ? undefined : JSON.parse(JSON.stringify(v)));
const deq = (actual, expected, msg) => assert.deepEqual(plain(actual), plain(expected), msg);

// The renderer files are classic scripts sharing globals (U, SCHEMA, M, App). Load them into a
// fresh context per test with a stub App so model mutations can be observed.
function load() {
  const ctx = vm.createContext({ console, refreshes: 0 });
  vm.runInContext('var App = { refresh() { refreshes++; } };', ctx);
  for (const f of ['util.js', 'schema.js', 'model.js']) {
    vm.runInContext(fs.readFileSync(path.join(JS_DIR, f), 'utf8'), ctx, { filename: f });
  }
  const g = vm.runInContext('({ U, SCHEMA, M })', ctx);
  g.refreshes = () => ctx.refreshes;
  return g;
}

// ---------------------------------------------------------------- util.js

test('U.parseLooseJson accepts comments, a BOM and keeps // inside strings', () => {
  const { U } = load();
  deq(U.parseLooseJson('{"a": 1}'), { a: 1 });
  deq(U.parseLooseJson('\uFEFF{"a": 1}'), { a: 1 });
  deq(U.parseLooseJson('{\n // comment\n "a": 1, /* block */ "b": "x//y /* not a comment */"\n}'), { a: 1, b: 'x//y /* not a comment */' });
  deq(U.parseLooseJson('{"s": "quote \\" and // slash"}'), { s: 'quote " and // slash' });
  assert.throws(() => U.parseLooseJson('{ not json'));
});

test('U.toJson pretty prints with a trailing newline', () => {
  const { U } = load();
  assert.equal(U.toJson({ a: [1, 2] }), '{\n  "a": [\n    1,\n    2\n  ]\n}\n');
});

test('U.clone deep copies and keeps undefined', () => {
  const { U } = load();
  const o = { a: { b: [1] } };
  const c = U.clone(o);
  deq(c, o);
  assert.notEqual(c.a, o.a);
  assert.equal(U.clone(undefined), undefined);
});

test('U.cssColor / U.hex6 understand the runtime color spellings', () => {
  const { U } = load();
  assert.equal(U.cssColor('#abc', 'x'), '#abc');
  assert.equal(U.cssColor('#AABBCC', 'x'), '#AABBCC');
  assert.equal(U.cssColor('aabbcc', 'x'), '#aabbcc');
  assert.equal(U.cssColor('0xaabbcc', 'x'), '#aabbcc');
  assert.equal(U.cssColor(0xff0000, 'x'), '#ff0000');
  assert.equal(U.cssColor(0x0000ff, 'x'), '#0000ff');
  assert.equal(U.cssColor('Red', 'x'), '#F44336');
  assert.equal(U.cssColor('blue_grey', 'x'), '#607D8B');
  assert.equal(U.cssColor('gray', 'x'), U.cssColor('grey', 'x'));
  assert.equal(U.cssColor('', 'fallback'), 'fallback');
  assert.equal(U.cssColor(undefined, 'fallback'), 'fallback');
  assert.equal(U.cssColor('nope', 'fallback'), 'fallback');

  assert.equal(U.hex6('#abc'), '#aabbcc');
  assert.equal(U.hex6('#aabbccdd'), '#aabbcc');
  assert.equal(U.hex6('nope'), '#000000');
  assert.equal(U.hex6('nope', '#123456'), '#123456');
});

test('U.cssSize maps runtime sizes to CSS', () => {
  const { U } = load();
  assert.equal(U.cssSize(120), '120px');
  assert.equal(U.cssSize('50%'), '50%');
  assert.equal(U.cssSize('CONTENT'), 'auto');
  assert.equal(U.cssSize('full'), '100%');
  assert.equal(U.cssSize('12'), '12px');
  assert.equal(U.cssSize('abc'), null);
  assert.equal(U.cssSize(''), null);
  assert.equal(U.cssSize(undefined), null);
});

test('U.num, paths and file helpers', () => {
  const { U } = load();
  assert.equal(U.num(5, 1), 5);
  assert.equal(U.num('2.5', 1), 2.5);
  assert.equal(U.num('x', 1), 1);
  assert.equal(U.num(undefined, 7), 7);

  assert.equal(U.pathStr([0, 2, 1]), '0.2.1');
  assert.equal(U.pathStr(null), '');
  deq(U.pathArr('0.2.1'), [0, 2, 1]);
  deq(U.pathArr(''), []);
  deq(U.pathArr(null), []);
  assert.equal(U.samePath([1, 2], [1, 2]), true);
  assert.equal(U.samePath([1, 2], [1, 3]), false);
  assert.equal(U.samePath([1], [1, 2]), false);
  assert.equal(U.samePath(null, [1]), false);

  assert.equal(U.basename('C:\\a\\b\\c.json'), 'c.json');
  assert.equal(U.basename('/a/b/c.json'), 'c.json');
  assert.equal(U.dirname('/a/b/c.json'), '/a/b');
  assert.equal(U.dirname('c.json'), '.');
  assert.equal(U.joinPath('/a/b/', '/c.json'), '/a/b/c.json');
  assert.equal(U.joinPath('C:\\a\\', 'c.json'), 'C:\\a/c.json');
  assert.equal(U.fileUrl('C:\\x y\\p.json'), 'file:///C:/x%20y/p.json');
  assert.equal(U.fileUrl('/tmp/p.json'), 'file:///tmp/p.json');
  assert.equal(U.esc('<a href="x">&</a>'), '&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;');
});

// ---------------------------------------------------------------- schema.js

test('SCHEMA is internally consistent', () => {
  const { SCHEMA } = load();
  for (const [type, t] of Object.entries(SCHEMA.types)) {
    assert.equal(typeof t.label, 'string', type + ' has a label');
    assert.equal(typeof t.def, 'object', type + ' has defaults');
    assert.ok(Array.isArray(t.props), type + ' has props');
    for (const p of t.props) assert.ok(p.k && p.t, type + ' prop has k and t');
  }
  for (const w of Object.keys(SCHEMA.naturalBind)) assert.ok(SCHEMA.types[w], 'naturalBind for known widget ' + w);
  deq(Object.keys(SCHEMA.driverDefaults).sort(), Object.keys(SCHEMA.drivers).sort());
  for (const [type, d] of Object.entries(SCHEMA.driverDefaults)) assert.equal(d.type, type);
  assert.ok(SCHEMA.fonts.includes(16));
  assert.ok(SCHEMA.events.includes('on_click'));
  assert.ok(SCHEMA.styleSelectors.includes('style_label'));
  assert.equal(SCHEMA.styleKeys.bg_color, 'color');
});

// ---------------------------------------------------------------- model.js

test('M.newProject creates a blank project with one screen', () => {
  const { M } = load();
  M.newProject('T', 480, 320);
  const st = M.st;
  assert.equal(st.project.title, 'T');
  deq(st.project.display, { width: 480, height: 320 });
  assert.equal(st.project.start_screen, 'main');
  deq(st.project.screens, ['screens/main.json']);
  assert.equal(st.screens.length, 1);
  assert.equal(st.screens[0].file, 'screens/main.json');
  assert.equal(st.screens[0].data.name, 'main');
  deq(st.screens[0].data.children, []);
  assert.equal(st.cur, 0);
  deq(st.sel, []);
  assert.equal(st.dirty, true);
  assert.equal(st.path, null);
  deq(M.display(), { w: 480, h: 320 });

  M.newProject();
  assert.equal(M.st.project.title, 'New HMI');
  deq(M.display(), { w: 800, h: 480 });
});

test('M.loadFromHost parses the project and screens from the host', () => {
  const { M } = load();
  M.loadFromHost({
    path: '/p/project.json', dir: '/p',
    project: '// comment\n{ "title": "X", "start_screen": "second", "tags": [{ "name": "a" }] }',
    screens: [
      { file: 'screens/first.json', text: '{ "name": "first", "children": [{ "type": "label" }] }' },
      { file: 'screens/second.json', text: '{ "children": "bad" }' },
      { file: 'screens/third.json', text: '' },
    ],
  });
  const st = M.st;
  assert.equal(st.path, '/p/project.json');
  assert.equal(st.dir, '/p');
  deq(st.project.screens, []);                   // missing array is created
  assert.equal(st.screens.length, 3);
  assert.equal(st.screens[0].data.children.length, 1);
  assert.equal(st.screens[1].data.name, 'second');            // name from file name
  deq(st.screens[1].data.children, []);          // non-array children replaced
  assert.equal(st.screens[2].data.name, 'third');             // empty text -> blank screen
  assert.equal(st.cur, 1);                                    // start_screen selects the current screen
  assert.equal(st.dirty, false);
  deq(M.screenNames(), ['first', 'second', 'third']);
  deq(M.tagNames(), ['a']);

  assert.throws(() => M.loadFromHost({ path: 'p', dir: 'd', project: '{}', screens: [{ file: 'bad.json', text: '{ oops' }] }), /bad\.json/);
});

test('M.filesToWrite syncs the screen list into project.json', () => {
  const { M, U } = load();
  M.newProject('T');
  M.addScreen('two');
  const files = M.filesToWrite();
  deq(files.map(f => f.rel), ['project.json', 'screens/main.json', 'screens/two.json']);
  deq(JSON.parse(files[0].text).screens, ['screens/main.json', 'screens/two.json']);
  assert.ok(files[1].text.endsWith('\n'));
  assert.equal(JSON.parse(files[2].text).name, 'two');
  assert.equal(files[0].text, U.toJson(M.st.project));
});

test('M.get / parent / selected / isContainer navigate widget paths', () => {
  const { M } = load();
  M.newProject('T');
  const scr = M.screen();
  scr.children = [{ type: 'panel', children: [{ type: 'label' }, { type: 'container', children: [] }] }, { type: 'button' }];
  assert.equal(M.get([]), scr);
  assert.equal(M.get([0, 1]).type, 'container');
  assert.equal(M.get([0, 5]), null);
  assert.equal(M.get([3]), null);
  assert.equal(M.get(null), null);
  assert.equal(M.parent([0, 1]), scr.children[0]);
  assert.equal(M.parent([]), null);
  M.st.sel = [1];
  assert.equal(M.selected().type, 'button');
  M.st.sel = null;
  assert.equal(M.selected(), null);
  assert.equal(M.isContainer({ type: 'panel' }), true);
  assert.equal(M.isContainer({ type: 'obj' }), true);
  assert.equal(M.isContainer({}), true);                      // no type = panel
  assert.equal(M.isContainer({ type: 'label' }), false);
  assert.equal(M.isContainer(null), false);
});

test('M.addWidget uses schema defaults, cascades positions and selects the new widget', () => {
  const { M, SCHEMA, refreshes } = load();
  M.newProject('T');
  const l = M.addWidget('label', []);
  assert.equal(l.type, 'label');
  assert.equal(l.text, SCHEMA.types.label.def.text);
  deq([l.x, l.y], [10, 10]);
  deq(M.st.sel, [0]);
  const b = M.addWidget('button', [0]);                       // a label is not a container: goes to its parent
  deq([b.x, b.y], [22, 22]);
  deq(M.st.sel, [1]);
  const p = M.addWidget('panel', null);
  assert.equal(p.w, 200);
  deq(M.st.sel, [2]);
  const inner = M.addWidget('slider', [2]);                   // into the panel
  deq(M.st.sel, [2, 0]);
  assert.equal(M.get([2]).children[0], inner);
  deq([inner.x, inner.y], [10, 10]);
  const k = M.addWidget('keyboard', []);
  assert.equal(k.x, undefined);                               // widgets with an align keep it
  assert.equal(k.align, 'bottom_mid');
  const unknown = M.addWidget('mystery', []);
  deq(Object.keys(unknown).sort(), ['type', 'x', 'y']);
  assert.equal(M.st.dirty, true);
  assert.ok(refreshes() >= 6);

  M.get([2]).flex = 'row';
  const f = M.addWidget('label', [2]);
  assert.equal(f.x, undefined);                               // flex containers position their children
});

test('M.deleteWidget / duplicateWidget / moveWidget', () => {
  const { M } = load();
  M.newProject('T');
  M.addWidget('label', []); M.addWidget('button', []); M.addWidget('switch', []);
  const names = () => M.screen().children.map(w => w.type);

  M.moveWidget([0], 1);
  deq(names(), ['button', 'label', 'switch']);
  deq(M.st.sel, [1]);
  M.moveWidget([2], 1);                                       // out of range: no change
  deq(names(), ['button', 'label', 'switch']);
  M.moveWidget([0], -1);
  deq(names(), ['button', 'label', 'switch']);
  M.moveWidget([], 1);
  M.moveWidget(null, 1);

  M.duplicateWidget([1]);
  deq(names(), ['button', 'label', 'label', 'switch']);
  assert.equal(M.get([2]).x, M.get([1]).x + 10);
  assert.equal(M.get([2]).y, M.get([1]).y + 10);
  deq(M.st.sel, [2]);
  M.duplicateWidget([]);
  assert.equal(names().length, 4);

  M.deleteWidget([2]);
  deq(names(), ['button', 'label', 'switch']);
  deq(M.st.sel, []);
  M.deleteWidget([]);
  M.deleteWidget(null);
  assert.equal(names().length, 3);
});

test('M.reparentWidget moves into the previous container or out to the grandparent', () => {
  const { M } = load();
  M.newProject('T');
  M.addWidget('panel', []);                                   // [0]
  M.addWidget('label', [1]);                                  // sibling at [1] (parent is the screen)
  assert.equal(M.screen().children.length, 2);

  M.reparentWidget([1], 'in');
  assert.equal(M.screen().children.length, 1);
  assert.equal(M.get([0, 0]).type, 'label');
  deq(M.st.sel, [0, 0]);

  M.reparentWidget([0, 0], 'out');
  assert.equal(M.screen().children.length, 2);
  assert.equal(M.get([1]).type, 'label');
  deq(M.st.sel, [1]);

  M.reparentWidget([0], 'in');                                // nothing before it: ignored
  assert.equal(M.screen().children.length, 2);
  M.reparentWidget([1], 'out');                               // already at top level: ignored
  assert.equal(M.screen().children.length, 2);
  M.addWidget('button', []);
  M.reparentWidget([2], 'in');                                // previous sibling is a label: ignored
  assert.equal(M.screen().children.length, 3);
});

test('M undo / redo restore snapshots and cap the history', () => {
  const { M } = load();
  M.newProject('T');
  M.addWidget('label', []);
  M.addWidget('button', []);
  assert.equal(M.screen().children.length, 2);
  M.undo();
  assert.equal(M.screen().children.length, 1);
  deq(M.st.sel, [0]);
  M.undo();
  assert.equal(M.screen().children.length, 0);
  M.undo();                                                   // nothing left: no-op
  assert.equal(M.screen().children.length, 0);
  M.redo();
  M.redo();
  assert.equal(M.screen().children.length, 2);
  M.redo();
  assert.equal(M.screen().children.length, 2);
  M.undo();
  M.addWidget('switch', []);                                  // a new mutation clears the redo stack
  assert.equal(M.st.redo.length, 0);
  M.redo();
  deq(M.screen().children.map(w => w.type), ['label', 'switch']);

  for (let i = 0; i < 120; i++) M.addTag();
  assert.equal(M.st.undo.length, 100);
});

test('M screens: add, remove, rename, select', () => {
  const { M, refreshes } = load();
  M.newProject('T');
  assert.equal(M.addScreen('Set up/2!'), true);
  deq(M.screenNames(), ['main', 'Set_up_2_']);
  assert.equal(M.st.screens[1].file, 'screens/Set_up_2_.json');
  assert.equal(M.st.cur, 1);
  assert.equal(M.addScreen('main'), false);                   // duplicate
  assert.equal(M.addScreen('   '), false);
  assert.equal(M.st.screens.length, 2);

  M.addScreen('third');
  M.selectScreen(0);
  assert.equal(M.st.cur, 0);
  const before = refreshes();
  M.addWidget('button', []);
  M.get([0]).on_click = { action: 'goto', screen: 'third' };
  M.addWidget('button', []);
  M.get([1]).on_press = [{ action: 'goto', screen: 'third' }, { action: 'goto', screen: 'main' }];
  M.st.project.start_screen = 'third';
  assert.ok(refreshes() > before);

  M.renameScreen(2, 'last');
  deq(M.screenNames(), ['main', 'Set_up_2_', 'last']);
  assert.equal(M.st.project.start_screen, 'last');
  assert.equal(M.get([0]).on_click.screen, 'last');
  assert.equal(M.get([1]).on_press[0].screen, 'last');
  assert.equal(M.get([1]).on_press[1].screen, 'main');
  M.renameScreen(2, '   ');                                   // empty: ignored
  assert.equal(M.screenNames()[2], 'last');

  M.removeScreen(0);
  deq(M.screenNames(), ['Set_up_2_', 'last']);
  assert.equal(M.st.cur, 0);
  M.selectScreen(1);
  M.removeScreen(1);
  assert.equal(M.st.cur, 0);
  M.removeScreen(0);                                          // the last screen cannot be removed
  assert.equal(M.st.screens.length, 1);
});

test('M tags and drivers', () => {
  const { M, SCHEMA } = load();
  M.newProject('T');
  M.addTag(); M.addTag();
  deq(M.tagNames(), ['tag1', 'tag2']);
  deq(M.st.project.tags[0], { name: 'tag1', type: 'int', init: 0 });
  M.removeTag(0);
  deq(M.tagNames(), ['tag2']);
  M.addTag();
  deq(M.tagNames(), ['tag2', 'tag3']);          // unique against existing names
  M.st.project.tags.push({ type: 'int' });
  deq(M.tagNames(), ['tag2', 'tag3']);          // unnamed tags are ignored

  M.addDriver('modbus_tcp');
  M.addDriver('unknown');
  assert.equal(M.st.project.drivers.length, 2);
  deq(M.st.project.drivers[0], SCHEMA.driverDefaults.modbus_tcp);
  assert.notEqual(M.st.project.drivers[0], SCHEMA.driverDefaults.modbus_tcp);   // a copy
  deq(M.st.project.drivers[1], { type: 'unknown' });
  M.removeDriver(0);
  deq(M.st.project.drivers, [{ type: 'unknown' }]);
});

test('M scripts: add, rename (script actions follow), trigger keys, remove', () => {
  const { M, SCHEMA } = load();
  M.newProject('T');
  assert.equal(M.st.project.scripts, undefined);              // no empty "scripts" array in projects without scripts
  deq(M.scriptNames(), []);
  assert.equal(M.addScript(), 0);
  assert.equal(M.addScript(), 1);
  deq(M.st.project.scripts, [{ name: 'script1', trigger: 'manual', code: '' }, { name: 'script2', trigger: 'manual', code: '' }]);
  deq(M.scriptNames(), ['script1', 'script2']);

  M.screen().children.push({ type: 'button', on_click: [{ action: 'script', name: 'script1' }, { action: 'script', code: 'tag.x = 1' }],
    children: [{ type: 'button', on_press: { action: 'script', name: 'script1' } }] });
  assert.equal(M.renameScript(0, 'script2'), false);           // already used
  assert.equal(M.renameScript(0, '  '), false);
  assert.equal(M.renameScript(5, 'x'), false);
  assert.equal(M.renameScript(0, ' init '), true);
  deq(M.scriptNames(), ['init', 'script2']);
  const btn = M.screen().children[0];
  deq(btn.on_click, [{ action: 'script', name: 'init' }, { action: 'script', code: 'tag.x = 1' }]);
  assert.equal(btn.children[0].on_press.name, 'init');

  const s = M.st.project.scripts[1];
  M.setScriptTrigger(1, 'interval');
  deq(s, { name: 'script2', trigger: 'interval', code: '', interval_ms: 1000 });
  M.setScriptTrigger(1, 'tag');
  deq(s, { name: 'script2', trigger: 'tag', code: '', tags: [] });
  s.tags = ['a'];
  M.setScriptTrigger(1, 'screen_show');
  s.screen = 'main';
  deq(s, { name: 'script2', trigger: 'screen_show', code: '', screen: 'main' });
  M.renameScreen(0, 'home');                                  // screen triggers follow a screen rename
  assert.equal(s.screen, 'home');
  M.setScriptTrigger(1, 'manual');
  deq(s, { name: 'script2', code: '' });
  for (const t of ['startup', 'interval', 'tag', 'screen_show', 'screen_hide', 'manual']) assert.ok(SCHEMA.scriptTriggers.includes(t));
  assert.ok(SCHEMA.actions.script);

  M.addScript();
  deq(M.scriptNames(), ['init', 'script2', 'script3']);
  M.removeScript(0); M.removeScript(0);
  deq(M.scriptNames(), ['script3']);
  M.removeScript(0);
  assert.equal(M.st.project.scripts, undefined);
  M.undo();
  deq(M.scriptNames(), ['script3']);
  assert.ok(M.filesToWrite()[0].text.includes('"scripts"'));
});

test('M system log: section exists only while enabled or customised', () => {
  const { M } = load();
  M.newProject('T');
  assert.equal(M.st.project.system_log, undefined);              // off by default: no section
  assert.equal(M.systemLogOn(), false);
  M.setSystemLog(true);
  deq(M.st.project.system_log, {});
  assert.equal(M.systemLogOn(), true);
  assert.ok(M.filesToWrite()[0].text.includes('"system_log"'));
  M.setSystemLog(false);                                        // nothing customised: the section goes away
  assert.equal(M.st.project.system_log, undefined);
  M.setSystemLog(true);
  M.st.project.system_log.retention_days = 7;
  M.setSystemLog(false);                                        // settings are kept while switched off
  deq(M.st.project.system_log, { retention_days: 7, enabled: false });
  assert.equal(M.systemLogOn(), false);
  M.setSystemLog(true);
  deq(M.st.project.system_log, { retention_days: 7 });
  M.undo();
  assert.equal(M.systemLogOn(), false);
});

test('M logs: data log / alarms / operation log sections', () => {
  const { M, SCHEMA } = load();
  M.newProject('T');
  for (const key of ['data_log', 'alarms', 'operation_log']) {
    assert.equal(M.logOn(key), false);
    M.setLog(key, true);
    deq(M.st.project[key], {});
    assert.equal(M.logOn(key), true);
    M.setLog(key, false);
    assert.equal(M.st.project[key], undefined);
  }
  M.setLog('data_log', true);
  M.st.project.data_log.groups = [];                          // an empty table is not worth keeping
  M.setLog('data_log', false);
  assert.equal(M.st.project.data_log, undefined);
  M.setLog('alarms', true);
  M.st.project.alarms.items = [{ name: 'hot', tag: 'temp', condition: 'gt', value: '30' }];
  M.setLog('alarms', false);                                  // definitions are kept while switched off
  assert.equal(M.st.project.alarms.enabled, false);
  assert.equal(M.st.project.alarms.items.length, 1);
  assert.equal(M.logOn('alarms'), false);
  M.setLog('alarms', true);
  assert.equal(M.st.project.alarms.enabled, undefined);
  assert.ok(M.filesToWrite()[0].text.includes('"alarms"'));
  for (const t of ['interval', 'change', 'tag', 'manual']) assert.ok(SCHEMA.dataLogTriggers.includes(t));
  for (const c of ['true', 'false', 'eq', 'ne', 'gt', 'lt', 'ge', 'le']) assert.ok(SCHEMA.alarmConditions.includes(c));
});

// ---------------------------------------------------------------- users
test('U.sha256hex matches the reference values', () => {
  const { U } = load();
  assert.equal(U.sha256hex(''), 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
  assert.equal(U.sha256hex('abc'), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  assert.equal(U.sha256hex('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'), '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1');
  const crypto = require('node:crypto');
  for (const t of ['s1:1234', 'x'.repeat(55), 'x'.repeat(56), 'x'.repeat(64), 'x'.repeat(1000), '日本語'])
    assert.equal(U.sha256hex(t), crypto.createHash('sha256').update(t, 'utf8').digest('hex'), t.slice(0, 10));
});

test('M users: section, PIN hashes, warnings', () => {
  const { M, U } = load();
  M.newProject('t', 480, 320);
  assert.equal(M.usersOn(), false);
  deq(M.userProblems(), []);
  M.setUsers(true);
  assert.equal(M.usersOn(), true);
  deq(M.st.project.users.levels, ['Operator', 'Maintenance', 'Admin']);
  deq(M.userProblems(), ['no user is defined: nobody can log in']);

  M.addUser(); M.addUser();
  deq(M.st.project.users.list.map(u => u.name), ['user1', 'user2']);
  assert.ok(M.userProblems().some(w => w.includes('"user1" has no PIN')));
  assert.equal(M.validPin('1234'), true);
  assert.equal(M.validPin('123'), false);
  assert.equal(M.validPin('12a4'), false);
  assert.equal(M.validPin('123456789'), false);
  assert.equal(M.setUserPin(0, '12'), false);
  assert.equal(M.setUserPin(0, '1234'), true);
  const u0 = M.st.project.users.list[0];
  assert.equal(u0.pin, undefined);
  const [salt, hash] = u0.pin_hash.split('$');
  assert.equal(salt.length, 16);
  assert.equal(hash, U.sha256hex(salt + ':1234'));              // what the runtime checks
  assert.equal(JSON.stringify(M.st.project).includes('1234'), false);
  assert.equal(M.hashPin('1234', 'abcd'), 'abcd$' + U.sha256hex('abcd:1234'));

  // nobody at the system level (default: the highest) yet
  M.setUserPin(1, '5678');
  assert.ok(M.userProblems().some(w => w.includes('no user has level 3')));
  M.mutate(() => { M.st.project.users.list[1].level = 3; });
  deq(M.userProblems(), []);
  M.mutate(() => { M.st.project.users.list[1].name = 'USER1'; });
  assert.ok(M.userProblems().some(w => w.includes('defined twice')));
  M.mutate(() => { M.st.project.users.list[1].name = 'bad$'; });
  assert.ok(M.userProblems().some(w => w.includes('a name is 1 to 16')));
  M.mutate(() => { M.st.project.users.list[1].name = 'admin'; M.st.project.users.system_level = 0; M.st.project.users.list[1].level = 1; });
  deq(M.userProblems(), []);                                     // system_level 0: no restriction, nobody needed

  // switching off keeps the users; switching on again brings them back
  M.setUsers(false);
  assert.equal(M.st.project.users.enabled, false);
  assert.equal(M.usersOn(), false);
  M.setUsers(true);
  assert.equal(M.st.project.users.enabled, undefined);
  assert.equal(M.st.project.users.list.length, 2);
  M.removeUser(0); M.removeUser(0);
  M.mutate(() => { delete M.st.project.users.system_level; });
  M.setUsers(false);
  assert.equal(M.st.project.users, undefined);                   // nothing worth keeping
});

test('schema: login / logout actions and the access properties exist', () => {
  const { SCHEMA } = load();
  deq(SCHEMA.actions.login, []);
  deq(SCHEMA.actions.logout, []);
  deq(SCHEMA.access.map(f => f.k), ['level', 'level_deny']);
});
