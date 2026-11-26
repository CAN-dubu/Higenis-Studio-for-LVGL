// 매끈하게 보기: LVGL이 정한 자리는 그대로 두고, 그리기만 화면 배율로 다시 한다.
// 장치 픽셀을 늘리지 않으므로 어느 척도에서나 글자와 기호의 윤곽이 또렷하다.
#pragma once
#include <cstdint>
#include <vector>
#include "lvgl.h"

struct FontAsset;

namespace smooth {
struct GlyphSource { const std::vector<uint32_t>* cps = nullptr; int size = 14; const FontAsset* face = nullptr; };   // face가 없으면 한글 기본 글꼴
void init(int (*fontSize)(const lv_font_t*), bool (*source)(const lv_font_t*, GlyphSource&),
          const uint32_t* (*image)(const void* src, int w, int h));
void setScale(float k);                // 0이면 끔
void enable(bool on);                  // 잠시 멈춤(비침·견본처럼 화면에 안 쓰는 렌더)
void begin(int w, int h, int depth);   // 한 프레임 시작(장치 해상도)
bool end();                            // 프레임 끝. 그렸으면 true
void forgetFonts();                    // 글꼴이 바뀌었다
const std::vector<uint32_t>& pixels();
int width();
int height();
}
