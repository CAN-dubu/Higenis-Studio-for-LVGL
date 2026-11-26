// LVGL 호스트: 모델 → 실제 LVGL 객체 → 프레임버퍼 → 텍스처
#include "app.h"
#include "fontgen.h"
#include "fontface.h"
#include "image.h"
#include "higenis_fx.h"
#include "lvgl.h"
#include "smooth.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace {
lv_display_t* g_disp = nullptr;
std::vector<uint8_t> g_buf;
std::vector<uint32_t> g_out;
int g_w = 0, g_h = 0, g_depth = 0;
bool g_ready = false, g_smoothReady = false;
uint32_t g_tickOffset = 0;
bool g_manual = false;
double g_manualMs = 0;
double g_speed = 1.0;          // 시운전 느리게 보기
uint32_t g_lastTimer = 0;
lv_obj_t* g_scr = nullptr;
std::map<int, Rect> g_coords, g_content;
std::map<int, lv_obj_t*> g_objs;
bool g_view = false;   // 매끈하게 보기
bool g_themeInit = false, g_themeDark = false;
int g_themePrimary = 0;

uint32_t tickCb() {
    static const auto t0 = std::chrono::steady_clock::now();
    static double last = 0, virt = 0;
    if (g_manual) return (uint32_t)g_manualMs + g_tickOffset;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    virt += (ms - last) * g_speed;
    last = ms;
    return (uint32_t)virt + g_tickOffset;
}

void refrStartCb(lv_event_t*) { smooth::begin(g_w, g_h, g_depth); }

void flushCb(lv_display_t* d, const lv_area_t*, uint8_t* px) {
    const int n = g_w * g_h;
    if (g_depth == 16) {
        const uint16_t* s = (const uint16_t*)px;
        for (int i = 0; i < n; i++) {
            uint32_t c = s[i];
            uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
            g_out[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    } else {
        const uint32_t* s = (const uint32_t*)px;
        for (int i = 0; i < n; i++) g_out[i] = s[i] | 0xFF000000u;
    }
    g_smoothReady = smooth::end();
    g_ready = true;
    lv_display_flush_ready(d);
}

void configure(int w, int h, int depth) {
    g_w = w; g_h = h; g_depth = depth;
    lv_display_set_resolution(g_disp, w, h);
    lv_display_set_color_format(g_disp, depth == 16 ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_XRGB8888);
    const uint32_t bpp = depth == 16 ? 2 : 4;
    const uint32_t size = (uint32_t)w * h * bpp;
    g_buf.assign(size + 64, 0);
    uint8_t* p = g_buf.data();
    p += (64 - ((uintptr_t)p & 63)) & 63;
    lv_display_set_buffers(g_disp, p, nullptr, size, LV_DISPLAY_RENDER_MODE_FULL);
    g_out.assign((size_t)w * h, 0xFFFFFFFFu);
}

lv_state_t lvState(int s) {
    switch (s) {
    case ST_PRESSED: return LV_STATE_PRESSED;
    case ST_CHECKED: return LV_STATE_CHECKED;
    case ST_FOCUSED: return LV_STATE_FOCUSED;
    case ST_DISABLED: return LV_STATE_DISABLED;
    default: return LV_STATE_DEFAULT;
    }
}

const lv_align_t kAlign[9] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_MID, LV_ALIGN_TOP_RIGHT,
                              LV_ALIGN_LEFT_MID, LV_ALIGN_CENTER, LV_ALIGN_RIGHT_MID,
                              LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_MID, LV_ALIGN_BOTTOM_RIGHT};

// 크기별 한글 글꼴. 주소가 바뀌지 않도록 고정 배열에 두고 내용만 갈아 끼운다
struct KrFont {
    std::vector<uint32_t> cps;
    std::vector<uint8_t> bitmap;
    std::vector<lv_font_fmt_txt_glyph_dsc_t> dsc;
    std::vector<uint16_t> list;
    lv_font_fmt_txt_cmap_t cmap = {};
    lv_font_fmt_txt_dsc_t fdsc = {};
    lv_font_t kr = {}, ui = {};
    bool on = false;
    std::shared_ptr<const FontAsset> face;
} g_kr[12];
std::map<std::pair<int, int>, std::unique_ptr<KrFont>> g_faces;   // (글꼴 번호, 크기)

const lv_font_t* builtinFont(int size) {
    switch (size) {
    case 10: return &lv_font_montserrat_10;
    case 12: return &lv_font_montserrat_12;
    case 16: return &lv_font_montserrat_16;
    case 18: return &lv_font_montserrat_18;
    case 20: return &lv_font_montserrat_20;
    case 22: return &lv_font_montserrat_22;
    case 24: return &lv_font_montserrat_24;
    case 26: return &lv_font_montserrat_26;
    case 28: return &lv_font_montserrat_28;
    case 36: return &lv_font_montserrat_36;
    case 48: return &lv_font_montserrat_48;
    default: return &lv_font_montserrat_14;
    }
}
const lv_font_t* fontFor(int size) {
    for (int i = 0; i < kFontSizeCount; i++)
        if (kFontSizes[i] == size && g_kr[i].on) return &g_kr[i].ui;
    return builtinFont(size);
}
int sizeOfFont(const lv_font_t* f) {
    for (int i = 0; i < kFontSizeCount; i++)
        if (builtinFont(kFontSizes[i]) == f || &g_kr[i].ui == f || &g_kr[i].kr == f) return kFontSizes[i];
    for (auto& [k, v] : g_faces) if (&v->kr == f) return k.second;
    return 14;
}
bool glyphSource(const lv_font_t* f, smooth::GlyphSource& out) {
    for (int i = 0; i < kFontSizeCount; i++) if (&g_kr[i].kr == f) { out.cps = &g_kr[i].cps; out.size = kFontSizes[i]; return true; }
    for (auto& [k, v] : g_faces) if (&v->kr == f) { out.cps = &v->cps; out.size = k.second; out.face = v->face.get(); return true; }
    return false;
}
const lv_font_t* fontFor(int size, int face) {
    auto it = g_faces.find({face, size});
    return it != g_faces.end() && it->second->on ? &it->second->kr : fontFor(size);
}

// 글꼴 계획을 실제 LVGL 글꼴로. 내보내는 ui_font.c와 같은 구조다
template <class GlyphFn> void fillFont(KrFont& k, GlyphFn glyphOf, int lh, int bl) {
    k.bitmap.clear();
    k.dsc.assign(1, {});
    k.list.clear();
    for (uint32_t cp : k.cps) {
        const fontgen::Glyph& g = glyphOf(cp);
        lv_font_fmt_txt_glyph_dsc_t d = {};
        d.bitmap_index = (uint32_t)k.bitmap.size();
        d.adv_w = (uint32_t)g.adv16;
        d.box_w = (uint8_t)g.w;
        d.box_h = (uint8_t)g.h;
        d.ofs_x = (int8_t)g.ox;
        d.ofs_y = (int8_t)g.oy;
        k.dsc.push_back(d);
        k.list.push_back((uint16_t)(cp - k.cps.front()));
        k.bitmap.insert(k.bitmap.end(), g.bits.begin(), g.bits.end());
    }
    if (k.bitmap.empty()) k.bitmap.push_back(0);
    k.cmap = {};
    k.cmap.range_start = k.cps.front();
    k.cmap.range_length = (uint16_t)(k.cps.back() - k.cps.front() + 1);
    k.cmap.glyph_id_start = 1;
    k.cmap.unicode_list = k.list.data();
    k.cmap.list_length = (uint16_t)k.list.size();
    k.cmap.type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY;
    k.fdsc = {};
    k.fdsc.glyph_bitmap = k.bitmap.data();
    k.fdsc.glyph_dsc = k.dsc.data();
    k.fdsc.cmaps = &k.cmap;
    k.fdsc.cmap_num = 1;
    k.fdsc.bpp = 4;
    k.kr = {};
    k.kr.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    k.kr.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
    k.kr.line_height = lh;
    k.kr.base_line = bl;
    k.kr.static_bitmap = 1;
    k.kr.dsc = &k.fdsc;
}
void applyFonts(const fontgen::Plan& pl, const Document& doc) {
    int lh, bl;
    for (int i = 0; i < kFontSizeCount; i++) {
        const int size = kFontSizes[i];
        KrFont& k = g_kr[i];
        auto it = pl.sizes.find(size);
        k.on = it != pl.sizes.end();
        if (!k.on || k.cps == it->second) continue;
        k.cps = it->second;
        fontgen::metrics(size, lh, bl);
        fillFont(k, [&](uint32_t cp) -> const fontgen::Glyph& { return fontgen::glyph(size, cp); }, lh, bl);
        k.ui = *builtinFont(size);    // 영문·숫자 배치는 그대로 두고
        k.ui.fallback = &k.kr;        // 없는 글자만 한글 글꼴에서
    }
    // 고른 글꼴. 거기에도 없는 글자는 위의 기본 글꼴로
    for (auto& [k, v] : g_faces) v->on = false;
    for (auto& [key, cps] : pl.faces) {
        const auto* f = fontface::find(doc, key.first);
        if (!f || cps.empty()) continue;
        auto& p = g_faces[key];
        if (!p) p = std::make_unique<KrFont>();
        p->on = true;
        if (p->face != *f || p->cps != cps) {
            p->face = *f;
            p->cps = cps;
            fontface::metrics(**f, key.second, lh, bl);
            fillFont(*p, [&](uint32_t cp) -> const fontgen::Glyph& { return fontface::glyph(**f, key.second, cp); }, lh, bl);
        }
        p->kr.fallback = fontFor(key.second);
    }
}

bool g_simOn = false;

// 도면의 그림을 LVGL 그림으로. 내보내는 C 배열과 같은 데이터다
struct HostImage {
    lv_image_dsc_t dsc = {};
    std::shared_ptr<const image::Variant> v;
    std::shared_ptr<const ImageAsset> a;
    int fit = 0, gen = 0;
    ImageFx fx;
    std::vector<uint32_t> big;   // 매끈하게 보기용
    int bw = 0, bh = 0;
};
std::vector<std::unique_ptr<HostImage>> g_images;
int g_gen = 0;
const Document* g_doc = nullptr;
static_assert(LV_COLOR_FORMAT_RGB565 == 0x12 && LV_COLOR_FORMAT_RGB565A8 == 0x14 && LV_COLOR_FORMAT_XRGB8888 == 0x11 && LV_COLOR_FORMAT_ARGB8888 == 0x10, "색 형식 번호");

const lv_image_dsc_t* hostImage(const Document& d, const Widget& w) {
    const auto* a = image::find(d, w.text);
    if (!a) return nullptr;
    int W, H;
    image::size(d, w, W, H);
    auto v = image::variant(*a, W, H, image::fit(w), d.depth, w.x2.img);
    for (auto& h : g_images) if (h->v == v) { h->gen = g_gen; return &h->dsc; }
    auto h = std::make_unique<HostImage>();
    h->v = v;
    h->a = *a;
    h->fit = image::fit(w);
    h->fx = w.x2.img;
    h->gen = g_gen;
    h->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    h->dsc.header.cf = (uint32_t)v->cf;
    h->dsc.header.w = (uint32_t)v->w;
    h->dsc.header.h = (uint32_t)v->h;
    h->dsc.header.stride = (uint32_t)v->stride;
    h->dsc.data_size = (uint32_t)v->data.size();
    h->dsc.data = v->data.data();
    g_images.push_back(std::move(h));
    return &g_images.back()->dsc;
}
// 화면 배율의 그림: 장치용으로 줄인 것이 아니라 원본에서 다시 만든다
const uint32_t* bigImage(const void* src, int W, int H) {
    for (auto& h : g_images) {
        if (&h->dsc != src) continue;
        if (h->bw != W || h->bh != H) {
            h->big = image::fitted(h->a, W, H, h->fit, (double)W / std::max(1, h->v->w), h->fx);
            h->bw = W;
            h->bh = H;
        }
        return h->big.data();
    }
    return nullptr;
}

lv_obj_t* createObj(const Widget& w, lv_obj_t* par) {
    lv_obj_t* o = nullptr;
    switch (w.type) {
    case WType::Container: o = lv_obj_create(par); break;
    case WType::Label: {
        o = lv_label_create(par);
        lv_label_set_text(o, w.text.c_str());
        static const lv_label_long_mode_t modes[] = {LV_LABEL_LONG_MODE_WRAP, LV_LABEL_LONG_MODE_DOTS, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR, LV_LABEL_LONG_MODE_CLIP};
        // 흐르는 글자는 편집 중에는 멈춰 두고 시운전에서만 흐른다
        const int lm = w.x2.longMode == 2 && !g_simOn ? 3 : w.x2.longMode;
        if (lm) lv_label_set_long_mode(o, modes[lm & 3]);
        if (w.x2.fx && w.x2.longMode != 2) higenis_fx(o, w.x2.fx, w.x2.fxSize, w.x2.fxDx, w.x2.fxDy, (uint32_t)w.x2.fxColor, w.x2.fxOpa);
        break;
    }
    case WType::Button: {
        o = lv_button_create(par);
        lv_obj_t* l = lv_label_create(o);
        lv_label_set_text(l, w.text.c_str());
        lv_obj_center(l);
        break;
    }
    case WType::Switch: o = lv_switch_create(par); if (w.checked) lv_obj_add_state(o, LV_STATE_CHECKED); break;
    case WType::Checkbox:
        o = lv_checkbox_create(par);
        lv_checkbox_set_text(o, w.text.c_str());
        if (w.checked) lv_obj_add_state(o, LV_STATE_CHECKED);
        break;
    case WType::Slider:
        o = lv_slider_create(par);
        lv_slider_set_range(o, w.minv, w.maxv);
        lv_slider_set_value(o, w.value, LV_ANIM_OFF);
        break;
    case WType::Bar:
        o = lv_bar_create(par);
        lv_bar_set_range(o, w.minv, w.maxv);
        lv_bar_set_value(o, w.value, LV_ANIM_OFF);
        break;
    case WType::Arc:
        o = lv_arc_create(par);
        lv_arc_set_range(o, w.minv, w.maxv);
        lv_arc_set_value(o, w.value);
        if (w.checked) {   // 게이지: 손잡이 없이 보기만
            lv_obj_remove_style(o, nullptr, LV_PART_KNOB);
            lv_obj_set_clickable(o, false);
        }
        break;
    case WType::Dropdown: o = lv_dropdown_create(par); lv_dropdown_set_options(o, w.text2.c_str()); break;
    case WType::Roller:
        o = lv_roller_create(par);
        lv_roller_set_options(o, w.text2.c_str(), LV_ROLLER_MODE_NORMAL);
        lv_roller_set_selected(o, (uint32_t)std::max(0, w.value), LV_ANIM_OFF);
        break;
    case WType::Textarea:
        o = lv_textarea_create(par);
        lv_textarea_set_one_line(o, w.checked);
        lv_textarea_set_placeholder_text(o, w.text2.c_str());
        lv_textarea_set_text(o, w.text.c_str());
        break;
    case WType::Led:
        o = lv_led_create(par);
        lv_led_set_color(o, lv_color_hex((uint32_t)w.color));
        if (w.checked) lv_led_on(o); else lv_led_off(o);
        break;
    case WType::Spinner: o = lv_spinner_create(par); break;
    case WType::Image:
        o = lv_image_create(par);
        if (const lv_image_dsc_t* src = g_doc ? hostImage(*g_doc, w) : nullptr) lv_image_set_src(o, src);
        break;
    default: o = lv_obj_create(par); break;
    }
    return o;
}

void applyCommon(const Screen& scr, const Widget& w, lv_obj_t* o) {
    if (w.autoSize) lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    else lv_obj_set_size(o, w.w, w.h);
    lv_obj_align(o, kAlign[w.align], w.x, w.y);
    if (w.hidden) lv_obj_set_hidden(o, true);
    if (wantsClick(w)) lv_obj_set_clickable(o, true);
    if (w.type == WType::Image) lv_obj_set_style_border_post(o, true, 0);   // 액자: 테두리를 그림 위에
    for (int st = 0; st < ST_COUNT; st++) {
        const lv_style_selector_t sel = LV_PART_MAIN | lvState(st);
        if (w.style[st][SP_TEXT_FONT].set || w.style[st][SP_FACE].set) {
            int size, face;
            resolveFont(scr, w, st, size, face);
            lv_obj_set_style_text_font(o, fontFor(size, face), sel);
            // LVGL은 상태에 따른 글꼴 변화만으로는 글자 배치를 다시 하지 않는다.
            // 배치 속성인 translate_y를 0으로 함께 지정하면 모양은 그대로인 채 다시 배치한다 (내보낸 코드도 같다)
            if (st != ST_DEFAULT) lv_obj_set_style_translate_y(o, 0, sel);
        }
        for (int p = 0; p < SP_COUNT; p++) {
            const StyleVal& v = w.style[st][p];
            if (!v.set) continue;
            switch (p) {
            case SP_BG_COLOR: lv_obj_set_style_bg_color(o, lv_color_hex((uint32_t)v.v), sel); break;
            case SP_BG_OPA: lv_obj_set_style_bg_opa(o, (lv_opa_t)v.v, sel); break;
            case SP_BORDER_COLOR: lv_obj_set_style_border_color(o, lv_color_hex((uint32_t)v.v), sel); break;
            case SP_BORDER_WIDTH: lv_obj_set_style_border_width(o, v.v, sel); break;
            case SP_RADIUS:
                lv_obj_set_style_radius(o, v.v, sel);
                if (radiusOnIndicator(w.type)) lv_obj_set_style_radius(o, v.v, (lv_style_selector_t)LV_PART_INDICATOR | (lv_style_selector_t)lvState(st));
                break;
            case SP_TEXT_COLOR: lv_obj_set_style_text_color(o, lv_color_hex((uint32_t)v.v), sel); break;
            case SP_OPA: lv_obj_set_style_opa(o, (lv_opa_t)v.v, sel); break;
            case SP_TEXT_OPA: lv_obj_set_style_text_opa(o, (lv_opa_t)v.v, sel); break;
            case SP_LETTER: lv_obj_set_style_text_letter_space(o, v.v, sel); break;
            case SP_LINE: lv_obj_set_style_text_line_space(o, v.v, sel); break;
            case SP_TALIGN: lv_obj_set_style_text_align(o, v.v == 1 ? LV_TEXT_ALIGN_LEFT : v.v == 2 ? LV_TEXT_ALIGN_CENTER : v.v == 3 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_AUTO, sel); break;
            case SP_DECOR: lv_obj_set_style_text_decor(o, (lv_text_decor_t)((v.v & 1 ? LV_TEXT_DECOR_UNDERLINE : 0) | (v.v & 2 ? LV_TEXT_DECOR_STRIKETHROUGH : 0)), sel); break;
            case SP_IMG_OPA: lv_obj_set_style_image_opa(o, (lv_opa_t)v.v, sel); break;
            case SP_IMG_TINT: lv_obj_set_style_image_recolor(o, lv_color_hex((uint32_t)v.v), sel); break;
            case SP_IMG_TINT_OPA: lv_obj_set_style_image_recolor_opa(o, (lv_opa_t)v.v, sel); break;
            case SP_PAD: lv_obj_set_style_pad_all(o, v.v, sel); break;
            case SP_SHADOW: lv_obj_set_style_shadow_width(o, v.v, sel); break;
            case SP_ARC_COLOR:
                if (styleApplies(p, w.type)) lv_obj_set_style_arc_color(o, lv_color_hex((uint32_t)v.v), sel);
                break;
            case SP_ARC_WIDTH:   // 바탕과 값의 두께를 함께
                if (styleApplies(p, w.type)) {
                    lv_obj_set_style_arc_width(o, v.v, sel);
                    lv_obj_set_style_arc_width(o, v.v, (lv_style_selector_t)LV_PART_INDICATOR | (lv_style_selector_t)lvState(st));
                }
                break;
            }
        }
    }
}

Rect toRect(const lv_area_t& a) { return {a.x1, a.y1, a.x2, a.y2}; }

// 해상도와 테마
void prepare(const Document& d) {
    if (d.resW != g_w || d.resH != g_h || d.depth != g_depth) configure(d.resW, d.resH, d.depth);
    if (!g_themeInit || d.dark != g_themeDark || d.primary != g_themePrimary) {
        lv_theme_t* th = lv_theme_default_init(g_disp, lv_color_hex((uint32_t)d.primary),
                                               lv_palette_main(LV_PALETTE_RED), d.dark, LV_FONT_DEFAULT);
        lv_display_set_theme(g_disp, th);
        g_themeInit = true; g_themeDark = d.dark; g_themePrimary = d.primary;
    }
}

lv_obj_t* makeScreen(const Document& d, int si, const fontgen::Plan& pl, std::map<int, lv_obj_t*>& objs) {
    const Screen& s = d.screens[si];
    g_doc = &d;
    lv_obj_t* scr = lv_obj_create(nullptr);
    if (pl.base[si]) lv_obj_set_style_text_font(scr, fontFor(14), 0);
    if (s.bgSet) lv_obj_set_style_bg_color(scr, lv_color_hex((uint32_t)s.bg), 0);
    objs.clear();
    objs[0] = scr;
    for (const Widget& w : s.widgets) {
        auto it = objs.find(w.parent);
        lv_obj_t* o = createObj(w, it != objs.end() ? it->second : scr);
        objs[w.id] = o;
        applyCommon(s, w, o);
    }
    return scr;
}

// ── 시운전 ──
Document g_simDoc;
fontgen::Plan g_simPlan;
int g_simScr = 0;
lv_indev_t* g_indev = nullptr;
lv_point_t g_simPt = {0, 0};
bool g_simDown = false;
lv_obj_t* g_simTa = nullptr;
uint32_t g_simT0 = 0;
std::vector<lvhost::SimEvent> g_simLog;

void readCb(lv_indev_t*, lv_indev_data_t* d) {
    d->point = g_simPt;
    d->state = g_simDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

std::string valueText(const Widget& w, lv_obj_t* o) {
    char b[128] = "";
    switch (w.type) {
    case WType::Slider: snprintf(b, sizeof b, "%d", (int)lv_slider_get_value(o)); break;
    case WType::Arc: snprintf(b, sizeof b, "%d", (int)lv_arc_get_value(o)); break;
    case WType::Switch:
    case WType::Checkbox: return lv_obj_has_state(o, LV_STATE_CHECKED) ? "켬" : "끔";
    case WType::Dropdown: lv_dropdown_get_selected_str(o, b, sizeof b); break;
    case WType::Roller: lv_roller_get_selected_str(o, b, sizeof b); break;
    case WType::Textarea: return lv_textarea_get_text(o);
    default: break;
    }
    return b;
}

void simLoad(int si);

void simEventCb(lv_event_t* e) {
    if (!g_simOn) return;
    const lv_event_code_t lc = lv_event_get_code(e);
    static const lv_event_code_t map[EV_COUNT] = {LV_EVENT_CLICKED, LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_LONG_PRESSED,
                                                  LV_EVENT_VALUE_CHANGED, LV_EVENT_FOCUSED, LV_EVENT_DEFOCUSED};
    lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
    const int id = (int)(intptr_t)lv_event_get_user_data(e);
    const Widget* w = findWidget(g_simDoc.screens[g_simScr], id);
    if (!w) return;
    if (w->type == WType::Textarea) {
        if (lc == LV_EVENT_FOCUSED) g_simTa = o;
        if ((lc == LV_EVENT_DEFOCUSED || lc == LV_EVENT_DELETE) && g_simTa == o) g_simTa = nullptr;
    }
    int code = -1;
    for (int i = 0; i < EV_COUNT; i++) if (map[i] == lc) code = i;
    if (code < 0) return;
    std::string go;
    bool bound = false;
    const int from = g_simScr;
    auto push = [&](const std::string& cb, const std::string& g) {
        lvhost::SimEvent ev;
        ev.t = lvhost::simTime();
        ev.screen = from; ev.widget = id; ev.code = code; ev.name = w->name; ev.cb = cb; ev.go = g;
        ev.value = code == EV_VALUE_CHANGED ? valueText(*w, o) : "";
        const bool flow = w->type == WType::Slider || w->type == WType::Arc || w->type == WType::Roller || w->type == WType::Textarea;
        if (flow && !g_simLog.empty()) {   // 끄는 동안 이어지는 값 변화는 한 줄로
            lvhost::SimEvent& l = g_simLog.back();
            if (code == EV_VALUE_CHANGED && l.widget == id && l.code == code && l.screen == from && l.cb == cb && l.go == g && ev.t - l.t < 1.0) {
                l.t = ev.t; l.value = ev.value; l.count++;
                return;
            }
        }
        g_simLog.push_back(ev);
        if (g_simLog.size() > 400) g_simLog.erase(g_simLog.begin(), g_simLog.begin() + 100);
    };
    for (const EventBind& b : w->events)
        if (b.code == code) {
            bound = true;
            push(b.cb, b.go);
            if (!b.go.empty() && go.empty()) go = b.go;
        }
    if (!bound && ((code == EV_CLICKED && w->type == WType::Button) || code == EV_VALUE_CHANGED)) push("", "");
    if (!go.empty()) {
        const int t = screenByName(g_simDoc, go);
        if (t >= 0 && t != g_simScr) simLoad(t);
    }
}

// 내보낸 코드와 같은 방식: 새로 만들어 띄우고 지금 화면은 지운다
void simLoad(int si) {
    std::map<int, lv_obj_t*> objs;
    g_simTa = nullptr;
    g_simScr = si;
    lv_obj_t* scr = makeScreen(g_simDoc, si, g_simPlan, objs);
    for (const Widget& w : g_simDoc.screens[si].widgets) lv_obj_add_event_cb(objs[w.id], simEventCb, LV_EVENT_ALL, (void*)(intptr_t)w.id);
    g_objs.clear();
    g_scr = scr;
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
}
}  // namespace

namespace lvhost {

void init(int w, int h, int depth) {
    lv_init();
    lv_tick_set_cb(tickCb);
    g_disp = lv_display_create(w, h);
    lv_display_set_flush_cb(g_disp, flushCb);
    lv_display_add_event_cb(g_disp, refrStartCb, LV_EVENT_REFR_START, nullptr);
    smooth::init(sizeOfFont, glyphSource, bigImage);
    configure(w, h, depth);
}

void build(const Document& d, int si, int previewId, int previewState) {
    smooth::enable(false);   // 마지막 한 장만 매끈하게 그린다
    if (g_simOn) simStop();
    prepare(d);
    const fontgen::Plan pl = fontgen::plan(d);
    applyFonts(pl, d);
    g_gen++;
    lv_obj_t* scr = makeScreen(d, si, pl, g_objs);
    lv_obj_t* preview = nullptr;
    if (previewState != ST_DEFAULT) {
        auto it = g_objs.find(previewId);
        if (previewId && it != g_objs.end()) preview = it->second;
    }
    lv_screen_load(scr);
    if (g_scr) lv_obj_delete(g_scr);
    g_scr = scr;
    // 이제 아무 화면도 쓰지 않는 그림을 치운다
    g_images.erase(std::remove_if(g_images.begin(), g_images.end(), [](const std::unique_ptr<HostImage>& h) { return h->gen < g_gen - 2; }), g_images.end());

    // 상태 전환 애니메이션을 즉시 끝냄: 시계를 앞으로 돌리고 타이머를 한 번 돌린다
    g_tickOffset += 5000;
    lv_timer_handler();
    lv_obj_update_layout(scr);
    if (preview) {   // 장치에서처럼 평소 모습을 한 번 그린 다음에 상태를 바꾼다
        lv_refr_now(g_disp);
        lv_obj_add_state(preview, lvState(previewState));
        g_tickOffset += 5000;
        lv_timer_handler();
        lv_obj_update_layout(scr);
    }

    g_coords.clear();
    g_content.clear();
    for (auto& [id, o] : g_objs) {
        lv_area_t a, c;
        lv_obj_get_coords(o, &a);
        lv_obj_get_content_coords(o, &c);
        g_coords[id] = toRect(a);
        g_content[id] = toRect(c);
    }
    smooth::enable(g_view);
    lv_obj_invalidate(scr);
    lv_refr_now(g_disp);
}

void setView(float k) {
    g_view = k > 0.f;
    smooth::setScale(k);
    smooth::enable(g_view);
    if (!g_view) g_smoothReady = false;
}
void redraw() {
    if (!g_scr) return;
    lv_obj_invalidate(g_scr);
    lv_refr_now(g_disp);
}
bool smoothOn() { return g_view && g_smoothReady; }
const uint32_t* smoothPixels() { return smooth::pixels().data(); }
int smoothWidth() { return smooth::width(); }
int smoothHeight() { return smooth::height(); }

void manualClock() { g_manual = true; }
void update() {
    if (g_manual) g_manualMs += 1000.0 / 60.0 * g_speed;
    uint32_t now = tickCb();
    if (now - g_lastTimer >= 15) {
        lv_timer_handler();
        g_lastTimer = now;
    }
}

bool takeFrame() {
    if (!g_ready) return false;
    g_ready = false;
    return true;
}
const uint32_t* pixels() { return g_out.data(); }
int width() { return g_w; }
int height() { return g_h; }
bool animating() { return lv_anim_count_running() > 0; }
const std::map<int, Rect>& coords() { return g_coords; }
Rect contentRect(int id) {
    auto it = g_content.find(id);
    return it == g_content.end() ? Rect{0, 0, g_w - 1, g_h - 1} : it->second;
}

void simStart(const Document& d, int si) {
    g_simDoc = d;
    g_simLog.clear();
    prepare(g_simDoc);
    g_simPlan = fontgen::plan(g_simDoc);
    applyFonts(g_simPlan, g_simDoc);
    if (!g_indev) {
        g_indev = lv_indev_create();
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(g_indev, g_disp);
        lv_indev_set_read_cb(g_indev, readCb);
        lv_indev_set_mode(g_indev, LV_INDEV_MODE_EVENT);   // 화면을 그릴 때마다 직접 읽는다
    }
    g_simDown = false;
    g_simOn = true;
    g_speed = 1.0;
    g_simT0 = tickCb();
    smooth::enable(g_view);
    lv_indev_enable(g_indev, true);
    simLoad(si);
    lv_obj_update_layout(g_scr);
    redraw();
}
void simStop() {
    if (!g_simOn) return;
    g_simOn = false;
    g_simDown = false;
    g_simTa = nullptr;
    g_speed = 1.0;
    lv_indev_read(g_indev);
    lv_indev_reset(g_indev, nullptr);
    lv_indev_enable(g_indev, false);
    // 펼친 목록처럼 화면 밖(top layer)에 만들어진 것을 치운다
    lv_obj_clean(lv_display_get_layer_top(g_disp));
}
bool simActive() { return g_simOn; }
int simScreen() { return g_simScr; }
void simShow(int si) {
    if (!g_simOn || si < 0 || si >= (int)g_simDoc.screens.size()) return;
    g_simDown = false;
    lv_indev_reset(g_indev, nullptr);
    simLoad(si);
}
void simPointer(int x, int y, bool down) {
    if (!g_simOn) return;
    g_simPt.x = std::clamp(x, 0, g_w - 1);
    g_simPt.y = std::clamp(y, 0, g_h - 1);
    g_simDown = down;
    lv_indev_read(g_indev);
}
void simType(const std::string& u) { if (g_simTa) lv_textarea_add_text(g_simTa, u.c_str()); }
void simKey(int k) {
    if (!g_simTa) return;
    switch (k) {
    case SK_BACK: lv_textarea_delete_char(g_simTa); break;
    case SK_DEL: lv_textarea_delete_char_forward(g_simTa); break;
    case SK_ENTER:
        if (lv_textarea_get_one_line(g_simTa)) lv_obj_send_event(g_simTa, LV_EVENT_READY, nullptr);
        else lv_textarea_add_char(g_simTa, '\n');
        break;
    case SK_LEFT: lv_textarea_cursor_left(g_simTa); break;
    case SK_RIGHT: lv_textarea_cursor_right(g_simTa); break;
    case SK_HOME: lv_textarea_set_cursor_pos(g_simTa, 0); break;
    case SK_END: lv_textarea_set_cursor_pos(g_simTa, LV_TEXTAREA_CURSOR_LAST); break;
    }
}
bool simHas(uint32_t cp) {
    if (baseHas(cp)) return true;
    for (const KrFont& k : g_kr) if (k.on && std::find(k.cps.begin(), k.cps.end(), cp) != k.cps.end()) return true;
    return false;
}
bool simWantsText() { return g_simOn && g_simTa; }
void simSpeed(float k) { tickCb(); g_speed = k; }
double simTime() { return (tickCb() - g_simT0) / 1000.0; }
bool simMoving() { return g_simOn && (g_simDown || lv_anim_count_running() > 0 || lv_indev_get_scroll_obj(g_indev)); }
std::vector<SimEvent>& simLog() { return g_simLog; }

void thumbnail(const Document& d, int maxW, int maxH, std::vector<uint32_t>& out, int& w, int& h) {
    setView(0);
    build(d, 0, 0, ST_DEFAULT);
    g_ready = false;
    const double k = std::min({1.0, (double)maxW / g_w, (double)maxH / g_h});
    w = std::max(1, (int)(g_w * k + 0.5));
    h = std::max(1, (int)(g_h * k + 0.5));
    out.assign((size_t)w * h, 0);
    // 면적 평균으로 줄인다(작아도 글자가 뭉개지지 않게)
    for (int y = 0; y < h; y++) {
        const double y0 = (double)y * g_h / h, y1 = (double)(y + 1) * g_h / h;
        for (int x = 0; x < w; x++) {
            const double x0 = (double)x * g_w / w, x1 = (double)(x + 1) * g_w / w;
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = (int)y0; sy < g_h && sy < y1; sy++) {
                const double wy = std::min(y1, sy + 1.0) - std::max(y0, (double)sy);
                for (int sx = (int)x0; sx < g_w && sx < x1; sx++) {
                    const double wt = wy * (std::min(x1, sx + 1.0) - std::max(x0, (double)sx));
                    const uint32_t c = g_out[(size_t)sy * g_w + sx];
                    r += ((c >> 16) & 255) * wt; g += ((c >> 8) & 255) * wt; b += (c & 255) * wt; a += wt;
                }
            }
            uint32_t al = 255;
            if (d.round && d.resW == d.resH) {   // 원형 화면은 둥글게 오려 낸다
                const double cx = w * 0.5, cy = h * 0.5, rr = w * 0.5, dist = std::sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
                al = (uint32_t)(255 * std::clamp(rr - dist + 0.5, 0.0, 1.0));
            }
            out[(size_t)y * w + x] = (al << 24) | ((uint32_t)(r / a + 0.5) << 16) | ((uint32_t)(g / a + 0.5) << 8) | (uint32_t)(b / a + 0.5);
        }
    }
}

bool baseHas(uint32_t cp) {
    lv_font_glyph_dsc_t g = {};
    return lv_font_montserrat_14.get_glyph_dsc(&lv_font_montserrat_14, &g, cp, 0);
}

int styleValue(int id, int prop) {
    auto it = g_objs.find(id);
    if (it == g_objs.end()) return 0;
    lv_obj_t* o = it->second;
    const lv_part_t m = LV_PART_MAIN;
    switch (prop) {
    case SP_BG_COLOR: return (int)(lv_color_to_u32(lv_obj_get_style_bg_color(o, m)) & 0xFFFFFF);
    case SP_BG_OPA: return lv_obj_get_style_bg_opa(o, m);
    case SP_BORDER_COLOR: return (int)(lv_color_to_u32(lv_obj_get_style_border_color(o, m)) & 0xFFFFFF);
    case SP_BORDER_WIDTH: return lv_obj_get_style_border_width(o, m);
    case SP_RADIUS: return lv_obj_get_style_radius(o, m);
    case SP_TEXT_COLOR: return (int)(lv_color_to_u32(lv_obj_get_style_text_color(o, m)) & 0xFFFFFF);
    case SP_TEXT_FONT: return sizeOfFont(lv_obj_get_style_text_font(o, m));
    case SP_PAD: return lv_obj_get_style_pad_left(o, m);
    case SP_SHADOW: return lv_obj_get_style_shadow_width(o, m);
    case SP_ARC_COLOR: return (int)(lv_color_to_u32(lv_obj_get_style_arc_color(o, m)) & 0xFFFFFF);
    case SP_ARC_WIDTH: return lv_obj_get_style_arc_width(o, m);
    case SP_OPA: return lv_obj_get_style_opa(o, m);
    case SP_TEXT_OPA: return lv_obj_get_style_text_opa(o, m);
    case SP_LETTER: return lv_obj_get_style_text_letter_space(o, m);
    case SP_LINE: return lv_obj_get_style_text_line_space(o, m);
    case SP_IMG_OPA: return lv_obj_get_style_image_opa(o, m);
    case SP_IMG_TINT: return (int)(lv_color_to_u32(lv_obj_get_style_image_recolor(o, m)) & 0xFFFFFF);
    case SP_IMG_TINT_OPA: return lv_obj_get_style_image_recolor_opa(o, m);
    }
    return 0;
}

}  // namespace lvhost
