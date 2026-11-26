/* Higenis Studio for LVGL — 글자 꾸밈(그림자·외곽선·번짐). 이 파일은 자유롭게 고쳐 써도 됩니다. */
#include "higenis_fx.h"

typedef struct {
    int kind, size, dx, dy;
    lv_color_t color;
    lv_opa_t opa;
} fx_t;

/* 원 둘레 16방향 (×64) */
static const int8_t ring[16][2] = {{64, 0}, {59, 24}, {45, 45}, {24, 59}, {0, 64}, {-24, 59}, {-45, 45}, {-59, 24},
    {-64, 0}, {-59, -24}, {-45, -45}, {-24, -59}, {0, -64}, {24, -59}, {45, -45}, {59, -24}};

static int round64(int v) { return v >= 0 ? (v + 32) / 64 : -((-v + 32) / 64); }

static void draw_at(lv_layer_t * layer, lv_draw_label_dsc_t * d, const lv_area_t * a, int x, int y)
{
    lv_area_t m = *a;
    m.x1 += x; m.x2 += x;
    m.y1 += y; m.y2 += y;
    lv_draw_label(layer, d, &m);
}

static void draw_fx(lv_obj_t * obj, lv_layer_t * layer, const fx_t * fx, bool text_too)
{
    lv_draw_label_dsc_t d;
    lv_area_t a;
    lv_draw_label_dsc_init(&d);
    d.base.layer = layer;
    lv_obj_init_draw_label_dsc(obj, LV_PART_MAIN, &d);
    d.text = lv_label_get_text(obj);
    lv_obj_get_content_coords(obj, &a);
    /* 자르기 라벨은 줄을 바꾸지 않고 제 칸 안에서만 그린다 */
    const lv_area_t clip_ori = layer->_clip_area;
    if (lv_label_get_long_mode(obj) == LV_LABEL_LONG_CLIP) {
        layer->_clip_area.x1 = LV_MAX(clip_ori.x1, a.x1);
        layer->_clip_area.y1 = LV_MAX(clip_ori.y1, a.y1);
        layer->_clip_area.x2 = LV_MIN(clip_ori.x2, a.x2);
        layer->_clip_area.y2 = LV_MIN(clip_ori.y2, a.y2);
        if (layer->_clip_area.x1 > layer->_clip_area.x2 || layer->_clip_area.y1 > layer->_clip_area.y2) {
            layer->_clip_area = clip_ori;
            return;
        }
        if (d.align != LV_TEXT_ALIGN_CENTER && d.align != LV_TEXT_ALIGN_RIGHT) a.x2 = a.x1 + 8000;
    }
    const lv_color_t color = d.color;
    const lv_opa_t opa = d.opa;
    d.color = fx->color;
    d.opa = (lv_opa_t)((int)fx->opa * opa / 255);
    if (fx->kind == 1) {
        draw_at(layer, &d, &a, fx->dx, fx->dy);
    }
    else if (fx->kind == 2) {
        const int step = fx->size <= 1 ? 2 : 1;   /* 얇으면 8방향이면 충분하다 */
        for (int r = fx->size; r > 0; r -= 2)
            for (int i = 0; i < 16; i += step) {
                int x = round64(ring[i][0] * r), y = round64(ring[i][1] * r);
                if (fx->size <= 1) { x = ring[i][0] > 20 ? 1 : ring[i][0] < -20 ? -1 : 0; y = ring[i][1] > 20 ? 1 : ring[i][1] < -20 ? -1 : 0; }
                draw_at(layer, &d, &a, x, y);
            }
    }
    else {
        /* 세 겹의 고리를 옅게 겹쳐 부드럽게 번지게 한다 */
        const int o = d.opa;
        for (int k = 3; k > 0; k--) {
            const int r64 = fx->size * 64 * k / 3;
            d.opa = (lv_opa_t)(o / (6 + k * 4) + 1);
            for (int i = 0; i < 16; i++) draw_at(layer, &d, &a, fx->dx + round64(ring[i][0] * r64 / 64), fx->dy + round64(ring[i][1] * r64 / 64));
        }
    }
    if (text_too) {
        d.color = color;
        d.opa = opa;
        draw_at(layer, &d, &a, 0, 0);
    }
    layer->_clip_area = clip_ori;
}

static void fx_event(lv_event_t * e)
{
    fx_t * fx = (fx_t *)lv_event_get_user_data(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_current_target(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        lv_free(fx);
        return;
    }
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        const int d = LV_MAX(LV_ABS(fx->dx), LV_ABS(fx->dy));
        lv_event_set_ext_draw_size(e, (fx->kind == 1 ? 0 : fx->size) + (fx->kind == 2 ? 0 : d) + 1);
        return;
    }
    /* 바탕이 없으면 글자보다 먼저, 있으면 바탕이 그려진 뒤에 그리고 글자를 다시 얹는다 */
    const bool plate = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) > LV_OPA_MIN;
    if (code == LV_EVENT_DRAW_MAIN_BEGIN && !plate) draw_fx(obj, lv_event_get_layer(e), fx, false);
    if (code == LV_EVENT_DRAW_MAIN && plate) draw_fx(obj, lv_event_get_layer(e), fx, true);
}

void higenis_fx(lv_obj_t * label, int kind, int size, int dx, int dy, uint32_t rgb, int opa)
{
    fx_t * fx = (fx_t *)lv_malloc(sizeof(fx_t));
    if (fx == NULL) return;
    fx->kind = kind;
    fx->size = size;
    fx->dx = dx;
    fx->dy = dy;
    fx->color = lv_color_hex(rgb);
    fx->opa = (lv_opa_t)opa;
    lv_obj_add_event_cb(label, fx_event, LV_EVENT_DRAW_MAIN_BEGIN, fx);
    lv_obj_add_event_cb(label, fx_event, LV_EVENT_DRAW_MAIN, fx);
    lv_obj_add_event_cb(label, fx_event, LV_EVENT_REFR_EXT_DRAW_SIZE, fx);
    lv_obj_add_event_cb(label, fx_event, LV_EVENT_DELETE, fx);
    lv_obj_refresh_ext_draw_size(label);
}
