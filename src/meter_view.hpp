// A meter with a needle: a round scale with ticks and numbers, coloured zones, a needle that
// shows the value of a tag, and the value as a number below the needle.
//
//   { "type": "meter", "w": 200, "h": 200, "bind": "temp",
//     "min": 0, "max": 100,
//     "angle": 270,               // how far the scale goes round (90 .. 360), open at the bottom
//     "ticks": 21,                // tick marks in all (2 .. 101)
//     "major_every": 5,           // every n-th tick is long and has a number
//     "decimals": 0,              // of the numbers on the scale and of the value (0 .. 3)
//     "unit": "°C",               // after the value
//     "show_value": true,         // the value as a number in the meter
//     "needle_color": "#ff5252",
//     "zones": [ { "from": 80, "to": 100, "color": "#e53935" } ] }   // parts of the scale in a colour
//
// The needle stops at the ends of the scale; the number shows the value as it is (so a value
// outside the scale is still read correctly). A zone tells by its place on the scale what the
// colour means, and the number is always there: nothing depends on the colour alone.
#pragma once
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_meter(lv_obj_t* parent, const json& w);
// What the binding of the widget calls ("bind": "tag" / { "value": ... }).
void meter_set_value(lv_obj_t* meter, double value);
// (tests) the value the needle points at, in units of the scale; the text of the value label
double meter_needle_value(lv_obj_t* meter);
std::string meter_value_text(lv_obj_t* meter);

} // namespace hml
