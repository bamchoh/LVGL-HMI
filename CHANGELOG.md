# Changelog

What changed from one release to the next, for the people who use Tsukuraku. The newest is on top.
The rules for the numbers are in the README ("Versions").

Each release lists, where there is something to say:
**Changed** - what behaves differently and may need a look at your projects or devices (always read this part);
**Added**; **Fixed**; **Security**.

## Unreleased

**Added**

- Every package of a release has an attestation of where it was built (`gh attestation verify <file> --repo bamchoh/tsukuraku`).
- `CHANGELOG.md` and `docs/` (the quick start) are in the packages.
- The system settings screens, the login screen and the keys of the on-screen keyboard in **German** (`"system_language": "de"`, or
  a project whose language is `de`), with a font that has ä ö ü ß.
- The Windows programs carry their product name and version (Properties > Details).
- The on-screen keyboard has a key "äé" for the letters with accents of Western Europe (ä ö ü ß é è ñ ç å ø …, capitals, €), when
  the font of the project has them.

- `"locale": { "date": "dmy" | "mdy", "decimal": "," }` in `project.json`: how dates and decimal numbers are written on the
  screen and typed (28.11.2026, 12,5). What is stored and exported keeps one form. In the designer: Project tab > "Dates and numbers".

**Changed**

- The header's "Apply" button of the system screens is as wide as its word; "Refresh" on the USB page is a sign.

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
