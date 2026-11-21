// A table of texts and tag values: rows and columns as the screen file says, the cells that
// name a tag follow it.
//
//   { "type": "table", "w": "100%", "h": 200,
//     "columns": [ "Machine", { "title": "Count", "width": 90, "align": "right" }, { "title": "State", "align": "center" } ],
//     "rows": [ [ "Loader",  { "tag": "m1_count" }, { "tag": "m1_state", "map": { "0": "Stop", "1": "Run", "default": "?" } } ],
//               [ "Press",   { "tag": "m2_count", "format": "%d pcs" }, { "tag": "m2_state", "map": { "0": "Stop", "1": "Run" } } ] ],
//     "header": true }               // false: no row of titles
//
// A column: its title, or { "title", "width" (pixels; the others share the rest), "align"
// ("left" | "center" | "right") }.  A cell: a text, a number, or
//   { "tag": "name", "format": "%.1f", "map": { "<value>": "text", "default": "text" } }
// ("map" gives a text for a value: the key is compared as a number when it is one, else as the
// text of the value). Texts may be "@ID" of the project's texts.
//
// The table only shows. It scrolls when it has more rows than fit; up to 2000 rows and 32
// columns. Cells are looked at four times a second while the screen is shown and redrawn only
// when their text changed.
#pragma once
#include <string>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_table_view(lv_obj_t* parent, const json& w);
// (tests) rows and columns without the titles; the text of a cell
int table_view_rows(lv_obj_t* table);
int table_view_columns(lv_obj_t* table);
std::string table_view_cell(lv_obj_t* table, int row, int column);     // row -1 = the titles

} // namespace hml
