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
  deq(Object.keys(u0).sort(), ['level', 'name', 'pin_hash']);    // nothing else is kept of the PIN (the hash is hex and may contain any digits)
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

// ---------------------------------------------------------------- local settings (per machine)
test('M.driverKey names drivers like the runtime does; local driver settings are set and pruned', () => {
  const { M } = load();
  M.newProject('t', 480, 320);
  M.mutate(() => { M.st.project.drivers = [{ type: 'sim' }, { type: 'modbus_rtu', name: 'PLC' }, { type: 'sim' }, { type: 'modbus_rtu' }, {}]; });
  deq([0, 1, 2, 3, 4].map(i => M.driverKey(i)), ['sim', 'PLC', 'sim#2', 'modbus_rtu', 'driver']);

  let local = M.withLocalDriverSetting({}, 'PLC', 'device', 'COM4');
  deq(local, { drivers: { PLC: { device: 'COM4' } } });
  const before = JSON.stringify(local);
  let next = M.withLocalDriverSetting(local, 'modbus_rtu', 'device', 'COM7');
  assert.equal(JSON.stringify(local), before);                  // the input is not changed
  deq(next, { drivers: { PLC: { device: 'COM4' }, modbus_rtu: { device: 'COM7' } } });
  // other local settings (users edited on a device, other driver values) are kept
  next = M.withLocalDriverSetting({ users: { list: [] }, drivers: { PLC: { device: 'COM4', host: 'x' } } }, 'PLC', 'device', '');
  deq(next, { users: { list: [] }, drivers: { PLC: { host: 'x' } } });
  deq(M.withLocalDriverSetting({ drivers: { PLC: { device: 'COM4' } } }, 'PLC', 'device', ''), {});
  deq(M.withLocalDriverSetting(null, 'PLC', 'device', ''), {});
});

// ---------------------------------------------------------------- project as one file
test('project files: name helpers and where the files live', () => {
  const { U, M } = load();
  assert.equal(U.isProjectFile('D:/work/line1.hmlproj'), true);
  assert.equal(U.isProjectFile('D:\work\LINE1.HMLPROJ'), true);
  assert.equal(U.isProjectFile('D:/work/project.json'), false);
  assert.equal(U.isProjectFile(''), false);
  assert.equal(U.isProjectFile(null), false);
  assert.equal(U.safeName('My Test HMI'), 'my-test-hmi');
  assert.equal(U.safeName('  ライン 1 / Pump #2 '), '1-pump-2');
  assert.equal(U.safeName('日本語', 'hmi-project'), 'hmi-project');
  assert.equal(U.safeName('..hidden..'), 'hidden');

  M.newProject('t', 480, 320);
  assert.equal(M.runPath(), null);                               // not saved yet
  assert.equal(M.st.archive, false);
  // a folder project: the files are where project.json is
  M.loadFromHost({ path: 'D:/p/line1/project.json', dir: 'D:/p/line1', project: '{"screens":[]}', screens: [], archive: false, name: 'line1' });
  assert.equal(M.runPath(), 'D:/p/line1/project.json');
  assert.equal(M.st.name, 'line1');
  // a project file: the user's path is the file, the files are in its working folder
  M.loadFromHost({ path: 'D:/p/line1.hmlproj', dir: 'C:/Users/x/AppData/Local/hml/work/line1-0123456789/', project: '{"screens":[]}', screens: [], archive: true, name: 'line1' });
  assert.equal(M.st.path, 'D:/p/line1.hmlproj');
  assert.equal(M.st.archive, true);
  assert.equal(M.runPath(), 'C:/Users/x/AppData/Local/hml/work/line1-0123456789/project.json');
  M.newProject('t', 480, 320);
  assert.equal(M.st.archive, false);
  assert.equal(M.st.name, '');
});

// ---------------------------------------------------------------- language, display presets
test('I18N: property names and fixed texts in Japanese, everything as it is in English', () => {
  const ctx = vm.createContext({ console, navigator: { language: 'ja-JP' } });
  vm.runInContext(fs.readFileSync(path.join(JS_DIR, 'i18n.js'), 'utf8'), ctx, { filename: 'i18n.js' });
  const I18N = vm.runInContext('I18N', ctx);
  I18N.init();                                                   // nothing saved: the system's language
  assert.equal(I18N.lang, 'ja');
  assert.equal(I18N.label('bg_color'), '背景色');
  assert.equal(I18N.named('bg_color'), true);
  assert.equal(I18N.label('some_new_key'), 'some_new_key');      // no entry: shown as it is
  assert.equal(I18N.named('some_new_key'), false);
  assert.equal(I18N.t('Save'), '保存');
  assert.equal(I18N.t('A text nobody translated'), 'A text nobody translated');
  assert.equal(I18N.label('Display'), '表示');                   // a fixed text used as a label
  assert.equal(I18N.label('toString'), 'toString');              // not something inherited from Object
  I18N.init('en');                                               // what was chosen before wins
  assert.equal(I18N.lang, 'en');
  assert.equal(I18N.label('bg_color'), 'bg_color');
  assert.equal(I18N.named('bg_color'), false);
  assert.equal(I18N.t('Save'), 'Save');
  I18N.set('xx');
  assert.equal(I18N.lang, 'en');
  I18N.init('xx');                                               // an unknown saved value: back to the system's
  assert.equal(I18N.lang, 'ja');
});

test('every style property and widget property has a Japanese name', () => {
  const { SCHEMA } = load();
  const ctx = vm.createContext({ console });
  for (const f of ['i18n.js', 'i18n_ja.js']) vm.runInContext(fs.readFileSync(path.join(JS_DIR, f), 'utf8'), ctx, { filename: f });
  const KEYS = vm.runInContext('I18N.KEYS_JA', ctx), TEXT = vm.runInContext('I18N.TEXT_JA', ctx);
  const missing = [];
  for (const k of Object.keys(SCHEMA.styleKeys)) if (!(k in KEYS)) missing.push('style ' + k);
  for (const [type, t] of Object.entries(SCHEMA.types)) {
    for (const f of t.props || []) if (!(f.k in KEYS)) missing.push(type + '.' + f.k);
    if (!(t.label in TEXT)) missing.push('palette ' + t.label);
  }
  for (const f of [].concat(SCHEMA.common, SCHEMA.flags, SCHEMA.flex, SCHEMA.access)) if (!(f.k in KEYS)) missing.push(f.k);
  deq(missing, []);
});

test('M display presets and touch calibration', () => {
  const { M, SCHEMA } = load();
  M.newProject('t', 800, 480);
  const lcd = SCHEMA.displayPresets.find(p => p.touch);
  M.applyDisplayPreset(lcd);
  deq(M.st.project.display, { width: 480, height: 320, touch: { swap_axes: true, calibration: [150, 3850, 3850, 220], jitter: 12, smooth: 3, settle: 1, release: 1, lift_ms: 60, scroll_limit: 8 } });
  M.st.project.display.touch.calibration[0] = 1;                 // the preset itself is not what the project holds
  assert.equal(lcd.touch.calibration[0], 150);
  M.setTouchCalibration(2, 3900);
  deq(M.st.project.display.touch.calibration, [1, 3850, 3900, 220]);
  M.setTouchCalibration(1, '');                                  // an empty field: no calibration at all
  assert.equal(M.st.project.display.touch.calibration, undefined);
  assert.equal(M.st.project.display.touch.swap_axes, true);
  M.setTouchCalibration(0, 100);                                 // starting again from the full range
  deq(M.st.project.display.touch.calibration, [100, 0, 4095, 4095]);
  M.applyDisplayPreset(SCHEMA.displayPresets.find(p => !p.touch));
  assert.equal(M.st.project.display.touch, undefined);           // a display that needs no correction
  assert.equal(M.st.project.display.width, 800);
});

// ---------------------------------------------------------------- several widgets, clipboard, problems
function threeButtons() {
  const { M, U } = load();
  M.newProject('T', 800, 480);
  M.screen().children = [
    { type: 'button', x: 10, y: 10, w: 100, h: 40 },
    { type: 'button', x: 200, y: 30, w: 60, h: 20 },
    { type: 'button', x: 500, y: 90, w: 80, h: 50 },
    { type: 'panel', x: 0, y: 200, children: [{ type: 'label', text: 'in' }] },
  ];
  return { M, U };
}
const rectsOf = (M, sels) => sels.map(p => { const w = M.get(p); return { path: p, x: w.x || 0, y: w.y || 0, w: w.w, h: w.h }; });

test('M selection: Ctrl+click adds and removes widgets of one parent', () => {
  const { M } = threeButtons();
  M.st.sel = [0]; M.st.more = [];
  M.toggleSelect([2]);
  deq(M.selection(), [[0], [2]]);
  M.toggleSelect([3, 0]);                                        // another parent: a new selection
  deq(M.selection(), [[3, 0]]);
  M.st.sel = [0]; M.st.more = [];
  M.toggleSelect([1]); M.toggleSelect([0]);                      // taking the first one out
  deq(M.selection(), [[1]]);
  M.toggleSelect([1]);                                           // the last one stays
  deq(M.selection(), [[1]]);
  M.selectSiblings();
  assert.equal(M.selection().length, 4);
  M.st.sel = []; deq(M.selection(), []);                         // the screen itself is not part of it
});

test('M arrange: align, distribute, same size - one undo step', () => {
  const { M } = threeButtons();
  const sels = [[0], [1], [2]];
  M.arrange('left', rectsOf(M, sels));
  deq(M.screen().children.slice(0, 3).map(w => w.x), [10, 10, 10]);
  M.undo();
  deq(M.screen().children.slice(0, 3).map(w => w.x), [10, 200, 500]);
  M.arrange('right', rectsOf(M, sels));
  deq(M.screen().children.slice(0, 3).map(w => w.x + w.w), [580, 580, 580]);
  M.undo();
  M.arrange('vcenter', rectsOf(M, sels));                        // box: y 10 .. 140, centre 75
  deq(M.screen().children.slice(0, 3).map(w => w.y + w.h / 2), [75, 75, 75]);
  M.undo();
  M.arrange('hdist', rectsOf(M, sels));                          // 10..580, widths 240 -> gaps of 165
  deq(M.screen().children.slice(0, 3).map(w => w.x), [10, 275, 500]);
  M.undo();
  M.arrange('samew', rectsOf(M, sels));
  deq(M.screen().children.slice(0, 3).map(w => w.w), [100, 100, 100]);
  const undos = M.st.undo.length;
  M.arrange('samew', rectsOf(M, sels));                          // nothing to do: no undo step
  M.arrange('left', rectsOf(M, [[0]]));
  assert.equal(M.st.undo.length, undos);
});

test('M copy and paste: several widgets, onto another screen, not on top of each other', () => {
  const { M } = threeButtons();
  M.st.sel = [1]; M.st.more = [[0]];
  const clip = M.copySelection();
  deq(clip.map(w => w.x), [10, 200]);                            // in the order on the screen
  M.paste(clip);                                                 // next to the selected widget: same container
  assert.equal(M.screen().children.length, 6);
  deq(M.screen().children.slice(4).map(w => [w.x, w.y]), [[20, 20], [210, 40]]);
  deq(M.selection(), [[4], [5]]);
  M.addScreen('second'); M.selectScreen(1);
  M.paste(clip);
  deq(M.screen().children.map(w => [w.x, w.y]), [[10, 10], [200, 30]]);
  clip[0].x = 999;                                               // the clipboard is a copy
  assert.equal(M.screen().children[0].x, 10);
  M.selectScreen(0); M.st.sel = [3];                             // a panel is selected: into it
  M.paste(clip.slice(1));
  assert.equal(M.get([3]).children.length, 2);
});

test('M delete / duplicate / nudge work on the whole selection', () => {
  const { M } = threeButtons();
  M.st.sel = [0]; M.st.more = [[2]];
  M.nudgeSelection(5, -2);
  deq([M.get([0]).x, M.get([0]).y, M.get([2]).x, M.get([2]).y, M.get([1]).x], [15, 8, 505, 88, 200]);
  deq(M.selection(), [[0], [2]]);                                // still selected
  M.duplicateSelection();
  assert.equal(M.screen().children.length, 6);
  deq(M.selection(), [[3], [4]]);
  M.deleteSelection();
  assert.equal(M.screen().children.length, 4);
  M.undo(); M.undo();
  assert.equal(M.screen().children.length, 4);
  deq(M.selection(), [[0], [2]]);                                // undo brings the selection back
});

test('M problems: names that do not exist', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  assert.equal(M.problems().length, 0);
  M.st.project.tags = [{ name: 'a', type: 'int' }, { name: 'a', type: 'int' }];
  M.screen().children = [
    { type: 'label', bind: { text: 'nope' } },
    { type: 'button', bind: { hidden: { tag: 'a', eq: { tag: 'gone' } } }, on_click: [{ action: 'set', tag: 'a', value: 1 }, { action: 'goto', screen: 'nowhere' }, { action: 'script', name: 'x' }] },
    { type: 'label', bind: { text: 'sys.user.name' }, level: 2 },
  ];
  const texts = M.problems().map(p => p.text);
  for (const t of ['tag "a" is defined twice', 'unknown tag "nope" (text)', 'unknown tag "gone" (hidden)', 'unknown screen "nowhere" (goto)', 'unknown script "x"',
    'a level is set, but user management is off (Users tab)']) assert.ok(texts.includes(t), t + ' / ' + texts.join(' | '));
  assert.equal(texts.length, 6);
  const p = M.problems().find(x => /nope/.test(x.text));
  deq([p.screen, p.path], [0, [0]]);
});

test('I18N: texts with values in them, and every problem text has a Japanese form', () => {
  const ctx = vm.createContext({ console, navigator: { language: 'ja-JP' } });
  for (const f of ['i18n.js', 'i18n_ja.js']) vm.runInContext(fs.readFileSync(path.join(JS_DIR, f), 'utf8'), ctx, { filename: f });
  const I18N = vm.runInContext('I18N', ctx);
  I18N.init();
  assert.equal(I18N.t('saved D:/x/project.json'), '保存しました: D:/x/project.json');
  assert.equal(I18N.f('{0} problem(s)', 3), '問題 3 件');
  for (const t of ['tag "a" is defined twice', 'unknown tag "nope" (text)', 'unknown screen "nowhere" (goto)', 'unknown script "x"', 'plc: unknown tag "t"',
    'start screen "s" does not exist', 'script "s": unknown tag "t"', 'plc: no serial port (device) is set']) assert.notEqual(I18N.t(t), t, t);
  I18N.set('en');
  assert.equal(I18N.t('saved D:/x/project.json'), 'saved D:/x/project.json');
  assert.equal(I18N.f('{0} problem(s)', 3), '3 problem(s)');
});

test('M snap: edges and centres of the other widgets, within the tolerance', () => {
  const { M } = load();
  const others = [{ x: 0, y: 0, w: 800, h: 480 }, { x: 100, y: 50, w: 200, h: 40 }];
  deq(M.snap({ x: 103, y: 200, w: 50, h: 30 }, others, 5), { dx: -3, dy: 0, gx: 100, gy: null });      // left edges
  deq(M.snap({ x: 252, y: 91, w: 50, h: 30 }, others, 5), { dx: -2, dy: -1, gx: 300, gy: 90 });         // right edge to right edge, top to bottom
  deq(M.snap({ x: 373, y: 223, w: 50, h: 30 }, others, 5), { dx: 2, dy: 2, gx: 400, gy: 240 });         // the middle of the screen
  deq(M.snap({ x: 500, y: 300, w: 50, h: 30 }, others, 5), { dx: 0, dy: 0, gx: null, gy: null });
});

test('M addWidget at a position (dropped from the palette); tag choices', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.addWidget('button', [], { x: 120.4, y: -3 });
  const w = M.screen().children[0];
  deq([w.type, w.x, w.y, w.align], ['button', 120, 0, undefined]);
  M.undo();
  assert.equal((M.screen().children || []).length, 0);
  M.st.project.tags = [{ name: 'temp', type: 'float' }, { type: 'int' }];
  deq(M.tagChoices().filter(t => !/^sys\./.test(t.name)), [{ name: 'temp', note: 'float' }]);
  assert.ok(M.tagChoices().some(t => t.name === 'sys.alarm.unacked'));      // made by the runtime, always there
  M.setUsers(true);
  assert.ok(M.tagChoices().some(t => t.name === 'sys.user.name'));
});

test('I18N: values of choices keep the value visible', () => {
  const ctx = vm.createContext({ console, navigator: { language: 'ja-JP' } });
  for (const f of ['i18n.js', 'i18n_ja.js']) vm.runInContext(fs.readFileSync(path.join(JS_DIR, f), 'utf8'), ctx, { filename: f });
  const I18N = vm.runInContext('I18N', ctx);
  I18N.init();
  assert.equal(I18N.value('center'), '中央  (center)');
  assert.equal(I18N.value('int16'), 'int16');
  assert.equal(I18N.action('goto'), '画面を切り替える  (goto)');
  I18N.set('en');
  assert.equal(I18N.value('center'), 'center');
  assert.equal(I18N.action('goto'), 'goto');
});

// ---------------------------------------------------------------- tag uses, rename, moving in the tree
function tagProject() {
  const { M } = load();
  M.newProject('T', 800, 480);
  const p = M.st.project;
  p.tags = [{ name: 'temp', type: 'float' }, { name: 'run', type: 'bool' }, { name: 'spare', type: 'int' }];
  M.screen().children = [
    { type: 'label', bind: { text: 'temp', hidden: { tag: 'run', eq: { tag: 'temp' } } } },
    { type: 'button', bind: 'run', on_click: [{ action: 'toggle', tag: 'run' }, { action: 'script', code: 'tag.temp = tag.temp + 1  -- tag.temperature stays' }] },
    { type: 'chart', series: [{ tag: 'temp' }] },
    { type: 'panel', children: [{ type: 'label', text: 'a' }, { type: 'label', text: 'b' }] },
  ];
  p.drivers = [{ type: 'modbus_tcp', status_tag: 'run', map: [{ tag: 'temp', addr: 0 }] }, { type: 'opcua_server', tags: ['temp'] }];
  p.scripts = [{ name: 's', trigger: 'tag', tags: ['run'], code: 'if tag.run then tag.temp = 0 end' }];
  p.data_log = { groups: [{ name: 'g', tags: ['temp', 'run'], trigger_tag: 'run' }] };
  p.alarms = { items: [{ name: 'a', tag: 'temp', value_tag: 'temp' }] };
  return { M, p };
}

test('M tagUses: every place that names a tag', () => {
  const { M } = tagProject();
  const uses = M.tagUses();
  assert.equal(uses.get('temp').length, 10);
  assert.equal(uses.get('run').length, 8);
  assert.equal(uses.has('spare'), false);
  deq(uses.get('temp')[0], { loc: { screen: 0, path: [0] }, what: 'text' });
  assert.equal(M.problems().length, 0);
  M.st.project.tags.pop(); M.st.project.tags.pop();                      // "run" is gone: reported, but not from Lua code
  const pr = M.problems().map(x => x.text);
  assert.equal(pr.length, 7, pr.join(' | '));
  assert.ok(pr.every(t => /unknown tag "run"/.test(t)));
});

test('M renameTag: everything that uses the tag follows, in one undo step', () => {
  const { M, p } = tagProject();
  const before = JSON.stringify(M.st.screens) + JSON.stringify(p);
  assert.equal(M.renameTag(0, 'temp_pv'), 10);
  const s = M.screen().children;
  deq(s[0].bind, { text: 'temp_pv', hidden: { tag: 'run', eq: { tag: 'temp_pv' } } });
  assert.equal(s[1].on_click[1].code, 'tag.temp_pv = tag.temp_pv + 1  -- tag.temperature stays');
  assert.equal(s[2].series[0].tag, 'temp_pv');
  deq([p.drivers[0].map[0].tag, p.drivers[1].tags[0], p.scripts[0].code], ['temp_pv', 'temp_pv', 'if tag.run then tag.temp_pv = 0 end']);
  deq([p.data_log.groups[0].tags, p.alarms.items[0].tag, p.alarms.items[0].value_tag], [['temp_pv', 'run'], 'temp_pv', 'temp_pv']);
  assert.equal(M.problems().length, 0);
  M.undo();
  assert.equal(JSON.stringify(M.st.screens) + JSON.stringify(M.st.project), before);   // (undo replaces the project object)
  assert.equal(M.renameTag(1, 'temp'), 0);                               // the name is taken: only the tag itself changes
  assert.equal(M.screen().children[1].bind, 'run');
  M.undo();
  assert.equal(M.renameTag(1, 'run'), 0);                                // unchanged: nothing happens
  assert.equal(M.st.undo.length, 0);
});

test('M renameTag: the conditions of "animate" and script code written as lines follow too', () => {
  const { M, p } = tagProject();
  const s = M.screen().children;
  s[3].animate = [{ preset: 'blink', when: 'temp' }, { preset: 'pulse', when: { tag: 'run', gt: { tag: 'temp' } } }];
  p.scripts.push({ name: 'lines', trigger: 'interval', code: ['local t = tag.temp', 'tag.temp = t + 1'] });
  assert.equal(M.tagUses().get('temp').length, 14);
  M.renameTag(0, 'temp_pv');
  deq(M.screen().children[3].animate, [{ preset: 'blink', when: 'temp_pv' }, { preset: 'pulse', when: { tag: 'run', gt: { tag: 'temp_pv' } } }]);
  deq(M.st.project.scripts[1].code, ['local t = tag.temp_pv', 'tag.temp_pv = t + 1']);
  assert.equal(M.problems().length, 0);
});

// ---------------------------------------------------------------- find and replace, copies of screens, another size (issue #71)

function lineProject() {
  const { M, U, SCHEMA } = load();
  M.newProject('T', 800, 480);
  const p = M.st.project;
  p.tags = [{ name: 'line1_speed', type: 'int', min: 0, max: 100 }, { name: 'line1_run', type: 'bool' }, { name: 'line2_speed', type: 'int' }, { name: 'total', type: 'int' }];
  M.screen().name = 'line1';
  M.screen().children = [
    { type: 'label', x: 10, y: 20, text: 'Line 1 speed', bind: { text: { tag: 'line1_speed', format: '%d' }, visible: { tag: 'line1_run', eq: 1 } } },
    { type: 'button', x: 10, y: 60, w: 120, h: 48, text: 'Start line 1', bind: 'line1_run',
      on_click: [{ action: 'set', tag: 'line1_run', value: true }, { action: 'script', code: 'tag.total = tag.line1_speed + tag.line2_speed' }] },
    { type: 'dropdown', x: 10, y: 120, options: ['Line 1', 'Line 2', 'line 1 (slow)'], bind: 'total' },
    { type: 'panel', x: 200, y: 20, w: 300, h: 200, style: { pad: 8, radius: 6, font: 24 }, children: [
      { type: 'label', x: 0, y: 0, w: '50%', h: 'content', text: 'Line 1', style: { font: 16 } },
      { type: 'line', points: [[0, 0], [100, 50]] }] },
  ];
  M.addScreen('other');
  M.screen().children = [{ type: 'label', text: 'Line 1 again', bind: { text: 'line1_speed' } }];
  M.st.cur = 0;
  p.alarms = { items: [{ name: 'fast', tag: 'line1_speed', condition: 'gt', value: 90, message: 'Line 1 is too fast' }] };
  p.drivers = [{ type: 'sim', name: 'sim', items: [{ tag: 'line1_speed', wave: 'sine' }] }];
  return { M, U, SCHEMA, p };
}

test('M replace: the plan lists what would change and changes nothing; applying is one step that can be undone', () => {
  const { M, p } = lineProject();
  const snap = JSON.stringify([p, M.st.screens]);
  const plan = M.replacePlan({ what: 'tags', find: 'line1_', replace: 'line3_', scope: 'project' });
  assert.equal(JSON.stringify([p, M.st.screens]), snap, 'a plan changes nothing');
  assert.ok(plan.length >= 8, plan.map(c => c.what + ':' + c.before).join(' '));
  assert.ok(plan.every(c => /^line1_/.test(c.before) && /^line3_/.test(c.after)));
  assert.ok(plan.some(c => c.loc.screen === 1), 'the other screen');
  assert.ok(plan.some(c => c.loc.view === 'logs'), 'the alarm');
  assert.ok(plan.some(c => c.loc.view === 'drivers'), 'the driver');
  assert.ok(plan.some(c => c.what === 'lua'));
  const undoBefore = M.st.undo.length;
  const r = M.replaceApply({ what: 'tags', find: 'line1_', replace: 'line3_', scope: 'project' });
  assert.equal(r.changed, plan.length);
  assert.equal(M.st.undo.length, undoBefore + 1, 'one undo step');
  const w = M.screen().children;
  assert.equal(w[0].bind.text.tag, 'line3_speed');
  assert.equal(w[0].bind.visible.tag, 'line3_run');
  assert.equal(w[1].bind, 'line3_run');
  assert.equal(w[1].on_click[0].tag, 'line3_run');
  assert.equal(w[1].on_click[1].code, 'tag.total = tag.line3_speed + tag.line2_speed');
  assert.equal(M.st.project.alarms.items[0].tag, 'line3_speed');
  assert.equal(M.st.project.drivers[0].items[0].tag, 'line3_speed');
  assert.equal(M.st.screens[1].data.children[0].bind.text, 'line3_speed');
  // the tags line3_* do not exist: that is said
  assert.ok(r.newProblems.length >= 2, 'new problems: ' + r.newProblems.map(x => x.text).join(' | '));
  assert.ok(r.newProblems.every(x => /line3_/.test(x.text)), r.newProblems.map(x => x.text).join(' | '));
  M.undo();
  assert.equal(JSON.stringify([M.st.project, M.st.screens]), snap, 'undone');
});

test('M replace: this screen / all screens / the project; the whole name or a part; with or without case', () => {
  const n = opt => { const { M } = lineProject(); return M.replacePlan(Object.assign({ what: 'tags', find: 'line1_speed', replace: 'x' }, opt)).length; };
  const all = n({ scope: 'project' }), screens = n({ scope: 'screens' }), one = n({ scope: 'screen' });
  assert.equal(one, 2);                               // the label and the Lua code of the button
  assert.equal(screens, 3);                           // ... and the other screen
  assert.equal(all, 5);                               // ... the alarm and the driver
  assert.equal(n({}), all);
  assert.equal(n({ find: 'speed', whole: true }), 0);
  assert.equal(n({ find: 'speed' }), all + 1);        // line2_speed in the code too
  assert.equal(n({ find: 'LINE1_SPEED' }), 0);
  assert.equal(n({ find: 'LINE1_SPEED', matchCase: false }), all);
  assert.equal(n({ find: 'LINE1_SPEED', matchCase: false, whole: true }), all);
  assert.equal(n({ find: '' }), 0);
  assert.equal(n({ find: 'line1_speed', replace: 'line1_speed' }), 0);
  // what looks like a pattern is text; what looks like a replacement pattern is text
  const { M } = lineProject();
  M.st.project.tags.push({ name: 'a.b', type: 'int' }, { name: 'aXb', type: 'int' });
  M.screen().children.push({ type: 'label', bind: { text: 'a.b' } }, { type: 'label', bind: { text: 'aXb' } });
  const plan = M.replacePlan({ what: 'tags', find: 'a.b', replace: '$&$1\\n', scope: 'screen' });
  assert.equal(plan.length, 1);
  assert.equal(plan[0].after, '$&$1\\n');
  assert.equal(M.replacePlan({ what: 'tags', find: '.*', replace: 'x' }).length, 0);
  assert.equal(M.replacePlan({ what: 'tags', find: '(', replace: 'x' }).length, 0);
});

test('M replace: the tags the new names need are made as copies', () => {
  const { M, p } = lineProject();
  const base = M.problems().length;
  let r = M.replaceApply({ what: 'tags', find: 'line1_', replace: 'line4_', scope: 'screen', createTags: true });
  assert.equal(r.created, 2);
  assert.equal(r.newProblems.length, 0, r.newProblems.map(x => x.text).join(' | '));
  assert.equal(M.problems().length, base);
  assert.deepEqual(plain(p.tags.map(t => t.name)), ['line1_speed', 'line4_speed', 'line1_run', 'line4_run', 'line2_speed', 'total']);
  assert.deepEqual(plain(p.tags[1]), { name: 'line4_speed', type: 'int', min: 0, max: 100 }, 'a copy: type and limits');
  assert.equal(M.st.screens[1].data.children[0].bind.text, 'line1_speed', 'the other screen is left alone');
  // to names of which one exists: only the missing one is made
  r = M.replaceApply({ what: 'tags', find: 'line4_', replace: 'line2_', scope: 'screen', createTags: true });
  assert.equal(r.created, 1);
  assert.ok(p.tags.some(t => t.name === 'line2_run' && t.type === 'bool'));
  assert.equal(p.tags.filter(t => t.name === 'line2_speed').length, 1);
  // without the option nothing is made, and the missing tags are named
  r = M.replaceApply({ what: 'tags', find: 'line2_', replace: 'line8_', scope: 'screen' });
  assert.equal(r.created, 0);
  assert.ok(r.newProblems.some(x => /line8_run/.test(x.text)));
  // the runtime's own tags are never made
  M.replaceApply({ what: 'tags', find: 'line8_run', replace: 'sys.user.level', whole: true, scope: 'screen', createTags: true });
  assert.ok(!p.tags.some(t => /^sys\./.test(t.name)));
});

test('M replace: names in the tag list change only when asked, and only where no other tag has the name', () => {
  const { M, p } = lineProject();
  const base = M.problems().length;
  const plan = M.replacePlan({ what: 'tags', find: 'line1_', replace: 'press_', scope: 'project', definitions: true });
  let r = M.replaceApply({ what: 'tags', find: 'line1_', replace: 'press_', scope: 'project', definitions: true });
  assert.equal(r.changed, plan.length);
  assert.equal(r.skipped, 0);
  assert.equal(M.problems().length, base, 'everything follows: no new problem');
  assert.deepEqual(plain(p.tags.map(t => t.name)), ['press_speed', 'press_run', 'line2_speed', 'total']);
  // onto a name that is taken: the definition stays, and that is counted
  r = M.replaceApply({ what: 'tags', find: 'press_speed', replace: 'line2_speed', whole: true, scope: 'project', definitions: true });
  assert.equal(r.skipped, 1);
  assert.deepEqual(plain(p.tags.map(t => t.name)), ['press_speed', 'press_run', 'line2_speed', 'total']);
  // not asked for, or not the whole project: the list is not touched
  assert.ok(!M.replacePlan({ what: 'tags', find: 'press_', replace: 'x_', scope: 'project' }).some(c => c.loc.view === 'tags'));
  assert.ok(!M.replacePlan({ what: 'tags', find: 'press_', replace: 'x_', scope: 'screens', definitions: true }).some(c => c.loc.view === 'tags'));
});

test('M replace: Lua code - a new name that is another old name, and names Lua cannot write', () => {
  const { M, p } = lineProject();
  p.tags.push({ name: 'x1', type: 'int' }, { name: 'x11', type: 'int' });
  M.screen().children = [{ type: 'button', on_click: { action: 'script', code: 'tag.x1 = tag.x11 + tag.x1' } }];
  let r = M.replaceApply({ what: 'tags', find: 'x1', replace: 'x11', scope: 'screen' });
  assert.equal(M.screen().children[0].on_click.code, 'tag.x11 = tag.x111 + tag.x11', 'each name once');
  assert.equal(r.changed, 2);
  M.undo();
  r = M.replaceApply({ what: 'tags', find: 'x1', replace: 'a-b', whole: true, scope: 'screen' });
  assert.equal(r.skipped, 1);
  assert.equal(M.screen().children[0].on_click.code, 'tag.x1 = tag.x11 + tag.x1', 'left as it was');
  // code as a list of lines
  M.st.project.scripts = [{ name: 's', trigger: 'interval', interval_ms: 1000, code: ['tag.x1 = 1', '-- tag.x1 in a comment', 'tag.x11 = tag.x1'] }];
  r = M.replaceApply({ what: 'tags', find: 'x1', replace: 'y', whole: true, scope: 'project' });
  assert.deepEqual(plain(M.st.project.scripts[0].code), ['tag.y = 1', '-- tag.y in a comment', 'tag.x11 = tag.y']);
});

test('M replace: texts - labels, options, alarm messages and the text table; "@id" is a name and stays', () => {
  const { M, p } = lineProject();
  let plan = M.replacePlan({ what: 'texts', find: 'Line 1', replace: 'Press', scope: 'project' });
  assert.deepEqual(plain(plan.map(c => c.after).sort()), ['Press', 'Press', 'Press again', 'Press is too fast', 'Press speed'].sort());
  assert.equal(M.replacePlan({ what: 'texts', find: 'line 1', replace: 'Press', scope: 'project', matchCase: false }).length, 7);
  assert.equal(M.replacePlan({ what: 'texts', find: 'Line 1', replace: 'Press', scope: 'project', whole: true }).length, 2);
  assert.equal(M.replacePlan({ what: 'texts', find: 'Line 1', replace: 'Press', scope: 'screen' }).length, 3);
  let r = M.replaceApply({ what: 'texts', find: 'Line 1', replace: 'Press <b>1</b>', scope: 'project' });
  assert.equal(r.changed, 5);
  assert.equal(M.screen().children[0].text, 'Press <b>1</b> speed');
  assert.deepEqual(plain(M.screen().children[2].options), ['Press <b>1</b>', 'Line 2', 'line 1 (slow)']);
  assert.equal(p.alarms.items[0].message, 'Press <b>1</b> is too fast');
  assert.equal(M.screen().children[0].bind.text.tag, 'line1_speed', 'tags are not texts');
  // several languages: the texts are in the table
  p.languages = ['en', 'ja'];
  p.texts = { start: { en: 'Start line', ja: 'ライン開始' }, line: { en: 'line', ja: 'ライン' } };
  M.screen().children.push({ type: 'label', text: '@line' }, { type: 'label', text: 'a line here' }, { type: 'label', text: '@@line' });
  plan = M.replacePlan({ what: 'texts', find: 'line', replace: 'row', scope: 'project' });
  assert.ok(!plan.some(c => c.before === '@line'), '"@line" names a text');
  assert.ok(plan.some(c => c.before === 'Start line' && c.loc.view === 'texts'));
  assert.ok(plan.some(c => c.before === 'line' && c.what === 'line / en'));
  assert.ok(plan.some(c => c.before === 'a line here'));
  assert.ok(plan.some(c => c.before === '@@line'), '"@@..." is a text that begins with @');
  r = M.replaceApply({ what: 'texts', find: 'ライン', replace: 'プレス', scope: 'project' });
  assert.equal(p.texts.start.ja, 'プレス開始');
  assert.equal(p.texts.line.ja, 'プレス');
  assert.equal(M.replacePlan({ what: 'texts', find: 'Start', replace: 'x', scope: 'screens' }).some(c => c.loc.view === 'texts'), false, 'the table belongs to the project');
});

test('M duplicateScreen: a copy after the original, with a name and a file of its own', () => {
  const { M } = lineProject();
  const before = JSON.stringify(M.st.screens[0].data.children);
  assert.equal(M.duplicateScreen(0), 1);
  assert.deepEqual(plain(M.screenNames()), ['line1', 'line1_copy', 'other']);
  assert.equal(M.st.cur, 1);
  assert.equal(M.st.screens[1].file, 'screens/line1_copy.json');
  assert.equal(JSON.stringify(M.st.screens[1].data.children), before);
  M.screen().children[0].text = 'changed in the copy';
  assert.equal(JSON.stringify(M.st.screens[0].data.children), before, 'a copy, not the same object');
  assert.equal(M.duplicateScreen(0), 1);
  assert.equal(M.st.screens[1].data.name, 'line1_copy2');
  assert.equal(M.duplicateScreen(0, 'other'), 1);
  assert.equal(M.st.screens[1].data.name, 'other2', 'a name that is taken gets a number');
  assert.equal(M.duplicateScreen(0, '../../x <y>'), 1);
  assert.ok(/^[A-Za-z0-9_-]+$/.test(M.st.screens[1].data.name), M.st.screens[1].data.name);
  assert.equal(M.st.screens[1].file, 'screens/' + M.st.screens[1].data.name + '.json');
  assert.equal(M.duplicateScreen(99), -1);
  assert.equal(new Set(M.screenNames()).size, M.st.screens.length);
  assert.equal(new Set(M.st.screens.map(s => s.file)).size, M.st.screens.length);
  M.undo();
  assert.equal(M.st.screens.length, 5);
  // the usual use: a copy for the next line, its tags replaced and made
  const { M: M2, p } = lineProject();
  M2.duplicateScreen(0, 'line5');
  const r = M2.replaceApply({ what: 'tags', find: 'line1_', replace: 'line5_', scope: 'screen', createTags: true });
  assert.equal(r.newProblems.length, 0);
  assert.equal(M2.st.screens[0].data.children[0].bind.text.tag, 'line1_speed');
  assert.equal(M2.st.screens[1].data.children[0].bind.text.tag, 'line5_speed');
  assert.ok(p.tags.some(t => t.name === 'line5_run'));
});

test('M resizeProject: positions, sizes, fonts and spacings for another display; percentages stay', () => {
  const { M, p, SCHEMA } = lineProject();
  assert.equal(M.resizeProject(800, 480), false, 'the same size: nothing to do');
  assert.equal(M.resizeProject(10, 10), false);
  assert.equal(M.resizeProject('x', 320), false);
  assert.equal(M.resizeProject(400, 240), true);
  assert.equal(p.display.width, 400);
  assert.equal(p.display.height, 240);
  const w = M.st.screens[0].data.children;
  assert.deepEqual([w[0].x, w[0].y], [5, 10]);
  assert.deepEqual([w[1].x, w[1].y, w[1].w, w[1].h], [5, 30, 60, 24]);
  assert.deepEqual([w[3].x, w[3].y, w[3].w, w[3].h], [100, 10, 150, 100]);
  assert.deepEqual(plain(w[3].style), { pad: 4, radius: 3, font: 12 });
  assert.equal(w[3].children[0].w, '50%');
  assert.equal(w[3].children[0].h, 'content');
  assert.ok(SCHEMA.fonts.includes(w[3].children[0].style.font), 'a font size there is');
  assert.deepEqual(plain(w[3].children[1].points), [[0, 0], [50, 25]]);
  assert.ok(!('w' in w[0]) && !('h' in w[0]), 'no size where there was none');
  M.undo();
  assert.equal(M.st.project.display.width, 800);
  assert.equal(M.st.screens[0].data.children[1].w, 120);
  // 800x480 -> 480x320: not the same ratio; a pad of 1 does not become 0
  M.screen().children.push({ type: 'panel', x: 100, y: 100, w: 100, h: 100, style: { pad: 1, pad_left: 10, pad_top: 10, font: 48, border_width: 2 } });
  M.resizeProject(480, 320);
  const q = M.st.screens[0].data.children.at(-1);
  assert.deepEqual([q.x, q.y, q.w, q.h], [60, 67, 60, 67]);
  assert.deepEqual(plain(q.style), { pad: 1, pad_left: 6, pad_top: 7, font: 28, border_width: 2 });
});

test('M replace: 100 screens with 5000 widgets - the plan and the replacement take a moment, not minutes', () => {
  const { M } = lineProject();
  for (let s = 0; s < 100; s++) {
    if (s) M.addScreen('s' + s);
    M.st.cur = M.st.screens.length - 1;
    M.screen().children = Array.from({ length: 50 }, (_, i) => ({ type: 'label', x: i, y: i, text: 'Line 1 value ' + i, bind: { text: { tag: i % 2 ? 'line1_speed' : 'total' }, visible: 'line1_run' } }));
  }
  let t0 = Date.now();
  const plan = M.replacePlan({ what: 'tags', find: 'line1_', replace: 'line9_', scope: 'project' });
  const tPlan = Date.now() - t0;
  assert.ok(plan.length >= 7500, String(plan.length));
  t0 = Date.now();
  const r = M.replaceApply({ what: 'tags', find: 'line1_', replace: 'line9_', scope: 'project', createTags: true });
  const tApply = Date.now() - t0;
  assert.equal(r.changed, plan.length);
  assert.equal(r.newProblems.length, 0);
  t0 = Date.now();
  const rt = M.replaceApply({ what: 'texts', find: 'Line 1', replace: 'Press', scope: 'project' });
  const tText = Date.now() - t0;
  assert.ok(rt.changed >= 5000);
  t0 = Date.now();
  M.resizeProject(480, 320);
  const tSize = Date.now() - t0;
  console.log('    5000 widgets on 101 screens: plan ' + tPlan + ' ms, replace tags ' + tApply + ' ms, replace texts ' + tText + ' ms, another size ' + tSize + ' ms');
  assert.ok(tPlan < 2000 && tApply < 10000 && tText < 10000 && tSize < 5000);
});

test('M transfers: the tags follow a rename; a ring, a wrong trigger and a destination of the runtime are problems', () => {
  const { M, p } = tagProject();
  const base = M.problems().length;
  p.transfers = [
    { name: 'a', from: 'temp', to: 'run' },
    { name: 'b', from: 'run', to: 'count', trigger: 'cyclic', every_ms: 500, scale: 2, offset: -1, out_of_range: 'clamp', enabled: false, when: { tag: 'temp', gt: 10 } },
    { name: 'c', from: 'temp', to: 'count', when: 'run' },
  ];
  if (!p.tags.some(t => t.name === 'count')) p.tags.push({ name: 'count', type: 'int' });
  assert.equal(M.problems().length, base, M.problems().map(x => x.text).join(' | '));
  M.renameTag(p.tags.findIndex(t => t.name === 'temp'), 'temp_pv');
  assert.equal(p.transfers[0].from, 'temp_pv');
  assert.equal(p.transfers[1].when.tag, 'temp_pv');
  assert.equal(p.transfers[2].from, 'temp_pv');
  assert.equal(M.problems().length, base);
  M.renameTag(p.tags.findIndex(t => t.name === 'run'), 'running');
  assert.equal(p.transfers[0].to, 'running');
  assert.equal(p.transfers[2].when, 'running');
  assert.equal(M.problems().length, base);
  const has = (list, re) => { p.transfers = list; const t = M.problems().map(x => x.text); assert.ok(t.some(x => re.test(x)), String(re) + ' in: ' + t.join(' | ')); };
  has([{ name: 'a', from: 'temp_pv', to: 'running' }, { name: 'b', from: 'running', to: 'count' }, { name: 'c', from: 'count', to: 'temp_pv' }], /transfer "b": part of a ring/);
  has([{ name: 'a', from: 'temp_pv', to: 'temp_pv' }], /the same tag/);
  has([{ name: 'a', from: 'temp_pv', to: 'sys.user.level' }], /tag of the runtime/);
  has([{ name: 'a', from: 'temp_pv', to: 'gone' }], /unknown tag "gone" \(transfer\)/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', when: { tag: 'nope', eq: 1 } }], /unknown tag "nope"/);
  has([{ name: 'a', from: 'temp_pv', to: 'count' }, { name: 'a', from: 'temp_pv', to: 'running' }], /the name is used twice/);
  has([{ from: 'temp_pv', to: 'count' }], /a transfer has no name/);
  has([{ name: 'a', to: 'count' }], /no tag to take the value from/);
  has([{ name: 'a', from: 'temp_pv' }], /no tag to write the value to/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', trigger: 'often' }], /"trigger" is/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', trigger: 'cyclic', every_ms: 5 }], /"every_ms" is 100/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', every_ms: 500 }], /no meaning without/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', scale: '2' }], /"scale" is a number/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', out_of_range: 'wrap' }], /"out_of_range" is/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', when: { tag: 'running', eq: 1, gt: 0 } }], /"when" is a tag/);
  has([{ name: 'a', from: 'temp_pv', to: 'count', when: { tag: 'running', like: 1 } }], /"when" is a tag/);
  has([5], /transfer #1 is not a transfer/);
  has({ name: 'a' }, /transfers: a list is needed/);
  delete p.transfers;
});

test('M sound: a wrong output, volume, line or sound is a problem; the sys.sound tags are offered', () => {
  const { M, p } = tagProject();
  const base = M.problems().length;
  assert.ok(!M.tagChoices().some(t => t.name === 'sys.sound.mute'));
  for (const ok of [{}, { output: 'gpio', gpio: 18, gpio_active_low: true }, { output: 'none', volume: 0 }, { touch: false, alarm: 'beep', alarm_every_s: 0 },
                    { touch: 'assets/tap.wav', alarm: 'assets\\sub\\a.WAV', volume: 100, device: 'plughw:1,0' }, { touch: '', alarm: true }]) {
    p.sound = ok;
    assert.equal(M.problems().length, base, JSON.stringify(ok) + ': ' + M.problems().map(x => x.text).join(' | '));
  }
  assert.ok(M.tagChoices().some(t => t.name === 'sys.sound.mute'));
  const bad = [[5, /an object/], [[], /an object/], [{ output: 'speaker' }, /output "speaker"/], [{ output: 'gpio' }, /needs the GPIO line/], [{ gpio: 1.5 }, /gpio is the number/], [{ gpio: -1 }, /gpio is the number/],
               [{ volume: 101 }, /volume is 0 to 100/], [{ volume: '80' }, /volume is 0 to 100/], [{ alarm_every_s: -1 }, /alarm_every_s/], [{ touch: 'ding' }, /touch "ding" is no built-in/],
               [{ alarm: '../a.wav' }, /alarm "..\/a.wav"/], [{ touch: '/etc/a.wav' }, /touch/], [{ touch: 'C:/a.wav' }, /touch/], [{ alarm: 'a.mp3' }, /alarm/], [{ touch: 5 }, /touch/]];
  for (const [snd, re] of bad) {
    p.sound = snd;
    assert.ok(M.problems().some(x => re.test(x.text)), JSON.stringify(snd) + ' -> ' + M.problems().map(x => x.text).join(' | '));
  }
  delete p.sound;
});

test('M schedules: the tag of an action follows a rename; what is no schedule is a problem', () => {
  const { M, p } = tagProject();
  const base = M.problems().length;
  p.schedules = [
    { name: 'start', at: '08:00', days: ['mon', 'fri'], do: { action: 'set', tag: 'temp', value: 1 } },
    { name: 'night', at: '2026-12-01 22:30', do: [{ action: 'toggle', tag: 'run' }, { action: 'inc', tag: 'temp', by: 2 }] },
    { name: 'daily', at: '23:59', days: 'weekdays', do: { action: 'back' } },
  ];
  assert.equal(M.problems().length, base);
  M.renameTag(0, 'temp_pv');
  assert.equal(p.schedules[0].do.tag, 'temp_pv');
  assert.equal(p.schedules[1].do[1].tag, 'temp_pv');
  assert.equal(p.schedules[1].do[0].tag, 'run');
  assert.equal(M.problems().length, base);
  p.schedules = [
    { at: '08:00', do: { action: 'back' } },
    { name: 'a', at: '8:00', do: { action: 'back' } },
    { name: 'a', at: '24:00', do: { action: 'back' } },
    { name: 'b', at: '2026-13-01 08:00', do: { action: 'back' } },
    { name: 'c', at: '08:00', days: ['mon', 'someday'], do: { action: 'back' } },
    { name: 'd', at: '08:00', days: [], do: { action: 'back' } },
    { name: 'e', at: '08:00' },
    { name: 'f', at: '08:00', do: { action: 'launch' } },
    { name: 'g', at: '08:00', do: 'reboot' },
    { name: 'h', at: '08:00', do: { action: 'set', tag: 'gone', value: 1 } },
    { name: 'i', at: '08:00', do: [{ action: 'goto', screen: 'nowhere' }, { action: 'script', name: 'nothing' }] },
    5,
  ];
  const t = M.problems().map(x => x.text);
  const has = re => assert.ok(t.some(x => re.test(x)), String(re) + ' in: ' + t.join(' | '));
  has(/a schedule has no name/);
  has(/schedule "a": "at" is HH:MM/);
  has(/schedule "a" is defined twice/);
  has(/schedule "b": "at" is/);
  has(/schedule "c": "someday" is not a day/);
  has(/schedule "d": no day is named/);
  has(/schedule "e": "do" is an action/);
  has(/schedule "f": "do" is an action/);
  has(/schedule "g": "do" is an action/);
  has(/unknown tag "gone" \(schedule\)/);
  has(/schedule "i": unknown screen "nowhere"/);
  has(/schedule "i": unknown script "nothing"/);
  has(/schedule #12 is not a schedule/);
});

test('M table cells: a cell as a line of text and back', () => {
  const { M } = load();
  const pairs = [
    ['Loader', 'Loader'], ['', ''], [5, '5'], [2.5, '2.5'], [-1, '-1'],
    [{ tag: 'count' }, '{count}'],
    [{ tag: 'temp', format: '%.1f C' }, '{temp|%.1f C}'],
    [{ tag: 'state', map: { 0: 'Stop', 1: 'Run', default: '?' } }, '{state|0=Stop,1=Run,*=?}'],
    [{ tag: 'state', format: '%d', map: { 1: 'on' } }, '{state|%d|1=on}'],
    [{ tag: 'pct', format: '%d %%' }, '{pct|%d %%}'],
    ['{literal}', '\\{literal}'], ['\\{x', '\\\\{x'],
    ['a {b} c', 'a {b} c'], ['50%', '50%'], ['a=b', 'a=b'], ['@title', '@title'],
  ];
  for (const [cell, text] of pairs) {
    assert.equal(M.cellToText(cell), text, 'to text: ' + JSON.stringify(cell));
    deq(M.textToCell(text), cell, 'back: ' + text);
    assert.equal(M.cellEditable(cell), true, JSON.stringify(cell));
  }
  assert.equal(M.cellToText(null), '');
  assert.equal(M.cellToText(undefined), '');
  assert.equal(M.cellToText(true), 'true');
  deq(M.textToCell(' {count} '), { tag: 'count' });                  // spaces around a tag do not matter
  deq(M.textToCell('{ count |%d}'), { tag: 'count', format: '%d' });
  deq(M.textToCell('{count'), '{count');                             // not closed: a text
  deq(M.textToCell('{a}{b}'), '{a}{b}');
  deq(M.textToCell('007'), '007');                                   // not a number as the runtime would print it
  deq(M.textToCell('1e3'), '1e3');
  deq(M.textToCell(' 5 '), 5);                                       // (spaces around a number do not matter)
  // what the grid cannot write as a text is left alone
  assert.equal(M.cellEditable('5'), true);                           // ("5" and 5 show the same)
  assert.equal(M.cellEditable(null), true);
  assert.equal(M.cellEditable({ tag: 't', map: { 0: 'a,b' } }), false);
  assert.equal(M.cellEditable({ tag: 't', map: { 0: 'a|b' } }), false);
  assert.equal(M.cellEditable({ tag: 't', format: 'x=%d' }), false);
  assert.equal(M.cellEditable({ tag: 't', colour: 'red' }), false);
  assert.equal(M.cellEditable({ text: 'x' }), false);
  assert.equal(M.cellEditable(['a']), false);
  assert.equal(M.cellEditable({ tag: 'a|b' }), false);
  assert.equal(M.cellEditable({ tag: 't', map: { '': 'x' } }), false);
});

test('SCHEMA.logColumns: the columns a log view shows', () => {
  const { SCHEMA } = load();
  deq(SCHEMA.logColumns({}), ['time', 'level', 'source', 'message']);
  deq(SCHEMA.logColumns({ source: 'operation' }), ['time', 'user', 'kind', 'target', 'old', 'new']);
  deq(SCHEMA.logColumns({ source: 'system', columns: ['time', 'message'] }), ['time', 'message']);
  deq(SCHEMA.logColumns({ columns: 'time, count' }), ['time', 'count']);
  deq(SCHEMA.logColumns({ source: 'data', group: 'none' }), ['time']);
});

test('M renameTag: the cells of a table and the filter tag of a log view follow; unknown ones are problems', () => {
  const { M } = tagProject();
  const s = M.screen().children;
  s.push({ type: 'table', rows: [['Temp', { tag: 'temp', format: '%.1f' }], [{ tag: 'run', map: { 1: 'on' } }, 5, null, 'x']] });
  s.push({ type: 'log_view', source: 'operation', filter_tag: 'temp' });
  const before = M.problems().length;
  M.renameTag(0, 'temp_pv');
  const t = M.screen().children[M.screen().children.length - 2], lv = M.screen().children[M.screen().children.length - 1];
  assert.equal(t.rows[0][1].tag, 'temp_pv');
  assert.equal(t.rows[1][0].tag, 'run');
  assert.equal(lv.filter_tag, 'temp_pv');
  assert.equal(M.problems().length, before);
  t.rows[0][1].tag = 'gone'; lv.filter_tag = 'lost';
  const texts = M.problems().map(x => x.text);
  assert.ok(texts.some(x => /unknown tag "gone" \(table\)/.test(x)), texts.join('; '));
  assert.ok(texts.some(x => /unknown tag "lost" \(filter\)/.test(x)), texts.join('; '));
});

test('M moveWidgets: reorder, into a panel and out again; never into itself', () => {
  const { M } = tagProject();
  const types = () => M.screen().children.map(w => w.type);
  assert.equal(M.moveWidgets([[0]], [], 3), true);                       // before the panel
  deq(types(), ['button', 'chart', 'label', 'panel']);
  deq(M.selection(), [[2]]);
  assert.equal(M.moveWidgets([[0], [1]], [3]), true);                    // two widgets into the panel, at the end
  deq(types(), ['label', 'panel']);
  deq(M.get([1]).children.map(w => w.type), ['label', 'label', 'button', 'chart']);
  deq(M.selection(), [[1, 2], [1, 3]]);
  assert.equal(M.moveWidgets([[1, 3]], [], 0), true);                    // out again, to the front of the list
  deq(types(), ['chart', 'label', 'panel']);
  assert.equal(M.moveWidgets([[2]], [2]), false);                        // a panel into itself
  assert.equal(M.moveWidgets([[0]], [1]), false);                        // into something that is not a panel
  M.st.sel = [0]; M.st.more = [];
  M.orderSelection('front');
  deq(types(), ['label', 'panel', 'chart']);
  M.orderSelection('back');
  deq(types(), ['chart', 'label', 'panel']);
});

test('M importTags / exportTags, pasteStyle, designer marks', () => {
  const { M } = tagProject();
  assert.equal(M.exportTags(), 'name,type,init,min,max\ntemp,float,,,\nrun,bool,,,\nspare,int,,,\n');
  let r = M.importTags('name\ttype\tinit\tmin\tmax\nlevel\tfloat\t1.5\t0\t10\ntemp,int\n"text",string,"a ""b"""\nflag\n');
  deq([r.added, r.updated, r.errors], [3, 1, []]);
  const tags = M.st.project.tags;
  deq(tags.find(t => t.name === 'level'), { name: 'level', type: 'float', init: 1.5, min: 0, max: 10 });
  deq([tags[0], tags.find(t => t.name === 'text').init, tags.find(t => t.name === 'flag').type], [{ name: 'temp', type: 'int' }, 'a "b"', 'int']);
  const n = tags.length, undo = M.st.undo.length;
  r = M.importTags('ok,int\n1bad,int\nx,word\ny,int,abc');
  assert.equal(r.errors.length, 3);
  deq([M.st.project.tags.length, M.st.undo.length], [n, undo]);            // nothing changed
  M.undo();
  assert.equal(M.st.project.tags.length, 3);

  const s = M.screen().children;
  s[0].style = { bg_color: '#111' }; s[0].style_pressed = { bg_color: '#222' }; s[1].style = { radius: 9 }; s[1].style_checked = { opa: 1 };
  const clip = M.styleOf(s[0]);
  M.st.sel = [1]; M.st.more = [[2]];
  M.pasteStyle(clip);
  for (const i of [1, 2]) deq(M.styleOf(M.screen().children[i]), { style: { bg_color: '#111' }, style_pressed: { bg_color: '#222' } });
  assert.equal(M.screen().children[1].bind, 'run');                        // nothing but the style

  M.setFlag('locked', true);
  deq([M.flag(M.get([1]), 'locked'), M.flag(M.get([2]), 'locked'), M.flag(M.get([0]), 'locked'), M.anyFlag('locked')], [true, true, false, true]);
  M.setFlag('hidden', true); M.setFlag('locked', false);
  deq(M.get([1]).designer, { hidden: true });
  M.clearFlag();
  deq([M.get([1]).designer, M.anyFlag('hidden')], [undefined, false]);
});

test('M recipes: books, values as the tag type, problems, renames, CSV both ways', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.st.project.tags = [{ name: 'sp', type: 'int', min: 0, max: 100 }, { name: 'on', type: 'bool' }, { name: 'txt', type: 'string' }];
  const name = M.addRecipeBook();
  assert.equal(name, 'book1');
  const b = M.recipeBooks()[0];
  deq(b.items, ['sp', 'on', 'txt']);
  M.addRecipe(0); M.addRecipe(0);
  deq(b.recipes.map(r => r.name), ['Recipe 1', 'Recipe 2']);
  M.setRecipeValue(0, 0, 'sp', '42'); M.setRecipeValue(0, 0, 'on', 'true'); M.setRecipeValue(0, 0, 'txt', '=x');
  deq(b.recipes[0].values, { sp: 42, on: true, txt: '=x' });
  M.setRecipeValue(0, 0, 'txt', '');
  deq(b.recipes[0].values, { sp: 42, on: true });
  assert.equal(M.problems().length, 0);

  M.setRecipeValue(0, 1, 'sp', '500');
  M.setRecipeValue(0, 1, 'on', 'x');
  M.st.project.recipes.level = 2;
  M.screen().children = [{ type: 'recipe_table', book: 'nope' }];
  const texts = M.problems().map(p => p.text);
  assert.ok(texts.includes('recipe "Recipe 2": sp = 500 is outside 0..100'), texts.join(' | '));
  assert.ok(texts.includes('recipes: a level is set, but user management is off (Users tab)'));
  assert.ok(texts.includes('recipe table: no recipe book "nope"'));
  M.st.project.recipes.level = 0;
  M.setRecipeValue(0, 1, 'sp', '50');

  // renaming the book: the tables follow; renaming a tag: items and values follow
  M.screen().children = [{ type: 'recipe_table', book: 'book1' }];
  assert.equal(M.renameRecipeBook(0, 'a/b'), false);
  assert.equal(M.renameRecipeBook(0, 'process'), true);
  assert.equal(M.screen().children[0].book, 'process');
  M.renameTag(0, 'speed');
  deq(b.items, ['speed', 'on', 'txt']);
  deq(b.recipes[0].values, { speed: 42, on: true });

  // CSV: formulas are escaped, numbers are not; the same text comes back
  M.setRecipeValue(0, 0, 'txt', '=HYPERLINK("x")');
  const csv = M.recipesCsv(0);
  assert.equal(csv.split('\n')[0], 'name,speed,on,txt');
  assert.ok(csv.includes(`Recipe 1,42,true,"'=HYPERLINK(""x"")"`), csv);
  let r = M.importRecipesCsv(0, csv);
  deq([r.count, r.errors], [2, []]);
  assert.equal(b.recipes[0].values.txt, '=HYPERLINK("x")');
  r = M.importRecipesCsv(0, 'name,secret\nA,1\n');
  assert.ok(r.errors.length && r.count === 0);
  r = M.importRecipesCsv(0, 'name\tspeed\nA\t1\nA\t2\n');
  assert.ok(r.errors.some(e => /twice/.test(e)));
  r = M.importRecipesCsv(0, 'name,speed\nA,fast\n');
  assert.ok(r.errors.some(e => /not a number/.test(e)));
  assert.equal(b.recipes.length, 2);                                       // nothing replaced by the bad ones
});

test('U.slmpAddress agrees with the runtime parser; M.problems names wrong SLMP addresses', () => {
  const { U, M } = load();
  const ok = s => !!U.slmpAddress(s);
  for (const s of ['D100', 'x1f', 'W 1A', 'SM400', 'SD0', 'SB0', 'DX10', 'ZR0FFFF', 'S5', 'TN3', 'D10.F', 'M0010', 'D16777215']) assert.ok(ok(s), s);
  for (const s of ['', 'Q10', 'D1A', 'D16777216', 'M10.1', 'D10.G', 'D10.10', 'D', 'D-1', 'D'.repeat(100), null, undefined]) assert.ok(!ok(s), String(s));
  deq(U.slmpAddress('d10.3'), { dev: 'D', number: 10, bit: 3 });
  deq(U.slmpAddress('X1F'), { dev: 'X', number: 31, bit: -1 });
  M.newProject('T', 800, 480);
  M.st.project.tags = [{ name: 'a', type: 'int' }];
  M.st.project.drivers = [{ type: 'slmp', name: 'plc', map: [{ tag: 'a', addr: 'D100' }, { tag: 'a', addr: 'D1A' }, { tag: 'b', addr: 'M0' }] }];
  const texts = M.problems().map(p => p.text);
  deq(texts.sort(), ['plc: "D1A" is not a device address', 'plc: no host is set', 'unknown tag "b" (map)'].sort());
});

test('U.finsAddress agrees with the runtime parser; M.problems names wrong FINS addresses', () => {
  const { U, M } = load();
  const ok = s => !!U.finsAddress(s);
  for (const s of ['D100', 'dm 100', 'CIO10', '10', '10.5', 'W5', 'H7', 'A440', 'W5.15', 'E0_100', 'EC_0', 'ED_0', 'E18_0', 'E1_5.03', 'EM2_7', 'T3', 'C4', 'D65535']) assert.ok(ok(s), s);
  for (const s of ['', 'Q10', 'D1A', 'D65536', 'T32768', 'D10.16', 'D10.', 'T1.00', 'E19_0', 'E_0', 'D', 'D-1', '1'.repeat(40), null, undefined]) assert.ok(!ok(s), String(s));
  deq(U.finsAddress('d10.03'), { area: 'D', number: 10, bit: 3 });
  deq(U.finsAddress('10.15'), { area: 'CIO', number: 10, bit: 15 });
  deq(U.finsAddress('e1_050'), { area: 'E1', number: 50, bit: -1 });
  M.newProject('T', 800, 480);
  M.st.project.tags = [{ name: 'a', type: 'int' }];
  M.st.project.drivers = [{ type: 'fins', name: 'plc', map: [{ tag: 'a', addr: 'D100' }, { tag: 'a', addr: 'Q5' }, { tag: 'b', addr: 'W0.00' }] }];
  const texts = M.problems().map(p => p.text);
  deq(texts.sort(), ['plc: "Q5" is not a device address', 'plc: no host is set', 'unknown tag "b" (map)'].sort());
});

test('M trend widgets: group and tags are checked, tag renames follow', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.st.project.tags = [{ name: 'temp', type: 'float' }, { name: 'p', type: 'float' }];
  M.st.project.data_log = { groups: [{ name: 'g', trigger: 'interval', tags: ['temp', 'p'] }] };
  M.screen().children = [{ type: 'trend', group: 'g', tags: ['temp', { tag: 'p', color: '#f00' }] }, { type: 'trend', group: 'nope' }, { type: 'trend', group: 'g', tags: ['ghost'] }];
  const texts = M.problems().map(p => p.text).sort();
  deq(texts, ['trend: data log group "g" does not record "ghost"', 'trend: no data log group "nope"', 'unknown tag "ghost" (trend)'].sort());
  M.renameTag(0, 'temperature');
  deq(M.screen().children[0].tags, ['temperature', { tag: 'p', color: '#f00' }]);
  M.renameTag(1, 'pressure');
  assert.equal(M.screen().children[0].tags[1].tag, 'pressure');
  deq(M.st.project.data_log.groups[0].tags, ['temperature', 'pressure']);
});

test('M popups: actions must name a popup screen, the start screen must not be one', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.addScreen('detail');
  M.st.screens[1].data.popup = true;
  M.st.cur = 0;
  M.screen().children = [{ type: 'button', on_click: [{ action: 'popup', screen: 'detail' }, { action: 'close' }, { action: 'popup', screen: 'main' },
    { action: 'popup', screen: 'gone' }, { action: 'popup' }, { action: 'close', screen: 'detail' }] }];
  const texts = M.problems().map(p => p.text).sort();
  deq(texts, ['popup without a screen', 'screen "main" is not a popup (popup)', 'unknown screen "gone" (popup)'].sort());
  M.st.project.start_screen = 'detail';
  assert.ok(M.problems().some(p => p.text === 'the start screen "detail" is a popup'));
});

test('M texts: @id in the preview language, checks, renames, CSV both ways', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  const p = M.st.project;
  p.languages = ['ja', 'en'];
  p.texts = { title: { ja: 'ポンプ場', en: 'Pump station' }, only_ja: { ja: '日本語' }, long: { ja: '停止', en: 'Emergency stop pressed' } };
  p.font = { file: 'assets/fonts/NotoSansJP-subset.otf' };
  assert.equal(M.resolveText('@title'), 'ポンプ場');
  M.st.previewLang = 'en';
  assert.equal(M.resolveText('@title'), 'Pump station');
  assert.equal(M.resolveText('@only_ja'), '日本語');                     // falls back to the first language
  assert.equal(M.resolveText('@nope'), '@nope');
  assert.equal(M.resolveText('@@x'), '@x');
  assert.equal(M.resolveText('plain'), 'plain');
  M.screen().children = [{ type: 'label', text: '@title' }, { type: 'button', text: '@missing' }, { type: 'dropdown', options: ['@title', 'x'] }];
  const texts = M.problems().map(x => x.text).sort();
  deq(texts, ['no text "missing" (shown as "@missing")', 'text "long" in en is much longer than in ja: it may not fit', 'text "only_ja" has no en'].sort());

  assert.equal(M.renameText('title', 'heading'), true);
  assert.equal(M.screen().children[0].text, '@heading');
  assert.equal(M.screen().children[2].options[0], '@heading');
  deq(Object.keys(p.texts), ['heading', 'only_ja', 'long']);           // the order is kept
  assert.equal(M.renameText('heading', 'only_ja'), false);
  assert.equal(M.addText('9bad'), false);
  assert.equal(M.addText('new.one'), true);

  const csv = M.textsCsv();
  assert.equal(csv.split('\n')[0], 'id,ja,en');
  let r = M.importTextsCsv(csv.replace('id,ja,en', 'id,ja,en,de') + 'x\t=1\tone\teins\n');
  deq([r.count, r.errors], [5, []]);
  deq(p.languages, ['ja', 'en', 'de']);
  assert.equal(p.texts.x.ja, '=1');
  r = M.importTextsCsv('nope,ja\n');
  assert.ok(r.errors.length);
  r = M.importTextsCsv('id,ja\na,1\na,2\n');
  assert.ok(r.errors.some(e => /twice/.test(e)));
  assert.equal(Object.keys(p.texts).length, 5);                        // nothing replaced by the bad ones
});

test('M mqtt driver: missing host, no TLS, password in the project, wildcards; tag renames follow', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.st.project.tags = [{ name: 'temp', type: 'float' }, { name: 'sp', type: 'int' }];
  M.st.project.drivers = [{ type: 'mqtt', name: 'mq', tls: false, password: 'x',
    publish: ['temp', { tag: 'sp', topic: 'a/#' }], subscribe: [{ tag: 'sp', topic: 'cmd/sp' }, 'ghost'] }];
  deq(M.problems().map(p => p.text).sort(), [
    'mq: "a/#": wildcards (+ #) are not allowed', 'mq: no TLS: user name, password and values can be read on the network',
    'mq: no host is set', 'mq: the password is in project.json; better enter it on the device (System > Driver)',
    'unknown tag "ghost" (subscribe)'].sort());
  M.st.project.drivers = [{ type: 'mqtt', host: 'b', verify: false, publish: ['temp'], subscribe: [{ tag: 'sp' }] }];
  deq(M.problems().map(p => p.text), ["mqtt: the broker's certificate is not checked (verify off)"]);
  M.renameTag(0, 'temperature');
  M.renameTag(1, 'speed');
  deq(M.st.project.drivers[0].publish, ['temperature']);
  deq(M.st.project.drivers[0].subscribe, [{ tag: 'speed' }]);
});

test('M opcua_client driver: endpoint, no security, verify off, nothing to trust, password; tag renames follow', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.st.project.tags = [{ name: 'temp', type: 'float' }];
  M.st.project.drivers = [{ type: 'opcua_client', name: 'plc', endpoint: 'http://x', security: 'none', password: 'p', cert_file: 'c.der', map: [{ tag: 'temp', node: 'ns=2;s=T' }] }];
  deq(M.problems().map(p => p.text).sort(), [
    'plc: cert_file and key_file go together', 'plc: no security: values can be read and changed on the network',
    'plc: the endpoint is not opc.tcp://host:port', 'plc: the password is in project.json; better enter it on the device (System > Driver)'].sort());
  M.st.project.drivers = [{ type: 'opcua_client', endpoint: 'opc.tcp://a:4840', map: [{ tag: 'temp', node: 'ns=2;s=T' }] }];
  deq(M.problems().map(p => p.text), ['opcua_client: no server certificate to trust (server_cert); put one in pki/trusted/ on the device, or it does not connect']);
  M.st.project.drivers[0].verify = false;
  deq(M.problems().map(p => p.text), ["opcua_client: the server's certificate is not checked (verify off)"]);
  M.st.project.drivers[0].verify = undefined; M.st.project.drivers[0].server_cert = 'certs/s.der';
  deq(M.problems(), []);
  M.renameTag(0, 'temperature');
  assert.equal(M.st.project.drivers[0].map[0].tag, 'temperature');
});

// ---------------------------------------------------------------- the AI skill (tools/skill)

test('skill: the widget reference is what schema.js says, and the example uses only known keys', () => {
  const { execFileSync } = require('node:child_process');
  const skill = path.join(__dirname, '..', '..', 'skill');
  execFileSync(process.execPath, [path.join(skill, 'gen_reference.js'), '--check']);       // throws when out of date
  const { SCHEMA } = load();
  const known = new Set(['type', 'name', 'children', 'bind', 'text', 'series', 'popup', 'dim', 'animate']
    .concat(SCHEMA.common.map(f => f.k), SCHEMA.flags.map(f => f.k), SCHEMA.flex.map(f => f.k), SCHEMA.access.map(f => f.k), SCHEMA.styleSelectors, SCHEMA.events));
  const dir = path.join(skill, 'hml-screens', 'examples');
  const project = JSON.parse(fs.readFileSync(path.join(dir, 'project.json'), 'utf8'));
  const tags = new Set(project.tags.map(t => t.name));
  const screens = project.screens.map(f => JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')));
  const names = new Set(screens.map(s => s.name));
  const visit = (w, top) => {
    if (!top) assert.ok(SCHEMA.types[w.type], 'widget type ' + w.type);
    const own = top ? [] : SCHEMA.types[w.type].props.map(p => p.k);
    for (const k of Object.keys(w)) assert.ok(known.has(k) || own.includes(k), (w.type || 'screen') + ': key "' + k + '"');
    for (const sel of SCHEMA.styleSelectors) for (const k of Object.keys(w[sel] || {})) assert.ok(k in SCHEMA.styleKeys, 'style key "' + k + '"');
    const binds = typeof w.bind === 'string' ? { _: w.bind } : (w.bind || {});
    for (const [kind, b] of Object.entries(binds)) {
      if (kind !== '_') assert.ok(SCHEMA.bindKinds.includes(kind), 'bind kind "' + kind + '"');
      assert.ok(tags.has(typeof b === 'string' ? b : b.tag), 'tag of bind ' + JSON.stringify(b));
    }
    for (const ev of SCHEMA.events) for (const a of [].concat(w[ev] || [])) {
      assert.ok(SCHEMA.actions[a.action], 'action ' + a.action);
      if (a.tag) assert.ok(tags.has(a.tag), 'tag ' + a.tag);
      if (a.screen) assert.ok(names.has(a.screen), 'screen ' + a.screen);
    }
    (w.children || []).forEach(c => visit(c, false));
  };
  screens.forEach(s => visit(s, true));
});

test('M animate: what is wrong in an animation is a problem', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.st.project.tags = [{ name: 'alarm', type: 'bool' }];
  M.screen().children = [
    { type: 'label', text: 'ok', animate: [{ preset: 'blink', when: 'alarm' }, 'spin', { prop: 'x', from: 0, to: 9, when: { tag: 'alarm', eq: 1 } }] },
    { type: 'label', text: 'bad', animate: [{ prop: 'colour', to: 1 }, { preset: 'wobble' }, { prop: 'y' }, { prop: 'opa', to: 0, time: 50, speed: 2 },
                                           { prop: 'frames', files: ['a.png'] }, { preset: 'pulse', when: 'ghost', path: 'bouncy' }, 5] },
    { type: 'image', animate: { prop: 'frames', files: ['a.png', 'b.png'], time: 400 } },
  ];
  deq(M.problems().map(p => p.text).sort(), [
    'animate: unknown prop "colour"', 'animate: unknown preset "wobble"', 'animate: "y" needs "to"', 'animate: unknown key "speed"',
    'animate: 50 ms flashes faster than 3 times a second (the runtime slows it down)', 'animate: "frames" is for image widgets',
    'animate: "frames" needs "files" with two pictures or more', 'animate: unknown tag "ghost" (when)', 'animate: unknown path "bouncy"',
    'animate: an animation is an object'].sort());
});

test('M font: Japanese text without a font in the project is a problem', () => {
  const { M } = load();
  M.newProject('T', 800, 480);
  M.screen().children = [{ type: 'label', text: 'Temperature' }];
  deq(M.problems(), []);
  M.screen().children = [{ type: 'label', text: '温度' }];
  deq(M.problems().map(p => p.text), ['Japanese text is used (a screen), but the project has no font for it: it is shown as empty boxes. Project tab > Font']);
  M.screen().children = [{ type: 'label', text: '@temp' }];
  M.st.project.languages = ['ja'];
  M.st.project.texts = { temp: { ja: '温度' } };
  deq(M.problems().map(p => p.text), ['Japanese text is used (the texts), but the project has no font for it: it is shown as empty boxes. Project tab > Font']);
  M.st.project.font = { file: 'assets/fonts/NotoSansJP-subset.otf' };
  deq(M.problems(), []);
});
