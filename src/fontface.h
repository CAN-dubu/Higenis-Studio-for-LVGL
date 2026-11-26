// 글꼴 고르기: 함께 들어 있는 글꼴과 PC에 설치된 글꼴.
// 도면에는 쓴 글자의 윤곽만 담으므로 글꼴 파일이 없는 PC에서도 그대로 보인다.
#pragma once
#include "app.h"
#include "fontgen.h"
#include "fonthint.h"

struct FontAsset {
    int id = 0, index = 0;
    std::string name, key;                    // 보이는 이름 / "@파일"(함께 든 글꼴) 또는 전체 경로
    std::shared_ptr<fonthint::Font> file;     // 열린 글꼴 파일(없을 수 있음)
    std::shared_ptr<fonthint::Font> pack;     // 도면에 담긴 윤곽
    bool restricted = false;                  // 내장 제한 표시가 있는 글꼴
    const fonthint::Font* pick(uint32_t cp) const;
};

namespace fontface {

struct Bundled { const char* file; const char* name; };
extern const Bundled kBundled[];
extern const int kBundledCount;

void setDir(const std::wstring& fontsDir);
std::shared_ptr<FontAsset> open(const std::string& key, int index, const std::string& name);   // 파일이 없으면 file이 빈 채로
const std::shared_ptr<const FontAsset>* find(const Document& d, int id);
int add(Document& d, const std::string& key, int index, const std::string& name);              // 이미 있으면 그 번호

bool has(const FontAsset& f, uint32_t cp);
const fontgen::Glyph& glyph(const FontAsset& f, int size, uint32_t cp);
void metrics(const FontAsset& f, int size, int& lineHeight, int& baseLine);
const fontgen::Raster& raster(const FontAsset& f, int devSize, float k, uint32_t cp, int subX);
void clearRaster();

// 저장: 쓴 글자의 윤곽
std::string packLines(const FontAsset& f, const std::vector<uint32_t>& cps);
bool unpackLine(FontAsset& f, const std::string& cpHex, const std::string& adv, const std::string& data);

// PC에 설치된 글꼴
struct Installed { std::string name; std::wstring path; int index = 0; };
const std::vector<Installed>& installed();
// 견본 한 줄(8비트). 한글이 없으면 hangul=false. 글꼴 안의 한글 이름도 돌려준다
bool sample(const Installed& f, const std::string& text, int w, int h, float em, std::vector<uint8_t>& out, bool& hangul, std::string& koName);

}  // namespace fontface
