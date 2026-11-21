// Japanese texts of the designer (the property names and the short words are in i18n.js).
//   TEXT_JA      whole texts, by their English wording
//   PATTERNS_JA  texts with values in them: [regular expression, replacement with $1 ...]
// Messages that come from the host program (the log of a deploy, error details) stay English.
'use strict';

Object.assign(I18N.TEXT_JA, {
  // ---- toolbar tooltips
  'New project (Ctrl+N)': '新しいプロジェクト (Ctrl+N)', 'Open project.json (Ctrl+O)': 'プロジェクトを開く (Ctrl+O)', 'Save (Ctrl+S)': '保存 (Ctrl+S)',
  'Save as': '別名で保存', 'Undo (Ctrl+Z)': '元に戻す (Ctrl+Z)', 'Redo (Ctrl+Y)': 'やり直し (Ctrl+Y)', 'Zoom out': '縮小', 'Zoom in': '拡大',
  'Fit to window': 'ウィンドウに合わせる', 'Show the JSON of the current screen': '現在の画面の JSON を表示',
  'Render this screen with hml_runtime (saves first)': 'この画面を hml_runtime で描画して表示（先に保存します）',
  'Launch hml_runtime with this project (saves first)': 'このプロジェクトを hml_runtime で起動（先に保存します）',
  'Send this project to a target (Raspberry Pi) over SSH and restart the runtime there (saves first)': 'このプロジェクトを SSH でターゲット（Raspberry Pi）へ送り、ランタイムを再起動（先に保存します）',
  'Language of the designer / 表示言語': '表示言語 / Language of the designer',
  'Screen editor (Ctrl+1)': '画面の編集 (Ctrl+1)', 'Tag list (Ctrl+2)': 'タグ一覧 (Ctrl+2)', 'Drivers (Ctrl+3)': 'ドライバ (Ctrl+3)',
  'Lua scripts (Ctrl+4)': 'Lua スクリプト (Ctrl+4)', 'Data log, alarms, operation log, system log (Ctrl+5)': '変数ロギング・アラーム・操作ログ・システムログ (Ctrl+5)',
  'Users, levels and login (Ctrl+6)': 'ユーザー・レベル・ログイン (Ctrl+6)',
  'Duplicate (Ctrl+D)': '複製 (Ctrl+D)', 'Delete (Del)': '削除 (Del)', 'Move up (earlier = drawn below)': '前へ（前にあるほど下に描かれる）',
  'Move down (later = drawn on top)': '後ろへ（後ろにあるほど上に描かれる）', 'Move into the previous panel': '1 つ前のパネルの中へ入れる',
  'Move out of the parent panel': '親パネルの外へ出す',

  // ---- screens / tags
  'start screen': '開始画面', 'rename': '名前の変更', 'delete': '削除', 'remove': '削除', 'filter by name': '名前で絞り込み', '+ Add tag': '+ タグを追加',
  'Screen name': '画面の名前', 'New screen name': '新しい画面の名前', 'name already used or invalid': 'その名前はすでに使われているか、使えない名前です',
  'Discard unsaved changes?': '保存していない変更を破棄しますか？',

  // ---- property panel
  'drag to resize': 'ドラッグでサイズ変更', 'px / 50% / content': 'px / 50% / content', '#rrggbb / red': '#rrggbb / red', 'value or {tag}': '値 または {タグ}',
  '+ bind': '+ 連動を追加', '+ add event…': '+ イベントを追加…', '+ action': '+ 動作を追加', 'remove event': 'イベントを削除', '+ map entry': '+ 対応を追加',
  'map value → color': '値 → 色の対応', '+ series': '+ 系列を追加', 'series (tag / color / scale)': '系列（タグ / 色 / 倍率）', 'x,y per line': '1 行に x,y',
  'choose a PNG (copied into the project)': 'PNG を選ぶ（プロジェクトにコピーされます）', 'tag': 'タグ', 'type here': 'ここに入力',
  'minimum user level to open this screen (0 / empty = everybody); the login screen is shown first. Needs users (Users tab)':
    'この画面を開くのに必要なユーザーレベル（0 / 空欄 = 誰でも）。足りないときはログイン画面が先に出ます。ユーザー管理（ユーザー タブ）が必要です',
  'minimum user level to operate this (0 / empty = everybody). Levels are defined on the Users tab':
    'この部品を操作するのに必要なユーザーレベル（0 / 空欄 = 誰でも）。レベルはユーザー タブで定義します',
  'for users below the level: disable (greyed out), hide, or login (looks normal; a touch opens the login screen)':
    'レベルが足りないユーザーへの見せ方: disable（薄く表示して操作不可）、hide（非表示）、login（通常表示。触るとログイン画面）',

  // ---- project tab
  'SDL window zoom (PC only)': 'PC で動かすときのウィンドウ拡大率', 'theme scale, default 130': 'テーマの拡大基準（既定 130）',
  'the panel reports X where the screen has Y': 'タッチパネルの X が画面の Y にあたる場合にオン',
  'empty = auto detect': '空欄 = 自動検出',
  'Shown during boot by hml-splash.service (hml_runtime --splash) until the HMI starts. PNG, centred and shrunk to fit; empty = the project title. Background default: black.':
    '起動中、HMI が始まるまで hml-splash.service（hml_runtime --splash）が表示します。PNG を中央に、収まるように縮小して表示。空欄ならプロジェクトのタイトル。背景の既定は黒です。',
  '3.5" LCD 480x320 (ADS7846 touch)': '3.5 インチ LCD 480x320（ADS7846 タッチ）', '7" touch display 800x480': '7 インチ タッチディスプレイ 800x480',
  '10.1" touch display 1024x600': '10.1 インチ タッチディスプレイ 1024x600', 'HD 1280x720': 'HD 1280x720',
  'You will be asked where to save the project: as one file (name.hmlproj, easy to carry around), or as a folder (choose "Project folder" in the dialog: project.json with screens/ and assets/ next to it).':
    '保存先はこのあと尋ねます。1 つのファイル（名前.hmlproj。持ち運びしやすい）か、フォルダー（ダイアログで "Project folder" を選ぶ。project.json と、その隣に screens/・assets/）を選べます。',

  // ---- drivers
  'Simulation': 'シミュレーション', 'Modbus TCP client': 'Modbus TCP クライアント', 'Modbus RTU client (serial)': 'Modbus RTU クライアント（シリアル）',
  'Modbus ASCII client (serial)': 'Modbus ASCII クライアント（シリアル）', 'OPC UA server': 'OPC UA サーバー',
  'Simulation (sim)': 'シミュレーション (sim)', 'Modbus TCP client (modbus_tcp)': 'Modbus TCP クライアント (modbus_tcp)',
  'Modbus RTU client (serial) (modbus_rtu)': 'Modbus RTU クライアント（シリアル） (modbus_rtu)',
  'Modbus ASCII client (serial) (modbus_ascii)': 'Modbus ASCII クライアント（シリアル） (modbus_ascii)', 'OPC UA server (opcua_server)': 'OPC UA サーバー (opcua_server)',
  'Items': '項目', 'Register map': 'レジスタの割り付け', 'Published tags (empty = all tags, read/write)': '公開するタグ（空 = すべてのタグを読み書き可で公開）',
  'raw JSON': 'JSON を直接編集', 'Select a driver on the left.': '左の一覧からドライバを選んでください。',
  'No drivers. Add one on the left (simulator, Modbus TCP, OPC UA server ...).': 'ドライバがありません。左で追加してください（シミュレーター、Modbus TCP、OPC UA サーバーなど）。',
  'Unknown driver type; edit the JSON below.': '未対応のドライバです。下の JSON を直接編集してください。',
  'serial port: /dev/ttyUSB0 on the device, COM4 on Windows. Leave empty and fill in host / port to go through a serial gateway (TCP)':
    'シリアルポート。装置では /dev/ttyUSB0、Windows では COM4 など。空欄にしてホスト / ポートを入れると、シリアル–イーサネット変換器（TCP）経由になります',
  'unit (slave) address; a row of the map may name another one': 'ユニット（スレーブ）番号。割り付けの行ごとに別の番号を指定できます',
  'how long to wait for an answer (default 500)': '応答を待つ時間（既定 500）',
  'serial gateway: its address (only without device)': 'シリアル変換器のアドレス（シリアルポートが空欄のときだけ）', 'serial gateway: its TCP port': 'シリアル変換器の TCP ポート',
  'device on this PC': 'この PC でのポート', 'e.g. COM4 (empty: the same as device)': '例: COM4（空欄ならシリアルポートと同じ）',
  'the port to use when the project runs on this PC (Run / Preview). Kept on this PC (project.local.json), not deployed':
    'このプロジェクトをこの PC で動かすとき（実行 / プレビュー）に使うポート。この PC にだけ保存され（project.local.json）、デプロイでは送られません',
  'Find serial ports …': 'シリアルポートを探す …', 'lists the ports of the target and of this PC': 'ターゲットとこの PC のポートを一覧表示します',
  'save the project first': '先にプロジェクトを保存してください', 'enabled': '有効',

  // ---- scripts
  '+ Add script': '+ スクリプトを追加', 'Lua API': 'Lua で使えるもの', 'when the script runs': 'スクリプトを実行するタイミング',
  'period in milliseconds (minimum 10)': '周期（ミリ秒、最小 10）',
  'Lua 5.4. All scripts share one Lua state: a function or variable defined by a startup script can be used by the others.':
    'Lua 5.4。すべてのスクリプトは 1 つの Lua 環境を共有します。startup スクリプトで定義した関数や変数は、ほかのスクリプトから使えます。',

  // ---- logs
  'Data log': '変数ロギング', 'Alarms': 'アラーム', 'Operation log': '操作ログ', 'System log': 'システムログ', ' record': ' 記録する', 'Groups': 'グループ',
  'rows older than this are deleted (default 30, 0 = keep forever)': 'これより古い行を削除（既定 30、0 = 無期限）',
  'size of the database in MB; the oldest rows are deleted above this (default 1000, 0 = unlimited)': 'データベースの容量（MB）。超えたら古い行から削除（既定 1000、0 = 無制限）',
  'string tag holding the current operator; its value is stored with every row': '現在のオペレーター名を持つ文字列タグ。各行にその値を記録します',
  'minimum level that is recorded': '記録する最低レベル',
  'Tag values over time, in groups. A group is recorded periodically (interval), when one of its tags changes (change; interval_ms = minimum time between samples), when trigger_tag turns on (tag), or only on request (manual: call "data_log.sample"). Export: hml_runtime --export-data-log out.csv project.json --group <name>, or call "data_log.export" (arg = group or group,file).':
    'タグの値をグループ単位で時系列に記録します。記録のタイミングは、周期（interval）、グループ内のタグが変化したとき（change。interval_ms は最小間隔）、トリガータグがオンになったとき（tag）、要求されたときだけ（manual: call "data_log.sample"）から選びます。書き出し: hml_runtime --export-data-log out.csv project.json --group <名前>、または call "data_log.export"（引数 = グループ、または グループ,ファイル）。',
  'A condition on a tag; every raise and clear is recorded. condition "true" = the tag is on / non-zero; the others compare the tag with value, or with value_tag if given. Export: hml_runtime --export-alarm-log out.csv project.json, or call "alarm_log.export".':
    'タグに対する条件です。発生と復帰を 1 行ずつ記録します。条件 "true" はタグがオン（0 以外）、そのほかはタグを値（または比較するタグ）と比べます。書き出し: hml_runtime --export-alarm-log out.csv project.json、または call "alarm_log.export"。',
  'What the operator did on the screens: values written from widgets and by set / toggle / inc / dec actions (old and new value), screen changes, scripts and calls started from widget events. Text typed into a password field is never stored. Export: hml_runtime --export-operation-log out.csv project.json, or call "operation_log.export".':
    'オペレーターが画面で行った操作を記録します。部品や set / toggle / inc / dec の動作によるタグへの書き込み（変更前後の値）、画面の切り替え、イベントから実行したスクリプトや call。パスワード欄に入力した内容は記録しません。書き出し: hml_runtime --export-operation-log out.csv project.json、または call "operation_log.export"。',
  'Runtime messages: script errors, driver connects / disconnects, project load messages, Lua log() / warn(). Export: hml_runtime --export-system-log out.csv project.json, or call "system_log.export".':
    'ランタイムのメッセージを記録します。スクリプトのエラー、ドライバの接続 / 切断、プロジェクト読み込み時のメッセージ、Lua の log() / warn()。書き出し: hml_runtime --export-system-log out.csv project.json、または call "system_log.export"。',

  // ---- users
  ' user management': ' ユーザー管理を使う',
  'names of level 1, 2, 3 ... (default: Operator, Maintenance, Admin)': 'レベル 1, 2, 3 … の名前（既定: Operator, Maintenance, Admin）',
  'level needed to open the system settings on the device (default: the highest level; 0 = no restriction)': '装置のシステム設定を開くのに必要なレベル（既定: いちばん高いレベル。0 = 制限なし）',
  'log out after this many seconds without input (default 300, 0 = never)': 'この秒数だけ操作がないとログアウト（既定 300、0 = しない）',
  'wrong PINs in a row before the user is locked (default 5)': 'PIN を続けてこの回数まちがえるとロック（既定 5）',
  'how long a locked user must wait, in seconds (default 60)': 'ロックされたユーザーが待つ時間（秒、既定 60）',
  'set (type a new one to change)': '設定済み（変えるときは新しい PIN を入力）', 'not set: 4 to 8 digits': '未設定: 4〜8 桁の数字', 'PIN: 4 to 8 digits': 'PIN は 4〜8 桁の数字です',
  'Users log in on the device with a PIN (the built-in login screen: action "login", or automatically when something needs a level). Give screens and widgets a minimum level in their properties (Screen > level, Access > level). Level 0 = nobody logged in. Tags for your screens: sys.user.name, sys.user.level, sys.user.level_name, sys.user.logged_in.':
    'ユーザーは装置で PIN を入れてログインします（組み込みのログイン画面。動作 "login" で開くほか、レベルが必要な操作をすると自動で出ます）。画面と部品には、プロパティで必要レベルを設定します（画面 > 必要レベル、アクセス制限 > 必要レベル）。レベル 0 は誰もログインしていない状態です。画面で使えるタグ: sys.user.name、sys.user.level、sys.user.level_name、sys.user.logged_in。',
  'PINs are stored as salted hashes, not as text. A PIN is short, so this only keeps it from being read by accident; anyone who has the project file can still try all combinations. Users can also be added, changed and removed on the device (System > Users). Once that was done, the device keeps its own list (project.local.json) and a deploy does not replace it; the Deploy dialog shows this and can remove the device\'s list.':
    'PIN は文字のままではなく、ソルト付きのハッシュで保存します。PIN は桁数が少ないので、うっかり読めてしまうことを防ぐ程度の保護です（プロジェクトのファイルを持っている人は総当たりで割り出せます）。ユーザーは装置の画面（System > Users）でも追加・変更・削除できます。装置で編集すると、装置は自分の一覧（project.local.json）を使うようになり、デプロイしても置き換わりません。デプロイのダイアログにその旨が表示され、装置側の一覧を消すこともできます。',

  // ---- save / open
  'Save the project in its own folder?': 'プロジェクト専用のフォルダーに保存しますか？', 'Save here anyway': 'このまま保存', 'Create the folder': 'フォルダーを作る',
  'new folder': '新しいフォルダー',
  'A project should have a folder of its own: project.json, screens/ and assets/ are created in it, and its name is the name of the project on the target.':
    'プロジェクトには専用のフォルダーを使ってください。その中に project.json、screens/、assets/ が作られ、フォルダー名がターゲットでのプロジェクト名になります。',
  'Letters, digits, . _ - only (it is also used as the folder name on the target).': '英数字と . _ - だけが使えます（ターゲットでのフォルダー名にもなります）。',
  'folder name: letters, digits, . _ - (not starting with . or -)': 'フォルダー名には英数字と . _ - が使えます（先頭に . や - は使えません）',
  'save the project first (images are copied into its assets/ folder)': '先にプロジェクトを保存してください（画像は assets/ フォルダーにコピーされます）',
  'hml_runtime not found next to the designer: Preview / Run are unavailable': 'デザイナーと同じ場所に hml_runtime が見つかりません。プレビューと実行は使えません',

  // ---- preview
  'Rendering with hml_runtime…': 'hml_runtime で描画しています…',

  // ---- serial ports dialog
  'Serial ports': 'シリアルポート', 'Looking for serial ports …': 'シリアルポートを探しています …', 'No serial port was found.': 'シリアルポートが見つかりませんでした。',
  'On the target': 'ターゲット', 'On this PC': 'この PC',
  'Goes into the project ("device"): the name the port has where the project will run.': 'プロジェクト（シリアルポートの欄）に入ります。プロジェクトを動かす機械での名前です。',
  'The long name on the right stays the same when adapters are plugged in another order; /dev/ttyUSB0 may become /dev/ttyUSB1.':
    '右の長い名前は、変換器を挿す順番が変わっても同じです。/dev/ttyUSB0 は /dev/ttyUSB1 に変わることがあります。',
  'The target is not known yet: enter its address once in the Deploy dialog (and deploy), then its ports are listed here.':
    'ターゲットがまだ分かりません。デプロイのダイアログでアドレスを入れて一度デプロイすると、ここにターゲットのポートが出ます。',
  'For running the project here (Run / Preview). Stored on this PC only (project.local.json), not deployed.':
    'この PC で動かすとき（実行 / プレビュー）用です。この PC にだけ保存され（project.local.json）、デプロイでは送られません。',

  // ---- deploy dialog
  'target (host)': 'ターゲット（ホスト）', 'user': 'ユーザー', 'private key': '秘密鍵', 'remote folder': '配置先フォルダー', 'send runtime': 'ランタイムも送る',
  'run on target': 'ターゲットで表示', 'restart runtime': 'ランタイムを再起動', 'local settings': 'ローカル設定',
  'Register…': '鍵を登録…',
  '192.168.0.10 or raspberrypi.local': '192.168.0.10 または raspberrypi.local', 'default key (~/.ssh/id_*)': '既定の鍵 (~/.ssh/id_*)',
  'hml_runtime built for the target (Linux)': 'ターゲット用にビルドした hml_runtime（Linux）',
  'Register this key on the target (the target\'s screen must be in key management mode: System > Keys)': 'この鍵をターゲットに登録します（ターゲットの画面を鍵管理モードにしておきます: System > Keys）',
  'make this the project the target shows (it keeps doing so after a reboot)': 'このプロジェクトをターゲットの表示にする（再起動後もそのまま）',
  'restart hml_runtime on the target after installing': '配置のあと、ターゲットの hml_runtime を再起動する',
  'remove the settings changed on the target (project.local.json)': 'ターゲットで変更された設定（project.local.json）を消す',
  'Uses ssh / scp with key authentication (no password prompt). logs/ on the target are kept.': 'ssh / scp を鍵認証で使います（パスワードは聞きません）。ターゲットの logs/ は残ります。',
  'Enter the target (host) and the user.': 'ターゲット（ホスト）とユーザーを入力してください。', 'Enter the target (host) first.': '先にターゲット（ホスト）を入力してください。',
  'Deployed.': 'デプロイしました。', 'hml_runtime is not running on the target.': 'ターゲットで hml_runtime が動いていません。',
  'The target is running this project.': 'ターゲットはこのプロジェクトを表示しています。',
  'Local settings on the target': 'ターゲットのローカル設定', 'Files': 'ファイル',
  'None: every driver setting comes from this project.': 'ありません。ドライバ設定はすべてこのプロジェクトの値が使われます。',
  'These will be removed by this deploy; the project\'s values take effect.': 'このデプロイで削除され、プロジェクトの値が有効になります。',
  'These were changed on the target and stay in effect after a deploy (the project\'s values are not used):': 'ターゲットで変更された設定です。デプロイしてもこちらが優先されます（プロジェクトの値は使われません）:',
  'driver': 'ドライバ', 'setting': '項目', 'on the target': 'ターゲットでの値', 'in this project': 'このプロジェクトの値', '(no such driver)': '(該当するドライバなし)',
  'The target has the same files as this project.': 'ターゲットのファイルは、このプロジェクトと同じです。',
  'The target has the same files as this project (as last saved).': 'ターゲットのファイルは、このプロジェクト（最後に保存した状態）と同じです。',
  'Unsaved changes are not part of this comparison (Deploy saves first).': '保存していない変更は、この比較に含まれていません（デプロイは先に保存します）。',
  'Waiting for the answer on the target …': 'ターゲットでの応答を待っています …',
  'The target already has this key.': 'この鍵はすでにターゲットに登録されています。', 'The key was accepted on the target.': '鍵がターゲットで承認されました。',
});

I18N.PATTERNS_JA = [
  [/^(\d+) tags$/, '$1 個のタグ'],
  [/^save failed: (.*)$/s, '保存に失敗しました: $1'], [/^saved (.*)$/, '保存しました: $1'], [/^open failed: (.*)$/s, '開けませんでした: $1'],
  [/^parse error: (.*)$/s, '読み込みエラー: $1'], [/^run failed: (.*)$/s, '実行に失敗しました: $1'],
  [/^cannot save the local settings: (.*)$/s, 'ローカル設定を保存できません: $1'],
  [/^Delete screen "(.*)"\?$/, '画面「$1」を削除しますか？'], [/^Delete script "(.*)"\?$/, 'スクリプト「$1」を削除しますか？'],
  [/^Preview: (.*)$/, 'プレビュー: $1'], [/^Screen JSON: (.*)$/, '画面の JSON: $1'], [/^Preview failed: (.*)$/s, 'プレビューに失敗しました: $1'],
  [/^This folder already contains (\d+) other item\(s\): (.*)$/s, 'このフォルダーには、ほかに $1 個の項目があります: $2'],
  [/^On the target \((.*)\)$/, 'ターゲット ($1)'], [/^Cannot ask the target: (.*)$/s, 'ターゲットに問い合わせできません: $1'],
  [/^Checking (.*) …$/, '$1 を確認しています …'], [/^Deploying to (.*) …$/, '$1 へデプロイしています …'],
  [/^Deployed, but: (.*)$/s, 'デプロイしましたが、注意があります: $1'], [/^Deploy failed: (.*)$/s, 'デプロイに失敗しました: $1'],
  [/^Check failed: (.*)$/s, '確認に失敗しました: $1'], [/^Register key: (.*)$/s, '鍵の登録: $1'],
  [/^This project has not been deployed to (.*) yet\.$/, 'このプロジェクトは、まだ $1 にデプロイされていません。'],
  [/^The target is running another project: (.*)\. This deploy switches it to this project\.$/, 'ターゲットは別のプロジェクトを表示しています: $1。このデプロイで、このプロジェクトに切り替わります。'],
  [/^The target is running another project: (.*)\. "run on target" is not ticked: it will keep running that one\.$/, 'ターゲットは別のプロジェクトを表示しています: $1。「ターゲットで表示」にチェックがないので、そのままになります。'],
  [/^Also (\d+) setting\(s\) saved on the target with the same value as this project\.$/, 'ほかに $1 項目が、このプロジェクトと同じ値でターゲットに保存されています。'],
  [/^(\d+) setting\(s\) saved on the target with the same value as this project: nothing differs, but they would not follow a change made here\.$/, '$1 項目が、このプロジェクトと同じ値でターゲットに保存されています。今は差がありませんが、ここで値を変えてもターゲットには反映されません。'],
  [/^Changed \((\d+)\):$/, '変更あり ($1):'], [/^New \((\d+)\):$/, '新規 ($1):'], [/^Only on the target, not removed by a deploy \((\d+)\):$/, 'ターゲットにだけあるファイル（デプロイでは消えません） ($1):'],
  [/^… (\d+) more$/, '… ほか $1 件'],
  [/^The (\d+) user\(s\) edited on the target will be removed by this deploy; the project's users take effect\.$/, 'ターゲットで編集された $1 人のユーザーは、このデプロイで削除され、プロジェクトのユーザーが有効になります。'],
  [/^Users were edited on the target \((\d+) user\(s\)\): they stay in effect after a deploy, the project's user list is not used there\.$/, 'ターゲットでユーザーが編集されています（$1 人）。デプロイしてもそちらが優先され、プロジェクトのユーザー一覧は使われません。'],
  [/^A new key was created: (.*)$/, '新しい鍵を作りました: $1'],
  [/^Sending the key to (.*) \(port (\d+)\)\. On the target's screen open System > Keys, turn Key management on, and accept the key if it shows this fingerprint:$/, '$1（ポート $2）へ鍵を送ります。ターゲットの画面で System > Keys を開き、Key management をオンにして、次のフィンガープリントが表示されたら Accept を押してください:'],
  [/^Project → (.*)$/, 'プロジェクト → $1'],
  [/^database file, relative to project\.json \(default (.*)\)$/, 'データベースファイル（project.json からの相対パス。既定 $1）'],
  [/^oldest rows are deleted above this \(default (\d+), 0 = unlimited\)$/, 'これを超えたら古い行から削除（既定 $1、0 = 無制限）'],
  [/^"(.*)" has no PIN \(4 to 8 digits\)$/, '「$1」に PIN がありません（4〜8 桁の数字）'], [/^"(.*)" is defined twice$/, '「$1」が 2 回定義されています'],
  [/^"(.*)": a name is 1 to 16 letters, digits, _ - \. or spaces$/, '「$1」: 名前は 1〜16 文字の英数字、_ - . 、空白です'],
  [/^"(.*)": the level must be 1 to (\d+)$/, '「$1」: レベルは 1〜$2 です'],
  [/^no user has level (\d+): nobody can open the system settings on the device$/, 'レベル $1 のユーザーがいません。装置のシステム設定を誰も開けません'],
  [/^unknown tags: (.*)$/, '未定義のタグ: $1'],
];
I18N.TEXT_JA['no user is defined: nobody can log in'] = 'ユーザーが定義されていません。誰もログインできません';

// ---- several widgets, copy and paste, problems
Object.assign(I18N.TEXT_JA, {
  '{0} widgets selected': '{0} 個の部品を選択中', '{0} widget(s) copied': '{0} 個の部品をコピーしました', 'Align': '整列',
  'Align left edges': '左端を揃える', 'Align centers horizontally': '左右の中央を揃える', 'Align right edges': '右端を揃える',
  'Align top edges': '上端を揃える', 'Align centers vertically': '上下の中央を揃える', 'Align bottom edges': '下端を揃える',
  'Space out horizontally (equal gaps)': '左右に等間隔で並べる', 'Space out vertically (equal gaps)': '上下に等間隔で並べる',
  'Same width as the first selected': '最初に選んだ部品と同じ幅にする', 'Same height as the first selected': '最初に選んだ部品と同じ高さにする',
  "These widgets are placed by their panel's layout (flex); change the order in the tree instead.": 'これらの部品はパネルのレイアウト（flex）で配置されています。並び順はツリーで変えてください。',
  'Drag one of them to move all. Arrow keys move by 1 px (Shift: 10). Ctrl+C / Ctrl+V copy and paste, Ctrl+D duplicates, Del deletes. Ctrl+click adds or removes a widget.':
    'どれか 1 つをドラッグすると全部が動きます。矢印キーで 1 px（Shift で 10 px）移動。Ctrl+C / Ctrl+V でコピーと貼り付け、Ctrl+D で複製、Del で削除。Ctrl+クリックで部品を選択に追加 / 解除します。',
  'Problems ({0})': '問題 ({0})', '{0} problem(s)': '問題 {0} 件', 'no problems': '問題なし',
  'Show what will not work on the device': '装置で動かない箇所を一覧表示します',
  'a tag has no name': '名前のないタグがあります',
  'a level is set, but user management is off (Users tab)': '必要レベルが設定されていますが、ユーザー管理がオフです（ユーザー タブ）',
});
I18N.PATTERNS_JA.push(
  [/^tag "(.*)" is defined twice$/, 'タグ「$1」が 2 回定義されています'], [/^screen "(.*)" is defined twice$/, '画面「$1」が 2 つあります'],
  [/^start screen "(.*)" does not exist$/, '開始画面「$1」がありません'],
  [/^unknown tag "(.*)" \((.*)\)$/, '未定義のタグ「$1」($2)'], [/^unknown tag "(.*)"$/, '未定義のタグ「$1」'],
  [/^unknown screen "(.*)" \(goto\)$/, '存在しない画面「$1」(goto)'], [/^unknown script "(.*)"$/, '存在しないスクリプト「$1」'],
  [/^action "(.*)" needs user management \(Users tab\)$/, '動作 "$1" にはユーザー管理が必要です（ユーザー タブ）'],
  [/^(.*): no serial port \(device\) is set$/, '$1: シリアルポートが設定されていません'],
  [/^script "(.*)": unknown tag "(.*)"$/, 'スクリプト「$1」: 未定義のタグ「$2」'], [/^script "(.*)": unknown screen "(.*)"$/, 'スクリプト「$1」: 存在しない画面「$2」'],
  [/^(.*): unknown tag "(.*)" \(status_tag\)$/, '$1: 未定義のタグ「$2」(接続状態タグ)'], [/^(.*): unknown tag "(.*)"$/, '$1: 未定義のタグ「$2」'],
);

// ---- values of choices, shown as "中央 (center)"
Object.assign(I18N.VALUES_JA, {
  top_left: '左上', top_mid: '上中央', top_right: '右上', left_mid: '左中央', center: '中央', right_mid: '右中央',
  bottom_left: '左下', bottom_mid: '下中央', bottom_right: '右下',
  row: '横に並べる', column: '縦に並べる', row_wrap: '横 (折り返し)', column_wrap: '縦 (折り返し)', row_reverse: '横 (逆順)', column_reverse: '縦 (逆順)',
  start: '先頭', end: '末尾', space_between: '両端揃え', space_around: '均等 (両端は半分)', space_evenly: '均等',
  disable: '無効にする', hide: '隠す', login: 'ログイン画面を出す',
  wrap: '折り返す', dots: '… で省略', scroll: 'スクロール', circular: '循環', clip: '切り捨て', shift: 'ずらす',
  off: 'なし', on: '常に表示', auto: '自動', active: '操作中だけ',
  line: '折れ線', bar: '棒', scatter: '散布図',
  none: 'なし', even: '偶数', odd: '奇数', rw: '読み書き', ro: '読み取りのみ', wo: '書き込みのみ',
  holding: '保持レジスタ', input: '入力レジスタ', coil: 'コイル', discrete: '入力ステータス',
  manual: '手動', startup: '起動時', interval: '周期', tag: 'タグの変化', screen_show: '画面を表示したとき', screen_hide: '画面を閉じたとき',
  on_click: 'クリック', on_press: '押したとき', on_release: '離したとき', on_long_press: '長押し', on_long_press_repeat: '長押し (繰り返し)',
  on_change: '値が変わったとき', on_ready: '入力確定',
  left: '左', right: '右', hor: '横', ver: '縦',
});
I18N.ACTIONS_JA = { set: '値を書き込む', toggle: '反転する', inc: '増やす', dec: '減らす', goto: '画面を切り替える', back: '前の画面に戻る', script: 'スクリプトを実行',
  call: '機能を呼び出す', restart: 'ランタイムを再起動', system: 'システム設定を開く', login: 'ログイン', logout: 'ログアウト', lua: 'Lua コードを実行' };
I18N.action = function (k) { return this.lang === 'ja' && this.ACTIONS_JA[k] ? this.ACTIONS_JA[k] + '  (' + k + ')' : k; };
Object.assign(I18N.TEXT_JA, {
  'Click to add to the selected panel (or the screen), or drag onto the canvas.': 'クリックすると選択中のパネル（または画面）に追加します。キャンバスへドラッグして置くこともできます。',
  'Recent projects': '最近使ったプロジェクト', 'No project was opened yet.': 'まだプロジェクトを開いていません。',
});

Object.assign(I18N.TEXT_JA, {
  'Keyboard shortcuts': 'キーボード操作', 'Keyboard shortcuts (F1)': 'キーボード操作の一覧 (F1)',
  'New / open / save (Ctrl+Shift+S: save as)': '新規 / 開く / 保存（Ctrl+Shift+S: 別名で保存）', 'Undo / redo': '元に戻す / やり直し',
  'Screen editor, tags, drivers, scripts, logs, users': '画面の編集・タグ・ドライバ・スクリプト・ログ・ユーザー',
  'Add a widget to the selection, or take it out': '部品を選択に追加 / 選択から外す', 'Select everything in the same panel': '同じパネル内の部品をすべて選択',
  'Copy / cut / paste widgets (also onto another screen)': '部品のコピー / 切り取り / 貼り付け（別の画面へも可）', 'Duplicate': '複製',
  'Move by 1 px (with Shift: 10 px)': '1 px 移動（Shift で 10 px）', 'Move without snapping to other widgets': 'ほかの部品に吸着させずに移動',
  'Clear the selection / close a dialog': '選択を解除 / ダイアログを閉じる', 'This list': 'この一覧',
});

// ---- screen tabs, context menu, tag uses
Object.assign(I18N.KEYS_JA, { used: '使用箇所', lua: 'Lua コード', bind: '連動', data_log: '変数ロギング', alarms: 'アラーム', items: '項目' });
Object.assign(I18N.TEXT_JA, {
  'Close the tab (the screen stays in the project)': 'タブを閉じる（画面はプロジェクトに残ります）', 'Open another screen in a tab': '別の画面をタブで開く',
  'Cut': '切り取り', 'Copy': 'コピー', 'Paste': '貼り付け', 'Bring to front': '最前面へ', 'Send to back': '最背面へ', 'Select all in this panel': 'このパネル内をすべて選択',
  'Show where this tag is used': 'このタグの使用箇所を表示', 'This tag is not used anywhere': 'このタグはどこでも使われていません',
  'Tag "{0}" is used in {1} place(s). Delete it anyway?': 'タグ「{0}」は {1} か所で使われています。削除しますか？',
  '{0} place(s) that use the tag were changed with it': 'このタグを使っている {0} か所も新しい名前に変更しました',
  'Tag "{0}" is used here ({1})': 'タグ「{0}」の使用箇所 ({1})',
  'Next / previous screen tab': '次 / 前の画面タブ', 'Drag on the background': '背景をドラッグ', 'Select the widgets inside the rectangle': '囲んだ部品を選択',
  'Right click': '右クリック', 'Menu: copy, paste, order …': 'メニュー（コピー、貼り付け、重なり順 …）',
  'No tags yet. Tags are the named values that widgets bind to and drivers read / write.': 'タグがまだありません。タグは名前の付いた値で、部品が連動し、ドライバが読み書きします。',
});
I18N.PATTERNS_JA.push([/^duplicate tag names: (.*)$/, '重複しているタグ名: $1']);

// ---- style copy, lock / hide, tag import, recovery
Object.assign(I18N.TEXT_JA, {
  'Copy style': 'スタイルをコピー', 'Paste style': 'スタイルを貼り付け', 'Style copied': 'スタイルをコピーしました',
  'Copy / paste the style (look) of a widget': '部品のスタイル（見た目）をコピー / 貼り付け',
  'Lock (cannot be clicked on the canvas)': 'ロック（キャンバスで選択・移動できなくする）', 'Unlock': 'ロックを解除',
  'Hide on the canvas (designer only)': 'キャンバスで隠す（デザイナー上だけ）', 'Show on the canvas': 'キャンバスに表示する',
  'Show all / unlock all': 'すべて表示 / すべてロック解除', 'Locked on the canvas': 'キャンバスでロック中',
  'Hidden on the canvas (designer only)': 'キャンバスで非表示（デザイナー上だけ。装置では表示されます）',
  'Import / export …': '取り込み / 書き出し …', 'Tags: import / export': 'タグの取り込み / 書き出し', 'Import': '取り込む',
  'One tag per line: name, type, init, min, max. Copy this into a spreadsheet, or paste lines from one here (commas or tabs) and press Import. A tag that already exists is updated; nothing is deleted.':
    '1 行に 1 タグ: 名前, 型, 初期値, 最小値, 最大値。これを表計算ソフトへコピーするか、表計算ソフトの行をここへ貼り付けて（カンマ区切りでもタブ区切りでも可）「取り込む」を押してください。すでにあるタグは更新され、削除されるタグはありません。',
  'Nothing was imported:': '取り込みませんでした:',
  '{0} tag(s) added, {1} updated': 'タグを {0} 個追加、{1} 個更新しました',
  'Changes that were not saved were found ({0}). Restore them?': '保存されていない変更が見つかりました（{0}）。復元しますか？',
});

// ---- recipes
Object.assign(I18N.KEYS_JA, { book: 'レシピブック', verify_ms: '書き込み後の確認 (ms)', items: '項目', row_height: '行の高さ' });
Object.assign(I18N.TEXT_JA, {
  'Recipes': 'レシピ', 'Recipe table': 'レシピ表', 'Recipe book': 'レシピブック', '+ Add recipe book': '+ レシピブックを追加', '+ Add recipe': '+ レシピを追加',
  'No recipe books yet.': 'レシピブックはまだありません。',
  'Recipes: sets of values written to the PLC in one go (Ctrl+7)': 'レシピ: まとめて PLC に書き込む値の組 (Ctrl+7)',
  'A recipe book is a list of tags (items) and named sets of values for them. On the device, the "Recipe table" widget shows a book: pick a recipe, compare it with the current values, write it to the PLC, save the current values as a recipe. Values are checked against the tags\' min / max before anything is written.':
    'レシピブックは、タグ（項目）の一覧と、それぞれの値の組（レシピ）に名前を付けたものです。装置では部品「レシピ表」で表示し、レシピを選んで現在値と比べ、PLC へ書き込み、現在値をレシピとして保存できます。書き込む前に、すべての値をタグの最小値・最大値で確かめます（1 つでも外れていれば何も書きません）。',
  'user level needed to write, save or delete recipes on the device (0 / empty = everybody)': '装置でレシピを書き込む・保存する・削除するのに必要なレベル（0 / 空欄 = 誰でも）',
  'after a write: compare the tags with what was written after this many ms (default 2000, 0 = do not check)': '書き込んだあと、この時間 (ms) が経ったらタグの値と書いた値を比べる（既定 2000、0 = 比べない）',
  'the tags of this book, comma separated': 'このブックのタグ（カンマ区切り）',
  'One recipe per line: name, then one value per item. Copy this into a spreadsheet, or paste lines from one here (commas or tabs) and press Import: the recipes of the book are replaced. Cells that a spreadsheet would read as a formula get a leading apostrophe.':
    '1 行に 1 レシピ: 名前、続けて項目ごとの値。表計算ソフトへコピーするか、表計算ソフトの行をここへ貼り付けて（カンマ区切りでもタブ区切りでも可）「取り込む」を押すと、このブックのレシピが置き換わります。表計算ソフトが数式と読むセルには、先頭にアポストロフィを付けます。',
  '{0} recipe(s) imported': '{0} 個のレシピを取り込みました',
  'Screen editor, tags, drivers, scripts, logs, users, recipes': '画面の編集・タグ・ドライバ・スクリプト・ログ・ユーザー・レシピ',
  'recipes: a level is set, but user management is off (Users tab)': 'レシピ: 必要レベルが設定されていますが、ユーザー管理がオフです（ユーザー タブ）',
});
I18N.PATTERNS_JA.push(
  [/^Delete recipe book "(.*)"\?$/, 'レシピブック「$1」を削除しますか？'], [/^Recipes "(.*)": import \/ export$/, 'レシピ「$1」の取り込み / 書き出し'],
  [/^recipe table: no recipe book "(.*)"$/, 'レシピ表: レシピブック「$1」がありません'],
  [/^recipe "(.*)": (.*) = (.*) is outside (.*)$/, 'レシピ「$1」: $2 = $3 は範囲 $4 の外です'],
  [/^recipe "(.*)": (.*) is not a number$/, 'レシピ「$1」: $2 が数値ではありません'],
  [/^recipe book "(.*)": recipe "(.*)" is defined twice$/, 'レシピブック「$1」: レシピ「$2」が 2 回定義されています'],
  [/^recipe book "(.*)": a recipe name is 1 to 32 characters$/, 'レシピブック「$1」: レシピ名は 1〜32 文字です'],
  [/^recipe book "(.*)" is defined twice$/, 'レシピブック「$1」が 2 回定義されています'],
);
// ---- FINS driver
Object.assign(I18N.KEYS_JA, { dest_network: '相手先ネットワークアドレス', dest_node: '相手先ノードアドレス', dest_unit: '相手先号機アドレス', node: '自分のノードアドレス' });
Object.assign(I18N.TEXT_JA, {
  'OMRON FINS (FINS/TCP)': 'オムロン FINS (FINS/TCP)',
  'OMRON FINS (FINS/TCP) (fins)': 'オムロン FINS (FINS/TCP) (fins)',
  'the FINS/TCP port of the CPU or of the Ethernet unit (default 9600)': 'CPU または Ethernet ユニットの FINS/TCP のポート番号（既定 9600）',
  'route: destination network address (default 0 = the local network)': '経路: 相手先ネットワークアドレス（既定 0 = 自分のネットワーク）',
  'route: destination node address (default 0 = the PLC the connection goes to)': '経路: 相手先ノードアドレス（既定 0 = 接続した PLC）',
  'route: destination unit address (default 0 = the CPU)': '経路: 相手先号機アドレス（既定 0 = CPU ユニット）',
  'our own node address (default 0 = the PLC gives us one)': '自分のノードアドレス（既定 0 = PLC が割り当てる）',
  'only these may be written, e.g. D0-D999, W0-W99 (empty = everything except the Auxiliary Area)': 'ここに書いた範囲だけ書き込める。例: D0-D999, W0-W99（空欄 = 特殊補助リレー以外すべて）',
  "also write the Auxiliary Area (A: the CPU's own flags and settings)": '特殊補助リレー（A: CPU 自身のフラグと設定）にも書き込む',
  'D100 / CIO10 / W0.00 / H5 / E0_100 / T1': 'D100 / CIO10 / W0.00 / H5 / E0_100 / T1',
});

// ---- SLMP driver
Object.assign(I18N.KEYS_JA, { frame: 'フレーム', network: 'ネットワーク番号', pc: 'PC 番号', io: '要求先ユニット I/O 番号', station: '要求先局番',
  write_ranges: '書き込みを許す範囲', allow_system_write: 'SM / SD にも書き込む', length: '文字数' });
Object.assign(I18N.TEXT_JA, {
  'Mitsubishi SLMP / MC protocol (MELSEC)': '三菱電機 SLMP / MC プロトコル (MELSEC)',
  'Mitsubishi SLMP / MC protocol (MELSEC) (slmp)': '三菱電機 SLMP / MC プロトコル (MELSEC) (slmp)',
  'Device map': 'デバイスの割り付け',
  'the SLMP port of the CPU or of the Ethernet unit (Karakuri: 5007)': 'CPU または Ethernet ユニットの SLMP のポート番号（Karakuri は 5007）',
  'how long to wait for an answer (default 1000)': '応答を待つ時間（既定 1000）',
  'route: network number (default 0 = the station itself)': '経路: ネットワーク番号（既定 0 = 接続した局そのもの）',
  'route: PC number (default 255)': '経路: PC 番号（既定 255）',
  'route: request destination module I/O number (default 1023 = 0x3FF, the CPU)': '経路: 要求先ユニット I/O 番号（既定 1023 = 0x3FF、CPU）',
  'route: request destination station number (default 0)': '経路: 要求先局番（既定 0）',
  'only these may be written, e.g. D0-D999, M0-M99 (empty = everything except SM / SD)': 'ここに書いた範囲だけ書き込める。例: D0-D999, M0-M99（空欄 = SM / SD 以外すべて）',
  "also write SM / SD (the CPU's own relays and registers)": 'SM / SD（CPU 自身の特殊リレー・特殊レジスタ）にも書き込む',
  'D100 / M0 / X1F / D10.3': 'D100 / M0 / X1F / D10.3',
});
I18N.PATTERNS_JA.push([/^(.*): "(.*)" is not a device address$/, '$1: 「$2」はデバイスの書き方ではありません'], [/^(.*): no host is set$/, '$1: ホストが設定されていません']);
// ---- OPC UA client driver
Object.assign(I18N.KEYS_JA, { endpoint: 'エンドポイント', security: 'セキュリティ', policy: 'セキュリティポリシー', server_cert: 'サーバー証明書',
  application_uri: 'アプリケーション URI', publish_ms: '更新の間隔 (ms)', node: 'ノード' });
Object.assign(I18N.VALUES_JA, { sign_encrypt: '署名と暗号化', sign: '署名のみ' });
Object.assign(I18N.TEXT_JA, {
  'OPC UA client': 'OPC UA クライアント',
  'sign_encrypt (default)': '署名と暗号化（既定）',
  'OPC UA client (opcua_client)': 'OPC UA クライアント (opcua_client)',
  'Nodes': 'ノード',
  'opc.tcp://host:4840 of the server': 'サーバーの opc.tcp://ホスト:4840',
  'none: values can be read and changed on the network': 'none: 値がネットワーク上で読めて、書き換えられます',
  '(the best both have)': '（両方が使える最も強いもの）',
  'e.g. Basic256Sha256, Aes128_Sha256_RsaOaep': '例: Basic256Sha256, Aes128_Sha256_RsaOaep',
  "the server's certificate (DER / PEM) to trust, relative to project.json; more can be put in pki/trusted/ on the device": '信頼するサーバーの証明書（DER / PEM、project.json からの相対パス）。装置の pki/trusted/ にも置けます',
  "check the server's certificate. Without it anybody on the way can pretend to be the server": 'サーバーの証明書を確認する。切ると途中の誰でもサーバーになりすませます',
  "this client's certificate (DER). Empty: the device makes one at the first start (pki/own/opcua_client.der); the server has to trust it": 'このクライアントの証明書（DER）。空欄なら装置が初回に作ります（pki/own/opcua_client.der）。サーバー側で信頼させてください',
  'must be the URI in the client certificate': 'クライアント証明書の URI と同じにする',
  'how often the server sends changes (default 500)': 'サーバーが変化を送る間隔（既定 500）',
  'connect (default 5000)': '接続の時間制限（既定 5000）',
  'ns=2;s=Temp / nsu=urn:...;i=5': 'ns=2;s=Temp / nsu=urn:...;i=5',
});
I18N.PATTERNS_JA.push(
  [/^(.*): the endpoint is not opc.tcp:\/\/host:port$/, '$1: エンドポイントが opc.tcp://ホスト:ポート の形ではありません'],
  [/^(.*): no security: values can be read and changed on the network$/, '$1: セキュリティなし: 値がネットワーク上で読めて、書き換えられます'],
  [/^(.*): the server's certificate is not checked \(verify off\)$/, '$1: サーバーの証明書を確認していません（verify がオフ）'],
  [/^(.*): no server certificate to trust \(server_cert\); put one in pki\/trusted\/ on the device, or it does not connect$/, '$1: 信頼するサーバー証明書（server_cert）がありません。装置の pki/trusted/ に置かないと接続しません'],
  [/^(.*): cert_file and key_file go together$/, '$1: cert_file と key_file は両方とも指定します'],
);
// ---- MQTT driver
Object.assign(I18N.KEYS_JA, { tls: 'TLS（暗号化）', verify: '証明書を確認', ca_file: 'CA 証明書', cert_file: 'クライアント証明書', key_file: '秘密鍵',
  client_id: 'クライアント ID', topic_prefix: 'トピックの前置き', status_topic: '状態トピック', payload: '送る形式', qos: 'QoS', retain: '保持 (retain)',
  keepalive_s: 'キープアライブ (秒)', reconnect_ms: '再接続の待ち (ms)', topic: 'トピック', publish: '送信', subscribe: '受信' });
Object.assign(I18N.VALUES_JA, { change: '変化したとき' });
Object.assign(I18N.TEXT_JA, {
  'MQTT client': 'MQTT クライアント',
  'MQTT client (mqtt)': 'MQTT クライアント (mqtt)',
  'Publish (tag values to the broker)': '送信（タグの値をブローカーへ）',
  'Subscribe (commands: only these tags can be written)': '受信（指令: ここにあるタグだけ書き込まれる）',
  'the broker': 'ブローカー',
  'default 8883 (TLS), 1883 without TLS': '既定 8883（TLS）、TLS なしは 1883',
  'encrypted (TLS 1.2 / 1.3). Without it user name, password and values can be read on the network': '暗号化する（TLS 1.2 / 1.3）。切るとユーザー名・パスワード・値がネットワーク上で読めます',
  "check the broker's certificate and host name. Without it anybody on the way can pretend to be the broker": 'ブローカーの証明書とホスト名を確認する。切ると途中の誰でもブローカーになりすませます',
  "the CA certificate of the broker (PEM), relative to project.json. On Linux the system's CAs when empty": 'ブローカーの CA 証明書（PEM、project.json からの相対パス）。Linux では空欄ならシステムの CA',
  'client certificate (PEM), if the broker wants one': 'クライアント証明書（PEM）。ブローカーが求める場合',
  'its private key (PEM)': 'その秘密鍵（PEM）',
  'unique per device': '装置ごとに別の値',
  'put before every topic, e.g. plant/line1/pump1/': 'すべてのトピックの前に付く。例: plant/line1/pump1/',
  '"online" (retained) while connected, "offline" as the last will': '接続中は "online"（保持）、切れたら遺言 (last will) で "offline"',
  'value: 21.5 / json: {"v": 21.5, "ts": ms}': 'value: 21.5 / json: {"v": 21.5, "ts": ミリ秒}',
  'default of the published tags': '送信するタグの既定',
  'connect / handshake (default 5000)': '接続とハンドシェイクの時間制限（既定 5000）',
  'first wait before connecting again; doubles up to 30 s (default 1000)': '再接続までの最初の待ち。30 秒まで倍に延びる（既定 1000）',
  '(the tag name)': '（タグ名）',
});
I18N.PATTERNS_JA.push(
  [/^(.*): no TLS: user name, password and values can be read on the network$/, '$1: TLS なし: ユーザー名・パスワード・値がネットワーク上で読めます'],
  [/^(.*): the broker's certificate is not checked \(verify off\)$/, '$1: ブローカーの証明書を確認していません（verify がオフ）'],
  [/^(.*): the password is in project.json; better enter it on the device \(System > Driver\)$/, '$1: パスワードが project.json にあります。装置で入力してください（システム > ドライバ）'],
  [/^(.*): "(.*)": wildcards \(\+ #\) are not allowed$/, '$1: 「$2」: ワイルドカード（+ #）は使えません'],
);
// ---- alarm widgets
Object.assign(I18N.KEYS_JA, { rows: '行数', row_height: '行の高さ', columns: '表示する列', ack_on_tap: 'タップで確認', severity: '重要度', days: '期間 (日)',
  blink: '点滅', ack: '確認が必要', ack_level: '確認に必要なレベル', labels: '表示文字' });
Object.assign(I18N.VALUES_JA, { current: '発生中のアラーム', history: '履歴' });
Object.assign(I18N.TEXT_JA, {
  'Alarm list': 'アラーム一覧', 'Alarm banner': 'アラームバナー',
  'user level needed to acknowledge an alarm on the device (0 / empty = everybody; needs user management)': '装置でアラームを確認 (ACK) するのに必要なユーザーレベル（0 / 空欄 = 誰でも。ユーザー管理が必要）',
  'ack: the alarm stays in the alarm list until it is acknowledged (default). Show alarms on the screens with the "Alarm list" and "Alarm banner" widgets; tags sys.alarm.active, sys.alarm.unacked, sys.alarm.latest; actions call "alarm.ack" / "alarm.ack_all".':
    '確認が必要（既定）: 確認 (ACK) されるまでアラーム一覧に残ります。画面には部品「アラーム一覧」「アラームバナー」で表示します。タグ sys.alarm.active（発生中の数）・sys.alarm.unacked（未確認の数）・sys.alarm.latest（最新のメッセージ）、動作 call "alarm.ack" / "alarm.ack_all" も使えます。',
  'alarms: ack_level is set, but user management is off (Users tab)': 'アラーム: 確認に必要なレベルが設定されていますが、ユーザー管理がオフです（ユーザー タブ）',
});

// ---- trend
Object.assign(I18N.KEYS_JA, { group: 'ロギンググループ', range_s: '表示する期間 (秒)', buckets: '横の点数', refresh_ms: '更新周期 (ms)' });
Object.assign(I18N.TEXT_JA, { 'Trend': 'トレンド' });
Object.assign(I18N.TEXT_JA, { 'Log view': 'ログ表示', 'Meter': 'メーター', 'Date and time input': '日付・時刻の入力', 'Table': '表' });
Object.assign(I18N.KEYS_JA, { header: '見出しの行を出す' });
Object.assign(I18N.TEXT_JA, { 'XY chart': 'XY グラフ', 'Alarm summary': 'アラーム集計', 'the texts of the widget': '部品の中の文字' });
Object.assign(I18N.KEYS_JA, { sort: '並べ方', bars: '棒を出す' });
Object.assign(I18N.KEYS_JA, { schedules: 'スケジュール', at: '時刻', weekdays: '曜日', do: '動作' });
Object.assign(I18N.TEXT_JA, { 'Replace': '置換', 'Find and replace tag names and texts, on this screen or in the whole project': 'タグ名と文字を、この画面またはプロジェクト全体で置換する',
  'Find and replace': '検索と置換', 'Tag names': 'タグ名', 'Texts': '文字', 'This screen': 'この画面', 'All screens': 'すべての画面', 'The whole project': 'プロジェクト全体',
  'Find': '検索する文字', 'Replace with': '置き換える文字', 'Only the whole name / text': '全体が一致するものだけ', 'Match case': '大文字と小文字を区別する',
  'Make the tags the new names need (copies of the old ones)': '新しい名前のタグがなければ作る（元のタグの型と上下限をコピー）', 'Rename in the tag list too': 'タグ一覧の名前も変える',
  'Type what to find. The places that will change are listed here first.': '検索する文字を入れると、変わる所の一覧がここに出ます。', '{0} place(s) will change': '{0} か所が変わります', '... and {0} more': '… ほか {0} か所',
  'Nothing to replace.': '置換する所がありません。', '{0} place(s) changed': '{0} か所を変えました', '{0} tag(s) made': 'タグを {0} 個作りました', '{0} left as they were': '{0} か所はそのままです',
  'Replaced - but there are {0} new problem(s)': '置換しましたが、問題が {0} 件増えました', 'Undo the replacement': '置換を取り消す', 'Keep it': 'このままにする', 'Undone.': '取り消しました。',
  'duplicate': '複製', 'A copy of the screen was made. "Replace" changes its tags.': '画面を複製しました。タグは「置換」で変えられます。',
  'Convert to another size…': '別の大きさに変換…', 'Scale every screen for a display of another size': 'すべての画面を、別の大きさの表示器に合わせて拡大・縮小する',
  'Convert the project to another display size (now {0} \u00d7 {1})': 'プロジェクトを別の表示サイズに変換（今は {0} × {1}）', 'Convert': '変換',
  'Every screen is scaled: positions, sizes, spacings; fonts become the nearest size there is. Sizes in % stay. Look at the screens afterwards - texts may need another font. One step that can be undone.':
    'すべての画面の位置・大きさ・余白を拡大・縮小し、フォントはいちばん近い大きさにします。% の指定はそのままです。変換のあと、文字が収まっているか画面を見てください。元に戻す（Ctrl+Z）1 回で戻せます。',
  'Give another size (64 to 8192).': '今と違う大きさ（64〜8192）を入れてください。', '{0} screen(s) converted': '{0} 画面を変換しました' });
Object.assign(I18N.KEYS_JA, { transfers: 'データ転送', transfer_from: '転送元', transfer_to: '転送先', trigger: 'きっかけ', every_ms: '周期 (ms)', out_of_range: '範囲外の値', transfer_when: '条件' });
Object.assign(I18N.TEXT_JA, { 'Transfers': 'データ転送', '(on change)': '(変化したとき)', '(not written)': '(書かない)', 'values copied from one tag to another': 'タグの値を別のタグに写す',
  'the tag the value is taken from': '値を取るタグ', 'the tag the value is written to': '値を書くタグ',
  'on change: when the source changes. cyclic: also every period (sets the destination right again when it was changed elsewhere)': '変化したとき: 転送元が変わったとき。cyclic: 周期ごとにも書く（転送先がほかで書き換えられても元に戻す）',
  'cyclic: the period in ms (default 1000, at least 100)': 'cyclic のときの周期 ms（既定 1000、100 以上）', 'the value is multiplied by this (default 1)': '値にこれを掛ける（既定 1）', '... and this is added (default 0)': '…さらにこれを足す（既定 0）',
  'a value outside the limits of the destination tag: not written, or the nearest limit is written (clamp)': '転送先のタグの上下限を外れた値: 書かない、または近いほうの限界値を書く（clamp）',
  'only while this holds (optional): "run" or { "tag": "mode", "eq": 2 }': 'この条件が成り立つ間だけ転送する（省略可）: "run" または { "tag": "mode", "eq": 2 }',
  'A transfer copies the value of one tag to another: from one PLC to another, or to a higher system. Nothing is written while the driver of either tag has no connection; after the connection is back the transfer waits until the values were read again and goes on with the newest value. A value that is not a number or outside the limits of the destination tag is not written. Transfers that form a ring are not run. On the device: System > Transfers shows what runs and what waits, and why.':
    '転送は、あるタグの値を別のタグに写します（PLC から別の PLC へ、上位のシステムへ）。どちらかのドライバーが切断されている間は書きません。再接続のあとは、値を読み直してから最新の値で再開します。数値でない値、転送先のタグの上下限を外れた値は書きません。輪になった転送は実行しません。装置では System > Transfers で、動いているもの・待っているものとその理由を見られます。' });
Object.assign(I18N.KEYS_JA, { sound: '音を使う', output: '出力先', volume: '音量', touch: 'タッチ音', alarm_sound: 'アラーム音', alarm_every_s: 'アラーム音の間隔 (秒)', gpio: 'ブザーの GPIO', gpio_active_low: 'Low で鳴る', device: 'ALSA デバイス' });
Object.assign(I18N.TEXT_JA, { 'Sound': '音', '(auto)': '(自動)', 'a sound for a touch and for a new alarm': 'タッチしたときと、新しいアラームのときに音を出す',
  'audio: the sound device (Windows, ALSA). gpio: a buzzer on a GPIO line (Raspberry Pi). auto: gpio when a line is given, else audio': 'audio: 音声デバイス（Windows、ALSA）。gpio: GPIO につないだブザー（Raspberry Pi）。自動: GPIO の番号があれば gpio、なければ audio',
  '0 to 100 (default 80; a buzzer has no volume). Can be changed on the device: System > Sound': '0〜100（既定 80。ブザーに音量はありません）。装置の System > Sound で変えられます',
  'the sound of a touch: click, beep, alarm, a WAV file of the project (assets/tap.wav), or off': 'タッチ音: click、beep、alarm、プロジェクトの WAV ファイル（assets/tap.wav）、または off',
  'the sound of a new alarm: click, beep, alarm, a WAV file of the project, or off': '新しいアラームの音: click、beep、alarm、プロジェクトの WAV ファイル、または off',
  'the alarm sound starts again every N seconds until the alarms are acknowledged (default 3; 0 = once)': 'アラームが確認されるまで、N 秒ごとに鳴らし直す（既定 3。0 = 1 回だけ）',
  'the GPIO line of the buzzer (Raspberry Pi: the GPIO number, e.g. 18)': 'ブザーをつないだ GPIO（Raspberry Pi: GPIO 番号。例 18）', 'the buzzer sounds while the line is low': 'Low のときに鳴るブザー',
  'Linux: the ALSA device (default, plughw:1,0 ...)': 'Linux: ALSA のデバイス（default、plughw:1,0 など）',
  'A sound file is a WAV (PCM, 8 or 16 bit) inside the project folder. Tags: sys.sound.mute, sys.sound.volume, sys.sound.alarm (1 while the alarm sound is on; a button that writes 0 silences it until the next alarm). A button can play a sound: { "action": "call", "fn": "sound.play", "arg": "beep" }. A device without a sound output stays silent and works as before.':
    '音のファイルは、プロジェクトのフォルダーの中の WAV（PCM、8 / 16 ビット）です。タグ: sys.sound.mute、sys.sound.volume、sys.sound.alarm（アラーム音が鳴っている間 1。0 を書くボタンで、次のアラームまで止まります）。ボタンから鳴らす: { "action": "call", "fn": "sound.play", "arg": "beep" }。音の出口がない装置では鳴らないだけで、ほかは今までどおり動きます。' });
Object.assign(I18N.TEXT_JA, { 'Schedules': 'スケジュール', 'actions that run by the clock': '時刻で実行する動作', 'HH:MM, or YYYY-MM-DD HH:MM for once': 'HH:MM。1 回だけなら YYYY-MM-DD HH:MM',
  'mon, tue, wed, thu, fri, sat, sun (empty = every day)': 'mon, tue, wed, thu, fri, sat, sun（空なら毎日）', 'off: kept, not run': 'オフ: 残すが実行しない',
  'what it does: an action of a screen, or a list of them. { "action": "set", "tag": "run", "value": true }': '実行する内容: 画面のボタンと同じ動作 1 つ、またはその配列。{ "action": "set", "tag": "run", "value": true }',
  'A schedule runs the actions a button can run, at a time of day on the days named (or once, at a date). On the device the time, the days and on / off can be changed under System > Schedule; what it does cannot. "catch_up": true (in the project file) runs a time that passed while the device was off, once, when it starts again.':
    'スケジュールは、ボタンと同じ動作を、指定した曜日の時刻に（または日付を指定して 1 回だけ）実行します。装置では System > Schedule で時刻・曜日・オン / オフを変えられますが、実行する内容は変えられません。プロジェクトのファイルに "catch_up": true を書くと、装置が止まっている間に過ぎた時刻を、次の起動時に 1 回だけ実行します。' });
Object.assign(I18N.KEYS_JA, { zones: '色を付ける区間', widths: '列の幅', cells: 'セル', from: '開始', to: '終了' });
Object.assign(I18N.TEXT_JA, { 'Edit cells…': 'セルを編集…', '+ row': '+ 行', '+ column': '+ 列', '− column': '− 列', 'Table cells': '表のセル', 'Move up': '上へ', 'Remove': '削除', 'Remove this row': 'この行を削除',
  'This cell is kept as it is (edit it as JSON in the project file)': 'このセルはそのまま残します（プロジェクトのファイルで JSON として編集してください）',
  'A cell is a text, or a tag in braces: {count}  ·  with a format: {temp|%.1f C}  ·  with a text per value: {state|0=Stop,1=Run,*=?} (* = any other value). The titles and widths of the columns are set under "columns".':
    'セルには文字か、波かっこで囲んだタグを書きます: {count}  ·  書式つき: {temp|%.1f C}  ·  値ごとの文字: {state|0=停止,1=運転,*=?}（* はそれ以外の値）。列の見出しと幅は「表示する列」で設定します。',
  'more curves on the same axes (4 in all)': '同じ軸に重ねる曲線（全部で 4 本まで）', 'a title per column; width in pixels (empty: the columns share what is left)': '列ごとの見出し。幅はピクセル（空なら残りを分け合う）',
  'the cells: texts and tags': 'セルの中身（文字とタグ）', 'parts of the scale in a colour (a warning range, an alarm range)': '目盛りに色を付ける区間（警告の範囲、異常の範囲）',
  'the title of each column, and the text shown when there is no row (empty)': '列ごとの見出しと、行がないときの文字（empty）', 'the width of a column in pixels (empty: the columns share what is left)': '列の幅（ピクセル。空なら残りを分け合う）' });
Object.assign(I18N.KEYS_JA, { jitter: 'ブレの許容 (px)', smooth: '平均する回数', settle: '押し始めに捨てる回数', release: '離す直前に捨てる回数', lift_ms: '離したと見なすまでの時間 (ms)', scroll_limit: 'スクロール開始の距離 (px)', log: 'タッチをログに残す' });
Object.assign(I18N.KEYS_JA, { x_tag: '横軸のタグ', x_min: '横軸の最小', x_max: '横軸の最大', y_tag: '縦軸のタグ', y_min: '縦軸の最小', y_max: '縦軸の最大', clear_tag: '消去のタグ' });
Object.assign(I18N.KEYS_JA, { seconds: '秒も入力する', year_min: '年の下限', year_max: '年の上限' });
Object.assign(I18N.KEYS_JA, { ticks: '目盛りの数', major_every: '数字を付ける間隔', show_value: '値を数字で出す', needle_color: '針の色' });
Object.assign(I18N.KEYS_JA, { source: 'ログの種類', min_level: '出すレベル', filter: 'レベルの選択を出す', filter_tag: '絞り込みのタグ' });
I18N.PATTERNS_JA.push(
  [/^trend: no data log group "(.*)"$/, 'トレンド: ロギンググループ「$1」がありません'],
  [/^trend: data log group "(.*)" does not record "(.*)"$/, 'トレンド: ロギンググループ「$1」は「$2」を記録していません'],
);

// ---- popups
Object.assign(I18N.KEYS_JA, { popup: 'ポップアップとして使う', dim: '下の画面を暗くする' });
Object.assign(I18N.ACTIONS_JA, { popup: 'ポップアップを開く', close: 'ポップアップを閉じる' });
Object.assign(I18N.TEXT_JA, {
  'Popup': 'ポップアップ', '(the top one)': '(いちばん上のもの)', 'popup without a screen': '開く画面が指定されていないポップアップ',
  'shown over the current screen by the action "popup" (closed by "close", "back" or a screen change), never as a screen of its own':
    '動作「ポップアップを開く」で今の画面の上に表示します（「閉じる」「戻る」、画面の切り替えで閉じます）。画面としては表示しません',
  'darken the screen below (it cannot be touched either way)': '下の画面を暗くする（どちらでも下の画面には触れません）',
});
I18N.PATTERNS_JA.push(
  [/^screen "(.*)" is not a popup \((.*)\)$/, '画面「$1」はポップアップではありません ($2)'],
  [/^unknown screen "(.*)" \((popup|close)\)$/, '存在しない画面「$1」($2)'],
  [/^the start screen "(.*)" is a popup$/, '開始画面「$1」がポップアップになっています'],
);

// ---- texts / languages
Object.assign(I18N.KEYS_JA, { languages: '言語', language: '起動時の言語', lang: '言語', file: 'ファイル' });
Object.assign(I18N.ACTIONS_JA, { language: '表示言語を切り替える' });
Object.assign(I18N.TEXT_JA, {
  'Texts': '多言語テキスト', 'Font': 'フォント', 'Preview in': 'プレビューの言語', '+ Add text': '+ テキストを追加', 'No texts yet.': 'テキストはまだありません。',
  'Texts in several languages (Ctrl+8)': '多言語テキスト (Ctrl+8)', 'Text id (letters, digits, _ . -)': 'テキストの ID（英数字と _ . -）',
  'Write "@id" as the text of a widget (text, placeholder, options, labels) or as an alarm message: the device shows the text in its language. The language is the tag sys.lang; switch it with the action "language" (or a widget bound to sys.lang). Japanese, Chinese ... need a font that has them: Project tab > Font.':
    '部品のテキスト（text、placeholder、options、labels）やアラームのメッセージに「@ID」と書くと、装置はその言語のテキストを表示します。言語はタグ sys.lang で、動作「表示言語を切り替える」（または sys.lang に連動した部品）で切り替えます。日本語・中国語などには、その文字があるフォントが必要です（プロジェクト タブ > フォント）。',
  'language codes, the first is the default (and what a missing translation falls back to), e.g. ja, en': '言語コード。最初のものが既定です（訳がないときもこれを表示）。例: ja, en',
  'the language at start': '起動時の言語',
  'a TTF / OTF file in the project for every text (any size); empty = the built-in font (Latin letters only). tools/subset_font.py makes a small copy with the characters the project needs':
    'すべてのテキストに使う、プロジェクト内の TTF / OTF ファイル（どの大きさでも可）。空欄 = 組み込みフォント（ラテン文字のみ）。tools/subset_font.py で、必要な文字だけの小さなファイルを作れます',
  'Keep the license of the font next to it (Noto Sans JP: SIL Open Font License, OFL.txt).': 'フォントのライセンスはファイルの隣に置いてください（Noto Sans JP は SIL Open Font License。OFL.txt）。',
  'Texts: import / export': '多言語テキストの取り込み / 書き出し',
  'One text per line: id, then one column per language (the first line names them). Copy this into a spreadsheet for the translators, or paste their lines here (commas or tabs) and press Import: the texts are replaced, new languages are added.':
    '1 行に 1 テキスト: ID、続けて言語ごとの列（1 行目に言語コード）。表計算ソフトへコピーして翻訳者に渡し、戻ってきた行をここへ貼り付けて（カンマ区切りでもタブ区切りでも可）「取り込む」を押すと、テキストが置き換わります。新しい言語は追加されます。',
  '{0} text(s) imported': '{0} 個のテキストを取り込みました',
  'Screen editor, tags, drivers, scripts, logs, users, recipes, texts': '画面の編集・タグ・ドライバ・スクリプト・ログ・ユーザー・レシピ・多言語テキスト',
  'a language code of the project, or "next"': 'プロジェクトの言語コード、または "next"（次の言語）',
});
I18N.PATTERNS_JA.push(
  [/^text "(.*)" has no (.*)$/, 'テキスト「$1」に $2 の訳がありません'],
  [/^text "(.*)" in (.*) is much longer than in (.*): it may not fit$/, 'テキスト「$1」の $2 の訳は $3 よりかなり長く、収まらないかもしれません'],
  [/^no text "(.*)" \(shown as "(.*)"\)$/, 'テキスト「$1」がありません（「$2」と表示されます）'],
  [/^the start language "(.*)" is not one of the languages$/, '起動時の言語「$1」が言語の一覧にありません'],
);

// ---- screen saver
Object.assign(I18N.KEYS_JA, { after_s: '消灯までの時間 (秒)', wake_on_alarm: 'アラームで点灯', logout: '消灯時にログアウト' });
Object.assign(I18N.TEXT_JA, {
  'Screen saver': '画面消灯',
  'the screen goes dark after this many seconds without a touch (0 / empty = never). The touch that wakes it presses nothing. Can be changed on the device: System > Display':
    '操作がないままこの秒数が経つと画面を消します（0 / 空欄 = 消さない）。点灯させるタッチではボタンは押されません。装置でも変更できます（System > Display）',
  'a new alarm wakes the screen': '新しいアラームで画面を点ける', 'log the user out when the screen goes dark': '画面が消えたらログアウトする',
  'Raspberry Pi: the backlight is switched off where the display allows it (/sys/class/backlight), else the screen is only made black. Tag sys.screen.off: 1 while dark; write 1 to make it dark now.':
    'Raspberry Pi: 表示器が対応していればバックライトを消します（/sys/class/backlight）。対応していなければ黒い画面にするだけです。タグ sys.screen.off は消灯中 1。1 を書くとすぐに消えます。',
});
// ---- the font that comes with the designer
Object.assign(I18N.TEXT_JA, {
  'Japanese font': '日本語フォント',
  'put Noto Sans JP into the project (about 1.2 MB). Without a font Japanese text is shown as empty boxes': 'Noto Sans JP をプロジェクトに入れます（約 1.2 MB）。フォントがないと日本語は □ で表示されます',
  'Add the Japanese font (Noto Sans JP)': '日本語フォントを入れる（Noto Sans JP）',
  'copies it into assets/fonts/ of the project, with its license, and sets it here': 'プロジェクトの assets/fonts/ にライセンス文と一緒にコピーし、ここに設定します',
  'font added: {0}': 'フォントを入れました: {0}',
});
I18N.PATTERNS_JA.push([/^Japanese text is used \((.*)\), but the project has no font for it: it is shown as empty boxes\. Project tab > Font$/,
  '日本語の文字が使われていますが、プロジェクトにフォントがありません（□ で表示されます）。プロジェクト タブ > フォント']);
// ---- animation
Object.assign(I18N.KEYS_JA, { animate: 'アニメーション' });
Object.assign(I18N.TEXT_JA, {
  'Animation': 'アニメーション', 'add': '追加',
  'what moves by itself: { "prop": "opa" | "x" | "y" | "w" | "h" | "rotation" | "zoom" | "bg_color" | "text_color" | "frames", "from", "to", "time" (ms), "playback", "repeat", "path", "when": tag or condition }, or { "preset": "blink" | "pulse" | "spin" }. It runs while its screen is shown and "when" holds. Not played on the canvas: use Preview / Run':
    '自動で動かすもの: { "prop": "opa" | "x" | "y" | "w" | "h" | "rotation" | "zoom" | "bg_color" | "text_color" | "frames", "from", "to", "time"（ミリ秒）, "playback"（往復）, "repeat"（回数）, "path"（動き方）, "when": タグか条件 }、または { "preset": "blink"（点滅）| "pulse"（ゆっくり明滅）| "spin"（回転）}。その画面が表示されていて "when" が成り立つ間だけ動きます。キャンバスでは再生されません（Preview / Run で確かめます）',
});
I18N.PATTERNS_JA.push([/^animate: (.*)$/, 'アニメーション: $1']);
// ---- images
Object.assign(I18N.KEYS_JA, { fit: '大きさの合わせ方', bg_image: '背景画像', bg_image_opa: '背景画像の不透明度', bg_image_tiled: '背景画像を敷き詰める' });
Object.assign(I18N.VALUES_JA, { contain: '全体が入るように', stretch: '引き伸ばす', tile: '敷き詰める' });
Object.assign(I18N.TEXT_JA, {
  '(its own size)': '（元の大きさ）',
  'how the picture fills w / h: contain (whole picture, proportions kept), stretch, tile (repeated), center. PNG only: a JPEG / BMP is shown in its own size': '画像を w / h にどう合わせるか: contain（比率を保って全体が入る）、stretch（引き伸ばす）、tile（敷き詰める）、center（中央に元の大きさで）。PNG だけ（JPEG / BMP は元の大きさで表示されます）',
  'choose an image: PNG, JPEG, BMP or GIF (copied into the project)': '画像を選ぶ: PNG、JPEG、BMP、GIF（プロジェクトにコピーされます）',
});
// ---- AI assistant, changes made from outside
Object.assign(I18N.TEXT_JA, {
  'Have an AI assistant make screens: what to give it': 'AI に画面を作ってもらう: AI に渡すもの',
  'Make screens with an AI assistant': 'AI に画面を作ってもらう',
  'An AI assistant that can read and write files on this PC (Claude Code, the Claude desktop app ...) can make screens for this project. Give it the text below: it names the guide (skill) that comes with the designer, the project folder and the runtime it checks its work with. The runtime check opens no window and contacts no device.':
    'この PC のファイルを読み書きできる AI（Claude Code、Claude のデスクトップアプリなど）に、このプロジェクトの画面を作ってもらえます。下の文を AI に渡してください。デザイナーに付いている手引き（Skill）、プロジェクトのフォルダー、確認に使うランタイムの場所が入っています。ランタイムでの確認は、ウィンドウを出さず、機器にもつながりません。',
  'Copy': 'コピー', 'copied': 'コピーしました',
  'Open the skill folder': 'Skill のフォルダーを開く',
  'Install the skill for Claude Code': 'Claude Code に Skill を入れる',
  'installed in {0}': '{0} に入れました',
  'Save first: the assistant reads the files, not what is open here.': '先に保存してください。AI が読むのはファイルで、ここで開いている内容ではありません。',
  'While the assistant works, keep the designer open: when it changes the files, the designer loads them again (it asks first if there are unsaved changes here).':
    'AI が作業している間、デザイナーは開いたままで構いません。AI がファイルを変えると、デザイナーが読み直します（ここに保存していない変更があるときは、先に聞きます）。',
  'This project is one file (.hmlproj): the assistant works in its working folder shown above. Press Save afterwards to put its changes into the file.':
    'このプロジェクトは 1 つのファイル（.hmlproj）です。AI は上に出ている作業用フォルダーで作業します。終わったら「保存」を押すと、AI の変更がファイルに入ります。',
  'The project was changed outside the designer': 'プロジェクトがデザイナーの外で変更されました',
  'Its files were changed by another program, and there are changes here that are not saved. Loading the files discards the changes made here; keeping them means the next Save overwrites the files.':
    'ほかのプログラムがファイルを変更しましたが、ここにも保存していない変更があります。ファイルを読み込むと、ここでの変更は捨てられます。このまま続けると、次の保存でファイルが上書きされます。',
  'Keep my changes': 'ここでの変更を残す', 'Load the files': 'ファイルを読み込む',
  'The project was changed outside the designer: loaded again': 'プロジェクトがデザイナーの外で変更されたので、読み直しました',
});
I18N.PATTERNS_JA.push([/^The files were changed, but cannot be read: (.*)$/, 'ファイルが変更されましたが、読めません: $1'], [/^reload failed: (.*)$/, '読み直しに失敗しました: $1']);
// ---- log of the runtime started by Run
Object.assign(I18N.TEXT_JA, {
  'Runtime log': 'ランタイムのログ', 'Runtime': 'ランタイム', 'Stop': '停止',
  'running': '実行中', 'ended (exit code {0})': '終了しました（終了コード {0}）',
  'Close the runtime that Run started': '「実行」で起動したランタイムを終了します', 'Copy the log': 'ログをコピーします',
  'Hide the log (the runtime keeps running)': 'ログを隠します（ランタイムは動いたままです）',
  'Show or hide the log of the runtime': 'ランタイムのログを表示 / 非表示にします',
  'Copied.': 'コピーしました。', 'Could not copy.': 'コピーできませんでした。',
});
