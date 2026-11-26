// Higenis Studio for LVGL — LVGL 화면 제도판
// 공용 선언: 데이터 모델, 개정 이력, 검도, 내보내기, LVGL 호스트, 플랫폼
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ───────────────────────── 모델 ─────────────────────────
enum class WType : int {
    Container, Label, Button, Switch, Checkbox, Slider, Bar, Arc,
    Dropdown, Roller, Textarea, Led, Spinner, Image, Count
};

struct TypeInfo {
    const char* key;      // 저장/코드용 키: "button"
    const char* ko;       // 템플릿 표기: "버튼"
    const char* abbr;     // 부품표 약어: "btn"
    const char* creator;  // LVGL 생성 함수
    int defW, defH;
    bool canParent;       // 자식을 가질 수 있는가
    bool touch;           // 터치 대상(검도: 최소 크기)
};
const TypeInfo& typeInfo(WType t);
int typeFromKey(const char* key);

enum StateIdx { ST_DEFAULT, ST_PRESSED, ST_CHECKED, ST_FOCUSED, ST_DISABLED, ST_COUNT };
const char* stateKo(int s);
const char* stateLv(int s);
const char* stateKey(int s);

enum StyleProp { SP_BG_COLOR, SP_BG_OPA, SP_BORDER_COLOR, SP_BORDER_WIDTH, SP_RADIUS,
                 SP_TEXT_COLOR, SP_TEXT_FONT, SP_PAD, SP_SHADOW, SP_ARC_COLOR, SP_ARC_WIDTH,
                 SP_OPA, SP_TEXT_OPA, SP_FACE, SP_LETTER, SP_LINE, SP_TALIGN, SP_DECOR, SP_IMG_OPA, SP_IMG_TINT, SP_IMG_TINT_OPA, SP_COUNT };
enum StyleKind { SK_NUM, SK_COLOR, SK_PCT, SK_FONT, SK_FACE, SK_ALIGN, SK_DECOR };
struct StylePropInfo { const char* key; const char* ko; bool color; int minv, maxv; int kind = SK_NUM; };
const StylePropInfo& stylePropInfo(int p);
bool radiusOnIndicator(WType t);      // 모서리를 값 부분에도 함께 주는 형식
bool styleApplies(int p, WType t);   // 이 형식에 뜻이 있는 속성인가
bool styleShown(int p, WType t);     // 속성 칸을 보여 줄 형식인가
extern const int kFontSizes[];
extern const int kFontSizeCount;

// LVGL 내장 기호 (LV_SYMBOL_*)
struct SymbolInfo { const char* macro; uint32_t cp; const char* ko; };
extern const SymbolInfo kSymbols[];
extern const int kSymbolCount;
const SymbolInfo* symbolOf(uint32_t cp);

enum EventCode { EV_CLICKED, EV_PRESSED, EV_RELEASED, EV_LONG_PRESSED, EV_VALUE_CHANGED,
                 EV_FOCUSED, EV_DEFOCUSED, EV_COUNT };
const char* eventLv(int e);
const char* eventKey(int e);
const char* eventKo(int e);

struct StyleVal { bool set = false; int v = 0; };
struct EventBind { int code = EV_CLICKED; std::string cb, go; };   // go: 옮겨 갈 시트 이름(비면 없음)

// 그림을 장치에 넣기 전에 미리 손보는 것(실행 중 부담 없음)
struct ImageFx {
    int focusX = 50, focusY = 50;     // 채움일 때 보이는 자리(%)
    int blur = 0, bright = 0, sat = 0, shade = 0, shadeDir = 0, crisp = 0, cut = 0;
    bool operator==(const ImageFx& o) const {
        return focusX == o.focusX && focusY == o.focusY && blur == o.blur && bright == o.bright && sat == o.sat && shade == o.shade && shadeDir == o.shadeDir && crisp == o.crisp && cut == o.cut;
    }
};
// 형식에 따라 쓰는 덧붙임
struct Extra {
    int longMode = 0;                 // 라벨: 0 줄바꿈, 1 점줄임, 2 흐르기, 3 자르기
    int fx = 0, fxSize = 2, fxDx = 1, fxDy = 2, fxColor = 0x000000, fxOpa = 220;   // 글자 꾸밈
    ImageFx img;
};
struct ExtraField { const char* key; int& (*get)(Extra&); int minv, maxv; };

// 움직임: 시트가 뜬 때부터 delay ms 뒤에 dur ms 동안. 미리보기(도면)는 움직임 없이 놓인 모습, 시운전과 내보낸 코드에서 움직인다
enum MotionKind { MO_FADE_IN, MO_SLIDE_IN, MO_GROW, MO_WIPE, MO_TYPE, MO_FADE_OUT, MO_SLIDE_OUT, MO_SHRINK,
                  MO_PULSE, MO_BLINK, MO_SPIN, MO_COUNT };
enum MotionEase { ME_LINEAR, ME_OUT, ME_IN_OUT, ME_OVERSHOOT, ME_BOUNCE, ME_COUNT };
struct Motion {
    int kind = MO_FADE_IN;
    int delay = 0, dur = 500;
    int ease = ME_OUT;
    int dir = 3;                      // 0 왼쪽 1 오른쪽 2 위 3 아래 (밀려오기: 어디서, 밀려나기: 어디로, 펼치기: 어디서부터)
    int amount = 40;                  // 밀기: 거리 px, 커지기·작아지기: 시작 크기 %, 두근: 부푸는 %, 돌기: 도
    bool repeat = false;              // 계속 되풀이(돌기·깜빡이기 말고는 갔다가 돌아온다)
    bool operator==(const Motion& o) const {
        return kind == o.kind && delay == o.delay && dur == o.dur && ease == o.ease && dir == o.dir && amount == o.amount && repeat == o.repeat;
    }
};
struct MotionInfo { const char* key; const char* ko; const char* macro; bool enter, exit, loop, usesDir; const char* amountKo; int amountMin, amountMax, defAmount, defDur, defEase; };
const MotionInfo& motionInfo(int k);
const char* motionEaseKo(int e);
const char* motionEaseKey(int e);
const char* motionEaseMacro(int e);
const char* motionDirKo(int kind, int dir);
const char* motionDirMacro(int dir);
Motion defaultMotion(int kind);
int  motionEnd(const Motion& m);      // 끝나는 때(ms). 되풀이는 시작 + 한 번
bool motionApplies(int kind, WType t);
extern const ExtraField kExtraFields[];
extern const int kExtraFieldCount;

struct Widget {
    int id = 0, parent = 0;           // parent 0 = 화면
    WType type = WType::Container;
    std::string name;
    int x = 0, y = 0, w = 100, h = 40;
    int align = 0;                    // 0..8 : 좌상 중상 우상 / 좌중 중앙 우중 / 좌하 중하 우하
    bool autoSize = false;
    bool hidden = false;
    std::string text, text2;          // 글자 / 자리표시·선택지
    int value = 0, minv = 0, maxv = 100;
    bool checked = false;             // 스위치·체크박스: 체크, LED: 켜짐, 입력창: 한 줄, 아크: 게이지(손잡이 없음)
    int color = 0x2196F3;             // LED 색
    Extra x2;
    StyleVal style[ST_COUNT][SP_COUNT];
    std::vector<EventBind> events;
    std::vector<Motion> motions;
};

struct Screen {
    std::string name = "screen";
    bool bgSet = false;
    int bg = 0xFFFFFF;
    std::string next;                 // 저절로 넘어갈 시트(비면 없음)
    int nextMs = 0;                   // 뜬 뒤 몇 ms에
    std::vector<Widget> widgets;      // 전위 순서(부모 → 하위 트리), 같은 부모 안에서는 z 순서
};

struct FontAsset;                     // fontface.h

// 도면에 담은 그림. 원본 파일을 그대로 품는다
struct ImageAsset {
    std::string name, file;           // C 이름 / 원래 파일 이름
    int w = 0, h = 0, bytes = 0;
    bool alpha = false;
    std::vector<uint32_t> px;         // BGRA (곱하지 않은 알파)
    std::string b64;                  // 저장용 원본
};

// 아두이노 보드 설정. -1은 쓰지 않음
struct Board {
    int driver = 0, rotation = 1, mhz = 40;             // 0 TFT_eSPI, 1 Adafruit ILI9341, 2 Adafruit ST7789, 3 직접, 4 JD9165 MIPI-DSI(ESP32-P4)
    int sck = 14, mosi = 13, miso = 12, cs = 15, dc = 2, rst = -1, bl = 21, invert = 0;
    int touch = 0, tcs = 33, tirq = 36, tsck = 25, tmosi = 32, tmiso = 39;   // 0 없음, 1 XPT2046, 2 TFT_eSPI, 3 GT911(I²C)
    int tx0 = 200, tx1 = 3700, ty0 = 240, ty1 = 3800, tswap = 0;
    int tsda = 7, tscl = 8, trst = 22, tint = 21;        // GT911
    int buf = 0, dbl = 0, psram = 0, period = 33;
    int sd = 0, sdcs = 5, sdsck = 18, sdmosi = 23, sdmiso = 19, sdmhz = 20;
    int sdmmc = 0, sdclk = 43, sdcmd = 44, sdd0 = 39, sdd1 = 40, sdd2 = 41, sdd3 = 42;   // 1: SDMMC 4비트(ESP32-P4·S3)
};
struct BoardField { const char* key; int Board::*p; };
extern const BoardField kBoardFields[];
extern const int kBoardFieldCount;

struct Document {
    std::string project = "untitled";
    int resW = 480, resH = 272, depth = 16;
    bool dark = false;
    int primary = 0x2196F3;
    bool round = false;               // 원형 화면(가로세로가 같을 때)
    std::string about, credit;        // 표준도 설명과 참고 출처
    std::vector<std::shared_ptr<const ImageAsset>> images;
    std::vector<std::shared_ptr<const FontAsset>> fonts;
    Board board;
    std::vector<Screen> screens;
    int nextId = 1;
};

Document makeDemoDocument();
Document makeEmptyDocument();

int  widgetIndex(const Screen& s, int id);
Widget*       findWidget(Screen& s, int id);
const Widget* findWidget(const Screen& s, int id);
int  widgetDepth(const Screen& s, int id);
int  subtreeEnd(const Screen& s, int index);               // [index, end) 가 하위 트리
bool isAncestor(const Screen& s, int anc, int id);
bool isLastSibling(const Screen& s, int index);
std::string uniqueName(const Screen& s, const std::string& base, int ignoreId = 0);
bool validIdent(const std::string& s);
std::string sanitizeIdent(const std::string& s);
int  addWidget(Document& d, Screen& s, WType t, int parent, int x, int y, int w, int h);
void deleteWidget(Screen& s, int id);
int  duplicateWidget(Document& d, Screen& s, int id);
void renameSheetRefs(Document& d, const std::string& from, const std::string& to);   // 시트 이름이 바뀌면 이동 대상도
int  sheetMotionEnd(const Screen& s);   // 시트의 움직임이 모두 끝나는 때(ms)
int  screenByName(const Document& d, const std::string& name);
void moveZ(Screen& s, int id, int dir);                    // -2 맨 뒤, -1 뒤로, +1 앞으로, +2 맨 앞
void resolveFont(const Screen& s, const Widget& w, int state, int& size, int& face);   // 실제로 쓰일 글꼴(지정이 없으면 물려받은 것)
const char* alignLv(int a);
const char* alignKo(int a);
std::vector<const std::string*> renderedTexts(const Widget& w);   // 장치 화면에 실제로 그려지는 글자들
int  alignAnchor(int a, bool vertical, int parentSize, int size);

// ───────────────────────── 개정 이력 ─────────────────────────
struct Revision { std::string note; Document doc; int screen = 0; int sel = 0; std::string time; };
struct History {
    std::vector<Revision> revs;
    int cur = -1;
    int base = 0;        // 오래된 개정을 버려도 기호가 밀리지 않게
    int saved = 0;       // 파일에 저장된 개정(-1: 없음)
    std::string label(int i) const;
    void reset(const Document& d, int screen, int sel, const std::string& note);
    void commit(const Document& d, int screen, int sel, const std::string& note);
    bool canUndo() const { return cur > 0; }
    bool canRedo() const { return cur + 1 < (int)revs.size(); }
};
std::string revLabel(int index);   // 0 → A, 25 → Z, 26 → AA
std::string nowStamp();            // "11-24 18:30"
std::string todayStamp();          // "2026.11.24"

// ───────────────────────── 검도 / 저장 / 내보내기 ─────────────────────────
struct Rect { int x1 = 0, y1 = 0, x2 = -1, y2 = -1; };   // 포함 좌표(장치 px)
inline int rw(const Rect& r) { return r.x2 - r.x1 + 1; }
inline int rh(const Rect& r) { return r.y2 - r.y1 + 1; }

struct Issue { int widget = 0; bool error = false; std::string msg; };
std::vector<Issue> runLint(const Document& d, const Screen& s, const std::map<int, Rect>& coords);

std::string serializeDocument(const Document& d, bool withData = true);   // withData=false: 그림 내용은 주소로만(빠른 비교)
bool saveDocument(const Document& d, const std::wstring& path, std::string& err);
bool loadDocument(Document& d, const std::wstring& path, std::string& err);

// 표준도: assets/standards 의 도면들
struct Standard { std::wstring path; Document doc; };
std::vector<Standard> loadStandards(const std::wstring& dir);

struct GenFile { std::string name, text; bool binary = false, keep = false; };   // keep: 이미 있으면 덮어쓰지 않는다
std::string fontLicenseText();       // 글꼴과 함께 내보낼 OFL 전문
std::vector<GenFile> generateCode(const Document& d, const std::string& rev);
std::vector<GenFile> generateArduino(const Document& d, const std::string& rev);
std::string sketchName(const Document& d);
bool usesSd(const Document& d);
const char* boardDriverKo(int d);
const char* boardTouchKo(int t);
void boardPresetJC1060(Board& b);
const char* boardBufKo(int b);
int boardBufLines(const Document& d);
int writeFiles(const std::vector<GenFile>& files, const std::wstring& dir, int& kept);   // 쓴 파일 수
// 이번에 내보내지 않은 옛 파일(.c .h .cpp)을 휴지통으로. 시트·그림 이름이 바뀌면 남아서 컴파일을 깨뜨린다
int removeStale(const std::vector<GenFile>& files, const std::wstring& dir, std::vector<std::string>* gone = nullptr);
bool wantsClick(const Widget& w);      // 눌러도 반응이 없는 형식인데 사건을 걸었다

// ───────────────────────── LVGL 호스트 ─────────────────────────
namespace lvhost {
void init(int w, int h, int depth);
void build(const Document& d, int screenIndex, int previewId, int previewState);
void setMotionTime(int ms);          // 다음 build()만: 움직임을 ms 시점에 멈춘 모습으로 그린다(-1 움직임 없이)
void manualClock();                  // 입력 재생: 프레임마다 1/60초씩
void update();                       // 매 프레임: 타이머·애니메이션
bool takeFrame();                    // 새 픽셀이 있으면 true
const uint32_t* pixels();            // BGRA
int  width();
int  height();
void setView(float k);               // 매끈하게 보기의 배율(0이면 장치 픽셀 그대로)
void redraw();
bool smoothOn();
const uint32_t* smoothPixels();
int  smoothWidth();
int  smoothHeight();
bool animating();
const std::map<int, Rect>& coords(); // 위젯 id → 절대 좌표 (0 = 화면)
Rect contentRect(int id);            // 부모 기준 정렬용 콘텐츠 영역
// 도면의 첫 시트를 줄인 그림(BGRA). 화면 객체를 새로 만드므로 부른 뒤 build()를 다시 해야 한다
void thumbnail(const Document& d, int maxW, int maxH, std::vector<uint32_t>& out, int& w, int& h);
bool baseHas(uint32_t cp);           // LVGL 기본 글꼴(Montserrat)에 있는 글자인가
int  styleValue(int id, int prop);   // 현재 미리보기 상태에서 실제 적용 값

// 시운전: 도면을 살아 있는 LVGL 화면으로 돌린다
struct SimEvent {
    double t = 0;                    // 시운전 시계(초)
    int screen = 0, widget = 0, code = 0, count = 1;
    std::string name, cb, go, value;
};
void simStart(const Document& d, int screen);
void simStop();
bool simActive();
void simShow(int screen);            // 시트를 처음 상태로 띄운다
int  simScreen();
void simPointer(int x, int y, bool down);
void simType(const std::string& utf8);
enum SimKey { SK_BACK, SK_DEL, SK_ENTER, SK_LEFT, SK_RIGHT, SK_HOME, SK_END };
void simKey(int k);
bool simHas(uint32_t cp);            // 장치 글꼴에 담긴 글자인가
bool simWantsText();                 // 글자를 받을 입력창이 있는가
void simSpeed(float k);
double simTime();
bool simMoving();                    // 애니메이션이나 스크롤이 남았는가
std::vector<SimEvent>& simLog();
}

// ───────────────────────── 플랫폼 ─────────────────────────
void platformUploadCanvas(const uint32_t* bgra, int w, int h, int prescale, int slot = 0);   // 0 화면, 1 비침, 2 표준도 견본, 3 그림 견본, 4 글꼴 견본
void* platformCanvasTexture(int slot = 0);
std::wstring platformAssetsDir();
void platformSetTitle(const std::wstring& t);
bool platformSaveDialog(std::wstring& path);
bool platformOpenDialog(std::wstring& path);
bool platformOpenImage(std::wstring& path);
bool platformOpenFont(std::wstring& path);
void platformOpenNotices();            // 라이선스 고지 파일을 연다
bool platformPickFolder(std::wstring& path, const wchar_t* title);
void platformRequestClose();
bool replaceFile(const std::wstring& from, const std::wstring& to);
std::wstring utf8ToWide(const std::string& s);
std::string  wideToUtf8(const std::wstring& s);
