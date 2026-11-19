---
name: hml-screens
description: HML（LVGL HMI）のプロジェクトに画面を作る・直す。「こんな感じで画面を作って」「ボタンを足して」「この画面を見やすくして」のような依頼や、project.json・screens/*.json・.hmlproj を扱うときに使う。Create or change screens of an HML (LVGL HMI runtime / designer) project: screen JSON, tags, bindings, actions.
---

# HML の画面を作る

HML の画面は JSON ファイルです。デザイナー（`hml_designer`）で開くプロジェクトと同じファイルを直接書き、
デザイナーと同じフォルダーにあるランタイム（`hml_runtime`）で描いて確かめます。ビルド環境は要りません。

## 最初に確かめること

0. 依頼の文に「プロジェクトのフォルダー」「ランタイム」の場所が書いてあれば（デザイナーの **AI** ボタンが作る文）、
   それをそのまま使う。プロジェクトファイル（`.hmlproj`）の作業用フォルダーのこともあるが、扱いは同じ。
1. **プロジェクトの場所**。フォルダー形式（`project.json` と `screens/`、`assets/`）で作業する。
   `.hmlproj`（1 ファイル）しかないときは、デザイナーの「名前を付けて保存」で**フォルダー形式**にしてもらう。
2. **ランタイムの場所**。`hml_designer.exe` と同じフォルダーの `hml_runtime.exe`。わからなければユーザーに聞く。
3. `project.json` を読む: `display`（画面の大きさ）、`theme`、`tags`（使えるタグ）、`languages` / `texts` / `font`、`screens`。
4. 既存の画面を 1 つ読み、色・余白・文字の大きさ・`@ID` の使い方を**そのプロジェクトに合わせる**。

## 手順

1. 依頼から「何を見せ、何を操作させる画面か」を決める。足りない情報が画面の中身を左右するときだけ聞く
   （どのタグを出すか、など）。見た目の細部は聞かずに決めて、あとで画像を見せる。
2. 使うタグを決める。**ある物を使い、ない物は `project.json` の `tags` に足す**（足したことを報告する）。
   `drivers` には触らない（機器との接続はユーザーが決める）。
3. `screens/<name>.json` を書く。新しい画面は `project.json` の `"screens"` に足す。
   そこへ行くボタンを既存の画面に足すのは、頼まれたときか、行く手段がないときだけ（足したら報告する）。
   足す場所が込んでいたら、隣とそろえて入る所に置き、どの言語でも重ならないことを画像で確かめる。
   戻るボタンは `{ "action": "back" }`（前の画面に戻る）。行き先が決まっているなら `goto`。
4. **確かめる**（下の「確かめ方」）。`--check` の間違いをなくし、画像を見て、重なり・はみ出し・読みにくさを直す。
   画像の手直しは 2〜3 回まで。
5. 報告する: 作った / 変えたファイル、足したタグ、決めたこと、ユーザーに見てほしい点。
   最後に「デザイナーで見た目と問題一覧を確かめてください」と伝える（開いているデザイナーは、ファイルが変わると
   自分で読み直す。`.hmlproj` のプロジェクトは、デザイナーで「保存」を押すとファイルに入る）。

## 画面ファイルの形

```json
{
  "name": "overview",
  "style": { "bg_color": "#141a22" },
  "children": [
    { "type": "label", "x": 16, "y": 12, "text": "@overview", "style": { "font": 24 } },
    { "type": "panel", "x": 16, "y": 56, "w": 240, "h": 120, "style": { "radius": 8, "pad": 12 },
      "children": [
        { "type": "label", "text": "@temperature", "style": { "text_color": "#90a4ae" } },
        { "type": "label", "y": 28, "bind": { "text": { "tag": "temp", "format": "%.1f °C" } }, "style": { "font": 32 } }
      ] },
    { "type": "button", "align": "bottom_right", "x": -16, "y": -16, "w": 140, "h": 56, "text": "@setup",
      "on_click": { "action": "goto", "screen": "settings" } }
  ]
}
```

- `name` は英数字と `_`。`project.json` の `"screens"` にはファイルのパス（`"screens/overview.json"`）を書く。
- 子の `x` `y` は親の内側（余白 `pad` の内側）からの位置。`align` を付けると、その基準点からのずれになる。
- ウィジェットの種類・プロパティ・スタイル・動作の**一覧は `reference/widgets.md`**（デザイナーの定義から生成したもの）。
  **そこにないキーは書かない**（ランタイムは黙って無視する。`--check` が見つける）。
- `bind`（タグとの接続）、動作、ポップアップ、多言語、アラーム・トレンドなどの部品の使い方は
  **`reference/binding-and-actions.md`**。
- そのまま動く見本は `examples/`（`project.json`、`screens/overview.json`、ポップアップの `screens/confirm_stop.json`）。

## 作るときの決まり

**既存のプロジェクトと下の決まりが食い違うとき**: 見た目（色、余白、文字の大きさ、単位の書き方、言葉づかい）は
プロジェクトに合わせる。新しく作る画面の押すものの大きさは、下の最小値を守る。既存の画面に足す部品は、
隣の部品と大きさをそろえる（それが最小値より小さいときは、そのことを報告に書く）。

**レイアウト**
- 画面の大きさは `display.width` × `display.height`。端から 16px は空ける。すべてこの中に収める（スクロールに頼らない）。
- 位置と大きさは 8px 刻み。同じ種類の物は幅・高さ・間隔をそろえる。
- 並べるものは `panel` に入れ、`"flex": "row"` / `"column"` と `pad_gap` で並べる（座標を 1 つずつ計算しない）。
  決まった位置に置くもの（画面の題名、隅のボタン）は `x` `y` か `align`。
- 上に題名と状態、中央に主な情報、下か右に操作、という並びを基本にする。既存の画面に並びがあれば合わせる。

**タッチ**
- 押すものは高さ 48px 以上、幅 80px 以上、間隔 8px 以上。手袋で使う現場なら 56px 以上。
- 危ない操作（停止、リセット、削除）は、ほかのボタンから離し、色を変える。必要なら `level` で操作者を限る。
  既存の画面が同じ操作に `level` を付けていれば、新しい画面にも同じ `level` を付ける。
- 「〜のときは操作できない」は、部品ごとに `bind` の `enabled`（下の資料）。止める操作まで無効にするかは安全に関わるので、
  依頼がはっきりしなければ依頼の言葉どおりに作り、報告で確かめてもらう。

**文字と色**
- `font` は `reference/widgets.md` にある大きさだけ。本文 16、見出し 24、主な数値 32〜48。14 未満は使わない。
- 値には単位を付ける（`"format": "%.1f °C"`、`numeric_display` の `unit`）。`°` などの記号は、プロジェクトのフォントに
  入っていないことがある。既存の画面の書き方に合わせ、新しい記号は画像で □ になっていないか確かめる。
- 文字の高さはおよそ `font` × 1.2。枠の中の余白は `style` の `pad` で決める（省くとテーマの値になる）。
  枠にぎりぎり入る寸法にしない。
- 赤は異常・停止、黄 / 橙は警告、緑は正常・運転だけに使う。飾りには使わない。色だけで伝えず、文字も変える。
- `theme.dark` が true なら暗い背景に明るい文字。背景と文字の明暗差を十分に取る。
- **日本語などは `project.json` の `font.file` がないと表示できない**（□ になる）。ないときは英語で書くか、
  ユーザーにフォントの設定（デザイナーの プロジェクト > フォント）を頼む。
- `languages` があるプロジェクトでは、文字を `texts` に足して `"@ID"` で書く（すべての言語の訳を入れる）。

**データ**
- 表示は `label` + `bind`（大きな数値、文章の中の値）か `numeric_display`（枠付きの値、入力もできる値）。入力は `numeric_display` / `text_display` に `"input": true`
  （画面に `keyboard` を 1 つ置く）。入力には `min` / `max` を付ける。
- 状態は `led` や `bind` の `bg_color` + `map`。値によって出し分けるものは `visible` と条件（`eq` `gt` …）。
- アラーム、トレンド、レシピは専用の部品（`alarm_list` / `alarm_banner`、`trend`、`recipe_table`）を使う。
  それぞれ `project.json` 側の設定（`alarms`、`data_log` のグループ、`recipes`）が要る。

## 確かめ方

ランタイムの `--check` を使います。**ウィンドウを出さず、機器につながず、プロジェクトに何も書かずに**、間違いの一覧を出し、
画面を画像にします。プロジェクトをコピーする必要はありません。

```powershell
& "<デザイナーのフォルダー>\hml_runtime.exe" "<プロジェクト>\project.json" --check --render overview,"<画像を置く場所>\overview.png"
```

出力の例:

```
screens/overview.json: children[2] (button): on_click (goto): unknown screen "setting"
screens/overview.json: children[4] (label): unknown key "colour" (ignored by the runtime)
rendered: overview -> C:\...\overview.png
2 problem(s)
```

1. **間違いの行をすべて直す**（1 行 = `ファイル: 場所: 内容`。`children[2]` は上から 3 つ目の部品）。`no problems` になるまで繰り返す。
   自分が触っていない所の間違いは、直さずに報告する（画像で見つけた、既存の画面の崩れも同じ）。`runtime:` で始まる行は、読み込んで画面を作るときにランタイムが出した警告。
2. **画像を見る**: はみ出し・重なりがないか、文字が切れていないか・□ になっていないか、そろっているか、
   押すものが十分大きいか、依頼の「こんな感じ」に合っているか。直したら、もう一度実行する。
3. **ほかの状態も描く**（オプションは組み合わせられる）:
   - `--render <画面>,<ファイル.png>` は何度でも書ける（変えた画面すべて）。
   - ポップアップは `--render <開く元の画面>+<ポップアップ>,<ファイル.png>` で、その画面の上に開いた状態を描く
     （ポップアップの名前だけを書くと、プロジェクトの `start_screen` の上に開く）。
   - `--lang en`: その言語で描く。**言語が複数あるプロジェクトは、すべての言語で描く**（訳によって長さが違い、片方だけはみ出すことがある）。
   - `--set temp=34.5 --set run=true`: タグの値を変えて描く。値によって見た目が変わる画面で使う。
     アラームは、`alarms` の条件が成り立つ値にすると出る（例: `temp` が `temp_hi` より大きい）。
4. 最後の画像は、ユーザーに見せられる場所に残す。

知っておくこと:
- 機器につながないので、値は `tags` の `init`（と `--set`）から始まる。`trend` は枠と目盛りだけで、線は出ない。
- プロジェクトのスクリプト（`scripts`）は動く。周期で動くスクリプトがタグを書き換えていると、`--set` した値と
  違う値が画像に出ることがある（`project.json` の `scripts` を読むとわかる）。
- 使い方は `hml_runtime --help`。`--screen` `--screenshot` `--click` `--shot-at` は `--check` と一緒には使えない。
- 画面を描くときは、いちばん高いユーザーレベルでログインした状態になる（`level` の付いた画面や部品も見える）。
- 文字が □ になっていても間違いの行には出ない。画像で確かめる。
- 終了コードは、間違いがなければ 0、あれば 1。

### 押したあとの様子を確かめたいとき

ボタンを押した結果（画面の切り替え、値の変化）まで確かめたいときだけ、ランタイムをふつうに動かします。
**実物のプロジェクトで動かすと機器につながり、ログも記録されるので、必ずコピーで行います。**

```powershell
$src = "<プロジェクトのフォルダー>"; $rt = "<デザイナーのフォルダー>\hml_runtime.exe"; $tmp = Join-Path $env:TEMP "hml-preview"
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
robocopy $src $tmp /E /XD logs pki /XF project.local.json | Out-Null
$p = Get-Content "$tmp\project.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$p | Add-Member -NotePropertyName drivers -NotePropertyValue @($p.drivers | Where-Object { $_.type -eq 'sim' }) -Force
[IO.File]::WriteAllText("$tmp\project.json", ($p | ConvertTo-Json -Depth 50), (New-Object Text.UTF8Encoding $false))
& $rt "$tmp\project.json" --screen overview --click 96,436,600 --shot-at 1100,"$tmp\after.png" --exit-after 1300
```

- コピーの `drivers` は、`"type": "sim"`（シミュレーター）だけを残す。
- `--click <x>,<y>,<ms>`: 起動から ms 後にその座標を押す。`--shot-at <ms>,<ファイル.png>`: その時刻に保存する。どちらも何度でも書ける。
- ここでも `--set <タグ>=<値>` と `--lang <言語>` が使える（起動時の値と言語）。
- 短い間ウィンドウが出る。ログの `showing '...'` は最初の画面の名前で、`--screen` が効いていないわけではない。
- 終わったら一時フォルダーを消す。**直すのは実物のファイル**。

### `--check` が使えないとき

古いランタイム（`unknown option --check` と出る）では、上のコピーの手順で `--screenshot` を使って描き、出力の
`[hml WARN ]` / `[hml ERROR]` を読む。その場合、つづりを間違えたキーや行き先のない画面遷移は警告に出ないので、
`reference/widgets.md` と `project.json` に自分で照らし、デザイナーの問題一覧も見てもらう。
コマンドを実行できない環境では、確かめられなかったことを報告に書く。

## 検査の限界

`--check` が見つけるのは、書き方の間違い（知らない部品・キー・スタイル・動作、ないタグ・画面・スクリプト・画像、
`texts` にない `"@ID"`、ログのグループやレシピのブックの指定）です。次は見つけません。画像と自分の目で確かめます。

- 見た目の問題（重なり、はみ出し、小さすぎる、読みにくい）
- 意味の間違い（別のタグを出している、単位が違う、押すと逆の動作をする）
- 訳の抜け・訳が長すぎること（デザイナーの問題一覧には出る）

## してはいけないこと

- `drivers`、`users`、`project.local.json`、`pki/`、`logs/` を、頼まれずに変えない。
- 既存の画面やタグを、頼まれずに消したり名前を変えたりしない（ほかの画面・スクリプト・ログ設定が使っている）。
- 一覧にない部品やキーを「ありそうだから」で書かない。できないことは、できないと伝えて代わりの案を出す。
