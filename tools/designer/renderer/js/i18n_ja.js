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
