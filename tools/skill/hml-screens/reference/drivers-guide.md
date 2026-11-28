# Connecting to equipment (driver settings)

The steps for writing the connection to a PLC or a broker in `drivers` of `project.json`. The list of keys is `drivers.md` (generated from the designer's definitions).
Do this **only when asked**. A wrong address means writing to the wrong place in the real machine. For a demo that only moves values, use
`simulation` (`project-features.md`), not a driver.

## Rules

- **Do not guess**: take the destination (IP address, port, model, station number / unit number, broker, endpoint) and the addresses from the request or from the
  address table the user handed over (CSV, spreadsheet, the PLC's label list). If there is none, do not make them up; ask. Do not write "probably D100".
- **Start read-only**: newly made rows are `"access": "ro"`. Rows that write (setpoints, run commands) are `rw` only for those the user approved for writing
  (`wo` for those that are not read back, such as a command that is ON only while pressed). For SLMP and FINS, put only the ranges that may be written into `write_ranges`
  (`["D100-D199", "M0-M15"]`). Do not write the system areas (SM / SD, A of FINS) (do not add `allow_system_write`).
- **Do not write passwords or keys**: up to the user name may be written. The password is entered on the device's System screen (`--check` tells you).
  Leave encryption at the default (MQTT is `tls: true`, `verify: true`; OPC UA is `security: "sign_encrypt"`). Turn encryption off only when asked, and write it in the report.
- **In existing `drivers`, change only what was asked** (do not reorder, delete or rename other rows).
- **Do not try the connection**: the AI does not connect to the real machine (running the runtime without `--check` makes the drivers connect to the real machine).
  The user confirms the connection. Write in the report what you want checked.

## Steps

1. **Read the address table**. 1 row = 1 value (name, address, data type, unit, read / write). If there is a column you do not understand, ask.
2. **Make the tags** (`tags` in `project.json`). Names are lower-case letters and `_` (made from the names in the table. If there is an existing tag, use it). Types:

   | Data type in the table | `type` of the tag | `dtype` of the row |
   | --- | --- | --- |
   | Bit (X, Y, M, coil, contact) | `bool` | none (a bit device / `coil` / `discrete`) |
   | 16-bit integer (signed / unsigned) | `int` | `int16` / `uint16` (for Modbus, `s16` / `u16` also work) |
   | 32-bit integer | `int` | `int32` / `uint32` (uses 2 words) |
   | Real (32-bit floating point) | `float` | `float` (uses 2 words) |
   | Integer with a decimal point (in units of 0.1 °C and the like) | `float` | `int16` + `"scale": 0.1` |
   | String | `string` | `string` + `"length"` (number of characters) |

   Give the tags of values that are written `min` / `max` (writes from the screen are clamped to the range).
   **When using an existing tag**: match the type to the address (if it is an int tag on a real address, change it to float and report it. `--check` tells you).
   If the range in the table and the tag's `min` / `max` differ, take the narrower one and ask which is right.
3. **Add one driver** (`type` and keys from `drivers.md`). `map` has 1 row per tag. Make a bool tag (such as `plc_online`:
   true while connected) and put it in `status_tag`.
4. **Tell when communication is lost**: in `alarms`, `{ "name": "plc_comm", "tag": "plc_online", "condition": "false", "severity": "critical", "message": "PLC communication lost" }`.
   **If there is already a communication-fault tag and alarm, use them** (do not add a new alarm. If the tag is the other way round, use a script: `tag.plc_comm_error = not tag.plc_online`).
   Show the communication status in the header (the template). Having been asked for the connection is a reason that allows adding this indication to the template (write it in the report).
5. **Take out the made-up values used until now** (`--check` finds them):
   - `simulation` items that move tags read from the PLC: **delete** them (even if stopped with `when`, `follow` keeps writing `idle` while stopped).
     Items for values that do not come from the PLC may stay (meanwhile `sys.sim.active` stays 1: the SIMULATION indication is correct). If every value comes from the PLC,
     delete the whole `simulation`.
   - If a script puts values into tags that are only read from the PLC, delete those lines (calculation of totals, made-up status). Calculate status (line status and the like)
     from the PLC's bits if there are any.
   - Places on a screen that show "planned addresses" and the like (an I/O monitor): fix them to match the address table (write the rows you deleted in the report).
6. **`--check`**: it finds how addresses are written (the driver itself reads them: `runtime: slmp: tag 'x': "Q5": unknown device ...`), 2 rows with the same tag or the same address,
   a mismatch between the tag's type and the address (string and number, real and int, bit and number), the range of Modbus numbers, and simulation or
   scripts that move values read from the PLC. Fix until `no problems`.
7. **Report**: the list of tags and rows added (tag, address, `dtype`, `ro` / `rw`), the rows where writing was allowed and `write_ranges`, what you want to ask,
   what the user is to check (the connection, comparing the values of a few rows with the PLC's display, trying the writing rows one at a time).

## Commands only while pressed (run, reset)

Write true on the button's `on_press` and false on `on_release` (`"access": "wo"`, or `rw` if the PLC reads it back). `on_release` comes even when the finger slides outside and is then lifted
(it does not forget to stop). For an operation with a confirmation popup, put these two on the popup's "Execute" button.
Have the PLC side act on the rising edge (say so in the report).

## Strings

`"dtype": "string"` of SLMP / FINS is 1 byte per character (ASCII. 2 characters in 1 word: for SLMP the low byte comes first, for FINS the high byte comes first); `length` is the number of characters. Japanese (Shift-JIS and the like) cannot be read.
Even if the table says "16 characters", ask if they are full-width.

## How to write addresses

- **Modbus** (`modbus_tcp` / `modbus_rtu` / `modbus_ascii`): `area` and `addr` (a number counted from 0). If the table has 5-digit / 6-digit numbers,
  40001 → 0 of `holding`, 30001 → 0 of `input`, 10001 → 0 of `discrete`, 00001 → 0 of `coil`. `coil` / `discrete` are bool tags.
  32 bits uses two in a row (`addr` and `addr + 1`). If the word order is reversed, `swap_words`.
- **SLMP (Mitsubishi MELSEC)**: as written in GX Works: `D100` `M0` `X1F` `Y20` `W1A` `ZR100` (X Y B W SB SW DX DY ZR are hexadecimal). `D10.3` is a bit of a word (read-only).
- **FINS (Omron)**: `D100` `CIO10` `W0.00` `H5` `A0` `E0_100` `T1` `C1` (`.00` to `.15` are bits). BCD values are `bcd16` / `bcd32`.
- **OPC UA server** (`opcua_server`: shows tags to SCADA and the like): signing and encryption by default. Put the certificates of the clients to accept in `client_certs`
  (put the files received from the user into the project. If there are none, ask. With none at all, no client can get in). `"security": "none"` only
  when asked (write it in the report). If login is needed, write only `username` and have the password set on the device. Narrow the tags shown down with `tags`, and
  tags that must not be written are `"access": "ro"`.
- **OPC UA client**: `node` on each row of `map` (a node ID like `ns=2;s=Line1.Temp`). Put the server's certificate in `server_cert`.
- **MQTT**: `publish` (tag → broker) and `subscribe` (broker → tag. Only the tags written here are written from outside). The topic is `topic_prefix` + `topic`.
  - 1 value per topic: `{ "tag": "temp", "topic": "oven/temp" }` (the value as it is, as a string. With `"payload": "json"`, `{"v": 21.5, "ts": ...}`).
  - **JSON decided by the other side** (there is a message specification or a sample): on 1 row, `"topic"` and `"fields": { "<JSON Pointer>": "<tag>" }`.
    publish can also have `const` (fixed values: `{ "/device": "line1" }`) and `ts` (the place to put the time: `"/ts"`). subscribe writes only the fields that are in the message.
    Example: if the specification is `{"device": "line1", "oven": {"temp": 211.5}, "ts": 1763800000000}`, then
    `{ "topic": "line1/status", "fields": { "/oven/temp": "oven_temperature" }, "const": { "/device": "line1" }, "ts": "/ts" }`.
    Inside an array: `/list/0/value`. Match the names and types (number or string) of the specification to the tags, and ask if they do not match.

## Example: from an address table

```
name,address,type,unit,read/write
Oven temperature,D100,real,°C,read
Oven temperature setpoint,D110,real,°C,write
Running,M0,bit,,read
Run command,M10,bit,,write
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

(The IP address and port are those in the request. If there are none, ask.)
