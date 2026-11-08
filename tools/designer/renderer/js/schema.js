// What the HML runtime understands (mirrors src/ui.cpp, src/project.cpp and the drivers).
// Field types: text, number, bool, select, color, size, lines (array of strings), points, image, json
'use strict';

const SCHEMA = {
  aligns: ['', 'top_left', 'top_mid', 'top_right', 'left_mid', 'center', 'right_mid', 'bottom_left', 'bottom_mid', 'bottom_right'],
  anims: ['none', 'fade', 'slide_left', 'slide_right', 'slide_top', 'slide_bottom', 'over_left', 'over_right', 'move_left', 'move_right', 'move_top', 'move_bottom'],
  fonts: [12, 14, 16, 18, 20, 24, 28, 32, 40, 48],
  events: ['on_click', 'on_press', 'on_release', 'on_long_press', 'on_long_press_repeat', 'on_change', 'on_ready'],
  tagTypes: ['bool', 'int', 'float', 'string'],

  // widget types: def = values written when the widget is created, props = type specific fields
  types: {
    panel:    { label: 'Panel', container: true, def: { w: 200, h: 120 }, props: [] },
    label:    { label: 'Label', def: { text: 'Label' },
                props: [{ k: 'text', t: 'text' }, { k: 'long_mode', t: 'select', o: ['', 'wrap', 'dots', 'scroll', 'circular', 'clip'] }] },
    button:   { label: 'Button', def: { text: 'Button' },
                props: [{ k: 'text', t: 'text' }, { k: 'checkable', t: 'bool' }] },
    switch:   { label: 'Switch', def: {}, props: [{ k: 'checked', t: 'bool' }] },
    checkbox: { label: 'Checkbox', def: { text: 'Check' }, props: [{ k: 'text', t: 'text' }, { k: 'checked', t: 'bool' }] },
    slider:   { label: 'Slider', def: { w: 200, min: 0, max: 100 },
                props: [{ k: 'min', t: 'number' }, { k: 'max', t: 'number' }, { k: 'value', t: 'number' }] },
    bar:      { label: 'Bar', def: { w: 200, h: 10, min: 0, max: 100 },
                props: [{ k: 'min', t: 'number' }, { k: 'max', t: 'number' }, { k: 'value', t: 'number' }] },
    arc:      { label: 'Arc', def: { w: 130, h: 130, min: 0, max: 100 },
                props: [{ k: 'min', t: 'number' }, { k: 'max', t: 'number' }, { k: 'value', t: 'number' },
                        { k: 'start_angle', t: 'number' }, { k: 'end_angle', t: 'number' }, { k: 'rotation', t: 'number' },
                        { k: 'adjustable', t: 'bool' }] },
    led:      { label: 'LED', def: { w: 26, h: 26, color: '#4caf50' }, props: [{ k: 'color', t: 'color' }, { k: 'on', t: 'bool' }] },
    dropdown: { label: 'Dropdown', def: { w: 160, options: ['Option 1', 'Option 2', 'Option 3'] },
                props: [{ k: 'options', t: 'lines' }, { k: 'selected', t: 'number' }] },
    roller:   { label: 'Roller', def: { w: 120, options: ['One', 'Two', 'Three'], visible_rows: 3 },
                props: [{ k: 'options', t: 'lines' }, { k: 'selected', t: 'number' }, { k: 'visible_rows', t: 'number' }, { k: 'infinite', t: 'bool' }] },
    textarea: { label: 'Text area', def: { w: 200, one_line: true, placeholder: 'type here' },
                props: [{ k: 'text', t: 'text' }, { k: 'placeholder', t: 'text' }, { k: 'one_line', t: 'bool' }, { k: 'password', t: 'bool' },
                        { k: 'max_length', t: 'number' }, { k: 'accepted_chars', t: 'text' }] },
    numeric_display: { label: 'Numeric display', def: { w: 120, decimals: 1 },
                props: [{ k: 'input', t: 'bool' }, { k: 'decimals', t: 'number' }, { k: 'format', t: 'text' }, { k: 'unit', t: 'text' },
                        { k: 'min', t: 'number' }, { k: 'max', t: 'number' }, { k: 'placeholder', t: 'text' }, { k: 'accepted_chars', t: 'text' }] },
    text_display: { label: 'Text display', def: { w: 160 },
                props: [{ k: 'input', t: 'bool' }, { k: 'text', t: 'text' }, { k: 'placeholder', t: 'text' }, { k: 'max_length', t: 'number' },
                        { k: 'accepted_chars', t: 'text' }, { k: 'one_line', t: 'bool' }] },
    password_display: { label: 'Password input', def: { w: 160, max_length: 8 },
                props: [{ k: 'input', t: 'bool' }, { k: 'placeholder', t: 'text' }, { k: 'max_length', t: 'number' }, { k: 'accepted_chars', t: 'text' }] },
    image:    { label: 'Image', def: {}, props: [{ k: 'src', t: 'image' }, { k: 'zoom', t: 'number' }, { k: 'rotation', t: 'number' }] },
    chart:    { label: 'Chart', def: { w: 300, h: 160, chart_type: 'line', points: 100, min: 0, max: 100, sample_ms: 500, hdiv: 3, vdiv: 5, series: [] },
                props: [{ k: 'chart_type', t: 'select', o: ['line', 'bar', 'scatter'] }, { k: 'points', t: 'number' }, { k: 'min', t: 'number' },
                        { k: 'max', t: 'number' }, { k: 'sample_ms', t: 'number' }, { k: 'hdiv', t: 'number' }, { k: 'vdiv', t: 'number' },
                        { k: 'update_mode', t: 'select', o: ['', 'shift', 'circular'] }], series: true },
    spinner:  { label: 'Spinner', def: { w: 60, h: 60 }, props: [{ k: 'time', t: 'number' }, { k: 'angle', t: 'number' }] },
    keyboard: { label: 'Keyboard', def: { align: 'bottom_mid', w: '100%', h: '45%' }, props: [] },
    line:     { label: 'Line', def: { points: [[0, 0], [120, 60]] }, props: [{ k: 'points', t: 'points' }] },
  },

  // keys of the widget object that are not "props" (handled by dedicated editors)
  common: [
    { k: 'x', t: 'number' }, { k: 'y', t: 'number' }, { k: 'w', t: 'size' }, { k: 'h', t: 'size' },
    { k: 'align', t: 'select', o: null /* aligns */ },
  ],
  flags: [
    { k: 'hidden', t: 'bool' }, { k: 'disabled', t: 'bool' }, { k: 'clickable', t: 'bool' },
    { k: 'scrollable', t: 'bool' }, { k: 'scrollbar', t: 'select', o: ['', 'off', 'on', 'auto', 'active'] },
  ],
  flex: [
    { k: 'flex', t: 'select', o: ['', 'row', 'column', 'row_wrap', 'column_wrap', 'row_reverse', 'column_reverse'] },
    { k: 'flex_main', t: 'select', o: ['', 'start', 'center', 'end', 'space_between', 'space_around', 'space_evenly'] },
    { k: 'flex_cross', t: 'select', o: ['', 'start', 'center', 'end'] },
    { k: 'flex_track', t: 'select', o: ['', 'start', 'center', 'end', 'space_between', 'space_around', 'space_evenly'] },
    { k: 'flex_grow', t: 'number' },
  ],

  styleSelectors: ['style', 'style_pressed', 'style_checked', 'style_disabled', 'style_focused', 'style_indicator',
                   'style_indicator_checked', 'style_knob', 'style_items', 'style_selected', 'style_label', 'style_scrollbar'],
  styleKeys: {
    bg_color: 'color', bg_opa: 'number', bg_grad_color: 'color', bg_grad_dir: ['hor', 'ver'],
    border_color: 'color', border_width: 'number', border_opa: 'number', outline_width: 'number', outline_color: 'color',
    radius: 'number', shadow_width: 'number', shadow_color: 'color', shadow_opa: 'number',
    text_color: 'color', text_opa: 'number', text_align: ['left', 'center', 'right'], text_letter_space: 'number', text_line_space: 'number',
    font: 'font', pad: 'number', pad_left: 'number', pad_right: 'number', pad_top: 'number', pad_bottom: 'number',
    pad_row: 'number', pad_column: 'number', pad_gap: 'number', margin: 'number', opa: 'number',
    line_color: 'color', line_width: 'number', line_rounded: 'bool', arc_color: 'color', arc_width: 'number', arc_rounded: 'bool',
    image_recolor: 'color', width: 'size', height: 'size',
  },

  bindKinds: ['text', 'value', 'checked', 'on', 'visible', 'hidden', 'enabled', 'disabled', 'bg_color', 'text_color', 'options', 'src'],
  // the property a plain "bind": "tag" means for each widget type
  naturalBind: { switch: 'checked', checkbox: 'checked', button: 'checked', slider: 'value', arc: 'value', dropdown: 'value',
                 roller: 'value', bar: 'value', label: 'text', led: 'on', textarea: 'text', image: 'src', spinner: 'visible', panel: 'visible',
                 numeric_display: 'value', text_display: 'text', password_display: 'text' },
  bindOps: ['', 'eq', 'ne', 'gt', 'lt', 'ge', 'le'],

  actions: {
    set:    [{ k: 'tag', t: 'tag' }, { k: 'value', t: 'text' }],
    toggle: [{ k: 'tag', t: 'tag' }],
    inc:    [{ k: 'tag', t: 'tag' }, { k: 'by', t: 'number' }],
    dec:    [{ k: 'tag', t: 'tag' }, { k: 'by', t: 'number' }],
    goto:   [{ k: 'screen', t: 'screen' }, { k: 'anim', t: 'anim' }],
    back:   [], reload: [], exit: [],
    restart: [{ k: 'what', t: 'select', o: ['', 'app', 'system'] }],
    system: [{ k: 'anim', t: 'anim' }],
    call:   [{ k: 'fn', t: 'text' }, { k: 'arg', t: 'text' }],
  },

  drivers: {
    sim: {
      label: 'Simulation', fields: [],
      table: { key: 'items', label: 'Items', columns: [
        { k: 'tag', t: 'tag' }, { k: 'mode', t: 'select', o: ['sine', 'triangle', 'ramp', 'square', 'random', 'counter', 'cycle'] },
        { k: 'min', t: 'number' }, { k: 'max', t: 'number' }, { k: 'period_ms', t: 'number' }, { k: 'values', t: 'csv' }] },
    },
    modbus_tcp: {
      label: 'Modbus TCP client',
      fields: [{ k: 'host', t: 'text' }, { k: 'port', t: 'number' }, { k: 'unit_id', t: 'number' }, { k: 'poll_ms', t: 'number' },
               { k: 'timeout_ms', t: 'number' }, { k: 'status_tag', t: 'tag' }, { k: 'swap_words', t: 'bool' }],
      table: { key: 'map', label: 'Register map', columns: [
        { k: 'tag', t: 'tag' }, { k: 'area', t: 'select', o: ['holding', 'input', 'coil', 'discrete'] }, { k: 'addr', t: 'number' },
        { k: 'dtype', t: 'select', o: ['', 'int16', 'uint16', 'int32', 'uint32', 'float', 'bool'] },
        { k: 'scale', t: 'number' }, { k: 'offset', t: 'number' }, { k: 'access', t: 'select', o: ['', 'rw', 'ro', 'wo'] }] },
    },
    opcua_server: {
      label: 'OPC UA server',
      fields: [{ k: 'port', t: 'number' }, { k: 'namespace', t: 'text' }, { k: 'folder', t: 'text' }],
      table: { key: 'tags', label: 'Published tags (empty = all tags, read/write)', columns: [
        { k: 'tag', t: 'tag' }, { k: 'access', t: 'select', o: ['rw', 'ro'] }], stringItems: true },
    },
  },
  driverDefaults: {
    sim: { type: 'sim', items: [] },
    modbus_tcp: { type: 'modbus_tcp', enabled: true, host: '127.0.0.1', port: 502, unit_id: 1, poll_ms: 200, timeout_ms: 1000, map: [] },
    opcua_server: { type: 'opcua_server', port: 4840, namespace: 'urn:hml:runtime', folder: 'HML' },
  },
};
