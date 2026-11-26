#include "fontface.h"
#include "image.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
std::wstring g_dir;

struct Loaded { std::wstring path; int index; std::weak_ptr<fonthint::Font> f; };
std::vector<Loaded> g_loaded;

std::shared_ptr<fonthint::Font> openFile(const std::wstring& path, int index) {
    for (Loaded& l : g_loaded)
        if (l.path == path && l.index == index)
            if (auto p = l.f.lock()) return p;
    auto data = std::make_shared<std::vector<char>>();
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 120L * 1024 * 1024) {
            data->resize((size_t)n);
            if (fread(data->data(), 1, data->size(), f) != data->size()) data->clear();
        }
        fclose(f);
    }
    if (data->size() < 12) return nullptr;
    fonthint::Font* raw = fonthint::open(data->data(), index);
    if (!raw) return nullptr;
    std::shared_ptr<fonthint::Font> sp(raw, [data](fonthint::Font* p) { fonthint::close(p); });   // 파일 내용도 함께 산다
    g_loaded.erase(std::remove_if(g_loaded.begin(), g_loaded.end(), [](const Loaded& l) { return l.f.expired(); }), g_loaded.end());
    g_loaded.push_back({path, index, sp});
    return sp;
}

struct Key {
    const void* f; int size, sub; uint32_t cp;
    bool operator<(const Key& o) const { return std::tie(f, size, sub, cp) < std::tie(o.f, o.size, o.sub, o.cp); }
};
// 글꼴 객체의 주소는 다시 쓰일 수 있으므로 살아 있는지 함께 본다
struct GlyphEntry { std::weak_ptr<fonthint::Font> own; fontgen::Glyph g; };
std::map<Key, GlyphEntry> g_glyphs;
struct RasterEntry { std::weak_ptr<fonthint::Font> own; fontgen::Raster r; };
std::map<Key, RasterEntry> g_rast;

bool hangul(uint32_t cp) { return (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x3131 && cp <= 0x318E); }
std::shared_ptr<fonthint::Font> owner(const FontAsset& f, const fonthint::Font* p) { return p == f.file.get() ? f.file : f.pack; }
}  // namespace

const fonthint::Font* FontAsset::pick(uint32_t cp) const {
    if (file && fonthint::glyphIndex(file.get(), cp)) return file.get();
    if (pack && fonthint::glyphIndex(pack.get(), cp)) return pack.get();
    return nullptr;
}

namespace fontface {

const Bundled kBundled[] = {{"IBMPlexSansKR-Light.ttf", "Plex Sans KR 가늘게"}, {"IBMPlexSansKR-Regular.ttf", "Plex Sans KR 보통"},
                            {"BarlowCondensed-Regular.ttf", "Barlow Condensed"}, {"BarlowCondensed-Medium.ttf", "Barlow Condensed 중간"}, {"ChivoMono-Regular.ttf", "Chivo Mono"}};
const int kBundledCount = (int)(sizeof kBundled / sizeof kBundled[0]);

void setDir(const std::wstring& d) { g_dir = d; }

std::shared_ptr<FontAsset> open(const std::string& key, int index, const std::string& name) {
    auto a = std::make_shared<FontAsset>();
    a->key = key;
    a->index = index;
    a->name = name;
    const std::wstring path = !key.empty() && key[0] == '@' ? g_dir + L"\\" + utf8ToWide(key.substr(1)) : utf8ToWide(key);
    a->file = openFile(path, index);
    if (a->file) a->restricted = fonthint::restricted(a->file.get());
    return a;
}
const std::shared_ptr<const FontAsset>* find(const Document& d, int id) {
    if (id > 0) for (auto& f : d.fonts) if (f->id == id) return &f;
    return nullptr;
}
int add(Document& d, const std::string& key, int index, const std::string& name) {
    int id = 1;
    for (auto& f : d.fonts) {
        if (f->key == key && f->index == index) return f->id;
        id = std::max(id, f->id + 1);
    }
    auto a = open(key, index, name);
    if (!a->file) return 0;
    a->id = id;
    d.fonts.push_back(a);
    return id;
}

bool has(const FontAsset& f, uint32_t cp) { return cp >= 0x20 && cp <= 0xFFFF && f.pick(cp) != nullptr; }

const fontgen::Glyph& glyph(const FontAsset& f, int size, uint32_t cp) {
    static const fontgen::Glyph none;
    const fonthint::Font* p = f.pick(cp);
    if (!p) return none;
    const Key k{p, size, 0, cp};
    auto it = g_glyphs.find(k);
    if (it != g_glyphs.end() && !it->second.own.expired()) return it->second.g;
    if (g_glyphs.size() > 8000) g_glyphs.clear();
    fontgen::Glyph g;
    g.cp = cp;
    const int gi = fonthint::glyphIndex(p, cp);
    const float sc = fonthint::scaleForEm(p, (float)size);
    fonthint::Bitmap bm;
    // 한글은 가로세로 모두, 라틴은 세로만 격자에 맞춘다(자간이 들쭉날쭉해지지 않게)
    fonthint::raster(p, gi, sc, size > 22 ? 0 : hangul(cp) ? fonthint::ALIGN_Y | fonthint::ALIGN_X : fonthint::ALIGN_Y, 0.f, 0.f, bm);
    g.adv16 = std::clamp((int)lroundf(fonthint::advance(p, gi) * sc * 16.f), 0, 4095);
    g.w = std::clamp(bm.w, 0, 255);
    g.h = std::clamp(bm.h, 0, 255);
    g.ox = std::clamp(bm.ox, -128, 127);
    g.oy = std::clamp(-(bm.oy + bm.h), -128, 127);
    if (bm.w > 0 && bm.h > 0 && bm.w <= 255 && bm.h <= 255) {
        g.bits.assign(((size_t)g.w * g.h + 1) / 2, 0);
        for (size_t i = 0; i < bm.a.size(); i++) {
            const int q = (int)(powf(bm.a[i] / 255.f, 0.85f) * 15.f + 0.5f);
            g.bits[i / 2] |= (uint8_t)((i & 1) ? q : q << 4);
        }
    } else g.w = g.h = 0;
    GlyphEntry& e = g_glyphs[k];
    e.own = owner(f, p);
    e.g = std::move(g);
    return e.g;
}

void metrics(const FontAsset& f, int size, int& lineHeight, int& baseLine) {
    const fonthint::Font* p = f.file ? f.file.get() : f.pack.get();
    lineHeight = size;
    baseLine = 0;
    if (!p) return;
    int a, d, g;
    fonthint::vmetrics(p, a, d, g);
    const float sc = fonthint::scaleForEm(p, (float)size);
    const int up = std::clamp((int)ceilf(a * sc), 1, 400), down = std::clamp((int)ceilf(-d * sc), 0, 200);
    lineHeight = up + down;
    baseLine = down;
}

const fontgen::Raster& raster(const FontAsset& f, int devSize, float k, uint32_t cp, int subX) {
    static const fontgen::Raster none;
    const fonthint::Font* p = f.pick(cp);
    if (!p) return none;
    const float size = devSize * k;
    const Key key{p, (int)(size * 64.f + 0.5f), subX & 3, cp};
    auto it = g_rast.find(key);
    if (it != g_rast.end() && !it->second.own.expired()) return it->second.r;
    if (g_rast.size() > 6000) g_rast.clear();
    fonthint::Bitmap bm;
    fonthint::raster(p, fonthint::glyphIndex(p, cp), fonthint::scaleForEm(p, size), 0, 0.f, subX * 0.25f, bm);
    RasterEntry& e = g_rast[key];
    e.own = owner(f, p);
    e.r.w = bm.w; e.r.h = bm.h; e.r.ox = bm.ox; e.r.oy = bm.oy;
    e.r.a.swap(bm.a);
    for (uint8_t& v : e.r.a) v = (uint8_t)(powf(v / 255.f, 0.85f) * 255.f + 0.5f);
    return e.r;
}
void clearRaster() { g_rast.clear(); }

std::string packLines(const FontAsset& f, const std::vector<uint32_t>& cps) {
    std::string o;
    char b[64];
    for (uint32_t cp : cps) {
        const fonthint::Font* p = f.pick(cp);
        if (!p) continue;
        const int gi = fonthint::glyphIndex(p, cp);
        const std::vector<int16_t> v = fonthint::outline(p, gi);
        std::vector<uint8_t> by(v.size() * 2);
        for (size_t i = 0; i < v.size(); i++) { by[i * 2] = (uint8_t)v[i]; by[i * 2 + 1] = (uint8_t)((uint16_t)v[i] >> 8); }
        snprintf(b, sizeof b, "fg\t%X\t%d\t", cp, fonthint::advance(p, gi));
        o += b + image::base64(by) + "\n";
    }
    return o;
}
bool unpackLine(FontAsset& f, const std::string& cpHex, const std::string& adv, const std::string& data) {
    if (!f.pack) return false;
    const std::vector<uint8_t> by = image::unbase64(data);
    std::vector<int16_t> v(by.size() / 2);
    for (size_t i = 0; i < v.size(); i++) v[i] = (int16_t)(by[i * 2] | by[i * 2 + 1] << 8);
    return fonthint::packAdd(f.pack.get(), (uint32_t)strtoul(cpHex.c_str(), nullptr, 16), atoi(adv.c_str()), v);
}

const std::vector<Installed>& installed() {
    static std::vector<Installed> list;
    static bool done = false;
    if (done) return list;
    done = true;
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        HKEY k;
        if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; i++) {
            wchar_t name[512], val[1024];
            DWORD nl = 512, vl = sizeof val - 2, type = 0;
            const LSTATUS r = RegEnumValueW(k, i, name, &nl, nullptr, &type, (BYTE*)val, &vl);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS || type != REG_SZ) continue;
            val[vl / 2] = 0;
            std::wstring path = val, nm = name;
            if (path.find(L'\\') == std::wstring::npos) path = std::wstring(win) + L"\\Fonts\\" + path;
            std::wstring ext = path.size() > 4 ? path.substr(path.size() - 4) : L"";
            for (wchar_t& c : ext) c = towlower(c);
            if (ext != L".ttf" && ext != L".otf" && ext != L".ttc") continue;
            const size_t par = nm.rfind(L" (");
            if (par != std::wstring::npos) nm = nm.substr(0, par);
            // TTC에는 여러 글꼴이 & 로 이어져 있다
            int idx = 0;
            size_t st = 0;
            while (st <= nm.size()) {
                size_t e = ext == L".ttc" ? nm.find(L" & ", st) : std::wstring::npos;
                if (e == std::wstring::npos) e = nm.size();
                list.push_back({wideToUtf8(nm.substr(st, e - st)), path, idx++});
                if (e == nm.size()) break;
                st = e + 3;
            }
        }
        RegCloseKey(k);
    }
    std::sort(list.begin(), list.end(), [](const Installed& a, const Installed& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    return list;
}

bool sample(const Installed& fi, const std::string& text, int w, int h, float em, std::vector<uint8_t>& out, bool& hg, std::string& koName) {
    out.assign((size_t)w * h, 0);
    hg = false;
    koName.clear();
    auto f = openFile(fi.path, fi.index);
    if (!f) return false;
    hg = fonthint::glyphIndex(f.get(), 0xAC00) != 0;
    koName = wideToUtf8(fonthint::localName(f.get()));
    const float sc = fonthint::scaleForEm(f.get(), em);
    int a, d, g;
    fonthint::vmetrics(f.get(), a, d, g);
    const float base = floorf((h - (a - d) * sc) * 0.5f + a * sc + 0.5f);
    float x = 0;
    for (uint32_t cp : fontgen::decode(text)) {
        const int gi = fonthint::glyphIndex(f.get(), cp);
        if (!gi && cp != ' ') continue;
        fonthint::Bitmap bm;
        fonthint::raster(f.get(), gi, sc, 0, 0.f, x - floorf(x), bm);
        for (int yy = 0; yy < bm.h; yy++)
            for (int xx = 0; xx < bm.w; xx++) {
                const int px = (int)floorf(x) + bm.ox + xx, py = (int)base + bm.oy + yy;
                if (px >= 0 && py >= 0 && px < w && py < h) out[(size_t)py * w + px] = std::max(out[(size_t)py * w + px], bm.a[(size_t)yy * bm.w + xx]);
            }
        x += fonthint::advance(f.get(), gi) * sc;
        if (x > w) break;
    }
    return true;
}

}  // namespace fontface
