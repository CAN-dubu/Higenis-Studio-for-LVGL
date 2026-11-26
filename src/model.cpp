// 모델: 위젯 트리, 데모 도면, 개정 이력, 검도, 저장/불러오기
#include "app.h"
#include "fontface.h"
#include "image.h"
#include "fontgen.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>

// ───────────────────────── 형식 정보 ─────────────────────────
static const TypeInfo kTypes[] = {
    {"container", "컨테이너", "obj", "lv_obj_create", 160, 100, true, false},
    {"label", "라벨", "lbl", "lv_label_create", 80, 20, false, false},
    {"button", "버튼", "btn", "lv_button_create", 100, 40, false, true},
    {"switch", "스위치", "sw", "lv_switch_create", 56, 28, false, true},
    {"checkbox", "체크박스", "cb", "lv_checkbox_create", 120, 24, false, true},
    {"slider", "슬라이더", "sl", "lv_slider_create", 160, 8, false, true},
    {"bar", "막대", "bar", "lv_bar_create", 160, 12, false, false},
    {"arc", "아크", "arc", "lv_arc_create", 120, 120, false, true},
    {"dropdown", "드롭다운", "dd", "lv_dropdown_create", 140, 40, false, true},
    {"roller", "롤러", "rol", "lv_roller_create", 100, 100, false, true},
    {"textarea", "입력창", "ta", "lv_textarea_create", 180, 40, false, true},
    {"led", "LED", "led", "lv_led_create", 16, 16, false, false},
    {"spinner", "스피너", "spn", "lv_spinner_create", 48, 48, false, false},
    {"image", "이미지", "img", "lv_image_create", 64, 64, false, false},
};
const TypeInfo& typeInfo(WType t) { return kTypes[(int)t]; }
int typeFromKey(const char* key) {
    for (int i = 0; i < (int)WType::Count; i++)
        if (!strcmp(kTypes[i].key, key)) return i;
    return -1;
}

static const char* kStateKo[] = {"기본", "눌림", "체크", "포커스", "비활성"};
static const char* kStateLv[] = {"LV_STATE_DEFAULT", "LV_STATE_PRESSED", "LV_STATE_CHECKED",
                                 "LV_STATE_FOCUSED", "LV_STATE_DISABLED"};
static const char* kStateKey[] = {"default", "pressed", "checked", "focused", "disabled"};
const char* stateKo(int s) { return kStateKo[s]; }
const char* stateLv(int s) { return kStateLv[s]; }
const char* stateKey(int s) { return kStateKey[s]; }

static const StylePropInfo kProps[] = {
    {"bg_color", "배경색", true, 0, 0xFFFFFF, SK_COLOR},
    {"bg_opa", "배경 불투명도", false, 0, 255, SK_PCT},
    {"border_color", "테두리 색", true, 0, 0xFFFFFF, SK_COLOR},
    {"border_width", "테두리 두께", false, 0, 32},
    {"radius", "모서리 반경", false, 0, 999},
    {"text_color", "글자색", true, 0, 0xFFFFFF, SK_COLOR},
    {"text_font", "글자 크기", false, 10, 48, SK_FONT},
    {"pad_all", "안쪽 여백", false, 0, 128},
    {"shadow_width", "그림자", false, 0, 64},
    {"arc_color", "호 바탕색", true, 0, 0xFFFFFF, SK_COLOR},
    {"arc_width", "호 두께", false, 1, 80},
    {"opa", "전체 불투명도", false, 0, 255, SK_PCT},
    {"text_opa", "글자 불투명도", false, 0, 255, SK_PCT},
    {"font_face", "글꼴", false, 0, 9999, SK_FACE},
    {"letter_space", "글자 사이", false, -8, 60},
    {"line_space", "줄 사이", false, -20, 80},
    {"text_align", "정렬", false, 0, 3, SK_ALIGN},
    {"text_decor", "밑줄·취소선", false, 0, 3, SK_DECOR},
    {"image_opa", "그림 불투명도", false, 0, 255, SK_PCT},
    {"image_tint", "물들일 색", true, 0, 0xFFFFFF, SK_COLOR},
    {"image_tint_opa", "물들이기", false, 0, 255, SK_PCT},
};
const StylePropInfo& stylePropInfo(int p) { return kProps[p]; }
bool styleShown(int p, WType t) {
    if (!styleApplies(p, t)) return false;
    switch (p) {
    case SP_TEXT_COLOR: case SP_TEXT_FONT: case SP_TEXT_OPA: case SP_FACE: case SP_LETTER: case SP_LINE: case SP_TALIGN: case SP_DECOR:
        return t == WType::Label || t == WType::Button || t == WType::Checkbox || t == WType::Dropdown || t == WType::Roller || t == WType::Textarea || t == WType::Container;
    case SP_IMG_OPA: case SP_IMG_TINT: case SP_IMG_TINT_OPA: return t == WType::Image;
    default: return true;
    }
}
#define XF(key, field, a, b) {key, [](Extra& x) -> int& { return x.field; }, a, b}
const ExtraField kExtraFields[] = {
    XF("long", longMode, 0, 3), XF("fx", fx, 0, 3), XF("fx_size", fxSize, 1, 12), XF("fx_dx", fxDx, -20, 20), XF("fx_dy", fxDy, -20, 20), XF("fx_color", fxColor, 0, 0xFFFFFF), XF("fx_opa", fxOpa, 0, 255),
    XF("focus_x", img.focusX, 0, 100), XF("focus_y", img.focusY, 0, 100), XF("blur", img.blur, 0, 40), XF("bright", img.bright, -100, 100), XF("sat", img.sat, -100, 100),
    XF("shade", img.shade, 0, 100), XF("shade_dir", img.shadeDir, 0, 2), XF("crisp", img.crisp, 0, 1), XF("cut", img.cut, 0, 100)};
#undef XF
const int kExtraFieldCount = (int)(sizeof kExtraFields / sizeof kExtraFields[0]);

void resolveFont(const Screen& s, const Widget& w, int state, int& size, int& face) {
    size = 0;
    face = -1;
    const Widget* p = &w;
    int st = state;
    while (p) {
        if (size <= 0) {
            if (p->style[st][SP_TEXT_FONT].set) size = p->style[st][SP_TEXT_FONT].v;
            else if (p->style[ST_DEFAULT][SP_TEXT_FONT].set) size = p->style[ST_DEFAULT][SP_TEXT_FONT].v;
        }
        if (face < 0) {
            if (p->style[st][SP_FACE].set) face = p->style[st][SP_FACE].v;
            else if (p->style[ST_DEFAULT][SP_FACE].set) face = p->style[ST_DEFAULT][SP_FACE].v;
        }
        if (size > 0 && face >= 0) return;
        st = ST_DEFAULT;
        p = p->parent ? findWidget(s, p->parent) : nullptr;
    }
    if (size <= 0) size = 14;
    if (face < 0) face = 0;
}
bool radiusOnIndicator(WType t) { return t == WType::Bar || t == WType::Slider; }
bool styleApplies(int p, WType t) {
    if (p == SP_ARC_COLOR || p == SP_ARC_WIDTH) return t == WType::Arc || t == WType::Spinner;
    return true;
}
const int kFontSizes[] = {10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48};
const int kFontSizeCount = 12;

const SymbolInfo kSymbols[] = {
    {"LV_SYMBOL_HOME", 0xF015, "집"}, {"LV_SYMBOL_POWER", 0xF011, "전원"}, {"LV_SYMBOL_SETTINGS", 0xF013, "설정"},
    {"LV_SYMBOL_BARS", 0xF0C9, "메뉴"}, {"LV_SYMBOL_LIST", 0xF00B, "목록"}, {"LV_SYMBOL_PLUS", 0xF067, "더하기"},
    {"LV_SYMBOL_MINUS", 0xF068, "빼기"}, {"LV_SYMBOL_OK", 0xF00C, "확인"}, {"LV_SYMBOL_CLOSE", 0xF00D, "닫기"},
    {"LV_SYMBOL_WARNING", 0xF071, "주의"}, {"LV_SYMBOL_UP", 0xF077, "위"}, {"LV_SYMBOL_DOWN", 0xF078, "아래"},
    {"LV_SYMBOL_LEFT", 0xF053, "왼쪽"}, {"LV_SYMBOL_RIGHT", 0xF054, "오른쪽"}, {"LV_SYMBOL_REFRESH", 0xF021, "새로 고침"},
    {"LV_SYMBOL_LOOP", 0xF079, "반복"}, {"LV_SYMBOL_PLAY", 0xF04B, "재생"}, {"LV_SYMBOL_PAUSE", 0xF04C, "일시 정지"},
    {"LV_SYMBOL_STOP", 0xF04D, "정지"}, {"LV_SYMBOL_PREV", 0xF048, "이전"}, {"LV_SYMBOL_NEXT", 0xF051, "다음"},
    {"LV_SYMBOL_EJECT", 0xF052, "꺼내기"}, {"LV_SYMBOL_SHUFFLE", 0xF074, "섞기"}, {"LV_SYMBOL_AUDIO", 0xF001, "음악"},
    {"LV_SYMBOL_VIDEO", 0xF008, "영상"}, {"LV_SYMBOL_IMAGE", 0xF03E, "그림"}, {"LV_SYMBOL_MUTE", 0xF026, "음소거"},
    {"LV_SYMBOL_VOLUME_MID", 0xF027, "음량 중간"}, {"LV_SYMBOL_VOLUME_MAX", 0xF028, "음량 크게"}, {"LV_SYMBOL_BELL", 0xF0F3, "알림"},
    {"LV_SYMBOL_CALL", 0xF095, "전화"}, {"LV_SYMBOL_ENVELOPE", 0xF0E0, "편지"}, {"LV_SYMBOL_WIFI", 0xF1EB, "와이파이"},
    {"LV_SYMBOL_BLUETOOTH", 0xF293, "블루투스"}, {"LV_SYMBOL_USB", 0xF287, "USB"}, {"LV_SYMBOL_GPS", 0xF124, "위치"},
    {"LV_SYMBOL_CHARGE", 0xF0E7, "전기"}, {"LV_SYMBOL_BATTERY_FULL", 0xF240, "배터리 가득"}, {"LV_SYMBOL_BATTERY_3", 0xF241, "배터리 3"},
    {"LV_SYMBOL_BATTERY_2", 0xF242, "배터리 2"}, {"LV_SYMBOL_BATTERY_1", 0xF243, "배터리 1"}, {"LV_SYMBOL_BATTERY_EMPTY", 0xF244, "배터리 없음"},
    {"LV_SYMBOL_TINT", 0xF043, "물방울"}, {"LV_SYMBOL_EYE_OPEN", 0xF06E, "보기"}, {"LV_SYMBOL_EYE_CLOSE", 0xF070, "숨기기"},
    {"LV_SYMBOL_FILE", 0xF15B, "파일"}, {"LV_SYMBOL_DIRECTORY", 0xF07B, "폴더"}, {"LV_SYMBOL_SAVE", 0xF0C7, "저장"},
    {"LV_SYMBOL_COPY", 0xF0C5, "복사"}, {"LV_SYMBOL_PASTE", 0xF0EA, "붙여넣기"}, {"LV_SYMBOL_CUT", 0xF0C4, "자르기"},
    {"LV_SYMBOL_EDIT", 0xF304, "편집"}, {"LV_SYMBOL_TRASH", 0xF2ED, "휴지통"}, {"LV_SYMBOL_UPLOAD", 0xF093, "올리기"},
    {"LV_SYMBOL_DOWNLOAD", 0xF019, "내리기"}, {"LV_SYMBOL_DRIVE", 0xF01C, "드라이브"}, {"LV_SYMBOL_SD_CARD", 0xF7C2, "SD 카드"},
    {"LV_SYMBOL_KEYBOARD", 0xF11C, "자판"}, {"LV_SYMBOL_BACKSPACE", 0xF55A, "지우기"},
};
const int kSymbolCount = (int)(sizeof kSymbols / sizeof kSymbols[0]);
const SymbolInfo* symbolOf(uint32_t cp) {
    for (int i = 0; i < kSymbolCount; i++) if (kSymbols[i].cp == cp) return &kSymbols[i];
    return nullptr;
}

static const char* kEvLv[] = {"LV_EVENT_CLICKED", "LV_EVENT_PRESSED", "LV_EVENT_RELEASED",
                              "LV_EVENT_LONG_PRESSED", "LV_EVENT_VALUE_CHANGED", "LV_EVENT_FOCUSED",
                              "LV_EVENT_DEFOCUSED"};
static const char* kEvKey[] = {"clicked", "pressed", "released", "long_pressed", "value_changed",
                               "focused", "defocused"};
static const char* kEvKo[] = {"클릭", "누름", "뗌", "길게 누름", "값 변경", "포커스", "포커스 해제"};
const char* eventLv(int e) { return kEvLv[e]; }
const char* eventKey(int e) { return kEvKey[e]; }
const char* eventKo(int e) { return kEvKo[e]; }

static const char* kAlignLv[] = {"LV_ALIGN_TOP_LEFT", "LV_ALIGN_TOP_MID", "LV_ALIGN_TOP_RIGHT",
                                 "LV_ALIGN_LEFT_MID", "LV_ALIGN_CENTER", "LV_ALIGN_RIGHT_MID",
                                 "LV_ALIGN_BOTTOM_LEFT", "LV_ALIGN_BOTTOM_MID", "LV_ALIGN_BOTTOM_RIGHT"};
static const char* kAlignKo[] = {"왼쪽 위", "가운데 위", "오른쪽 위", "왼쪽 가운데", "정가운데",
                                 "오른쪽 가운데", "왼쪽 아래", "가운데 아래", "오른쪽 아래"};
const char* alignLv(int a) { return kAlignLv[a]; }
const char* alignKo(int a) { return kAlignKo[a]; }
std::vector<const std::string*> renderedTexts(const Widget& w) {
    switch (w.type) {
    case WType::Label: case WType::Button: case WType::Checkbox: return {&w.text};
    case WType::Dropdown: case WType::Roller: return {&w.text2};
    case WType::Textarea: return {&w.text, &w.text2};
    default: return {};   // 이미지의 글자는 C 이름이라 화면에 나오지 않는다
    }
}
int alignAnchor(int a, bool vertical, int parentSize, int size) {
    int k = vertical ? a / 3 : a % 3;
    if (k == 0) return 0;
    if (k == 1) return (parentSize - size) / 2;
    return parentSize - size;
}

// ───────────────────────── 트리 조작 ─────────────────────────
int widgetIndex(const Screen& s, int id) {
    for (int i = 0; i < (int)s.widgets.size(); i++)
        if (s.widgets[i].id == id) return i;
    return -1;
}
Widget* findWidget(Screen& s, int id) {
    int i = widgetIndex(s, id);
    return i < 0 ? nullptr : &s.widgets[i];
}
const Widget* findWidget(const Screen& s, int id) {
    int i = widgetIndex(s, id);
    return i < 0 ? nullptr : &s.widgets[i];
}
int widgetDepth(const Screen& s, int id) {
    int d = 0;
    const Widget* w = findWidget(s, id);
    while (w && w->parent) { d++; w = findWidget(s, w->parent); }
    return d;
}
bool isAncestor(const Screen& s, int anc, int id) {
    const Widget* w = findWidget(s, id);
    while (w && w->parent) {
        if (w->parent == anc) return true;
        w = findWidget(s, w->parent);
    }
    return false;
}
int subtreeEnd(const Screen& s, int index) {
    int id = s.widgets[index].id;
    int e = index + 1;
    while (e < (int)s.widgets.size() && isAncestor(s, id, s.widgets[e].id)) e++;
    return e;
}
bool isLastSibling(const Screen& s, int index) {
    int p = s.widgets[index].parent;
    for (int i = subtreeEnd(s, index); i < (int)s.widgets.size(); i++)
        if (s.widgets[i].parent == p) return false;
    return true;
}
bool validIdent(const std::string& s) {
    if (s.empty() || (!isalpha((unsigned char)s[0]) && s[0] != '_')) return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_') return false;
    static const std::set<std::string> kw = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
        "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed",
        "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while"};
    return !kw.count(s);
}
// C 식별자로 다듬는다. 쓸 수 있는 글자가 하나도 없으면(예: 한글만) 빈 문자열
std::string sanitizeIdent(const std::string& in) {
    std::string o;
    bool any = false;
    for (char c : in) {
        if (isalnum((unsigned char)c)) { o += (char)tolower((unsigned char)c); any = true; }
        else if (c == '_') o += c;
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (o.size() > 1 && o.back() == '_' && (in.empty() || in.back() != '_')) o.pop_back();
    if (!any) return "";
    if (isdigit((unsigned char)o[0])) o = "s_" + o;
    if (!validIdent(o)) o += "_";
    return o;
}
std::string uniqueName(const Screen& s, const std::string& base, int ignoreId) {
    auto taken = [&](const std::string& n) {
        for (auto& w : s.widgets)
            if (w.id != ignoreId && w.name == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; i++) {
        std::string n = base + "_" + std::to_string(i);
        if (!taken(n)) return n;
    }
}

static void applyTypeDefaults(Widget& w) {
    switch (w.type) {
    case WType::Label: w.text = "Label"; w.autoSize = true; break;
    case WType::Button: w.text = "Button"; break;
    case WType::Checkbox: w.text = "Option"; w.autoSize = true; break;
    case WType::Slider: case WType::Bar: w.value = 40; break;
    case WType::Arc: w.value = 60; break;
    case WType::Dropdown: w.text2 = "Option 1\nOption 2\nOption 3"; break;
    case WType::Roller: w.text2 = "00\n15\n30\n45"; break;
    case WType::Textarea: w.text2 = "Type here"; w.checked = true; break;
    case WType::Led: w.checked = true; break;
    default: break;
    }
}

int addWidget(Document& d, Screen& s, WType t, int parent, int x, int y, int w, int h) {
    Widget nw;
    nw.id = d.nextId++;
    nw.parent = parent;
    nw.type = t;
    for (int k = 1;; k++) {   // btn_1, btn_2 … 시트 안에서 비어 있는 가장 작은 번호
        nw.name = std::string(typeInfo(t).abbr) + "_" + std::to_string(k);
        if (uniqueName(s, nw.name) == nw.name) break;
    }
    nw.x = x; nw.y = y; nw.w = w; nw.h = h;
    applyTypeDefaults(nw);
    int at = (int)s.widgets.size();
    if (parent) {
        int pi = widgetIndex(s, parent);
        if (pi >= 0) at = subtreeEnd(s, pi);
    }
    s.widgets.insert(s.widgets.begin() + at, nw);
    return nw.id;
}

void deleteWidget(Screen& s, int id) {
    int i = widgetIndex(s, id);
    if (i < 0) return;
    s.widgets.erase(s.widgets.begin() + i, s.widgets.begin() + subtreeEnd(s, i));
}

int duplicateWidget(Document& d, Screen& s, int id) {
    int i = widgetIndex(s, id);
    if (i < 0) return 0;
    int e = subtreeEnd(s, i);
    std::vector<Widget> copy(s.widgets.begin() + i, s.widgets.begin() + e);
    std::map<int, int> remap;
    for (auto& w : copy) remap[w.id] = d.nextId++;
    for (auto& w : copy) {
        w.id = remap[w.id];
        if (remap.count(w.parent)) w.parent = remap[w.parent];
        w.name = uniqueName(s, w.name);
        s.widgets.push_back(w);  // 이름 충돌 검사를 위해 임시로 뒤에 추가
    }
    s.widgets.resize(s.widgets.size() - copy.size());
    copy[0].x += 12; copy[0].y += 12;
    s.widgets.insert(s.widgets.begin() + e, copy.begin(), copy.end());
    return copy[0].id;
}

void moveZ(Screen& s, int id, int dir) {
    int i = widgetIndex(s, id);
    if (i < 0) return;
    int p = s.widgets[i].parent;
    std::vector<int> sib;  // 같은 부모의 형제 시작 인덱스
    for (int k = 0; k < (int)s.widgets.size(); k++)
        if (s.widgets[k].parent == p) sib.push_back(k);
    int pos = (int)(std::find(sib.begin(), sib.end(), i) - sib.begin());
    int target = pos;
    if (dir == -1) target = std::max(0, pos - 1);
    if (dir == 1) target = std::min((int)sib.size() - 1, pos + 1);
    if (dir == -2) target = 0;
    if (dir == 2) target = (int)sib.size() - 1;
    if (target == pos) return;
    // 하위 트리 묶음 단위로 재배열
    std::vector<std::vector<Widget>> blocks;
    for (int k : sib) blocks.emplace_back(s.widgets.begin() + k, s.widgets.begin() + subtreeEnd(s, k));
    auto blk = blocks[pos];
    blocks.erase(blocks.begin() + pos);
    blocks.insert(blocks.begin() + target, blk);
    int start = sib.front();
    int end = subtreeEnd(s, sib.back());
    std::vector<Widget> flat;
    for (auto& b : blocks) flat.insert(flat.end(), b.begin(), b.end());
    // 형제 사이에 끼어 있던 다른 항목은 없음(전위 순서) → 그대로 교체
    s.widgets.erase(s.widgets.begin() + start, s.widgets.begin() + end);
    s.widgets.insert(s.widgets.begin() + start, flat.begin(), flat.end());
}

// ───────────────────────── 데모 도면 ─────────────────────────
static Widget& put(Document& d, Screen& s, WType t, const char* name, int parent, int x, int y, int w, int h) {
    int id = addWidget(d, s, t, parent, x, y, w, h);
    Widget* wd = findWidget(s, id);
    wd->name = name;
    return *wd;
}
static void setStyle(Widget& w, int st, int prop, int v) { w.style[st][prop] = {true, v}; }

Document makeDemoDocument() {
    Document d;
    d.project = "온도 조절기";
    d.screens.push_back({});
    Screen& s = d.screens[0];
    s.name = "home";
    {
        Widget& w = put(d, s, WType::Label, "lbl_title", 0, 20, 16, 0, 0);
        w.text = "거실"; w.autoSize = true;
        setStyle(w, ST_DEFAULT, SP_TEXT_FONT, 20);
    }
    {
        Widget& w = put(d, s, WType::Label, "lbl_time", 0, -20, 20, 0, 0);
        w.text = "21:40"; w.autoSize = true; w.align = 2;
        setStyle(w, ST_DEFAULT, SP_TEXT_COLOR, 0x6B7280);
        setStyle(w, ST_DEFAULT, SP_TEXT_FONT, 16);
    }
    {
        Widget& w = put(d, s, WType::Arc, "arc_temp", 0, 20, 64, 150, 150);
        w.minv = 10; w.maxv = 30; w.value = 22;
        w.events.push_back({EV_VALUE_CHANGED, "on_temp_changed"});
    }
    {
        Widget& w = put(d, s, WType::Label, "lbl_temp", 0, -145, 3, 0, 0);
        w.text = "22.5°"; w.autoSize = true; w.align = 4;
        setStyle(w, ST_DEFAULT, SP_TEXT_FONT, 28);
    }
    {
        Widget& w = put(d, s, WType::Label, "lbl_mode", 0, 196, 66, 0, 0);
        w.text = "운전 모드"; w.autoSize = true;
        setStyle(w, ST_DEFAULT, SP_TEXT_COLOR, 0x6B7280);
    }
    {
        Widget& w = put(d, s, WType::Dropdown, "dd_mode", 0, 196, 88, 150, 40);
        w.text2 = "자동\n냉방\n난방";
        w.events.push_back({EV_VALUE_CHANGED, "on_mode_changed"});
    }
    {
        Widget& w = put(d, s, WType::Label, "lbl_eco", 0, 372, 66, 0, 0);
        w.text = "절전"; w.autoSize = true;
        setStyle(w, ST_DEFAULT, SP_TEXT_COLOR, 0x6B7280);
    }
    {
        Widget& w = put(d, s, WType::Switch, "sw_eco", 0, 372, 94, 56, 28);
        w.checked = true;
    }
    {
        Widget& w = put(d, s, WType::Label, "lbl_fan", 0, 196, 146, 0, 0);
        w.text = "바람 세기"; w.autoSize = true;
        setStyle(w, ST_DEFAULT, SP_TEXT_COLOR, 0x6B7280);
    }
    {
        Widget& w = put(d, s, WType::Slider, "sl_fan", 0, 200, 176, 256, 8);
        w.value = 60;
    }
    {
        Widget& w = put(d, s, WType::Button, "btn_schedule", 0, 196, 212, 140, 40);
        w.text = "예약";
        setStyle(w, ST_DEFAULT, SP_BG_COLOR, 0xFFFFFF);
        setStyle(w, ST_DEFAULT, SP_BORDER_COLOR, 0x2196F3);
        setStyle(w, ST_DEFAULT, SP_BORDER_WIDTH, 2);
        setStyle(w, ST_DEFAULT, SP_TEXT_COLOR, 0x2196F3);
        w.events.push_back({EV_CLICKED, "", "settings"});
    }
    {
        Widget& w = put(d, s, WType::Button, "btn_start", 0, 356, 212, 100, 40);
        w.text = "시작";
        setStyle(w, ST_PRESSED, SP_BG_COLOR, 0x0D47A1);
        w.events.push_back({EV_CLICKED, "on_start"});
    }

    d.screens.push_back({});
    Screen& t = d.screens[1];
    t.name = "settings";
    {
        Widget& w = put(d, t, WType::Label, "lbl_title", 0, 20, 16, 0, 0);
        w.text = "설정"; w.autoSize = true;
        setStyle(w, ST_DEFAULT, SP_TEXT_FONT, 20);
    }
    {
        Widget& c = put(d, t, WType::Container, "cont_wifi", 0, 20, 56, 268, 120);
        (void)c;
        int cid = c.id;
        Widget& l = put(d, t, WType::Label, "lbl_wifi", cid, 0, 0, 0, 0);
        l.text = "Wi-Fi 네트워크"; l.autoSize = true;
        Widget& ta = put(d, t, WType::Textarea, "ta_ssid", cid, 0, 28, 228, 40);
        ta.text2 = "네트워크 이름"; ta.checked = true;
    }
    {
        Widget& w = put(d, t, WType::Checkbox, "cb_update", 0, 20, 196, 0, 0);
        w.text = "자동 업데이트"; w.autoSize = true; w.checked = true;
    }
    {
        Widget& w = put(d, t, WType::Roller, "rol_bright", 0, 316, 56, 140, 120);
        w.text2 = "25%\n50%\n75%\n100%"; w.value = 2;
    }
    {
        Widget& w = put(d, t, WType::Bar, "bar_battery", 0, 316, 200, 140, 12);
        w.value = 72;
    }
    {
        Widget& w = put(d, t, WType::Led, "led_link", 0, -24, 24, 14, 14);
        w.align = 2; w.color = 0x4CAF50;
    }
    {
        Widget& w = put(d, t, WType::Button, "btn_back", 0, 316, 222, 140, 36);
        w.text = "돌아가기";
        w.events.push_back({EV_CLICKED, "", "home"});
    }
    return d;
}

Document makeEmptyDocument() {
    Document d;
    d.project = "untitled";
    d.screens.push_back({});
    d.screens[0].name = "main";
    return d;
}

// ───────────────────────── 개정 이력 ─────────────────────────
std::string revLabel(int i) {
    std::string s;
    i += 1;
    while (i > 0) { int r = (i - 1) % 26; s.insert(s.begin(), (char)('A' + r)); i = (i - 1) / 26; }
    return s;
}
std::string nowStamp() {
    time_t t = time(nullptr);
    tm lt; localtime_s(&lt, &t);
    char b[32]; snprintf(b, sizeof b, "%02d-%02d %02d:%02d", lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    return b;
}
std::string todayStamp() {
    time_t t = time(nullptr);
    tm lt; localtime_s(&lt, &t);
    char b[32]; snprintf(b, sizeof b, "%04d.%02d.%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return b;
}
std::string History::label(int i) const { return i < 0 ? std::string() : revLabel(base + i); }
void History::reset(const Document& d, int screen, int sel, const std::string& note) {
    revs.clear();
    revs.push_back({note, d, screen, sel, nowStamp()});
    cur = base = saved = 0;
}
void History::commit(const Document& d, int screen, int sel, const std::string& note) {
    if (cur + 1 < (int)revs.size()) {
        revs.resize(cur + 1);
        if (saved > cur) saved = -1;   // 저장했던 개정이 갈라져 사라짐
    }
    revs.push_back({note, d, screen, sel, nowStamp()});
    if (revs.size() > 400) {
        revs.erase(revs.begin());
        base++;
        saved = saved < 0 ? -1 : saved - 1;
    }
    cur = (int)revs.size() - 1;
}

// ───────────────────────── 검도 ─────────────────────────
std::vector<Issue> runLint(const Document& d, const Screen& s, const std::map<int, Rect>& coords) {
    std::vector<Issue> out;
    std::set<std::string> names;
    auto rectOf = [&](int id) -> const Rect* {
        auto it = coords.find(id);
        return it == coords.end() ? nullptr : &it->second;
    };
    for (auto& w : s.widgets) {
        const TypeInfo& ti = typeInfo(w.type);
        if (!validIdent(w.name))
            out.push_back({w.id, true, "이름이 C 식별자가 아님"});
        else if (names.count(w.name))
            out.push_back({w.id, true, "이름 중복"});
        names.insert(w.name);

        const Rect* r = rectOf(w.id);
        const Rect* p = rectOf(w.parent);
        if (r && p && !w.hidden) {
            int over = std::max({p->x1 - r->x1, r->x2 - p->x2, p->y1 - r->y1, r->y2 - p->y2});
            if (over > 0)
                out.push_back({w.id, true, (w.parent ? "부모 밖으로 " : "화면 밖으로 ") + std::to_string(over) + "px 나감"});
        }
        if (r && ti.touch && !w.hidden && !(w.type == WType::Arc && w.checked)) {
            int m = std::min(rw(*r), rh(*r));
            int need = (w.type == WType::Slider) ? 0 : 32;
            if (m < need)
                out.push_back({w.id, false, "터치 영역 " + std::to_string(m) + "px — 32px 이상 권장"});
        }
        // 기본 글꼴에도, 함께 내보낼 한글 글꼴에도 없는 글자
        // 원형 화면: 네 귀가 원 밖이면 보이지 않는다
        if (d.round && d.resW == d.resH && r && !w.hidden && w.type != WType::Arc && w.type != WType::Spinner && w.type != WType::Led) {
            const bool disc = w.type == WType::Container && w.style[ST_DEFAULT][SP_RADIUS].set && w.style[ST_DEFAULT][SP_RADIUS].v * 2 >= std::min(rw(*r), rh(*r));
            const double c = d.resW * 0.5;
            const double dx = std::max(std::abs(r->x1 - c), std::abs(r->x2 + 1 - c)), dy = std::max(std::abs(r->y1 - c), std::abs(r->y2 + 1 - c));
            if (!disc && dx * dx + dy * dy > (c + 1) * (c + 1)) out.push_back({w.id, false, "원형 화면 밖으로 모서리가 잘림"});
        }
        if (w.type == WType::Image && !w.hidden) {
            const bool have = image::find(d, w.text) != nullptr;
            if (!have && w.text.empty() && w.text2.empty()) out.push_back({w.id, false, "그림이 없음 — 파일을 끌어다 놓거나 불러오세요"});
            if (have && !image::onSd(w)) {
                int iw, ih;
                image::size(d, w, iw, ih);
                const int kb = iw * ih * (d.depth == 16 ? 2 : 4) / 1024;
                if (kb > 512) out.push_back({w.id, false, "그림이 플래시 " + std::to_string(kb) + " KB를 차지함 — SD 카드에 두는 것도 좋음"});
            }
        }
        int fsz, fid;
        resolveFont(s, w, ST_DEFAULT, fsz, fid);
        const auto* face = fontface::find(d, fid);
        if (face && !(*face)->file && !renderedTexts(w).empty()) {   // 이 PC에 없는 글꼴
            bool lack = false;
            for (const std::string* t : renderedTexts(w)) for (uint32_t cp : fontgen::decode(*t)) if (cp > 0x20 && !fontface::has(**face, cp) && !symbolOf(cp)) lack = true;
            if (lack) out.push_back({w.id, false, "글꼴 " + (*face)->name + "이(가) 이 PC에 없어 새 글자는 기본 글꼴로 나옴"});
        }
        if (w.type == WType::Label && !w.hidden && r && w.x2.fx < 2) {   // 그림 위 글자가 읽히는가
            const double share = image::hardToRead(d, s, w, coords);
            if (share > 0.18) out.push_back({w.id, false, "그림 위 글자의 " + std::to_string((int)(share * 100)) + "%가 잘 안 보임 — 글자 꾸밈이나 그늘을 써 보세요"});
        }
        std::string missing;
        std::set<uint32_t> seen;
        for (const std::string* t : renderedTexts(w))
            for (uint32_t cp : fontgen::decode(*t))
                if (cp >= 0x20 && !lvhost::baseHas(cp) && !fontgen::has(cp) && !(face && fontface::has(**face, cp)) && seen.insert(cp).second && seen.size() <= 4)
                    missing += (missing.empty() ? "" : " ") + fontgen::encode(cp);
        if (!missing.empty()) out.push_back({w.id, true, "글꼴에 없는 글자: " + missing});
        for (auto& e : w.events) {
            if (!validIdent(e.cb) && !(e.cb.empty() && !e.go.empty()))
                out.push_back({w.id, true, std::string("주기 ") + eventKo(e.code) + ": 콜백 이름이 비었거나 잘못됨"});
            if (!e.go.empty() && screenByName(d, e.go) < 0)
                out.push_back({w.id, true, std::string("주기 ") + eventKo(e.code) + ": 없는 시트 " + e.go});
        }
        if (w.style[ST_DEFAULT][SP_BG_COLOR].set && w.style[ST_DEFAULT][SP_BG_OPA].set &&
            w.style[ST_DEFAULT][SP_BG_OPA].v == 0)
            out.push_back({w.id, false, "배경색을 지정했지만 불투명도가 0"});
    }
    // 형제끼리 겹침 (컨테이너 제외)
    for (size_t i = 0; i < s.widgets.size(); i++) {
        const Widget& a = s.widgets[i];
        if (a.hidden || a.type == WType::Container || a.type == WType::Arc) continue;
        const Rect* ra = rectOf(a.id);
        if (!ra) continue;
        for (size_t j = i + 1; j < s.widgets.size(); j++) {
            const Widget& b = s.widgets[j];
            if (b.parent != a.parent || b.hidden || b.type == WType::Container || b.type == WType::Arc) continue;
            const Rect* rb = rectOf(b.id);
            if (!rb) continue;
            if (ra->x1 <= rb->x2 && rb->x1 <= ra->x2 && ra->y1 <= rb->y2 && rb->y1 <= ra->y2) {
                // 아크 위 라벨처럼 의도된 겹침이 흔하므로 권고로만
                out.push_back({b.id, false, a.name + "와 겹침"});
            }
        }
    }
    (void)d;
    std::stable_sort(out.begin(), out.end(), [](const Issue& x, const Issue& y) { return x.error > y.error; });
    return out;
}

// ───────────────────────── 저장 / 불러오기 ─────────────────────────
static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}
static std::string unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else o += s[i];
    }
    return o;
}
static std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> f;
    size_t st = 0;
    for (size_t i = 0; i <= line.size(); i++)
        if (i == line.size() || line[i] == '\t') { f.push_back(line.substr(st, i - st)); st = i + 1; }
    return f;
}

bool wantsClick(const Widget& w) {
    return !w.events.empty() && (w.type == WType::Label || w.type == WType::Image || w.type == WType::Bar || w.type == WType::Spinner);
}

std::string serializeDocument(const Document& d, bool withData) {
    std::string o = "HG\t1\tHigenis Studio\n";
    char b[128];
    o += "project\t" + esc(d.project) + "\n";
    snprintf(b, sizeof b, "res\t%d\t%d\t%d\n", d.resW, d.resH, d.depth); o += b;
    snprintf(b, sizeof b, "theme\t%d\t%06X\n", d.dark ? 1 : 0, d.primary); o += b;
    if (d.round) o += "round\t1\n";
    if (!d.about.empty()) o += "about\t" + esc(d.about) + "\n";
    if (!d.credit.empty()) o += "credit\t" + esc(d.credit) + "\n";
    const Board def;
    for (int i = 0; i < kBoardFieldCount; i++)
        if (d.board.*kBoardFields[i].p != def.*kBoardFields[i].p) {
            snprintf(b, sizeof b, "board\t%s\t%d\n", kBoardFields[i].key, d.board.*kBoardFields[i].p);
            o += b;
        }
    // 어느 부품도 쓰지 않는 그림은 파일에 남기지 않는다
    for (auto& a : d.images) {
        bool used = false;
        for (auto& sc : d.screens) for (auto& w : sc.widgets) if (w.type == WType::Image && w.text == a->name) used = true;
        if (!used) continue;
        o += "image\t" + esc(a->name) + "\t" + esc(a->file) + "\t";
        if (withData) o += a->b64;
        else { snprintf(b, sizeof b, "%p", (const void*)a.get()); o += b; }
        o += "\n";
    }
    if (!d.fonts.empty()) {
        const fontgen::Plan pl = withData ? fontgen::plan(d) : fontgen::Plan();
        for (auto& f : d.fonts) {
            bool used = false;
            for (auto& sc : d.screens) for (auto& w : sc.widgets) for (int st = 0; st < ST_COUNT; st++) if (w.style[st][SP_FACE].set && w.style[st][SP_FACE].v == f->id) used = true;
            if (!used) continue;
            int a = 0, de = 0, g = 0;
            float em = 1000;
            const fonthint::Font* src = f->file ? f->file.get() : f->pack.get();
            if (src) { fonthint::vmetrics(src, a, de, g); em = fonthint::unitsPerEm(src); }
            o += "font\t" + std::to_string(f->id) + "\t" + esc(f->name) + "\t" + esc(f->key) + "\t" + std::to_string(f->index);
            snprintf(b, sizeof b, "\t%d\t%d\t%d\n", (int)em, a, de);
            o += b;
            if (!withData || (!f->key.empty() && f->key[0] == '@')) continue;   // 함께 든 글꼴은 윤곽을 담지 않아도 된다
            std::set<uint32_t> cps;
            for (auto& [k, v] : pl.faces) if (k.first == f->id) cps.insert(v.begin(), v.end());
            o += fontface::packLines(*f, std::vector<uint32_t>(cps.begin(), cps.end()));
        }
    }
    snprintf(b, sizeof b, "nextid\t%d\n", d.nextId); o += b;
    for (auto& s : d.screens) {
        snprintf(b, sizeof b, "\t%d\t%06X\n", s.bgSet ? 1 : 0, s.bg);
        o += "screen\t" + esc(s.name) + b;
        for (auto& w : s.widgets) {
            o += "w\t" + std::to_string(w.id) + "\t" + std::to_string(w.parent) + "\t" + typeInfo(w.type).key + "\t" +
                 esc(w.name);
            snprintf(b, sizeof b, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t", w.x, w.y, w.w, w.h, w.align, w.autoSize, w.hidden);
            o += b;
            o += esc(w.text) + "\t" + esc(w.text2);
            snprintf(b, sizeof b, "\t%d\t%d\t%d\t%d\t%06X\n", w.value, w.minv, w.maxv, w.checked, w.color);
            o += b;
            for (int st = 0; st < ST_COUNT; st++)
                for (int p = 0; p < SP_COUNT; p++)
                    if (w.style[st][p].set) {
                        snprintf(b, sizeof b, "s\t%s\t%s\t%d\n", stateKey(st), stylePropInfo(p).key, w.style[st][p].v);
                        o += b;
                    }
            {
                Extra cur = w.x2, dx;
                for (int i = 0; i < kExtraFieldCount; i++)
                    if (kExtraFields[i].get(cur) != kExtraFields[i].get(dx)) {
                        snprintf(b, sizeof b, "x\t%s\t%d\n", kExtraFields[i].key, kExtraFields[i].get(cur));
                        o += b;
                    }
            }
            for (auto& e : w.events) o += std::string("e\t") + eventKey(e.code) + "\t" + esc(e.cb) + (e.go.empty() ? "" : "\t" + esc(e.go)) + "\n";
        }
    }
    return o;
}

int screenByName(const Document& d, const std::string& name) {
    for (size_t i = 0; i < d.screens.size(); i++) if (d.screens[i].name == name) return (int)i;
    return -1;
}
void renameSheetRefs(Document& d, const std::string& from, const std::string& to) {
    if (from.empty() || from == to || screenByName(d, from) >= 0) return;   // 같은 이름의 시트가 남아 있으면 그대로
    for (auto& s : d.screens)
        for (auto& w : s.widgets)
            for (auto& e : w.events) if (e.go == from) e.go = to;
}

bool saveDocument(const Document& d, const std::wstring& path, std::string& err) {
    const std::string o = serializeDocument(d);
    const std::wstring tmp = path + L".tmp";   // 다 쓴 뒤에 바꿔치기해서, 도중에 실패해도 원본이 남는다
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) { err = "파일을 쓸 수 없음"; return false; }
    const bool wrote = fwrite(o.data(), 1, o.size(), f) == o.size();
    const bool closed = fclose(f) == 0;
    if (!wrote || !closed || !replaceFile(tmp, path)) {
        _wremove(tmp.c_str());
        err = "파일을 쓸 수 없음";
        return false;
    }
    return true;
}

bool loadDocument(Document& out, const std::wstring& path, std::string& err) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) { err = "파일을 열 수 없음"; return false; }
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
    fclose(f);
    Document d;
    d.screens.clear();
    Screen* cs = nullptr;
    Widget* cw = nullptr;
    std::shared_ptr<FontAsset> cf;
    size_t st = 0;
    bool header = false;
    while (st < data.size()) {
        size_t e = data.find('\n', st);
        if (e == std::string::npos) e = data.size();
        std::string line = data.substr(st, e - st);
        st = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto F = splitTabs(line);
        const std::string& k = F[0];
        auto I = [&](size_t i) { return i < F.size() ? atoi(F[i].c_str()) : 0; };
        auto H = [&](size_t i) { return i < F.size() ? (int)strtol(F[i].c_str(), nullptr, 16) : 0; };
        auto S = [&](size_t i) { return i < F.size() ? unesc(F[i]) : std::string(); };
        if (k == "HG" || k == "VELLUM") header = true;
        else if (k == "project") d.project = S(1);
        else if (k == "res") { d.resW = I(1); d.resH = I(2); d.depth = I(3); }
        else if (k == "theme") { d.dark = I(1) != 0; d.primary = H(2); }
        else if (k == "round") d.round = I(1) != 0;
        else if (k == "about") d.about = S(1);
        else if (k == "credit") d.credit = S(1);
        else if (k == "board") {
            for (int i = 0; i < kBoardFieldCount; i++) if (F.size() > 2 && F[1] == kBoardFields[i].key) d.board.*kBoardFields[i].p = I(2);
        } else if (k == "image" && F.size() >= 4) {
            const std::string nm = sanitizeIdent(S(1));
            if (!nm.empty() && !image::find(d, nm))
                if (auto a = image::fromBytes(image::unbase64(F[3]), nm, S(2))) d.images.push_back(a);
        }
        else if (k == "font" && F.size() >= 8) {
            auto f = fontface::open(S(3), std::max(0, I(4)), S(2));
            f->id = I(1);
            f->pack = std::shared_ptr<fonthint::Font>(fonthint::newPack((float)std::max(1, I(5)), I(6), I(7)), fonthint::close);
            if (f->id > 0 && !fontface::find(d, f->id)) { d.fonts.push_back(f); cf = f; } else cf.reset();
        } else if (k == "fg" && cf && F.size() >= 4) fontface::unpackLine(*cf, F[1], F[2], F[3]);
        else if (k == "nextid") d.nextId = I(1);
        else if (k == "screen") {
            d.screens.push_back({});
            cs = &d.screens.back();
            cs->name = S(1); cs->bgSet = I(2) != 0; cs->bg = H(3);
            cw = nullptr;
        } else if (k == "w" && cs && F.size() >= 19) {
            Widget w;
            w.id = I(1); w.parent = I(2);
            int t = typeFromKey(F[3].c_str());
            if (t < 0) continue;
            w.type = (WType)t; w.name = S(4);
            w.x = I(5); w.y = I(6); w.w = I(7); w.h = I(8); w.align = std::clamp(I(9), 0, 8);
            w.autoSize = I(10) != 0; w.hidden = I(11) != 0;
            w.text = S(12); w.text2 = S(13);
            w.value = I(14); w.minv = I(15); w.maxv = I(16); w.checked = I(17) != 0; w.color = H(18);
            cs->widgets.push_back(w);
            cw = &cs->widgets.back();
        } else if (k == "s" && cw && F.size() >= 4) {
            int sti = -1, pi = -1;
            for (int i = 0; i < ST_COUNT; i++) if (F[1] == stateKey(i)) sti = i;
            for (int i = 0; i < SP_COUNT; i++) if (F[2] == stylePropInfo(i).key) pi = i;
            if (sti >= 0 && pi >= 0) {
                int v = I(3);
                if (pi == SP_TEXT_FONT) {   // 기본 글꼴에 있는 크기 가운데 가장 가까운 것으로
                    int best = 14;
                    for (int k = 0; k < kFontSizeCount; k++)
                        if (abs(kFontSizes[k] - v) < abs(best - v)) best = kFontSizes[k];
                    v = best;
                } else if (!stylePropInfo(pi).color) {
                    v = std::clamp(v, stylePropInfo(pi).minv, stylePropInfo(pi).maxv);
                }
                cw->style[sti][pi] = {true, v};
            }
        } else if (k == "x" && cw && F.size() >= 3) {
            for (int i = 0; i < kExtraFieldCount; i++)
                if (F[1] == kExtraFields[i].key) kExtraFields[i].get(cw->x2) = std::clamp(I(2), kExtraFields[i].minv, kExtraFields[i].maxv);
        } else if (k == "e" && cw && F.size() >= 3) {
            int c = -1;
            for (int i = 0; i < EV_COUNT; i++) if (F[1] == eventKey(i)) c = i;
            if (c >= 0) cw->events.push_back({c, S(2), S(3)});
        }
    }
    if (!header || d.screens.empty()) { err = "Higenis Studio 도면 파일(.hg)이 아님"; return false; }
    d.resW = std::clamp(d.resW, 16, 2048);
    d.resH = std::clamp(d.resH, 16, 2048);
    d.depth = d.depth == 32 ? 32 : 16;
    d.nextId = std::max(d.nextId, 1);
    {   // 보드 설정은 쓸 수 있는 범위로
        Board& bd = d.board;
        bd.driver = std::clamp(bd.driver, 0, 3); bd.rotation = std::clamp(bd.rotation, 0, 3); bd.touch = std::clamp(bd.touch, 0, 2);
        bd.buf = std::clamp(bd.buf, 0, 3); bd.period = std::clamp(bd.period, 5, 500); bd.mhz = std::clamp(bd.mhz, 1, 80); bd.sdmhz = std::clamp(bd.sdmhz, 1, 40);
    }
    for (auto& s : d.screens)
        for (auto& w : s.widgets) d.nextId = std::max(d.nextId, w.id + 1);
    // 번호 중복·없는 부모·순환 참조를 바로잡고, 전위 순서(부모 → 하위 트리)로 다시 놓는다
    for (auto& s : d.screens) {
        std::set<int> seen;
        for (auto& w : s.widgets) {
            if (w.id <= 0 || seen.count(w.id)) w.id = d.nextId++;
            if (!seen.count(w.parent) || !typeInfo(findWidget(s, w.parent)->type).canParent) w.parent = 0;
            seen.insert(w.id);
            if (w.maxv <= w.minv) w.maxv = w.minv + 1;
            w.w = std::clamp(w.w, 0, 8192);
            w.h = std::clamp(w.h, 0, 8192);
        }
        std::vector<Widget> flat;
        std::vector<int> stack = {0};
        while (!stack.empty()) {
            int p = stack.back();
            stack.pop_back();
            if (p) flat.push_back(*findWidget(s, p));
            for (int i = (int)s.widgets.size() - 1; i >= 0; i--)
                if (s.widgets[i].parent == p) stack.push_back(s.widgets[i].id);
        }
        s.widgets.swap(flat);
    }
    out = d;
    return true;
}

// ───────────────────────── 표준도 ─────────────────────────
std::vector<Standard> loadStandards(const std::wstring& dir) {
    std::vector<Standard> out;
    std::error_code ec;
    std::vector<std::wstring> files;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        if (it->path().extension() == L".hg" || it->path().extension() == L".vellum") files.push_back(it->path().wstring());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        Standard s;
        std::string err;
        s.path = f;
        if (loadDocument(s.doc, f, err)) out.push_back(std::move(s));   // 깨진 파일은 건너뛴다
    }
    return out;
}
