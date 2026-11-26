// LVGL의 그리기 명령을 가로채 화면 배율의 버퍼에 한 번 더 그린다.
// 장치 쪽 렌더는 손대지 않으므로 내보낸 코드와의 픽셀 일치는 그대로다.
#include "smooth.h"
#include "fontface.h"
#include "fontgen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

#include "src/misc/lv_area_private.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/lv_draw_label_private.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"

namespace {
float g_k = 0.f;
bool g_on = true, g_drew = false;
int g_depth = 0;
std::vector<uint32_t> g_out;
int (*g_fontSize)(const lv_font_t*) = nullptr;
bool (*g_source)(const lv_font_t*, smooth::GlyphSource&) = nullptr;
const uint32_t* (*g_image)(const void*, int, int) = nullptr;

// LVGL 층 하나에 대응하는 화면 배율의 층
struct Big {
    lv_layer_t layer = {};
    lv_draw_buf_t* buf = nullptr;
    ~Big() { if (buf) lv_draw_buf_destroy(buf); }
};
std::unique_ptr<Big> g_root;
std::map<lv_layer_t*, std::unique_ptr<Big>> g_layers;

int edge(int v) { return (int)lroundf(v * g_k); }
int len(int v) {
    if (v == 0) return 0;
    const int r = std::max(1, (int)lroundf(abs(v) * g_k));
    return v > 0 ? r : -r;
}
int radius(int r) { return r >= LV_RADIUS_CIRCLE ? r : (int)lroundf(r * g_k); }
lv_area_t area(const lv_area_t& a) {
    lv_area_t r;
    r.x1 = edge(a.x1); r.y1 = edge(a.y1); r.x2 = edge(a.x2 + 1) - 1; r.y2 = edge(a.y2 + 1) - 1;
    return r;
}
// 가는 선이 반올림으로 사라지지 않게
lv_area_t solidArea(const lv_area_t& a) {
    lv_area_t r = area(a);
    if (r.x2 < r.x1 && a.x2 >= a.x1) r.x2 = r.x1;
    if (r.y2 < r.y1 && a.y2 >= a.y1) r.y2 = r.y1;
    return r;
}
// 장치가 RGB565면 단색은 그 장치에서 나올 색으로
lv_color_t tone(lv_color_t c) {
    if (g_depth != 16) return c;
    const int r = c.red >> 3, g = c.green >> 2, b = c.blue >> 3;
    c.red = (uint8_t)(r << 3 | r >> 2);
    c.green = (uint8_t)(g << 2 | g >> 4);
    c.blue = (uint8_t)(b << 3 | b >> 2);
    return c;
}

std::unique_ptr<Big> makeBig(const lv_area_t& a, lv_color_format_t cf) {
    const int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
    if (w <= 0 || h <= 0) return nullptr;
    auto b = std::make_unique<Big>();
    b->buf = lv_draw_buf_create((uint32_t)w, (uint32_t)h, cf, LV_STRIDE_AUTO);
    if (!b->buf) return nullptr;
    lv_draw_buf_clear(b->buf, nullptr);
    b->layer.draw_buf = b->buf;
    b->layer.color_format = cf;
    b->layer.buf_area = a;
    b->layer.phy_clip_area = a;
    b->layer._clip_area = a;
    b->layer.opa = LV_OPA_COVER;
    return b;
}
Big* bigFor(lv_layer_t* l) {
    if (!l->parent) return g_root.get();
    auto it = g_layers.find(l);
    if (it != g_layers.end()) return it->second.get();
    auto b = makeBig(area(l->buf_area), LV_COLOR_FORMAT_ARGB8888);
    if (!b) return nullptr;
    Big* p = b.get();
    g_layers[l] = std::move(b);
    return p;
}

// 글리프 번호 → 문자 (그리기 명령에는 번호만 남는다)
std::map<const lv_font_t*, std::map<uint32_t, uint32_t>> g_faces;
uint32_t codepointOf(const lv_font_glyph_dsc_t* g, int& size, bool& kr, const FontAsset*& face) {
    const lv_font_t* f = g->resolved_font;
    if (!f) return 0;
    size = g_fontSize(f);
    smooth::GlyphSource src;
    kr = g_source(f, src);
    if (kr) {
        size = src.size;
        face = src.face;
        const uint32_t i = g->gid.index;
        return i >= 1 && i <= src.cps->size() ? (*src.cps)[i - 1] : 0;
    }
    auto it = g_faces.find(f);
    if (it == g_faces.end()) {
        it = g_faces.emplace(f, std::map<uint32_t, uint32_t>()).first;
        auto add = [&](uint32_t cp) {
            lv_font_glyph_dsc_t d = {};
            if (f->get_glyph_dsc(f, &d, cp, 0)) it->second[d.gid.index] = cp;
        };
        for (uint32_t c = 0x20; c < 0x7F; c++) add(c);
        add(0xB0);
        add(0x2022);
        for (uint32_t c = 0xF000; c <= 0xF8FF; c++) add(c);
    }
    auto c = it->second.find(g->gid.index);
    return c == it->second.end() ? 0 : c->second;
}

lv_draw_task_t* g_task = nullptr;   // 화면 배율로 옮긴 지금의 그리기 명령

void letterCb(lv_draw_task_t*, lv_draw_glyph_dsc_t* gd, lv_draw_fill_dsc_t* fd, const lv_area_t* fa) {
    if (gd && gd->format == LV_FONT_GLYPH_FORMAT_NONE) {   // 글꼴에 없는 글자: 장치처럼 빈 네모
        if (!gd->bg_coords) return;
        lv_draw_border_dsc_t b;
        lv_draw_border_dsc_init(&b);
        b.opa = gd->opa;
        b.color = tone(gd->color);
        b.width = len(1);
        lv_area_t a = area(*gd->bg_coords);
        lv_draw_sw_border(g_task, &b, &a);
        return;
    }
    if (gd && gd->letter_coords && gd->g) {
        int size = 14;
        bool kr = false;
        const FontAsset* face = nullptr;
        const uint32_t cp = codepointOf(gd->g, size, kr, face);
        // 장치에서의 글자 원점(펜 위치와 기준선)에 같은 윤곽을 크게 놓는다
        const float ox = (float)(gd->letter_coords->x1 - gd->g->ofs_x) * g_k;
        const float by = (float)(gd->letter_coords->y2 + 1 + gd->g->ofs_y) * g_k;
        const float fx = floorf(ox);
        static fontgen::Raster scaled;
        const fontgen::Raster* pr = face ? &fontface::raster(*face, size, g_k, cp, (int)((ox - fx) * 4.f)) : &fontgen::raster(kr, size, g_k, cp, (int)((ox - fx) * 4.f));
        lv_area_t a;
        a.x1 = (int)fx + pr->ox;
        a.y1 = (int)lroundf(by) + pr->oy;
        if (pr->w <= 0 || pr->h <= 0) {   // 윤곽을 모르는 글자: 장치 글꼴의 그림을 키워 쓴다
            const lv_draw_buf_t* db = (const lv_draw_buf_t*)lv_font_get_glyph_bitmap(gd->g, gd->_draw_buf);
            if (!db) return;
            a = area(*gd->letter_coords);
            scaled.w = a.x2 - a.x1 + 1;
            scaled.h = a.y2 - a.y1 + 1;
            if (scaled.w <= 0 || scaled.h <= 0) return;
            scaled.a.assign((size_t)scaled.w * scaled.h, 0);
            for (int y = 0; y < scaled.h; y++)
                for (int x = 0; x < scaled.w; x++)
                    scaled.a[(size_t)y * scaled.w + x] = db->data[(size_t)(y * gd->g->box_h / scaled.h) * db->header.stride + x * gd->g->box_w / scaled.w];
            pr = &scaled;
        }
        const fontgen::Raster& r = *pr;
        a.x2 = a.x1 + r.w - 1;
        a.y2 = a.y1 + r.h - 1;
        lv_draw_sw_blend_dsc_t bd;
        memset(&bd, 0, sizeof bd);
        bd.blend_area = &a;
        bd.mask_area = &a;
        bd.mask_buf = r.a.data();
        bd.mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
        bd.mask_stride = r.w;
        bd.color = tone(gd->color);
        bd.opa = gd->opa;
        bd.blend_mode = LV_BLEND_MODE_NORMAL;
        lv_draw_sw_blend(g_task, &bd);
    }
    if (fd && fa) {
        lv_draw_fill_dsc_t f = *fd;
        f.color = tone(f.color);
        f.radius = radius(f.radius);
        lv_area_t a = solidArea(*fa);
        lv_draw_sw_fill(g_task, &f, &a);
    }
}

int32_t evaluate(lv_draw_unit_t*, lv_draw_task_t* t) {
    if (!g_on || g_k <= 0.f || !g_root || !t->target_layer) return 0;
    Big* big = bigFor(t->target_layer);
    if (!big) return 0;
    lv_draw_task_t T = *t;
    T.target_layer = &big->layer;
    T.clip_area = area(t->clip_area);
    T.area = area(t->area);
    T._real_area = area(t->_real_area);
    if (!lv_area_intersect(&T.clip_area, &T.clip_area, &big->layer.buf_area)) return 0;
    g_drew = true;
    switch (t->type) {
    case LV_DRAW_TASK_TYPE_FILL: {
        lv_draw_fill_dsc_t d = *(lv_draw_fill_dsc_t*)t->draw_dsc;
        d.color = tone(d.color);
        d.radius = radius(d.radius);
        lv_area_t a = solidArea(t->area);
        T.draw_dsc = &d;
        lv_draw_sw_fill(&T, &d, &a);
        break;
    }
    case LV_DRAW_TASK_TYPE_BORDER: {
        lv_draw_border_dsc_t d = *(lv_draw_border_dsc_t*)t->draw_dsc;
        d.color = tone(d.color);
        d.radius = radius(d.radius);
        d.width = len(d.width);
        T.draw_dsc = &d;
        lv_draw_sw_border(&T, &d, &T.area);
        break;
    }
    case LV_DRAW_TASK_TYPE_BOX_SHADOW: {
        lv_draw_box_shadow_dsc_t d = *(lv_draw_box_shadow_dsc_t*)t->draw_dsc;
        d.radius = radius(d.radius);
        d.width = len(d.width);
        d.spread = len(d.spread);
        d.ofs_x = len(d.ofs_x);
        d.ofs_y = len(d.ofs_y);
        T.draw_dsc = &d;
        lv_draw_sw_box_shadow(&T, &d, &T.area);
        break;
    }
    case LV_DRAW_TASK_TYPE_LABEL: {
        g_task = &T;
        lv_draw_task_t src = *t;
        lv_draw_label_dsc_t d = *(lv_draw_label_dsc_t*)t->draw_dsc;
        d.hint = nullptr;   // 장치 쪽 렌더가 쓰는 기억을 건드리지 않는다
        src.draw_dsc = &d;
        lv_draw_label_iterate_characters(&src, &d, &t->area, letterCb);
        break;
    }
    case LV_DRAW_TASK_TYPE_LETTER: {
        g_task = &T;
        lv_draw_task_t src = *t;
        const lv_draw_letter_dsc_t* d = (const lv_draw_letter_dsc_t*)t->draw_dsc;
        lv_draw_glyph_dsc_t gd;
        lv_draw_glyph_dsc_init(&gd);
        gd.opa = d->opa;
        gd.color = d->color;
        gd.pivot = d->pivot;
        lv_point_t pos = {t->area.x1, t->area.y1};
        lv_draw_unit_draw_letter(&src, &gd, &pos, d->font, d->unicode, letterCb);
        if (gd._draw_buf) lv_draw_buf_destroy(gd._draw_buf);
        break;
    }
    case LV_DRAW_TASK_TYPE_ARC: {
        lv_draw_arc_dsc_t d = *(lv_draw_arc_dsc_t*)t->draw_dsc;
        d.color = tone(d.color);
        d.center.x = edge(d.center.x);
        d.center.y = edge(d.center.y);
        d.radius = (uint16_t)std::max(1, (int)lroundf(d.radius * g_k));
        d.width = std::max(1, (int)lroundf(d.width * g_k));
        lv_area_t a;
        a.x1 = d.center.x - d.radius;
        a.y1 = d.center.y - d.radius;
        a.x2 = d.center.x + d.radius - 1;
        a.y2 = d.center.y + d.radius - 1;
        T.draw_dsc = &d;
        lv_draw_sw_arc(&T, &d, &a);
        break;
    }
    case LV_DRAW_TASK_TYPE_LINE: {
        lv_draw_line_dsc_t d = *(lv_draw_line_dsc_t*)t->draw_dsc;
        d.color = tone(d.color);
        d.width = len(d.width);
        d.p1.x = (lv_value_precise_t)(d.p1.x * g_k);
        d.p1.y = (lv_value_precise_t)(d.p1.y * g_k);
        d.p2.x = (lv_value_precise_t)(d.p2.x * g_k);
        d.p2.y = (lv_value_precise_t)(d.p2.y * g_k);
        T.draw_dsc = &d;
        lv_draw_sw_line(&T, &d);
        break;
    }
    case LV_DRAW_TASK_TYPE_MASK_RECTANGLE: {
        lv_draw_mask_rect_dsc_t d = *(lv_draw_mask_rect_dsc_t*)t->draw_dsc;
        d.area = area(d.area);
        d.radius = radius(d.radius);
        T.draw_dsc = &d;
        lv_draw_sw_mask_rect(&T, &d);
        break;
    }
    case LV_DRAW_TASK_TYPE_IMAGE: {
        const lv_draw_image_dsc_t* d = (const lv_draw_image_dsc_t*)t->draw_dsc;
        lv_area_t a = area(d->image_area);
        const int W = a.x2 - a.x1 + 1, H = a.y2 - a.y1 + 1;
        const uint32_t* px = g_image && W > 0 && H > 0 ? g_image(d->src, W, H) : nullptr;
        if (!px) break;
        static std::vector<uint32_t> cut, tinted;
        if (d->recolor_opa > 0 && d->recolor_opa <= 255) {   // 물들이기
            tinted.assign(px, px + (size_t)W * H);
            const int o = d->recolor_opa;
            for (uint32_t& p : tinted) {
                const uint32_t b = ((p & 255) * (255 - o) + d->recolor.blue * o) / 255, g = (((p >> 8) & 255) * (255 - o) + d->recolor.green * o) / 255, r = (((p >> 16) & 255) * (255 - o) + d->recolor.red * o) / 255;
                p = (p & 0xFF000000u) | r << 16 | g << 8 | b;
            }
            px = tinted.data();
        }
        if (d->clip_radius > 0) {   // 둥근 모서리
            const float r = std::min({(float)radius(d->clip_radius), W * 0.5f, H * 0.5f});
            cut.assign(px, px + (size_t)W * H);
            const int R = (int)ceilf(r);
            for (int y = 0; y < H; y++) {
                if (y >= R && y < H - R) continue;
                const float dy = y < R ? r - (y + 0.5f) : (y + 0.5f) - (H - r);
                for (int x = 0; x < W; x++) {
                    if (x >= R && x < W - R) { x = W - R - 1; continue; }
                    const float dx = x < R ? r - (x + 0.5f) : (x + 0.5f) - (W - r);
                    if (dx <= 0 || dy <= 0) continue;
                    const float k = std::clamp(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0.f, 1.f);
                    uint32_t& p = cut[(size_t)y * W + x];
                    p = (p & 0xFFFFFF) | (uint32_t)((p >> 24) * k + 0.5f) << 24;
                }
            }
            px = cut.data();
        }
        lv_draw_sw_blend_dsc_t bd;
        memset(&bd, 0, sizeof bd);
        bd.blend_area = &a;
        bd.src_area = &a;
        bd.src_buf = px;
        bd.src_stride = (uint32_t)W * 4;
        bd.src_color_format = LV_COLOR_FORMAT_ARGB8888;
        bd.opa = d->opa;
        bd.blend_mode = d->blend_mode;
        lv_draw_sw_blend(&T, &bd);
        break;
    }
    case LV_DRAW_TASK_TYPE_LAYER: {
        const lv_draw_image_dsc_t* d = (const lv_draw_image_dsc_t*)t->draw_dsc;
        auto it = g_layers.find((lv_layer_t*)d->src);
        if (it == g_layers.end()) break;
        Big& s = *it->second;
        lv_draw_sw_blend_dsc_t bd;
        memset(&bd, 0, sizeof bd);
        bd.blend_area = &s.layer.buf_area;
        bd.src_area = &s.layer.buf_area;
        bd.src_buf = s.buf->data;
        bd.src_stride = s.buf->header.stride;
        bd.src_color_format = LV_COLOR_FORMAT_ARGB8888;
        bd.opa = d->opa;
        bd.blend_mode = d->blend_mode;
        lv_draw_sw_blend(&T, &bd);
        g_layers.erase(it);
        break;
    }
    default: break;
    }
    return 0;
}
int32_t dispatch(lv_draw_unit_t*, lv_layer_t*) { return LV_DRAW_UNIT_IDLE; }
}  // namespace

namespace smooth {

void init(int (*fontSize)(const lv_font_t*), bool (*source)(const lv_font_t*, GlyphSource&),
          const uint32_t* (*image)(const void*, int, int)) {
    g_image = image;
    g_fontSize = fontSize;
    g_source = source;
    lv_draw_unit_t* u = (lv_draw_unit_t*)lv_draw_create_unit(sizeof(lv_draw_unit_t));
    u->name = "HIGENIS_SMOOTH";
    u->evaluate_cb = evaluate;
    u->dispatch_cb = dispatch;
}

void setScale(float k) {
    if (k == g_k) return;
    g_k = k;
    g_root.reset();
    g_layers.clear();
    fontgen::clearRaster();
    if (k <= 0.f) std::vector<uint32_t>().swap(g_out);
}
void enable(bool on) { g_on = on; }
void forgetFonts() { g_faces.clear(); }

void begin(int w, int h, int depth) {
    g_drew = false;
    g_layers.clear();
    if (g_k <= 0.f || !g_on) return;
    g_depth = depth;
    lv_area_t a;
    a.x1 = a.y1 = 0;
    a.x2 = edge(w) - 1;
    a.y2 = edge(h) - 1;
    if (!g_root || g_root->layer.buf_area.x2 != a.x2 || g_root->layer.buf_area.y2 != a.y2)
        g_root = makeBig(a, LV_COLOR_FORMAT_XRGB8888);
}

bool end() {
    if (!g_drew || !g_on || g_k <= 0.f || !g_root) return false;
    const int W = width(), H = height();
    g_out.resize((size_t)W * H);
    for (int y = 0; y < H; y++) {
        const uint32_t* s = (const uint32_t*)(g_root->buf->data + (size_t)y * g_root->buf->header.stride);
        uint32_t* d = &g_out[(size_t)y * W];
        for (int x = 0; x < W; x++) d[x] = s[x] | 0xFF000000u;
    }
    return true;
}

const std::vector<uint32_t>& pixels() { return g_out; }
int width() { return g_root ? g_root->layer.buf_area.x2 + 1 : 0; }
int height() { return g_root ? g_root->layer.buf_area.y2 + 1 : 0; }

}  // namespace smooth
