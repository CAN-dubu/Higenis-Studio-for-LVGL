/* Higenis Studio for LVGL — 글자 꾸밈(그림자·외곽선·번짐)과 움직임. 이 파일은 자유롭게 고쳐 써도 됩니다. */
#include "higenis_fx.h"
#include <string.h>

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

/* ───────────────────────── 움직임 ───────────────────────── */

typedef struct {
    int kind, dir, amount;
    lv_opa_t base_opa;      /* 움직이기 전의 불투명도 */
    char * text;            /* 글자 치기: 전체 글 */
    uint32_t chars;         /* 글자 수(UTF-8) */
    bool hid;               /* 펼치기: 너무 얇아 잠시 감췄다 */
} motion_t;

#define MO_FULL 1024        /* 진행 정도: 0 → 1024 */
#define MO_MIN_SCALE 16     /* 이보다 작게(256 = 100 %) 줄이지 않는다. 아주 작은 배율은 변환 그리기에 무리가 된다 */

static void mo_exec(lv_anim_t * a, int32_t v);

static lv_opa_t mo_opa(const motion_t * m, int32_t v)
{
    if (v < 0) v = 0;
    if (v > MO_FULL) v = MO_FULL;
    return (lv_opa_t)((int32_t)m->base_opa * v / MO_FULL);
}

static void mo_translate(lv_obj_t * obj, int dir, int32_t d)
{
    if (dir == HIGENIS_DIR_LEFT || dir == HIGENIS_DIR_RIGHT) lv_obj_set_style_translate_x(obj, dir == HIGENIS_DIR_LEFT ? -d : d, 0);
    else lv_obj_set_style_translate_y(obj, dir == HIGENIS_DIR_UP ? -d : d, 0);
}

static void mo_scale(lv_obj_t * obj, int32_t pct1024)
{
    /* 256 = 100 %. 이만큼 작으면 거의 투명하다(커지기·작아지기는 함께 흐려진다) */
    int32_t s = 256 * pct1024 / (100 * MO_FULL);
    if (s < MO_MIN_SCALE) s = MO_MIN_SCALE;
    lv_obj_set_style_transform_scale_x(obj, s, 0);
    lv_obj_set_style_transform_scale_y(obj, s, 0);
}

/* UTF-8 앞에서 n글자의 바이트 수 */
static uint32_t mo_bytes(const char * t, uint32_t n)
{
    uint32_t i = 0;
    while (t[i] && n) {
        i++;
        while ((t[i] & 0xC0) == 0x80) i++;
        n--;
    }
    return i;
}

static void mo_exec(lv_anim_t * a, int32_t v)
{
    motion_t * m = (motion_t *)lv_anim_get_user_data(a);
    lv_obj_t * obj = (lv_obj_t *)a->var;
    switch (m->kind) {
    case HIGENIS_MO_FADE_IN: lv_obj_set_style_opa(obj, mo_opa(m, v), 0); break;
    case HIGENIS_MO_FADE_OUT: lv_obj_set_style_opa(obj, mo_opa(m, MO_FULL - v), 0); break;
    case HIGENIS_MO_SLIDE_IN:
        mo_translate(obj, m->dir, m->amount * (MO_FULL - v) / MO_FULL);
        lv_obj_set_style_opa(obj, mo_opa(m, v * 2), 0);   /* 앞쪽 절반에 또렷해진다 */
        break;
    case HIGENIS_MO_SLIDE_OUT:
        mo_translate(obj, m->dir, m->amount * v / MO_FULL);
        lv_obj_set_style_opa(obj, mo_opa(m, (MO_FULL - v) * 2), 0);
        break;
    case HIGENIS_MO_GROW:
        mo_scale(obj, m->amount * MO_FULL + (100 - m->amount) * v);
        lv_obj_set_style_opa(obj, mo_opa(m, v * 3), 0);
        break;
    case HIGENIS_MO_SHRINK:
        mo_scale(obj, 100 * MO_FULL - (100 - m->amount) * v);
        lv_obj_set_style_opa(obj, mo_opa(m, MO_FULL - v), 0);
        break;
    case HIGENIS_MO_WIPE: {
        int32_t s = 256 * v / MO_FULL;
        /* 아직 너무 얇으면 줄이지 말고 감춘다 */
        if (s < MO_MIN_SCALE) {
            s = MO_MIN_SCALE;
            if (!m->hid) { lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0); m->hid = true; }
        }
        else if (m->hid) { lv_obj_set_style_opa(obj, m->base_opa, 0); m->hid = false; }
        if (m->dir == HIGENIS_DIR_LEFT || m->dir == HIGENIS_DIR_RIGHT) lv_obj_set_style_transform_scale_x(obj, s, 0);
        else lv_obj_set_style_transform_scale_y(obj, s, 0);
        break;
    }
    case HIGENIS_MO_TYPE: {
        int32_t n = (int32_t)m->chars * v / MO_FULL;
        if (n < 0) n = 0;
        if (n > (int32_t)m->chars) n = (int32_t)m->chars;
        const uint32_t len = mo_bytes(m->text, (uint32_t)n);
        const char keep = m->text[len];
        m->text[len] = 0;
        if (strcmp(lv_label_get_text(obj), m->text) != 0) lv_label_set_text(obj, m->text);
        m->text[len] = keep;
        break;
    }
    case HIGENIS_MO_PULSE: mo_scale(obj, 100 * MO_FULL + m->amount * v); break;
    case HIGENIS_MO_BLINK: lv_obj_set_style_opa(obj, (v < MO_FULL / 2 || v >= MO_FULL) ? m->base_opa : LV_OPA_TRANSP, 0); break;
    case HIGENIS_MO_SPIN: lv_obj_set_style_transform_rotation(obj, (int32_t)m->amount * 10 * v / MO_FULL, 0); break;
    default: break;
    }
}

static void mo_deleted(lv_anim_t * a)
{
    motion_t * m = (motion_t *)lv_anim_get_user_data(a);
    if (m == NULL) return;
    if (m->text) lv_free(m->text);
    lv_free(m);
}

void higenis_motion(lv_obj_t * obj, int kind, uint32_t delay, uint32_t dur, int ease, int dir, int amount, int repeat)
{
    if (obj == NULL) return;
    if (kind == HIGENIS_MO_TYPE && !lv_obj_check_type(obj, &lv_label_class)) return;
    motion_t * m = (motion_t *)lv_malloc(sizeof(motion_t));
    if (m == NULL) return;
    lv_memzero(m, sizeof(motion_t));
    m->kind = kind;
    m->dir = dir;
    m->amount = amount;
    /* 앞서 건 움직임이 불투명도를 이미 바꿨을 수 있으니 그것이 기억한 값을 쓴다 */
    m->base_opa = lv_obj_get_style_opa(obj, LV_PART_MAIN);
    lv_anim_t * prev = lv_anim_get(obj, NULL);
    if (prev && prev->custom_exec_cb == mo_exec && lv_anim_get_user_data(prev)) m->base_opa = ((motion_t *)lv_anim_get_user_data(prev))->base_opa;
    if (kind == HIGENIS_MO_TYPE) {
        const char * t = lv_label_get_text(obj);
        const uint32_t len = (uint32_t)lv_strlen(t);
        m->text = (char *)lv_malloc(len + 1);
        if (m->text == NULL) { lv_free(m); return; }
        lv_memcpy(m->text, t, len + 1);
        for (uint32_t i = 0; i < len; i++) if ((t[i] & 0xC0) != 0x80) m->chars++;
    }
    /* 크기와 회전은 가운데를, 펼치기는 시작하는 쪽을 축으로 */
    if (kind == HIGENIS_MO_GROW || kind == HIGENIS_MO_SHRINK || kind == HIGENIS_MO_PULSE || kind == HIGENIS_MO_SPIN) {
        lv_obj_set_style_transform_pivot_x(obj, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(obj, lv_pct(50), 0);
    }
    if (kind == HIGENIS_MO_WIPE) {
        lv_obj_set_style_transform_pivot_x(obj, dir == HIGENIS_DIR_RIGHT ? lv_pct(100) : 0, 0);
        lv_obj_set_style_transform_pivot_y(obj, dir == HIGENIS_DIR_DOWN ? lv_pct(100) : 0, 0);
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_custom_exec_cb(&a, mo_exec);
    lv_anim_set_user_data(&a, m);
    lv_anim_set_deleted_cb(&a, mo_deleted);
    lv_anim_set_values(&a, 0, MO_FULL);
    lv_anim_set_duration(&a, dur ? dur : 1);
    lv_anim_set_delay(&a, delay);
    switch (ease) {
    case HIGENIS_EASE_OUT: lv_anim_set_path_cb(&a, lv_anim_path_ease_out); break;
    case HIGENIS_EASE_IN_OUT: lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out); break;
    case HIGENIS_EASE_OVERSHOOT: lv_anim_set_path_cb(&a, lv_anim_path_overshoot); break;
    case HIGENIS_EASE_BOUNCE: lv_anim_set_path_cb(&a, lv_anim_path_bounce); break;
    default: lv_anim_set_path_cb(&a, lv_anim_path_linear); break;
    }
    /* 들어오는 움직임만 기다리는 동안 첫 모습(숨김)을 미리 보인다 */
    const bool enter = kind <= HIGENIS_MO_TYPE;
    lv_anim_set_early_apply(&a, enter);
    if (repeat) {
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        if (kind != HIGENIS_MO_SPIN && kind != HIGENIS_MO_BLINK) {
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 3)
            lv_anim_set_reverse_duration(&a, dur);
#elif LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 2
            lv_anim_set_playback_duration(&a, dur);
#else
            lv_anim_set_playback_time(&a, dur);
#endif
        }
    }
    else if (kind == HIGENIS_MO_PULSE) {
        /* 한 번이면 부풀었다가 돌아온다 */
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 3)
        lv_anim_set_reverse_duration(&a, dur);
#elif LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 2
        lv_anim_set_playback_duration(&a, dur);
#else
        lv_anim_set_playback_time(&a, dur);
#endif
    }
    lv_anim_start(&a);
}

/* 다음 화면으로 넘어가기 */
typedef struct {
    lv_obj_t * scr;
    lv_timer_t * timer;
    void (*go)(void);
} after_t;

static void after_scr_deleted(lv_event_t * e)
{
    after_t * a = (after_t *)lv_event_get_user_data(e);
    lv_timer_delete(a->timer);
    lv_free(a);
}

static void after_fire(lv_timer_t * t)
{
    after_t * a = (after_t *)lv_timer_get_user_data(t);
    void (*go)(void) = a->go;
    lv_obj_remove_event_cb_with_user_data(a->scr, after_scr_deleted, a);
    lv_timer_delete(t);
    lv_free(a);
    go();   /* 보통 이 화면을 지우고 다음 화면을 띄운다 */
}

void higenis_after(lv_obj_t * scr, uint32_t ms, void (*go)(void))
{
    if (scr == NULL || go == NULL) return;
    after_t * a = (after_t *)lv_malloc(sizeof(after_t));
    if (a == NULL) return;
    a->scr = scr;
    a->go = go;
    a->timer = lv_timer_create(after_fire, ms ? ms : 1, a);
    lv_obj_add_event_cb(scr, after_scr_deleted, LV_EVENT_DELETE, a);
}
