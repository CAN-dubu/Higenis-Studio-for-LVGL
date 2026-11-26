// 글자 윤곽을 픽셀 격자에 맞춰 그린다(자동 힌팅).
// 작은 크기에서 획이 픽셀 사이에 걸려 번지는 것을 막는다. 편집기 글자와 장치 글꼴이 함께 쓴다.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fonthint {
struct Font;
Font* newPack(float unitsPerEm, int ascent, int descent);      // 파일 없이 윤곽만 담는 글꼴. 글자 번호 = 문자
bool packAdd(Font* f, uint32_t cp, int advance, const std::vector<int16_t>& outline);
std::vector<int16_t> outline(const Font* f, int glyph);        // [종류 x y cx cy cx1 cy1] …
float unitsPerEm(const Font* f);
bool restricted(const Font* f);                                // 내장을 막아 둔 글꼴(OS/2 fsType)
std::wstring localName(const Font* f);                         // 글꼴 안의 한글 이름
Font* open(const void* data, int fontNo = 0);   // data는 닫을 때까지 살아 있어야 한다
void  close(Font* f);
int   glyphIndex(const Font* f, uint32_t cp);
int   glyphCount(const Font* f);
int   advance(const Font* f, int glyph);        // 글꼴 단위
void  vmetrics(const Font* f, int& ascent, int& descent, int& gap);
float scaleForEm(const Font* f, float px);
float scaleForHeight(const Font* f, float px);

enum { ALIGN_Y = 1, ALIGN_X = 2 };
struct Bitmap { int w = 0, h = 0, ox = 0, oy = 0; std::vector<uint8_t> a; };   // oy: 기준선에서 위가 음수
// unit이 0보다 크면 자리는 그대로 두고 획 굵기만 장치에서 나올 굵기(unit의 정수배)로 맞춘다.
// axes: 맞출 방향, grow: 획 굵기에 더하는 픽셀(맞춤 때), shiftX: 픽셀보다 작은 가로 이동
void raster(const Font* f, int glyph, float scale, int axes, float grow, float shiftX, Bitmap& out, float unit = 0.f);
}  // namespace fonthint
