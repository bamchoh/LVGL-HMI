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
               ├── screens   : screens/*.json → LVGL ウィジェットに展開
               │                 bind でタグ⇔ウィジェットを接続、on_click 等でアクション
               └── scripts   : Lua スクリプト（起動時 / 周期 / タグ変化 / 画面の表示・非表示 / イベント）
```

## ビルド（Windows / MSVC）

必要なもの: Visual Studio 2022 以降 (C++ ツール), CMake 3.24+, Ninja, ネットワーク接続
（初回に LVGL / SDL2 / nlohmann-json / Lua / SQLite / open62541 / webview / WebView2 SDK をダウンロードします）。OPC UA サーバーを組み込むには
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
| `test_system_log.cpp`                                | システムログ（SQLite への記録、繰り返しの集約、保持件数、CSV エクスポート、スレッドからの書き込み）                         |
| `test_logs.cpp`                                      | 変数ロギング（各トリガー）、アラーム（発生 / 復帰）、操作ログ（ウィジェット・アクション、パスワードの伏せ字）、CSV エクスポート |
| `test_key_service.cpp`                               | 鍵の管理（SHA-256 / フィンガープリント、公開鍵の検証、authorized_keys の編集、受付プロトコル、Keys ページ）       |
| `test_users.cpp`                                     | ユーザー管理（ログイン・ロック・自動ログアウト、ウィジェットと画面のレベル、ログイン画面、装置上でのユーザー編集） |
| `test_script.cpp`                                    | Lua スクリプト（タグの読み書き、各トリガー、サンドボックス、タイムアウト、ホットリロード）                                  |
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
| `SIGUSR2`（Linux fbdev/drm）| ランタイムを再起動（終了処理をしてから同じコマンドラインで起動し直す）。デザイナーの Deploy が使う                |
| `--screen <name>`           | 起動時に表示する画面を指定                                                                                        |
| `--screenshot <file.bmp>`   | 起動後にスクリーンショットを保存（`tools/bmp2png.py` で PNG 化できます）                                          |
| `--exit-after <ms>`         | 指定時間後に自動終了（CI / 自動テスト用）                                                                         |
| `--reload-after <ms>`       | 指定時間後にホットリロードを実行（テスト用）                                                                      |
| `--export-system-log <file.csv>` | システムログを CSV に書き出して終了（`--log-level warn` などで絞り込み）→「システムログ」                    |
| `--export-data-log <file.csv>` | 変数ロギングのグループを CSV に書き出して終了（`--group <name>`）。`--export-alarm-log` / `--export-operation-log` も同様 →「ロギング」 |
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
| Deploy                                 | 保存して、プロジェクト（と任意でランタイム）を SSH でターゲット（Raspberry Pi など）へ送り、ランタイムを再起動 →「ターゲットへのデプロイ」                        |
| 中央: Logs タブ                        | 変数ロギング（グループ）、アラーム、操作ログ、システムログの設定。チェックを入れたものだけ記録される     |
| 中央: Users タブ                       | ユーザー管理: レベル名、ユーザー（名前・レベル・PIN）、自動ログアウトやロックの設定 →「ユーザー管理」   |
| 中央: Scripts タブ                     | Lua スクリプトの追加・削除、トリガー（起動時 / 周期 / タグ変化 / 画面の表示・非表示 / manual）とコードの編集。ウィジェットのイベントには `script` アクションで割り当てる |
| JSON                                   | 現在の画面 JSON を直接編集して適用                                                                                                                                 |

### ターゲットへのデプロイ

ツールバーの **Deploy** で、開いているプロジェクトを Raspberry Pi などのターゲットへ送ります。

| 項目            | 内容                                                                                                   |
| --------------- | ------------------------------------------------------------------------------------------------------ |
| target (host)   | ターゲットの IP アドレスまたはホスト名                                                                 |
| user / port     | ログインするユーザー名と SSH のポート（既定 22）                                                       |
| private key     | SSH の秘密鍵ファイル。空なら既定の鍵（`~/.ssh/id_*`）                                                  |
| remote folder   | ターゲット上の配置先（既定 `hml` = ホームの `hml/`）                                                   |
| send runtime    | チェックすると、指定したターゲット用の `hml_runtime`（Linux 用のビルド）も送って入れ替える             |
| restart runtime | 配置後にターゲットのランタイムを再起動する（`SIGUSR2`）                                                |
| local settings  | チェックすると、ターゲットの画面で変更された設定（`project.local.json`）を削除する。毎回オフで始まる   |

ダイアログを開いたとき（接続先が入力済みの場合）と **Check** を押したときに、何も送らずにターゲットの状態を調べて表示します。

- **ターゲットで動いているプロジェクト**（このプロジェクトか、別のプロジェクトか、ランタイムが動いていないか）
- **ターゲットのローカル設定**: ターゲットのシステム設定画面で変更されたドライバ設定は `project.local.json` に保存され、
  デプロイしてもそちらが優先されます。該当する項目を「ターゲットでの値」と「このプロジェクトの値」を並べた表で表示します。
  プロジェクトの値に戻したいときは「local settings」にチェックを入れてデプロイします。
- **ファイルの差分**: 変更されたファイル、新しいファイル、ターゲットにだけ残っているファイルの一覧（ファイル単位）

デプロイが終わると、同じ内容を調べ直して表示します。

ターゲット上の配置は次のとおりです。systemd のユニットなどは、この `project.json` を指すようにしておきます。

```
<remote folder>/hml_runtime
<remote folder>/projects/<プロジェクトのフォルダ名>/project.json, screens/, assets/ ...
```

- PC 側は OS 付属の OpenSSH（`ssh` / `scp`）を使います。**鍵認証のみ**で、パスワードは聞きません。
  公開鍵は、ターゲットの画面（System > Keys）を鍵管理モードにして private key の右の **Register…** を押すと登録できます
  （→「鍵の管理」）。手作業でターゲットの `~/.ssh/authorized_keys` に追加してもかまいません。
- 初めて接続するターゲットのホスト鍵は、自動的に `known_hosts` に登録されます。
- ターゲットにだけあるファイル（`logs/`、システム設定画面が書く `project.local.json`）は残ります
  （`project.local.json` は「local settings」にチェックしたときだけ削除します）。
  PC 側の `logs/` と `project.local.json` は送りません。プロジェクトから削除したファイルは、ターゲットからは消えません。
- ランタイムが動いていなければ、ファイルの配置だけを行います（結果にその旨が表示されます）。
  ターゲットが別のプロジェクトを実行している場合も、結果に表示されます。
- 接続先の設定は、プロジェクトではなくデザイナーの設定ファイル（Windows: `%APPDATA%\hml_designer\settings.json`）に、
  プロジェクトごとに保存されます。

ウィンドウを開かずにコマンドラインからもデプロイできます。省略したオプションには、Deploy ダイアログで
そのプロジェクトに最後に使った値が使われます。

```
hml_designer --deploy projects\lcd35\project.json --host 192.168.0.8 --user pi ^
    [--port 22] [--key <秘密鍵>] [--remote-dir hml] [--runtime <Linux 用 hml_runtime>] [--no-restart] [--clear-local]
```

`--check` を付けると何も送らず、ターゲットの状態（上の Check と同じ内容）を JSON で表示します。

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

- `tags[].type`: `bool` / `int` / `float` / `string`。`min` / `max` を付けると、HMI 側からの書き込み（ウィジェット、アクション、スクリプト、OPC UA クライアント）が
  その範囲に丸められます。機器（Modbus）から読んだ値は丸めず、機器が持っている値をそのまま表示します。
  範囲外の値を読んだときは、タグごとに 1 回、警告をログに出します。
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

## ユーザー管理

ユーザーごとにレベルを決め、レベルが足りないユーザーには画面やウィジェットを操作させないようにできます。
project.json に `users` を書いたときだけ有効で、書かなければ今までどおり何も制限されません。

```json
"users": {
  "levels": ["Operator", "Maintenance", "Admin"],
  "list": [
    { "name": "operator", "level": 1, "pin": "1111" },
    { "name": "maint",    "level": 2, "pin": "2222" },
    { "name": "admin",    "level": 3, "pin_hash": "<salt>$<sha256>" }
  ],
  "auto_logout_s": 300,
  "max_failures": 5,
  "lock_s": 60,
  "system_level": 3
}
```

| キー            | 既定値                          | 内容                                                                              |
| --------------- | ------------------------------- | --------------------------------------------------------------------------------- |
| `levels`        | Operator / Maintenance / Admin  | レベル 1, 2, 3 … の名前（最大 9）。レベル 0 は「誰もログインしていない」          |
| `list`          |                                 | ユーザー（最大 16）。`name`（16 文字まで）、`level`、`pin`（4〜8 桁の数字）       |
| `auto_logout_s` | `300`                           | この秒数だけ操作がないと自動でログアウト（`0` = しない）                          |
| `max_failures`  | `5`                             | PIN を続けてこの回数まちがえると、そのユーザーをロック                            |
| `lock_s`        | `60`                            | ロックの時間（秒）                                                                |
| `system_level`  | いちばん高いレベル              | システム設定画面を開くのに必要なレベル（`0` = 制限なし）                          |
| `enabled`       | `true`                          | `false` で設定を残したまま無効にする                                              |

**レベルの指定**

画面とウィジェットに `level`（必要な最低レベル）を書きます。

```json
{ "name": "settings", "level": 2, "children": [ ... ] }

{ "type": "button", "text": "Start", "level": 1, "on_click": { "action": "set", "tag": "run", "value": true } }
{ "type": "button", "text": "Reset", "level": 3, "level_deny": "hide", "on_click": { ... } }
{ "type": "numeric_display", "input": true, "bind": "setpoint", "level": 2, "level_deny": "login" }
```

- **画面の `level`**: レベルが足りないユーザーがその画面へ移動しようとすると、ログイン画面が出ます。
  十分なレベルでログインすると、そのまま目的の画面へ進みます。ログアウトしたとき（自動ログアウトを含む）に
  表示中の画面が見られなくなった場合は、開始画面へ戻ります。
- **ウィジェットの `level`**: レベルが足りないあいだの扱いを `level_deny` で選びます。

  | `level_deny`       | レベルが足りないとき                                               |
  | ------------------ | ------------------------------------------------------------------ |
  | `disable`（既定）  | 無効表示（薄くなる）になり、操作できない                           |
  | `hide`             | 表示しない                                                         |
  | `login`            | 見た目は通常どおり。触るとログイン画面が出る（操作は行われない）   |

  `level` はそのウィジェット自身にかかります。パネルに付けた場合、`hide` は中身ごと隠れますが、
  `disable` は中のウィジェットには及ばないので、操作させたくないウィジェットそれぞれに付けてください。
- **システム設定画面**は `system_level` 以上でないと開けません（鍵の管理やドライバ設定を含む全ページ）。

**ログイン**

ログイン画面は組み込みです。左の一覧からユーザーを選び、テンキーで PIN を入れて OK を押します。
`{ "action": "login" }` で開けるほか、レベルが必要な操作をしたときに自動で出ます。ログイン中は右上に
Logout ボタンが出ます。`{ "action": "logout" }` でもログアウトできます。

画面からは次のタグを使えます（OPC UA には公開されません）。

| タグ                  | 内容                                          |
| --------------------- | --------------------------------------------- |
| `sys.user.name`       | ログイン中のユーザー名（いなければ空）        |
| `sys.user.level`      | レベル（`0` = ログインしていない）            |
| `sys.user.level_name` | レベルの名前                                  |
| `sys.user.logged_in`  | ログイン中なら true                           |

たとえば `"bind": { "visible": { "tag": "sys.user.level", "ge": 2 } }` で、表示だけを切り替えることもできます。

**装置の画面でユーザーを編集する**

システム設定の Users ページで、ユーザーの追加・変更（レベル、PIN）・削除ができます。

- 追加: Name・Level・PIN を入れて Save。変更: 一覧の Edit で Name と Level が入るので、直して Save
  （PIN を空のままにすると、今の PIN のまま）。削除: Delete を押し、確認でもう一度 Delete。
- システム設定を開けるユーザー（`system_level` 以上）の最後の 1 人は、削除もレベルを下げることもできません。
- 装置で編集すると、その装置は自分のユーザー一覧（`project.local.json` の `users`）を使うようになり、
  **project.json の `list` は使われなくなります**。デプロイしても装置の一覧は残ります。デザイナーの Deploy
  ダイアログにその旨が表示され、「local settings」にチェックを入れてデプロイすると project.json の一覧に戻せます。

**PIN の保存について**

- デザイナーと装置の画面で設定した PIN は、ソルト付きの SHA-256 ハッシュ（`pin_hash`）で保存され、
  ファイルに数字は残りません。手で書くときは `"pin": "1234"` と平文で書くこともできます。
- PIN は桁数が少ないので、ハッシュは「うっかり読めてしまう」ことを防ぐ程度のものです。プロジェクトの
  ファイルを手に入れた人は総当たりで PIN を割り出せます。強い秘密を守る仕組みではありません。
- ログイン画面で押したキーは操作ログに記録されません。

レベルや `auto_logout_s` などの設定の変更は、ランタイムの再起動で反映されます。

## システム設定画面

ランタイムは project.json とは別に、組み込みの「System」メニュー画面（`__system`）と設定ページ画面
（`__system_<id>`）を自動生成します。画面のボタンから `{ "action": "system" }` で開けます。

| ページ | 内容 |
|---|---|
| About | ホスト名、IP アドレス、プロジェクト、表示バックエンド、ビルド情報（表示のみ） |
| Network | NetworkManager (`nmcli`) のある Linux で、インターフェース、DHCP / 固定 IP、アドレス (CIDR)、ゲートウェイ、DNS を設定。Apply で `nmcli connection modify` + `up` を実行（一般ユーザーでは polkit が認証を求めるので `sudo -n` で再試行。`tools/rpi/hml.sudoers` を入れておく）。接続中の IP を変えると SSH が切れます。Windows では表示のみ |
| Keys | SSH の鍵の管理（Linux の fbdev / drm バックエンドで表示）。デプロイに使う鍵の登録・削除 →「鍵の管理」 |
| Users | ユーザーの追加・変更・削除（project.json に `users` があるとき）→「ユーザー管理」 |
| Driver: … | project.json の各ドライバ（1 ドライバ 1 ページ）。`enabled` と、`host` / `port` / `poll_ms` などのスカラー設定を編集。Apply で `project.local.json` に保存し、全ドライバを再起動 |

設定値は `sys.<page>.<key>` というタグに入り、Apply で各ページの `apply()` が呼ばれます（結果は `sys.<page>.status`）。
`project.local.json` は project.json と同じ場所に書かれ、起動時に `drivers` のスカラー値を上書きします
（project.json 自体は変更しません。OPC UA には `sys.*` タグは公開されません）。
保存されるのは project.json と値が違う項目だけです。project.json と同じ値に戻して Apply するとその項目は消え、
何も残らなければファイルごと削除されます（変更していない項目は、あとから project.json を書き換えれば反映されます）。

### 鍵の管理（Keys ページ）

デザイナーからのデプロイは SSH の鍵でログインします。Keys ページでは、この装置にログインできる鍵
（ランタイムを実行しているユーザーの `~/.ssh/authorized_keys`）を、装置の画面から管理できます。
新しい PC からデプロイしたいときや、鍵をなくしたときに、SSH で入らなくても鍵を登録できます。

**新しい鍵を登録する**

1. 装置の画面で System > Keys を開き、**Key management** をオンにします（鍵管理モード）。
   画面に、この装置の IP アドレスとポート番号が表示されます。
2. PC のデザイナーで Deploy ダイアログを開き、private key の右の **Register…** を押します。
   private key が空（またはファイルがない）ときは、新しい鍵（`~/.ssh/hml_deploy_ed25519`）を作ります。
   デザイナーに、送った鍵のフィンガープリント（`SHA256:...`）が表示されます。
3. 装置の画面に、送信元のアドレス、鍵のフィンガープリント、コメントが表示されます。
   **デザイナーに表示されたフィンガープリントと同じことを確かめてから** Accept を押します。Reject で拒否できます。

- 鍵管理モードがオフのあいだは、受付用のポートは閉じていて、外部から鍵を送ることはできません。
  モードは 5 分で自動的にオフになります（ランタイムを再起動したときもオフに戻ります）。
- 送られてきた鍵は、画面で Accept を押すまで登録されません。別の画面を表示していても、鍵が届くと Keys ページに切り替わります。
  2 分以内に答えなければ、その申し出は取り消されます。
- 受け付けるのは、形式の正しい公開鍵（ed25519 / rsa / ecdsa）だけです。
- 通信は暗号化していません（公開鍵は秘密の情報ではありません）。他人の鍵を登録してしまわないことは、
  画面でのフィンガープリントの確認が保証します。

**登録済みの鍵を削除する**

登録済みの鍵が、種類・フィンガープリント・コメントで一覧表示されます（8 個まで）。Delete を押すと確認が出て、
もう一度 Delete で削除します。最後の 1 個も削除できますが、新しい鍵を登録するまで鍵でのログイン（デプロイ）はできなくなります。
`authorized_keys` のコメント行や、オプション付きの行（`command="..."` など）は、そのまま残します。

```json
"key_management": { "enabled": true, "port": 4855, "file": "" }
```

| キー      | 既定値                                    | 内容                                                                  |
| --------- | ----------------------------------------- | --------------------------------------------------------------------- |
| `enabled` | fbdev / drm では `true`、SDL では `false` | Keys ページを出すかどうか（PC では開発者自身の鍵ファイルを編集してしまうので既定でオフ） |
| `port`    | `4855`                                    | 鍵管理モードのあいだだけ開く TCP ポート                               |
| `file`    | `~/.ssh/authorized_keys`                  | 管理する鍵ファイル（相対パスは project.json から）                    |

コマンドラインからは `hml_designer --send-key <ホスト> [--key <秘密鍵>] [--key-port 4855]` で鍵を送れます。

ページを追加するには `src/system_settings.hpp` の `SettingsPage`（`id` / `title` / `fields()` / `apply()`）を
実装して `register_builtin_pages()` で登録するだけで、画面とタグは自動で作られます。フィールド型は
Text / Number / Bool / Select / Info。フォーム以外の画面が必要なページは、`custom_rows()`（ウィジェットを自分で並べる）、
`command()`（`{ "action": "call", "fn": "system.do", "arg": "<page>:<command>" }` で呼ばれる）、`poll()`（定期的に呼ばれる）を
実装します（例: `src/system_keys.cpp`）。

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
| `level`, `level_deny`                                                   | 操作に必要なユーザーレベルと、足りないときの扱い（`disable` / `hide` / `login`）→「ユーザー管理」                          |
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
  { "action": "login" },
  { "action": "logout" },
  { "action": "call", "fn": "system.apply", "arg": "network" },
  { "action": "script", "code": "if tag.temp > tag.temp_hi then tag.run = false end" },
  { "action": "script", "name": "reset_counters" }
]
```

`script` は Lua スクリプトを実行します。`code` にその場で書くか、`name` で project.json の `scripts` に
定義したスクリプトを呼びます（下記「スクリプト (Lua)」）。

`login` はログイン画面を開き、`logout` はログアウトします（→「ユーザー管理」）。

`system` は組み込みのシステム設定画面（下記）を開きます。`call` はランタイムに登録された名前付きアクションを呼びます。

`restart` は Raspberry Pi（fbdev / drm バックエンド）では OS を再起動し、PC（SDL バックエンド）ではアプリを
再起動します。`"what": "app"` / `"system"` で明示もできます。OS の再起動は `systemctl reboot` を `sudo -n`
経由で呼ぶので、サービスを一般ユーザーで動かしている Pi では一度だけ次を実行してパスワードなしで許可してください
（許可がない場合はアプリの再起動にフォールバックします）。

```
sudo install -m 440 tools/rpi/hml.sudoers /etc/sudoers.d/hml
```

`anim`: `none` / `fade` / `slide_left` / `slide_right` / `slide_top` / `slide_bottom` / `over_left` / `over_right`

## スクリプト (Lua)

固定のアクション（`set` / `inc` / `goto` …）では書けない条件分岐や計算は、Lua 5.4 のスクリプトで書けます。
project.json の `scripts` に定義し、`trigger` で実行のタイミングを選びます。

```json
"scripts": [
  { "name": "init",       "trigger": "startup",  "code": "total = 0" },
  { "name": "totalizer",  "trigger": "interval", "interval_ms": 1000,
    "code": "if tag.run then total = total + tag.flow / 3600  tag.total = total end" },
  { "name": "temp_alarm", "trigger": "tag", "tags": ["temp", "temp_hi"],
    "code": ["local high = tag.temp > tag.temp_hi",
             "if high and not tag.alarm then log('temperature high: ' .. tag.temp) end",
             "tag.alarm = high"] },
  { "name": "enter_setup", "trigger": "screen_show", "screen": "settings", "code": "tag.pin = ''" },
  { "name": "reset_counters", "code": "total = 0  tag.total = 0" },
  { "name": "recipes", "trigger": "startup", "file": "scripts/recipes.lua" }
]
```

| `trigger`                     | 実行されるとき                                                                                                   |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `startup`                     | 起動時に 1 回（最初の画面を表示する前、定義順）。共通の関数や変数の定義に使う                                    |
| `interval`                    | `interval_ms` ミリ秒ごと（既定 1000、最小 10）                                                                   |
| `tag`                         | `tags`（配列）のどれかの値が変わったとき。`event.tag` / `event.value` で変化したタグが分かる                     |
| `screen_show` / `screen_hide` | 画面が表示された / 別の画面に切り替わったとき。`screen` で画面を限定（省略時はすべての画面）。`event.screen` に画面名 |
| `manual`（既定）              | 自動では実行しない。`{ "action": "script", "name": "..." }` や Lua の `run("...")` から呼ぶ                      |

- コードは `code`（文字列、または行の配列）か `file`（project.json からの相対パスの `.lua` ファイル）で指定します。
  `"enabled": false` で無効化できます。
- スクリプトは画面と同じくメインスレッドで実行されます。全スクリプトが 1 つの Lua ステートを共有するので、
  `startup` で定義したグローバル変数・関数を他のスクリプトから使えます。
- ホットリロード（F5 / `SIGHUP`）でスクリプトも読み直されます。Lua のグローバル変数は初期化され、`startup`
  スクリプトがもう一度実行されます（タグの値は維持されます）。
- エラー（文法エラー、未定義のタグなど）はログに `[hml ERROR] script '<name>': <name>:<行>: ...` と出力され、
  そのスクリプトの実行だけが中断されます。500 ms を超えて動き続けるスクリプト（無限ループなど）も打ち切られます。
- `tag` トリガーのスクリプトが自分で書いたタグの変化では、自分自身は再実行されません（他のスクリプトは実行されます）。

### Lua から使えるもの

| API                                   | 内容                                                                                                    |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `tag.name` / `tag["sys.about.host"]`  | タグの読み書き。bool タグは `true` / `false`、int は整数、float は実数、string は文字列。書き込みはタグの型に変換され、`min` / `max` も適用される。未定義のタグはエラー |
| `goto_screen("name" [, "anim"])`      | 画面を切り替える（`anim` 省略時は `fade`）。画面がなければ `false` を返す                               |
| `back()` / `current_screen()`         | 前の画面に戻る / 表示中の画面名                                                                         |
| `run("name")`                         | 別のスクリプトを実行                                                                                    |
| `call("fn" [, "arg"])`                | `call` アクションと同じ（ランタイムに登録された名前付きアクション）                                     |
| `log(...)` / `print(...)`             | ログに出力（`[hml INFO ] [script] ...`）                                                                |
| `warn(...)`                           | 警告としてログに出力（`[hml WARN ] [script] ...`）                                                      |
| `millis()`                            | 起動からの経過ミリ秒                                                                                    |
| `os.time()` / `os.date(fmt)` / `os.clock()` | 時刻（`os.date("%H:%M:%S")` など）                                                                |
| `event`                               | 実行のきっかけ: `event.trigger`（`startup` / `interval` / `tag` / `screen_show` / `screen_hide` / `action`）、`event.tag`、`event.value`、`event.screen` |
| 標準ライブラリ                        | `string` / `table` / `math` / `utf8` / `coroutine` と基本関数。ファイル・プロセス操作（`io`、`os.execute`、`require`、`load` など）は使えません |

bool タグは Lua の真偽値です。`if tag.run then` と書いてください（`tag.run == 1` は常に偽になります）。

デモプロジェクトの `auto_speed` スクリプト（Auto mode かつ Run のとき、温度に応じて速度設定値を動かす）が例です。

## ロギング

変数（タグ値）、アラーム、オペレーターの操作を記録できます。どれも project.json に設定を書いたものだけが
有効になり、それぞれ別の SQLite データベース（`logs/` フォルダ）に記録され、CSV で取り出せます。
ランタイム自身のメッセージ（スクリプトのエラーなど）の記録は、次の「システムログ」です。

共通の設定キー:

| キー             | 既定値                                   | 内容                                         |
| ---------------- | ---------------------------------------- | -------------------------------------------- |
| `enabled`        | `true`                                   | `false` で設定を残したまま記録を止める       |
| `path`           | `logs/data.db` / `alarm.db` / `operation.db` | データベースファイル（project.json からの相対） |
| `retention_days` | `30`                                     | これより古い行を削除（`0` = 無期限）         |
| `max_rows`       | 変数 `1000000`、ほかは `100000`          | これを超えたら古い行から削除（`0` = 無制限） |

書き込みは約 1 秒ごとにバックグラウンドでまとめて行います（電源断で直前の約 1 秒分が失われることがあります）。
設定の変更はランタイムの再起動で反映されます。

### 変数ロギング (`data_log`)

タグをグループにまとめ、グループごとに記録のタイミングを決めます。1 回の記録（サンプル）には、その時点の
グループ内の全タグの値が入ります。

```json
"data_log": {
  "groups": [
    { "name": "process", "trigger": "interval", "interval_ms": 1000, "tags": ["temp", "pressure", "level"] },
    { "name": "state",   "trigger": "change", "tags": ["run", "mode"] },
    { "name": "batch",   "trigger": "tag", "trigger_tag": "batch_done", "tags": ["total", "recipe"] },
    { "name": "report",  "trigger": "manual", "tags": ["total"] }
  ]
}
```

| `trigger`          | 記録されるとき                                                                                   |
| ------------------ | ------------------------------------------------------------------------------------------------ |
| `interval`（既定） | `interval_ms` ミリ秒ごと（既定 1000、最小 100）                                                  |
| `change`           | グループ内のタグのどれかが変化したとき。`interval_ms` を付けると、その時間内は 1 回にまとめる    |
| `tag`              | `trigger_tag` がオフ（0 / 空）からオン（0 以外）になったとき                                     |
| `manual`           | `{ "action": "call", "fn": "data_log.sample", "arg": "report" }`（Lua: `call("data_log.sample", "report")`）で記録 |

CSV はグループごとに、時刻 + タグごとの列になります（bool は 1 / 0）。

```
hml_runtime --export-data-log process.csv projects/demo/project.json --group process
```

画面からは `{ "action": "call", "fn": "data_log.export", "arg": "process" }`（`arg` は `グループ` または
`グループ,出力先`。出力先を省くとデータベースと同じフォルダ、`arg` が空なら全グループ）。

### アラーム (`alarms`)

タグに対する条件を定義します。条件が成立したとき（発生 `raised`）と成立しなくなったとき（復帰 `cleared`）に
1 行ずつ記録されます。

```json
"alarms": {
  "items": [
    { "name": "temp_high", "tag": "temp", "condition": "gt", "value_tag": "temp_hi",
      "severity": "warning", "message": "Temperature high" },
    { "name": "level_low", "tag": "level", "condition": "lt", "value": 10, "message": "Tank level low" },
    { "name": "estop", "tag": "estop", "severity": "critical", "message": "Emergency stop" }
  ]
}
```

| キー                  | 内容                                                                                               |
| --------------------- | -------------------------------------------------------------------------------------------------- |
| `name` / `tag`        | アラーム名（一意）と、監視するタグ                                                                 |
| `condition`           | `true`（既定: タグがオン / 0 以外）、`false`、`eq` `ne` `gt` `lt` `ge` `le`（`==` `!=` `>` `<` `>=` `<=` も可） |
| `value` / `value_tag` | 比較する値。`value_tag` を書くと、別のタグの現在値と比較する（設定値タグをしきい値にできる）       |
| `severity`            | `warning`（既定）/ `critical` / `info` など。記録されるだけで、動作は変わらない                    |
| `message`             | 記録するメッセージ（省略時はアラーム名）                                                           |

- 起動時にすでに成立しているアラームは、起動時刻で発生として記録されます。
- 記録される列は、時刻、名前、`raised` / `cleared`、重要度、メッセージ、そのときのタグ値です。
- 確認（アクノリッジ）と、画面上のアラーム一覧はまだありません。

```
hml_runtime --export-alarm-log alarms.csv projects/demo/project.json
```

画面からは `{ "action": "call", "fn": "alarm_log.export" }`（`arg` で出力先を指定可）。

### 操作ログ (`operation_log`)

オペレーターが画面で行った操作を記録します。

```json
"operation_log": { "user_tag": "operator" }
```

| 記録される操作（`kind`）        | 内容                                                                                          |
| ------------------------------- | --------------------------------------------------------------------------------------------- |
| `write`                         | スイッチ・スライダー・入力欄などからのタグ書き込みと、`set` / `toggle` / `inc` / `dec` アクション。変更前と変更後の値 |
| `screen`                        | `goto` / `back` アクションによる画面切り替え                                                  |
| `script` / `call` / `system`    | イベントから実行したスクリプト、`call` アクション、`reload` / `exit` / `restart`              |

- ユーザー管理（`users`）を使っているときは、ログイン中のユーザー名が各行に入り、ログイン・ログアウト・
  ログイン失敗・ロックも `kind` = `user` の行として記録されます（`user_tag` は使われません）。
- `user_tag` に文字列タグ（ログイン中のオペレーター名など）を指定すると、その値が各行に記録されます。
  各行には、操作した画面の名前も入ります。
- 同じタグへの書き込みが 1 秒以内に続いたとき（スライダーのドラッグ、+ ボタンの連打）は 1 行にまとめ、
  最初の変更前の値と最後の変更後の値を記録します。
- パスワード入力欄に入力した内容は記録しません（`***` になります）。
- 値が変わらなかった書き込み、スクリプトの中でのタグ書き込み、ドライバ（PLC）からの変化は記録しません。

```
hml_runtime --export-operation-log operations.csv projects/demo/project.json
```

画面からは `{ "action": "call", "fn": "operation_log.export" }`（`arg` で出力先を指定可）。

## システムログ

スクリプトのエラー、ドライバの接続・切断、プロジェクトの読み込み結果など、ランタイムがコンソールに出す
メッセージ（`[hml INFO ]` / `[hml WARN ]` / `[hml ERROR]`）を SQLite データベースに記録し、あとから CSV で
取り出せます。project.json に `system_log` を書くと有効になります（書かなければ記録しません）。

```json
"system_log": { "path": "logs/system.db", "level": "info", "retention_days": 30, "max_rows": 100000 }
```

| キー             | 既定値        | 内容                                                                      |
| ---------------- | ------------- | ------------------------------------------------------------------------- |
| `enabled`        | `true`        | `false` で設定を残したまま記録を止める                                    |
| `path`           | `logs/system.db` | データベースファイル（project.json からの相対パス。フォルダは自動で作成） |
| `level`          | `info`        | 記録する最低レベル: `info` / `warn` / `error`                             |
| `retention_days` | `30`          | これより古い行を削除（`0` = 無期限）                                      |
| `max_rows`       | `100000`      | これを超えたら古い行から削除（`0` = 無制限）                              |

記録される列は、時刻、レベル、種別（`source`: `script` / `driver` / `system`）、メッセージ、回数です。

- 同じメッセージが連続したとき（周期スクリプトが毎回同じエラーになる、など）は 1 行にまとめ、回数と最後の時刻を
  更新します。
- 書き込みは約 1 秒ごとにバックグラウンドでまとめて行います（画面の動作を止めず、SD カードへの書き込み回数も
  抑えます）。電源断でファイルが壊れることはありませんが、直前の約 1 秒分は失われることがあります。
- Lua の `log(...)` / `warn(...)` と、スクリプトのエラーも記録されます。
- 設定の変更はランタイムの再起動で反映されます（ホットリロードでは反映されません）。
- OPC UA ライブラリ（open62541）が直接コンソールに出すメッセージは記録されません。

### CSV に取り出す

```
hml_runtime --export-system-log out.csv projects/demo/project.json
hml_runtime --export-system-log errors.csv projects/demo/project.json --log-level warn
```

HMI の実行中でも別プロセスとして実行できます。HMI の画面から取り出すには `call` アクションの `system_log.export` を
使います（`arg` が出力先。省略するとデータベースと同じフォルダに `system-log-日付-時刻.csv`）。

```json
"on_click": { "action": "call", "fn": "system_log.export", "arg": "/media/usb/system-log.csv" }
```

Lua からは `call("system_log.export", "/media/usb/system-log.csv")` です。CSV は UTF-8（BOM 付き）で、Excel でそのまま
開けます。列は `time,level,source,message,count,last_time` です。データベースは SQLite なので、`sqlite3` コマンドや
DB Browser for SQLite でも直接見られます（テーブル `log`）。

## ソース構成

| ファイル                       | 役割                                                                                                |
| ------------------------------ | --------------------------------------------------------------------------------------------------- |
| `src/main.cpp`                 | 起動、コマンドライン、メインループ、テスト自動化（`--click` など）                                  |
| `src/platform.hpp`             | 表示 / 入力バックエンドの共通インターフェース（`hml::platform`）                                    |
| `src/platform_sdl.cpp`         | SDL2 バックエンド（Windows / デスクトップ）: ウィンドウ、F5 / Esc、スクリーンショット               |
| `src/platform_linux.cpp`       | Linux fbdev / DRM + evdev バックエンド（Raspberry Pi）: シグナル制御、スナップショット              |
| `src/tag.[ch]pp`               | `hml::Tag` / `hml::TagStore`: タグストア（スレッドセーフ、変更通知、書き込み元の追跡）              |
| `src/ui.[ch]pp`                | `hml::Ui`: JSON → LVGL ウィジェット、バインディング、アクション、画面遷移                           |
| `src/system_log.[ch]pp`        | `hml::SystemLog`: システムログ（SQLite への記録、検索、CSV エクスポート）                            |
| `src/key_service.[ch]pp`       | `hml::KeyService`: SSH の鍵の管理（authorized_keys の一覧・追加・削除、鍵管理モードでの受付）        |
| `src/users.[ch]pp`             | `hml::Users`: ユーザー管理（ユーザーとレベル、PIN ログイン、ロック、自動ログアウト）                 |
| `src/system_users.cpp`         | ログイン画面（`__login`）と、システム設定の Users ページ                                             |
| `src/system_keys.cpp`          | システム設定の Keys ページ（`KeyService` の画面）                                                    |
| `src/logdb.[ch]pp`             | `hml::LogDb`: ロギング共通の SQLite 記録（バックグラウンド書き込み、保持日数・件数、CSV 出力）      |
| `src/data_log.[ch]pp`          | `hml::DataLog`: 変数ロギング（グループ、周期 / 変化 / タグ / 手動トリガー）                         |
| `src/alarm.[ch]pp`             | `hml::Alarms`: アラームの判定と発生・復帰の記録                                                     |
| `src/operation_log.[ch]pp`     | `hml::OperationLog`: 操作ログ（ウィジェットからの書き込み、イベントのアクション）                   |
| `src/script.[ch]pp`            | `hml::ScriptEngine`: Lua スクリプト（トリガー、タグ / 画面 API、サンドボックス、タイムアウト）      |
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

ランタイム本体（tag / ui / project / driver / script / ロギング）は LVGL、nlohmann/json、Lua、SQLite と C++17 標準ライブラリ
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
- **Lua の関数を追加**: `script.cpp` の `ScriptEngine::Impl::ensure_state()` の `funcs` に登録します
  （デザイナーの Scripts タブに出るヘルプは `panels.js` の `SCRIPT_HELP`）。

### スレッドモデル

- LVGL と UI バインディング、Lua スクリプトは常にメインスレッドで動作します。
- ドライバは任意のスレッドから `Tag::set_number()` / `set_string()` を呼べます。変更はメインループの
  `TagStore::dispatch()` で購読者（ウィジェット・ドライバ）に配信されます。
- 各書き込みには「書き込み元」が付くため、スライダー自身の書き込みがスライダーに戻って
  ドラッグ中に跳ねる、Modbus ドライバが自分で読んだ値を PLC に書き戻す、といったループは起きません。
