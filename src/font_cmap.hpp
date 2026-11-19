// What a TTF / OTF file has: its characters (the "cmap" table, formats 4 and 12) and their
// advance widths ("head", "hhea", "hmtx").
//
// Why the runtime reads this itself instead of trusting TinyTTF (LVGL 9.4):
//  - TinyTTF must only be asked for characters the font has: LVGL frees a cache entry twice when
//    a glyph is missing (lv_cache_acquire_or_create -> remove_cb + lv_cache_entry_delete), which
//    corrupts the heap. With has(), a missing character goes straight to the fallback font.
//  - TinyTTF's kerning cache uses a freed entry on eviction (found with AddressSanitizer) and,
//    even with kerning off, gives some glyphs the advance of another one ("oper ator"). The
//    fonts are made without kerning and the advance comes from advance_px().
#pragma once
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace hml {

class FontCmap {
public:
    // Read a font file. False (and *err) if it is not a font or has no usable cmap.
    bool load(const std::string& path, std::string* err = nullptr);
    // the same from bytes (tests)
    bool parse(const uint8_t* data, size_t size, std::string* err = nullptr);
    bool has(uint32_t cp) const { return glyph(cp) != 0; }
    uint32_t glyph(uint32_t cp) const;    // glyph index, 0 = none
    // The advance of a character at a size (pixels per em), as TinyTTF scales it; -1 if unknown.
    int advance_px(uint32_t cp, int size) const;
    size_t count() const;                 // characters with a glyph

private:
    std::vector<uint16_t> bmp_;           // glyph of U+0000..U+FFFF
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> ranges_;   // above U+FFFF: first, last, glyph of first
    std::vector<uint16_t> advances_;      // per glyph (hmtx; the last one repeats)
    uint32_t units_per_em_ = 0;
};

} // namespace hml
