# bind、動作、ポップアップ、多言語、専用の部品

キーの一覧は `widgets.md`。ここは使い方です。

## タグ（`project.json` の `tags`）

```json
{ "name": "temp",  "type": "float",  "init": 20.0 },
{ "name": "speed", "type": "int",    "init": 0, "min": 0, "max": 100 },
{ "name": "run",   "type": "bool",   "init": false },
{ "name": "state", "type": "string", "init": "Idle" }
```

- `min` / `max` を付けると、画面からの書き込みがその範囲に丸められる。操作できる数値のタグには付ける。
- `sys.` で始まるタグはランタイムが持つ（`sys.date`、`sys.time`、`sys.datetime`、`sys.screen`（表示中の画面の名前）、`sys.lang`、`sys.user.name`、`sys.user.level`、`sys.alarm.active`、`sys.alarm.unacked`、`sys.screen.off` など）。作らない。
  ここと `project-features.md` にない `sys.` の名前は書かない（`--check` は、そのプロジェクトにない `sys.` のタグを知らせる。
  `sys.sound.*` などは、その機能が `project.json` にあるときだけある）。

## bind

短い形 `"bind": "tag"` は、部品に合ったプロパティにつながる（`widgets.md` の表）。入力する部品は自動で双方向。

```json
"bind": {
  "text":       { "tag": "temp", "format": "%.1f °C" },
  "text":       { "tag": "state", "map": { "0": "@stopped", "1": "@running", "2": "@fault", "default": "---" } },
  "value":      { "tag": "pressure", "scale": 10 },
  "checked":    "run",
  "on":         "run",
  "visible":    { "tag": "alarm", "eq": 2 },
  "enabled":    { "tag": "auto_mode", "invert": true },
  "bg_color":   { "tag": "alarm", "map": { "0": "#2e7d32", "1": "#f9a825", "2": "#c62828", "default": "#555" } },
  "text_color": { "tag": "plc_online", "map": { "true": "#4caf50", "false": "#78909c" } }
}
```

| キー | 意味 |
| --- | --- |
| `format` | printf 形式、変換指定子は 1 つ。`%d` `%x` 整数、`%f` `%g` 実数、`%s` 文字列。`%` そのものは `%%`（`"%d %%"`） |
| `scale` / `offset` | 部品の値 = タグの値 × scale + offset（書き込みは逆算） |
| `invert` | 真偽を反転 |
| `eq` `ne` `gt` `lt` `ge` `le` | 条件。`visible` `hidden` `enabled` `disabled` `checked` `on` で使う。比べる相手は定数か `{ "tag": "temp_hi" }` |
| `map` | 値 → 色（`bg_color` `text_color`）、値 → 文字（`text`。`"@ID"` が使える）。`default` がその他。bool は `"true"` / `"false"`。文字列のタグには、その文字のキーが当たる（`{ "main": "#26c6da" }`） |
| `part` | `bg_color` を付ける場所: `main`（既定）/ `indicator` / `knob` |

- 短い形と一緒にほかの種類も付けたいときは、長い形で書く:
  `"bind": { "value": "speed_sp", "enabled": { "tag": "auto_mode", "invert": true } }`。
- `enabled` / `disabled` はどの部品にも付けられ、無効のあいだは薄く表示されて押せない。親の `panel` に付けても
  子は無効にならないので、操作する部品それぞれに付ける。
- 条件は 1 つのタグだけ（「A かつ B」は書けない）。要るときは、条件をまとめた bool のタグを作り、スクリプトで
  計算する。`project.json` の `scripts` に
  `{ "name": "can_run", "trigger": "tag", "tags": ["auto_mode", "run"], "code": "tag.can_run = not tag.auto_mode and not tag.run" }`。
- 状態の番号や文字列のタグを言葉で出すときは、`text` の `map`（上の例。値ごとに `label` を重ねなくてよい）。
  当たるキーも `default` もない値は、そのまま（`format` で）出る。言葉と一緒に色も変えるなら、同じタグで `text_color` か `bg_color` の `map` を並べて書く。
  言葉はすべての値の分を書き、いちばん長い言葉で `--set` して入るか確かめる。
- `checked` に `eq` を付けたボタン（`"checkable": true`）は、押すとその値をタグに書く。同じタグで値を変えて並べると
  択一（ラジオボタン）になる。
- 同じ場所に条件違いの `panel` を重ねて `visible` で切り替えると、状態ごとの表示になる。

## スタイルを付ける場所

`style` は部品の本体。ほかは部品の一部や状態に付く。

| キー | 付く所 |
| --- | --- |
| `style_label` | `button` / `checkbox` の中の文字（ボタンの文字の大きさは `"style_label": { "font": 20 }`） |
| `style_indicator` | `bar` / `slider` / `arc` の値の部分、`switch` / `checkbox` の印 |
| `style_indicator_checked` | ON のときのその部分 |
| `style_knob` | `slider` / `arc` / `switch` のつまみ |
| `style_items` | `keyboard` のキー、`dropdown` / `roller` の項目 |
| `style_selected` | `roller` / `dropdown` の選ばれている項目 |
| `style_pressed` `style_checked` `style_disabled` `style_focused` | 押されている / ON / 無効 / 入力中 のときの本体 |
| `style_scrollbar` | スクロールバー |

`w` `h` を省くと、大きさは中身とテーマで決まる。並べてそろえるもの、押すものには `w` `h` を書く。`panel` の内側の余白も
テーマで決まるので、位置を計算で合わせたいときは `"style": { "pad": 12 }` のように自分で決める。

## 画像

画像はプロジェクトのフォルダーの中のファイル（ふつう `assets/`）を、`project.json` からの相対パスで書く。
**画像は AI が作らない。** `assets/` にあるものを使い（中身と大きさは画像として開いて確かめる）、合うものがなければ、
要る画像（何の絵か、幅 × 高さ、背景を透かすか）を伝えてユーザーに用意してもらう。それまでは `panel` と `label` で場所を取っておく。

```json
{ "type": "image", "x": 16, "y": 16, "src": "assets/logo.png" },
{ "type": "image", "w": 200, "h": 120, "src": "assets/pump.png", "fit": "contain" },
{ "type": "image", "src": "assets/fan.png",
  "bind": { "src": { "tag": "state", "map": { "0": "assets/fan_off.png", "1": "assets/fan_on.png", "default": "assets/fan_err.png" } } } },
{ "type": "image", "src": "assets/home.png", "on_click": { "action": "goto", "screen": "main" } },
{ "type": "panel", "w": "100%", "h": "100%", "style": { "bg_image": "assets/plant.png" }, "children": [ ... ] }
```

| 形式 | できること |
| --- | --- |
| PNG | すべて。透過あり。`fit`・`zoom`・`rotation` が使える |
| JPEG / BMP | **元の大きさで表示するだけ**（`fit`・`zoom`・`rotation` は効かない）。写真は、表示する大きさのファイルを用意してもらう |
| GIF | アニメーションとして再生される（元の大きさ。`bind` で切り替えられない） |

- `fit`（PNG）: `w` `h` の枠に、`contain`（比率を保って全体が入る）/ `stretch`（引き伸ばす）/ `tile`（敷き詰める）/ `center`。
  省くと元の大きさで、`w` `h` は画像の大きさになる。
- 値で絵を替える: `bind` の `src` に `map`（値 → ファイル。bool は `"true"` / `"false"`、その他は `"default"`）。
  当てはまるファイルがない値のときは何も表示しない。文字列のタグにファイル名を入れて `"bind": { "src": "タグ" }` でもよい。
- 背景に敷く: `style` の `bg_image`（画面・`panel`・`button` など。中央に元の大きさ。`"bg_image_tiled": true` で敷き詰め、
  `bg_image_opa` で薄く）。`style_pressed` などに書けば、その状態のときだけ出る。上に載せる部品の文字が読めるか、画像で確かめる。
- 絵を重ねる: 同じ親の中で、あとに書いた部品が上に描かれる。2 つの `image` に同じ `align`（`center` など）と同じずれを付けると
  中心が重なる（ポンプの絵の上に羽根、など）。`rotation` と `zoom` は絵の中心を軸にする。`zoom` と `animate` の回転は一緒に使える。
- 拡大すると縁がぎざぎざになる（`fit` も `zoom` も補間しない）。大きく見せたい絵は、表示する大きさのファイルを用意してもらう。
  縮小はきれいに出る。
- 大きさの調べ方（PowerShell）: `Add-Type -AssemblyName System.Drawing; $i = [System.Drawing.Image]::FromFile("<ファイル>"); "$($i.Width)x$($i.Height)"; $i.Dispose()`。
  白い絵に透明の背景のアイコンは、画像として開くと真っ白に見える。暗い `panel` の上に置いて `--render` で確かめる。
- 押せる絵: `image` に `on_click`。小さい絵は、まわりに `panel` を置いて押せる範囲を 48px 以上にする。
- 単色のアイコン（白い PNG）は `"style": { "image_recolor": "#4caf50" }` で色を付けられる（`bind` の色は `image` には使えない）。
- 1 枚の画像は幅 × 高さが約 400 万ピクセルまで。画面より大きい画像は使わない（メモリは 幅 × 高さ × 4 バイト要る）。
- `--check` が見るもの: ファイルがあるか、プロジェクトの中か、形式、JPEG / BMP に `fit` などを付けていないか。
  壊れたファイルや大きすぎる画像は `runtime:` の行に出る。

## アニメーション

どの部品にも `animate` を書ける（1 つ、または配列）。**その画面が表示されていて、`when` が成り立つ間だけ**動き、
止まると部品は元の見た目に戻る。`when` を省くと、画面が出ている間ずっと動く。

```json
{ "type": "label", "text": "@alarm", "animate": { "preset": "blink", "when": "alarm_active" } },
{ "type": "image", "src": "assets/fan.png", "animate": { "preset": "spin", "time": 1500, "when": "run" } },
{ "type": "panel", "w": 24, "h": 24, "animate": { "prop": "x", "from": 100, "to": 400, "time": 2000, "path": "linear", "when": { "tag": "flow", "gt": 0 } } },
{ "type": "panel", "style": { "bg_color": "#37474f" }, "animate": { "prop": "bg_color", "to": "#c62828", "time": 600, "playback": true, "when": { "tag": "state", "eq": 2 } } },
{ "type": "image", "src": "assets/conv1.png", "animate": { "prop": "frames", "files": ["assets/conv1.png", "assets/conv2.png", "assets/conv3.png"], "time": 600, "when": "run" } }
```

| キー | 意味 |
| --- | --- |
| `preset` | `blink`（点いたり消えたり、1 秒周期）/ `pulse`（ゆっくり明滅）/ `spin`（1 回転 2 秒）。下のキーで上書きできる |
| `prop` | 動かすもの: `opa`（不透明度 0〜100）/ `x` `y` `w` `h`（px）/ `rotation`（度）/ `zoom`（1 = 元の大きさ）/ `bg_color` `text_color` / `frames`（`image` の絵を順に替える） |
| `from` / `to` | 両端の値。`from` を省くと今の値から |
| `time` | 片道の時間（ミリ秒、既定 1000）。`frames` では一巡の時間 |
| `playback` | true で往復する |
| `repeat` | 回数（省くとずっと） |
| `path` | 動き方: `linear`（既定）/ `ease_in` / `ease_out` / `ease_in_out` / `step`（時間の終わりで切り替わる） |
| `delay` | 始まるまでの時間（ミリ秒） |
| `when` | タグ名（真のとき）か条件（`{ "tag": "state", "eq": 2 }`。`bind` の条件と同じ書き方） |

- `preset` の `time`: `blink` は既定 500（点いている 500 + 消えている 500 = 1 秒周期）、`pulse` は 700、`spin` は 2000（1 回転）。
  遅くしたいときは `time` を大きくする。既定の `blink` は十分ゆっくりで、そのまま使ってよい。
- `when` の条件は `bind` と同じで、別のタグとも比べられる: `"when": { "tag": "temp", "gt": { "tag": "temp_hi" } }`。
- 止まると元の見た目に戻る（回転の途中でも元の角度に、点滅の途中でも見える状態に）。**「条件のときだけ出て、その間点滅する」**
  ものは、`visible` と `animate` の両方に同じ条件を書く:
  `{ "type": "image", "src": "assets/warning.png", "bind": { "visible": { "tag": "temp", "gt": { "tag": "temp_hi" } } }, "animate": { "preset": "blink", "when": { "tag": "temp", "gt": { "tag": "temp_hi" } } } }`

使いどころと決まり:
- **動きは「いま気づいてほしいこと」だけに使う**（未確認のアラーム、運転中、流れている）。飾りに動かさない。
  1 画面で同時に動くものは少なく（目安 3 つまで）。常に動くもの（`when` なし）は避ける。
- 点滅（`opa` と色）は 1 秒に 3 回より速くできない（ランタイムが遅くする）。警告は `pulse` か、ゆっくりした `blink` にする。
- 動いても読めること: 値や操作するボタンそのものは点滅させない。枠・帯・アイコンを動かす。
- `rotation` と `zoom` は `image`（PNG）に使うのが軽い。ほかの部品に付けると描画が重くなるので、小さい部品だけにする。
  JPEG / BMP の `image` は回転・拡大できない。
- `x` `y` を動かす部品は `align` を付けず、`from` / `to` に座標を書く。flex で並べた中の部品は動かさない。
- `frames` の絵は同じ大きさにする。
- 確かめ方: `--render` に `--at <ミリ秒>` を付けると、その画面が出てからおよそその時間がたった絵になる。
  時刻を変えて 2 枚描き、動いていること・`when` が成り立たないとき（`--set`）は止まっていることを見る。
  `--at` は 1 回の実行で 1 つ（全部の `--render` に効く）なので、時刻ごとに実行する。
  **点滅は「消えている」瞬間に当たると部品がないように見える**。1 秒周期の `blink` なら `--at 450` と `--at 950` のように
  半周期ずらした 2 枚を描き、片方に出ていればよい。回転は `--at 200` と `--at 700` で角度が違えばよい。

## 動作

イベント（`on_click` など）に、動作 1 つか配列（順に実行）を書く。

```json
"on_click": [
  { "action": "set",    "tag": "run", "value": false },
  { "action": "toggle", "tag": "lamp" },
  { "action": "inc",    "tag": "speed", "by": 10 },
  { "action": "goto",   "screen": "settings", "anim": "slide_left" },
  { "action": "back" },
  { "action": "popup",  "screen": "temp_detail", "set": { "detail_unit": 2 } },
  { "action": "close" },
  { "action": "language", "lang": "next" },
  { "action": "login" },
  { "action": "script", "code": "if tag.temp > tag.temp_hi then tag.run = false end" }
]
```

- `set` の `value` は、タグの型に合わせて JSON の値で書く（`true` / `false`、数値、文字列）。
- 押している間だけ ON: `on_press` で `set` true、`on_release` で `set` false。
- イベントはどの部品にも書ける（`panel` や `label` も、イベントを書けば押せるようになる）。
- `call` の `fn`（ランタイムに登録された動作）: `alarm.ack_all`（すべてのアラームを確認）、`data_log.sample`（`arg` に
  グループ名。今の値を記録）、`recipe.write` などレシピの操作（ふつうは `recipe_table` が行う）、
  `system_log.export` / `data_log.export` / `alarm_log.export` / `operation_log.export`（CSV に書き出す）、
  `alarm.export_summary`（アラーム集計を CSV に。`arg` は日数）、
  `sound.play`（`arg` は音の名前）/ `sound.stop` / `sound.silence`、`mail.send`（`arg` は `"件名\n本文"`）/ `mail.test`、
  `upload.run`（`arg` は `uploads` の名前。省くとすべて）。後ろの 3 組は、プロジェクトにその機能（`sound` / `mail` / `uploads`）が
  あるときだけ働く（下の「画面の外の機能を画面から使う」）。ほかの名前は書かない。
- `system` は組み込みのシステム設定画面を開く。`reload` `exit` `restart` は保守用。ふつうの画面には置かない。
- `script` は Lua。`code` に短く書くか、`name` で `project.json` の `scripts` のものを呼ぶ。タグは `tag.名前`。

## ポップアップ

画面ファイルに `"popup": true` を書くと、今の画面の上に重ねて開く。

```json
{ "name": "confirm_stop", "popup": true, "w": 420, "h": 220, "dim": true,
  "style": { "bg_color": "#1f2a38", "radius": 10, "pad": 16 },
  "children": [
    { "type": "label", "text": "@confirm_stop" },
    { "type": "button", "align": "bottom_left",  "w": 160, "h": 56, "text": "@cancel", "on_click": { "action": "close" } },
    { "type": "button", "align": "bottom_right", "w": 160, "h": 56, "text": "@stop",
      "style": { "bg_color": "#c62828" },
      "on_click": [ { "action": "set", "tag": "run", "value": false }, { "action": "close" } ] }
  ] }
```

- 開くのは `{ "action": "popup", "screen": "confirm_stop" }`。開いている間、下の画面は触れない。
- 確かめるときは `--render <開く元の画面>+confirm_stop,<ファイル.png>`。
- 大きさと位置は `w` `h` `x` `y` `align`（既定は 60% × 60%、中央）。
- 確認が要る操作（停止、削除）や、1 台分の詳細表示に使う。`"set"` で「どの装置か」をタグに渡せる。
- ポップアップも `project.json` の `"screens"` に足す。

## 多言語

`project.json` に `languages` があるとき、文字は `texts` に置いて `"@ID"` で使う。

```json
"languages": ["ja", "en"],
"texts": { "temperature": { "ja": "温度", "en": "Temperature" } }
```

- `"@ID"` が使えるのは、部品の `text` / `placeholder` / `options` の各項目、アラームの `message`、部品の `labels`。
- `bind` の `format` の中では使えない。単位や記号だけにして、説明の文字は別の `label` に分ける。
- 足す ID は既存のものと重ならない、意味のわかる英小文字と `_`。すべての言語の訳を入れる。
- 操作の言葉（「運転」「停止」: ボタン）と状態の言葉（「運転中」「停止中」: 表示）は別の意味なので、別の ID にする。
- 同じ意味の ID がもうあれば、それを使う。言い回しが少し違うだけなら既存の言葉に合わせる（既存の訳は変えない）。
- `languages` がないプロジェクトでは、文字をそのまま書く。

## 専用の部品

これらは中に自分の表示（見出し、ボタン、行）を持つので、小さすぎると中身が入らない。下の「大きさ」を守る。

| 部品 | 要るもの | 例 |
| --- | --- | --- |
| `alarm_banner` | `project.json` の `alarms` | `{ "type": "alarm_banner", "align": "top_mid", "w": "100%", "h": 36 }`（いちばん新しい未確認のアラームを帯で出す） |
| `alarm_summary` | `alarms` | `{ "type": "alarm_summary", "w": "100%", "h": 300, "days": 7 }`（アラームごとの回数と時間） |
| `alarm_list` | `alarms` | `{ "type": "alarm_list", "w": "100%", "h": 300, "mode": "current" }`（`history` で履歴） |
| `log_view` | 出すログの設定（`system_log` / `operation_log` / `data_log`） | `{ "type": "log_view", "w": "100%", "h": 300, "source": "system" }`（`operation`、`data` + `group`） |
| `trend` | `data_log` のグループ | `{ "type": "trend", "w": "100%", "h": 300, "group": "process", "tags": ["temp", "level"], "range_s": 3600 }` |
| `chart` | なし（表示している間の値だけ） | `{ "type": "chart", "w": 300, "h": 160, "series": [ { "tag": "temp", "color": "#ff9800" } ], "min": 0, "max": 100 }` |
| `recipe_table` | `recipes` のブック | `{ "type": "recipe_table", "w": "100%", "h": 300, "book": "products" }` |
| `keyboard` | なし | `{ "type": "keyboard", "align": "bottom_mid", "w": "100%", "h": "45%" }`（入力欄がある画面に 1 つ。ふだんは隠れている） |

### `alarm_summary`

- アラームごとの発生回数と発生時間の合計を、期間を選んで出す部品（保全向け: 「何が多いか」「何で長く止まったか」）。`alarms` が要る。
- 中に、上の帯（期間と並べ方のドロップダウン、合計。高さ約 46px）と行を持つ。1 行 36px、見出し 28px。
  **高さは「90 + 39 × 行数」を目安**にし、`rows` をそれに合わせる（480x320 の画面なら `rows` 5〜6）。
  **幅は 360 以上**が安心（320 では、英語の並べ方の文字や長いアラーム名が切れる）。狭い所に置くなら `"bars": false`（回数の棒を出さない）にする。
  アラームの名前が長いと 1 行に入らず切れる。名前の列が十分に取れる幅にする。
- `days` で最初の期間（1 / 7 / 30 / 0 = すべて）、`sort` で `count`（回数）か `time`（時間）。どちらも操作者が部品の上で変えられる。
- 部品の中の文字は英語（`Alarm` `Count` `Time` `24 h` …）。日本語にするには `labels` に書く:
  `"labels": { "alarm": "アラーム", "count": "回数", "time": "時間", "day": "24 時間", "week": "7 日", "month": "30 日", "all": "すべて", "by_count": "回数順", "by_time": "時間順", "empty": "アラームなし" }`
- CSV に書き出すボタンを付けるなら `{ "action": "call", "fn": "alarm.export_summary", "arg": "7" }`（引数は日数）。
- `--check --render` の画像では、`--set` でアラームが出る値にすると、その分の行が出る（過去の記録は数えない）。
  行の見え方は、こうして 1〜2 行出して確かめる。

### `alarm_list` / `alarm_banner`

- 「今出ているアラームを 1 行で」は `alarm_banner`（いちばん新しい未確認の 1 件。なければ `idle` の文字）。
  「一覧で」は `alarm_list`。
- `alarm_list` の `columns`: `time` `severity` `message` `state` から選ぶ（省くと `["time", "message", "state"]`）。
  `mode`: `current`（省いたとき。発生中と未確認）/ `history`（履歴。`days` で期間）。`rows` は出す行数の上限
  （省くと 50、履歴は 20）、`row_height` は省くと 40。`ack_on_tap` は省くと true（行を押すと確認）。
- 大きさ: 高さ ≒ 見せたい行数 × `row_height` + 16。入りきらない行はスクロールする。
- `labels` で中の文字を置き換えられる（`"@ID"` も使える）: `unacked` `active` `cleared` `empty`（1 件もないとき）
  `idle`（バナーで何もないとき）`more`（`%d` が件数）`raised` `cleared_event` `acked_event`。
  省くと英語（`UNACK`、`No alarms` など）。日本語のプロジェクトでは `labels` を付ける。
  `severity` の列に出るのは `alarms` に書かれた語（`warning` など）そのままで、訳せない。

### `trend`

- 中に、期間の見出し（上）と操作のボタン 5 つ・凡例（下）を持つ。この 2 つで高さを約 110px 使い、消せない。
  **高さは 240 以上**（グラフの部分が 130px 以上になるように）、幅は 400 以上。
- `tags`: タグ名の配列か、色を決める形 `[ { "tag": "temp", "color": "#42a5f5" }, { "tag": "level", "color": "#66bb6a" } ]`。
  省くとグループの全タグ。色を省くとテーマの色が順に付く（2 本目は橙になりやすい。警告と紛らわしければ色を決める）。
  `group` に入っていないタグは出せない（`project.json` の `data_log.groups[].tags` を見る）。
- 縦軸は 1 本。`min` と `max` を両方書くとその範囲、省くと出ているデータに合わせて自動で決まる。範囲が大きく違う値
  （温度 20〜30 と液面 0〜100）は同じ軸だと片方が平らになるので、頼まれた見え方に合わせて部品を分けるか、そのことを伝える。
- 凡例にはタグ名がそのまま出る（訳せない）。何の線かは、上に `label` で題を付けて伝える。
- `labels`: `now`（「今に戻る」ボタンの文字。省くと `Now`）。

### `xy_chart`

- 2 つのタグを横軸と縦軸にするグラフ（位置と荷重、開度と流量）。時間の流れを見せるなら `chart`（短い時間）か `trend`（記録した履歴）を使う。
- 軸のキーは `x_tag` `x_min` `x_max` / `y_tag` `y_min` `y_max`（`x` `y` は部品の位置）。範囲は必ず書く（自動では決まらない）。
  軸の両端に範囲の数字が出るだけなので、何の軸かは近くに `label` で書く（「位置 mm」「荷重 kN」）。
- 大きさは 200x160 以上。**`points` は数百まで**にする（点が増えるたびに全部の線を描き直すので、小さな装置では重い。
  Raspberry Pi 4 で 300 点なら CPU 5〜8 %、2000 点で 25〜32 %）。
- 1 サイクルごとの波形を見せるなら、サイクルの開始で真になるタグを `clear_tag` に書く。
- 複数の曲線は `series` に足す（全部で 4 本、軸は共通）。色は順に青・赤・緑・橙。赤が「異常」に見える画面では `color` を決める。
- `--check --render` の画像では点は出ない（枠と軸の数字だけ）。線の見え方は、コピーを動かす手順で確かめる。

### `table`

- 文字とタグの値を行と列で見せる部品（表示だけ）。同じ形の値が何行も並ぶとき（機械ごとの生産数と状態、レシピの一覧）に使う。
  数個の値なら `label` や `numeric_display` を並べるほうが大きく読める。
- `columns` は見出し（文字か `{ "title", "width", "align" }`）、`rows` は行の配列で、セルは文字・数値か `{ "tag": "<タグ>", "format": "%.1f", "map": { "0": "停止", "1": "運転" } }`。
  数値の列は `"align": "right"` にし、単位は `format` に入れる（`"%d 個"`）か見出しに書く。
- 状態を `map` で文字にするときは、既存の画面の言い方に合わせる（「運転 / 停止」など）。色は付けられないので、状態は文字で区別する。
- 1 行は約 36px（`font` 16 のとき。見出しも同じくらい）。高さは「(行数 + 1) × 36 + 8」を目安にし、入りきらない行は表の中でスクロールする
  （画像で最後の行が切れていないか、スクロールバーが出ていないかを見る）。
- `map` の値にも `"@ID"` を書ける（言語に合わせて訳される）。
  幅を書かない列は残りを等分するので、長い文字の列以外には `width` を書くとそろう。
- `languages` のあるプロジェクトでは、見出しとセルの文字を `"@ID"` で書く。

### `datetime_input`

- 日付・時刻をローラーで入れる部品（キーボード不要）。`"bind": "<タグ>"`、`mode`: `datetime` / `date` / `time`、`seconds` で秒。
- タグは**文字列**（`2026-11-21 13:05`、`2026-11-21`、`13:05`）か**数値**（Unix 時刻の秒。`time` は 0 時からの秒数）。
  どちらにするかは、そのタグを使う側（スクリプト、PLC）に合わせる。新しく作るなら文字列が読みやすい。
- 大きさは中身で決まる（`w` `h` は書かない）: 日付 + 時刻で幅約 350、秒つきで約 420、日付だけ 230、時刻だけ 130、高さ約 110。
  480x320 の画面では 1 行に 1 つ。隣にラベルを置くなら上に置く。
- 設定を変える入力なので、既存の画面が同じ種類の設定に `level` を付けていれば、同じ `level` を付ける。
- `--check --render` の画像では、タグの `init` の日付がローラーに出る（`init` が空なら今の時刻）。

### `meter`

- 針のメーター（表示だけ）。`"bind": "<タグ>"` で値、`min` / `max` で目盛りの範囲。値の数字は部品の中に出る（`unit` で単位、`decimals` で桁）。
- **一辺 160 以上にする**（140 未満では目盛りの数字が出ない）。正方形に置く（横長にしても、目盛りは短い辺の正方形になる）。
- 注意・異常の範囲は `zones` で色を付ける: `"zones": [ { "from": 80, "to": 100, "color": "#e53935" } ]`（赤は異常、橙は警告。飾りには使わない）。
- 目盛りの数字が込むときは `ticks` と `major_every` を減らす（例: 0〜100 なら `"ticks": 11, "major_every": 2`）。
  `(ticks - 1)` を `major_every` で割り切れるようにすると、最後の数字が `max` になる。
- `decimals` は目盛りの数字にも効く（`2` にすると `0.00 1.00` になる）。目盛りが込んだら `decimals` を減らす。
- 大きな数値をまとめて見せるなら `label` や `numeric_display` のほうが読みやすい。メーターは「どのあたりか」を一目で見せたいときに使う。

### `log_view`

- ログを表で見る部品（読むだけ）。`source`: `system`（ランタイムのメッセージ）/ `operation`（操作の記録）/ `data`（変数ロギング。`group` が要る）。
  **`project.json` にそのログの設定がないと何も出ない**（`--check` が指摘する）。ログの設定を足すのは頼まれたときだけ。
- 中に、上の帯（レベルのドロップダウン、件数、ページ送りのボタン 2 つ。高さ約 46px）と表を持つ。
  **高さは 200 以上、幅は 440 以上**。1 行は約 32px なので、`rows` は「(高さ − 90) ÷ 32」を目安にする（長いメッセージは折り返して行が高くなる。あふれた分は表の中でスクロールする）。
- `columns`: `system` は `time` `level` `source` `message` `count`、`operation` は `time` `user` `screen` `kind` `target` `old` `new`、
  `data` は `time` とグループのタグ名。省くとよく使う列（幅が 560 未満なら `system` は `time` `level` `message`、`operation` は `time` `user` `target` `new`）。
- 列の見出しは英語（`Time` `Message` …）。日本語などにするには `labels` に書く: `"labels": { "time": "時刻", "level": "種類", "message": "内容", "empty": "記録なし" }`
  （`languages` のあるプロジェクトでは `"@ID"`）。`data` の列の見出しはタグ名のまま。
- 文字で絞り込ませたいときは、文字列タグを作り、`text_display`（`"input": true`）をそのタグに結び付け、`"filter_tag": "<タグ>"` を書く（画面に `keyboard` が要る）。
- 列の幅を決めるには `widths` に列の名前ごとの幅を書く: `"widths": { "time": 150, "user": 90 }`（書かない列は残りを分ける）。
  見出しが折り返したら（日本語の「ユーザー」など）、その列を広げる。
- 2 つ並べるには幅が要る（440 + 440）。800 px の画面で操作ログとシステムログの両方を見せるなら、同じ場所に重ねて置き、
  タグ（ボタンで 0 / 1 を書く）で `bind` の `visible` を切り替える。
- 操作ログは「誰が何をしたか」の記録。見せる相手を限るなら、その画面か部品に `level` を付ける（既存の画面の `level` に合わせる）。
- `--check --render` の画像では、表は見出しだけで行は出ない（ログを読まないため）。`system` は、設定があっても
  「The system log is not recorded …」と出ることがある（検査中は記録しないため。間違いではない）。行の見え方は、コピーを動かす手順で確かめる。

### `recipe_table`

- 中に、レシピの一覧と書き込み・保存などのボタンを持つ。高さ 300 以上、幅は画面いっぱいが基本。
- `labels`: `write` `read` `save_as` `delete` `item` `recipe` `current` `ok` `cancel` `name`。

これらの部品が要る設定（`alarms`、`data_log` のグループ、`recipes`）の書き方は `project-features.md`。
プロジェクトにないのにこれらの部品を頼まれたら、設定も足してよい（既存の設定があればその書き方に合わせる。足したことを報告する）。

## ユーザーのレベル

`project.json` に `users` があるとき、部品や画面に `"level": 2` を付けると、そのレベル以上のユーザーだけが操作できる。
`"level_deny"`: `disable`（既定、薄く表示）/ `hide` / `login`（押すとログイン画面）。`users` がなければ付けない。

## 画面の外の機能を画面から使う

スケジュール、音、機器間のデータ転送、メール、ログの送信、テンプレート画面は `project.json`（と画面ファイルの `template`）に書く。
**設定キーの一覧と書き方は `project-features.md`**。頼まれたときだけ足す・変える。画面の側で使えるのは次のもの
（タグは、その機能がプロジェクトにあるときだけある。`"enabled": false` でもタグはある）:

| 機能 | 画面から |
| --- | --- |
| 音（`sound`） | 消音のスイッチ `{ "type": "switch", "bind": "sys.sound.mute" }`、音量 `{ "type": "slider", "min": 0, "max": 100, "bind": "sys.sound.volume" }`、音を鳴らす `{ "action": "call", "fn": "sound.play", "arg": "beep" }` |
| 音: ブザー停止 | `{ "type": "button", "text": "ブザー停止", "bind": { "visible": "sys.sound.alarm" }, "on_click": { "action": "set", "tag": "sys.sound.alarm", "value": false } }`（アラーム音が鳴っている間だけ出る。押すと次の新しいアラームまで止まる） |
| メール（`mail`） | 送る `{ "action": "call", "fn": "mail.send", "arg": "件名\n本文 {タグ名}" }`（文字そのまま。`"@ID"` は使えない）、テスト `"mail.test"`、`sys.mail.waiting`（送信待ちの数）、`sys.mail.error`（失敗中 1） |
| ログの送信（`uploads`） | 今すぐ送る `{ "action": "call", "fn": "upload.run", "arg": "<name>" }`、`sys.upload.waiting`、`sys.upload.error` |
| スケジュール（`schedules`） | 画面に出す部品はない（装置の System > Schedule で見る・変える）。`{ "action": "system" }` のボタンで System を開ける |
| データ転送（`transfers`） | 画面に出す部品はない。転送元と転送先のタグをふつうに表示すればよい（装置の System > Transfers に状態が出る） |

- `mail` / `uploads` が `"enabled": false` のとき、`mail.send` や `upload.run` は何も送らない（ログに警告が出るだけ）。
- `--check --render` の画像では、音・スケジュール・転送・メール・送信は動かない（転送先のタグは `init` の値のまま）。
  `sys.sound.alarm` は `--set` で 1 にできないので、「ブザー停止」ボタンは画像では確かめられない。位置と色は、ほかのボタンに合わせて決め、報告に書く。
- 「毎朝 8 時に〜する」「夜にログを送る」は、画面ではなく `schedules` に書く（`project-features.md`）。
