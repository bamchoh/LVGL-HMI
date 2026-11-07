#include "screenshot.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include "util.hpp"

namespace hml {

bool save_screenshot_bmp(lv_display_t* disp, const std::string& path)
{
    lv_draw_buf_t* buf = lv_snapshot_take(lv_display_get_screen_active(disp), LV_COLOR_FORMAT_XRGB8888);
    if(!buf) { HML_WARN("screenshot: snapshot failed"); return false; }
    const uint32_t w = buf->header.w, h = buf->header.h;
    const uint32_t row = (w * 3 + 3) & ~3u;
    FILE* f = std::fopen(path.c_str(), "wb");
    if(!f) { HML_WARN("screenshot: cannot write %s", path.c_str()); lv_draw_buf_destroy(buf); return false; }

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
