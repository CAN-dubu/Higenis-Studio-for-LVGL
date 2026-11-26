// 그림: 파일 읽기(WIC), 크기 맞춤, LVGL 픽셀 형식으로 바꾸기.
// 미리보기와 내보내기가 같은 데이터를 쓰므로 두 결과의 픽셀이 같다.
#pragma once
#include "app.h"

namespace image {

// 파일을 읽어 도면에 담을 그림으로. 너무 크면 줄여서 담는다
std::shared_ptr<const ImageAsset> load(const std::wstring& path, const Document& d, std::string& err);
std::shared_ptr<const ImageAsset> fromBytes(std::vector<uint8_t> bytes, const std::string& name, const std::string& file);
std::string base64(const std::vector<uint8_t>& b);
std::vector<uint8_t> unbase64(const std::string& s);

enum Fit { FIT_COVER, FIT_CONTAIN, FIT_STRETCH, FIT_CENTER, FIT_COUNT };
const char* fitKo(int f);
// W×H 칸에 맞춘 그림 (BGRA, 곱하지 않은 알파)
std::vector<uint32_t> fitted(const std::shared_ptr<const ImageAsset>& a, int W, int H, int fit, double k = 1.0, const ImageFx& fx = ImageFx());

// 장치에 들어갈 데이터
struct Variant {
    int w = 0, h = 0, stride = 0, cf = 0;   // cf: LVGL 색 형식 번호
    bool alpha = false;
    std::vector<uint8_t> data;
    std::vector<uint8_t> bin() const;       // LVGL .bin 파일 (12바이트 머리 + 데이터)
    const char* cfName() const;             // "LV_COLOR_FORMAT_RGB565"
};
std::shared_ptr<const Variant> variant(const std::shared_ptr<const ImageAsset>& a, int W, int H, int fit, int depth, const ImageFx& fx = ImageFx());

// 부품에 대해
const std::shared_ptr<const ImageAsset>* find(const Document& d, const std::string& name);
void size(const Document& d, const Widget& w, int& W, int& H);       // 실제로 들어갈 크기
std::string sdPath(const Document& d, const Widget& w);               // "/higenis/logo_120x32.bin"
std::string ident(const Document& d, const Widget& w);                // "img_logo_120x32"
// 라벨 글자가 아래 그림과 구별되지 않는 넓이의 비율(그림 위가 아니거나 판이 있으면 0)
double hardToRead(const Document& d, const Screen& s, const Widget& w, const std::map<int, Rect>& coords);
inline bool onSd(const Widget& w) { return w.value == 1; }
inline int fit(const Widget& w) { return w.minv >= 0 && w.minv < FIT_COUNT ? w.minv : 0; }

}  // namespace image
