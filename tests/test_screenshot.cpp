#include <doctest.h>
#include "lvgl_fixture.hpp"
#include "screenshot.hpp"
#include "ui.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace hml;
using hmltest::Runtime;

namespace {

uint32_t le32(const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)); }
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

std::vector<uint8_t> read_all(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("save_screenshot_bmp writes a 24-bit BMP of the active screen")
{
    Runtime rt;
    lv_obj_t* scr = ui().build_screen(json::parse(R"({ "name": "s", "style": { "bg_color": "#ff0000" } })"), "");
    REQUIRE(scr);
    REQUIRE(ui().show_screen("s", "none"));
    hmltest::Lvgl& lv = hmltest::Lvgl::get();
    lv.render();

    std::filesystem::path out = std::filesystem::temp_directory_path() / "hml_test_shot.bmp";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    REQUIRE(save_screenshot_bmp(lv.disp, out.generic_string()));
    std::vector<uint8_t> bmp = read_all(out);
    std::filesystem::remove(out, ec);

    const uint32_t w = static_cast<uint32_t>(lv_display_get_horizontal_resolution(lv.disp));
    const uint32_t h = static_cast<uint32_t>(lv_display_get_vertical_resolution(lv.disp));
    const uint32_t row = (w * 3 + 3) & ~3u;
    REQUIRE(bmp.size() == 54 + row * h);
    CHECK(bmp[0] == 'B');
    CHECK(bmp[1] == 'M');
    CHECK(le32(&bmp[2]) == bmp.size());
    CHECK(le32(&bmp[10]) == 54);              // pixel data offset
    CHECK(le32(&bmp[14]) == 40);              // BITMAPINFOHEADER
    CHECK(le32(&bmp[18]) == w);
    CHECK(le32(&bmp[22]) == h);
    CHECK(le16(&bmp[26]) == 1);               // planes
    CHECK(le16(&bmp[28]) == 24);              // bits per pixel
    CHECK(le32(&bmp[34]) == row * h);

    // every pixel is the red background: BGR = 00 00 FF
    size_t red = 0;
    for(uint32_t y = 0; y < h; y++) {
        const uint8_t* line = &bmp[54 + y * row];
        for(uint32_t x = 0; x < w; x++) if(line[x * 3] == 0x00 && line[x * 3 + 1] == 0x00 && line[x * 3 + 2] == 0xFF) red++;
    }
    CHECK(red == static_cast<size_t>(w) * h);
}

TEST_CASE("save_screenshot_bmp fails cleanly for an unwritable path")
{
    Runtime rt;
    hmltest::Lvgl& lv = hmltest::Lvgl::get();
    std::filesystem::path bad = std::filesystem::temp_directory_path() / "hml_no_such_dir_zzz" / "x.bmp";
    CHECK_FALSE(save_screenshot_bmp(lv.disp, bad.generic_string()));
}
