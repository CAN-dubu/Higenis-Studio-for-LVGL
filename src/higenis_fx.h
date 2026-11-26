/* 글자 꾸밈: 그림 위에서도 라벨이 읽히게 그림자·외곽선·번짐을 더한다.
 * LVGL 9.1 이상의 공개 함수만 쓴다. Higenis Studio의 미리보기와 내보낸 코드가 이 파일을 함께 쓴다. */
#ifndef HIGENIS_FX_H
#define HIGENIS_FX_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* kind 1 그림자(dx, dy만큼 밀어서) · 2 외곽선(size 두께) · 3 번짐(size 반경으로 부드럽게) */
void higenis_fx(lv_obj_t * label, int kind, int size, int dx, int dy, uint32_t rgb, int opa);

#ifdef __cplusplus
}
#endif

#endif
