#include "font_cmap.hpp"
#include <algorithm>
#include <cmath>
#include "util.hpp"

namespace hml {
namespace {

// bounded big-endian reads: every offset is checked against the size of the data
struct Reader {
    const uint8_t* d;
    size_t n;
    bool ok = true;
    uint32_t u16(size_t at) { if(at + 2 > n || at + 2 < at) { ok = false; return 0; } return static_cast<uint32_t>(d[at] << 8 | d[at + 1]); }
    uint32_t u32(size_t at) { if(at + 4 > n || at + 4 < at) { ok = false; return 0; } return static_cast<uint32_t>(d[at]) << 24 | static_cast<uint32_t>(d[at + 1]) << 16 | static_cast<uint32_t>(d[at + 2]) << 8 | d[at + 3]; }
};

} // namespace

bool FontCmap::load(const std::string& path, std::string* err)
{
    // (fonts can be large; 64 MB is far more than any font an HMI needs)
    auto data = read_file(path, 64u * 1024 * 1024, err);
    if(!data) return false;
    return parse(reinterpret_cast<const uint8_t*>(data->data()), data->size(), err);
}

bool FontCmap::parse(const uint8_t* data, size_t size, std::string* err)
{
    bmp_.assign(0x10000, 0);
    ranges_.clear();
    advances_.clear();
    units_per_em_ = 0;
    auto fail = [&](const char* msg) { if(err) *err = msg; bmp_.assign(0x10000, 0); ranges_.clear(); advances_.clear(); return false; };
    Reader r{ data, size };
    const uint32_t tag = r.u32(0);
    if(!r.ok || (tag != 0x00010000 && tag != 0x4F54544F /* OTTO */ && tag != 0x74727565 /* true */)) return fail("not a TTF / OTF font");
    const uint32_t ntables = r.u16(4);
    size_t cmap = 0, head = 0, hhea = 0, hmtx = 0;
    for(uint32_t i = 0; i < ntables && r.ok; i++) {
        const size_t rec = 12 + i * 16;
        const uint32_t t = r.u32(rec), off = r.u32(rec + 8);
        if(t == 0x636D6170) cmap = off;                  // cmap
        else if(t == 0x68656164) head = off;             // head
        else if(t == 0x68686561) hhea = off;             // hhea
        else if(t == 0x686D7478) hmtx = off;             // hmtx
    }
    if(!r.ok || cmap == 0 || cmap >= size) return fail("the font has no cmap table");
    // the best subtable: (3,10) / (0,4+) format 12 for everything, else (3,1) / (0,x) format 4
    const uint32_t nsub = r.u16(cmap + 2);
    size_t best12 = 0, best4 = 0;
    for(uint32_t i = 0; i < nsub && r.ok; i++) {
        const size_t rec = cmap + 4 + i * 8;
        const uint32_t plat = r.u16(rec), enc = r.u16(rec + 2);
        const size_t off = cmap + r.u32(rec + 4);
        if(off >= size) continue;
        const uint32_t fmt = r.u16(off);
        const bool unicode = plat == 0 || (plat == 3 && (enc == 1 || enc == 10));
        if(!unicode) continue;
        if(fmt == 12 && !best12) best12 = off;
        if(fmt == 4 && !best4) best4 = off;
    }
    if(!r.ok) return fail("broken cmap table");
    if(best4) {
        const size_t o = best4;
        const uint32_t segx2 = r.u16(o + 6);
        const size_t ends = o + 14, starts = ends + segx2 + 2, deltas = starts + segx2, ranges = deltas + segx2;
        for(uint32_t s = 0; s < segx2 / 2 && r.ok; s++) {
            const uint32_t end = r.u16(ends + s * 2), start = r.u16(starts + s * 2);
            const uint32_t delta = r.u16(deltas + s * 2), roff = r.u16(ranges + s * 2);
            if(start > end) continue;
            for(uint32_t c = start; c <= end && r.ok; c++) {
                uint32_t g;
                if(roff == 0) g = (c + delta) & 0xFFFF;
                else {
                    const size_t at = ranges + s * 2 + roff + (c - start) * 2;
                    g = r.u16(at);
                    if(g) g = (g + delta) & 0xFFFF;
                }
                if(g && c != 0xFFFF) bmp_[c] = static_cast<uint16_t>(g);
            }
        }
        if(!r.ok) return fail("broken cmap format 4");
    }
    if(best12) {
        const size_t o = best12;
        const uint32_t groups = r.u32(o + 12);
        if(groups > (size - std::min(size, o + 16)) / 12) return fail("broken cmap format 12");
        for(uint32_t i = 0; i < groups && r.ok; i++) {
            const uint32_t a = r.u32(o + 16 + i * 12), b = r.u32(o + 20 + i * 12), g = r.u32(o + 24 + i * 12);
            if(a > b || b > 0x10FFFF) continue;
            for(uint32_t c = a; c <= std::min<uint32_t>(b, 0xFFFF); c++) {
                const uint32_t gc = g + (c - a);
                if(gc && gc <= 0xFFFF) bmp_[c] = static_cast<uint16_t>(gc);
            }
            if(b > 0xFFFF) { const uint32_t first = std::max<uint32_t>(a, 0x10000); ranges_.emplace_back(first, b, g + (first - a)); }
        }
        if(!r.ok) return fail("broken cmap format 12");
        std::sort(ranges_.begin(), ranges_.end());
    }
    if(!best4 && !best12) return fail("the font has no Unicode cmap (format 4 or 12)");
    // advances (optional: without them TinyTTF's are used)
    if(head && hhea && hmtx) {
        units_per_em_ = r.u16(head + 18);
        const uint32_t nmetrics = r.u16(hhea + 34);
        if(r.ok && units_per_em_ >= 16 && nmetrics > 0 && hmtx + static_cast<size_t>(nmetrics) * 4 <= size) {
            advances_.resize(nmetrics);
            for(uint32_t i = 0; i < nmetrics; i++) advances_[i] = static_cast<uint16_t>(r.u16(hmtx + i * 4));
        }
        else { units_per_em_ = 0; r.ok = true; }
    }
    return true;
}

uint32_t FontCmap::glyph(uint32_t cp) const
{
    if(cp < 0x10000) return bmp_.empty() ? 0 : bmp_[cp];
    auto it = std::upper_bound(ranges_.begin(), ranges_.end(), std::make_tuple(cp, UINT32_MAX, UINT32_MAX));
    if(it == ranges_.begin()) return 0;
    --it;
    if(cp > std::get<1>(*it)) return 0;
    return std::get<2>(*it) + (cp - std::get<0>(*it));
}

int FontCmap::advance_px(uint32_t cp, int size) const
{
    const uint32_t g = glyph(cp);
    if(!g || advances_.empty() || units_per_em_ == 0) return -1;
    const uint16_t adv = advances_[std::min<size_t>(g, advances_.size() - 1)];
    // as TinyTTF: scale = size / units per em (stbtt_ScaleForMappingEmToPixels), rounded
    return static_cast<int>(static_cast<float>(size) / static_cast<float>(units_per_em_) * adv + 0.5f);
}

size_t FontCmap::count() const
{
    size_t n = static_cast<size_t>(std::count_if(bmp_.begin(), bmp_.end(), [](uint16_t g) { return g != 0; }));
    for(const auto& [a, b, g] : ranges_) n += b - a + 1;
    return n;
}

} // namespace hml
