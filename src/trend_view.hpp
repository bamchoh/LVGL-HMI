// The "trend" widget: the data log of a group as a chart over a time window (see trend_view.cpp).
#pragma once
#include "lvgl.h"
#include "jsonutil.hpp"

namespace hml {

lv_obj_t* build_trend(lv_obj_t* parent, const json& w);
// Wait until no trend data is being fetched (tests).
void trend_wait_idle();

} // namespace hml
