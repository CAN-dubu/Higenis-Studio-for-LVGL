// UI 공용: 도면 팔레트, 글꼴, 앱 상태, 제도 도구
#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include "app.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

// ── 팔레트: 트레이싱지 · 흑연 · 빨간 연필 · 무사진 청색 ──
namespace col {
constexpr ImU32 paper = IM_COL32(0xEC, 0xE8, 0xDE, 255);    // 제도판(패널)
constexpr ImU32 sheet = IM_COL32(0xF6, 0xF3, 0xEC, 255);    // 도면 시트
constexpr ImU32 plate = IM_COL32(0xE2, 0xDD, 0xCF, 255);    // 템플릿 자 판
constexpr ImU32 ink = IM_COL32(0x2A, 0x29, 0x26, 255);      // 흑연
constexpr ImU32 ink2 = IM_COL32(0x50, 0x4C, 0x46, 255);     // 보조 글자 (7:1)
constexpr ImU32 ink3 = IM_COL32(0x6C, 0x67, 0x5D, 255);     // 희미한 글자 (4.6:1)
constexpr ImU32 hair = IM_COL32(0xCF, 0xC9, 0xBB, 255);     // 가는 선
constexpr ImU32 hair2 = IM_COL32(0xB3, 0xAC, 0x9D, 255);    // 조금 진한 선
constexpr ImU32 hover = IM_COL32(0xE3, 0xDE, 0xD2, 255);
constexpr ImU32 press = IM_COL32(0xD8, 0xD2, 0xC4, 255);
constexpr ImU32 red = IM_COL32(0xC8, 0x37, 0x2D, 255);      // 빨간 연필: 선택·치수·주의
constexpr ImU32 redWash = IM_COL32(0xC8, 0x37, 0x2D, 26);
constexpr ImU32 blue = IM_COL32(0x6F, 0xA8, 0xDC, 255);     // 무사진 청색: 가이드·스냅
constexpr ImU32 blueInk = IM_COL32(0x2F, 0x6F, 0xA8, 255);
constexpr ImU32 blueWash = IM_COL32(0x6F, 0xA8, 0xDC, 40);
}  // namespace col

struct Fonts {
    ImFont* ui = nullptr;    // Barlow Condensed + IBM Plex Sans KR
    ImFont* med = nullptr;   // Barlow Condensed Medium + IBM Plex Sans KR Medium
    ImFont* mono = nullptr;  // Chivo Mono + IBM Plex Sans KR
    ImFont* head = nullptr;  // IBM Plex Sans KR Light: 섹션 머리
};
extern Fonts F;

enum class Drag { None, Move, Resize, Create, Pan, Template };
struct Guide { bool vertical; float pos, a, b; };   // 장치 px

struct App {
    Document doc;
    History hist;
    int screen = 0, sel = 0, hover = 0;
    int editState = ST_DEFAULT;
    std::wstring path;
    bool dirty = false;

    // 보기
    float zoom = 2.f, zoomTarget = 2.f;
    int scaleIdx = 3;
    ImVec2 org{0, 0}, zoomAnchor{0, 0}, lastCanvas{0, 0};
    bool needFit = true;

    // 상호작용
    Drag drag = Drag::None;
    int handle = -1;
    ImVec2 mouse0{0, 0};
    Widget w0;
    Rect r0, cont0;
    bool moved = false;
    int armed = -1;          // 템플릿에서 고른 형식(그리기 도구)
    int tplType = -1;        // 템플릿에서 끌고 있는 형식
    ImVec2 create0{0, 0};    // 장치 좌표
    std::vector<Guide> guides;
    bool snapGrid = true;
    int grid = 8;
    bool balloons = false;
    bool trace = false;      // 비침: 다른 시트를 청색 밑그림으로
    int prevScreen = -1;     // 직전에 보던 시트
    std::string traceKey;    // 비침 텍스처가 어느 개정의 것인지

    // 화면 상태
    bool needsBuild = true;
    int focusName = 0;
    int renameId = 0;
    std::string renameBuf;
    bool openExport = false, openLint = false, openRevs = false, openStd = false;
    int stdSel = 0;              // 표준도 창에서 고른 것
    int pendingStd = 0;
    int pending = 0;
    ImVec2 lintAnchor{0, 0};
    std::vector<GenFile> gen;
    int genTab = 0;
    bool openAbout = false;      // 프로그램 정보
    bool openFonts = false;      // PC 글꼴 고르기 창
    int fontFor = 0, fontState = 0;
    bool fontsPending = false;   // 견본을 아직 그리는 중
    int genMode = 0;             // 0 C 코드, 1 아두이노
    double stampT = -10;
    std::vector<Issue> issues;
    int lintHover = 0;
    bool lintOpen = false;
    double selAnimT = 0, flashSheetT = -10;
    int animSel = -1;
    std::string toast;
    double toastT = -10;
    bool live = false;           // 편집 중이라 아직 개정으로 남기지 않은 변경이 있다
    std::string liveNote;
    int liveSel = 0;
    int nameEdit = 0, sheetEdit = -1;   // 이름을 고치는 중인 부품 / 시트
    std::string nameBefore;
    std::string mergeKey;
    double revAnimT = -10;                       // 새 개정이 표에 써지는 연출
    struct Ghost { Rect r; double t; };
    std::vector<Ghost> ghosts;                   // 지운 자리의 연필 자국
    // 시운전
    bool sim = false, simTouch = true, simSlow = false;
    int simBack = 0;                             // 시운전을 시작한 시트
    int simFrameNo = -1;                         // 시운전을 끝낸 프레임
    double simBusy = -10;                        // 이때까지는 계속 그린다
    bool simDown = false;
    ImVec2 simPt{0, 0};

    int texPre = 1;                              // 텍스처 선배율(3:2 척도용)
    bool pixelView = false;                      // 장치 픽셀 그대로 보기
    float viewK = -1.f;                          // 지금 텍스처를 그린 배율(0: 장치 픽셀)
    bool openScale = false;
    double mergeT = 0;

    float S = 1.f;
    double now = 0;
    float dt = 0.016f;

    // 캡처(자체 점검용)
    bool fakeAlt = false;
};
extern App g;
inline float px(float v) { return v * g.S; }

// ── 동작 ──
Screen& curScreen();
Widget* selWidget();
int  widgetNumber(int id);
void commit(const std::string& note);
void flushLive();                // 편집 중이던 변경을 개정으로 남긴다
void commitMerge(const std::string& note, const std::string& key);
void undo();
void redo();
void gotoRevision(int i);
void selectWidget(int id);
void switchScreen(int i);
void rebuildNow();
int  traceSheet();          // 비쳐 볼 시트(-1: 없음)
void toggleTrace();
void fitView();
void setScale(int idx, ImVec2 anchorScreen);
ImVec2 canvasCenter();
ImVec2 canvasTitleLeft();   // 표제란의 왼쪽 끝
ImVec2 canvasDevToScreen(float x, float y);
bool canvasMouseDev(ImVec2& out);
void requestAction(int a);   // 1 닫기, 2 새 도면, 3 열기, 4 표준도로 시작 — 저장 안 된 개정이 있으면 확인
int  standardCount();
void startStandard(int i);
bool addStandardSheet(int i);
void newDocument();
void openDocumentDialog();
bool saveCurrent(bool saveAs);
void openExportDialog();
void dropFile(const std::wstring& path, ImVec2 screen);   // 창에 놓은 파일
void showToast(const std::string& t);
void deleteSelected();
void duplicateSelected();
bool moveSubtree(Screen& s, int id, int target, int mode);  // 0 앞, 1 뒤, 2 안으로
extern const float kScales[];
extern const char* kScaleTxt[];
extern const int kScaleCount;

// ── 제도 도구 / 컨트롤 ──
ImU32 alpha(ImU32 c, float a);
float easeOut(float t);
float textW(ImFont* f, float size, const char* s, float tracking = 0.f);
float textH(ImFont* f, float size);
void  pushFont(ImFont* f, float size);   // size = em(px)
std::string ellipsize(ImFont* f, float size, const std::string& s, float maxw);
void  textAt(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const char* s, float tracking = 0.f);
void  textRot(ImDrawList* dl, ImFont* f, float size, ImVec2 center, ImU32 c, const char* s, float angle);
void  rotateVerts(ImDrawList* dl, int start, ImVec2 center, float angle);
void  hline(ImDrawList* dl, float x0, float x1, float y, ImU32 c, float th = 1.f);   // 픽셀에 맞춘 실선
void  vline(ImDrawList* dl, float x, float y0, float y1, ImU32 c, float th = 1.f);
void  dashLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float dash, float gap, float th = 1.f);
void  dashRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float dash, float gap);
void  arrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, ImU32 c);
void  dimH(ImDrawList* dl, float x1, float x2, float y, float extFrom, const char* txt, ImU32 c, float t);
void  dimV(ImDrawList* dl, float y1, float y2, float x, float extFrom, const char* txt, ImU32 c, float t);
void  revisionCloud(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float r, float th);
void  stamp(ImDrawList* dl, ImVec2 center, float r, const char* top, const char* bottom, float angle, float a);

void  sectionHeader(const char* num, const char* title, const char* aside = nullptr);
void  fieldLabel(const char* label, bool mark = false);
int   scrubInt(const char* id, const char* label, int* v, int vmin, int vmax, float width, bool dim = false);
bool  lineButton(const char* label, ImVec2 size, int kind = 0, const char* hint = nullptr);
bool  textLink(const char* label, ImU32 c = col::ink2);
int   colorField(const char* id, int* rgb, bool set, int themeVal, float width);
bool  segTabs(const char* id, const char* const* items, int n, int* sel, const bool* marks = nullptr);
bool  toggleField(const char* id, bool* v);
bool  alignPicker(const char* id, int* a);
int   fieldText(const char* id, std::string* s, float width, const char* hint = nullptr);   // 1 편집 중, 2 확정
int   fieldMultiline(const char* id, std::string* s, float width, float height);
bool  comboField(const char* id, const char* preview, float width);
void  tooltip(const char* text, const char* shortcut = nullptr);
void  styleImGui();

// ── 화면 영역 ──
const ImFontLoader* vellumFontLoader();
void uiInit(float dpi, const std::string& assetsDir);
void uiFrame();
bool uiWantsAnimation();
void drawCanvas(ImVec2 pos, ImVec2 size);
void drawTopBar(ImVec2 pos, ImVec2 size);
void drawLeft(ImVec2 pos, ImVec2 size);
void drawRight(ImVec2 pos, ImVec2 size);
void drawStatus(ImVec2 pos, ImVec2 size);
void drawModals();

// ── 시운전 (ui_sim.cpp) ──
void simToggle();
void simFrame();                                    // 매 프레임: 단축키와 글자 입력
void simCanvas(ImDrawList* dl, bool hover, ImVec2 dev, ImVec2 devA, ImVec2 devB, float zoom);   // 누름 전달과 손가락 자국
void drawSimTop(ImVec2 pos, ImVec2 size);
void drawSimLeft(ImVec2 pos, ImVec2 size);
void drawSimRight(ImVec2 pos, ImVec2 size);
void drawSimStatus(ImVec2 pos, ImVec2 size);
void drawScaleBox(float& rx, float cy);
bool statusToggle(const char* id, const char* label, bool on, const char* tip, const char* key);
