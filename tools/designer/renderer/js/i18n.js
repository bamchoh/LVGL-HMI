// Language of the designer's own texts. Global `I18N`.
//
// Two kinds of text are translated:
//   - the names of properties (JSON keys such as bg_color): I18N.label('bg_color') -> '背景色'.
//     The key itself stays visible in the tooltip; the project files are not affected.
//   - fixed texts of the user interface, looked up by their English wording: I18N.t('Save').
// Anything without a translation is shown as it is (English), so a missing entry is harmless.
'use strict';

const I18N = {
  lang: 'en',
  LANGS: { ja: '日本語', en: 'English' },

  // property names (JSON keys)
  KEYS_JA: {
    // position / size
    x: 'X 位置', y: 'Y 位置', w: '幅', h: '高さ', align: '配置基準', width: '幅', height: '高さ',
    // common widget properties
    name: '名前', type: '種類', text: 'テキスト', title: 'タイトル', min: '最小値', max: '最大値', value: '値', step: '刻み',
    checked: 'オン状態', checkable: 'トグル動作', placeholder: '未入力時の表示', one_line: '1 行のみ', password: 'パスワード',
    max_length: '最大文字数', accepted_chars: '入力できる文字', input: '入力可', decimals: '小数桁数', format: '書式', unit: '単位',
    options: '選択肢', selected: '選択中', visible_rows: '表示行数', infinite: '無限スクロール', color: '色', on: '点灯',
    src: '画像ファイル', zoom: '拡大率', rotation: '回転', start_angle: '開始角度', end_angle: '終了角度', adjustable: '操作可',
    chart_type: 'グラフ種別', points: '点数', sample_ms: '更新周期 (ms)', hdiv: '横の分割数', vdiv: '縦の分割数', update_mode: '更新方法',
    time: '1 周の時間 (ms)', angle: '弧の角度', series: '系列', long_mode: '長いテキスト',
    // flags
    hidden: '非表示', disabled: '無効', clickable: 'タッチ可', scrollable: 'スクロール可', scrollbar: 'スクロールバー',
    // access
    level: '必要レベル', level_deny: 'レベル不足時',
    // layout
    flex: '並べ方', flex_main: '主軸の揃え', flex_cross: '交差軸の揃え', flex_track: '行の揃え', flex_grow: '伸長率',
    // style
    selector: '対象', bg_color: '背景色', bg_opa: '背景の不透明度', bg_grad_color: 'グラデーション色', bg_grad_dir: 'グラデーション方向',
    border_color: '枠線の色', border_width: '枠線の太さ', border_opa: '枠線の不透明度', outline_width: '外枠の太さ', outline_color: '外枠の色',
    radius: '角の丸み', shadow_width: '影の大きさ', shadow_color: '影の色', shadow_opa: '影の不透明度',
    text_color: '文字色', text_opa: '文字の不透明度', text_align: '文字揃え', text_letter_space: '文字間隔', text_line_space: '行間隔',
    font: '文字サイズ', pad: '内側の余白', pad_left: '余白 (左)', pad_right: '余白 (右)', pad_top: '余白 (上)', pad_bottom: '余白 (下)',
    pad_row: '行の間隔', pad_column: '列の間隔', pad_gap: '項目の間隔', margin: '外側の余白', opa: '不透明度',
    line_color: '線の色', line_width: '線の太さ', line_rounded: '線端を丸く', arc_color: '円弧の色', arc_width: '円弧の太さ', arc_rounded: '円弧の端を丸く',
    image_recolor: '画像の着色',
    // bind / actions
    tag: 'タグ', scale: '倍率', offset: 'オフセット', invert: '反転', two_way: '双方向', map: '値と色の対応', part: '部位',
    screen: '画面', anim: '切り替え効果', by: '増減量', fn: '関数名', arg: '引数', code: 'コード', what: '対象', lang: '言語',
    when: '動く条件', rate: '変化の速さ', idle: '止まっているときの値', noise: 'ゆらぎ', target: '目標',
    // project / display
    start_screen: '開始画面', dpi: 'DPI', fbdev: 'フレームバッファ', drm: 'DRM デバイス', swap_axes: 'XY を入れ替え',
    calibration: '補正値', splash: 'スプラッシュ画像', splash_bg: 'スプラッシュ背景色', splash_ms: '表示時間 (ms)', console: 'コンソール',
    dark: 'ダークテーマ', primary: 'メインカラー', secondary: 'サブカラー',
    // tags
    init: '初期値',
    // drivers
    enabled: '有効', host: 'ホスト', port: 'ポート', unit_id: 'ユニット番号', poll_ms: 'ポーリング周期 (ms)', timeout_ms: 'タイムアウト (ms)',
    status_tag: '接続状態タグ', swap_words: 'ワード順を入れ替え', device: 'シリアルポート', baud: 'ボーレート', data_bits: 'データ長',
    parity: 'パリティ', stop_bits: 'ストップビット', namespace: '名前空間', folder: 'フォルダー名', area: '種別', addr: 'アドレス',
    dtype: 'データ型', access: 'アクセス', mode: 'モード', period_ms: '周期 (ms)', values: '値の一覧',
    // logs
    path: '保存先', retention_days: '保存日数', max_rows: '最大行数', max_size_mb: '最大サイズ (MB)', trigger: 'トリガー',
    interval_ms: '周期 (ms)', tags: 'タグ', trigger_tag: 'トリガータグ', condition: '条件', value_tag: '比較するタグ',
    severity: '重要度', message: 'メッセージ', user_tag: 'ユーザー名タグ',
    // users
    levels: 'レベル名', system_level: 'システム設定に必要なレベル', auto_logout_s: '自動ログアウト (秒)', max_failures: '失敗の許容回数',
    lock_s: 'ロック時間 (秒)', PIN: 'PIN',
  },

  // fixed texts, by their English wording
  TEXT_JA: {
    // toolbar
    'New': '新規', 'Open': '開く', 'Save': '保存', 'Save As': '別名で保存', 'Screen': '画面', 'Fit': '全体表示',
    'Preview': 'プレビュー', 'Run': '実行', 'Deploy': 'デプロイ', 'JSON': 'JSON',
    // tabs
    'Widgets': '部品', 'Screens': '画面一覧', 'Project': 'プロジェクト', 'Tags': 'タグ', 'Drivers': 'ドライバ', 'Scripts': 'スクリプト',
    'Logs': 'ログ', 'Users': 'ユーザー',
    // property panel sections
    'Widget': '部品の設定', 'Position & size': '位置とサイズ', 'Layout': 'レイアウト', 'Layout (flex)': 'レイアウト (flex)', 'Flags': '状態',
    'Access (users)': 'アクセス制限', 'Style': 'スタイル', 'Bind (tags)': 'タグとの連動', 'Events / actions': 'イベントと動作',
    'Display': '表示', 'Theme': 'テーマ', 'Touch panel': 'タッチパネル', 'Raspberry Pi (fbdev / drm / evdev)': 'Raspberry Pi (デバイス)',
    'Boot splash (Raspberry Pi)': '起動時のスプラッシュ (Raspberry Pi)', 'project.json (raw)': 'project.json (直接編集)',
    // widget palette
    'Panel': 'パネル', 'Label': 'ラベル', 'Button': 'ボタン', 'Switch': 'スイッチ', 'Checkbox': 'チェックボックス', 'Slider': 'スライダー',
    'Bar': 'バー', 'Arc': '円弧メーター', 'LED': 'ランプ', 'Dropdown': 'ドロップダウン', 'Roller': 'ローラー', 'Text area': 'テキスト入力',
    'Numeric display': '数値表示', 'Text display': '文字表示', 'Password input': 'パスワード入力', 'Image': '画像', 'Chart': 'グラフ',
    'Spinner': 'スピナー', 'Keyboard': 'キーボード', 'Line': '線',
    // common words
    'Close': '閉じる', 'Cancel': 'キャンセル', 'OK': 'OK', 'Apply': '適用', 'Create': '作成', 'Check': '確認', 'Delete': '削除',
    '(default)': '(既定)', '(unsaved)': '(未保存)', 'preset': 'プリセット', 'presets': 'プリセット',
    '+ add property…': '+ 項目を追加…', '+ row': '+ 行を追加', '+ Add screen': '+ 画面を追加', '+ Add': '+ 追加', '+ user': '+ ユーザー',
    'more style properties': 'その他のスタイル項目',
    'Click to add to the selected panel (or the screen).': 'クリックすると、選択中のパネル（または画面）に追加します。',
    'Select a widget on the canvas or in the tree.': 'キャンバスかツリーで部品を選択してください。',
    'The radio button marks the start screen. Files are screens/<name>.json; renaming keeps the file name.':
      '丸いボタンが開始画面です。ファイルは screens/<名前>.json に保存され、名前を変えてもファイル名はそのままです。',
    'New project': '新しいプロジェクト', 'width × height': '幅 × 高さ', 'Deploy to target': 'ターゲットへデプロイ',
    'touch calibration': 'タッチ補正', 'left edge': '左端', 'top edge': '上端', 'right edge': '右端', 'bottom edge': '下端',
    'display preset': '画面のプリセット', '(choose to apply)': '(選ぶと適用)',
    'Sets the size and the touch correction for a known display.': '画面サイズとタッチ補正を、その表示器の標準値に設定します。',
    'Raw values the touch panel reports at the edges of the screen (after the axis swap). Take them from a preset, or measure with tools/rpi/touch_raw.py.':
      'タッチパネルが画面の端で返す生の値です（XY 入れ替え後）。プリセットを使うか、tools/rpi/touch_raw.py で測ってください。',
  },

  init(saved) {
    const nav = (typeof navigator !== 'undefined' && navigator.language) || 'en';
    this.lang = (saved && this.LANGS[saved]) ? saved : (/^ja/i.test(nav) ? 'ja' : 'en');
  },
  set(lang) { if (this.LANGS[lang]) this.lang = lang; },

  // the display name of a property key; the key itself when there is none
  label(key) {
    if (this.lang !== 'ja') return key;
    const k = String(key);
    return Object.prototype.hasOwnProperty.call(this.KEYS_JA, k) ? this.KEYS_JA[k] : this.t(k);
  },
  // has the key a display name that differs from it? (then the key is worth showing in a tooltip)
  named(key) { return this.lang === 'ja' && Object.prototype.hasOwnProperty.call(this.KEYS_JA, String(key)); },
  // a fixed text
  t(text) {
    if (this.lang !== 'ja') return text;
    const s = String(text);
    if (Object.prototype.hasOwnProperty.call(this.TEXT_JA, s)) return this.TEXT_JA[s];
    // texts with values in them ("saved D:/x/project.json")
    for (const [re, to] of this.PATTERNS_JA) if (re.test(s)) return s.replace(re, to);
    if (this.collect && /[A-Za-z]{3}/.test(s) && /\s/.test(s.trim())) this.collect.add(s);   // development aid: phrases without a translation
    return text;
  },
  // a text with values in it: I18N.f('Delete screen "{0}"?', name)
  f(template, ...args) {
    return this.t(template).replace(/\{(\d)\}/g, (m, i) => (args[Number(i)] === undefined ? '' : String(args[Number(i)])));
  },
  // a value of a choice (align: center): "中央 (center)". The value itself stays visible, since
  // it is what the project file and the documentation say.
  value(v) {
    const s = String(v);
    return this.lang === 'ja' && Object.prototype.hasOwnProperty.call(this.VALUES_JA, s) ? this.VALUES_JA[s] + '  (' + s + ')' : s;
  },
  VALUES_JA: {},                                    // filled by i18n_ja.js
  collect: null,
  PATTERNS_JA: [],                                  // filled by i18n_ja.js
};
