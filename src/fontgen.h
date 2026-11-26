// 글꼴 생성: 도면에 쓴 글자 중 기본 글꼴(Montserrat)에 없는 것만 뽑아 LVGL 비트맵 글꼴로 만든다.
// 미리보기(lvhost)와 내보내기(codegen)가 같은 데이터를 쓰므로 두 결과의 픽셀이 같다.
#pragma once
#include "app.h"

namespace fontgen {

bool load(const std::wstring& path);          // IBM Plex Sans KR (TTF)
std::vector<char>& data();                    // 읽어 둔 TTF (화면 글꼴과 함께 쓴다)
bool has(uint32_t cp);                        // 이 글꼴로 만들 수 있는 글자인가

struct Glyph {
    uint32_t cp = 0;
    int adv16 = 0;                            // 너비 × 16
    int w = 0, h = 0, ox = 0, oy = 0;         // oy: 기준선에서 상자 아래까지(위가 +)
    std::vector<uint8_t> bits;                // 4bpp, 줄 사이 채움 없이 이어 붙임
};
const Glyph& glyph(int size, uint32_t cp);    // 캐시됨
void metrics(int size, int& lineHeight, int& baseLine);

// 매끈하게 보기용: 화면 배율의 글자 그림. 장치 글꼴과 같은 윤곽이다
bool loadView(const std::wstring& montserrat, const std::wstring& symbols);
struct Raster { int w = 0, h = 0, ox = 0, oy = 0; std::vector<uint8_t> a; };   // oy: 기준선에서 위가 음수
const Raster& raster(bool kr, int size, float k, uint32_t cp, int subX);   // subX: 0~3 (1/4 픽셀)
void clearRaster();

struct Plan {
    std::map<int, std::vector<uint32_t>> sizes;   // 글꼴 크기 → 담을 글자(오름차순)
    std::map<std::pair<int, int>, std::vector<uint32_t>> faces;   // (글꼴 번호, 크기) → 담을 글자
    std::vector<bool> base;                       // 시트별: 화면의 기본 글꼴(14)도 바꿔야 하는가
    bool uses(int size) const { return sizes.count(size) != 0; }
    bool usesFace(int face, int size) const { return faces.count({face, size}) != 0; }
    bool any() const { return !sizes.empty() || !faces.empty(); }
    int  bytes(int size) const;                   // 장치 플래시에 들어갈 크기
};
int faceBytes(const Document& d, int face, int size, const std::vector<uint32_t>& cps);
Plan plan(const Document& d);

std::vector<uint32_t> decode(const std::string& utf8);
std::string encode(uint32_t cp);
int countExtra(const std::string& utf8);      // 글꼴에 따로 담아야 하는 글자 수(중복 제외)

}  // namespace fontgen
