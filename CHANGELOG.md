# Changelog

What changed from one release to the next, for the people who use Tsukuraku. The newest is on top.
The rules for the numbers are in the README ("Versions").

Each release lists, where there is something to say:
**Changed** - what behaves differently and may need a look at your projects or devices (always read this part);
**Added**; **Fixed**; **Security**.

## Unreleased

**Added**

- **Simplified Chinese** for the system settings screens, the login screen and the keyboard's keys (`"system_language": "zh"`, or a
  project whose language is `zh`). The translation has not been read by a native speaker yet.
- A font for Simplified Chinese comes with the designer (Noto Sans SC, the hanzi of GB 2312, 1.7 MB).
- `README.zh-CN.md`: an overview in Simplified Chinese (also in the packages), with the video in Chinese.
- `trend`: a name for each line in the legend (`"tags": [{ "tag": "p", "label": "Pressure" }]`), and `"legend": false`.

**Fixed**

- `trend`: a legend that did not fit next to the buttons was cut off (at the documented smallest width of 400, even a short tag name). It goes on a
  row of its own now.
- `chart`: `min`, `max` and the values were drawn as whole numbers - 3.2 on a 0 to 6 axis was drawn at 3. Decimals are drawn as they are now.

## 0.1.1 - 2026-11-28

For users in Europe: a driver for Siemens S7, German on the device, accented letters, and dates and numbers written the local way.
One fix that matters for safety (first item).

**Security**

- **FINS and SLMP: a `"write_ranges"` without a valid range let every write through.** `write_ranges` limits what the HMI may write
  to the PLC. With a list that named no valid range - a typing mistake in every entry, or an empty list - the log said "nothing can
  be written", and everything was written. Now such a list allows nothing, as it says. If you use `write_ranges`, run
  `hml_runtime <project.json> --check` and look at the log once: a project that relied on the mistake stops writing.

**Changed**

- See above: a project whose `write_ranges` has no valid entry no longer writes to the PLC.
- The "Apply" button in the head of the system screens is as wide as its word; "Refresh" on the USB page is a sign.

**Added**

- Driver **`s7`**: Siemens S7-300 / 400 / 1200 / 1500 over S7comm (ISO-on-TCP, port 102). Addresses as in TIA Portal
  (`DB1.DBW4`, `M0.0`, `IW2`, `QB0`; the German `E` / `A` too), bool / 8 / 16 / 32-bit integers / float / STRING, `write_ranges`,
  the Addresses page. S7-1200 / 1500 need "PUT/GET communication" permitted in the CPU.
  **Checked against a simulated PLC and against the Snap7 server, not yet against a real Siemens PLC.**
- **German** for the system settings screens, the login screen and the keys of the on-screen keyboard (`"system_language": "de"`, or
  a project whose language is `de`), with a font that has ä ö ü ß. The translation has not been read by a native speaker yet.
- The on-screen keyboard has a key "äé" for the letters with accents of Western Europe (ä ö ü ß é è ñ ç å ø …, capitals, €), when
  the font of the project has them.
- `"locale": { "date": "dmy" | "mdy", "decimal": "," }` in `project.json`: how dates and decimal numbers are written on the
  screen and typed (28.11.2026, 12,5). What is stored and exported keeps one form. In the designer: Project tab > "Dates and numbers".
- Every package of a release has an attestation of where it was built (`gh attestation verify <file> --repo bamchoh/tsukuraku`).
- The Windows programs carry their product name and version (Properties > Details).
- `CHANGELOG.md` and `docs/` (the quick start) are in the packages.

**Known limits**

- As 0.1.0: the programs are not signed; no driver for EtherNet/IP.
- With `locale`: the rollers of `datetime_input` stay in the order year, month, day; there is no 12-hour clock.
- The keyboard cannot type the letters of Central and Eastern Europe, Cyrillic or Greek.

## 0.1.0 - 2026-11-28

The first public version. Tsukuraku is work in progress (0.x): it works, and it still changes.

**What it is**

- **Designer** (Windows): screens, tags, drivers, scripts, logs, users, recipes and texts of a project; preview and run with the
  real runtime; a problem list; deploy to a device.
- **Runtime**: Windows, Linux x64 and Raspberry Pi (aarch64, framebuffer). The same project runs on all of them.
- **An AI makes the screens**: a project is text, the runtime checks it (`--check`) and draws it to a picture (`--render`), and the
  skill (`skills/hml-screens`, English) tells an assistant how. The designer's **AI** button hands it over.
- **Drivers**: Modbus TCP / RTU / ASCII, SLMP (Mitsubishi MELSEC), FINS (OMRON), OPC UA client and server, MQTT; a simulation.
- **Widgets**: labels, buttons, switches, sliders, meters, charts (trend, bar, XY), tables, alarm list and summary, recipe table,
  log view, date and time input, images, popups, templates, animations.
- **On the device**: alarms, data log, operation log, system log (SQLite, CSV export), recipes, schedules, sound, mail, log upload,
  data transfer between PLCs, Lua scripts, users with levels and PINs, several languages, a screen saver, remote view (VNC).
- **System settings on the device** (hold the top left corner): network, display, users, drivers, tags, addresses of a PLC,
  deploy, update from a USB memory. In English or Japanese, dark or light after the project.
- **Deploy**: over SSH, or directly over TLS with fingerprints on both sides and a question on the device's screen
  (also for a new runtime). `--capabilities` says what a project may do before it is run.
- **Fonts** for Japanese and for the languages of Europe come with the designer.

**Security**

Three problems were found in a review and fixed before this first release; no released version has them. They are published as
advisories for those who built from source before 2026-11-26 (commit 62296eb):

- The designer read and wrote files outside the project through paths in `screens` of a project made by somebody else.
- A project could open remote access (VNC) to the device on the network with a password known to its author.
- A Lua script could use up the memory of the device.

**Known limits**

- The programs are not signed: Windows warns when the designer is started for the first time.
- The system screens and the designer are in English and Japanese only.
- No drivers yet for Siemens S7 and EtherNet/IP.
