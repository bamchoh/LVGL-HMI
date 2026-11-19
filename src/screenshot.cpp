#include "screenshot.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include "miniz.h"
#include "util.hpp"

namespace hml {

bool save_screenshot(lv_display_t* disp, const std::string& path)
{
    lv_draw_buf_t* buf = lv_snapshot_take(lv_display_get_screen_active(disp), LV_COLOR_FORMAT_XRGB8888);
    if(!buf) { HML_WARN("screenshot: snapshot failed"); return false; }
    const uint32_t w = buf->header.w, h = buf->header.h;
    // what is on the top layer (dialogs) and the system layer (screen saver) is drawn over the
    // screen, as on the display
    for(lv_obj_t* top : { lv_display_get_layer_top(disp), lv_display_get_layer_sys(disp) }) {
    if(top && lv_obj_get_child_count(top) > 0) {
        if(lv_draw_buf_t* ov = lv_snapshot_take(top, LV_COLOR_FORMAT_ARGB8888)) {
            const uint32_t ow = ov->header.w < w ? ov->header.w : w, oh = ov->header.h < h ? ov->header.h : h;
            for(uint32_t y = 0; y < oh; y++) {
                uint8_t* d = static_cast<uint8_t*>(buf->data) + y * buf->header.stride;
                const uint8_t* o = static_cast<const uint8_t*>(ov->data) + y * ov->header.stride;
                for(uint32_t x = 0; x < ow; x++) {
                    const uint32_t a = o[x * 4 + 3];
                    for(int c = 0; c < 3; c++) d[x * 4 + c] = static_cast<uint8_t>((o[x * 4 + c] * a + d[x * 4 + c] * (255 - a)) / 255);
                }
            }
            lv_draw_buf_destroy(ov);
        }
    }
    }
    const uint32_t row = (w * 3 + 3) & ~3u;
    FILE* f = std::fopen(path.c_str(), "wb");
    if(!f) { HML_WARN("screenshot: cannot write %s", path.c_str()); lv_draw_buf_destroy(buf); return false; }

    if(path.size() >= 4 && iequals(path.substr(path.size() - 4), ".png")) {
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        for(uint32_t y = 0; y < h; y++) {
            const uint8_t* src = static_cast<const uint8_t*>(buf->data) + y * buf->header.stride;
            uint8_t* d = rgb.data() + static_cast<size_t>(y) * w * 3;
            for(uint32_t x = 0; x < w; x++) { d[x * 3] = src[x * 4 + 2]; d[x * 3 + 1] = src[x * 4 + 1]; d[x * 3 + 2] = src[x * 4]; }
        }
        lv_draw_buf_destroy(buf);
        size_t len = 0;
        void* png = tdefl_write_image_to_png_file_in_memory(rgb.data(), static_cast<int>(w), static_cast<int>(h), 3, &len);
        const bool ok = png && std::fwrite(png, 1, len, f) == len;
        if(png) mz_free(png);
        std::fclose(f);
        if(!ok) { HML_WARN("screenshot: cannot write %s", path.c_str()); return false; }
        HML_INFO("screenshot saved to %s", path.c_str());
        return true;
    }

    uint8_t hdr[54] = { 'B', 'M' };
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(hdr + off, &v, 4); };
    put32(2, 54 + row * h); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    hdr[26] = 1; hdr[28] = 24;
    put32(34, row * h);
    std::fwrite(hdr, 1, 54, f);

    std::vector<uint8_t> line(row, 0);
    for(uint32_t y = h; y-- > 0;) {
        const uint8_t* src = static_cast<const uint8_t*>(buf->data) + y * buf->header.stride;
        for(uint32_t x = 0; x < w; x++) { line[x * 3] = src[x * 4]; line[x * 3 + 1] = src[x * 4 + 1]; line[x * 3 + 2] = src[x * 4 + 2]; }
        std::fwrite(line.data(), 1, row, f);
    }
    std::fclose(f);
    lv_draw_buf_destroy(buf);
    HML_INFO("screenshot saved to %s", path.c_str());
    return true;
}

} // namespace hml
