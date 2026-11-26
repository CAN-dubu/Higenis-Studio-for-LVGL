/* 글자 꾸밈: 그림 위에서도 라벨이 읽히게 그림자·외곽선·번짐을 더한다.
 * 움직임: 화면이 뜰 때 부품을 나타나게·밀려오게·커지게 하는 등의 애니메이션.
 * LVGL 9.1 이상의 공개 함수만 쓴다. Higenis Studio의 미리보기와 내보낸 코드가 이 파일을 함께 쓴다. */
#ifndef HIGENIS_FX_H
#define HIGENIS_FX_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* kind 1 그림자(dx, dy만큼 밀어서) · 2 외곽선(size 두께) · 3 번짐(size 반경으로 부드럽게) */
void higenis_fx(lv_obj_t * label, int kind, int size, int dx, int dy, uint32_t rgb, int opa);

/* 움직임 종류 */
enum {
    HIGENIS_MO_FADE_IN, HIGENIS_MO_SLIDE_IN, HIGENIS_MO_GROW, HIGENIS_MO_WIPE, HIGENIS_MO_TYPE,
    HIGENIS_MO_FADE_OUT, HIGENIS_MO_SLIDE_OUT, HIGENIS_MO_SHRINK,
    HIGENIS_MO_PULSE, HIGENIS_MO_BLINK, HIGENIS_MO_SPIN
};
/* 속도 곡선 */
enum { HIGENIS_EASE_LINEAR, HIGENIS_EASE_OUT, HIGENIS_EASE_IN_OUT, HIGENIS_EASE_OVERSHOOT, HIGENIS_EASE_BOUNCE };
/* 방향: 밀려오기는 어디서, 밀려나기는 어디로, 펼치기는 어디서부터 */
enum { HIGENIS_DIR_LEFT, HIGENIS_DIR_RIGHT, HIGENIS_DIR_UP, HIGENIS_DIR_DOWN };

/* 지금부터 delay ms 뒤에 dur ms 동안 움직인다. 화면을 만들 때 부르면 화면이 뜬 때부터 센다.
 * amount: 밀기는 거리(px), 커지기·작아지기는 시작·끝 크기(%), 두근두근은 부푸는 정도(%), 돌기는 각도(도)
 * repeat: 0 한 번, 1 계속 되풀이(돌기·깜빡이기 말고는 갔다가 돌아온다)
 * 들어오는 움직임(나타나기·밀려오기·커지기·펼치기·글자 치기)은 기다리는 동안 숨어 있다. */
void higenis_motion(lv_obj_t * obj, int kind, uint32_t delay, uint32_t dur, int ease, int dir, int amount, int repeat);

/* 화면 scr가 뜬 뒤 ms가 지나면 go()를 부른다(다음 화면으로 넘어가기). 그 전에 화면이 지워지면 부르지 않는다 */
void higenis_after(lv_obj_t * scr, uint32_t ms, void (*go)(void));

#ifdef __cplusplus
}
#endif

#endif
