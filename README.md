# HML Runtime — LVGL 宣言型 HMI ランタイム

[![CI](https://github.com/bamchoh/LVGL-HMI/actions/workflows/ci.yml/badge.svg)](https://github.com/bamchoh/LVGL-HMI/actions/workflows/ci.yml)

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
（初回に LVGL / SDL2 / nlohmann-json / open62541 / webview / WebView2 SDK をダウンロードします）。OPC UA サーバーを組み込むには
Python 3 も必要です（open62541 のコード生成に使用。不要なら `-DHML_OPCUA=OFF`）。

```
build.cmd            # Release ビルド → build\bin\hml_runtime.exe
build.cmd Debug
```

### 単体テスト

`tests/` に [doctest](https://github.com/doctest/doctest) の単体テストがあります。LVGL はメモリ上のディスプレイで
ヘッドレスに動かし（`tests/lvgl_fixture.hpp`）、ティックを手動で進めるのでタイマーや画面遷移も決定的にテストできます。

| テスト                                               | 対象                                                                                                                        |
| ---------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `test_util.cpp`, `test_jsonutil.cpp`, `test_tag.cpp` | ヘルパー、JSON アクセサ、タグストア（型変換・リミット・通知）                                                               |
| `test_project.cpp`, `test_demo_projects.cpp`         | project.json の読み込み・バリデーション、同梱プロジェクトが全画面ビルドできること                                           |
| `test_ui.cpp`, `test_screenshot.cpp`                 | 全ウィジェット種別、スタイル、バインディング（双方向含む）、アクション、画面遷移、BMP 出力                                  |
| `test_driver_registry.cpp`, `test_driver_sim.cpp`    | ドライバの登録 / 起動 / 停止、シミュレーション波形                                                                          |
| `test_driver_modbus.cpp`                             | テスト内の疑似 Modbus TCP サーバーに対するエンドツーエンド（全エリア・全データ型・書き込み・再接続）                        |
| `test_driver_opcua.cpp`                              | open62541 クライアント API で OPC UA サーバードライバを読み書き                                                             |
| `tools/designer/test/model.test.js`                  | デザイナーの util / schema / model（`node --test`、依存パッケージなし）                                                     |
| `smoke_runtime.cmake`                                | 実行ファイル `hml_runtime` をデモプロジェクトでヘッドレス起動（SDL dummy ドライバ）し、スクリーンショットと終了コードを確認 |

```
test.cmd                         # ビルドして hml_tests を実行
test.cmd -tc="*modbus*"          # doctest の引数をそのまま渡せる（テストケース名でフィルタ）
ctest --test-dir build --output-on-failure   # hml_tests + designer_js (node) + hml_runtime_smoke
```

Linux では `./build.sh` のあとに `build-<backend>/bin/hml_tests` を実行してください。
テストを組み込みたくない場合は `-DHML_TESTS=OFF` を付けます。

GitHub Actions（`.github/workflows/ci.yml`）が push / PR ごとに Windows (MSVC, sdl) と Linux (gcc, fbdev) で
テストを実行し、続けてランタイムとデザイナーのフルビルドも行います。

## ビルド（Raspberry Pi / Linux）

表示バックエンドは CMake の `-DHML_BACKEND=` で選びます（`build.sh` が引数で切り替えます）。

| バックエンド | 用途                                                                                       | 追加パッケージ |
| ------------ | ------------------------------------------------------------------------------------------ | -------------- |
| `fbdev`      | `/dev/fb0` + evdev。X11/Wayland 不要、コンソールから起動する Raspberry Pi 向け（**既定**） | なし           |
| `drm`        | `/dev/dri/card0` (KMS) + evdev。vc4-kms-v3d 環境でティアリングを避けたい場合               | `libdrm-dev`   |
| `sdl`        | デスクトップ Linux のウィンドウ表示（Windows と同じ）                                      | `libsdl2-dev`  |

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
- カーネルメッセージやログインプロンプトが HMI の上に描かれるのを防ぐため、ランタイムは起動時にコンソール
  （`display.console`、既定 `/dev/tty1`）をグラフィックスモード（`KD_GRAPHICS`）に切り替え、終了時に戻します。
  `/dev/tty1` は root 専用なので、`tools/rpi/hml.service` のように `TTYPath=/dev/tty1` + `StandardInput=tty` で
  systemd から tty を受け取るか、root で実行してください（開けない場合は警告を出して続行します）。
  `"console": "none"` で無効化、環境変数 `HML_CONSOLE` で上書きできます。ユニットは `getty@tty1` と排他にして
  あるので tty1 のログインプロンプトは出なくなります（SSH を使ってください）。
- 起動・終了時のコンソールログ（カーネルや systemd の `[ OK ] ...`）を画面に出さないようにするには
  `sudo tools/rpi/quiet_boot.sh` を実行して再起動します。`cmdline.txt` の `console=tty1` を `console=tty3`
  （画面に出ない VT）に変えて `quiet loglevel=3` を足し、`getty@tty1` を無効にします（ログインは SSH で）。
  シリアルコンソールはそのままです。`--revert` で元に戻ります。`apply_lcd.sh` も最後にこれを実行します。
- 起動スプラッシュ: `hml_runtime --splash project.json` は `display.splash`（PNG、プロジェクトからの相対パス。
  中央に表示し、画面より大きければ縮小）を背景色 `display.splash_bg`（既定は黒）の上に 1 枚描いて終了します。
  画像がなければプロジェクトのタイトルを表示します。tty1 をグラフィックスモードのまま残すので、HMI が描画を
  始めるまで画面に残ります。`tools/rpi/hml-splash.service` が起動のごく初期（ルートファイルシステムが
  あれば動く）にこれを実行します。デザイナーでは Project タブの「Boot splash」で設定できます。
  ```
  sudo install -m 644 ~/hml/tools/rpi/hml.service ~/hml/tools/rpi/hml-splash.service /etc/systemd/system/
  sudo systemctl daemon-reload && sudo systemctl enable hml-splash
  ```
  `hml.service` は `TTYReset` / `TTYVTDisallocate` を使いません（テキストモードに戻してスプラッシュが消えるため）。
- コンソールのカーソル点滅が画面に残る場合は `echo 0 | sudo tee /sys/class/graphics/fbcon/cursor_blink`。
- 画面サイズは framebuffer の解像度が使われます。`project.json` の `display.width/height` は
  画面 JSON の座標基準なので、Pi の解像度に合わせておくか `rotation` と合わせて調整してください。
- fbdev/drm ドライバはカーネルが返す物理サイズから DPI を計算しますが、HDMI ではでたらめな値
  （Pi 4 で 1280 px = 70 mm → 464 dpi）が返ることがあり、テーマの余白や角丸が数倍に膨らみます。
  ランタイムは `display.dpi`（既定 130）で必ず上書きするので、通常は何もしなくて大丈夫です。
- 入力は `/dev/input/event*` を自動検出します（タッチ / マウス / キーボード）。特定のデバイスだけ
  使う場合は `display.input` か環境変数 `HML_INPUT` で指定します。
- 終了は `Ctrl-C` (SIGINT/SIGTERM)、画面 JSON のホットリロードは `kill -HUP <pid>` または `kill -USR1 <pid>`。

### 3.5 インチ HDMI LCD (480x320) + ADS7846 タッチの例

`tools/rpi/` に Raspberry Pi OS 用の設定一式があります。`projects/lcd35` は同じデモを 480x320 向けに
組み直したものです（タッチは 180 度回転なので `calibration` が [max, max, min, min] の順になっています）。

```
scp -r tools/rpi pi:~/hml/lcd          # config-lcd.txt / apply_lcd.sh / quiet_boot.sh / hml.service
sudo ~/hml/lcd/apply_lcd.sh            # config.txt と cmdline.txt を書き換え、起動ログも非表示（--revert で元に戻る）
sudo reboot
sudo cp ~/hml/lcd/hml.service /etc/systemd/system/ && sudo systemctl enable --now hml   # 自動起動
```

- 既定は vc4 オーバーレイなし（firmware が `hdmi_cvt` で 480x320 を出し、`/dev/fb0` は simple-framebuffer）。
  `vc4-fkms-v3d` はカーネル 6.18 では `Cannot find any crtc or sizes` で失敗し fb0 が消えるため使いません。
- タッチの向きを調べるには `python3 tools/rpi/touch_raw.py /dev/input/event0 60` で生の座標を見ます。

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

| キー / オプション           | 動作                                                                                                              |
| --------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `F5`                        | 画面 JSON をホットリロード（タグ・ドライバは維持、表示中の画面を保つ）。Linux fbdev/drm では `SIGHUP` / `SIGUSR1` |
| `Esc`                       | 終了。Linux fbdev/drm では `Ctrl-C` (`SIGINT` / `SIGTERM`)                                                        |
| `--screen <name>`           | 起動時に表示する画面を指定                                                                                        |
| `--screenshot <file.bmp>`   | 起動後にスクリーンショットを保存（`tools/bmp2png.py` で PNG 化できます）                                          |
| `--exit-after <ms>`         | 指定時間後に自動終了（CI / 自動テスト用）                                                                         |
| `--reload-after <ms>`       | 指定時間後にホットリロードを実行（テスト用）                                                                      |
| `--click <x>,<y>,<ms>`      | 起動から `ms` 後に座標 (x,y) をクリック（複数指定可、UI 自動テスト用）                                            |
| `--shot-at <ms>,<file.bmp>` | 指定時刻にスクリーンショットを保存（複数指定可）                                                                  |

デモの Modbus 接続を試すには、同梱のテストサーバーを別ターミナルで起動してください。

```
python tools\modbus_server.py --port 5020
```

接続すると画面右上の PLC ランプが点灯し、Run スイッチ・Speed スライダー・Setpoint アークが
サーバーのコイル / 保持レジスタと同期します。サーバーが無い場合も画面は動作します（オフライン表示）。

## デザイナー（GUI でプロジェクトを作る）

`hml_designer.exe`（Windows ビルドでは既定で一緒にビルドされます）は、`project.json` と画面 JSON を
GUI で編集するツールです。追加のランタイムは不要で、OS 標準の WebView（Windows: WebView2、
Windows 10/11 に標準搭載）に HTML/JS の画面を表示し、C++ のホストがファイル入出力とランタイムの起動を担当します。

```
build\bin\hml_designer.exe                       # 起動（New / Open で開始）
build\bin\hml_designer.exe projects\demo\project.json   # プロジェクトを指定して起動
```

| 場所                                   | 内容                                                                                                                                                               |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 左: Widgets                            | パレット。クリックすると選択中のパネル（または画面）に追加                                                                                                         |
| 左: Screens / Tags / Drivers / Project | 画面の追加・削除・開始画面、タグ表、ドライバ設定（sim / modbus_tcp / opcua_server のフォーム + raw JSON）、表示サイズ・テーマ・タッチ補正                          |
| 中央: キャンバス                       | ドラッグで移動、右下のハンドルでサイズ変更、矢印キーで 1 px（Shift で 10 px）移動。flex レイアウトの子は親の設定に従うので移動不可                                 |
| 右: ツリー + プロパティ                | 階層と順序（▲▼ で前後、⇥⇤ でパネルに出し入れ）、位置 / サイズ / フラグ / スタイル（セレクタごと）/ バインド / イベント（アクション列）                             |
| Preview                                | 保存してから `hml_runtime` をウィンドウなし（`SDL_VIDEODRIVER=dummy`）で起動し、その画面のスクリーンショットを表示。キャンバスは近似表示なので、最終確認はこちらで |
| Run                                    | 保存して `hml_runtime` を通常起動                                                                                                                                  |
| JSON                                   | 現在の画面 JSON を直接編集して適用                                                                                                                                 |

- キーボード: Ctrl+S 保存、Ctrl+Z / Ctrl+Y 取り消し / やり直し、Ctrl+D 複製、Delete 削除、Esc 選択解除。
- 画面 JSON のコメントは読み込めますが、保存時には整形された JSON（コメントなし）になります。
- `hml_runtime.exe` は `hml_designer.exe` と同じフォルダから探します。
- ページ本体は `tools/designer/renderer/`（`index.html`, `js/*.js`）で、ビルド時に `bin/designer/` へコピーされます。
  ランタイムが対応するウィジェット・プロパティ一覧は `js/schema.js` にまとまっているので、ランタイムに
  機能を足したらここに 1 行追加すれば GUI に現れます。
- Linux でもビルドできます（`-DHML_DESIGNER=ON`、`libwebkit2gtk-4.1-dev` が必要）。Raspberry Pi 上で使う想定ではありません。

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
{
  "type": "sim",
  "items": [
    { "tag": "temp", "mode": "sine", "min": 18, "max": 32, "period_ms": 12000 },
    {
      "tag": "level",
      "mode": "ramp",
      "min": 0,
      "max": 100,
      "period_ms": 20000
    },
    { "tag": "blink", "mode": "square", "period_ms": 1000 },
    {
      "tag": "alarm",
      "mode": "counter",
      "min": 0,
      "max": 2,
      "period_ms": 4000
    },
    { "tag": "noise", "mode": "random", "min": 0, "max": 10, "period_ms": 500 },
    {
      "tag": "state",
      "mode": "cycle",
      "values": ["Idle", "Run", "Alarm"],
      "period_ms": 3000
    }
  ]
}
```

`mode`: `sine` / `triangle` / `ramp` / `square` / `random` / `counter` / `cycle`

### ドライバ: modbus_tcp

```json
{
  "type": "modbus_tcp",
  "host": "192.168.0.10",
  "port": 502,
  "unit_id": 1,
  "poll_ms": 200,
  "timeout_ms": 1000,
  "status_tag": "plc_online",
  "map": [
    { "tag": "speed", "area": "holding", "addr": 0 },
    {
      "tag": "temp",
      "area": "input",
      "addr": 10,
      "dtype": "s16",
      "scale": 0.1
    },
    {
      "tag": "flow",
      "area": "holding",
      "addr": 20,
      "dtype": "float",
      "swap_words": false
    },
    { "tag": "run", "area": "coil", "addr": 0 },
    { "tag": "fault", "area": "discrete", "addr": 3 },
    { "tag": "cmd", "area": "holding", "addr": 5, "access": "wo" }
  ]
}
```

| キー               | 値                                                                                             |
| ------------------ | ---------------------------------------------------------------------------------------------- |
| `area`             | `coil` (FC1/5), `discrete` (FC2), `holding` (FC3/6/16), `input` (FC4)                          |
| `dtype`            | `u16` (既定), `s16`, `u32`, `s32`, `float`。32bit 型は 2 レジスタ、`swap_words` でワード順反転 |
| `scale` / `offset` | タグ値 = 生値 × scale + offset。書き込み時は逆変換                                             |
| `access`           | `rw` (coil/holding の既定), `ro`, `wo`                                                         |
| `status_tag`       | 接続状態を書き込む bool タグ（任意）                                                           |

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
  "tags": [
    "temp",
    { "tag": "setpoint", "access": "rw" },
    { "tag": "alarm", "access": "ro" }
  ]
}
```

| キー        | 意味                                                                                              |
| ----------- | ------------------------------------------------------------------------------------------------- |
| `port`      | 待ち受けポート（既定 4840）。エンドポイントは `opc.tcp://<host>:<port>`                           |
| `namespace` | タグノードの名前空間 URI。インデックスは起動時に割り当てられます（通常 2）                        |
| `folder`    | `Objects/` 直下に作るフォルダ名（既定 `HML`）                                                     |
| `tags`      | 公開するタグ。省略時は全タグを読み書き可で公開。要素は文字列か `{ "tag", "access": "rw" / "ro" }` |

- ノード ID は `ns=<index>;s=<タグ名>`、型は bool → Boolean、int → Int64、float → Double、string → String です。
- 読み出しはタグストアから都度取得するので、Subscription / MonitoredItem でも最新値が届きます。
- セキュリティポリシーは `None`（暗号化・認証なし）のみです。閉じたネットワークでの利用を想定しています。
- 動作確認用のクライアントを同梱しています（`pip install asyncua` が必要）。

```
python tools\opcua_client_test.py                       # Objects/HML 配下のタグを一覧
python tools\opcua_client_test.py --write speed_sp=77   # 書き込み（型はノードから自動判定）
```

## システム設定画面

ランタイムは project.json とは別に、組み込みの「System」メニュー画面（`__system`）と設定ページ画面
（`__system_<id>`）を自動生成します。画面のボタンから `{ "action": "system" }` で開けます。

| ページ | 内容 |
|---|---|
| About | ホスト名、IP アドレス、プロジェクト、表示バックエンド、ビルド情報（表示のみ） |
| Network | NetworkManager (`nmcli`) のある Linux で、インターフェース、DHCP / 固定 IP、アドレス (CIDR)、ゲートウェイ、DNS を設定。Apply で `nmcli connection modify` + `up` を実行（一般ユーザーでは polkit が認証を求めるので `sudo -n` で再試行。`tools/rpi/hml.sudoers` を入れておく）。接続中の IP を変えると SSH が切れます。Windows では表示のみ |
| Driver: … | project.json の各ドライバ（1 ドライバ 1 ページ）。`enabled` と、`host` / `port` / `poll_ms` などのスカラー設定を編集。Apply で `project.local.json` に保存し、全ドライバを再起動 |

設定値は `sys.<page>.<key>` というタグに入り、Apply で各ページの `apply()` が呼ばれます（結果は `sys.<page>.status`）。
`project.local.json` は project.json と同じ場所に書かれ、起動時に `drivers` のスカラー値を上書きします
（project.json 自体は変更しません。OPC UA には `sys.*` タグは公開されません）。

ページを追加するには `src/system_settings.hpp` の `SettingsPage`（`id` / `title` / `fields()` / `apply()`）を
実装して `register_builtin_pages()` で登録するだけで、画面とタグは自動で作られます。フィールド型は
Text / Number / Bool / Select / Info。

## 画面 JSON

```json
{
  "name": "main",
  "style": { "bg_color": "#141a22" },
  "children": [
    {
      "type": "label",
      "x": 20,
      "y": 20,
      "text": "Temperature",
      "style": { "font": 24, "text_color": "#fff" }
    },
    {
      "type": "label",
      "x": 20,
      "y": 60,
      "bind": { "text": { "tag": "temp", "format": "%.1f C" } }
    },
    { "type": "slider", "x": 20, "y": 120, "w": 300, "bind": "speed" },
    { "type": "switch", "x": 20, "y": 160, "bind": "run" },
    {
      "type": "button",
      "x": 20,
      "y": 200,
      "text": "Settings",
      "on_click": {
        "action": "goto",
        "screen": "settings",
        "anim": "slide_left"
      }
    }
  ]
}
```

### ウィジェット `type`

| type                  | 固有プロパティ                                                                                                            |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `panel` (`container`) | `children`, `flex`                                                                                                        |
| `label`               | `text`, `long_mode` (`wrap`/`dots`/`scroll`/`clip`)                                                                       |
| `button`              | `text`, `checkable`, `style_label`                                                                                        |
| `switch` / `checkbox` | `checked`, (`text`)                                                                                                       |
| `slider` / `bar`      | `min`, `max`, `value`                                                                                                     |
| `arc`                 | `min`, `max`, `value`, `start_angle`, `end_angle`, `rotation`, `adjustable`                                               |
| `led`                 | `color`, `on`                                                                                                             |
| `dropdown` / `roller` | `options` (配列), `selected`, (`visible_rows`, `infinite`)                                                                |
| `textarea`            | `text`, `placeholder`, `one_line`, `password`, `max_length`, `accepted_chars`                                             |
| `numeric_display`     | 数値表示器: `input`, `decimals`, `format`, `unit`, `min`, `max`, `placeholder`, `accepted_chars`（下記「データ表示器」）  |
| `text_display`        | 文字列表示器: `input`, `text`, `placeholder`, `max_length`, `accepted_chars`, `one_line`                                   |
| `password_display`    | パスワード入力器（常に伏せ字）: `input` (既定 true), `placeholder`, `max_length`, `accepted_chars`                         |
| `image`               | `src` (project.json からの相対パス, PNG 対応), `zoom`, `rotation`                                                         |
| `chart`               | `chart_type` (`line`/`bar`), `points`, `min`, `max`, `sample_ms`, `hdiv`, `vdiv`, `series: [{ "tag", "color", "scale" }]` |
| `spinner`             | `time`, `angle`                                                                                                           |
| `keyboard`            | 画面に 1 つ置くと、textarea / 入力可能な表示器のタップ時に表示される（`numeric_display` ではテンキー）。✓ で確定、× で閉じる（入力は破棄）。`bottom_mid` / `top_mid` 配置なら、入力欄を隠す場合は反対側の端に出る。キーの大きさは `h` と `style_items: { "font": 18 }`、`style: { "pad", "pad_gap" }` で調整 |
| `line`                | `points: [[x,y], ...]`                                                                                                    |

#### データ表示器 (`numeric_display` / `text_display` / `password_display`)

タグの値を 1 行の枠内に表示する、用途を限定したウィジェットです。既定では表示専用（クリック不可、カーソルなし）で、
`"input": true` にすると入力欄になります（`password_display` は既定で入力可）。

```json
{ "type": "numeric_display", "bind": "setpoint", "decimals": 1, "unit": " rpm",
  "input": true, "min": 0, "max": 100 },
{ "type": "numeric_display", "bind": { "value": { "tag": "pressure", "scale": 10 } }, "format": "%04d" },
{ "type": "text_display", "bind": "operator", "input": true, "max_length": 16 },
{ "type": "password_display", "bind": "pin", "max_length": 4, "accepted_chars": "0123456789" }
```

| プロパティ | 意味 |
|---|---|
| `input` | true で入力可。タップすると現在値が全選択され、最初のキー入力で置き換わる。確定（キーボードの OK / Enter、またはフォーカスが外れたとき）にタグへ書き込み、キャンセルで元の値に戻る。`textarea` と違い 1 文字ごとには書き込まない |
| `decimals` / `format` / `unit` | 数値の表示形式。`decimals: 1` は `%.1f` と同じ。`format` を書くとそちらを優先。`unit` は末尾に付加（例 `" rpm"`）。`bind` 側の `format` があればそれが最優先 |
| `min` / `max` | 入力値の範囲。範囲外の入力は丸められ、丸めた値が表示に反映される（タグ側の `min`/`max` も別途適用） |
| `accepted_chars` | 入力できる文字。`numeric_display` の既定は `0123456789.-+` |

短縮形 `"bind": "tag"` は `numeric_display` では `value`、`text_display` / `password_display` では `text` に結び付きます。

### 共通プロパティ

| キー                                                                    | 説明                                                                                                                                                                                |
| ----------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `x`, `y`                                                                | 位置（`align` 指定時はそこからのオフセット）                                                                                                                                        |
| `w`, `h`                                                                | 数値 (px) / `"50%"` / `"content"` / `"full"`                                                                                                                                        |
| `align`                                                                 | `center`, `top_left`, `top_mid`, `top_right`, `left_mid`, `right_mid`, `bottom_left`, `bottom_mid`, `bottom_right`                                                                  |
| `hidden`, `disabled`, `checked`, `clickable`, `scrollable`, `checkable` | フラグ / 状態                                                                                                                                                                       |
| `scrollbar`                                                             | `off` / `on` / `auto` / `active`                                                                                                                                                    |
| `flex`                                                                  | `row` / `column` / `row_wrap` / `column_wrap`。`flex_main` / `flex_cross` / `flex_track` に `start`/`center`/`end`/`space_between`/`space_around`/`space_evenly`                    |
| `flex_grow`                                                             | flex 子要素の伸長率                                                                                                                                                                 |
| `style`                                                                 | 下記スタイル。`style_pressed`, `style_checked`, `style_disabled`, `style_focused`, `style_indicator`, `style_indicator_checked`, `style_knob`, `style_items`, `style_selected` も可 |

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
（switch/checkbox/button → `checked`、slider/arc/dropdown/roller/bar/numeric_display → `value`、label/textarea/text_display/password_display → `text`、led → `on`）。
入力系ウィジェットは自動的に双方向になります。

オブジェクト形式では複数のプロパティを指定できます。

```json
"bind": {
  "text":       { "tag": "temp", "format": "%.1f C" },
  "value":      { "tag": "pressure", "scale": 10 },
  "checked":    "run",
  "on":         "run",
  "visible":    { "tag": "alarm", "eq": 2 },
  "visible":    { "tag": "temp", "gt": { "tag": "temp_hi" } },
  "hidden":     "maintenance",
  "enabled":    { "tag": "auto_mode", "invert": true },
  "bg_color":   { "tag": "alarm", "map": { "0": "#2e7d32", "1": "#f9a825", "2": "#c62828", "default": "#555" } },
  "text_color": { "tag": "plc_online", "map": { "true": "#4caf50", "false": "#78909c" } },
  "options":    "option_list_tag",
  "src":        "image_path_tag"
}
```

| キー                                    | 意味                                                                                                                                        |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `format`                                | printf 形式（変換指定子は 1 つ）。`%d` `%x` → 整数、`%f` `%g` → 実数、`%s` → 文字列。`%%` で `%`                                            |
| `scale` / `offset`                      | ウィジェット値 = タグ値 × scale + offset（双方向時は逆変換）                                                                                |
| `invert`                                | 真偽を反転                                                                                                                                  |
| `eq` / `ne` / `gt` / `lt` / `ge` / `le` | 条件付き真偽（`visible`, `enabled`, `checked`, `on` で使用）。比較対象は定数か、`{ "tag": "temp_hi" }` のように別のタグ（どちらのタグが変わっても再評価）。`checked` に `eq` を付けると、ON 時にその値をタグへ書く（ラジオボタン的動作） |
| `map`                                   | 値 → 色 のマップ。`default` で既定色                                                                                                        |
| `part`                                  | `bg_color` の対象パート: `main` (既定) / `indicator` / `knob`                                                                               |
| `two_way`                               | 双方向の明示的な ON/OFF                                                                                                                     |

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
  { "action": "exit" },
  { "action": "restart" },
  { "action": "system" },
  { "action": "call", "fn": "system.apply", "arg": "network" }
]
```

`system` は組み込みのシステム設定画面（下記）を開きます。`call` はランタイムに登録された名前付きアクションを呼びます。

`restart` は Raspberry Pi（fbdev / drm バックエンド）では OS を再起動し、PC（SDL バックエンド）ではアプリを
再起動します。`"what": "app"` / `"system"` で明示もできます。OS の再起動は `systemctl reboot` を `sudo -n`
経由で呼ぶので、サービスを一般ユーザーで動かしている Pi では一度だけ次を実行してパスワードなしで許可してください
（許可がない場合はアプリの再起動にフォールバックします）。

```
sudo install -m 440 tools/rpi/hml.sudoers /etc/sudoers.d/hml
```

`anim`: `none` / `fade` / `slide_left` / `slide_right` / `slide_top` / `slide_bottom` / `over_left` / `over_right`

## ソース構成

| ファイル                       | 役割                                                                                                |
| ------------------------------ | --------------------------------------------------------------------------------------------------- |
| `src/main.cpp`                 | 起動、コマンドライン、メインループ、テスト自動化（`--click` など）                                  |
| `src/platform.hpp`             | 表示 / 入力バックエンドの共通インターフェース（`hml::platform`）                                    |
| `src/platform_sdl.cpp`         | SDL2 バックエンド（Windows / デスクトップ）: ウィンドウ、F5 / Esc、スクリーンショット               |
| `src/platform_linux.cpp`       | Linux fbdev / DRM + evdev バックエンド（Raspberry Pi）: シグナル制御、スナップショット              |
| `src/tag.[ch]pp`               | `hml::Tag` / `hml::TagStore`: タグストア（スレッドセーフ、変更通知、書き込み元の追跡）              |
| `src/ui.[ch]pp`                | `hml::Ui`: JSON → LVGL ウィジェット、バインディング、アクション、画面遷移                           |
| `src/project.[ch]pp`           | `hml::Project`: project.json の読み込み、画面構築とホットリロード                                   |
| `src/driver.[ch]pp`            | `hml::Driver` 基底クラスと `hml::DriverRegistry`（登録 / 起動 / 停止）                              |
| `src/driver_sim.cpp`           | シミュレーションドライバ                                                                            |
| `src/driver_modbus.cpp`        | Modbus TCP クライアント（std::thread で別スレッド）                                                 |
| `src/driver_opcua.cpp`         | OPC UA サーバー（open62541、DataSource コールバックでタグを公開）                                   |
| `src/jsonutil.[ch]pp`          | nlohmann/json と、欠損キー・型違いを既定値で吸収するアクセサ（`jstr` / `jnum` / `jbool`）           |
| `src/util.[ch]pp`              | ログ（`HML_INFO` など）、`iequals`、単調時計                                                        |
| `legacy_c/`                    | 書き直し前の C 版（参照用、ビルド対象外。不要なら削除可）                                           |
| `lv_conf.h`                    | LVGL 設定（`HML_BACKEND_*` でドライバ切替、フォント、ウィジェット有効化）                           |
| `build.cmd` / `build.sh`       | Windows (MSVC) / Linux・Raspberry Pi (gcc, クロスコンパイル対応) のビルドスクリプト                 |
| `test.cmd` / `tests/`          | 単体テスト（doctest）: `test_jsonutil.cpp`, `test_tag.cpp`。`hml_tests` にビルドされ CTest にも登録 |
| `cmake/toolchain-*.cmake`      | aarch64 / armhf クロスコンパイル用ツールチェーンファイル                                            |
| `tools/modbus_server.py`       | テスト用 Modbus TCP サーバー（標準ライブラリのみ）                                                  |
| `tools/opcua_client_test.py`   | OPC UA 動作確認クライアント（asyncua）                                                              |
| `tools/bmp2png.py`             | スクリーンショット BMP → PNG 変換                                                                   |
| `tools/designer/host/main.cpp` | デザイナーのホスト（webview、ファイルダイアログ、ランタイム起動）                                   |
| `tools/designer/renderer/`     | デザイナーの画面（HTML / CSS / JS、ビルド不要）                                                     |
| `tools/rpi/`                   | Raspberry Pi の LCD 設定スクリプト、systemd ユニット、タッチ生データ確認                            |

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
