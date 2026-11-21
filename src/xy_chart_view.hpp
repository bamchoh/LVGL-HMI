// An XY graph: one tag along the bottom, another up the side (a press: force against position;
// a valve: flow against opening). A point is added at an interval or when a value changes, and
// the oldest points are dropped.
//
//   { "type": "xy_chart", "w": 300, "h": 240,
//     "x_tag": "position", "x_min": 0, "x_max": 100,
//     "y_tag": "force",    "y_min": 0, "y_max": 50,
//     "points": 200,              // points kept (2 .. 2000)
//     "sample_ms": 100,           // a point every N ms (20 or more); 0 = when x or y changed
//     "refresh_ms": 250,          // the graph is drawn again at most this often (50 or more)
//     "mode": "line",             // "line" (points joined in the order they came) | "dots"
//     "color": "#42a5f5",
//     "hdiv": 5, "vdiv": 5,       // lines of the grid
//     "decimals": 0,              // of the numbers at the ends of the axes
//     "clear_tag": "cycle_start", // the curve starts anew whenever this tag becomes true / non-zero
//     "series": [ { "x_tag": "position", "y_tag": "force2", "color": "#ef5350" } ] }   // more curves (up to 4 in all)
//
// (The axes are called x_tag / y_tag, x_min ... because "x" and "y" are where the widget is.)
// A value that is no number adds no point. A point outside the ranges is kept as it is and drawn
// at the edge of the graph. The numbers at the ends of the axes are the ranges.
// Points are taken also while another screen is shown: the curve is there when one comes back.
// Drawing costs with the number of points (every line again whenever points were added): on a
// small computer keep "points" in the hundreds, or raise "refresh_ms".
#pragma once
#include <utility>
#include <vector>
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_xy_chart(lv_obj_t* parent, const json& w);
// (tests) the points of a curve, oldest first, in the units of the tags
std::vector<std::pair<double, double>> xy_chart_points(lv_obj_t* chart, int series = 0);
int xy_chart_series(lv_obj_t* chart);
uint32_t xy_chart_draws(lv_obj_t* chart);       // (tests) how often new points were handed to the graph to be drawn

} // namespace hml
