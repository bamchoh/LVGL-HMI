# bind, actions, popups, multiple languages, special widgets

The list of keys is in `widgets.md`. This file is how to use them.

## Tags (`tags` in `project.json`)

```json
{ "name": "temp",  "type": "float",  "init": 20.0 },
{ "name": "speed", "type": "int",    "init": 0, "min": 0, "max": 100 },
{ "name": "run",   "type": "bool",   "init": false },
{ "name": "state", "type": "string", "init": "Idle" }
```

- With `min` / `max`, writes from the screen are clamped to that range. Add them to numeric tags that can be operated.
- Tags that start with `sys.` belong to the runtime (`sys.date`, `sys.time`, `sys.datetime`, `sys.screen` (name of the screen being shown), `sys.lang` (the language shown now: a code of `languages`, such as `en`), `sys.languages` (list of languages `ja,en`), `sys.user.name`, `sys.user.level`, `sys.user.level_name`, `sys.user.logged_in`, `sys.alarm.active`, `sys.alarm.unacked`, `sys.alarm.latest` (message of the newest unacknowledged alarm. If there is none, of an active one), `sys.alarm.latest_severity`, `sys.screen.off` and so on). Do not create them.
  Do not write a `sys.` name that is not here or in `project-features.md` (`--check` reports `sys.` tags that the project does not have.
  `sys.sound.*` and the like exist only when that feature is in `project.json`).

## bind

The short form `"bind": "tag"` connects to the property that fits the widget (table in `widgets.md`). Input widgets are two-way automatically.

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
  "text_color": { "tag": "plc_online", "map": { "true": "#4caf50", "false": "#78909c" } },
  "border_color": { "tag": "state", "map": { "1": "#36c98f", "2": "#f06464", "default": "#3b4a5e" } },
  "image_recolor": { "tag": "state", "map": { "1": "#36c98f", "2": "#f06464", "default": "#91a0b4" } }
}
```

| Key | Meaning |
| --- | --- |
| `format` | printf format, one conversion specifier. `%d` `%x` integer, `%f` `%g` real, `%s` string. A literal `%` is `%%` (`"%d %%"`) |
| `scale` / `offset` | Widget value = tag value × scale + offset (writes are computed in reverse) |
| `invert` | Inverts true / false |
| `eq` `ne` `gt` `lt` `ge` `le` | Condition. Used with `visible` `hidden` `enabled` `disabled` `checked` `on`. The value compared against is a constant or `{ "tag": "temp_hi" }` |
| `map` | Value → colour (`bg_color` `text_color` `border_color` `image_recolor`), value → text (`text`. `"@ID"` can be used). `default` is everything else. bool is `"true"` / `"false"`. For a string tag, the key with that text matches (`{ "main": "#26c6da" }`) |
| `part` | Where `bg_color` is applied: `main` (default) / `indicator` / `knob` |

- To add other kinds together with the short form, write the long form:
  `"bind": { "value": "speed_sp", "enabled": { "tag": "auto_mode", "invert": true } }`.
- `enabled` / `disabled` can be put on any widget; while disabled it is shown dimmed and cannot be pressed. Putting it on the parent `panel`
  does not disable the children, so put it on each widget that is operated.
- A condition uses one tag only ("A and B" cannot be written). When needed, make a bool tag that combines the conditions and compute it
  in a script. In `scripts` of `project.json`:
  `{ "name": "can_run", "trigger": "tag", "tags": ["auto_mode", "run"], "code": "tag.can_run = not tag.auto_mode and not tag.run" }`.
- To show a tag holding a state number or a string as words, use the `map` of `text` (example above. No need to stack a `label` per value).
  A value with no matching key and no `default` is shown as it is (with `format`). To change the colour along with the words, write a `map` of `text_color` or `bg_color` for the same tag beside it.
  Write the words for all values, and `--set` the longest word to check that it fits.
- A button with `"checkable": true` and `checked` in `bind` turns the tag ON / OFF just by being pressed (no `toggle` action is needed.
  A `toggle` on the same tag immediately puts back the value the button wrote and nothing changes: `--check` reports it).
- A button (`"checkable": true`) with `eq` on `checked` writes that value to the tag when pressed. Lining up several with the same tag and different values
  makes a choice of one (radio buttons).
- Stacking `panel`s with different conditions at the same place and switching them with `visible` gives a display per state.

## Where styles apply

`style` is the body of the widget. The others apply to a part or a state of the widget.

| Key | Applies to |
| --- | --- |
| `style_label` | The text inside a `button` / `checkbox` (the text size of a button is `"style_label": { "font": 20 }`) |
| `style_indicator` | The value part of a `bar` / `slider` / `arc`, the mark of a `switch` / `checkbox` |
| `style_indicator_checked` | That part when ON |
| `style_knob` | The knob of a `slider` / `arc` / `switch` |
| `style_items` | The keys of a `keyboard`, the items of a `dropdown` / `roller` |
| `style_selected` | The selected item of a `roller` / `dropdown` |
| `style_pressed` `style_checked` `style_disabled` `style_focused` | The body when pressed / ON / disabled / receiving input |
| `style_scrollbar` | The scrollbar |

When `w` `h` are omitted, the size is decided by the content and the theme. Write `w` `h` for things that are lined up and aligned, and for things that are pressed. The inner padding of a `panel` is also
decided by the theme, so when you want to position by calculation, set it yourself, like `"style": { "pad": 12 }`.

## Pictures

A picture is a file inside the project folder (usually `assets/`), written as a path relative to `project.json`.
**The AI does not make pictures.** Use what is in `assets/` (open it as a picture to check its content and size); if nothing fits,
tell the user which picture is needed (what it shows, width × height, whether the background is transparent) and have the user provide it. Until then, reserve the place with a `panel` and a `label`.

**Preset icons**: white pictograms that come with the designer (`<designer folder>\icons\`). If the project has no `assets/icons/`,
copy the whole folder to `assets\icons\` (the same as the Project > Icons button of the designer).

- File: `assets/icons/<size>/<name>.png`. The size is `24` `32` `48` `64` (do not enlarge. Choose the file of the size to be shown).
- The list of names is `assets/icons/icons.txt`, all the pictures are in `assets/icons/preview.png` (open it, look and choose).
  Equipment: `motor` `pump` `fan` `heater` `flame` `valve` `tank` `conveyor` `mixer` `oven` `cooling` `slicer` `package`
  `thermometer` `humidity` `gauge` `factory`. Menu and operation: `home` `trend` `report` `alarm` `warning` `info` `check` `error`
  `play` `stop` `pause` `reset` `power` `settings` `user` `lock` `recipe` `io` `back` `forward` `menu` `list` `clock` `process`.
- The colour is `"style": { "image_recolor": "#36c98f" }` (the picture is white, so it takes that colour as it is). To change the colour by state, use `image_recolor` + `map` in `bind`.
- To put one on a button, place an `image` inside the `button` (`"align": "center"`, or `flex` to line it up with text). The pressable size is taken by the `button`.

```json
{ "type": "image", "x": 16, "y": 16, "src": "assets/logo.png" },
{ "type": "image", "w": 200, "h": 120, "src": "assets/pump.png", "fit": "contain" },
{ "type": "image", "src": "assets/fan.png",
  "bind": { "src": { "tag": "state", "map": { "0": "assets/fan_off.png", "1": "assets/fan_on.png", "default": "assets/fan_err.png" } } } },
{ "type": "image", "src": "assets/home.png", "on_click": { "action": "goto", "screen": "main" } },
{ "type": "panel", "w": "100%", "h": "100%", "style": { "bg_image": "assets/plant.png" }, "children": [ ... ] }
```

| Format | What it can do |
| --- | --- |
| PNG | Everything. Has transparency. `fit`, `zoom` and `rotation` can be used |
| JPEG / BMP | **Shown at the original size only** (`fit`, `zoom` and `rotation` have no effect). For photos, have a file of the size to be shown provided |
| GIF | Played as an animation (original size. Cannot be switched with `bind`) |

- `fit` (PNG): in the box of `w` `h`, `contain` (the whole picture fits, keeping the ratio) / `stretch` (stretched) / `tile` (tiled) / `center`.
  When omitted, it is the original size, and `w` `h` become the size of the picture.
- **An icon that changes colour by state**: `"bind": { "image_recolor": { "tag": "state", "map": { ... } } }` on the `image` (a white icon takes that colour).
  There is no need to stack `image`s of different colours. For the border of a card, use `border_color` + `map` in `bind` (set the border thickness with `border_width` in `style`).
- Changing the picture by value: `map` on `src` in `bind` (value → file. bool is `"true"` / `"false"`, everything else is `"default"`).
  For a value with no matching file, nothing is shown. Putting a file name in a string tag and using `"bind": { "src": "<tag>" }` is also fine.
- As a background: `bg_image` in `style` (screen, `panel`, `button` and so on. In the centre at the original size. `"bg_image_tiled": true` tiles it,
  `bg_image_opa` makes it fainter). Written in `style_pressed` and the like, it shows only in that state. Check in the picture that the text of the widgets placed on top can be read.
- Stacking pictures: within the same parent, a widget written later is rendered on top. Giving 2 `image`s the same `align` (`center` and so on) and the same offset
  makes their centres coincide (blades on top of a pump picture, and so on). `rotation` and `zoom` use the centre of the picture as the axis. `zoom` and the rotation of `animate` can be used together.
- Enlarging makes the edges jagged (neither `fit` nor `zoom` interpolates). For a picture to be shown large, have a file of the size to be shown provided.
  Shrinking comes out clean.
- How to find the size (PowerShell): `Add-Type -AssemblyName System.Drawing; $i = [System.Drawing.Image]::FromFile("<file>"); "$($i.Width)x$($i.Height)"; $i.Dispose()`.
  An icon that is a white picture on a transparent background looks all white when opened as a picture. Place it on a dark `panel` and check with `--render`.
- A pressable picture: `on_click` on the `image`. For a small picture, put a `panel` around it and make the pressable area 48px or more.
- A single-colour icon (white PNG) can be coloured with `"style": { "image_recolor": "#4caf50" }` (to change it by value, `image_recolor` in `bind`).
- One picture may be up to about 4 million pixels in width × height. Do not use a picture larger than the screen (memory needed is width × height × 4 bytes).
- What `--check` looks at: whether the file exists, whether it is inside the project, the format, whether `fit` and the like are put on a JPEG / BMP.
  Broken files and pictures that are too large appear on `runtime:` lines.

## Animation

`animate` can be written on any widget (one, or an array). It moves **only while that screen is shown and `when` holds**;
when it stops, the widget returns to its original look. When `when` is omitted, it moves all the time the screen is shown.

```json
{ "type": "label", "text": "@alarm", "animate": { "preset": "blink", "when": "alarm_active" } },
{ "type": "image", "src": "assets/fan.png", "animate": { "preset": "spin", "time": 1500, "when": "run" } },
{ "type": "panel", "w": 24, "h": 24, "animate": { "prop": "x", "from": 100, "to": 400, "time": 2000, "path": "linear", "when": { "tag": "flow", "gt": 0 } } },
{ "type": "panel", "style": { "bg_color": "#37474f" }, "animate": { "prop": "bg_color", "to": "#c62828", "time": 600, "playback": true, "when": { "tag": "state", "eq": 2 } } },
{ "type": "image", "src": "assets/conv1.png", "animate": { "prop": "frames", "files": ["assets/conv1.png", "assets/conv2.png", "assets/conv3.png"], "time": 600, "when": "run" } }
```

| Key | Meaning |
| --- | --- |
| `preset` | `blink` (on and off, 1 second period) / `pulse` (slow brightening and dimming) / `spin` (one turn in 2 seconds). Can be overridden with the keys below |
| `prop` | What is animated: `opa` (opacity 0–100) / `x` `y` `w` `h` (px) / `rotation` (degrees) / `zoom` (1 = original size) / `bg_color` `text_color` / `frames` (changes the picture of an `image` in turn) |
| `from` / `to` | The values at both ends. When `from` is omitted, from the current value |
| `time` | Time one way (milliseconds, default 1000). For `frames`, the time of one cycle |
| `playback` | true makes it go back and forth |
| `repeat` | Number of times (when omitted, forever) |
| `path` | How it moves: `linear` (default) / `ease_in` / `ease_out` / `ease_in_out` / `step` (switches at the end of the time) |
| `delay` | Time until it starts (milliseconds) |
| `when` | A tag name (when true) or a condition (`{ "tag": "state", "eq": 2 }`. Written the same way as a condition of `bind`) |

- `time` of a `preset`: `blink` is 500 by default (on 500 + off 500 = 1 second period), `pulse` is 700, `spin` is 2000 (one turn).
  To make it slower, make `time` larger. The default `blink` is slow enough and may be used as it is.
- The condition of `when` is the same as in `bind` and can also compare with another tag: `"when": { "tag": "temp", "gt": { "tag": "temp_hi" } }`.
- When it stops, it returns to the original look (to the original angle even in the middle of a rotation, to the visible state even in the middle of a blink). For something that
  **"appears only under a condition, and blinks during that time"**, write the same condition on both `visible` and `animate`:
  `{ "type": "image", "src": "assets/warning.png", "bind": { "visible": { "tag": "temp", "gt": { "tag": "temp_hi" } } }, "animate": { "preset": "blink", "when": { "tag": "temp", "gt": { "tag": "temp_hi" } } } }`

Where to use it, and rules:
- **Use motion only for "what should be noticed now"** (an unacknowledged alarm, running, flowing). Do not animate for decoration.
  Keep the things moving at the same time on 1 screen few (guide: up to 3). Avoid things that always move (no `when`).
- Blinking (`opa` and colours) cannot be faster than 3 times per second (the runtime slows it down). For a warning, use `pulse` or a slow `blink`.
- It must be readable while moving: do not blink a value or an operated button itself. Animate a frame, a band or an icon.
- `rotation` and `zoom` are light when used on an `image` (PNG). On other widgets rendering becomes heavy, so use them on small widgets only.
  A JPEG / BMP `image` cannot be rotated or enlarged.
- A widget whose `x` `y` are animated gets no `align`; write the coordinates in `from` / `to`. Do not animate a widget inside a flex layout.
- Make the pictures of `frames` the same size.
- How to check: adding `--at <milliseconds>` to `--render` gives the picture at about that time after the screen appeared.
  Render 2 pictures at different times, and see that it moves and that it is stopped when `when` does not hold (`--set`).
  `--at` is one per run (it applies to every `--render`), so run once per time.
  **When a blink is hit at the moment it is "off", the widget looks as if it were not there**. For a `blink` with a 1 second period, render 2 pictures
  half a period apart, like `--at 450` and `--at 950`; it is fine if it shows in one of them. For a rotation, it is fine if the angle differs between `--at 200` and `--at 700`.

## Actions

On an event (`on_click` and so on), write one action or an array (run in order).

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

- Write the `value` of `set` as a JSON value that matches the type of the tag (`true` / `false`, a number, a string).
- ON only while pressed: `set` true in `on_press`, `set` false in `on_release`.
- Events can be written on any widget (a `panel` or a `label` also becomes pressable when an event is written on it).
- `fn` of `call` (actions registered in the runtime): `alarm.ack_all` (acknowledges all alarms), `data_log.sample` (group name
  in `arg`. Records the current values), recipe operations such as `recipe.write` (usually done by `recipe_table`),
  `system_log.export` / `data_log.export` / `alarm_log.export` / `operation_log.export` (export to CSV),
  `alarm.export_summary` (the alarm summary to CSV. `arg` is the number of days),
  `sound.play` (`arg` is the name of the sound) / `sound.stop` / `sound.silence`, `mail.send` (`arg` is `"Subject\nBody"`) / `mail.test`,
  `upload.run` (`arg` is a name in `uploads`. When omitted, all). The last 3 groups work only when the project has
  that feature (`sound` / `mail` / `uploads`) (see "Using features outside the screens from a screen" below). Do not write other names.
- `system` opens the built-in system settings screen (even without a button, on the device, pressing the top-left corner of the screen for 3 seconds opens it). `reload` `exit` `restart` are for maintenance. Do not put them on ordinary screens.
- `script` is Lua. Write it briefly in `code`, or call one in `scripts` of `project.json` with `name`. Tags are `tag.<name>`.

## Popups

Writing `"popup": true` in a screen file makes it open on top of the current screen.

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

- It is opened with `{ "action": "popup", "screen": "confirm_stop" }`. While it is open, the screen below cannot be touched.
- To check it, use `--render <screen it is opened from>+confirm_stop,<file.png>`.
- The inner width is `w` minus 2 times `pad` and 2 times the border (`border_width`. 2 px in the theme). Making a child exactly that wide makes a horizontal scrollbar
  appear. Make the width of a child `"100%"`, or leave some room.
- The size and position are `w` `h` `x` `y` `align` (default is 60% × 60%, centre).
- Use it for operations that need confirmation (stop, delete) and for the detail display of one unit. With `"set"`, "which machine" can be passed in a tag.
- Add popups to `"screens"` in `project.json` too.

## Multiple languages

When `project.json` has `languages`, put the text in `texts` and use it with `"@ID"`.

```json
"languages": ["ja", "en"],
"texts": { "temperature": { "ja": "温度", "en": "Temperature" } }
```

- `"@ID"` can be used in: the `text` / `placeholder` / each item of `options` of a widget, the `message` of an alarm, the `labels` of a widget.
- It cannot be used inside the `format` of `bind`. Keep that to units and symbols only, and put the descriptive text in a separate `label`.
- An ID that is added must not clash with the existing ones, and is made of meaningful lower-case English letters and `_`. Enter the translation for every language.
- Words for an operation ("Run", "Stop": buttons) and words for a state ("Running", "Stopped": display) have different meanings, so make them different IDs.
- If an ID with the same meaning already exists, use it. If only the wording differs a little, match the existing words (do not change existing translations).
- In a project without `languages`, write the text as it is.

- **Adding languages to a project that has none**: `"languages": ["de", "en"]` - any short codes; **the first is the default** and what a
  missing translation falls back to. `"language"` picks another one for the start. Screens that still have their words written
  directly keep showing them as they are, in every language: say which screens you converted and which are left.
  Alarm messages are translated only when they are `"@ID"` - changing the existing ones is part of "this screen in two languages" only
  when the screen shows them (an alarm table); report it.
- A button that switches the language: `{ "action": "language", "lang": "next" }`. For its text use a text id whose value in each
  language is the name of the *other* language (`"lang_other": { "de": "English", "en": "Deutsch" }`), or the fixed word "Language".
- The built-in screens (system settings, login, the keys "Close" / "OK" of the keyboard) are in English, German (`de`), Japanese (`ja`) or Simplified Chinese (`zh`): they follow the language of the project when it is one of these and the font has its letters, and are English otherwise.

## Special widgets

These have their own display inside (headings, buttons, rows), so when too small the content does not fit. Keep to the "Size" given below.
The `style` of the widget (`bg_color`, `border_width`, `radius`, `pad`, `font`, `text_color`) applies to the frame and the text. The colours of the rows and buttons inside (the severity
colours of `alarm_list`, the history rows, the write button of `recipe_table`) cannot be changed.

| Widget | What it needs | Example |
| --- | --- | --- |
| `alarm_banner` | `alarms` in `project.json` | `{ "type": "alarm_banner", "align": "top_mid", "w": "100%", "h": 36 }` (shows the newest unacknowledged alarm as a band) |
| `alarm_summary` | `alarms` | `{ "type": "alarm_summary", "w": "100%", "h": 300, "days": 7 }` (count and time per alarm) |
| `alarm_list` | `alarms` | `{ "type": "alarm_list", "w": "100%", "h": 300, "mode": "current" }` (`history` for the history) |
| `log_view` | The settings of the log to show (`system_log` / `operation_log` / `data_log`) | `{ "type": "log_view", "w": "100%", "h": 300, "source": "system" }` (`operation`, `data` + `group`) |
| `trend` | A group of `data_log` | `{ "type": "trend", "w": "100%", "h": 300, "group": "process", "tags": ["temp", "level"], "range_s": 3600 }` |
| `bar_chart` | A group of `data_log` | `{ "type": "bar_chart", "w": 600, "h": 260, "group": "production", "tag": "production_count", "bars": 12 }` (production count per hour and the like) |
| `chart` | Nothing (only the values while it is shown) | `{ "type": "chart", "w": 300, "h": 160, "series": [ { "tag": "temp", "color": "#ff9800" } ], "min": 0, "max": 100 }` |
| `recipe_table` | A book of `recipes` | `{ "type": "recipe_table", "w": "100%", "h": 300, "book": "products" }` |
| `keyboard` | Nothing | `{ "type": "keyboard", "align": "bottom_mid", "w": "100%", "h": "45%" }` (one on a screen that has input fields. Normally hidden) |

### `alarm_summary`

- A widget that shows, for a chosen period, the number of occurrences and the total active time per alarm (for maintenance: "what happens often", "what caused long stops"). Needs `alarms`.
- Inside it has a top band (dropdowns for the period and the sort order, the total. About 46px high) and rows. One row is 36px, the heading 28px.
  **As a guide the height is "90 + 39 × number of rows"**, and `rows` is set to match (on a 480x320 screen, `rows` 5–6).
  **A width of 360 or more** is safe (at 320, the English sort-order text and long alarm names are cut off). When placing it in a narrow place, set `"bars": false` (the count bars are not shown).
  A long alarm name does not fit on one row and is cut off. Use a width that leaves enough for the name column.
- `days` is the initial period (1 / 7 / 30 / 0 = all), `sort` is `count` (count) or `time` (time). The operator can change both on the widget.
- The text inside the widget is English (`Alarm` `Count` `Time` `24 h` …). To make it Japanese, write it in `labels`:
  `"labels": { "alarm": "アラーム", "count": "回数", "time": "時間", "day": "24 時間", "week": "7 日", "month": "30 日", "all": "すべて", "by_count": "回数順", "by_time": "時間順", "empty": "アラームなし" }`
- To add a button that exports to CSV, use `{ "action": "call", "fn": "alarm.export_summary", "arg": "7" }` (the argument is the number of days).
- In the picture of `--check --render`, using `--set` to give values that raise alarms makes those rows appear (past records are not counted).
  Check how the rows look by making 1–2 rows appear this way.

### `alarm_list` / `alarm_banner`

- "The alarm that is active now, in 1 line" is `alarm_banner` (the newest unacknowledged one. If there is none, the `idle` text).
  "As a list" is `alarm_list`.
- The equipment of an alarm is `equipment` of its item in `alarms` (`"Oven 1"`); the `equipment` column shows it. In the history it is what the alarm is set to now.
- `columns` of `alarm_list`: choose and order from `time` `severity` `equipment` `message` `state` (when omitted, `["time", "message", "state"]`).
  The columns line up (each is as wide as its widest cell); `message` takes the rest.
  `mode`: `current` (when omitted. Active and unacknowledged) / `history` (history. Period with `days`). `rows` is the upper limit of rows shown
  (when omitted 50, for history 20), `row_height` is 40 when omitted. `ack_on_tap` is true when omitted (pressing a row acknowledges it).
- Size: height ≈ number of rows to show × `row_height` + 16. Rows that do not fit scroll.
  In `history` mode add about 50 px: a pager ("< 1/1 >") and a filter list ("All") are at the bottom. They cannot be hidden and
  their words stay English. Give a history list at least 320 px of width.
- `labels` replaces the text inside (`"@ID"` can also be used): `unacked` `active` `cleared` `empty` (when there is not even one)
  `idle` (when there is nothing in the banner) `more` (`%d` is the number) `raised` `cleared_event` `acked_event`.
  When omitted, English (`UNACK`, `No alarms` and so on). In a Japanese project, add `labels`.
  The `severity` column and the history's filter show the word written in `alarms` (`warning` and so on); `labels` replaces it per severity:
  `"severity.critical": "@sev_critical"`, `"severity.warning": "@sev_warning"` (a project in another language should do this).
- `colors` matches the colours to the project's rules (default: `critical` red, `warning` amber, `info` blue, white text, grey history):
  `"colors": { "critical": "#c62828", "warning": "#ef6c00", "info": "#1565c0", "text": "#ffffff", "cleared": "#cfd8dc", "history": "#eceff1", "history_text": "#263238" }`.
  On a light theme set `history` / `cleared` / `history_text` so the rows match the panels. `alarm_banner` takes the severities and `text` too.

### `trend`

- Inside it has the heading of the period (top) and 5 operation buttons and the legend (bottom). These use about 110px of height; the heading and the buttons cannot be removed.
  **Height 240 or more** (so that the graph part is 130px or more), width 400 or more.
  The 5 buttons take about 340px. A legend that does not fit next to them goes on a row of its own below (it is not cut), which takes about 30px
  more height from the graph. `"legend": false` shows no legend (then say what the lines are with a `label`).
- `tags`: an array of tag names, or the form that sets colours and names `[ { "tag": "temp", "color": "#42a5f5", "label": "Temperature" }, { "tag": "level", "color": "#66bb6a" } ]`.
  `label` is what the legend and the readout at a touch call the line (`"@ID"` of `texts` works); without it, the tag name.
  When omitted, all tags of the group. When the colour is omitted, theme colours are given in turn (the 2nd line tends to be orange. If it can be confused with a warning, set the colours).
  A tag that is not in `group` cannot be shown (look at `data_log.groups[].tags` in `project.json`).
- There is 1 vertical axis. Writing both `min` and `max` gives that range; when omitted it is decided automatically from the data shown. Values whose ranges differ greatly
  (temperature 20–30 and level 0–100) on the same axis make one of them flat, so split the widget to suit the look that was asked for, or say so.
- `labels`: `now` (text of the "back to now" button. `Now` when omitted).

### `bar_chart`

- Shows 1 tag of data logging as bars, per interval aligned to the clock (default 1 hour). For **production count per hour**, put the production counter
  (a tag that keeps increasing) in a group of `data_log` (`"trigger": "interval"`, `"interval_ms": 60000` is enough) and use `"mode": "increase"` (default).
  For the last value of each interval (temperature, stock), `"mode": "value"`.
- `bucket_s` (seconds per bar, default 3600), `bars` (number of bars 1–48), `start`: `now` (the last so many bars up to now. Default 12 bars) / `day` (from 0:00. When `bars` is
  omitted, one day = 24 bars. With 12 bars it is only 0:00–11:00).
- `target` draws a horizontal line at the target height on each bar (it looks like a dashed line broken between the bars). `label` is **the format of the time under the bars** (not a title. A title is a separate `label` widget).
- `target` (number) for the target line, `color` / `target_color`, `decimals`, `show_values` (the numbers above the bars), `label` (the time under the bars, strftime. Default is `%H`).
- Inside it has the numbers on top, the bars, and the times below. **Height 160 or more, width 24 or more per bar** (for 12 bars, width 360 or more. If the numbers do not fit, `show_values: false`).
  The size and colour of the text are the `style` of the widget (`font`, `text_color`).
- `--check --render` does not read the log, so the bars are empty (only the times appear). Check how the bars look with the procedure for running a copy (SKILL.md, "When you want to check what happens after a press"), after records have accumulated.
  `--check` finds mistakes in the names of the group and the tag.

### `chart`

- The values of tags while the screen is shown, as lines (or bars: `"chart_type": "bar"`), sampled every `sample_ms` (default 500). It keeps `points` samples
  (default 50): the time across is `points` × `sample_ms` (5 minutes = `"points": 100, "sample_ms": 3000`). Nothing is stored; it starts empty
  each time the screen is shown. For history that survives, use `trend` with a group of `data_log`.
- `min` / `max` is the vertical range (default 0–100) and may have decimals (`"min": 0, "max": 6.5` for bar). `series[].scale` multiplies a value before it is drawn
  (only needed to put tags of different units on one axis).
- **It draws no numbers on the axes.** Put the scale as `label` widgets beside it ("6", "3", "0", and "-5 min" / "now" below), and say what the line is with a `label`.
  `hdiv` / `vdiv` are the numbers of grid lines (default 5 / 8); with labels at 0, 3 and 6, `"hdiv": 3` puts a grid line at each.
- `--check --render` takes one sample, so the line is not visible in the picture (as with `trend`). How it looks with data can only be seen by running a copy
  (SKILL.md, "When you want to check what happens after a press").

### `xy_chart`

- A graph with 2 tags as the horizontal and vertical axes (position and load, opening and flow). To show the passage of time, use `chart` (a short time) or `trend` (recorded history).
- The keys of the axes are `x_tag` `x_min` `x_max` / `y_tag` `y_min` `y_max` (`x` `y` are the position of the widget). Always write the ranges (they are not decided automatically).
  Only the range numbers appear at both ends of the axes, so write what each axis is with a `label` nearby ("Position mm", "Load kN").
- The size is 200x160 or more. Keep **`points` to a few hundred at most** (every time a point is added all the lines are redrawn, so it is heavy on a small device.
  On a Raspberry Pi 4, 300 points is CPU 5–8 %, 2000 points is 25–32 %).
- To show the waveform of each cycle, write in `clear_tag` a tag that becomes true at the start of the cycle.
- Add more curves to `series` (4 in all, the axes are shared). The colours are, in order, blue, red, green, orange. On a screen where red looks like "abnormal", set `color`.
- In the picture of `--check --render` the points do not appear (only the frame and the axis numbers). Check how the lines look with the procedure for running a copy (SKILL.md, "When you want to check what happens after a press").

### `table`

- A widget that shows text and tag values in rows and columns (display only). Use it when values of the same shape line up in many rows (production count and state per machine, a list of recipes).
  For a few values, lining up `label` or `numeric_display` reads larger.
- `columns` is the headings (text or `{ "title", "width", "align" }`), `rows` is an array of rows, and a cell is text, a number, or `{ "tag": "<tag>", "format": "%.1f", "map": { "0": "Stop", "1": "Run" } }`.
  Make number columns `"align": "right"`, and put the unit in `format` (`"%d pcs"`) or write it in the heading.
- When turning a state into text with `map`, match the wording of the existing screens ("Run / Stop" and so on). Colours cannot be applied, so distinguish states by text.
- One row is about 36px (with `font` 16. The heading is about the same). As a guide the height is "(number of rows + 1) × 36 + 8"; rows that do not fit scroll inside the table
  (look in the picture at whether the last row is cut off and whether a scrollbar appears).
- `"@ID"` can also be written in the values of `map` (translated to match the language).
  Columns without a width share the rest equally, so writing `width` on all but the columns of long text lines them up.
- In a project with `languages`, write the headings and the cell text with `"@ID"`.

### `datetime_input`

- A widget for entering a date and time with rollers (no keyboard needed). `"bind": "<tag>"`, `mode`: `datetime` / `date` / `time`, `seconds` for seconds.
- The tag is a **string** (`2026-11-21 13:05`, `2026-11-21`, `13:05`) or a **number** (Unix time in seconds. For `time`, the number of seconds from 0:00).
  Which to use follows the side that uses the tag (script, PLC). When making a new one, a string is easier to read.
- The size is decided by the content (do not write `w` `h`): date + time is about 350 wide, with seconds about 420, date only 230, time only 130, height about 110.
  On a 480x320 screen, 1 per row. If a label goes next to it, place it above.
- It is an input that changes a setting, so if the existing screens put `level` on settings of the same kind, put the same `level` on it.
- In the picture of `--check --render`, the date of the tag's `init` appears on the rollers (the current time if `init` is empty).

### `meter`

- A needle meter (display only). `"bind": "<tag>"` for the value, `min` / `max` for the range of the scale. The number of the value appears inside the widget (`unit` for the unit, `decimals` for the digits).
- **Make each side 160 or more** (under 140 the scale numbers do not appear). Place it as a square (even if made wide, the scale becomes a square of the shorter side).
- Colour the caution and abnormal ranges with `zones`: `"zones": [ { "from": 80, "to": 100, "color": "#e53935" } ]` (red is abnormal, orange is warning. Do not use them for decoration).
- When the scale numbers are crowded, reduce `ticks` and `major_every` (example: for 0–100, `"ticks": 11, "major_every": 2`).
  Making `(ticks - 1)` divisible by `major_every` makes the last number `max`.
- `decimals` also applies to the scale numbers (`2` gives `0.00 1.00`). If the scale is crowded, reduce `decimals`.
- To show large numbers together, `label` or `numeric_display` is easier to read. Use a meter when you want to show "about where it is" at a glance.

### `log_view`

- A widget for viewing a log as a table (read only). `source`: `system` (messages of the runtime) / `operation` (record of operations) / `data` (data logging. Needs `group`).
  **If `project.json` has no settings for that log, nothing appears** (`--check` points it out). Add log settings only when asked.
- Inside it has a top band (a level dropdown, the count, 2 paging buttons. About 46px high) and a table.
  **Height 200 or more, width 440 or more**. One row is about 32px, so as a guide `rows` is "(height − 90) ÷ 32" (a long message wraps and makes the row taller. What overflows scrolls inside the table).
- `columns`: for `system`, `time` `level` `source` `message` `count`; for `operation`, `time` `user` `screen` `kind` `target` `old` `new`;
  for `data`, `time` and the tag names of the group. When omitted, the commonly used columns (if the width is under 560, `system` is `time` `level` `message`, `operation` is `time` `user` `target` `new`).
- The column headings are English (`Time` `Message` …). To make them Japanese or another language, write them in `labels`: `"labels": { "time": "時刻", "level": "種類", "message": "内容", "empty": "記録なし" }`
  (`"@ID"` in a project with `languages`). The column headings of `data` stay as the tag names.
- To let the user filter by text, make a string tag, bind a `text_display` (`"input": true`) to that tag, and write `"filter_tag": "<tag>"` (the screen needs a `keyboard`).
- To set the column widths, write the width per column name in `widths`: `"widths": { "time": 150, "user": 90 }` (columns not written share the rest).
  If a heading wraps (the Japanese 「ユーザー」 and so on), widen that column.
- Placing 2 side by side needs width (440 + 440). To show both the operation log and the system log on an 800 px screen, stack them at the same place and
  switch `visible` in `bind` with a tag (a button writes 0 / 1).
- The operation log is a record of "who did what". To limit who sees it, put `level` on that screen or widget (match the `level` of the existing screens).
- In the picture of `--check --render`, the table has only the headings and no rows (because the log is not read). For `system`, even when the settings exist,
  "The system log is not recorded …" may appear (because nothing is recorded during the check. It is not a mistake). Check how the rows look with the procedure for running a copy (SKILL.md, "When you want to check what happens after a press").

### `recipe_table`

- Inside it has the list of recipes and buttons such as write and save. Height 300 or more; the width is basically the full screen.
- `labels`: `write` `read` `save_as` `delete` `item` `recipe` `current` `ok` `cancel` `name`, the confirmation sentences `confirm_write` `confirm_read`
  `confirm_delete` (`%s` is the recipe name. The write confirmation is "Write '%s'?" when omitted). Write the names and units of the items in `items` of `recipes` (`project-features.md`).

How to write the settings these widgets need (`alarms`, groups of `data_log`, `recipes`) is in `project-features.md`.
If these widgets are asked for and the project does not have the settings, the settings may be added too (if there are existing settings, match how they are written. Report that they were added).

## User levels

When `project.json` has `users`, putting `"level": 2` on a widget or a screen lets only users of that level or higher operate it.
`"level_deny"`: `disable` (default, shown dimmed) / `hide` / `login` (pressing shows the login screen). Do not add it if there is no `users`.

- **`level` of a widget**: `level_deny` is how it is treated while the level is not enough. `level` applies only to that widget itself (`disable` put on a panel does not reach the widgets inside.
  Put it on each widget that must not be operated. `hide` hides it together with its content).
- **`level` of a screen**: `level_deny` **does not exist for a screen** (if written, `--check` points it out). When a user whose level is not enough tries to go to that screen,
  the login screen appears, and after logging in with a sufficient level the user goes straight on to the intended screen. So the button that goes there does not need `level`.
- On logout (including automatic logout), if the screen being shown can no longer be viewed, it **returns to the start screen**. Do not add a `goto` after `logout`.
- When not logged in: `sys.user.name` is empty, `sys.user.level` is 0, `sys.user.level_name` is empty. To show "Not logged in",
  write a key of the empty string in `map`: `"bind": { "text": { "tag": "sys.user.name", "map": { "": "@not_logged_in" } } }`
  (a value not in `map` is shown as it is. An empty string that means "nothing yet" can be replaced by words in this form for any tag).
- The level names (`users.levels`) are in 1 language only (`"@ID"` cannot be used).
- **The picture of `--check` is rendered in the state of being logged in at the highest level** (the user name is `preview`). How it looks when not logged in or
  at a low level is checked with the procedure in "When you want to check what happens after a press" of SKILL.md, pressing the digits of the login screen with `--click`
  (read the coordinates to press from a picture taken beforehand).
- **When adding `users` later**: if the project already has widgets or screens with `"level"` written, or `recipes.level`, they start to take effect as soon as `users` is added
  (while there is no `users` they are ignored. `--check` reports "`level` does nothing" just once). Before adding, search for `"level"` and report what will become restricted.

## Using features outside the screens from a screen

Schedules, sound, data transfer between equipment, mail, sending of logs, and template screens are written in `project.json` (and `template` in the screen file).
**The list of setting keys and how to write them is in `project-features.md`**. Add or change them only when asked. What can be used from the screen side is the following
(the tags exist only when the project has that feature. The tags exist even with `"enabled": false`):

| Feature | From the screen |
| --- | --- |
| Sound (`sound`) | Mute switch `{ "type": "switch", "bind": "sys.sound.mute" }`, volume `{ "type": "slider", "min": 0, "max": 100, "bind": "sys.sound.volume" }`, play a sound `{ "action": "call", "fn": "sound.play", "arg": "beep" }` |
| Sound: buzzer stop | `{ "type": "button", "text": "Buzzer stop", "bind": { "visible": "sys.sound.alarm" }, "on_click": { "action": "set", "tag": "sys.sound.alarm", "value": false } }` (appears only while the alarm sound is sounding. Pressing it stops the sound until the next new alarm) |
| Mail (`mail`) | Send `{ "action": "call", "fn": "mail.send", "arg": "Subject\nBody {tag name}" }` (the text as it is. `"@ID"` cannot be used), test `"mail.test"`, `sys.mail.waiting` (number waiting to be sent), `sys.mail.error` (1 while failing) |
| Sending of logs (`uploads`) | Send now `{ "action": "call", "fn": "upload.run", "arg": "<name>" }`, `sys.upload.waiting`, `sys.upload.error` |
| Schedules (`schedules`) | There is no widget to put on a screen (view and change them in System > Schedule on the device). A button with `{ "action": "system" }` opens System |
| Data transfer (`transfers`) | There is no widget to put on a screen. Just display the source and destination tags in the ordinary way (the state appears in System > Transfers on the device) |

- When `mail` / `uploads` is `"enabled": false`, `mail.send` and `upload.run` send nothing (only a warning appears in the log).
- In the picture of `--check --render`, sound, schedules, transfers, mail and sending do not run (the destination tags of a transfer stay at their `init` values).
  `sys.sound.alarm` cannot be set to 1 with `--set`, so the "Buzzer stop" button cannot be checked in the picture. Decide its position and colour to match the other buttons, and write this in the report.
- "Do ... every morning at 8:00" and "send the logs at night" are written in `schedules`, not in a screen (`project-features.md`).
