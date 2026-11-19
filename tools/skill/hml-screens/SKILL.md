---
name: hml-screens
description: HML（LVGL HMI）のプロジェクトに画面を作る・直す。「こんな感じで画面を作って」「ボタンを足して」「この画面を見やすくして」のような依頼や、project.json・screens/*.json・.hmlproj を扱うときに使う。Create or change screens of an HML (LVGL HMI runtime / designer) project: screen JSON, tags, bindings, actions.
---

# HML の画面を作る

HML の画面は JSON ファイルです。デザイナー（`hml_designer`）で開くプロジェクトと同じファイルを直接書き、
デザイナーと同じフォルダーにあるランタイム（`hml_runtime`）で描いて確かめます。ビルド環境は要りません。

## 最初に確かめること

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
4. **確かめる**（下の「確かめ方」）。警告を直し、画像を見て、重なり・はみ出し・読みにくさを直す。2〜3 回まで。
5. 報告する: 作った / 変えたファイル、足したタグ、決めたこと、ユーザーに見てほしい点。
   最後に「デザイナーで開き直して、問題一覧を見てください」と伝える（下の「検査の限界」）。

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
  **そこにないキーは書かない**（黙って無視される）。
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

実物のプロジェクトでランタイムを動かすと、機器につながり、ログも記録されます。**必ずコピーで確かめます。**

1. プロジェクトのフォルダーを一時フォルダーにコピーする（`logs/`、`pki/`、`project.local.json` は除く）。
2. コピーの `project.json` の `"drivers"` から、`"type": "sim"`（シミュレーター）以外を消す（機器に接続させない）。
   `sim` を残すと値が動く。なければ値は `tags` の `init` のまま。
3. ランタイムで描く。

Windows / PowerShell では、1〜3 をこれで行える（描き直すたびに実行する。コピーは毎回作り直される）:

```powershell
$src = "<プロジェクトのフォルダー>"; $rt = "<デザイナーのフォルダー>\hml_runtime.exe"; $tmp = Join-Path $env:TEMP "hml-preview"
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
robocopy $src $tmp /E /XD logs pki /XF project.local.json | Out-Null
$p = Get-Content "$tmp\project.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$p | Add-Member -NotePropertyName drivers -NotePropertyValue @($p.drivers | Where-Object { $_.type -eq 'sim' }) -Force
[IO.File]::WriteAllText("$tmp\project.json", ($p | ConvertTo-Json -Depth 50), (New-Object Text.UTF8Encoding $false))
& $rt "$tmp\project.json" --screen overview --screenshot "$tmp\shot.bmp" --exit-after 800
```

（`project.json` にコメントがあって `ConvertFrom-Json` が失敗するときは、コピーの `drivers` を手で書き換える。
ログの `showing '...'` は最初の画面の名前で、`--screen` が効いていないわけではない。短い間ウィンドウが出る。）

4. 出力の `[hml WARN ]` / `[hml ERROR]` の行を読み、自分の変更によるものをすべて直す
   （`unknown widget type`、`unknown tag`、JSON の文法エラーなど）。
5. 画像を PNG にして見る（BMP のままでは見られないことがある）:

```powershell
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Image]::FromFile("<コピー>\shot.bmp"); $b.Save("<コピー>\shot.png", [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
```

6. 画像で見るもの: はみ出し・重なりがないか、文字が切れていないか・□ になっていないか、そろっているか、
   押すものが十分大きいか、依頼の「こんな感じ」に合っているか。直したら 3 からやり直す。
7. ほかの状態も描いてみる:
   - 値によって見た目が変わる画面は、コピーの `tags` の `init` を変えて描く。アラームは、`alarms` の条件が成り立つ
     値にすると出る（例: `temp` が `temp_hi` より大きい）。
   - **言語が複数あるプロジェクトは、すべての言語で描く**（コピーの `"language"` を変える）。訳によって長さが違い、
     片方だけはみ出すことがある。
   - `trend` はログがたまるまで線が出ない。`--exit-after` を 10000 くらいにすると少し出る。枠と目盛りの確認だけなら短くてよい。
   - ポップアップは `--screen <ポップアップの名前>` で、最初の画面の上に開いた状態を描ける。
   - 押したあとの様子は `--click <x>,<y>,<ms>`（起動から ms 後にその座標を押す）と
     `--shot-at <ms>,<file.bmp>`（その時刻に保存。何度でも書ける）で描ける。
8. **直すのは実物のファイル**（コピーだけ直して終わらない）。最後の画像は、ユーザーに見せられる場所に残してから
   一時フォルダーを消す。

コマンドを実行できない環境では、確かめられなかったことを報告に書く。

## 検査の限界

ランタイムの警告に出るのは、知らない部品の種類と知らないタグ、文法エラーくらいです。次は**出ません**。
書く前と書いたあとに自分で確かめます。

- 知らないプロパティやスタイルのキー（つづりの間違い）: 黙って無視される → `reference/widgets.md` と照らす
- `goto` / `popup` の行き先の画面がない → `project.json` の `"screens"` と各ファイルの `name` を照らす
- `texts` にない `"@ID"`、訳の抜け → `texts` と照らす
- 画像ファイル（`src`）がない → ファイルがあるか確かめる
- 描いていない状態（条件で隠れる部品、押したあとの動き）の中の間違い

デザイナーはこれらを「問題」一覧に出します。だから最後に、デザイナーで開き直して見てもらいます。

## してはいけないこと

- `drivers`、`users`、`project.local.json`、`pki/`、`logs/` を、頼まれずに変えない。
- 既存の画面やタグを、頼まれずに消したり名前を変えたりしない（ほかの画面・スクリプト・ログ設定が使っている）。
- 一覧にない部品やキーを「ありそうだから」で書かない。できないことは、できないと伝えて代わりの案を出す。
