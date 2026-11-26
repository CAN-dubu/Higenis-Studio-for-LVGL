// 글꼴 생성: TTF → LVGL fmt_txt 4bpp 글리프
#include "fontgen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "fontface.h"
#include "fonthint.h"

namespace {
std::vector<char> g_ttf;
fonthint::Font* g_font = nullptr;
bool g_ok = false;
std::map<std::pair<int, uint32_t>, fontgen::Glyph> g_cache;

struct View { std::vector<char> ttf; fonthint::Font* f = nullptr; } g_view[2];   // Montserrat, 기호
std::map<std::pair<uint64_t, uint32_t>, fontgen::Raster> g_rast;

bool readFile(const std::wstring& path, std::vector<char>& out) {
    out.clear();
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0) {
            out.resize((size_t)n);
            if (fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
        }
        fclose(f);
    }
    return !out.empty();
}
}  // namespace

namespace fontgen {

bool load(const std::wstring& path) {
    g_ok = false;
    readFile(path, g_ttf);
    if (g_ttf.empty()) return false;
    fonthint::close(g_font);
    g_font = fonthint::open(g_ttf.data());
    g_ok = g_font != nullptr;
    return g_ok;
}

std::vector<char>& data() { return g_ttf; }

// LVGL의 희소 표는 16비트라 기본 다국어 평면까지만 담는다
bool has(uint32_t cp) { return g_ok && cp >= 0x20 && cp <= 0xFFFF && fonthint::glyphIndex(g_font, cp) != 0; }

const Glyph& glyph(int size, uint32_t cp) {
    auto key = std::make_pair(size, cp);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    Glyph g;
    g.cp = cp;
    if (has(cp)) {
        const int gi = fonthint::glyphIndex(g_font, cp);
        const float sc = fonthint::scaleForEm(g_font, (float)size);
        // 작은 글자는 획을 픽셀에 맞춘다. Montserrat Medium과 굵기가 맞도록 조금 굵게
        fonthint::Bitmap bm;
        fonthint::raster(g_font, gi, sc, size <= 22 ? fonthint::ALIGN_Y | fonthint::ALIGN_X : 0, 0.25f, 0.f, bm);
        g.adv16 = std::clamp((int)lroundf(fonthint::advance(g_font, gi) * sc * 16.f), 0, 4095);
        g.w = std::clamp(bm.w, 0, 255);
        g.h = std::clamp(bm.h, 0, 255);
        g.ox = std::clamp(bm.ox, -128, 127);
        g.oy = std::clamp(-(bm.oy + bm.h), -128, 127);
        if (g.w > 0 && g.h > 0) {
            g.bits.assign(((size_t)g.w * g.h + 1) / 2, 0);
            for (size_t i = 0; i < bm.a.size(); i++) {
                int q = (int)(powf(bm.a[i] / 255.f, 0.75f) * 15.f + 0.5f);
                g.bits[i / 2] |= (uint8_t)((i & 1) ? q : q << 4);   // 앞 픽셀이 윗 니블
            }
        }
    }
    return g_cache.emplace(key, std::move(g)).first->second;
}

void metrics(int size, int& lineHeight, int& baseLine) {
    lineHeight = size;
    baseLine = 0;
    if (!g_ok) return;
    int as = 0, de = 0, lg = 0;
    fonthint::vmetrics(g_font, as, de, lg);
    const float sc = fonthint::scaleForEm(g_font, (float)size);
    const int up = (int)ceilf(as * sc), down = (int)ceilf(-de * sc);
    lineHeight = up + down;
    baseLine = down;
}

std::vector<uint32_t> decode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) { i++; continue; }   // 깨진 바이트는 건너뛴다
        uint32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
        bool ok = true;
        for (int k = 1; k < n; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { i++; continue; }
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string encode(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
    else { o += (char)(0xF0 | cp >> 18); o += (char)(0x80 | (cp >> 12 & 63)); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
    return o;
}

bool loadView(const std::wstring& montserrat, const std::wstring& symbols) {
    const std::wstring* p[2] = {&montserrat, &symbols};
    bool all = true;
    for (int i = 0; i < 2; i++) {
        View& v = g_view[i];
        fonthint::close(v.f);
        v.f = readFile(*p[i], v.ttf) ? fonthint::open(v.ttf.data()) : nullptr;
        all = all && v.f;
    }
    return all;
}
void clearRaster() { g_rast.clear(); fontface::clearRaster(); }
const Raster& raster(bool kr, int devSize, float scale, uint32_t cp, int subX) {
    const float size = devSize * scale;
    const uint64_t k = (uint64_t)(size * 64.f + 0.5f) << 8 | (uint64_t)(subX & 3) << 1 | (kr ? 1 : 0);
    auto key = std::make_pair(k, cp);
    auto it = g_rast.find(key);
    if (it != g_rast.end()) return it->second;
    if (g_rast.size() > 6000) g_rast.clear();
    Raster r;
    const bool sym = !kr && cp >= 0xE000;
    const fonthint::Font* f = kr ? g_font : g_view[sym ? 1 : 0].f;
    const int gi = f ? fonthint::glyphIndex(f, cp) : 0;
    if (gi) {
        fonthint::Bitmap bm;
        const bool thick = kr && devSize <= 22;   // 장치에서 획을 정수 픽셀로 맞춘 크기
        fonthint::raster(f, gi, fonthint::scaleForEm(f, size), thick ? fonthint::ALIGN_Y | fonthint::ALIGN_X : 0, 0.25f, subX * 0.25f, bm, thick ? scale : 0.f);
        r.w = bm.w; r.h = bm.h; r.ox = bm.ox; r.oy = bm.oy;
        r.a.swap(bm.a);
        if (kr) for (uint8_t& v : r.a) v = (uint8_t)(powf(v / 255.f, 0.85f) * 255.f + 0.5f);
    }
    return g_rast.emplace(key, std::move(r)).first->second;
}

static bool extra(uint32_t cp) { return cp >= 0x20 && !lvhost::baseHas(cp) && has(cp); }

int countExtra(const std::string& s) {
    std::set<uint32_t> u;
    for (uint32_t cp : decode(s)) if (extra(cp)) u.insert(cp);
    return (int)u.size();
}

// 이 위젯의 글자가 그려질 수 있는 크기: 자기 상태별 지정 + (기본 상태에 지정이 없으면) 부모에게서 물려받는 것
static void effectiveSizes(const Screen& s, const Widget& w, std::set<int>& out, bool& base) {
    const Widget* p = &w;
    while (p) {
        for (int st = 0; st < ST_COUNT; st++)
            if (p->style[st][SP_TEXT_FONT].set) out.insert(p->style[st][SP_TEXT_FONT].v);
        if (p->style[ST_DEFAULT][SP_TEXT_FONT].set) return;
        p = p->parent ? findWidget(s, p->parent) : nullptr;
    }
    base = true;
    out.insert(14);
}

static void effectiveFaces(const Screen& s, const Widget& w, std::set<int>& out) {
    const Widget* p = &w;
    while (p) {
        for (int st = 0; st < ST_COUNT; st++) if (p->style[st][SP_FACE].set) out.insert(p->style[st][SP_FACE].v);
        if (p->style[ST_DEFAULT][SP_FACE].set) return;
        p = p->parent ? findWidget(s, p->parent) : nullptr;
    }
    out.insert(0);
}

int faceBytes(const Document& d, int face, int size, const std::vector<uint32_t>& cps) {
    const auto* f = fontface::find(d, face);
    if (!f) return 0;
    size_t n = 8;
    for (uint32_t cp : cps) n += fontface::glyph(**f, size, cp).bits.size() + 8 + 2;
    return (int)n;
}

Plan plan(const Document& d) {
    Plan pl;
    pl.base.assign(d.screens.size(), false);
    if (!g_ok) return pl;
    std::map<int, std::set<uint32_t>> sets;
    std::map<std::pair<int, int>, std::set<uint32_t>> faceSets;
    for (size_t i = 0; i < d.screens.size(); i++) {
        for (const Widget& w : d.screens[i].widgets) {
            std::set<uint32_t> all;
            for (const std::string* t : renderedTexts(w))
                for (uint32_t cp : decode(*t)) if (cp >= 0x20) all.insert(cp);
            if (renderedTexts(w).empty()) continue;
            std::set<int> sizes, faces;
            bool base = false;
            effectiveSizes(d.screens[i], w, sizes, base);
            effectiveFaces(d.screens[i], w, faces);
            for (int fc : faces) {
                const auto* f = fontface::find(d, fc);
                std::set<uint32_t> need;
                for (uint32_t cp : all) if (extra(cp) && !(f && fontface::has(**f, cp))) need.insert(cp);
                if (!need.empty()) {
                    if (base) pl.base[i] = true;
                    for (int sz : sizes) sets[sz].insert(need.begin(), need.end());
                }
                if (!f) continue;
                for (int sz : sizes) {
                    auto& fs = faceSets[{fc, sz}];
                    for (uint32_t cp : all) if (fontface::has(**f, cp)) fs.insert(cp);
                    // 실행 중에 숫자만 바뀌는 글자가 많으므로 숫자와 부호는 늘 담는다
                    for (const char* c = " 0123456789.,:%+-/()"; *c; c++) if (fontface::has(**f, (uint32_t)*c)) fs.insert((uint32_t)*c);
                }
            }
        }
    }
    for (auto& [sz, cps] : sets) pl.sizes[sz].assign(cps.begin(), cps.end());
    for (auto& [k, cps] : faceSets) if (!cps.empty()) pl.faces[k].assign(cps.begin(), cps.end());
    return pl;
}

int Plan::bytes(int size) const {
    auto it = sizes.find(size);
    if (it == sizes.end()) return 0;
    size_t n = 8;                                        // 예약 글리프
    for (uint32_t cp : it->second) n += glyph(size, cp).bits.size() + 8 + 2;   // 비트맵 + 글리프 설명 + 코드표
    return (int)n;
}

}  // namespace fontgen
