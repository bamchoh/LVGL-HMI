# 機器とつなぐ（ドライバーの設定）

`project.json` の `drivers` に、PLC やブローカーとの接続を書く手順です。キーの一覧は `drivers.md`（デザイナーの定義から生成）。
**頼まれたときだけ**行います。アドレスの間違いは、実機の違う場所への書き込みになります。値を動かすだけのデモなら、ドライバーではなく
`simulation`（`project-features.md`）を使います。

## 決まり

- **推測しない**: 接続先（IP アドレス、ポート、機種、局番・ユニット番号、ブローカー、エンドポイント）とアドレスは、依頼の文かユーザーが渡した
  アドレス表（CSV、表計算、PLC のラベル一覧）から取る。なければ作らずに聞く。「たぶん D100」で書かない。
- **読むだけから始める**: 新しく作る行は `"access": "ro"`。書き込む行（設定値、運転指令）は、ユーザーが書き込みを認めたものだけ `rw`
  （押している間だけ ON にする指令などで、読み返さないものは `wo`）。SLMP と FINS は `write_ranges` に、書いてよい範囲だけを入れる
  （`["D100-D199", "M0-M15"]`）。システムの領域（SM / SD、FINS の A）は書かない（`allow_system_write` は付けない）。
- **パスワードや鍵を書かない**: ユーザー名までは書いてよい。パスワードは装置の System 画面で入れる（`--check` が知らせる）。
  暗号化は既定のまま（MQTT は `tls: true`、`verify: true`、OPC UA は `security: "sign_encrypt"`）。暗号化を切るのは頼まれたときだけで、報告に書く。
- **既存の `drivers` は、頼まれた所しか変えない**（ほかの行を並べ替えない、消さない、名前を変えない）。
- **接続は試さない**: AI は実機につながない（ランタイムを `--check` なしで動かすと、ドライバーが実機につながる）。
  接続の確認はユーザーが行う。報告に、確かめてほしいことを書く。

## 手順

1. **アドレス表を読む**。1 行 = 1 つの値（名前、アドレス、データ型、単位、読む / 書く）。わからない列があれば聞く。
2. **タグを作る**（`project.json` の `tags`）。名前は英小文字と `_`（表の名前から作る。既存のタグがあればそれを使う）。型:

   | 表のデータ型 | タグの `type` | 行の `dtype` |
   | --- | --- | --- |
   | ビット（X、Y、M、コイル、接点） | `bool` | なし（ビットのデバイス / `coil` / `discrete`） |
   | 16 ビット整数（符号あり / なし） | `int` | `int16` / `uint16`（Modbus は `s16` / `u16` も可） |
   | 32 ビット整数 | `int` | `int32` / `uint32`（2 ワード使う） |
   | 実数（32 ビット浮動小数） | `float` | `float`（2 ワード使う） |
   | 小数点付きの整数（0.1 °C 単位など） | `float` | `int16` + `"scale": 0.1` |
   | 文字列 | `string` | `string` + `"length"`（文字数） |

   書き込む値のタグには `min` / `max` を付ける（画面からの書き込みが範囲に丸められる）。
   **既存のタグを使うとき**: 型はアドレスに合わせる（実数のアドレスに int のタグなら float に変え、報告する。`--check` が知らせる）。
   表の範囲とタグの `min` / `max` が違えば、狭いほうにして、どちらが正しいか聞く。
3. **ドライバーを 1 つ足す**（`drivers.md` の `type` とキー）。`map` は 1 タグ 1 行。`status_tag` に bool のタグ（`plc_online` など。
   つながっている間 true）を作って入れる。
4. **通信が切れたことを知らせる**: `alarms` に `{ "name": "plc_comm", "tag": "plc_online", "condition": "false", "severity": "critical", "message": "PLC communication lost" }`。
   **既に通信異常のタグとアラームがあれば、それを使う**（新しいアラームを足さない。向きが逆のタグなら、スクリプトで `tag.plc_comm_error = not tag.plc_online`）。
   通信の状態はヘッダー（テンプレート）に出す。接続を頼まれたことは、テンプレートにこの表示を足してよい理由になる（報告に書く）。
5. **今までの作り物の値を外す**（`--check` が見つける）:
   - `simulation` の項目で、PLC から読むタグを動かしているものは**消す**（`when` で止めても、`follow` は止まっている間 `idle` を書き続ける）。
     PLC から来ない値の項目は残してよい（そのあいだ `sys.sim.active` は 1 のまま: SIMULATION の表示は正しい）。すべての値が PLC から来るなら、
     `simulation` ごと消す。
   - スクリプトが、PLC から読むだけのタグに値を入れていれば、その行を消す（合計の計算、状態の作り物）。状態の計算（ライン状態など）は、
     PLC のビットがあればそれから作る。
   - 画面に「予定のアドレス」などを出している所（I/O モニタ）は、アドレス表に合わせて直す（消した行は報告に書く）。
6. **`--check`**: アドレスの書き方（ドライバー自身が読む: `runtime: slmp: tag 'x': "Q5": unknown device ...`）、同じタグや同じアドレスの 2 行、
   タグの型とアドレスの食い違い（文字列と数値、実数と int、ビットと数値）、Modbus の番号の範囲、PLC から読む値を動かすシミュレーションや
   スクリプトを見つける。`no problems` まで直す。
7. **報告**: 足したタグと行の一覧（タグ、アドレス、`dtype`、`ro` / `rw`）、書き込みを許した行と `write_ranges`、聞きたいこと、
   ユーザーが確かめること（接続、数行の値を PLC の表示と見比べる、書き込みの行を 1 つずつ試す）。

## 押している間だけの指令（運転、リセット）

ボタンの `on_press` で true、`on_release` で false を書く（`"access": "wo"`、または PLC が読み返すなら `rw`）。指が外へ滑ってから離しても
`on_release` は来る（止め忘れない）。確認のポップアップがある操作なら、ポップアップの「実行」ボタンにこの 2 つを付ける。
PLC 側は、立ち上がりで動くようにしてもらう（報告で伝える）。

## 文字列

SLMP / FINS の `"dtype": "string"` は 1 文字 1 バイト（ASCII。1 ワードに 2 文字: SLMP は下位バイトが先、FINS は上位バイトが先）、`length` は文字数。日本語（Shift-JIS など）は読めない。
表に「16 文字」とあっても、全角なら聞く。

## アドレスの書き方

- **Modbus**（`modbus_tcp` / `modbus_rtu` / `modbus_ascii`）: `area` と `addr`（0 から数える番号）。表が 5 桁 / 6 桁の番号なら、
  40001 → `holding` の 0、30001 → `input` の 0、10001 → `discrete` の 0、00001 → `coil` の 0。`coil` / `discrete` は bool のタグ。
  32 ビットは 2 つ続けて使う（`addr` と `addr + 1`）。語の順が逆なら `swap_words`。
- **SLMP（三菱 MELSEC）**: GX Works の書き方そのまま `D100` `M0` `X1F` `Y20` `W1A` `ZR100`（X Y B W SB SW DX DY ZR は 16 進）。`D10.3` はワードのビット（読むだけ）。
- **FINS（オムロン）**: `D100` `CIO10` `W0.00` `H5` `A0` `E0_100` `T1` `C1`（`.00`〜`.15` はビット）。BCD の値は `bcd16` / `bcd32`。
- **OPC UA サーバー**（`opcua_server`: SCADA などにタグを見せる）: 既定で署名と暗号化。受け入れるクライアントの証明書を `client_certs` に
  （ユーザーから受け取ったファイルをプロジェクトに入れる。なければ聞く。1 つもないと、どのクライアントも入れない）。`"security": "none"` は
  頼まれたときだけ（報告に書く）。ログインが要るなら `username` だけ書き、パスワードは装置で設定してもらう。見せるタグは `tags` に絞り、
  書かせないタグは `"access": "ro"`。
- **OPC UA クライアント**: `map` の各行に `node`（`ns=2;s=Line1.Temp` のようなノード ID）。サーバーの証明書を `server_cert` に。
- **MQTT**: `publish`（タグ → ブローカー）と `subscribe`（ブローカー → タグ。外から書かれるのはここに書いたタグだけ）。トピックは `topic_prefix` + `topic`。
  - 1 トピックに 1 つの値: `{ "tag": "temp", "topic": "oven/temp" }`（値はそのまま文字列。`"payload": "json"` なら `{"v": 21.5, "ts": ...}`）。
  - **相手が決めた JSON**（メッセージの仕様書やサンプルがある）: 1 行に `"topic"` と `"fields": { "<JSON Pointer>": "<タグ>" }`。
    publish は `const`（決まった値: `{ "/device": "line1" }`）と `ts`（時刻を入れる場所: `"/ts"`）も書ける。subscribe は、メッセージにあるフィールドだけを書く。
    例: 仕様が `{"device": "line1", "oven": {"temp": 211.5}, "ts": 1763800000000}` なら
    `{ "topic": "line1/status", "fields": { "/oven/temp": "oven_temperature" }, "const": { "/device": "line1" }, "ts": "/ts" }`。
    配列の中は `/list/0/value`。仕様の名前と型（数値か文字列か）をタグに合わせ、合わなければ聞く。

## 例: アドレス表から

```
名前,アドレス,型,単位,読み書き
炉温度,D100,実数,°C,読む
炉温度設定,D110,実数,°C,書く
運転中,M0,ビット,,読む
運転指令,M10,ビット,,書く
```

```json
"tags": [
  { "name": "oven_temperature", "type": "float", "init": 0 },
  { "name": "oven_temperature_sp", "type": "float", "init": 0, "min": 0, "max": 300 },
  { "name": "line_running", "type": "bool", "init": false },
  { "name": "line_run_cmd", "type": "bool", "init": false },
  { "name": "plc_online", "type": "bool", "init": false }
],
"drivers": [
  { "type": "slmp", "name": "line1", "host": "192.168.0.10", "port": 5007, "status_tag": "plc_online",
    "write_ranges": ["D110-D111", "M10"],
    "map": [
      { "tag": "oven_temperature", "addr": "D100", "dtype": "float", "access": "ro" },
      { "tag": "oven_temperature_sp", "addr": "D110", "dtype": "float", "access": "rw" },
      { "tag": "line_running", "addr": "M0", "access": "ro" },
      { "tag": "line_run_cmd", "addr": "M10", "access": "rw" } ] }
]
```

（IP アドレスとポートは依頼の文にあったもの。なければ聞く。）
