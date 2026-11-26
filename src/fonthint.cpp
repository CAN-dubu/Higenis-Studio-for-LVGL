#include "fonthint.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace fonthint {

struct Packed { int adv = 0; std::vector<stbtt_vertex> v; };
struct Font {
    stbtt_fontinfo info = {};
    bool pack = false;
    int packAsc = 0, packDesc = 0;
    std::map<uint32_t, Packed> packed;
    float em = 1000.f;
    float stdX = 0.f, stdY = 0.f;   // 표준 획 굵기(글꼴 단위): 세로획 ㅣ의 폭, 가로획 ㅡ의 높이
};

Font* open(const void* data, int no) {
    if (!data) return nullptr;
    const unsigned char* d = (const unsigned char*)data;
    const int off = stbtt_GetFontOffsetForIndex(d, no);
    if (off < 0) return nullptr;
    Font* f = new Font;
    if (!stbtt_InitFont(&f->info, d, off)) { delete f; return nullptr; }
    f->em = 1.f / stbtt_ScaleForMappingEmToPixels(&f->info, 1.f);
    int x0, y0, x1, y1;
    int gi = stbtt_FindGlyphIndex(&f->info, 0x3163);
    if (gi && stbtt_GetGlyphBox(&f->info, gi, &x0, &y0, &x1, &y1)) f->stdX = (float)(x1 - x0);
    gi = stbtt_FindGlyphIndex(&f->info, 0x3161);
    if (gi && stbtt_GetGlyphBox(&f->info, gi, &x0, &y0, &x1, &y1)) f->stdY = (float)(y1 - y0);
    return f;
}
void close(Font* f) { delete f; }
Font* newPack(float upem, int asc, int desc) {
    Font* f = new Font;
    f->pack = true;
    f->em = upem;
    f->packAsc = asc;
    f->packDesc = desc;
    return f;
}
bool packAdd(Font* f, uint32_t cp, int adv, const std::vector<int16_t>& o) {
    if (!f || !f->pack || o.size() % 7 || o.size() > 7 * 4000) return false;
    Packed p;
    p.adv = adv;
    for (size_t i = 0; i < o.size(); i += 7) {
        stbtt_vertex v = {};
        v.type = (unsigned char)o[i];
        if (v.type < STBTT_vmove || v.type > STBTT_vcubic || (i == 0 && v.type != STBTT_vmove)) return false;
        v.x = o[i + 1]; v.y = o[i + 2]; v.cx = o[i + 3]; v.cy = o[i + 4]; v.cx1 = o[i + 5]; v.cy1 = o[i + 6];
        p.v.push_back(v);
    }
    f->packed[cp] = std::move(p);
    return true;
}
std::vector<int16_t> outline(const Font* f, int g) {
    std::vector<int16_t> o;
    auto put = [&](const stbtt_vertex& v) { for (int x : {(int)v.type, (int)v.x, (int)v.y, (int)v.cx, (int)v.cy, (int)v.cx1, (int)v.cy1}) o.push_back((int16_t)x); };
    if (f->pack) {
        auto it = f->packed.find((uint32_t)g);
        if (it != f->packed.end()) for (auto& v : it->second.v) put(v);
        return o;
    }
    stbtt_vertex* sv = nullptr;
    const int n = stbtt_GetGlyphShape(&f->info, g, &sv);
    for (int i = 0; i < n; i++) put(sv[i]);
    if (sv) stbtt_FreeShape(&f->info, sv);
    return o;
}
float unitsPerEm(const Font* f) { return f->em; }
bool restricted(const Font* f) {
    if (f->pack) return false;
    const stbtt_uint32 t = stbtt__find_table(f->info.data, f->info.fontstart, "OS/2");
    return t && (ttUSHORT(f->info.data + t + 8) & 0xF) == 2;
}
std::wstring localName(const Font* f) {
    if (f->pack) return L"";
    int n = 0;
    const char* s = stbtt_GetFontNameString(&f->info, &n, STBTT_PLATFORM_ID_MICROSOFT, 1, 0x412, 4);
    std::wstring o;
    for (int i = 0; s && i + 1 < n && i < 200; i += 2) o += (wchar_t)((unsigned char)s[i] << 8 | (unsigned char)s[i + 1]);
    return o;
}
int glyphIndex(const Font* f, uint32_t cp) { if (f->pack) return f->packed.count(cp) ? (int)cp : 0; return stbtt_FindGlyphIndex(&f->info, (int)cp); }
int glyphCount(const Font* f) { return f->info.numGlyphs; }
int advance(const Font* f, int g) {
    if (f->pack) { auto it = f->packed.find((uint32_t)g); return it == f->packed.end() ? 0 : it->second.adv; }
    int a = 0, l = 0;
    stbtt_GetGlyphHMetrics(&f->info, g, &a, &l);
    return a;
}
void vmetrics(const Font* f, int& a, int& d, int& g) { if (f->pack) { a = f->packAsc; d = f->packDesc; g = 0; return; } stbtt_GetFontVMetrics(&f->info, &a, &d, &g); }
float scaleForEm(const Font* f, float px) { if (f->pack) return px / f->em; return stbtt_ScaleForMappingEmToPixels(&f->info, px); }
float scaleForHeight(const Font* f, float px) { if (f->pack) return px / std::max(1, f->packAsc - f->packDesc); return stbtt_ScaleForPixelHeight(&f->info, px); }

namespace {
struct Pt { float x, y; };
struct V { Pt p, c, c1; unsigned char type; };
struct Edge { float pos, lo, hi, len; int dir; };
struct Stem { float a, b, lo, hi; float A, B; };   // [a,b] → [A,B]

// 한 방향의 획을 찾아 격자에 맞추고, 그 사이는 고르게 늘리거나 줄인다
// 획을 놓을 자리를 정한다. widths: 획마다의 굵기(픽셀). 안 맞으면 false
bool place(std::vector<Stem>& s, const std::vector<float>& widths, float unit) {
    if (unit > 0.f) {   // 자리는 그대로, 굵기만
        for (size_t i = 0; i < s.size(); i++) {
            const float c = (s[i].a + s[i].b) * 0.5f;
            s[i].A = c - widths[i] * unit * 0.5f;
            s[i].B = c + widths[i] * unit * 0.5f;
        }
        for (size_t i = 0; i + 1 < s.size(); i++)
            if (s[i].B + unit * 0.45f > s[i + 1].A && s[i + 1].a - s[i].b > 0.01f) return false;
        return true;
    }
    for (size_t i = 0; i < s.size(); i++) {
        s[i].A = floorf((s[i].a + s[i].b) * 0.5f - widths[i] * 0.5f + 0.5f);
        s[i].B = s[i].A + widths[i];
    }
    // 획 사이 빈칸은 적어도 1픽셀. 모자라면 아래에서 위로, 다시 위에서 아래로 민다
    auto need = [&](size_t i) { return s[i + 1].a - s[i].b > 0.3f ? 1.f : 0.f; };
    for (size_t i = 0; i + 1 < s.size(); i++) {
        const float g = s[i].B + need(i) - s[i + 1].A;
        if (g > 0) { s[i + 1].A += g; s[i + 1].B += g; }
    }
    const Stem& t = s.back();
    const float over = (t.A + t.B - t.a - t.b) * 0.5f;
    if (over > 0.6f) {
        const float d = floorf(over + 0.5f);
        s.back().A -= d; s.back().B -= d;
        for (size_t i = s.size() - 1; i-- > 0;) {
            const float g = s[i].B + need(i) - s[i + 1].A;
            if (g > 0) { s[i].A -= g; s[i].B -= g; }
        }
    }
    for (const Stem& k : s) if (fabsf((k.A + k.B - k.a - k.b) * 0.5f) > 1.6f) return false;   // 너무 비틀린다
    return true;
}

void alignAxis(std::vector<V>& vs, bool yAxis, float em, float grow, float unit, float stdW) {
    auto main = [&](Pt& p) -> float& { return yAxis ? p.y : p.x; };
    auto cross = [&](const Pt& p) { return yAxis ? p.x : p.y; };
    // 윤곽의 도는 방향: 바깥 윤곽이 더 크므로 넓이의 부호로 안다
    double areaSum = 0;
    std::vector<Edge> es;
    Pt last = {0, 0}, start = {0, 0};
    auto seg = [&](Pt a, Pt b) {
        areaSum += (double)a.x * b.y - (double)b.x * a.y;
        const float dm = main(b) - main(a), dc = cross(b) - cross(a), L = fabsf(dc);
        if (L < em * 0.045f || fabsf(dm) > L * 0.06f) return;
        es.push_back({(main(a) + main(b)) * 0.5f, std::min(cross(a), cross(b)), std::max(cross(a), cross(b)), L, dc > 0 ? 1 : -1});
    };
    for (size_t i = 0; i < vs.size(); i++) {
        V& v = vs[i];
        if (v.type == STBTT_vmove) { if (i) seg(last, start); start = v.p; }
        else if (v.type == STBTT_vline) seg(last, v.p);
        else if (v.type == STBTT_vcubic) { seg(last, v.c); seg(v.c, v.c1); seg(v.c1, v.p); }
        else { seg(last, v.c); seg(v.c, v.p); }
        last = v.p;
    }
    if (!vs.empty()) seg(last, start);
    if (es.empty()) return;
    // 같은 높이의 조각들을 하나로
    std::sort(es.begin(), es.end(), [](const Edge& a, const Edge& b) { return a.dir != b.dir ? a.dir < b.dir : a.pos < b.pos; });
    std::vector<Edge> m;
    for (const Edge& e : es) {
        if (!m.empty() && m.back().dir == e.dir && e.pos - m.back().pos < em * 0.014f) {
            Edge& k = m.back();
            k.pos = (k.pos * k.len + e.pos * e.len) / (k.len + e.len);
            k.len += e.len;
            k.lo = std::min(k.lo, e.lo);
            k.hi = std::max(k.hi, e.hi);
        } else m.push_back(e);
    }
    // 시계 방향(TrueType)이면 위쪽 변은 왼→오른쪽. 세로 획(x축)은 왼쪽 변이 위로 간다
    const bool cw = areaSum < 0;
    int topDir = cw ? 1 : -1;            // 가로획의 위쪽 변
    if (!yAxis) topDir = cw ? -1 : 1;    // 세로획의 오른쪽 변은 아래로
    std::vector<Stem> st;
    for (const Edge& lo : m) {
        if (lo.dir == topDir) continue;
        const Edge* best = nullptr;
        for (const Edge& hi : m)
            if (hi.dir == topDir && hi.pos > lo.pos && hi.pos - lo.pos < em * 0.17f && std::min(hi.hi, lo.hi) - std::max(hi.lo, lo.lo) > em * 0.02f)
                if (!best || hi.pos < best->pos) best = &hi;
        if (best) st.push_back({lo.pos, best->pos, std::min(lo.lo, best->lo), std::max(lo.hi, best->hi), 0, 0});
    }
    if (st.empty()) return;
    std::sort(st.begin(), st.end(), [](const Stem& x, const Stem& y) { return x.a < y.a; });
    // 겹치는 획은 먼저 온 것만
    std::vector<Stem> s;
    for (const Stem& k : st) if (s.empty() || k.a >= s.back().b - em * 0.004f) s.push_back(k);
    // 비슷한 획은 모두 같은 굵기로. 빽빽해서 안 들어가면 한 픽셀씩 가늘게
    const float u = unit > 0.f ? unit : 1.f;
    std::vector<float> want(s.size());
    float widest = 1.f;
    for (size_t i = 0; i < s.size(); i++) {
        float w = s[i].b - s[i].a;
        if (stdW > 0 && w > stdW * 0.6f && w < stdW * 1.45f) w = stdW;
        want[i] = std::max(1.f, floorf(w / u + grow + 0.5f));
        widest = std::max(widest, want[i]);
    }
    bool ok = false;
    for (float lim = widest; lim >= 1.f && !ok; lim -= 1.f) {
        std::vector<float> w = want;
        for (float& v : w) v = std::min(v, lim);
        ok = place(s, w, unit);
    }
    if (!ok) return;
    auto map = [&](float v) {
        if (v <= s.front().a) return v + s.front().A - s.front().a;
        if (v >= s.back().b) return v + s.back().B - s.back().b;
        for (size_t i = 0; i < s.size(); i++) {
            if (v <= s[i].b && v >= s[i].a) {
                const float d = s[i].b - s[i].a;
                return d < 1e-4f ? s[i].A : s[i].A + (v - s[i].a) * (s[i].B - s[i].A) / d;
            }
            if (i + 1 < s.size() && v > s[i].b && v < s[i + 1].a) {
                const float d = s[i + 1].a - s[i].b;
                return s[i].B + (v - s[i].b) * (s[i + 1].A - s[i].B) / d;
            }
        }
        return v;
    };
    for (V& v : vs) { main(v.p) = map(main(v.p)); main(v.c) = map(main(v.c)); main(v.c1) = map(main(v.c1)); }
}
}  // namespace

void raster(const Font* f, int glyph, float scale, int axes, float grow, float shiftX, Bitmap& out, float unit) {
    out = Bitmap();
    std::vector<stbtt_vertex> sv;
    if (f->pack) {
        auto it = f->packed.find((uint32_t)glyph);
        if (it != f->packed.end()) sv = it->second.v;
    } else {
        stbtt_vertex* p = nullptr;
        const int cnt = stbtt_GetGlyphShape(&f->info, glyph, &p);
        if (cnt > 0) sv.assign(p, p + cnt);
        if (p) stbtt_FreeShape(&f->info, p);
    }
    const int n = (int)sv.size();
    if (n <= 0) return;
    std::vector<V> vs((size_t)n);
    for (int i = 0; i < n; i++) vs[i] = {{sv[i].x * scale, sv[i].y * scale}, {sv[i].cx * scale, sv[i].cy * scale}, {sv[i].cx1 * scale, sv[i].cy1 * scale}, sv[i].type};
    const float em = f->em * scale;
    if (axes & ALIGN_Y) alignAxis(vs, true, em, grow, unit, f->stdY * scale);
    if (axes & ALIGN_X) alignAxis(vs, false, em, grow, unit, f->stdX * scale);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        V& v = vs[i];
        v.p.x += shiftX; v.c.x += shiftX; v.c1.x += shiftX;
        auto add = [&](Pt p) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); };
        add(v.p);
        if (v.type == STBTT_vcurve || v.type == STBTT_vcubic) add(v.c);
        if (v.type == STBTT_vcubic) add(v.c1);
        // 1/64 픽셀 단위의 정수로 (stb의 꼭짓점은 16비트)
        sv[i].x = (stbtt_vertex_type)lroundf(v.p.x * 64.f); sv[i].y = (stbtt_vertex_type)lroundf(v.p.y * 64.f);
        sv[i].cx = (stbtt_vertex_type)lroundf(v.c.x * 64.f); sv[i].cy = (stbtt_vertex_type)lroundf(v.c.y * 64.f);
        sv[i].cx1 = (stbtt_vertex_type)lroundf(v.c1.x * 64.f); sv[i].cy1 = (stbtt_vertex_type)lroundf(v.c1.y * 64.f);
    }
    const int bx0 = (int)floorf(x0 + 1e-3f), by0 = (int)floorf(-y1 + 1e-3f), bx1 = (int)ceilf(x1 - 1e-3f), by1 = (int)ceilf(-y0 - 1e-3f);
    if (x1 - x0 < 500.f && y1 - y0 < 500.f && bx1 > bx0 && by1 > by0) {
        out.w = bx1 - bx0; out.h = by1 - by0; out.ox = bx0; out.oy = by0;
        out.a.assign((size_t)out.w * out.h, 0);
        stbtt__bitmap bm;
        bm.w = out.w; bm.h = out.h; bm.stride = out.w; bm.pixels = out.a.data();
        stbtt_Rasterize(&bm, 0.3f, sv.data(), n, 1.f / 64, 1.f / 64, 0, 0, bx0, by0, 1, f->info.userdata);
    }
}

}  // namespace fonthint
