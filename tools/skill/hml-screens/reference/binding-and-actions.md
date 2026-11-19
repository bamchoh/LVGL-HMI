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
- `sys.` で始まるタグはランタイムが持つ（`sys.lang`、`sys.user.name`、`sys.user.level`、`sys.alarm.active`、`sys.alarm.unacked`、`sys.screen.off` など）。作らない。

## bind

短い形 `"bind": "tag"` は、部品に合ったプロパティにつながる（`widgets.md` の表）。入力する部品は自動で双方向。

```json
"bind": {
  "text":       { "tag": "temp", "format": "%.1f °C" },
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
| `map` | 値 → 色。`default` がその他。bool は `"true"` / `"false"` |
| `part` | `bg_color` を付ける場所: `main`（既定）/ `indicator` / `knob` |

- 短い形と一緒にほかの種類も付けたいときは、長い形で書く:
  `"bind": { "value": "speed_sp", "enabled": { "tag": "auto_mode", "invert": true } }`。
- `enabled` / `disabled` はどの部品にも付けられ、無効のあいだは薄く表示されて押せない。親の `panel` に付けても
  子は無効にならないので、操作する部品それぞれに付ける。
- 条件は 1 つのタグだけ（「A かつ B」は書けない）。要るときは、条件をまとめた bool のタグを作り、スクリプトで
  計算する。`project.json` の `scripts` に
  `{ "name": "can_run", "trigger": "tag", "tags": ["auto_mode", "run"], "code": "tag.can_run = not tag.auto_mode and not tag.run" }`。
- 文字列のタグの値を訳して出す仕組みはない。値ごとの `label` を重ねて `visible` と `eq` で出し分ける
  （`{ "type": "label", "text": "@running", "bind": { "visible": { "tag": "state", "eq": 2 } } }`）。
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
  `system_log.export` / `data_log.export` / `alarm_log.export` / `operation_log.export`（CSV に書き出す）。
  ほかの名前は書かない。
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
- 同じ意味の ID がもうあれば、それを使う。言い回しが少し違うだけなら既存の言葉に合わせる（既存の訳は変えない）。
- `languages` がないプロジェクトでは、文字をそのまま書く。

## 専用の部品

これらは中に自分の表示（見出し、ボタン、行）を持つので、小さすぎると中身が入らない。下の「大きさ」を守る。

| 部品 | 要るもの | 例 |
| --- | --- | --- |
| `alarm_banner` | `project.json` の `alarms` | `{ "type": "alarm_banner", "align": "top_mid", "w": "100%", "h": 36 }`（いちばん新しい未確認のアラームを帯で出す） |
| `alarm_list` | `alarms` | `{ "type": "alarm_list", "w": "100%", "h": 300, "mode": "current" }`（`history` で履歴） |
| `trend` | `data_log` のグループ | `{ "type": "trend", "w": "100%", "h": 300, "group": "process", "tags": ["temp", "level"], "range_s": 3600 }` |
| `chart` | なし（表示している間の値だけ） | `{ "type": "chart", "w": 300, "h": 160, "series": [ { "tag": "temp", "color": "#ff9800" } ], "min": 0, "max": 100 }` |
| `recipe_table` | `recipes` のブック | `{ "type": "recipe_table", "w": "100%", "h": 300, "book": "products" }` |
| `keyboard` | なし | `{ "type": "keyboard", "align": "bottom_mid", "w": "100%", "h": "45%" }`（入力欄がある画面に 1 つ。ふだんは隠れている） |

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

### `recipe_table`

- 中に、レシピの一覧と書き込み・保存などのボタンを持つ。高さ 300 以上、幅は画面いっぱいが基本。
- `labels`: `write` `read` `save_as` `delete` `item` `recipe` `current` `ok` `cancel` `name`。

`alarms`、`data_log`、`recipes` がプロジェクトにないのにこれらの部品を頼まれたら、その設定が要ることを伝え、
デザイナーの「ログ」「レシピ」タブで作ってもらう（頼まれれば `project.json` に足す。既存の設定の書き方に合わせる）。

## ユーザーのレベル

`project.json` に `users` があるとき、部品や画面に `"level": 2` を付けると、そのレベル以上のユーザーだけが操作できる。
`"level_deny"`: `disable`（既定、薄く表示）/ `hide` / `login`（押すとログイン画面）。`users` がなければ付けない。
