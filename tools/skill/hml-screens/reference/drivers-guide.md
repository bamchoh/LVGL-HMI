# 機器とつなぐ（ドライバの設定）

`project.json` の `drivers` に、PLC やブローカーとの接続を書く手順です。キーの一覧は `drivers.md`（デザイナーの定義から生成）。
**頼まれたときだけ**行います。アドレスの間違いは、実機の違う場所への書き込みになります。値を動かすだけのデモなら、ドライバではなく
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
- **接続は試さない**: AI は実機につながない（ランタイムを `--check` なしで動かすと、ドライバが実機につながる）。
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
3. **ドライバを 1 つ足す**（`drivers.md` の `type` とキー）。`map` は 1 タグ 1 行。`status_tag` に bool のタグ（`plc_online` など）を作って入れる。
4. **通信が切れたことを知らせる**: `alarms` に `{ "name": "plc_comm", "tag": "plc_online", "condition": "false", "severity": "critical", "message": "PLC communication lost" }`。
   画面のヘッダー（テンプレート）に通信の状態を出す。
5. **`simulation` があるプロジェクト**: 実機の値を書き続けないよう、同じタグを動かす項目に `"when": { "tag": "plc_online", "eq": false }` を付ける。
6. **`--check`**: アドレスの書き方（ドライバ自身が読む: `runtime: slmp: tag 'x': "Q5": unknown device ...`）、同じタグや同じアドレスの 2 行、
   タグの型とアドレスの食い違い、Modbus の番号の範囲を見つける。`no problems` まで直す。
7. **報告**: 足したタグと行の一覧（タグ、アドレス、`dtype`、`ro` / `rw`）、書き込みを許した行と `write_ranges`、聞きたいこと、
   ユーザーが確かめること（接続、数行の値を PLC の表示と見比べる、書き込みの行を 1 つずつ試す）。

## アドレスの書き方

- **Modbus**（`modbus_tcp` / `modbus_rtu` / `modbus_ascii`）: `area` と `addr`（0 から数える番号）。表が 5 桁 / 6 桁の番号なら、
  40001 → `holding` の 0、30001 → `input` の 0、10001 → `discrete` の 0、00001 → `coil` の 0。`coil` / `discrete` は bool のタグ。
  32 ビットは 2 つ続けて使う（`addr` と `addr + 1`）。語の順が逆なら `swap_words`。
- **SLMP（三菱 MELSEC）**: GX Works の書き方そのまま `D100` `M0` `X1F` `Y20` `W1A` `ZR100`（X Y B W SB SW DX DY ZR は 16 進）。`D10.3` はワードのビット（読むだけ）。
- **FINS（オムロン）**: `D100` `CIO10` `W0.00` `H5` `A0` `E0_100` `T1` `C1`（`.00`〜`.15` はビット）。BCD の値は `bcd16` / `bcd32`。
- **OPC UA クライアント**: `map` の各行に `node`（`ns=2;s=Line1.Temp` のようなノード ID）。サーバーの証明書を `server_cert` に。
- **MQTT**: `publish`（タグ → ブローカー）と `subscribe`（ブローカー → タグ。外から書かれるのはここに書いたタグだけ）。トピックは `topic_prefix` + `topic`。

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
