# HML Runtime — LVGL 宣言型 HMI ランタイム

JSON で書いた画面定義を LVGL (v9.4) で描画し、タグ（プロセス変数）とウィジェットを
双方向にバインドする HMI ランタイムです。PC 上では SDL2 ウィンドウで、Raspberry Pi などの
Linux ボードでは framebuffer / DRM + evdev（タッチパネル）で動作します。
データソースはドライバ方式で、シミュレーション、Modbus TCP クライアント、OPC UA サーバー
（タグを OPC UA 変数として公開）を同梱しています。
ランタイム本体は C++17 で書かれています（LVGL 本体は C ライブラリのまま利用）。

```
project.json ──┬── tags      : 名前付き変数（bool/int/float/string）
               ├── drivers   : sim / modbus_tcp がタグを読み書き
               └── screens   : screens/*.json → LVGL ウィジェットに展開
                                 bind でタグ⇔ウィジェットを接続、on_click 等でアクション
```

## ビルド（Windows / MSVC）

必要なもの: Visual Studio 2022 以降 (C++ ツール), CMake 3.24+, Ninja, ネットワーク接続
（初回に LVGL / SDL2 / nlohmann-json / open62541 をダウンロードします）。OPC UA サーバーを組み込むには
Python 3 も必要です（open62541 のコード生成に使用。不要なら `-DHML_OPCUA=OFF`）。

```
build.cmd            # Release ビルド → build\bin\hml_runtime.exe
build.cmd Debug
```

## ビルド（Raspberry Pi / Linux）

表示バックエンドは CMake の `-DHML_BACKEND=` で選びます（`build.sh` が引数で切り替えます）。

| バックエンド | 用途 | 追加パッケージ |
|---|---|---|
| `fbdev` | `/dev/fb0` + evdev。X11/Wayland 不要、コンソールから起動する Raspberry Pi 向け（**既定**） | なし |
| `drm` | `/dev/dri/card0` (KMS) + evdev。vc4-kms-v3d 環境でティアリングを避けたい場合 | `libdrm-dev` |
| `sdl` | デスクトップ Linux のウィンドウ表示（Windows と同じ） | `libsdl2-dev` |

### Raspberry Pi 上でビルドする（ネイティブ）

Raspberry Pi OS (Bookworm, 32/64 bit どちらでも) で:

```
sudo apt install build-essential cmake ninja-build git
./build.sh fbdev                # → build-fbdev/bin/hml_runtime
sudo apt install libdrm-dev && ./build.sh drm    # DRM 版を使う場合
```

Pi 4 で初回ビルドは数分です（LVGL / nlohmann-json / open62541 のダウンロードにネットワークが必要）。
コンパイラは g++ 10 以降（C++17）、OPC UA を組み込む場合は python3 も必要です（Raspberry Pi OS には標準で入っています）。

### PC (Linux / WSL) でクロスコンパイルする

```
sudo apt install gcc-aarch64-linux-gnu        # 64bit OS 用
sudo apt install gcc-arm-linux-gnueabihf      # 32bit OS 用
./build.sh fbdev --cross aarch64              # → build-fbdev-aarch64/bin/hml_runtime
./build.sh fbdev --cross armhf                # → build-fbdev-armhf/bin/hml_runtime
```

`fbdev` バックエンドは libc 以外に依存しないため、sysroot 無しでクロスコンパイルできます。
できたバイナリと `projects/` ディレクトリを Pi にコピーすれば動作します。
（`drm` / `sdl` をクロスビルドするには対象ライブラリ入りの sysroot が必要です。）

### Raspberry Pi で実行する

```
sudo ./build-fbdev/bin/hml_runtime projects/demo/project.json
```

- デスクトップ (X11/Wayland) が動いていると framebuffer を奪い合うので、`raspi-config` で
  「Console」起動にするか、`sudo systemctl stop lightdm` 等でデスクトップを止めてから起動してください。
- root 以外で動かすには `sudo usermod -aG video,input $USER` を実行して再ログインします。
- コンソールのカーソル点滅が画面に残る場合は `echo 0 | sudo tee /sys/class/graphics/fbcon/cursor_blink`。
- 画面サイズは framebuffer の解像度が使われます。`project.json` の `display.width/height` は
  画面 JSON の座標基準なので、Pi の解像度に合わせておくか `rotation` と合わせて調整してください。
- fbdev/drm ドライバはカーネルが返す物理サイズから DPI を計算しますが、HDMI ではでたらめな値
  （Pi 4 で 1280 px = 70 mm → 464 dpi）が返ることがあり、テーマの余白や角丸が数倍に膨らみます。
  ランタイムは `display.dpi`（既定 130）で必ず上書きするので、通常は何もしなくて大丈夫です。
- 入力は `/dev/input/event*` を自動検出します（タッチ / マウス / キーボード）。特定のデバイスだけ
  使う場合は `display.input` か環境変数 `HML_INPUT` で指定します。
- 終了は `Ctrl-C` (SIGINT/SIGTERM)、画面 JSON のホットリロードは `kill -HUP <pid>` または `kill -USR1 <pid>`。

Linux 用の `display` 設定:

```json
"display": {
  "width": 800, "height": 480,
  "rotation": 0,                       // 0 / 90 / 180 / 270
  "dpi": 130,                          // テーマの余白・角丸の基準（省略時 130 = Windows/SDL と同じ）
  "fbdev": "/dev/fb0",                 // fbdev バックエンドのデバイス（省略可）
  "drm":   "/dev/dri/card0",           // drm バックエンドのデバイス（省略可）
  "input": "",                         // 空 = 自動検出、例 "/dev/input/event0"
  "touch": { "swap_axes": false, "calibration": [0, 0, 4095, 4095] }   // 抵抗膜式などの補正（省略可）
}
```

環境変数 `HML_FBDEV` / `HML_DRM` / `HML_INPUT` を設定すると `project.json` より優先されます。

systemd で自動起動する例 (`/etc/systemd/system/hml.service`):

```ini
[Unit]
Description=HML Runtime
After=multi-user.target

[Service]
ExecStart=/home/pi/lvgl-hml/build-fbdev/bin/hml_runtime /home/pi/lvgl-hml/projects/demo/project.json
ExecReload=/bin/kill -HUP $MAINPID
Restart=always
User=root

[Install]
WantedBy=multi-user.target
```

`sudo systemctl enable --now hml` で起動し、`sudo systemctl reload hml` で画面 JSON を再読み込みできます。

## 実行

```
build\bin\hml_runtime.exe projects\demo\project.json
```

| キー / オプション | 動作 |
|---|---|
| `F5` | 画面 JSON をホットリロード（タグ・ドライバは維持、表示中の画面を保つ）。Linux fbdev/drm では `SIGHUP` / `SIGUSR1` |
| `Esc` | 終了。Linux fbdev/drm では `Ctrl-C` (`SIGINT` / `SIGTERM`) |
| `--screen <name>` | 起動時に表示する画面を指定 |
| `--screenshot <file.bmp>` | 起動後にスクリーンショットを保存（`tools/bmp2png.py` で PNG 化できます） |
| `--exit-after <ms>` | 指定時間後に自動終了（CI / 自動テスト用） |
| `--reload-after <ms>` | 指定時間後にホットリロードを実行（テスト用） |
| `--click <x>,<y>,<ms>` | 起動から `ms` 後に座標 (x,y) をクリック（複数指定可、UI 自動テスト用） |
| `--shot-at <ms>,<file.bmp>` | 指定時刻にスクリーンショットを保存（複数指定可） |

デモの Modbus 接続を試すには、同梱のテストサーバーを別ターミナルで起動してください。

```
python tools\modbus_server.py --port 5020
```

接続すると画面右上の PLC ランプが点灯し、Run スイッチ・Speed スライダー・Setpoint アークが
サーバーのコイル / 保持レジスタと同期します。サーバーが無い場合も画面は動作します（オフライン表示）。

## project.json

```json
{
  "title": "Pump Station",
  "display": { "width": 800, "height": 480, "zoom": 1.0 },
  "theme":   { "dark": true, "primary": "#2196f3", "secondary": "#ff9800", "font": 16 },
  "start_screen": "main",
  "tags": [
    { "name": "temp",  "type": "float", "init": 20.0 },
    { "name": "speed", "type": "int",   "init": 0, "min": 0, "max": 100 },
    { "name": "run",   "type": "bool",  "init": false },
    { "name": "state", "type": "string","init": "Idle" }
  ],
  "drivers": [ ... ],
  "screens": [ "screens/main.json", "screens/settings.json" ]
}
```

- `tags[].type`: `bool` / `int` / `float` / `string`。`min` / `max` を付けると書き込み時にクランプされます。
- `screens` はファイルパス（project.json からの相対）か、画面オブジェクトを直接書けます。

### ドライバ: sim（シミュレーション）

```json
{ "type": "sim", "items": [
  { "tag": "temp",  "mode": "sine",     "min": 18, "max": 32, "period_ms": 12000 },
  { "tag": "level", "mode": "ramp",     "min": 0,  "max": 100, "period_ms": 20000 },
  { "tag": "blink", "mode": "square",   "period_ms": 1000 },
  { "tag": "alarm", "mode": "counter",  "min": 0,  "max": 2,  "period_ms": 4000 },
  { "tag": "noise", "mode": "random",   "min": 0,  "max": 10, "period_ms": 500 },
  { "tag": "state", "mode": "cycle",    "values": ["Idle", "Run", "Alarm"], "period_ms": 3000 }
] }
```

`mode`: `sine` / `triangle` / `ramp` / `square` / `random` / `counter` / `cycle`

### ドライバ: modbus_tcp

```json
{ "type": "modbus_tcp", "host": "192.168.0.10", "port": 502, "unit_id": 1,
  "poll_ms": 200, "timeout_ms": 1000, "status_tag": "plc_online",
  "map": [
    { "tag": "speed", "area": "holding", "addr": 0 },
    { "tag": "temp",  "area": "input",   "addr": 10, "dtype": "s16", "scale": 0.1 },
    { "tag": "flow",  "area": "holding", "addr": 20, "dtype": "float", "swap_words": false },
    { "tag": "run",   "area": "coil",    "addr": 0 },
    { "tag": "fault", "area": "discrete","addr": 3 },
    { "tag": "cmd",   "area": "holding", "addr": 5, "access": "wo" }
  ] }
```

| キー | 値 |
|---|---|
| `area` | `coil` (FC1/5), `discrete` (FC2), `holding` (FC3/6/16), `input` (FC4) |
| `dtype` | `u16` (既定), `s16`, `u32`, `s32`, `float`。32bit 型は 2 レジスタ、`swap_words` でワード順反転 |
| `scale` / `offset` | タグ値 = 生値 × scale + offset。書き込み時は逆変換 |
| `access` | `rw` (coil/holding の既定), `ro`, `wo` |
| `status_tag` | 接続状態を書き込む bool タグ（任意） |

- 読み出しはワーカースレッドで周期実行。UI からの書き込みはキューに積まれ、同じスレッドが送信します。
- 起動時にタグの初期値を PLC に書き込むことはありません（PLC 側の値が優先されます）。
- 切断時は指数バックオフ（0.5s → 5s）で再接続します。

### ドライバ: opcua_server（OPC UA サーバー）

タグを OPC UA の変数ノードとして公開します。SCADA や他の HMI、Python (asyncua) などの
OPC UA クライアントから読み書きでき、クライアントからの書き込みはそのままタグに反映されて
画面と他のドライバ（Modbus など）に伝わります。実装は open62541 (MPL-2.0) です。

```json
{
  "type": "opcua_server",
  "port": 4840,
  "namespace": "urn:hml:runtime",
  "folder": "HML",
  "tags": [ "temp", { "tag": "setpoint", "access": "rw" }, { "tag": "alarm", "access": "ro" } ]
}
```

| キー | 意味 |
|---|---|
| `port` | 待ち受けポート（既定 4840）。エンドポイントは `opc.tcp://<host>:<port>` |
| `namespace` | タグノードの名前空間 URI。インデックスは起動時に割り当てられます（通常 2） |
| `folder` | `Objects/` 直下に作るフォルダ名（既定 `HML`） |
| `tags` | 公開するタグ。省略時は全タグを読み書き可で公開。要素は文字列か `{ "tag", "access": "rw" / "ro" }` |

- ノード ID は `ns=<index>;s=<タグ名>`、型は bool → Boolean、int → Int64、float → Double、string → String です。
- 読み出しはタグストアから都度取得するので、Subscription / MonitoredItem でも最新値が届きます。
- セキュリティポリシーは `None`（暗号化・認証なし）のみです。閉じたネットワークでの利用を想定しています。
- 動作確認用のクライアントを同梱しています（`pip install asyncua` が必要）。

```
python tools\opcua_client_test.py                       # Objects/HML 配下のタグを一覧
python tools\opcua_client_test.py --write speed_sp=77   # 書き込み（型はノードから自動判定）
```

## 画面 JSON

```json
{
  "name": "main",
  "style": { "bg_color": "#141a22" },
  "children": [
    { "type": "label", "x": 20, "y": 20, "text": "Temperature", "style": { "font": 24, "text_color": "#fff" } },
    { "type": "label", "x": 20, "y": 60, "bind": { "text": { "tag": "temp", "format": "%.1f C" } } },
    { "type": "slider", "x": 20, "y": 120, "w": 300, "bind": "speed" },
    { "type": "switch", "x": 20, "y": 160, "bind": "run" },
    { "type": "button", "x": 20, "y": 200, "text": "Settings",
      "on_click": { "action": "goto", "screen": "settings", "anim": "slide_left" } }
  ]
}
```

### ウィジェット `type`

| type | 固有プロパティ |
|---|---|
| `panel` (`container`) | `children`, `flex` |
| `label` | `text`, `long_mode` (`wrap`/`dots`/`scroll`/`clip`) |
| `button` | `text`, `checkable`, `style_label` |
| `switch` / `checkbox` | `checked`, (`text`) |
| `slider` / `bar` | `min`, `max`, `value` |
| `arc` | `min`, `max`, `value`, `start_angle`, `end_angle`, `rotation`, `adjustable` |
| `led` | `color`, `on` |
| `dropdown` / `roller` | `options` (配列), `selected`, (`visible_rows`, `infinite`) |
| `textarea` | `text`, `placeholder`, `one_line`, `password`, `max_length`, `accepted_chars` |
| `image` | `src` (project.json からの相対パス, PNG 対応), `zoom`, `rotation` |
| `chart` | `chart_type` (`line`/`bar`), `points`, `min`, `max`, `sample_ms`, `hdiv`, `vdiv`, `series: [{ "tag", "color", "scale" }]` |
| `spinner` | `time`, `angle` |
| `keyboard` | 画面に 1 つ置くと、textarea のフォーカス時に表示される |
| `line` | `points: [[x,y], ...]` |

### 共通プロパティ

| キー | 説明 |
|---|---|
| `x`, `y` | 位置（`align` 指定時はそこからのオフセット） |
| `w`, `h` | 数値 (px) / `"50%"` / `"content"` / `"full"` |
| `align` | `center`, `top_left`, `top_mid`, `top_right`, `left_mid`, `right_mid`, `bottom_left`, `bottom_mid`, `bottom_right` |
| `hidden`, `disabled`, `checked`, `clickable`, `scrollable`, `checkable` | フラグ / 状態 |
| `scrollbar` | `off` / `on` / `auto` / `active` |
| `flex` | `row` / `column` / `row_wrap` / `column_wrap`。`flex_main` / `flex_cross` / `flex_track` に `start`/`center`/`end`/`space_between`/`space_around`/`space_evenly` |
| `flex_grow` | flex 子要素の伸長率 |
| `style` | 下記スタイル。`style_pressed`, `style_checked`, `style_disabled`, `style_focused`, `style_indicator`, `style_indicator_checked`, `style_knob`, `style_items`, `style_selected` も可 |

### `style` のプロパティ

`bg_color`, `bg_opa` (0-100), `bg_grad_color`, `bg_grad_dir` (`hor`/`ver`), `border_color`, `border_width`,
`border_opa`, `outline_width`, `outline_color`, `radius`, `shadow_width`, `shadow_color`, `shadow_opa`,
`text_color`, `text_opa`, `text_align` (`left`/`center`/`right`), `text_letter_space`, `text_line_space`,
`font` (12/14/16/18/20/24/28/32/40/48), `pad`, `pad_left/right/top/bottom`, `pad_row`, `pad_column`, `pad_gap`,
`margin`, `opa`, `line_color`, `line_width`, `line_rounded`, `arc_color`, `arc_width`, `arc_rounded`,
`image_recolor`, `width`, `height`

色は `#rgb` / `#rrggbb` またはパレット名 (`red`, `blue`, `green`, `orange`, `grey`, `teal`, ... , `white`, `black`)。

### `bind` — タグとウィジェットの接続

短縮形 `"bind": "tagname"` は、ウィジェットに自然なプロパティへ結び付けます
（switch/checkbox/button → `checked`、slider/arc/dropdown/roller/bar → `value`、label → `text`、led → `on`、textarea → `text`）。
入力系ウィジェットは自動的に双方向になります。

オブジェクト形式では複数のプロパティを指定できます。

```json
"bind": {
  "text":       { "tag": "temp", "format": "%.1f C" },
  "value":      { "tag": "pressure", "scale": 10 },
  "checked":    "run",
  "on":         "run",
  "visible":    { "tag": "alarm", "eq": 2 },
  "hidden":     "maintenance",
  "enabled":    { "tag": "auto_mode", "invert": true },
  "bg_color":   { "tag": "alarm", "map": { "0": "#2e7d32", "1": "#f9a825", "2": "#c62828", "default": "#555" } },
  "text_color": { "tag": "plc_online", "map": { "true": "#4caf50", "false": "#78909c" } },
  "options":    "option_list_tag",
  "src":        "image_path_tag"
}
```

| キー | 意味 |
|---|---|
| `format` | printf 形式（変換指定子は 1 つ）。`%d` `%x` → 整数、`%f` `%g` → 実数、`%s` → 文字列。`%%` で `%` |
| `scale` / `offset` | ウィジェット値 = タグ値 × scale + offset（双方向時は逆変換） |
| `invert` | 真偽を反転 |
| `eq` / `ne` / `gt` / `lt` / `ge` / `le` | 条件付き真偽（`visible`, `enabled`, `checked`, `on` で使用）。`checked` に `eq` を付けると、ON 時にその値をタグへ書く（ラジオボタン的動作） |
| `map` | 値 → 色 のマップ。`default` で既定色 |
| `part` | `bg_color` の対象パート: `main` (既定) / `indicator` / `knob` |
| `two_way` | 双方向の明示的な ON/OFF |

### アクション

イベント: `on_click`, `on_press`, `on_release`, `on_long_press`, `on_long_press_repeat`, `on_change`, `on_ready`。
値は単一オブジェクトか配列（順に実行）。

```json
"on_click": [
  { "action": "set",    "tag": "run", "value": false },
  { "action": "toggle", "tag": "lamp" },
  { "action": "inc",    "tag": "speed", "by": 10 },
  { "action": "dec",    "tag": "speed", "by": 10 },
  { "action": "goto",   "screen": "settings", "anim": "slide_left" },
  { "action": "back" },
  { "action": "reload" },
  { "action": "exit" }
]
```

`anim`: `none` / `fade` / `slide_left` / `slide_right` / `slide_top` / `slide_bottom` / `over_left` / `over_right`

## ソース構成

| ファイル | 役割 |
|---|---|
| `src/main.cpp` | 起動、コマンドライン、メインループ、テスト自動化（`--click` など） |
| `src/platform.hpp` | 表示 / 入力バックエンドの共通インターフェース（`hml::platform`） |
| `src/platform_sdl.cpp` | SDL2 バックエンド（Windows / デスクトップ）: ウィンドウ、F5 / Esc、スクリーンショット |
| `src/platform_linux.cpp` | Linux fbdev / DRM + evdev バックエンド（Raspberry Pi）: シグナル制御、スナップショット |
| `src/tag.[ch]pp` | `hml::Tag` / `hml::TagStore`: タグストア（スレッドセーフ、変更通知、書き込み元の追跡） |
| `src/ui.[ch]pp` | `hml::Ui`: JSON → LVGL ウィジェット、バインディング、アクション、画面遷移 |
| `src/project.[ch]pp` | `hml::Project`: project.json の読み込み、画面構築とホットリロード |
| `src/driver.[ch]pp` | `hml::Driver` 基底クラスと `hml::DriverRegistry`（登録 / 起動 / 停止） |
| `src/driver_sim.cpp` | シミュレーションドライバ |
| `src/driver_modbus.cpp` | Modbus TCP クライアント（std::thread で別スレッド） |
| `src/driver_opcua.cpp` | OPC UA サーバー（open62541、DataSource コールバックでタグを公開） |
| `src/jsonutil.[ch]pp` | nlohmann/json と、欠損キー・型違いを既定値で吸収するアクセサ（`jstr` / `jnum` / `jbool`） |
| `src/util.[ch]pp` | ログ（`HML_INFO` など）、`iequals`、単調時計 |
| `legacy_c/` | 書き直し前の C 版（参照用、ビルド対象外。不要なら削除可） |
| `lv_conf.h` | LVGL 設定（`HML_BACKEND_*` でドライバ切替、フォント、ウィジェット有効化） |
| `build.cmd` / `build.sh` | Windows (MSVC) / Linux・Raspberry Pi (gcc, クロスコンパイル対応) のビルドスクリプト |
| `cmake/toolchain-*.cmake` | aarch64 / armhf クロスコンパイル用ツールチェーンファイル |
| `tools/modbus_server.py` | テスト用 Modbus TCP サーバー（標準ライブラリのみ） |
| `tools/opcua_client_test.py` | OPC UA 動作確認クライアント（asyncua） |
| `tools/bmp2png.py` | スクリーンショット BMP → PNG 変換 |

### 組み込みターゲットへの移植

ランタイム本体（tag / ui / project / driver）は LVGL、nlohmann/json と C++17 標準ライブラリ
（`std::thread` / `std::mutex` / `std::function`）にしか依存しません。
Linux ボードは上記の `fbdev` / `drm` バックエンドでそのまま動きます。それ以外の RTOS /
ベアメタルへは、`platform.hpp` を実装するファイルを 1 つ追加して対象ボードの表示・入力
ドライバを呼び出します（`std::thread` が無い環境では `driver_modbus.cpp` のスレッド部分も
その OS の API に置き換えます）。JSON ファイルの読み込みは `std::ifstream` なので、
ファイルシステムの無いターゲットでは `project.cpp` の `read_file()` を差し替えてください。

### 拡張のしかた

- **ウィジェットを追加**: `ui.cpp` の `Ui::Impl::build_widget()` に `type` の分岐を 1 つ足します。
  バインド対象にするなら `Widget` 列挙と `Binding::apply_value()` / `on_widget_changed()` にも追加します。
- **ドライバを追加**: `hml::Driver` を継承したクラスを書き、`DriverRegistry::register_builtins()`
  にファクトリを登録します（`driver_sim.cpp` が最小の例です）。
- **アクションを追加**: `ui.cpp` の `ActionKind` と `parse_action()` / `run_actions()` に追加します。

### スレッドモデル

- LVGL と UI バインディングは常にメインスレッドで動作します。
- ドライバは任意のスレッドから `Tag::set_number()` / `set_string()` を呼べます。変更はメインループの
  `TagStore::dispatch()` で購読者（ウィジェット・ドライバ）に配信されます。
- 各書き込みには「書き込み元」が付くため、スライダー自身の書き込みがスライダーに戻って
  ドラッグ中に跳ねる、Modbus ドライバが自分で読んだ値を PLC に書き戻す、といったループは起きません。
