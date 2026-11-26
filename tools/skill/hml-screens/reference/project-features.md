# project.json の機能（画面の外）

画面（`screens/*.json`）ではなく `project.json` に書く機能の一覧です。**頼まれたときだけ**足す・変える（機器や外のサーバーに関わる）。
書いたら `--check` を通す（キーの書き間違い、ないタグ・画面・ログ、暗号化なしなどを見つける）。
**パスワードは `project.json` に書かない**（装置の System 画面で設定する。`--check` が指摘する）。

目次: [テンプレート画面](#テンプレート画面) / [アラーム](#アラーム-alarms) / [データのログとトレンド](#データのログとトレンド-data_log) /
[レシピ](#レシピ-recipes) / [スクリプト](#スクリプト-scripts) / [日付と時刻](#日付と時刻) / [機器なしで値を動かす](#機器なしで値を動かす) /
[スケジュール](#スケジュール-schedules) / [音](#音-sound) /
[機器間のデータ転送](#機器間のデータ転送-transfers) / [メール](#メール-mail) / [ログの送信](#ログの送信-uploads) /
[遠隔表示](#遠隔表示-remote)

`screens` に並べるファイルは**プロジェクトのフォルダーの中**の相対パスだけ（`..`、絶対パス、ドライブ文字は不可。デザイナーが開かない）。

アラーム・トレンド・レシピの画面を頼まれたら、その部品が要る設定（`alarms`、`data_log` のグループ、`recipes`）も頼まれたものとして足してよい。
プロジェクトにもう設定があれば、**その書き方に合わせて足す**（既存の項目は消さない・名前を変えない）。足したことを報告する。

## テンプレート画面

ヘッダーやナビゲーションを 1 つの画面（テンプレート）に置き、ほかの画面の土台にする。**これは画面ファイルのキー**。

```json
// screens/frame.json: 土台（ふつうの画面と同じ書き方。"${名前}" が各画面の値に置き換わる）
{ "name": "frame", "scrollable": false, "style": { "bg_color": "#141a22", "pad": 0 }, "children": [
  { "type": "label", "x": 16, "y": 12, "text": "${title}", "style": { "font": 24 } },
  { "type": "button", "x": 16, "y": 424, "w": 120, "h": 48, "text": "@main", "on_click": { "action": "goto", "screen": "main" } } ] }

// screens/report.json: 土台の上に作る画面
{ "name": "report", "template": "frame", "template_args": { "title": "@report" }, "children": [ ... ] }
```

- 画面のキー: `template`（土台の画面の名前）、`template_args`（`{ "名前": 文字・数値・true/false }`）。
- 土台の部品が先に置かれ、画面自身の部品がその上に載る。土台の `style` `scrollable` `flex` も引き継ぎ、画面側に書けばそちらが優先。
- 置き換わるのは土台の部品の `${名前}`（`${screen}` は画面の名前。渡さなかったものは空）。画面自身の部品では置き換わらない。
  **`${名前}` は `text` などの表示する文字に使う。** 色（`bg_color`）やタグ名に書くと、土台の画面そのものには `${…}` のまま残るので、
  `--check` が土台を問題にする（`runtime: unknown color '${…}'`）。
- 「今いる画面のボタンだけ色を変える」は土台でできる: タグ `sys.screen`（表示中の画面の名前。ポップアップは含まない）を、
  各ボタンの `bind` に書く: `"bind": { "bg_color": { "tag": "sys.screen", "map": { "trend": "#26c6da", "default": "#192538" } } }`
  （`trend` はそのボタンの行き先）。下線などを出すなら、`visible` に `{ "tag": "sys.screen", "eq": "trend" }`。
- 土台もふつうの画面なので、`project.json` の `"screens"` に入れる。土台にポップアップは使えない。8 段まで重ねられる。
- **土台を重ねるとき**（`frame` の上に `equip_frame`、その上に各画面）: 真ん中の土台は自分の `template_args` で、上の画面から
  来る値を下へ渡す: `{ "name": "equip_frame", "template": "frame", "template_args": { "title": "${title}" }, ... }`。
  渡さないと `--check` が「template … has ${title}, which template_args does not give」と出す。
- 画面自身の部品は、土台の部品（ヘッダー、ナビゲーション）に重ならない位置に置く。`--render` の画像には土台も描かれる。
- 既存の画面がテンプレートを使っているプロジェクトでは、新しい画面も同じ `template` を付ける（共通部分を書き写さない）。

## アラーム (`alarms`)

タグの値が条件を満たす間、アラームが出る（`alarm_list` / `alarm_banner` / `alarm_summary` に出る。記録は `logs/alarm.db`）。

```json
"alarms": {
  "items": [
    { "name": "oven_temp_high", "tag": "oven_temperature", "condition": "gt", "value_tag": "oven_temp_hi", "severity": "critical", "message": "@alarm_oven_temp_high" },
    { "name": "film_low", "tag": "packaging_film_remaining", "condition": "lt", "value": 10, "severity": "warning", "message": "@alarm_film_low" },
    { "name": "plc_comm", "tag": "plc_online", "condition": "false", "severity": "critical", "message": "@alarm_plc_comm" },
    { "name": "door_open", "tag": "door_open", "severity": "info", "ack": false, "message": "@alarm_door_open" }
  ]
}
```

| キー | 内容 |
| --- | --- |
| `items[].name` | 名前（必須、重複不可。記録と CSV に出る） |
| `items[].tag` | 見るタグ（必須） |
| `items[].condition` | `"true"`（既定。タグが真の間）/ `"false"` / `"eq"` `"ne"` `"gt"` `"lt"` `"ge"` `"le"`（`value` か `value_tag` と比べる） |
| `items[].value`、`items[].value_tag` | 比べる値（定数）、または比べる相手のタグ（設定値を画面で変えられるようにするなら `value_tag`） |
| `items[].severity` | 重要度の語（既定 `"warning"`。ふつう `"info"` `"warning"` `"critical"`）。`alarm_list` の列にそのまま出る（訳せない） |
| `items[].message` | 出す文字（`"@ID"` が使える。省くと `name`） |
| `items[].ack` | `false`: 確認しなくても、条件が消えれば一覧から消える（既定 true: 確認されるまで残る） |
| `items[].enabled` | `false` で使わない |
| `ack_level` | 確認できるユーザーのレベル（`users` があるとき） |
| `enabled`、`path`、`retention_days`、`max_rows`、`max_size_mb` | 記録の設定（省いてよい。既定 `logs/alarm.db`、30 日、10 万行） |

- タグ: `sys.alarm.active`（出ている数）、`sys.alarm.unacked`（未確認の数）、`sys.alarm.latest`（いちばん新しいものの文字）。
- 確認の動作: `{ "action": "call", "fn": "alarm.ack_all" }`（`alarm_list` の行を押しても確認できる）。
- 「A かつ B のとき」は 1 つのアラームでは書けない。bool のタグをスクリプトで計算し、そのタグを `tag` にする。
- `--check --render` では、条件が成り立つ値を `--set` すると出る（`--set oven_temperature=260`）。

## データのログとトレンド (`data_log`)

タグの値を記録する（`logs/data.db`）。`trend` 部品はここの**グループ**を描く。

```json
"data_log": {
  "groups": [
    { "name": "process", "trigger": "interval", "interval_ms": 1000, "tags": ["oven_temperature", "proofing_temperature", "proofing_humidity"] },
    { "name": "state", "trigger": "change", "tags": ["mixer_running", "oven_heater_on"] },
    { "name": "batch", "trigger": "tag", "trigger_tag": "batch_done", "tags": ["production_count", "defect_count"] }
  ]
}
```

| キー | 内容 |
| --- | --- |
| `groups[].name` | グループの名前（必須、重複不可。`trend` の `group`、`log_view` の `group`、`uploads` の `"data:<名前>"`） |
| `groups[].tags` | 記録するタグ名の配列（必須。数値のタグ。`trend` に出せるのはここにあるタグだけ） |
| `groups[].trigger` | `"interval"`（既定。`interval_ms` ごと、100 以上、既定 1000）/ `"change"`（どれかが変わったとき）/ `"tag"`（`trigger_tag` が偽から真になったとき）/ `"manual"`（`data_log.sample` の動作のときだけ） |
| `groups[].enabled` | `false` で記録しない |
| `enabled`、`path`、`retention_days`、`max_rows`、`max_size_mb` | 記録の設定（省いてよい。既定 `logs/data.db`、30 日、100 万行） |

- トレンドを描くなら `trigger` は `interval`（1000〜5000 ms）。範囲の大きく違う値（温度 200 °C と湿度 80 %）は、
  グループは同じでもよいが、`trend` 部品を分ける（縦軸は 1 本）。
- 記録は装置の SD カードに書かれる。速い周期（100 ms）にたくさんのタグを入れない。

## レシピ (`recipes`)

タグの組（品種ごとの設定値）に名前を付けて持ち、`recipe_table` で選んでタグへ書き込む。

```json
"recipes": {
  "level": 2,
  "books": [
    { "name": "bread",
      "items": [ { "tag": "mixer_rpm_sp", "label": "Mixer speed", "unit": "rpm" }, { "tag": "mix_time_sp", "label": "Mixing time", "unit": "min" },
                 "proofing_temp_sp", "oven_temp_sp" ],
      "recipes": [
        { "name": "Shokupan", "values": { "mixer_rpm_sp": 120, "mix_time_sp": 600, "proofing_temp_sp": 38, "oven_temp_sp": 210 } },
        { "name": "Baguette", "values": { "mixer_rpm_sp": 90,  "mix_time_sp": 480, "proofing_temp_sp": 26, "oven_temp_sp": 240 } }
      ] }
  ]
}
```

| キー | 内容 |
| --- | --- |
| `books[].name` | ブックの名前（必須、1〜32 文字、`/ \ : * ? " < > | .` は使えない。`recipe_table` の `book`） |
| `books[].items` | レシピが持つタグ（必須。設定値のタグにする: 書き込むと機器へ送られる）。タグ名か `{ "tag", "label", "unit" }`: `recipe_table` には「label (unit)」と出る（`label` は `"@ID"` も可。省くとタグ名）。**読みやすい名前と単位を必ず付ける** |
| `books[].recipes` | 最初から入っているレシピ: `{ "name", "values": { タグ名: 値 } }`（名前は重複不可） |
| `level` | 書き込み・保存・削除ができるユーザーのレベル（`users` があるとき。0 = だれでも） |
| `verify_ms` | 書き込んだあと、機器の値が同じになったか確かめる時間（既定 2000、0 = 確かめない） |
| `path` | 装置で変えたレシピを置くフォルダー（既定 `recipes`。そこにファイルがあれば、プロジェクトの `recipes` より優先） |

- レシピの名前は表示にそのまま出る（`"@ID"` は使えない）。日本語の名前にするならフォントが要る。
- `sys.recipe.<ブック名>.selected` は**一覧で選んでいる**レシピ、`sys.recipe.<ブック名>.written` は**最後に書き込んだ（今使っている）**レシピ
  （書き込むまでは空）。「今のレシピ」をヘッダーなどに出すなら `written`。最後の操作の結果は `sys.recipe.status`。

## スクリプト (`scripts`)

Lua で、タグを計算する・画面を切り替えるなど。`{ "action": "script", "name": "<名前>" }` で呼ぶか、`trigger` で自動に動く。

```json
"scripts": [
  { "name": "can_start", "trigger": "tag", "tags": ["auto_mode", "mixer_running"], "code": "tag.can_start = not tag.auto_mode and not tag.mixer_running" },
  { "name": "line", "trigger": "interval", "interval_ms": 500, "file": "scripts/line.lua" },
  { "name": "init", "trigger": "startup", "code": ["tag.shift = 1", "tag.batch = 0"] }
]
```

| キー | 内容 |
| --- | --- |
| `name` | 名前（重複不可） |
| `code` / `file` | Lua のコード（文字列か、1 行ずつの配列）、またはプロジェクトの中の `.lua` ファイル |
| `trigger` | `"manual"`（既定。動作から呼ぶ）/ `"startup"` / `"interval"`（+ `interval_ms`）/ `"tag"`（+ `tags`: どれかが変わったとき）/ `"screen_show"` `"screen_hide"`（+ `screen`） |
| `enabled` | `false` で使わない |

Lua で使えるもの: `tag.名前`（読み書き。`sys.` のタグは `tag["sys.alarm.active"]`）、`goto_screen("名前")`、`back()`、
`current_screen()`、`run("スクリプト")`、`call("fn", "arg")`（`call` の動作と同じ）、`log(...)` / `warn(...)`、`millis()`、
`os.time()` / `os.date(...)` / `os.clock()`、`event.trigger` / `event.tag` / `event.value` / `event.screen`。
ライブラリは `string` `table` `math` `utf8` `coroutine`（ファイルやプロセスは使えない）。1 回 500 ms を超えると止められる。
メモリはスクリプト全体で 32 MB まで（超えたスクリプトが "not enough memory" で失敗する）。大きな表や文字列をグローバルにためこまない。

- 書いたタグは、機器につながるタグなら機器へ送られる。**機器が持つ値（実際の温度など）をスクリプトで書かない**（下の「機器なしで値を動かす」は別）。
- `--check --render` でもスクリプトは動く（周期のものは描く前に数回動く）。

## 日付と時刻

ランタイムが持つ文字列のタグ: `sys.date`（`2026-11-22`）、`sys.time`（`13:05:07`）、`sys.datetime`（`2026-11-22 13:05:07`）。
装置の時刻（その地域の時刻）で、秒ごとに変わる。ヘッダーの時計は `{ "type": "label", "bind": { "text": "sys.datetime" } }`。

- 別の形（`13:05`、曜日つき）が要るときは、文字列のタグを作り、周期のスクリプトで `tag.clock = os.date("%H:%M")` と書く。
- `--check --render` の画像は、実行したときの時刻になる。決まった時刻で描くなら `--set "sys.datetime=2026-11-22 08:30:00"`
  （`--set` はそのタグだけを変える。`sys.date` / `sys.time` も出しているなら、それぞれに `--set` する）。

## 機器なしで値を動かす

デモや確認で PLC なしに値を動かすのは、**頼まれたときだけ**（頼まれていなければ、値は `tags` の `init` のまま止まる。それで正しい）。
動いて見える値は本物ではないので、**画面に必ず「SIMULATION」と出す**（`sys.sim.active` が 1 の間だけ出る `label`。下）。

**`simulation`（`project.json`）に書く**。`drivers`（機器との接続）とは別の場所なので、「`drivers` に触らない」という決まりのままで足せる。
`drivers` の `"type": "sim"` は使わない（既にあれば、そのままにする）。

```json
"simulation": { "items": [
  { "tag": "oven_temperature", "mode": "sine", "min": 208, "max": 212, "period_ms": 30000 },
  { "tag": "mixer_rpm", "mode": "follow", "target": "mixer_rpm_sp", "rate": 40, "when": "mixer_running", "idle": 0, "noise": 1.5, "min": 0, "max": 300 },
  { "tag": "production_count", "mode": "counter", "min": 0, "max": 999999, "period_ms": 3000, "when": "line_running" },
  { "tag": "line_state", "mode": "cycle", "values": ["Idle", "Run"], "period_ms": 10000 }
] }
```

| キー | 内容 |
| --- | --- |
| `tag` | 書くタグ（必須） |
| `mode` | `sine` `triangle` `ramp`（`min` → `max` を繰り返す）/ `square`（`max` と `min` を交互）/ `random` / `counter`（`period_ms` ごとに 1 増え、`max` の次は `min`）/ `cycle`（`values` の文字を順に）/ `follow`（下） |
| `min`、`max`、`period_ms` | 範囲（省くとタグの `min` / `max`）、周期（既定 1000） |
| `when` | タグ名（真の間だけ動く）か `{ "tag": "mode", "eq": 2 }`。偽の間は書かない（カウンターは止まる） |
| `target`、`rate`、`idle` | `follow`: `target`（タグか数値）へ 1 秒に `rate` ずつ近づく。`when` が偽の間は `idle`（既定 `min`）へ。運転ボタンで回転数が上がる、ヒーターで温度が上がる、という動き |
| `noise` | `sine` `triangle` `ramp` `follow` の値に ± `noise` の揺れを足す（本物らしく見せる。小さく）。`follow` は `when` が偽の間は揺らさず、揺れても `min`..`max` の外に出ない |
| `enabled`（`simulation` の） | `false` で止める |

- **始まりの値**: `counter` はタグの `init`（`min`..`max` の中なら）から数え続ける（今日の生産数 3420 から増える）。波形は位相 0 から:
  `sine` は真ん中、`ramp` / `triangle` は `min` から始まる。起動直後にアラームの条件に入らないよう `min` / `max` を決める（下限アラームの
  あるタグを `ramp` で `min` から始めない）。bool のタグは `square`（`period_ms` の半分ずつ真と偽）で動かす。
- 操作に応じて動かす: ボタンが書くタグ（`mixer_running`、設定値 `mixer_rpm_sp`）を `when` と `target` に使う。
  「A かつ B のとき」はスクリプトで bool のタグを作って `when` にする。もっと複雑な動きはスクリプト（`projects/demo` の `pump`）。
- 機器の値（実際の温度など）を、機器がつながったあとも書き続けないよう、PLC のつながりを示すタグがあれば
  `"when": { "tag": "plc_online", "eq": false }` を付ける。
- 表示: `{ "type": "label", "text": "SIMULATION", "bind": { "visible": "sys.sim.active" }, "style": { "text_color": "#f5b942" } }`
  をヘッダー（テンプレート）に置く。`sys.sim.active` は、`simulation` か有効な `sim` ドライバがあるとき 1。
- `--check --render` では動かない（値は `init`。`--set` で確かめる）。動きを見るのは「押したあとの様子を確かめたいとき」の手順で
  （コピーの `drivers` を消しても `simulation` は残る）。

## スケジュール (`schedules`)

決まった時刻にアクション（ボタンと同じ動作）を実行する。

```json
"schedules": [
  { "name": "start", "at": "08:00", "days": "weekdays", "do": { "action": "set", "tag": "run_enable", "value": true } },
  { "name": "report", "at": "23:55", "catch_up": true, "do": [ { "action": "call", "fn": "upload.run", "arg": "nightly" } ] },
  { "name": "service", "at": "2026-12-01 09:00", "do": { "action": "popup", "screen": "service_due" } }
]
```

| キー | 内容 |
| --- | --- |
| `name` | 名前（必須、重複不可、64 文字まで） |
| `at` | `"HH:MM"`（毎日 / `days` の曜日）、または `"YYYY-MM-DD HH:MM"`（1 回だけ） |
| `days` | `"daily"`（既定）、`"weekdays"`、`"weekend"`、または `["mon", "wed"]`（`sun mon tue wed thu fri sat`）。日付つきの `at` には書かない |
| `do` | アクション 1 つ、またはその配列（必須） |
| `enabled` | `false` で実行しない |
| `catch_up` | `true`: 装置が止まっていた間に過ぎた時刻を、次の起動時に 1 回実行する |

装置では System > Schedule で時刻・曜日・オン / オフを変えられる（`do` は変えられない）。200 件まで。

## 音 (`sound`)

`"sound": {}` があるだけで、タッチ音（`click`）とアラーム音（`alarm`、確認されるまで 3 秒ごと）が鳴る。

| キー | 内容 |
| --- | --- |
| `volume` | 0〜100（既定 80） |
| `mute` | `true` で消音 |
| `touch`、`alarm` | 音: `"click"` `"beep"` `"alarm"`、プロジェクトの中の `.wav`（`"assets/tap.wav"`）、または `false` |
| `alarm_every_s` | アラーム音を鳴らし直す間隔の秒（既定 3、0 = 1 回だけ） |
| `output` | `"auto"`（既定）/ `"audio"` / `"gpio"` / `"none"` |
| `device` | Linux の ALSA デバイス（Raspberry Pi のイヤホン端子: `"plughw:CARD=Headphones,DEV=0"`） |
| `gpio`、`gpio_active_low`、`gpio_chip` | ブザーをつないだ GPIO（Linux） |

画面からの使い方（消音、音量、ブザー停止、音を鳴らす）は `binding-and-actions.md` の「画面の外の機能を画面から使う」。

## 機器間のデータ転送 (`transfers`)

タグの値を別のタグに写す（PLC → 別の PLC など）。切断中は書かない。

```json
"transfers": [
  { "name": "speed", "from": "plc1_speed", "to": "plc2_speed" },
  { "name": "temp", "from": "plc1_temp", "to": "plc2_temp", "trigger": "cyclic", "every_ms": 1000, "scale": 0.1, "when": "run" }
]
```

| キー | 内容 |
| --- | --- |
| `name` | 名前（必須、重複不可） |
| `from`、`to` | タグ名（必須。同じタグは不可、`to` に `sys.*` は不可） |
| `trigger` | `"change"`（既定）/ `"cyclic"`（+ `every_ms`、100 以上、既定 1000） |
| `scale`、`offset` | 書く値 = 元の値 × `scale` + `offset` |
| `out_of_range` | 転送先のタグの `min` / `max` を外れた値: `"skip"`（既定）/ `"clamp"` |
| `when` | 条件: タグ名、または `{ "tag": "mode", "eq": 2 }`（`eq ne gt lt ge le`） |
| `enabled` | `false` で実行しない |

輪（A → B → A）は実行されない（`--check` が指摘）。500 件まで。

## メール (`mail`)

アラームをメールで送る（SMTP）。

```json
"mail": { "host": "smtp.example.com", "security": "starttls", "username": "hmi",
          "from": "hmi@example.com", "to": ["shift@example.com"],
          "alarms": { "severities": ["critical"], "subject": "[{project}] {alarm.message}", "body": "{alarm.time}\n液位 {level} %" } }
```

| キー | 内容 |
| --- | --- |
| `host`、`port` | メールサーバー（必須）。`port` を省くと 587 / 465 / 25 |
| `security` | `"starttls"`（既定）/ `"tls"` / `"none"`（暗号化なし: `--check` が知らせる） |
| `username` | ログイン名（省くとログインしない）。**`password` は書かない**（装置の System > Mail） |
| `from`、`to` | 差出人のアドレス（必須）、宛先（1 つ、または配列。必須、20 件まで） |
| `verify`、`ca_file` | 証明書を検証する（既定 true）。`ca_file` はプロジェクトの中のファイル |
| `timeout_ms`、`retry_s`、`retries`、`queue` | 待ち時間、再送の間隔と回数、送信待ちの上限 |
| `alarms` | `events`（`"raised"` 既定、`"cleared"`、`"acked"`）、`severities`、`min_interval_s`、`subject`、`body`。`false` でアラームは送らない |
| `enabled` | `false` で送らない |

件名・本文の `{名前}`: タグの値、`{project}`、`{time}`、`{alarm.name}` `{alarm.message}` `{alarm.severity}` `{alarm.value}` `{alarm.event}` `{alarm.time}`。
`"enabled": false` でも設定は検査され、`sys.mail.*` のタグもある（送らないだけ）。画面からの使い方は `binding-and-actions.md`。

## ログの送信 (`uploads`)

ログを CSV ファイルにしてサーバーへ送る。**自動では送らない**: `upload.run` アクションをスケジュールやボタンに置く。

```json
"uploads": [ { "name": "nightly", "url": "https://files.example.com/hml/", "username": "hmi",
               "logs": ["data:line1", "alarms", "operation", "system"] } ]
```

| キー | 内容 |
| --- | --- |
| `name` | 名前（必須。英数字、`_`、`-`） |
| `url` | `https://サーバー/フォルダー/`、`http://…`、`ftp://サーバー/フォルダー/`（http と ftp は暗号化なし: `--check` が知らせる）。URL にユーザー名・`?`・`..` は書けない |
| `method` | HTTP: `"put"`（既定）/ `"post"` |
| `username` | ログイン名。**`password` は書かない**（装置の System > Upload） |
| `logs` | 送るもの（必須）: `"data:<data_log のグループ名>"`、`"alarms"`、`"operation"`、`"system"`。そのログがプロジェクトにあること |
| `verify`、`ca_file`、`timeout_ms`、`retry_s`、`retries`、`keep` | 証明書の検証、待ち時間、再送、送信待ちのファイルの上限 |
| `enabled` | `false` で送らない |

スケジュールから送るなら `"do": { "action": "call", "fn": "upload.run", "arg": "nightly" }`。画面からの使い方は `binding-and-actions.md`。
`"enabled": false` でも設定は検査され、`sys.upload.*` のタグもある。10 件まで。

## 遠隔表示 (`remote`)

装置の画面を VNC ビューアーで見る・操作する。

```json
"remote": { "enabled": true, "port": 5900, "view_only": false }
```

| キー | 内容 |
| --- | --- |
| `enabled` | `true` で待ち受ける（既定 `false`） |
| `port` | 既定 5900 |
| `view_only` | `true` で見るだけ |

- **`password` は書かない**（書いても使われず、`--check` が指摘する）。パスワードは装置の System > Remote で設定する。
  設定するまでは装置自身（127.0.0.1）からしかつなげない。そのことを使う人に伝える。
- VNC は暗号化されない。閉じたネットワーク向け。
