#include "lvgl_fixture.hpp"
#include "ui.hpp"

namespace hmltest {

namespace {

constexpr int32_t kW = 320, kH = 240;
alignas(64) uint8_t s_buf[kW * 40 * 4];      // partial render buffer, 40 lines of XRGB8888

Lvgl s_inst;                                   // plain static: safe to touch from LVGL callbacks during init
bool s_ready = false;

uint32_t tick_cb() { return s_inst.tick; }

void flush_cb(lv_display_t* disp, const lv_area_t*, uint8_t*)
{
    s_inst.flushes++;
    lv_display_flush_ready(disp);
}

} // namespace

Lvgl& Lvgl::get()
{
    Lvgl& inst = s_inst;
    if(!s_ready) {
        s_ready = true;                        // set first: lv_init / lv_display_create already call the tick callback
        lv_init();
        lv_tick_set_cb(tick_cb);
        inst.disp = lv_display_create(kW, kH);
        lv_display_set_buffers(inst.disp, s_buf, nullptr, sizeof(s_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(inst.disp, flush_cb);
    }
    return inst;
}

void Lvgl::advance(uint32_t ms, uint32_t step)
{
    if(step == 0) step = 1;
    for(uint32_t t = 0; t < ms; t += step) {
        tick += (ms - t < step) ? ms - t : step;
        lv_timer_handler();
        hml::tags().dispatch(false);
    }
}

void Lvgl::render()
{
    lv_refr_now(disp);
}

Runtime::Runtime()
{
    Lvgl::get();
    hml::tags().clear();
    hml::ui().init(nullptr, hml::AppCallbacks{ [this] { reload_requests++; }, [this] { exit_requests++; },
                                               [this](hml::RestartTarget t) { restart_requests++; last_restart = t; } });
    drivers.register_builtins();
}

Runtime::~Runtime()
{
    drivers.stop_all();
    hml::drivers().stop_all();
    hml::ui().unload_all();
    hml::tags().clear();
}

hml::Tag* Runtime::tag(const std::string& name, hml::TagType type, double init)
{
    hml::Value v = hml::Value::number(init, type);
    return hml::tags().create(name, type, &v, false, 0, 0);
}

hml::Tag* Runtime::tag(const std::string& name, hml::TagType type, double init, double min, double max)
{
    hml::Value v = hml::Value::number(init, type);
    return hml::tags().create(name, type, &v, true, min, max);
}

hml::Tag* Runtime::tag(const std::string& name, const std::string& init)
{
    hml::Value v = hml::Value::string(init);
    return hml::tags().create(name, hml::TagType::String, &v, false, 0, 0);
}

} // namespace hmltest
