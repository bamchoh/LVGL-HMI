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
               ├── drivers   : sim / modbus_tcp / modbus_rtu / modbus_ascii がタグを読み書き
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
| `test_driver_slmp.cpp`                               | SLMP（デバイスの書き方、要求と応答のバイト列、壊れた・他のプロトコルの・長すぎる応答とランダムなバイト列の拒否、疑似サーバーに対する読み書き（全データ型・3E / 4E）、書き込みの制限、終了コード・無応答・切断からの復帰、1000 点のまとめ読み） |
| `test_driver_mqtt.cpp`                               | MQTT（パケットのバイト列、残り長さの境界、壊れたパケットの拒否と 10 万個のランダムなパケット、疑似ブローカーに対する送信・状態トピックと遺言・指令の受信、**指令の検査**（範囲外・数値でない・長すぎる・UTF-8 でない・登録していないトピック・ワイルドカード）、**TLS**（正しい CA で接続、別の CA・別のホスト名の証明書を拒否、verify なしの警告、平文の拒否、パスワードがログに出ない）、拒否からの再試行、切断中は最新の値だけ送る、QoS 1 の再送、停止がすぐ終わる） |
| `test_driver_opcua_client.cpp`                       | OPC UA クライアント（同じプロセスの open62541 サーバーを相手に: ノードの読み込みとサブスクリプション、`rw` の行だけ書き込み、**署名と暗号化**、**別の証明書のサーバーを拒否**、verify なしの警告、信頼する証明書がないと起動しない、平文で暗号化専用サーバーにつながらない、ユーザー名とパスワード（間違いの拒否、ログに出ない）、型に入らない値を書かない、サーバーの停止と復帰、停止がすぐ終わる、装置の証明書を 1 度だけ作る） |
| `test_driver_modbus.cpp`                             | テスト内の疑似 Modbus TCP サーバーに対するエンドツーエンド（全エリア・全データ型・書き込み・再接続）                        |
| `test_driver_modbus_serial.cpp`                      | Modbus RTU / ASCII（フレームと CRC・LRC、複数ユニット、壊れた応答、無応答からの復帰、疑似シリアルポート） |
| `test_driver_opcua.cpp`                              | open62541 クライアント API で OPC UA サーバードライバを読み書き                                                             |
| `test_system_log.cpp`                                | システムログ（SQLite への記録、繰り返しの集約、保持件数、CSV エクスポート、スレッドからの書き込み）                         |
| `test_recipe.cpp`                                    | レシピ（読み込み・選択・書き込み・保存・値の変更・削除、範囲外なら何も書かない、書き込み後の確認、装置での保存と再起動後の読み戻し、壊れたファイル・書けないとき、CSV の数式エスケープと取り込みの検証・大きさの上限、名前とブック名（パスにならない）、ランダムなバイト列、レベル、レシピ表の部品と確認ダイアログ）、`write_file_atomic` |
| `test_alarm_view.cpp`                                | アラームの確認（発生・復帰・確認の全順序）、再起動後の状態、履歴のページ送りと絞り込み、古い DB への列追加、確認のレベルと記録、一覧とバナー、1000 件の性能、SQL インジェクション・制御文字・不正な UTF-8 |
| `test_trend.cpp`                                     | トレンド（区間ごとの最小・最大・最後、短いピークが消えない、bool・文字列・点を含むタグ名、1 分ごとの要約の書き込み、要約と記録の組み合わせが記録だけの結果と一致、名前の SQL インジェクション、100 万件の速さ、部品の移動・拡大縮小・凡例・存在しないグループ） |
| `test_logs.cpp`                                      | 変数ロギング（各トリガー）、アラーム（発生 / 復帰）、操作ログ（ウィジェット・アクション、パスワードの伏せ字）、CSV エクスポート |
| `test_key_service.cpp`                               | 鍵の管理（SHA-256 / フィンガープリント、公開鍵の検証、authorized_keys の編集、受付プロトコル、Keys ページ）       |
| `test_popup.cpp`                                     | ポップアップ（開く・モーダル・重ねる・名前を指定して閉じる・戻るで閉じる、画面の切り替えで閉じる、goto でも開く、ポップアップでない画面は開かない、閉じている間は更新しない、レベル、1000 回の開閉で部品が増えない） |
| `test_screen_saver.cpp`                              | 画面消灯（時間で消える・0 は消さない、**点けるタッチではボタンが押されない**（模擬タッチパネルで確認）、タグでの切り替え、ログアウト、アラームで点灯 / しない設定、ポップアップも覆う、装置での時間の変更と `project.local.json`） |
| `test_texts.cpp`                                     | 多言語（`@ID` の解決・既定の言語へのフォールバック・`@@`・存在しない ID、`sys.lang` での切り替えと不正な値の差し戻し、部品・選択肢・アラームの言語、切り替えで画面を作り直す）、フォント（どの大きさでも作れる・ない文字は組み込みフォント・`cmap` と `hmtx` の読み取り・壊れた / 切り詰めた / ランダムなフォントファイル、デモ全画面の描画） |
| `test_users.cpp`                                     | ユーザー管理（ログイン・ロック・自動ログアウト、ウィジェットと画面のレベル、ログイン画面、装置上でのユーザー編集） |
| `test_project_archive.cpp`                           | プロジェクトファイル（構成ファイルの一覧、作成と展開、プロジェクト外へ出るパスの拒否、PC ごとのファイルの保護）     |
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
- 画面に触らずにタッチ操作を試すには `tools/rpi/tap.py` を使います。入力デバイスにタッチのイベントを書き込むので、
  ランタイムには本物のタッチとして届きます（実行するユーザーが `input` グループに入っている必要があります）。
  座標は画面上の位置で、表示サイズとタッチ補正は `--project` で指定した project.json から読みます。
  `python3 tools/rpi/tap.py --project projects/lcd35/project.json 440,20,900 95,70`
  （`X,Y,待ち時間ms` を並べる）。結果は `cat /dev/fb0 > shot.raw` で画面を取得して確認できます。

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
| `--stats <seconds>`          | メインループの負荷を指定秒ごとにログに出す（1 秒あたりの周回数、占有率、最長の 1 周の時間）。負荷の確認用 →「負荷の確認」 |
| `--list-serial-ports`        | その機械のシリアルポートを一覧表示して終了（project.json は不要）→「ドライバ: modbus_rtu / modbus_ascii」 |
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

### 負荷の確認

規模の大きいプロジェクトで装置がどのくらい忙しくなるかは、`--stats` で確認できます。

```
hml_runtime project.json --stats 5
[hml INFO ] stats: 5.0 s, 724 passes (145/s), busy 20%, longest pass 31.2 ms, 0 of 50 ms or more; tags avg 0.79 max 20.1 ms, lvgl avg 0.62 max 11.1 ms
```

- メインループの 1 周は「タグ変化の通知（`tags`）」と「LVGL の処理（`lvgl`: タイマー、入力、描画）」です。
  1 周のあいだは他の処理が待たされるので、`longest pass` がタッチやタグ変化に対する最悪の遅れの目安になります。
- `busy` はメインループが処理に使っている時間の割合です。100% に近づくと、周回数（`passes`）が落ちて表示の更新が遅れます。

規模が大きくても重くなりにくいよう、次のようにしています。

- 表示していない画面のウィジェットは、タグが変化しても更新しません。その画面を表示するときにまとめて反映します。
- 画面は、最初に表示するときに組み立てます。起動時は開始画面だけを組み立て、残りは動作中に 1 枚ずつ組み立てます
  （画面 JSON の誤りによる警告は、起動後しばらくのあいだに出ます）。

`tools/make_stress_project.py` は、タグ数・画面数・ウィジェット数・ロギング量などを指定して負荷確認用のプロジェクトを作ります
（`--preset light | medium | heavy | extreme`、個別のオプションで上書き可）。外部とは通信しません。

```
python tools/make_stress_project.py stress --preset heavy
hml_runtime stress/project.json --stats 5
```

## デザイナー（GUI でプロジェクトを作る）

`hml_designer.exe`（Windows ビルドでは既定で一緒にビルドされます）は、`project.json` と画面 JSON を
GUI で編集するツールです。追加のランタイムは不要で、OS 標準の WebView（Windows: WebView2、
Windows 10/11 に標準搭載）に HTML/JS の画面を表示し、C++ のホストがファイル入出力とランタイムの起動を担当します。

```
build\bin\hml_designer.exe                       # 起動（New / Open で開始）
build\bin\hml_designer.exe projects\demo\project.json   # プロジェクトを指定して起動
build\bin\hml_designer.exe line1.hmlproj                  # プロジェクトファイル（1 ファイルの形式）を指定して起動
```

| 場所                                   | 内容                                                                                                                                                               |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 左: Widgets                            | パレット。クリックすると選択中のパネル（または画面）に追加                                                                                                         |
| 左: Screens / Tags / Drivers / Project | 画面の追加・削除・開始画面、タグ表、ドライバ設定（sim / modbus_tcp / modbus_rtu / modbus_ascii / opcua_server のフォーム + raw JSON）、表示サイズ・テーマ・タッチ補正                          |
| 中央: キャンバス                       | ドラッグで移動、右下のハンドルでサイズ変更、矢印キーで 1 px（Shift で 10 px）移動。flex レイアウトの子は親の設定に従うので移動不可                                 |
| 右: ツリー + プロパティ                | 階層と順序（▲▼ で前後、⇥⇤ でパネルに出し入れ）、位置 / サイズ / フラグ / スタイル（セレクタごと）/ バインド / イベント（アクション列）                             |
| Preview                                | 保存してから `hml_runtime` をウィンドウなし（`SDL_VIDEODRIVER=dummy`）で起動し、その画面のスクリーンショットを表示。キャンバスは近似表示なので、最終確認はこちらで |
| Run                                    | 保存して `hml_runtime` を通常起動                                                                                                                                  |
| Deploy                                 | 保存して、プロジェクト（と任意でランタイム）を SSH でターゲット（Raspberry Pi など）へ送り、ランタイムを再起動 →「ターゲットへのデプロイ」                        |
| 中央: Logs タブ                        | 変数ロギング（グループ）、アラーム、操作ログ、システムログの設定。チェックを入れたものだけ記録される     |
| 中央: Users タブ                       | ユーザー管理: レベル名、ユーザー（名前・レベル・PIN）、自動ログアウトやロックの設定 →「ユーザー管理」   |
| 中央: Scripts タブ                     | Lua スクリプトの追加・削除、トリガー（起動時 / 周期 / タグ変化 / 画面の表示・非表示 / manual）とコードの編集。ウィジェットのイベントには `script` アクションで割り当てる |
| JSON                                   | 現在の画面 JSON を直接編集して適用                                                                                                                                 |

### プロジェクトの保存形式

プロジェクトは 2 つの形で保存できます。Save As のダイアログで選びます（既定はプロジェクトファイル）。

| 形式                 | 内容                                                                                           |
| -------------------- | ---------------------------------------------------------------------------------------------- |
| プロジェクトファイル | `<名前>.hmlproj` の 1 ファイル。project.json・画面・スクリプト・画像をまとめた zip 形式        |
| フォルダー           | `project.json` と、その隣の `screens/`、`assets/`（これまでの形）                              |

- **プロジェクトファイルは 1 つで完結します。** USB メモリやメールで持ち運んだり、そのままバージョン管理に入れたりできます。
  中身は通常の zip なので、拡張子を `.zip` にすれば一般のツールで開けます。
- デザイナーの Open / Save / Run / Preview / Deploy は、どちらの形でも同じように使えます。
  フォルダーのプロジェクトを開いて Save As でプロジェクトファイルにする（またはその逆）と、画像などのファイルも一緒に移ります。
- ランタイムにも直接渡せます: `hml_runtime line1.hmlproj`
- **入るのはプロジェクトを構成するファイルだけです**（project.json、そこや画面から名前で参照されているファイル、`assets/` の中身）。
  記録されたログや、その PC だけの設定（`project.local.json`）は入りません。
- プロジェクトファイルは、開くときに作業用フォルダーへ展開されます（Windows: `%LOCALAPPDATA%\hml\work\<名前>-<識別子>`、
  Linux: `~/.cache/hml/work/...`）。Run したときのログや「device on this PC」などの PC ごとの設定はそこに置かれ、
  プロジェクトファイルには書き戻されません。画像を追加したときも、Save するまでプロジェクトファイルには入りません。
- デプロイしたときのターゲット上の名前は、ファイル名から拡張子を除いたものです（`Pump Station.hmlproj` → `projects/pump-station` の
  ように、使えない文字は `-` になります）。ターゲット上は今までどおりフォルダーの形で置かれます。

### ターゲットへのデプロイ

ツールバーの **Deploy** で、開いているプロジェクトを Raspberry Pi などのターゲットへ送ります。

| 項目            | 内容                                                                                                   |
| --------------- | ------------------------------------------------------------------------------------------------------ |
| target (host)   | ターゲットの IP アドレスまたはホスト名                                                                 |
| user / port     | ログインするユーザー名と SSH のポート（既定 22）                                                       |
| private key     | SSH の秘密鍵ファイル。空なら既定の鍵（`~/.ssh/id_*`）                                                  |
| remote folder   | ターゲット上の配置先（既定 `hml` = ホームの `hml/`）                                                   |
| send runtime    | チェックすると、指定したターゲット用の `hml_runtime`（Linux 用のビルド）も送って入れ替える             |
| run on target   | このプロジェクトを、ターゲットが表示するプロジェクトにする（既定でオン。再起動後もそのまま）           |
| restart runtime | 配置後にターゲットのランタイムを再起動する（`SIGUSR2`）                                                |
| local settings  | チェックすると、ターゲットの画面で変更された設定（`project.local.json`）を削除する。毎回オフで始まる   |

ダイアログを開いたとき（接続先が入力済みの場合）と **Check** を押したときに、何も送らずにターゲットの状態を調べて表示します。

- **ターゲットで動いているプロジェクト**（このプロジェクトか、別のプロジェクトか、ランタイムが動いていないか）
- **ターゲットのローカル設定**: ターゲットのシステム設定画面で変更されたドライバ設定は `project.local.json` に保存され、
  デプロイしてもそちらが優先されます。該当する項目を「ターゲットでの値」と「このプロジェクトの値」を並べた表で表示します。
  プロジェクトの値に戻したいときは「local settings」にチェックを入れてデプロイします。
- **ファイルの差分**: 変更されたファイル、新しいファイル、ターゲットにだけ残っているファイルの一覧（ファイル単位）

デプロイが終わると、同じ内容を調べ直して表示します。

ターゲット上の配置は次のとおりです。

```
<remote folder>/hml_runtime
<remote folder>/projects/<プロジェクトのフォルダ名>/project.json, screens/, assets/ ...
<remote folder>/active_project      表示するプロジェクト（例: projects/myhmi/project.json）
```

**送るもの**: プロジェクトを構成するファイルだけです。`project.json`、そこや画面ファイルに名前が書かれているファイル
（画面、スクリプト、画像、スプラッシュ）、`assets/` フォルダーの中身。同じフォルダーにほかのファイルがあっても送りません。
プロジェクトは専用のフォルダーに保存してください（フォルダー名がターゲットでのプロジェクト名になります）。
ほかのファイルがあるフォルダーに保存しようとすると、デザイナーが新しいフォルダーを作るかどうか尋ねます。

**表示するプロジェクトの切り替え**: 「run on target」がオンなら、デプロイしたプロジェクトがターゲットの表示になります。
ターゲットの自動起動（systemd のユニットなど）はコマンドラインで 1 つのプロジェクトを指していますが、ランタイムは
自分の隣に `active_project` があればそちらを読み込みます。ユニットを書き換える必要はありません（管理者権限も不要）。
この動作は、実行ファイルの隣に `projects` フォルダーがある配置のときだけで、`--ignore-active` で無効にできます。
切り替えには、この機能を持つランタイムがターゲットで動いている必要があります（古い場合は「send runtime」で入れ替えてください）。

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
    [--port 22] [--key <秘密鍵>] [--remote-dir hml] [--runtime <Linux 用 hml_runtime>] [--no-restart] [--no-activate] [--clear-local]
```

`--check` を付けると何も送らず、ターゲットの状態（上の Check と同じ内容）を JSON で表示します。

- **表示言語**: ツールバーの選択欄で日本語 / English を切り替えられます（初回は OS の言語）。日本語では、プロパティが
  「背景色」「文字色」のような名前で表示され、マウスを乗せると JSON のキー名（`bg_color` など）が出ます。保存される JSON は変わりません。
- **編集はすぐキャンバスに反映されます**: 色の選択中や数値・テキストの入力中も、確定を待たずにキャンバスが変わります（取り消しは 1 回分）。
- **よく使うスタイルは最初から並んでいます**（背景色、文字色、文字サイズ、角の丸み、枠線、余白）。空欄はテーマの既定値です。
- **画面のプリセット**: Project タブの「画面のプリセット」で、表示器を選ぶと画面サイズとタッチ補正がまとめて入ります
  （3.5 インチ LCD + ADS7846 など）。タッチ補正は「タッチパネル」の欄で 4 つの値として個別に直せます。
- キーボード: Ctrl+S 保存、Ctrl+Z / Ctrl+Y 取り消し / やり直し、Ctrl+D 複製、Delete 削除、Esc 選択解除。
- 画面 JSON のコメントは読み込めますが、保存時には整形された JSON（コメントなし）になります。
- `hml_runtime.exe` は `hml_designer.exe` と同じフォルダから探します。
- ページ本体は `tools/designer/renderer/`（`index.html`, `js/*.js`）で、ビルド時に `bin/designer/` へコピーされます。
  ランタイムが対応するウィジェット・プロパティ一覧は `js/schema.js` にまとまっているので、ランタイムに
  機能を足したらここに 1 行追加すれば GUI に現れます。
- Linux でもビルドできます（`-DHML_DESIGNER=ON`、`libwebkit2gtk-4.1-dev` が必要）。Raspberry Pi 上で使う想定ではありません。

## AI に画面を作ってもらう（Skill）

「こんな感じで画面を作って」と AI に頼めるように、HML の画面の書き方をまとめた **Skill**（AI 向けの手引き）が
`tools/skill/hml-screens/` にあります。要るのはデザイナー一式（`hml_designer` と、同じフォルダーの `hml_runtime`）と、
PC のファイルを読み書きできる AI（Claude Code、Claude のデスクトップアプリなど）だけです。ビルド環境は要りません。

| ファイル                              | 内容                                                                                   |
| ------------------------------------- | -------------------------------------------------------------------------------------- |
| `SKILL.md`                            | 手順（プロジェクトを読む → 画面を書く → ランタイムで描いて確かめる → 報告）、レイアウト・タッチ・色・文字の決まり、してはいけないこと |
| `reference/widgets.md`                | 部品・プロパティ・スタイル・動作の一覧。デザイナーの `schema.js` から生成（`node tools/skill/gen_reference.js`） |
| `reference/binding-and-actions.md`    | `bind`、動作、ポップアップ、多言語、アラーム・トレンドなどの部品の使い方                |
| `examples/`                           | そのまま動く見本のプロジェクト（概要画面と確認のポップアップ）                          |

使い方:

1. `hml-screens` フォルダーを AI に Skill として入れる（Claude Code: `~/.claude/skills/` かプロジェクトの `.claude/skills/` に
   フォルダーごと置く。Claude のアプリ: フォルダーを zip にして、設定の Skill の追加から入れる）。
2. プロジェクトを**フォルダー形式**で保存する（デザイナーの「名前を付けて保存」）。
3. AI にプロジェクトのフォルダーとデザイナーのフォルダーを教えて、作りたい画面を頼む。
4. できたらデザイナーでプロジェクトを開き直し、問題一覧と見た目を確かめる。

- AI は、プロジェクトの**コピー**（`drivers` を空にしたもの）をランタイムで描いて画像で確かめます。実物のプロジェクトで
  ランタイムを動かして機器につながることはありません。
- AI が変えるのは画面と、必要なら `tags` と `texts` です。`drivers`、`users`、装置の設定には触れません。
- ランタイムが警告するのは、知らない部品・知らないタグ・文法エラーまでです。つづりを間違えたプロパティや、行き先のない
  画面遷移は、デザイナーの問題一覧で見つけます。
- 見本と一覧が実装とずれないよう、テストが確かめています（見本のプロジェクトは `hml_tests` が読み込んで全画面を描き、
  一覧は `node --test` が `schema.js` と突き合わせます）。

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

### ドライバ: modbus_rtu / modbus_ascii（シリアル）

シリアルポート（RS-232 / RS-485、USB シリアル変換を含む）で Modbus RTU または Modbus ASCII のクライアント（マスター）として
通信します。`map` の書き方やデータ型は `modbus_tcp` と同じです。

```json
{
  "type": "modbus_rtu",
  "device": "/dev/ttyUSB0",
  "baud": 9600, "data_bits": 8, "parity": "none", "stop_bits": 1,
  "unit_id": 1, "poll_ms": 200, "timeout_ms": 500,
  "status_tag": "plc_online",
  "map": [
    { "tag": "speed", "area": "holding", "addr": 0 },
    { "tag": "temp2", "area": "input",   "addr": 10, "dtype": "s16", "scale": 0.1, "unit_id": 2 }
  ]
}
```

| キー                    | 既定値                                  | 内容                                                                    |
| ----------------------- | --------------------------------------- | ----------------------------------------------------------------------- |
| `device`                |                                         | シリアルポート。Linux は `/dev/ttyUSB0` など、Windows は `COM4` など    |
| `baud`                  | `9600`                                  | 1200 / 2400 / 4800 / 9600 / 19200 / 38400 / 57600 / 115200 / 230400     |
| `data_bits`             | RTU `8`、ASCII `7`                      | RTU は 8 のみ                                                           |
| `parity`                | RTU `none`、ASCII `even`                | `none` / `even` / `odd`                                                 |
| `stop_bits`             | `1`                                     | `1` / `2`                                                               |
| `unit_id`               | `1`                                     | 相手のユニット（スレーブ）番号。`map` の行ごとに `unit_id` で上書きできる |
| `timeout_ms`            | `500`                                   | 応答を待つ時間                                                          |
| `host` / `port`         |                                         | `device` を書かずにこちらを書くと、同じフレームを TCP で送る（シリアル–イーサネット変換器） |

**シリアルポートの名前**

ポートの名前は OS によって、また機械によって違います。`device` には、**そのプロジェクトを動かす機械（ターゲット）での名前**を書きます。

| OS      | 名前の例                                                             | 備考                                                                 |
| ------- | -------------------------------------------------------------------- | -------------------------------------------------------------------- |
| Linux   | `/dev/ttyUSB0`、`/dev/ttyACM0`、`/dev/ttyAMA0`                       | USB 変換器は挿した順に番号が付くので、2 本あると入れ替わることがある |
| Linux   | `/dev/serial/by-id/usb-FTDI_USB_HS_SERIAL_CONVERTER_XXXX-if00-port0` | USB 変換器の「変わらない名前」。挿す順番や口を変えても同じ           |
| Windows | `COM4`                                                               | 番号は PC ごとに決まる                                               |

名前を調べる方法:

- **装置の画面**: システム設定の Driver ページでは、`device` をその装置で見つかったポートの一覧から選べます。
  ページを開いたときに調べ直すので、あとから挿した変換器も出ます。「ports found」に説明つきの一覧も表示されます。
- **デザイナー**: ドライバ設定の **Find serial ports …** で、ターゲットのポート（Deploy の接続設定を使って調べます）と
  この PC のポートを、説明つきで一覧表示します。ターゲットのポートを選ぶと `device` に、PC のポートを選ぶと下記の
  「device on this PC」に入ります。
- **コマンド**: `hml_runtime --list-serial-ports` が、その機械のポートを 1 行に 1 つ表示します
  （`デバイス名 <TAB> 変わらない名前 <TAB> 説明`）。

**同じプロジェクトを PC でも動かすとき**（デザイナーの Run / Preview など）は、プロジェクトを分ける必要はありません。
PC でのポート名は、デザイナーのドライバ設定の **device on this PC** に書きます。これは PC 側の `project.local.json` に保存され、
その PC で動かすときだけ `device` の代わりに使われます。デプロイでは送られないので、ターゲットには影響しません。

別の OS の名前のまま動かした場合（Windows で `/dev/ttyUSB0` など）は、その旨と、その機械にあるポートの一覧がログに出ます。

- 通信条件（ボーレート、データ長、パリティ、ストップビット）は相手の機器に合わせてください。
- **1 本の回線に複数のユニットをつなげます**（RS-485）。`map` の行に `unit_id` を書くと、そのユニットに問い合わせます。
  応答しないユニットがあっても、ほかのユニットとの通信は続きます（応答しないユニットは 1 周につき 1 回だけ待ちます）。
- シリアル回線には「接続」がないので、`status_tag` は「どれか 1 つでもユニットが応答している」あいだオンになります。
  どのユニットも応答しなくなるとオフになり、間隔を空けながら（最大 5 秒）問い合わせを続けます。
- Linux では、ランタイムを動かすユーザーがシリアルポートに読み書きできる必要があります（通常は `dialout` グループ）。
- 応答のチェックサム（RTU は CRC-16、ASCII は LRC）が合わないフレームは捨てます。

### ドライバ: slmp（三菱電機 MELSEC: SLMP / MC プロトコル）

SLMP（MC プロトコルの 3E / 4E フレーム、バイナリコード、TCP）で MELSEC の CPU または Ethernet ユニットと読み書きします。

```json
{
  "type": "slmp",
  "host": "192.168.3.39", "port": 5007, "frame": "3e",
  "poll_ms": 200, "timeout_ms": 1000, "status_tag": "plc_online",
  "write_ranges": ["D0-D999", "M0-M99"],
  "map": [
    { "tag": "speed", "addr": "D100" },
    { "tag": "total", "addr": "D102", "dtype": "int32" },
    { "tag": "temp",  "addr": "D110", "dtype": "float" },
    { "tag": "run",   "addr": "M0" },
    { "tag": "in3",   "addr": "X1F", "access": "ro" },
    { "tag": "ready", "addr": "D120.3" },
    { "tag": "name",  "addr": "D200", "dtype": "string", "length": 16 }
  ]
}
```

| キー                 | 内容                                                                                                   |
| -------------------- | ------------------------------------------------------------------------------------------------------ |
| `host` / `port`      | CPU（または Ethernet ユニット）のアドレスと SLMP のポート。GX Works の「外部機器接続設定」で開けたポート |
| `frame`              | `3e`（既定）/ `4e`（要求ごとにシリアル番号が付き、応答の取り違えを防げる）                              |
| `network` `pc` `io` `station` | 経路（既定 0 / 255 / 0x3FF / 0 = 接続した局の CPU）                                          |
| `write_ranges`       | 書き込みを許す範囲（`"D0-D999"` のように）。省略時は SM / SD 以外すべて書き込める                      |
| `allow_system_write` | `true` のときだけ SM / SD に書き込む（既定 `false`）                                                    |
| `addr`               | GX Works の書き方: `D100` `W1A` `R0` `ZR100` `M0` `X1F` `Y20` `B0` `L0` `F0` `V0` `S0` `TS0` `TC0` `TN0` `SS0` `SC0` `SN0` `CS0` `CC0` `CN0` `SM400` `SD0` `SB0` `SW0` `DX0` `DY0` `Z0`。X Y B W SB SW DX DY ZR は 16 進。`D10.3` はワードのビット（読み取りのみ） |
| `dtype`              | ワードデバイス: `int16`（既定）`uint16` `int32` `uint32` `float` `string`（`length` 文字、下位バイトが先）。ビットデバイスは bool |
| `scale` / `offset`   | 値 = 生の値 × scale + offset（書き込みは逆算）                                                          |
| `access`             | `rw`（既定）/ `ro` / `wo`                                                                               |
| `swap_words`         | 32 ビットの値の上位ワードが先（既定は MELSEC と同じく下位ワードが先）                                   |

- 読み出しは、同じデバイスでまとめて一括読み出し（ワード単位、1 回 960 ワードまで、32 ワードまでの隙間は続けて読む）
  にするので、1000 点でも数回の要求で済みます。ビットデバイスは 16 点ずつワード単位で読みます。
- 書き込みは、ワードデバイスはワード単位、ビットデバイスはビット単位の一括書き込みです。ワードのビット（`D10.3`）は
  書き込みません（PLC が同時に同じワードを書くと消えてしまう読み書きになるため）。
- 応答はヘッダー・長さ（最大 960 ワード分）・4E のシリアル番号を確かめ、合わないもの・途中で切れたもの・時間内に
  来ないものは捨てて接続し直します。終了コード（`C056` など）が返ったときは、接続を保ったまま値を変えずに警告を
  1 度出します。
- SLMP は暗号化も認証もない平文のプロトコルです。PLC とは閉じたネットワークで使い、`write_ranges` で書き込める
  範囲を絞ってください。UDP と MC プロトコルのシリアル（4C フレーム）にはまだ対応していません。

### ドライバ: mqtt（MQTT クライアント）

MQTT 3.1.1 でブローカーに接続し、タグの値を送り（publish）、指令を受け取って（subscribe）タグに書き込みます。
**既定で TLS（1.2 / 1.3）を使い、ブローカーの証明書とホスト名を確認します。** 実装は mbedTLS（Apache-2.0）です。

```json
{
  "type": "mqtt",
  "host": "broker.example.com", "port": 8883,
  "ca_file": "certs/ca.pem",
  "client_id": "hml-pump1", "topic_prefix": "plant/line1/pump1/",
  "status_topic": "status", "status_tag": "mqtt_online",
  "publish": [
    "temp",
    { "tag": "state", "qos": 1, "retain": true },
    { "tag": "flow", "topic": "flow_lpm", "mode": "interval", "interval_ms": 10000 }
  ],
  "subscribe": [
    { "tag": "speed_sp", "topic": "cmd/speed_sp" }
  ]
}
```

| キー                         | 内容                                                                                          |
| ---------------------------- | --------------------------------------------------------------------------------------------- |
| `host` / `port`              | ブローカー。ポートの既定は 8883（TLS）、`"tls": false` なら 1883                                |
| `tls`                        | 既定 `true`。`false` は平文（ユーザー名・パスワード・値がネットワーク上で読める）。閉じたネットワークだけで |
| `verify`                     | 既定 `true`: 証明書を `ca_file` の CA で確かめ、ホスト名も確認する。`false` は警告付き（なりすましを防げない） |
| `ca_file`                    | ブローカーの CA 証明書（PEM、project.json からの相対パス）。Linux では空欄ならシステムの CA（`/etc/ssl/certs/ca-certificates.crt`） |
| `cert_file` / `key_file`     | クライアント証明書と秘密鍵（PEM）。ブローカーが求める場合                                       |
| `username` / `password`      | 装置のシステム設定（System > Driver）で入れると `project.local.json` に入り、project.json には残りません |
| `client_id`                  | 装置ごとに別の値にする                                                                         |
| `topic_prefix`               | すべてのトピックの前に付く                                                                     |
| `status_topic`               | 接続すると `online`（retain）、切れるとブローカーが遺言（last will）で `offline` を出す          |
| `status_tag`                 | 接続中は 1                                                                                    |
| `payload`                    | `value`（既定。`21.5`、bool は `true` / `false`）/ `json`（`{"v": 21.5, "ts": ミリ秒}`）         |
| `qos` / `retain`             | publish の既定（QoS は 0 か 1）                                                                |
| `publish[]`                  | `tag`、`topic`（既定はタグ名）、`mode`: `change`（既定、変化したとき）/ `interval`（`interval_ms` ごと）、`qos`、`retain` |
| `subscribe[]`                | `tag`、`topic`（既定はタグ名）。**ここに書いたタグだけが外から書き込まれます**。ワイルドカード（`+` `#`）は使えません |
| `keepalive_s` / `timeout_ms` / `reconnect_ms` | 既定 30 秒 / 5000 ms（接続とハンドシェイク）/ 1000 ms（再接続の最初の待ち。30 秒まで倍に延びる） |

- 受け取った値は、数値のタグなら数値か、bool なら `true` / `false` / `1` / `0` / `on` / `off` か、文字列なら UTF-8 で
  256 バイト以下かを確かめ、タグの `min` / `max` の外なら書き込みません（ログに警告）。`{"v": ...}` の形も受け付けます。
  受け取って書いた値は、同じタグを publish していても送り返しません。
- ブローカーからのパケットは、長さ（64 KiB まで）・種類・フラグ・トピック（UTF-8、ワイルドカードなし）を確かめ、
  壊れていれば接続を切ってつなぎ直します（QoS 2 には対応していません）。
- ブローカーにつながらない間は、`change` のトピックごとに最新の値だけを覚えておき、つながったときに送ります。
  確認（PUBACK）が来ていない QoS 1 のメッセージは、つなぎ直したあと DUP を付けて送り直します（100 件まで）。
- パスワードはログに出しません。`project.local.json` には平文で入るので、装置のファイルを読める人は読めます。

### ドライバ: opcua_client（OPC UA クライアント）

ほかの OPC UA サーバー（PLC、SCADA など）のノードをタグにします。**既定で署名と暗号化（Sign & Encrypt）を使い、
サーバーの証明書を確認します。** 実装は open62541（MPL-2.0）で、暗号は mbedTLS です。

```json
{
  "type": "opcua_client",
  "endpoint": "opc.tcp://192.168.0.20:4840",
  "server_cert": "certs/plc.der",
  "status_tag": "plc_online", "publish_ms": 500,
  "map": [
    { "tag": "temp", "node": "ns=2;s=Temp" },
    { "tag": "sp",   "node": "nsu=urn:plc:vars;s=Setpoint", "access": "rw" }
  ]
}
```

| キー                         | 内容                                                                                          |
| ---------------------------- | --------------------------------------------------------------------------------------------- |
| `endpoint`                   | サーバーの `opc.tcp://ホスト:ポート`                                                            |
| `security`                   | `sign_encrypt`（既定）/ `sign` / `none`（平文。値がネットワーク上で読めて、書き換えられる。警告付き） |
| `policy`                     | 空欄なら両方が使える最も強いもの。`Basic256Sha256`、`Aes128_Sha256_RsaOaep`、`Aes256_Sha256_RsaPss` など |
| `server_cert`                | 信頼するサーバーの証明書（DER / PEM、project.json からの相対パス。配列も可）。装置の `pki/trusted/` に置いたものも信頼する。**どちらもないと接続しません**（open62541 は信頼リストが空だと何でも受け入れるため） |
| `verify`                     | 既定 `true`。`false` はサーバーの証明書を確認しない（警告付き。なりすましを防げない）              |
| `cert_file` / `key_file`     | このクライアントの証明書と秘密鍵（DER）。空欄なら、装置が初回に `pki/own/opcua_client.der`（RSA 2048、20 年）を作り、以後それを使う。サーバー側でこの証明書を信頼させてください |
| `application_uri`            | クライアントの URI（既定 `urn:hml:opcua_client`）。`cert_file` を使うときは証明書の URI と同じに |
| `username` / `password`      | 装置のシステム設定（System > Driver）で入れると `project.local.json` に入り、project.json には残りません |
| `publish_ms`                 | サーバーが変化を送る間隔（サブスクリプション。既定 500）                                         |
| `timeout_ms` / `reconnect_ms`| 既定 5000 ms（接続）/ 1000 ms（再接続の最初の待ち。30 秒まで倍に延びる）                         |
| `status_tag`                 | 接続中は 1                                                                                    |
| `map[]`                      | `tag`、`node`（`ns=2;s=Temp`、`ns=3;i=1001`、名前空間 URI で書く `nsu=urn:...;s=...`）、`access`: `ro`（既定）/ `rw` |

- 値はサブスクリプションで受け取ります（Boolean、整数、Float、Double、String、LocalizedText。配列は未対応）。
- **サーバーへの書き込みは `"access": "rw"` の行だけ**です。値はノードのデータ型に変換し、入らないもの
  （Int16 のノードに 40000、数値のノードに文字列など）は書き込まずにログに残します。接続していない間の変更は、
  あとからまとめて送りません。
- 接続が切れたら、セッションとサブスクリプションを作り直します。
- `pki/` は装置の持ち物で、`logs/` や `project.local.json` と同じくデプロイで消えず、プロジェクトファイルにも入りません
  （プロジェクトファイルから信頼する証明書を仕込むこともできません）。
- 接続できない理由を調べるときは、環境変数 `HML_OPCUA_LOG=info`（または `debug`）で open62541 のログを増やせます。

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

## 画面消灯（スクリーンセーバー）

操作がないまま一定時間が経つと画面を消します。

```json
"screen_saver": { "after_s": 600, "wake_on_alarm": true, "logout": false }
```

| キー            | 内容                                                                 |
| --------------- | -------------------------------------------------------------------- |
| `after_s`       | 操作がないまま消すまでの秒数（既定 0 = 消さない）                     |
| `wake_on_alarm` | 新しいアラーム（発生、未確認）で点ける（既定 `true`）                  |
| `logout`        | 消えたらログアウトする（既定 `false`）                                |

- 消灯中は、画面・ポップアップ・ダイアログの上に黒い層を出し、バックライトを消せる表示器では消します
  （Linux: `/sys/class/backlight/*/bl_power`、なければフレームバッファのブランク。どちらも使えない表示器、
  PC では黒い画面にするだけです）。
- **点けるためのタッチでは何も押されません**（そのタッチは最後まで黒い層が受け取ります）。
- タグ `sys.screen.off`: 消灯中は 1。1 を書くとすぐに消え、0 で点きます（「画面を消す」ボタンなど）。
- 装置のシステム設定 **System > Display** で時間を変えられます。変えた値は `project.local.json`（`"screen_saver": { "after_s": N }`）
  に残り、プロジェクトの値と同じに戻すと消えます。デプロイでは上書きされません。

## 多言語表示とフォント

画面の文字を複数の言語で持ち、装置で切り替えられます。日本語などラテン文字以外には、その文字があるフォントを
プロジェクトに入れます。

```json
"languages": ["ja", "en"],
"language": "ja",
"font": { "file": "assets/fonts/NotoSansJP-subset.otf" },
"texts": {
  "title": { "ja": "ポンプ場 1号", "en": "Pump Station #1" },
  "alarm_temp_high": { "ja": "温度が高い", "en": "Temperature high" }
}
```

| キー        | 内容                                                                                          |
| ----------- | --------------------------------------------------------------------------------------------- |
| `languages` | 言語コード。最初のものが既定で、訳がないときもその言語を表示する                                |
| `language`  | 起動時の言語（既定: 最初のもの）                                                              |
| `texts`     | ID ごとの各言語のテキスト                                                                     |
| `font.file` | すべての文字に使う TTF / OTF（どの大きさでも使える）。省略時は組み込みの Montserrat（ラテン文字のみ） |

- 部品の `text` / `placeholder` / `options` の各項目、アラーム・レシピ・トレンド部品の `labels`、アラームの `message` に
  `"@ID"` と書くと、その言語のテキストになります。`texts` にない ID はそのまま表示され（既存の `"@..."` を壊さない）、
  `"@@"` は `"@"` 1 文字です。
- 言語はタグ `sys.lang` です。動作 `{ "action": "language", "lang": "en" }`（`"next"` で次の言語）や、`sys.lang` に連動した
  部品、Lua で書き換えると、画面を作り直してその言語で表示します（表示中の画面と「戻る」の履歴はそのまま。スクリプトの
  状態も保たれます）。`sys.languages` に一覧（`ja,en`）があります。
- アラームの記録には、その時点の言語のメッセージが残ります。
- 組み込みのシステム設定画面とログイン画面は英語のままです。
- フォントのファイルがあれば、各大きさの字形はそのファイルから作られ（TinyTTF）、ファイルにない文字（`LV_SYMBOL_*` の
  アイコンなど）は組み込みのフォントで表示します。字形は最初に表示するときに作られ、キャッシュされます。
- 日本語のフォントは大きいので、`tools/subset_font.py` で使う文字だけにした小さなファイルを作れます
  （ASCII・かな・記号・JIS 第 1 水準漢字で約 1.2 MB。`--jis2` で第 2 水準、`--text` でプロジェクトの文字を追加）。
  デモには Noto Sans JP のサブセット（SIL Open Font License。`projects/demo/assets/fonts/OFL.txt`）が入っています。
  フォントを同梱するときは、ライセンスの条件（著作権表示とライセンス文を添える、フォント単体で販売しない）を守ってください。

```
python -I tools/subset_font.py NotoSansJP-Regular.otf projects/myhmi/assets/fonts/NotoSansJP-subset.otf --text projects/myhmi/project.json projects/myhmi/screens/*.json
```

デザイナーでは「多言語テキスト」タブ（Ctrl+8）で ID と各言語の訳を表で編集でき、表計算ソフトとの取り込み / 書き出し
（翻訳者に渡す用）、キャンバスに表示する言語の切り替えもできます。訳がないテキスト、`texts` にない `"@ID"`、元の言語より
かなり長い訳（収まらないかもしれない）は問題一覧に出ます。フォントはプロジェクト タブの「フォント」で指定します。

LVGL 9.4 の TinyTTF には不具合が 2 つあり、ランタイムは回避しています: フォントにない文字を求めるとキャッシュの要素が
二重に解放される（フォントの文字表 `cmap` を自分で読み、ある文字だけを TinyTTF に求める）、カーニングのキャッシュが
解放済みの要素を使い、一部の文字の送り幅が別の字形のものになる（カーニングを使わず、送り幅は `hmtx` から求める）。
どちらも AddressSanitizer と表示の確認で見つけたものです（`src/font_cmap.hpp`）。

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
  { "action": "popup",  "screen": "temp_detail", "set": { "detail_unit": 2 } },
  { "action": "close" },
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

#### ポップアップ（`popup` / `close`）

画面ファイルに `"popup": true` を書くと、その画面は今の画面の上に重ねて開く**ポップアップ**になります
（画面として切り替わることはありません。`goto` で指定しても開くだけです）。

```json
{ "name": "temp_detail", "popup": true, "w": 420, "h": 260, "dim": true, "level": 0,
  "style": { "bg_color": "#1f2a38", "radius": 10 },
  "children": [ ...,  { "type": "button", "text": "Close", "on_click": { "action": "close" } } ] }
```

- 大きさと位置は部品と同じ `w` `h` `x` `y` `align`（既定: 60 % × 60 %、中央）。`dim: false` で下の画面を暗くしない。
- 開いている間、下の画面には触れません（モーダル）。ポップアップからさらにポップアップを開けます（上に重なる）。
- 閉じるのは `close`（いちばん上のもの。`"screen"` で名前を指定もできる）、`back`（ポップアップの中ではそれを閉じる）、
  画面の切り替え（`goto` / `back` / ログイン画面）です。
- `popup` の `"set": { "タグ": 値 }` は、開く前にタグへ書き込みます。どの装置の詳細かを渡すのに使います
  （ポップアップの中の部品はそのタグや、それをもとにスクリプトで用意したタグを表示する）。
- `level` は画面と同じで、足りなければログイン画面が出て、ポップアップは開きません。
- 一度作ったポップアップは閉じても残して使い回すので、何度開閉してもメモリは増えません。閉じている間は部品を更新せず、
  開いたときに最新の値にします。

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
| `max_size_mb`    | `1000`                                   | データベースの容量（MB）がこれを超えたら古い行から削除（`0` = 無制限）。1 行に多くのタグを入れるグループは行が大きくなるので、行数だけでは容量を抑えられない |

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

#### 画面で見る: `trend`（トレンド履歴）

変数ロギングのグループを、時間の窓を指定して折れ線で表示します。

```json
{ "type": "trend", "group": "process", "w": "100%", "h": 300, "range_s": 3600,
  "tags": ["temp", { "tag": "pressure", "color": "#e91e63" }] }
```

| キー         | 内容                                                                             |
| ------------ | -------------------------------------------------------------------------------- |
| `group`      | 変数ロギングのグループ                                                           |
| `tags`       | 表示するタグ（省略時はグループの全タグ、8 本まで）。`{ "tag", "color" }` で色も指定 |
| `range_s`    | 表示する期間（既定 3600 秒）。`−` / `+` で 2 倍 / 半分（1 分〜1 年）              |
| `min` / `max`| 縦軸の範囲（省略時はデータに合わせる）                                           |
| `buckets`    | 横の点数（既定 200）。各点はその区間の最小値と最大値の 2 本の線になる              |
| `refresh_ms` | 「今」を表示している間の更新周期（既定 2000）                                     |

- 各点は時間の区間で、その区間の**最小値と最大値**を描くので、長い期間を表示しても短いピークが消えません。
- `◀` `▶` で半分ずつ移動、「Now」で最新に戻ります（最新を表示している間は自動で更新）。グラフに触れると、
  その時刻の値（区間に幅があれば最小..最大）が出ます。
- 変数ロギングは、記録と同時に**1 分ごとの要約**（各タグの最小・最大・最後の値、テーブル `minute`）を書きます。
  1 区間が 1 分以上の長い期間はこの要約を読み、要約がまだない部分（今の 1 分と、以前のバージョンで記録した期間）
  だけ記録そのものを読みます。100 万件の記録でも数十 ms で描けます（要約がなければ数秒）。
- 読み出しは別スレッドで行うので、長い期間を表示しても画面は止まりません。

### アラーム (`alarms`)

タグに対する条件を定義します。条件が成立したとき（発生 `raised`）、成立しなくなったとき（復帰 `cleared`）、
確認（アクノリッジ）されたとき（`acked`）に 1 行ずつ記録されます。

```json
"alarms": {
  "ack_level": 1,
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
| `ack`                 | `true`（既定）: 確認されるまで「発生中のアラーム」に残る。`false`: 復帰すれば消える（確認は不要）    |
| `ack_level`（全体）   | 確認に必要なユーザーレベル（既定 0 = 誰でも。ユーザー管理を使うとき）。足りなければログイン画面が出る |

- 状態（発生中か、確認済みか）は再起動しても残ります（データベースから読み戻します）。止まっている間に
  発生していたアラームは起動時刻で発生、止まっている間に消えたアラームは起動時刻で復帰として記録されます。
  状態が変わっていないアラームは記録し直しません。
- 記録される列は、時刻、名前、`raised` / `cleared` / `acked`、重要度、メッセージ、そのときのタグ値、確認した人です。
  以前のバージョンで作ったデータベースには、開いたときに列が追加されます。
- 確認は操作ログにも記録されます（種別 `alarm`）。
- タグ: `sys.alarm.active`（発生中の数）、`sys.alarm.unacked`（未確認の数）、`sys.alarm.latest`（いちばん新しい未確認の
  アラーム、なければ発生中のアラームのメッセージ）、`sys.alarm.latest_severity`。
- 動作: `{ "action": "call", "fn": "alarm.ack", "arg": "<名前>" }`（`arg` 省略 = いちばん新しい未確認）、
  `{ "action": "call", "fn": "alarm.ack_all" }`。

#### 画面に出す: `alarm_list` と `alarm_banner`

```json
{ "type": "alarm_banner", "w": "100%", "h": 36 },
{ "type": "alarm_list", "w": "100%", "h": 200, "columns": ["time", "severity", "message", "state"] },
{ "type": "alarm_list", "w": "100%", "h": 200, "mode": "history", "rows": 10 }
```

| キー           | 内容                                                                                                   |
| -------------- | ------------------------------------------------------------------------------------------------------ |
| `mode`         | `current`（既定: 発生中・未確認のアラーム、新しい順）/ `history`（記録、新しい順。ページ送りと重要度の絞り込み付き） |
| `rows`         | `current`: 表示する最大行数（既定 50、超えた分は「+N more」）。`history`: 1 ページの行数（既定 20）     |
| `row_height`   | 行の高さ（既定 40 px。指で押しやすい高さ）                                                             |
| `columns`      | `time` / `severity` / `message` / `state` から選んで並べる（既定 `time, message, state`）              |
| `ack_on_tap`   | `current`: 行をタップするとそのアラームを確認する（既定 `true`）。バナーでは既定 `false`               |
| `severity`     | その重要度だけ表示する                                                                                 |
| `days`         | `history`: 直近 N 日だけ（既定 0 = すべて）                                                            |
| `blink`        | バナー: 未確認の間点滅する（既定 `true`）                                                              |
| `labels`       | 表示文字の置き換え: `unacked` `active` `cleared` `empty` `idle` `all` `more`（`%d` が件数）`raised` `cleared_event` `acked_event` |

- 色は重要度（`critical` 赤 / `warning` 琥珀 / `info` 青）、状態は塗り方（未確認 = 塗りつぶし、確認済み = 薄く、
  復帰・未確認 = 灰色に色の帯）に加えて、文字とアイコンでも示します（色だけに頼らない）。
- バナーは、いちばん新しい未確認のアラーム（なければ発生中のアラーム）と、ほかの件数を出します。`on_click` に
  アラーム画面への `goto` などを書けます。
- 一覧は表示中の画面にあるときだけ描き直します（250 ms ごとに変化を確認）。
- 表示文字のフォントは Montserrat なので、`labels` で日本語を使う場合は日本語の字形を含むフォントが必要です
  （多言語対応 #22 で扱います）。

```
hml_runtime --export-alarm-log alarms.csv projects/demo/project.json
```

画面からは `{ "action": "call", "fn": "alarm_log.export" }`（`arg` で出力先を指定可）。

### レシピ (`recipes`)

品種ごとの設定値など、タグの値の組に名前を付けておき、まとめて書き込みます（ドライバを通して PLC へ）。

```json
"recipes": {
  "level": 2,
  "verify_ms": 2000,
  "books": [
    { "name": "process", "items": ["speed_sp", "setpoint", "temp_lo", "temp_hi"],
      "recipes": [ { "name": "Standard", "values": { "speed_sp": 60, "setpoint": 50, "temp_lo": 20, "temp_hi": 30 } },
                   { "name": "Gentle",   "values": { "speed_sp": 35, "setpoint": 40, "temp_lo": 22, "temp_hi": 28 } } ] }
  ]
}
```

| キー         | 内容                                                                                                   |
| ------------ | ------------------------------------------------------------------------------------------------------ |
| `level`      | 装置でレシピを書き込む・保存する・削除するのに必要なユーザーレベル（既定 0 = 誰でも。選ぶだけなら不要） |
| `verify_ms`  | 書き込んでからこの時間（ms）後に、タグの値と書いた値を比べる（既定 2000、0 = 比べない）                |
| `books`      | レシピブック: `name`（1〜32 文字、`/ \ : * ? " < > | .` は不可）、`items`（タグ）、`recipes`（初期のレシピ） |

- **書き込み**: 先にすべての値を、型とタグの `min` / `max` で確かめます。1 つでも外れていれば何も書きません。
  書いたあと `verify_ms` 経ったら値を比べ、PLC が受け付けなかった値があれば `sys.recipe.status` に出します。
- **装置での保存**: 装置で保存・変更・削除したレシピは、project.json の隣の `recipes/<ブック名>.json` に保存します
  （一時ファイルに書いてから置き換えるので、保存中に電源が切れても壊れません。形式は `{ "version": 1, "recipes": [...] }`）。
  このファイルができてからは、プロジェクトのレシピではなくこちらを使います。デプロイでは上書きされません（`logs/` と同じ）。
- **CSV**: 1 行に 1 レシピ（`name,<タグ>,<タグ>...`、UTF-8 BOM 付き）。表計算ソフトが数式と読むセル（`=` `+` `-` `@`
  で始まる文字）は先頭に `'` を付けて書き出します（CSV インジェクション対策）。取り込みは、列がブックの項目か、
  名前と値が正しいか、大きさ（4 MB まで）を確かめ、1 つでも問題があれば何も変えません。
- 書き込み・保存・削除・取り込み・書き出しは操作ログに記録します（種別 `recipe`）。
- タグ: `sys.recipe.status`（最後の操作の結果）、`sys.recipe.<ブック名>.selected`（選んでいるレシピ）。
- 動作（`call`）: `recipe.select`（`"ブック,名前"`）、`recipe.write`（`"ブック[,名前]"`、省略時は選んでいるもの）、
  `recipe.save`（`"ブック,名前"`: 今のタグの値で保存）、`recipe.delete`、`recipe.export`（`"ブック[,ファイル]"`）、`recipe.import`（`"ブック,ファイル"`）。

#### 画面に出す: `recipe_table`

```json
{ "type": "recipe_table", "book": "process", "w": "100%", "h": 400 }
```

左にレシピの一覧、右に「項目・レシピの値・現在値」の表と、書き込み・現在値の読み込み・名前を付けて保存・削除の
ボタンが出ます。レシピの値と現在値が違うところは色と `*` で示します（書き込むと変わる値）。値をタップすると
その値を変えられます。書き込み・読み込み・削除は確認してから行い、`level` が足りなければログイン画面が出ます。
`labels` で表示文字を置き換えられます（`write` `read` `save_as` `delete` `item` `recipe` `current` `ok` `cancel` `name`
`confirm_write` `confirm_read` `confirm_delete`。`%s` はレシピ名）。

デザイナーでは「レシピ」タブ（Ctrl+7）でブックとレシピを表の形で編集でき、表計算ソフトとの取り込み / 書き出しもできます。
範囲外の値や存在しないブックは問題一覧に出ます。

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
| `src/modbus_link.[ch]pp`       | Modbus の通信路（TCP / シリアルポート）とフレーム（MBAP / RTU の CRC-16 / ASCII の LRC）          |
| `src/serial_ports.[ch]pp`      | その機械のシリアルポートの一覧（Linux: sysfs と by-id、Windows: SetupAPI）                         |
| `src/project_archive.[ch]pp`   | プロジェクトファイル（`.hmlproj`）の作成と展開、プロジェクトを構成するファイルの一覧（デザイナーと共用） |
| `src/driver_opcua.cpp`         | OPC UA サーバー（open62541、DataSource コールバックでタグを公開）                                   |
| `src/driver_opcua_client.cpp`  | OPC UA クライアント（open62541 + mbedTLS、サブスクリプションで読み、`rw` の行だけ書く）             |
| `src/mqtt.[ch]pp` / `src/driver_mqtt.cpp` | MQTT 3.1.1 のパケット（入出力なし）と接続（TCP / mbedTLS の TLS）、MQTT クライアントドライバ |
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
