# legacy_c

C 言語で書かれた旧ランタイム（2026-11-07 に C++17 へ書き直す前のもの）です。
ビルド対象には含まれておらず、参照用に残しているだけです。不要なら削除して構いません。

対応関係:

| 旧 (C) | 新 (C++) |
|---|---|
| `hml_os.[ch]` | `util.[ch]pp`（std::thread / std::mutex / std::chrono を直接使用） |
| `hml_tag.[ch]` | `tag.[ch]pp` |
| `hml_driver.[ch]` + cJSON ヘルパ | `driver.[ch]pp`, `jsonutil.[ch]pp`（nlohmann/json） |
| `hml_driver_sim.c` | `driver_sim.cpp` |
| `hml_driver_modbus.c` | `driver_modbus.cpp` |
| `hml_ui.[ch]` | `ui.[ch]pp` |
| `hml_project.[ch]` | `project.[ch]pp` |
| `hml_platform*.{c,h}` | `platform*.{cpp,hpp}` |
| `main.c` | `main.cpp` |
