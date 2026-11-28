# Features of project.json (outside the screens)

A list of the features written in `project.json`, not in the screens (`screens/*.json`). Add or change them **only when asked** (they concern equipment and outside servers).
After writing, pass `--check` (it finds mistyped keys, tags, screens and logs that do not exist, no encryption, and so on).
**Do not write passwords in `project.json`** (they are set on the System screen of the device. `--check` points it out).

Contents: [Template screens](#template-screens) / [Alarms](#alarms-alarms) / [Data logging and trends](#data-logging-and-trends-data_log) /
[Recipes](#recipes-recipes) / [Scripts](#scripts-scripts) / [Date and time](#date-and-time) / [Moving values without equipment](#moving-values-without-equipment) /
[Schedules](#schedules-schedules) / [Sound](#sound-sound) /
[Data transfer between equipment](#data-transfer-between-equipment-transfers) / [Mail](#mail-mail) / [Sending logs](#sending-logs-uploads) /
[Remote display](#remote-display-remote) / [Long press that opens the system settings](#long-press-that-opens-the-system-settings-system_access) /
[Basis of the look](#basis-of-the-look-display--theme--font) / [Users and levels](#users-and-levels-users) / [Screen saver](#screen-saver-screen_saver) /
[Operation log and system log](#operation-log-and-system-log-operation_log--system_log) / [Send to the device](#send-to-the-device-deploy)

The files listed in `screens` are relative paths **inside the project folder** only (`..`, absolute paths and drive letters are not allowed. The designer does not open it).

When asked for an alarm, trend or recipe screen, you may also add the settings that widget needs (`alarms`, a group of `data_log`, `recipes`) as something that was asked for.
If the project already has the settings, **add in the same style** (do not delete existing items, do not rename them). Report what you added.

## Template screens

Put the header and navigation on 1 screen (the template) and make it the base of the other screens. **This is a key of the screen file**.

```json
// screens/frame.json: the base (written like an ordinary screen. "${name}" is replaced by the value of each screen)
{ "name": "frame", "scrollable": false, "style": { "bg_color": "#141a22", "pad": 0 }, "children": [
  { "type": "label", "x": 16, "y": 12, "text": "${title}", "style": { "font": 24 } },
  { "type": "button", "x": 16, "y": 424, "w": 120, "h": 48, "text": "@main", "on_click": { "action": "goto", "screen": "main" } } ] }

// screens/report.json: a screen made on top of the base
{ "name": "report", "template": "frame", "template_args": { "title": "@report" }, "children": [ ... ] }
```

- Keys of the screen: `template` (the name of the base screen), `template_args` (`{ "name": text, number or true/false }`).
- The widgets of the base are placed first, and the screen's own widgets go on top of them. The `style` `scrollable` `flex` of the base are inherited too; if the screen writes them, the screen's take priority.
- What is replaced is `${name}` in the widgets of the base (`${screen}` is the name of the screen. What was not passed is empty). It is not replaced in the screen's own widgets.
  **Use `${name}` in displayed text such as `text`.** Written in a colour (`bg_color`) or a tag name, it stays as `${…}` in the base screen itself, so
  `--check` reports a problem in the base (`runtime: unknown color '${…}'`).
- "Change the colour of only the button of the current screen" can be done in the base: write the tag `sys.screen` (the name of the screen being shown. Popups are not included)
  in the `bind` of each button: `"bind": { "bg_color": { "tag": "sys.screen", "map": { "trend": "#26c6da", "default": "#192538" } } }`
  (`trend` is where that button goes). To show an underline or the like, put `{ "tag": "sys.screen", "eq": "trend" }` in `visible`.
- The base is an ordinary screen too, so put it in `"screens"` of `project.json`. A popup cannot be used as a base. Up to 8 levels can be stacked.
- **When stacking bases** (`equip_frame` on top of `frame`, and each screen on top of that): the middle base passes the values that come from
  the screen above down, with its own `template_args`: `{ "name": "equip_frame", "template": "frame", "template_args": { "title": "${title}" }, ... }`.
  If it does not pass them, `--check` says "template … has ${title}, which template_args does not give".
- Place the screen's own widgets where they do not overlap the widgets of the base (header, navigation). The picture of `--render` renders the base too.
- In a project whose existing screens use a template, give the new screen the same `template` too (do not copy the common parts).

## Alarms (`alarms`)

While the value of a tag meets the condition, the alarm is raised (shown in `alarm_list` / `alarm_banner` / `alarm_summary`. The record is `logs/alarm.db`).

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

| Key | Content |
| --- | --- |
| `items[].name` | Name (required, must be unique. Shown in the record and the CSV) |
| `items[].tag` | Tag to watch (required) |
| `items[].condition` | `"true"` (default. While the tag is true) / `"false"` / `"eq"` `"ne"` `"gt"` `"lt"` `"ge"` `"le"` (compared with `value` or `value_tag`) |
| `items[].value`, `items[].value_tag` | Value to compare with (constant), or the tag to compare with (use `value_tag` to let the setpoint be changed on a screen) |
| `items[].severity` | Severity word (default `"warning"`. Usually `"info"` `"warning"` `"critical"`). Shown as is in the column of `alarm_list` (cannot be translated) |
| `items[].message` | Text to show (`"@ID"` can be used. If omitted, `name`) |
| `items[].ack` | `false`: disappears from the list when the condition goes away, even without acknowledging (default true: stays until acknowledged) |
| `items[].enabled` | `false`: not used |
| `ack_level` | Level of the user who can acknowledge (when there is `users`) |
| `enabled`, `path`, `retention_days`, `max_rows`, `max_size_mb` | Settings of the record (may be omitted. Default `logs/alarm.db`, 30 days, 100,000 rows) |

- Tags: `sys.alarm.active` (number raised), `sys.alarm.unacked` (number not acknowledged), `sys.alarm.latest` (text of the newest one).
- Action to acknowledge: `{ "action": "call", "fn": "alarm.ack_all" }` (pressing a row of `alarm_list` acknowledges too).
- "When A and B" cannot be written with 1 alarm. Calculate a bool tag with a script and use that tag as `tag`.
- In `--check --render`, it is raised when you `--set` a value that meets the condition (`--set oven_temperature=260`).

## Data logging and trends (`data_log`)

Records the values of tags (`logs/data.db`). The `trend` widget renders a **group** from here.

```json
"data_log": {
  "groups": [
    { "name": "process", "trigger": "interval", "interval_ms": 1000, "tags": ["oven_temperature", "proofing_temperature", "proofing_humidity"] },
    { "name": "state", "trigger": "change", "tags": ["mixer_running", "oven_heater_on"] },
    { "name": "batch", "trigger": "tag", "trigger_tag": "batch_done", "tags": ["production_count", "defect_count"] }
  ]
}
```

| Key | Content |
| --- | --- |
| `groups[].name` | Name of the group (required, must be unique. The `group` of `trend`, the `group` of `log_view`, `"data:<name>"` of `uploads`) |
| `groups[].tags` | Array of the tag names to record (required. Numeric tags. Only the tags listed here can be shown in `trend`) |
| `groups[].trigger` | `"interval"` (default. Every `interval_ms`, 100 or more, default 1000) / `"change"` (when any of them changes) / `"tag"` (when `trigger_tag` goes from false to true) / `"manual"` (only on the `data_log.sample` action) |
| `groups[].enabled` | `false`: not recorded |
| `enabled`, `path`, `retention_days`, `max_rows`, `max_size_mb` | Settings of the record (may be omitted. Default `logs/data.db`, 30 days, 1,000,000 rows) |

- To render a trend, `trigger` is `interval` (1000 to 5000 ms). Values with very different ranges (temperature 200 °C and humidity 80 %)
  may be in the same group, but use separate `trend` widgets (there is 1 vertical axis).
- The record is written to the SD card of the device. Do not put many tags in a fast period (100 ms).

## Recipes (`recipes`)

Holds named sets of tags (the setpoints of each product); choose one in `recipe_table` and write it to the tags.

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

| Key | Content |
| --- | --- |
| `books[].name` | Name of the book (required, 1 to 32 characters, `/ \ : * ? " < > | .` cannot be used. The `book` of `recipe_table`) |
| `books[].items` | Tags the recipe holds (required. Use setpoint tags: writing sends them to the equipment). A tag name or `{ "tag", "label", "unit" }`: `recipe_table` shows "label (unit)" (`label` may also be `"@ID"`. If omitted, the tag name). **Always give a readable name and a unit** |
| `books[].recipes` | Recipes that are there from the start: `{ "name", "values": { tag name: value } }` (names must be unique) |
| `level` | Level of the user who can write, save and delete (when there is `users`. 0 = anyone) |
| `verify_ms` | Time to check, after writing, that the value of the equipment became the same (default 2000, 0 = do not check) |
| `path` | Folder for the recipes changed on the device (default `recipes`. If there is a file there, it takes priority over the `recipes` of the project) |

- The name of a recipe is shown as is (`"@ID"` cannot be used). A Japanese name needs a font.
- Actions for operating with buttons without placing a `recipe_table` (`{ "action": "call", "fn": ..., "arg": ... }`):
  `recipe.select` (`"book,name"`), `recipe.write` (`"book[,name]"`. If the name is omitted, the selected one),
  `recipe.save` (`"book,name"`: saves with the current values of the tags), `recipe.delete` (`"book,name"`),
  `recipe.export` (`"book[,file]"`), `recipe.import` (`"book,file"`).
  `recipes.level` is needed for `write` / `save` / `delete` / `import` (if it is not enough, the login screen appears and nothing is done). Anyone can do `select` and `export`.
- `recipes.level` is **one setting shared by all books**. To give only a certain `recipe_table` a higher level, put `"level"` on that widget.
- `sys.recipe.<book name>.selected` holds the **first recipe** from startup (it is not empty). Do not write it from a screen (selecting is `recipe.select`).
  To change the colour of the selected button: `"bind": { "bg_color": { "tag": "sys.recipe.<book name>.selected", "map": { "<recipe name>": "<colour>", "default": "<colour>" } } }`.
- `sys.recipe.status` is 1 line of English (`'Product B' written, checking ...`, or the reason of a failure). Its length is not fixed, so fix the width and set `"long_mode": "wrap"`.
- The look "after writing" can be rendered by adding `--set "sys.recipe.<book name>.written=<recipe name>"` to `--check`
  (`sys.recipe.status` can be set too). Check the flow of actually writing with the steps of "When you want to check what happens after a press" in SKILL.md.
- `sys.recipe.<book name>.selected` is the recipe **selected in the list**; `sys.recipe.<book name>.written` is the recipe **written last (in use now)**
  (empty until one is written). To show "the current recipe" in the header or the like, use `written`. The result of the last operation is `sys.recipe.status`.

## Scripts (`scripts`)

In Lua: calculate tags, switch screens and so on. Call one with `{ "action": "script", "name": "<name>" }`, or it runs automatically by `trigger`.

```json
"scripts": [
  { "name": "can_start", "trigger": "tag", "tags": ["auto_mode", "mixer_running"], "code": "tag.can_start = not tag.auto_mode and not tag.mixer_running" },
  { "name": "line", "trigger": "interval", "interval_ms": 500, "file": "scripts/line.lua" },
  { "name": "init", "trigger": "startup", "code": ["tag.shift = 1", "tag.batch = 0"] }
]
```

| Key | Content |
| --- | --- |
| `name` | Name (must be unique) |
| `code` / `file` | Lua code (a string, or an array of single lines), or a `.lua` file inside the project |
| `trigger` | `"manual"` (default. Called from an action) / `"startup"` / `"interval"` (+ `interval_ms`) / `"tag"` (+ `tags`: when any of them changes) / `"screen_show"` `"screen_hide"` (+ `screen`) |
| `enabled` | `false`: not used |

Available in Lua: `tag.name` (read and write. `sys.` tags are `tag["sys.alarm.active"]`), `goto_screen("name")`, `back()`,
`current_screen()`, `run("script")`, `call("fn", "arg")` (the same as the `call` action), `log(...)` / `warn(...)`, `millis()`,
`os.time()` / `os.date(...)` / `os.clock()`, `event.trigger` / `event.tag` / `event.value` / `event.screen`.
The libraries are `string` `table` `math` `utf8` `coroutine` (files and processes cannot be used). A run that exceeds 500 ms is stopped.
Memory is up to 32 MB for all scripts together (the script that exceeds it fails with "not enough memory"). Do not pile up large tables or strings in globals.

- A written tag is sent to the equipment if it is a tag connected to equipment. **Do not write values that the equipment owns (the actual temperature and the like) from a script** ("Moving values without equipment" below is a different matter).
- Scripts run in `--check --render` too (periodic ones run a few times before rendering).

## Date and time

String tags that the runtime has: `sys.date` (`2026-11-22`), `sys.time` (`13:05:07`), `sys.datetime` (`2026-11-22 13:05:07`).
They are the time of the device (the local time of its region) and change every second. A clock in the header is `{ "type": "label", "bind": { "text": "sys.datetime" } }`.

- When another form is needed (`13:05`, with the day of the week), make a string tag and write `tag.clock = os.date("%H:%M")` in a periodic script.
- The picture of `--check --render` has the time when it was run. To render at a fixed time, `--set "sys.datetime=2026-11-22 08:30:00"`
  (`--set` changes only that tag. If `sys.date` / `sys.time` are shown too, `--set` each of them).

## Moving values without equipment

Moving values without a PLC for a demo or a check is **only when asked** (if not asked, the values stay at the `init` of `tags`. That is correct).
Values that appear to move are not real, so **always show "SIMULATION" on the screen** (a `label` shown only while `sys.sim.active` is 1. Below).

**Write it in `simulation` (`project.json`)**. It is a different place from `drivers` (the connections to equipment), so it can be added while keeping the rule "do not touch `drivers`".
Do not use `"type": "sim"` of `drivers` (if it is already there, leave it as it is).

```json
"simulation": { "items": [
  { "tag": "oven_temperature", "mode": "sine", "min": 208, "max": 212, "period_ms": 30000 },
  { "tag": "mixer_rpm", "mode": "follow", "target": "mixer_rpm_sp", "rate": 40, "when": "mixer_running", "idle": 0, "noise": 1.5, "min": 0, "max": 300 },
  { "tag": "production_count", "mode": "counter", "min": 0, "max": 999999, "period_ms": 3000, "when": "line_running" },
  { "tag": "line_state", "mode": "cycle", "values": ["Idle", "Run"], "period_ms": 10000 }
] }
```

| Key | Content |
| --- | --- |
| `tag` | Tag to write (required) |
| `mode` | `sine` `triangle` `ramp` (repeats `min` → `max`) / `square` (alternates `max` and `min`) / `random` / `counter` (increases by 1 every `period_ms`; after `max` comes `min`) / `cycle` (the texts of `values` in order) / `follow` (below) |
| `min`, `max`, `period_ms` | Range (if omitted, the `min` / `max` of the tag), period (default 1000) |
| `when` | A tag name (moves only while true) or `{ "tag": "mode", "eq": 2 }`. Does not write while false (a counter stops) |
| `target`, `rate`, `idle` | `follow`: approaches `target` (a tag or a number) by `rate` per second. While `when` is false, goes to `idle` (default `min`). Movements such as: the speed rises with the run button, the temperature rises with the heater |
| `noise` | Adds a fluctuation of ± `noise` to the value of `sine` `triangle` `ramp` `follow` (to make it look real. Keep it small). `follow` does not fluctuate while `when` is false, and even when fluctuating does not go outside `min`..`max` |
| `enabled` (of `simulation`) | `false`: stops it |

- **Starting value**: `counter` continues counting from the `init` of the tag (if it is within `min`..`max`) (today's production count increases from 3420). Waveforms start from phase 0:
  `sine` starts at the middle, `ramp` / `triangle` start at `min`. Decide `min` / `max` so that an alarm condition is not met right after startup (do not start a tag
  that has a low-limit alarm from `min` with `ramp`). Move a bool tag with `square` (true and false for half of `period_ms` each).
- Moving in response to operation: use the tags that buttons write (`mixer_running`, the setpoint `mixer_rpm_sp`) for `when` and `target`.
  For "when A and B", make a bool tag with a script and use it as `when`. More complex movements are for a script (a periodic script that moves the values itself).
- So as not to keep writing the values of the equipment (the actual temperature and the like) after the equipment is connected, if there is a tag that shows the PLC connection,
  add `"when": { "tag": "plc_online", "eq": false }`.
- Display: put `{ "type": "label", "text": "SIMULATION", "bind": { "visible": "sys.sim.active" }, "style": { "text_color": "#f5b942" } }`
  in the header (template). `sys.sim.active` is 1 when there is `simulation` or an enabled `sim` driver.
- It does not move in `--check --render` (the values are `init`. Check with `--set`). See the movement with the steps of "When you want to check what happens after a press"
  (`simulation` remains even if the `drivers` of the copy are deleted).

## Schedules (`schedules`)

Runs actions (the same actions as a button) at a fixed time.

```json
"schedules": [
  { "name": "start", "at": "08:00", "days": "weekdays", "do": { "action": "set", "tag": "run_enable", "value": true } },
  { "name": "report", "at": "23:55", "catch_up": true, "do": [ { "action": "call", "fn": "upload.run", "arg": "nightly" } ] },
  { "name": "service", "at": "2026-12-01 09:00", "do": { "action": "popup", "screen": "service_due" } }
]
```

| Key | Content |
| --- | --- |
| `name` | Name (required, must be unique, up to 64 characters) |
| `at` | `"HH:MM"` (every day / the days of `days`), or `"YYYY-MM-DD HH:MM"` (once only) |
| `days` | `"daily"` (default), `"weekdays"`, `"weekend"`, or `["mon", "wed"]` (`sun mon tue wed thu fri sat`). Do not write it with an `at` that has a date |
| `do` | 1 action, or an array of them (required) |
| `enabled` | `false`: not run |
| `catch_up` | `true`: a time that passed while the device was stopped is run once at the next startup |

On the device, the time, days and on / off can be changed in System > Schedule (`do` cannot be changed). Up to 200 items.

## Sound (`sound`)

With just `"sound": {}` present, the touch sound (`click`) and the alarm sound (`alarm`, every 3 seconds until acknowledged) play.

| Key | Content |
| --- | --- |
| `volume` | 0 to 100 (default 80) |
| `mute` | `true`: muted |
| `touch`, `alarm` | Sound: `"click"` `"beep"` `"alarm"`, a `.wav` inside the project (`"assets/tap.wav"`), or `false` |
| `alarm_every_s` | Interval in seconds for playing the alarm sound again (default 3, 0 = once only) |
| `output` | `"auto"` (default) / `"audio"` / `"gpio"` / `"none"` |
| `device` | ALSA device of Linux (headphone jack of the Raspberry Pi: `"plughw:CARD=Headphones,DEV=0"`) |
| `gpio`, `gpio_active_low`, `gpio_chip` | GPIO with a buzzer connected (Linux) |

How to use it from a screen (mute, volume, stop the buzzer, play a sound) is in "Using features outside the screens from a screen" of `binding-and-actions.md`.

## Data transfer between equipment (`transfers`)

Copies the value of a tag to another tag (PLC → another PLC and the like). Does not write while disconnected.

```json
"transfers": [
  { "name": "speed", "from": "plc1_speed", "to": "plc2_speed" },
  { "name": "temp", "from": "plc1_temp", "to": "plc2_temp", "trigger": "cyclic", "every_ms": 1000, "scale": 0.1, "when": "run" }
]
```

| Key | Content |
| --- | --- |
| `name` | Name (required, must be unique) |
| `from`, `to` | Tag names (required. The same tag is not allowed, `sys.*` is not allowed in `to`) |
| `trigger` | `"change"` (default) / `"cyclic"` (+ `every_ms`, 100 or more, default 1000) |
| `scale`, `offset` | Value written = source value × `scale` + `offset` |
| `out_of_range` | A value outside the `min` / `max` of the destination tag: `"skip"` (default) / `"clamp"` |
| `when` | Condition: a tag name, or `{ "tag": "mode", "eq": 2 }` (`eq ne gt lt ge le`) |
| `enabled` | `false`: not run |

A loop (A → B → A) is not run (`--check` points it out). Up to 500 items.

## Mail (`mail`)

Sends alarms by mail (SMTP).

```json
"mail": { "host": "smtp.example.com", "security": "starttls", "username": "hmi",
          "from": "hmi@example.com", "to": ["shift@example.com"],
          "alarms": { "severities": ["critical"], "subject": "[{project}] {alarm.message}", "body": "{alarm.time}\nLevel {level} %" } }
```

| Key | Content |
| --- | --- |
| `host`, `port` | Mail server (required). If `port` is omitted, 587 / 465 / 25 |
| `security` | `"starttls"` (default) / `"tls"` / `"none"` (no encryption: `--check` tells you) |
| `username` | Login name (if omitted, no login). **Do not write `password`** (System > Mail on the device) |
| `from`, `to` | Sender address (required), recipients (1, or an array. Required, up to 20) |
| `verify`, `ca_file` | Verify the certificate (default true). `ca_file` is a file inside the project |
| `timeout_ms`, `retry_s`, `retries`, `queue` | Timeout, interval and number of resends, limit of mails waiting to be sent |
| `alarms` | `events` (`"raised"` default, `"cleared"`, `"acked"`), `severities`, `min_interval_s`, `subject`, `body`. `false`: alarms are not sent |
| `enabled` | `false`: not sent |

`{name}` in the subject and body: the value of a tag, `{project}`, `{time}`, `{alarm.name}` `{alarm.message}` `{alarm.severity}` `{alarm.value}` `{alarm.event}` `{alarm.time}`.
Even with `"enabled": false` the settings are checked, and the `sys.mail.*` tags exist too (it only does not send). How to use it from a screen is in `binding-and-actions.md`.

## Sending logs (`uploads`)

Makes logs into CSV files and sends them to a server. **It does not send automatically**: put the `upload.run` action in a schedule or a button.

```json
"uploads": [ { "name": "nightly", "url": "https://files.example.com/hml/", "username": "hmi",
               "logs": ["data:line1", "alarms", "operation", "system"] } ]
```

| Key | Content |
| --- | --- |
| `name` | Name (required. Letters and digits, `_`, `-`) |
| `url` | `https://server/folder/`, `http://…`, `ftp://server/folder/` (http and ftp are not encrypted: `--check` tells you). A user name, `?` and `..` cannot be written in the URL |
| `method` | HTTP: `"put"` (default) / `"post"` |
| `username` | Login name. **Do not write `password`** (System > Upload on the device) |
| `logs` | What to send (required): `"data:<group name of data_log>"`, `"alarms"`, `"operation"`, `"system"`. That log must exist in the project |
| `verify`, `ca_file`, `timeout_ms`, `retry_s`, `retries`, `keep` | Certificate verification, timeout, resends, limit of files waiting to be sent |
| `enabled` | `false`: not sent |

To send from a schedule, `"do": { "action": "call", "fn": "upload.run", "arg": "nightly" }`. How to use it from a screen is in `binding-and-actions.md`.
Even with `"enabled": false` the settings are checked, and the `sys.upload.*` tags exist too. Up to 10 items.

## Remote display (`remote`)

View and operate the screen of the device with a VNC viewer.

```json
"remote": { "enabled": true, "port": 5900, "view_only": false }
```

| Key | Content |
| --- | --- |
| `enabled` | `true`: listens (default `false`) |
| `port` | Default 5900 |
| `view_only` | `true`: view only |

- **Do not write `password`** (even if written it is not used, and `--check` points it out). The password is set in System > Remote on the device.
  Until it is set, a connection is possible only from the device itself (127.0.0.1). Tell that to the person who uses it.
- VNC is not encrypted. It is for closed networks.

## Long press that opens the system settings (`system_access`)

The system settings of the device (System) open when **the top left corner of the screen is pressed for 3 seconds**, even without a `{ "action": "system" }` button (nothing needs to be written).

```json
"system_access": { "hold": "top_left", "hold_s": 3 }
```

| Key | Content |
| --- | --- |
| `hold` | `"top_left"` (default) / `"top_right"` / `"bottom_left"` / `"bottom_right"` / `"none"` |
| `hold_s` | Seconds to keep pressing (2 to 10, default 3) |

- The corner is a square of 1/8 of the short side (minimum 48 px). **Do not place a button that works by `repeat` or by a long press in that corner** (if you place one, choose another corner with `hold`).
- Do not set `"none"` without being asked (if there is no `{ "action": "system" }` either, the settings can no longer be opened on the device. `--check` points it out).

The language of the system settings screens follows the display language of the project (`texts`) (Japanese / English). A project that has no languages
is Japanese if its title is Japanese. To fix it, `"system_language": "ja"` (or `"en"`). Japanese needs a Japanese font
in `font.file` (without it, it stays English).

## Basis of the look (`display` / `theme` / `font`)

```json
"display": { "width": 800, "height": 480 },
"theme":   { "dark": true, "primary": "#2196f3", "secondary": "#ff9800", "font": 16 },
"font":    { "file": "assets/fonts/NotoSansJP-subset.otf" }
```

- **Do not change. Read and match.** The screen size, light or dark, and the main colours are the premise of all screens of that project. Change them only when asked.
- **When asked "make it a light theme"**: changing `theme.dark` alone is not enough. Most screens write the background and text colours as fixed values,
  so **replace the colours of all screens and templates** (it was asked for, so you may change the templates). Do not change the layout.
  - Guide: screen background `#eef1f5`, top and bottom bands `#dfe5ec`, card `#ffffff`, text `#1c2733`, subdued text `#546e7a`.
    Make the state colours dark enough to read on a white background (green `#2e7d32`, orange `#e65100`, red `#c62828`).
  - **White text on coloured buttons and badges stays white** (darken only the white text on the background). After replacing, render all screens and look at them.
  - A button that cannot be pressed is shown in a pale colour with dark text in the light theme. A button with a fixed text colour (`text_color` of `style_label`)
    fades with that colour kept, so do not write fixed white text on a button that can become unpressable.
- `theme.dark`: `false` is the light theme. Switches, dropdowns, input fields, the keyboard, and **the system settings screens and the login screen** become light too.
  Match the background and text colours of a new screen to the existing screens (do not write a fixed dark background colour on a screen of the light theme).
- `theme.primary`: the colour of what is selected and of the main buttons to press. "Apply" on the system settings screens and "OK" on the keyboard are this colour too.
  On your own screens too, make "confirm, save, next" this colour, and use **red only for operations that cannot be undone and for alarms**.
- `theme.font`: the default size of text. `font.file`: the font that shows Japanese and the like (without it, only the built-in Latin).
- `display.touch` (`swap_axes`, `calibration`, `jitter` and so on) and `display.fbdev` / `input` are the wiring and adjustment values of that device. **Do not touch them**.

## Users and levels (`users`)

Active only when written (without it, anyone can operate everything). **Add it only when asked**.

```json
"users": {
  "levels": ["Operator", "Maintenance", "Admin"],
  "list": [ { "name": "operator", "level": 1, "pin": "1111" }, { "name": "admin", "level": 3, "pin": "0000" } ],
  "auto_logout_s": 300, "max_failures": 5, "lock_s": 60, "system_level": 3
}
```

| Key | Content |
| --- | --- |
| `levels` | Names of levels 1, 2, 3 … (at most 9). Level 0 is "nobody is logged in" |
| `list` | Users (at most 16): `name` (up to 16 characters), `level`, `pin` (4 to 8 digits) |
| `auto_logout_s` | Seconds without operation until logout (default 300, 0 = never) |
| `max_failures` / `lock_s` | After this many wrong PINs, locked for this many seconds (default 5 times / 60 seconds) |
| `system_level` | Level that can open the system settings screens (default: the highest level) |

- How to write `"level"` / `"level_deny"` on screens and widgets is in `binding-and-actions.md`. The login screen is built in
  (opened with `{ "action": "login" }`. It appears by itself when an operation that needs a level is done). `{ "action": "logout" }`.
- Tags: `sys.user.name`, `sys.user.level` (0 = not logged in), `sys.user.level_name`, `sys.user.logged_in`.
  Use them to show the user name in the header, to show "Log out" only while logged in, and so on.
- Making 1 user per role (operator, maintenance, administrator) is enough. Tell the user that more people can be added in System > Users on the device.
- The default of `auto_logout_s` is 300 seconds. When used together with the `logout` of `screen_saver`, **the shorter one logs out first**.
  For "log out only when the screen goes off", set `auto_logout_s` to `screen_saver.after_s` or more (or 0).
- **Write a temporary PIN and tell the user "change it in System > Users on the device".** Do not ask for the real PIN and write it
  (`project.json` is a file that is handed to other people). Always include 1 user of `system_level` or higher (without one, nobody can open the system settings).
- When users are edited on the device, that device uses its own list (`project.local.json`), and the `list` of `project.json` is no longer used.

## Screen saver (`screen_saver`)

```json
"screen_saver": { "after_s": 600, "wake_on_alarm": true, "logout": false }
```

- After `after_s` seconds without operation, the screen is turned off (0 = never). A new alarm turns it on (`wake_on_alarm`). `logout`: log out when it turns off.
- The touch that turns it on presses nothing. Tag `sys.screen.off` (1 while off. Writing 1 turns it off at once: a "turn the screen off" button).

## Operation log and system log (`operation_log` / `system_log`)

```json
"operation_log": { "user_tag": "operator" },
"system_log":    { "level": "info", "retention_days": 30 }
```

- `operation_log`: records value writes from the screens (before and after the change), screen switches, `call` and restarts. `user_tag` is
  the string tag that holds "who" (not used in a project with `users`; the name of the logged-in user goes in). What was typed in a password field is not recorded.
- `system_log`: records the messages of the runtime (script errors, connection to and disconnection from equipment). `level`: `info` / `warn` / `error`.
- Both are shown on a screen with `log_view` (`binding-and-actions.md`). CSV is `operation_log.export` / `system_log.export`.
- When asked "keep a record of operations", first see whether `operation_log` is already there (if it is, there is nothing to add. Report that).
  In a project with `users`, login, logout and failed logins are recorded too, and `user_tag` is not used (it does no harm if it remains. Do not delete it).

## Send to the device (deploy)

**Only when asked.** Before sending, always pass `--check` and show the user the content of `--capabilities`.

```
hml_runtime --capabilities project.json            # what this project can do (writes to equipment, sending to the outside, things that run by themselves)
hml_designer --deploy project.json --direct --host <device address> --check     # only looks at whether it connects and whether the other side is the same device as before
hml_designer --deploy project.json --direct --host <device address>             # sends, and restarts the runtime of the device
```

- `hml_designer` is in the same folder as `hml_runtime`. The order of the options does not matter (`project.json` first or last).
- `--deploy ... --check` **sends nothing** (it only looks at whether it connects and at the fingerprint of the other side). Even when told "do not send", this may be run.
  When told "tell me how to send it", do not run it; tell the steps: `--check`, the content of `--capabilities`, the 2 commands above,
  that a confirmation appears on the screen of the device, and to change the temporary PIN.
- Do not guess the address of the device (ask). Do not run `hml_designer --help` (the designer window opens and it stops there).
- For a first device or a first designer, and for a project that **can do more**, a confirmation appears on the screen of the device
  (System > Deploy). It waits a few minutes until the person at the device presses it; if it is not pressed, nothing is sent. **Tell this before sending**.
- Values changed on the device (`project.local.json`), such as the user and driver settings, are not overwritten by a deploy.
- Replacing the runtime itself (`--runtime <file>`) is only when the user says so.

## About the system settings screens (do not make them, do not imitate them)

The settings of the device (network, connection targets of the drivers, users, the clock, update from a USB memory, acceptance of deploys) are
in the built-in screens. **Do not make the same thing as screens of the project**. Place only the entrance: a `{ "action": "system" }` button
(even without it, a long press on the corner of the screen opens them). These screens are displayed to match the theme (light or dark, `primary`) and the language of the project.
