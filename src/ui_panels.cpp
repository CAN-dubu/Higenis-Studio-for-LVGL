// 패널: 상단 막대, 템플릿 자, 부품표, 속성(치수·사양·스타일·주기), 상태 막대, 내보내기·검도·확인 창
#include "fontface.h"
#include "fontgen.h"
#include "image.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

App g;

// ───────────────────────── 동작 ─────────────────────────
Screen& curScreen() { return g.doc.screens[ImClamp(g.screen, 0, (int)g.doc.screens.size() - 1)]; }
Widget* selWidget() { return g.sel ? findWidget(curScreen(), g.sel) : nullptr; }
int widgetNumber(int id) { return widgetIndex(curScreen(), id) + 1; }

// 3:2 척도는 정수배가 아니라서 그대로 늘리면 흐려진다.
// 3배로 선명하게 키운 텍스처를 절반으로 줄여 그리면 픽셀 면적이 정확히 보존된다.
static int wantPrescale() { return g.zoomTarget == 1.5f ? 3 : 1; }
// 매끈하게 보기: 척도만큼 다시 그린다. 1:1에서는 장치 픽셀이 곧 화면 픽셀이다
static float wantView() {
    if (g.pixelView || fabsf(g.zoomTarget - 1.f) < 1e-3f) return 0.f;
    const float cap = sqrtf(6.0e6f / ((float)g.doc.resW * g.doc.resH));   // 너무 크면 조금 작게 그려 늘린다
    return std::min(g.zoomTarget, std::max(cap, 1.f));
}
static void uploadCanvas() {
    if (lvhost::smoothOn()) {
        g.texPre = 1;
        g.viewK = wantView();
        platformUploadCanvas(lvhost::smoothPixels(), lvhost::smoothWidth(), lvhost::smoothHeight(), 1);
        return;
    }
    g.viewK = 0.f;
    g.texPre = wantPrescale();
    platformUploadCanvas(lvhost::pixels(), lvhost::width(), lvhost::height(), g.texPre);
}
static bool viewStale() { return wantView() != g.viewK || (g.viewK == 0.f && wantPrescale() != g.texPre); }

// 비쳐 볼 시트: 직전에 보던 시트, 없으면 이웃 시트
int traceSheet() {
    int n = (int)g.doc.screens.size();
    if (n < 2) return -1;
    if (g.prevScreen >= 0 && g.prevScreen < n && g.prevScreen != g.screen) return g.prevScreen;
    return g.screen > 0 ? g.screen - 1 : 1;
}
void toggleTrace() {
    if (!g.trace && traceSheet() < 0) { showToast("비쳐 볼 다른 시트가 없습니다"); return; }
    g.trace = !g.trace;
    g.traceKey.clear();
    g.needsBuild = true;
}
// 다른 시트를 렌더한 뒤 윤곽선만 청색 연필로 따라 그린다
static void buildTrace() {
    int ts = traceSheet();
    if (!g.trace || ts < 0) return;
    char key[96];
    snprintf(key, sizeof key, "%d/%d/%d/%dx%d/%d", ts, g.hist.base, g.hist.cur, g.doc.resW, g.doc.resH, wantPrescale());
    if (g.traceKey == key) return;
    g.traceKey = key;
    lvhost::setView(0);
    lvhost::build(g.doc, ts, 0, ST_DEFAULT);
    lvhost::takeFrame();
    const int n = lvhost::width() * lvhost::height();
    std::vector<uint32_t> out(n);
    const uint32_t* p = lvhost::pixels();
    const int W = lvhost::width(), H = lvhost::height();
    auto diff = [](uint32_t a, uint32_t b) {
        return std::max({abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255)), abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)),
                         abs((int)(a & 255) - (int)(b & 255))});
    };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const uint32_t c = p[y * W + x];
            int e = 0;
            if (x + 1 < W) e = std::max(e, diff(c, p[y * W + x + 1]));
            if (y + 1 < H) e = std::max(e, diff(c, p[(y + 1) * W + x]));
            uint32_t al = e < 6 ? 0u : (uint32_t)std::min(235, 40 + e * 2);   // 여린 테두리도 선으로
            out[y * W + x] = (al << 24) | 0x00559BD8u;
        }
    platformUploadCanvas(out.data(), lvhost::width(), lvhost::height(), wantPrescale(), 1);
}

void rebuildNow() {
    buildTrace();
    if (g.sel && !findWidget(curScreen(), g.sel)) g.sel = 0;
    lvhost::setView(wantView());
    lvhost::setMotionTime(g.motionT);
    lvhost::build(g.doc, g.screen, g.sel, g.editState);
    g.issues = runLint(g.doc, curScreen(), lvhost::coords());
    if (lvhost::takeFrame()) uploadCanvas();
    g.needsBuild = false;
}

// 새 이름을 다듬어 돌려준다. 쓸 수 없으면 알리고 빈 문자열
static std::string identOrWarn(const std::string& in) {
    std::string n = sanitizeIdent(in);
    if (n.empty()) showToast("C 이름이 되므로 영문·숫자·밑줄을 쓰세요");
    return n;
}

// 마지막 개정과 내용이 같으면 개정을 만들지 않는다
static bool unchanged() {
    return g.hist.cur >= 0 && serializeDocument(g.hist.revs[g.hist.cur].doc, false) == serializeDocument(g.doc, false);
}
void flushLive() {
    // 이름을 고치다 다른 곳으로 간 경우: 쓸 수 있는 이름으로 다듬어 확정한다
    if (g.nameEdit) {
        for (Screen& s : g.doc.screens)
            if (Widget* w = findWidget(s, g.nameEdit)) {
                std::string n = identOrWarn(w->name);
                w->name = n.empty() ? g.nameBefore : uniqueName(s, n, w->id);
                if (w->name != g.nameBefore) { g.live = true; g.liveNote = g.nameBefore + " → " + w->name; g.liveSel = w->id; }
            }
        g.nameEdit = 0;
        g.needsBuild = true;
    }
    if (g.sheetEdit >= 0) {
        if (g.sheetEdit < (int)g.doc.screens.size()) {
            Screen& s = g.doc.screens[g.sheetEdit];
            std::string n = identOrWarn(s.name);
            s.name = n.empty() ? g.nameBefore : n;
            if (s.name != g.nameBefore) { renameSheetRefs(g.doc, g.nameBefore, s.name); g.live = true; g.liveNote = "시트 이름 " + s.name; g.liveSel = 0; }
        }
        g.sheetEdit = -1;
    }
    if (!g.live) return;
    g.live = false;
    if (unchanged()) return;
    g.hist.commit(g.doc, g.screen, g.liveSel, g.liveNote);
    g.revAnimT = g.now;
    g.dirty = true;
    g.needsBuild = true;
    g.mergeKey.clear();
}
void commit(const std::string& note) {
    g.live = false;
    g.nameEdit = 0;
    g.sheetEdit = -1;
    if (unchanged()) return;
    g.hist.commit(g.doc, g.screen, g.sel, note);
    g.revAnimT = g.now;
    g.dirty = true;
    g.needsBuild = true;
    g.mergeKey.clear();
}
void commitMerge(const std::string& note, const std::string& key) {
    if (key == g.mergeKey && g.now - g.mergeT < 1.2 && g.hist.cur == (int)g.hist.revs.size() - 1 && g.hist.cur > 0) {
        Revision& r = g.hist.revs[g.hist.cur];
        r.doc = g.doc; r.note = note; r.sel = g.sel; r.time = nowStamp();
        if (g.hist.saved == g.hist.cur) g.hist.saved = -1;
        g.dirty = true;
        g.needsBuild = true;
    } else {
        commit(note);
        g.mergeKey = key;
    }
    g.mergeT = g.now;
}
static void applyRevision(int from, int to) {
    g.hist.cur = to;
    const Revision& r = g.hist.revs[to];
    g.doc = r.doc;
    g.screen = ImClamp(r.screen, 0, (int)g.doc.screens.size() - 1);
    int want = g.hist.revs[std::max(from, to)].sel;
    g.sel = (want && findWidget(curScreen(), want)) ? want : (r.sel && findWidget(curScreen(), r.sel) ? r.sel : 0);
    g.dirty = g.hist.cur != g.hist.saved;
    g.needsBuild = true;
    g.mergeKey.clear();
}
void undo() { flushLive(); if (g.hist.canUndo()) applyRevision(g.hist.cur, g.hist.cur - 1); }
void redo() { flushLive(); if (g.hist.canRedo()) applyRevision(g.hist.cur, g.hist.cur + 1); }
void gotoRevision(int i) {
    flushLive();
    if (i >= 0 && i < (int)g.hist.revs.size() && i != g.hist.cur) applyRevision(g.hist.cur, i);
}
void selectWidget(int id) {
    if (g.sel == id) return;
    flushLive();
    g.sel = id;
    g.editState = ST_DEFAULT;
    g.renameId = 0;
    g.needsBuild = true;
}
void switchScreen(int i) {
    if (i == g.screen || i < 0 || i >= (int)g.doc.screens.size()) return;
    flushLive();
    g.prevScreen = g.screen;
    g.screen = i;
    g.motionT = -1;
    g.sel = 0;
    g.editState = ST_DEFAULT;
    g.needsBuild = true;
}
void showToast(const std::string& t) { g.toast = t; g.toastT = g.now; }
static void resetTo(const Document& d, const std::wstring& path, const char* note) {
    if (g.sim) simToggle();
    g.doc = d;
    g.path = path;
    g.screen = 0;
    g.sel = 0;
    g.armed = -1;
    g.editState = ST_DEFAULT;
    g.hist.reset(g.doc, 0, 0, note);
    g.dirty = false;
    g.needsBuild = true;
    g.needFit = true;
}
void newDocument() { resetTo(makeEmptyDocument(), L"", "새 도면"); }
void openDocumentDialog() {
    std::wstring p;
    if (!platformOpenDialog(p)) return;
    Document d;
    std::string err;
    if (!loadDocument(d, p, err)) { showToast("열 수 없음 · " + err); return; }
    resetTo(d, p, "도면 열기");
    showToast("열림 · " + wideToUtf8(p.substr(p.find_last_of(L"\\/") + 1)));
}
bool saveCurrent(bool saveAs) {
    flushLive();
    std::wstring p = g.path;
    if (p.empty() || saveAs) {
        if (p.size() > 7 && p.substr(p.size() - 7) == L".vellum") p = p.substr(0, p.size() - 7) + L".hg";
        std::string base = sanitizeIdent(g.doc.project);
        p = utf8ToWide((base.empty() ? std::string("untitled") : base) + ".hg");
        if (!platformSaveDialog(p)) return false;
    }
    std::string err;
    if (!saveDocument(g.doc, p, err)) { showToast("저장 실패 · " + err); return false; }
    g.path = p;
    g.dirty = false;
    g.hist.saved = g.hist.cur;
    showToast("저장됨 · " + wideToUtf8(p.substr(p.find_last_of(L"\\/") + 1)));
    return true;
}
// ───────────────────────── 표준도 ─────────────────────────
static std::vector<Standard> g_stds;
static bool g_stdsLoaded = false;
static const std::vector<Standard>& standards() {
    if (!g_stdsLoaded) { g_stds = loadStandards(platformAssetsDir() + L"\\standards"); g_stdsLoaded = true; }
    return g_stds;
}
int standardCount() { return (int)standards().size(); }
static void openStandardNow(int i) {
    if (i < 0 || i >= standardCount()) return;
    Document d = standards()[i].doc;
    resetTo(d, L"", ("표준도 " + d.project).c_str());
    showToast("표준도로 시작 · " + d.project);
}
void startStandard(int i) { g.pendingStd = i; requestAction(4); }
// 지금 도면에 시트로 더한다 (해상도가 같을 때만)
bool addStandardSheet(int i) {
    if (i < 0 || i >= standardCount()) return false;
    const Document& t = standards()[i].doc;
    if (t.resW != g.doc.resW || t.resH != g.doc.resH || t.screens.empty()) return false;
    flushLive();
    Screen s = t.screens[0];
    for (auto& a : t.images) {
        if (image::find(g.doc, a->name)) {   // 이름이 겹치면 바꿔서 담는다
            auto c = std::make_shared<ImageAsset>(*a);
            for (int k = 2; image::find(g.doc, c->name) || image::find(t, c->name); k++) c->name = a->name + "_" + std::to_string(k);
            for (auto& w : s.widgets) if (w.type == WType::Image && w.text == a->name) w.text = c->name;
            g.doc.images.push_back(c);
        } else g.doc.images.push_back(a);
    }
    std::map<int, int> faceMap;
    for (auto& f : t.fonts) {
        int id = 1;
        bool have = false;
        for (auto& x : g.doc.fonts) { if (x->key == f->key && x->index == f->index) { faceMap[f->id] = x->id; have = true; } id = std::max(id, x->id + 1); }
        if (have) continue;
        auto c = std::make_shared<FontAsset>(*f);
        c->id = id;
        faceMap[f->id] = id;
        g.doc.fonts.push_back(c);
    }
    for (auto& w : s.widgets) for (int st = 0; st < ST_COUNT; st++) if (w.style[st][SP_FACE].set && faceMap.count(w.style[st][SP_FACE].v)) w.style[st][SP_FACE].v = faceMap[w.style[st][SP_FACE].v];
    std::map<int, int> remap;
    for (auto& w : s.widgets) remap[w.id] = g.doc.nextId++;
    for (auto& w : s.widgets) { w.id = remap[w.id]; if (w.parent) w.parent = remap[w.parent]; }
    std::string base = s.name;
    for (int k = 2;; k++) {
        bool dup = false;
        for (auto& x : g.doc.screens) if (x.name == s.name) dup = true;
        if (!dup) break;
        s.name = base + "_" + std::to_string(k);
    }
    if (!t.credit.empty() && g.doc.credit.find(t.credit) == std::string::npos) g.doc.credit += (g.doc.credit.empty() ? "" : " / ") + t.credit;
    g.doc.screens.insert(g.doc.screens.begin() + g.screen + 1, s);
    g.prevScreen = g.screen;
    g.screen++;
    g.sel = 0;
    commit("표준도 시트 " + t.project);
    return true;
}

static void doAction(int a) {
    if (a == 1) platformRequestClose();
    if (a == 2) newDocument();
    if (a == 3) openDocumentDialog();
    if (a == 4) openStandardNow(g.pendingStd);
}
void requestAction(int a) {
    flushLive();
    if (g.dirty) g.pending = a;
    else doAction(a);
}
static void regenerate() {
    const std::string rev = g.hist.label(g.hist.cur);
    g.gen = g.genMode == 1 ? generateArduino(g.doc, rev) : generateCode(g.doc, rev);
    g.genTab = g.genMode == 1 ? std::min(1, (int)g.gen.size() - 1) : std::min(2, (int)g.gen.size() - 1);
}
void openExportDialog() {
    flushLive();
    regenerate();
    g.stampT = -10;
    g.openExport = true;
}
// ── 그림 ──
// 처음 넣은 그림은 화면에 알맞은 크기로
static void sizeToImage(Widget& w, const ImageAsset& a, double maxFrac) {
    const double s = std::min({1.0, g.doc.resW * maxFrac / a.w, g.doc.resH * maxFrac / a.h});
    w.w = std::max(8, (int)lround(a.w * s));
    w.h = std::max(8, (int)lround(a.h * s));
    w.autoSize = false;
}
static bool setImage(Widget& w, const std::wstring& path) {
    std::string err;
    auto a = image::load(path, g.doc, err);
    if (!a) { showToast("불러올 수 없음 · " + err); return false; }
    const TypeInfo& ti = typeInfo(WType::Image);
    const bool fresh = w.text.empty() && w.w == ti.defW && w.h == ti.defH;
    g.doc.images.push_back(a);
    w.text = a->name;
    w.text2.clear();
    if (fresh) sizeToImage(w, *a, 0.6);
    commit(w.name + " 그림 " + a->file);
    showToast("그림 " + a->file + " · " + std::to_string(a->w) + " × " + std::to_string(a->h));
    return true;
}
static void importImage(Widget& w) {
    std::wstring p;
    if (platformOpenImage(p)) setImage(w, p);
}
void dropFile(const std::wstring& path, ImVec2 m) {
    if (g.sim || GImGui->OpenPopupStack.Size > 0) return;
    std::wstring ext = path.substr(path.find_last_of(L'.') == std::wstring::npos ? path.size() : path.find_last_of(L'.'));
    for (wchar_t& c : ext) c = towlower(c);
    flushLive();
    if (ext == L".hg" || ext == L".vellum") {
        Document d;
        std::string err;
        if (g.dirty) { showToast("저장하지 않은 개정이 있어 열지 않았습니다"); return; }
        if (!loadDocument(d, path, err)) { showToast("열 수 없음 · " + err); return; }
        resetTo(d, path, "도면 열기");
        return;
    }

    const ImVec2 o = canvasDevToScreen(0, 0), e = canvasDevToScreen((float)g.doc.resW, (float)g.doc.resH);
    const float Z = (e.x - o.x) / g.doc.resW;
    const ImVec2 dev((m.x - o.x) / Z, (m.y - o.y) / Z);
    // 그림 부품 위에 놓으면 그 그림을 바꾸고, 빈 곳이면 새로 만든다
    Screen& s = curScreen();
    Widget* hit = nullptr;
    for (auto& w : s.widgets) {
        if (w.type != WType::Image || w.hidden) continue;
        auto it = lvhost::coords().find(w.id);
        if (it != lvhost::coords().end() && dev.x >= it->second.x1 && dev.x <= it->second.x2 && dev.y >= it->second.y1 && dev.y <= it->second.y2) hit = &w;
    }
    if (hit) { const int id = hit->id; selectWidget(id); if (Widget* w = findWidget(curScreen(), id)) setImage(*w, path); return; }
    std::string err;
    auto a = image::load(path, g.doc, err);
    if (!a) { showToast("불러올 수 없음 · " + err); return; }
    const int id = addWidget(g.doc, s, WType::Image, 0, 0, 0, 10, 10);
    Widget& w = *findWidget(s, id);
    g.doc.images.push_back(a);
    w.text = a->name;
    sizeToImage(w, *a, 0.6);
    w.x = ImClamp((int)lroundf(dev.x - w.w * 0.5f), 0, std::max(0, g.doc.resW - w.w));
    w.y = ImClamp((int)lroundf(dev.y - w.h * 0.5f), 0, std::max(0, g.doc.resH - w.h));
    g.sel = id;
    g.editState = ST_DEFAULT;
    commit(w.name + " 그림 " + a->file);
    showToast("그림 " + a->file + " · " + std::to_string(a->w) + " × " + std::to_string(a->h));
}
void deleteSelected() {
    Widget* w = selWidget();
    if (!w) return;
    std::string n = w->name;
    auto it = lvhost::coords().find(w->id);
    if (it != lvhost::coords().end()) g.ghosts.push_back({it->second, g.now});
    deleteWidget(curScreen(), w->id);
    g.sel = 0;
    commit(n + " 삭제");
}
void duplicateSelected() {
    Widget* w = selWidget();
    if (!w) return;
    std::string n = w->name;
    int id = duplicateWidget(g.doc, curScreen(), w->id);
    g.sel = id;
    commit(n + " 복제");
}

bool moveSubtree(Screen& s, int id, int target, int mode) {
    if (id == target || isAncestor(s, id, target)) return false;
    int i = widgetIndex(s, id);
    if (i < 0) return false;
    auto it = lvhost::coords().find(id);
    bool haveAbs = it != lvhost::coords().end();
    Rect abs = haveAbs ? it->second : Rect{};
    int oldParent = s.widgets[i].parent;
    int e = subtreeEnd(s, i);
    std::vector<Widget> blk(s.widgets.begin() + i, s.widgets.begin() + e);
    s.widgets.erase(s.widgets.begin() + i, s.widgets.begin() + e);
    int ti = widgetIndex(s, target);
    if (ti < 0) { s.widgets.insert(s.widgets.begin() + i, blk.begin(), blk.end()); return false; }
    int at, np;
    if (mode == 0) { at = ti; np = s.widgets[ti].parent; }
    else if (mode == 1) { at = subtreeEnd(s, ti); np = s.widgets[ti].parent; }
    else { at = subtreeEnd(s, ti); np = target; }
    blk[0].parent = np;
    if (haveAbs && np != oldParent) {
        Rect c = lvhost::contentRect(np);
        blk[0].x = abs.x1 - c.x1 - alignAnchor(blk[0].align, false, rw(c), rw(abs));
        blk[0].y = abs.y1 - c.y1 - alignAnchor(blk[0].align, true, rh(c), rh(abs));
    }
    s.widgets.insert(s.widgets.begin() + at, blk.begin(), blk.end());
    return true;
}

// ───────────────────────── 단축키 ─────────────────────────
static void shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (g.sim || ImGui::GetFrameCount() == g.simFrameNo) return;   // 시운전 중(그리고 끝낸 프레임)에는 simFrame이 맡는다
    if (GImGui->OpenPopupStack.Size > 0) {   // 메뉴·창이 열려 있으면 단축키는 쉬고, Esc로 닫기만
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::ClosePopupsExceptModals();
        return;
    }
    if (io.WantTextInput || g.drag != Drag::None) return;
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) { simToggle(); return; }
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    auto P = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto PR = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    Widget* w = selWidget();
    if (ctrl) {
        if (P(ImGuiKey_Z)) { if (shift) redo(); else undo(); }
        if (P(ImGuiKey_Y)) redo();
        if (P(ImGuiKey_S)) saveCurrent(shift);
        if (P(ImGuiKey_O)) requestAction(3);
        if (P(ImGuiKey_N)) { if (shift) g.openStd = true; else requestAction(2); }
        if (P(ImGuiKey_E)) openExportDialog();
        if (P(ImGuiKey_D)) duplicateSelected();
        if (P(ImGuiKey_Tab)) switchScreen((g.screen + (shift ? -1 : 1) + (int)g.doc.screens.size()) % (int)g.doc.screens.size());
        return;
    }
    if (P(ImGuiKey_Delete) || P(ImGuiKey_Backspace)) deleteSelected();
    if (P(ImGuiKey_Escape)) { if (g.armed >= 0) g.armed = -1; else selectWidget(0); }
    if (P(ImGuiKey_G)) g.snapGrid = !g.snapGrid;
    if (P(ImGuiKey_N)) g.balloons = !g.balloons;
    if (P(ImGuiKey_T)) toggleTrace();
    if (P(ImGuiKey_P)) g.pixelView = !g.pixelView;
    if (P(ImGuiKey_0)) fitView();
    if (P(ImGuiKey_1)) setScale(1, canvasCenter());
    if (P(ImGuiKey_Equal) || P(ImGuiKey_KeypadAdd)) setScale(g.scaleIdx + 1, canvasCenter());
    if (P(ImGuiKey_Minus) || P(ImGuiKey_KeypadSubtract)) setScale(g.scaleIdx - 1, canvasCenter());
    if (P(ImGuiKey_Tab)) {
        Screen& s = curScreen();
        if (!s.widgets.empty()) {
            int i = g.sel ? widgetIndex(s, g.sel) : -1;
            int n = (int)s.widgets.size();
            i = shift ? (i <= 0 ? n - 1 : i - 1) : (i + 1) % n;
            selectWidget(s.widgets[i].id);
        }
    }
    if (!w) return;
    if (P(ImGuiKey_H)) { w->hidden = !w->hidden; commit(w->name + (w->hidden ? " 숨김" : " 보임")); }
    if (P(ImGuiKey_LeftBracket)) { moveZ(curScreen(), w->id, shift ? -2 : -1); commit(w->name + " 뒤로"); }
    if (P(ImGuiKey_RightBracket)) { moveZ(curScreen(), w->id, shift ? 2 : 1); commit(w->name + " 앞으로"); }
    int step = shift ? 10 : 1, dx = 0, dy = 0;
    if (PR(ImGuiKey_LeftArrow)) dx = -step;
    if (PR(ImGuiKey_RightArrow)) dx = step;
    if (PR(ImGuiKey_UpArrow)) dy = -step;
    if (PR(ImGuiKey_DownArrow)) dy = step;
    if (dx || dy) {
        w->x += dx; w->y += dy;
        char b[96];
        snprintf(b, sizeof b, "%s 이동 → %d, %d", w->name.c_str(), w->x, w->y);
        commitMerge(b, "nudge" + std::to_string(w->id));
    }
}

// ───────────────────────── 상단 막대 ─────────────────────────
static void iconTurn(ImDrawList* dl, ImVec2 c, ImU32 col, bool redoIcon) {
    const float d = redoIcon ? -1.f : 1.f, r = px(4.f), top = floorf(c.y - r) + 0.5f;
    // 화살촉에서 나와 오른쪽으로 갔다가 반원을 돌아 왼쪽으로 빠지는 선
    dl->PathLineTo(ImVec2(c.x - d * px(5.f), top));
    dl->PathLineTo(ImVec2(c.x + d * px(2.f), top));
    for (int i = 0; i <= 12; i++) {
        float a = -3.14159f * 0.5f + 3.14159f * i / 12;
        dl->PathLineTo(ImVec2(c.x + d * (px(2.f) + cosf(a) * r), top + r + sinf(a) * r));
    }
    dl->PathLineTo(ImVec2(c.x - d * px(2.f), top + r * 2));
    dl->PathStroke(col, ImMax(1.f, px(1.2f)));
    ImVec2 tip(c.x - d * px(7.5f), top);
    dl->AddTriangleFilled(tip, tip + ImVec2(d * px(5.f), -px(3.2f)), tip + ImVec2(d * px(5.f), px(3.2f)), col);
}

// 척도: 누르면 고르기
void drawScaleBox(float& rx, float cy) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        float w = textW(F.mono, 12, kScaleTxt[g.scaleIdx]) + px(26), h = px(22);
        ImVec2 a = ImFloor(ImVec2(rx - w, cy - h * 0.5f));
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##scale", ImVec2(w, h))) ImGui::OpenPopup("##scalepop");
        bool hv = ImGui::IsItemHovered();
        if (hv) { dl->AddRectFilled(a, a + ImVec2(w, h), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        textAt(dl, F.mono, 12, a + ImVec2(px(8), px(4)), hv ? col::ink : col::ink2, kScaleTxt[g.scaleIdx]);
        ImVec2 t(a.x + w - px(11), a.y + h * 0.5f);
        dl->AddTriangleFilled(t + ImVec2(-px(3), -px(1.5f)), t + ImVec2(px(3), -px(1.5f)), t + ImVec2(0, px(2.5f)), hv ? col::ink : col::ink3);
        tooltip("척도 · 휠로도 바꿉니다");
        ImGui::SetNextWindowPos(ImVec2(a.x, a.y + h + px(6)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(10)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(12), px(7)));
        if (ImGui::BeginPopup("##scalepop")) {
            ImGui::Dummy(ImVec2(px(140), 0));
            pushFont(F.mono, 13);
            for (int i = 0; i < kScaleCount; i++)
                if (ImGui::MenuItem(kScaleTxt[i], i == 1 ? "1" : nullptr, i == g.scaleIdx)) setScale(i, canvasCenter());
            ImGui::PopFont();
            ImGui::Separator();
            pushFont(F.ui, 15);
            if (ImGui::MenuItem("화면에 맞추기", "0")) fitView();
            ImGui::PopFont();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        rx = a.x - px(16);
    }
}

void drawTopBar(ImVec2 p, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + sz, col::paper);
    const float cy = p.y + sz.y * 0.5f;
    float x = p.x + px(18);
    {   // 이름표: 누르면 프로그램 정보
        const float w0 = textW(F.med, 15, "HIGENIS", 4.6f), w1 = textW(F.mono, 11, "studio");
        ImGui::SetCursorScreenPos(ImVec2(x - px(6), p.y + px(8)));
        if (ImGui::InvisibleButton("##about", ImVec2(w0 + w1 + px(20), sz.y - px(16)))) g.openAbout = true;
        const bool hv = ImGui::IsItemHovered();
        if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        tooltip("Higenis Studio for LVGL 정보");
        textAt(dl, F.med, 15, ImVec2(x, cy - px(9)), hv ? col::red : col::ink, "HIGENIS", 4.6f);
        x += w0 + px(8);
        textAt(dl, F.mono, 11, ImVec2(x, cy - px(4)), col::ink3, "studio");
        x += w1 + px(16);
    }
    dl->AddLine(ImVec2(x, p.y + px(12)), ImVec2(x, p.y + sz.y - px(12)), col::hair2);
    x += px(10);

    // 파일
    ImGui::SetCursorScreenPos(ImVec2(x, p.y + px(8)));
    if (lineButton("파일", ImVec2(0, sz.y - px(16)), 2)) ImGui::OpenPopup("##file");
    tooltip("새 도면 · 열기 · 저장");
    ImVec2 fileBtnMin = ImGui::GetItemRectMin(), fileBtnMax = ImGui::GetItemRectMax();
    x = fileBtnMax.x + px(10);
    ImGui::SetNextWindowPos(ImVec2(fileBtnMin.x, p.y + sz.y - 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(12), px(7)));
    if (ImGui::BeginPopup("##file")) {
        ImGui::Dummy(ImVec2(px(220), 0));
        pushFont(F.ui, 15);
        if (ImGui::MenuItem("새 도면", "Ctrl+N")) requestAction(2);
        if (ImGui::MenuItem("표준도에서 시작…", "Ctrl+Shift+N")) g.openStd = true;
        if (ImGui::MenuItem("열기…", "Ctrl+O")) requestAction(3);
        ImGui::Separator();
        if (ImGui::MenuItem("저장", "Ctrl+S")) saveCurrent(false);
        if (ImGui::MenuItem("다른 이름으로 저장…", "Ctrl+Shift+S")) saveCurrent(true);
        ImGui::Separator();
        if (ImGui::MenuItem("내보내기…", "Ctrl+E")) openExportDialog();
        if (ImGui::MenuItem("예제 도면 열기")) resetTo(makeDemoDocument(), L"", "예제 도면");
        ImGui::Separator();
        if (ImGui::MenuItem("프로그램 정보…")) g.openAbout = true;
        ImGui::Separator();
        if (ImGui::MenuItem("끝내기", "Alt+F4")) requestAction(1);
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);

    // 도면 이름 + 저장 안 됨 표시
    float pw = textW(F.med, 16, g.doc.project.c_str());
    textAt(dl, F.med, 16, ImVec2(x, cy - px(10)), col::ink, g.doc.project.c_str());
    ImGui::SetCursorScreenPos(ImVec2(x, p.y + px(8)));
    ImGui::InvisibleButton("##proj", ImVec2(pw + px(14), sz.y - px(16)));
    if (ImGui::IsItemClicked()) { selectWidget(0); g.flashSheetT = g.now; }
    tooltip(g.dirty ? "저장하지 않은 개정이 있음 · 눌러서 도면 설정" : "눌러서 도면 설정", g.dirty ? "Ctrl+S" : nullptr);
    if (g.dirty) dl->AddCircleFilled(ImVec2(x + pw + px(7), cy - px(4)), px(3), col::red);
    x += pw + px(30);

    // 시트 탭
    Document& d = g.doc;
    static int renameSheet = -1;
    static std::string renameBuf;
    int removeSheet = -1, dupSheet = -1;
    for (int i = 0; i < (int)d.screens.size(); i++) {
        ImGui::PushID(i);
        char num[8];
        snprintf(num, sizeof num, "%02d", i + 1);
        float nw = textW(F.mono, 11, num), tw = textW(F.med, 15, d.screens[i].name.c_str());
        float w = nw + tw + px(26);
        ImVec2 a(x, p.y), b(x + w, p.y + sz.y);
        bool on = i == g.screen;
        if (renameSheet == i) {
            ImGui::SetCursorScreenPos(ImVec2(x + nw + px(14), cy - px(13)));
            pushFont(F.med, 15);
            ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(std::max(tw, px(90)));
            bool enter = ImGui::InputText("##rn", &renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopFont();
            w = nw + std::max(tw, px(90)) + px(26);
            b.x = x + w;
            if (enter || ImGui::IsItemDeactivated()) {
                std::string nn = ImGui::IsKeyPressed(ImGuiKey_Escape) ? std::string() : identOrWarn(renameBuf);
                if (!nn.empty() && nn != d.screens[i].name) {
                    const std::string old = d.screens[i].name;
                    d.screens[i].name = nn;
                    renameSheetRefs(d, old, nn);
                    commit("시트 이름 " + nn);
                }
                renameSheet = -1;
            }
        } else {
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##tab", b - a)) switchScreen(i);
            bool hv = ImGui::IsItemHovered();
            if (hv && ImGui::IsMouseDoubleClicked(0)) { renameSheet = i; renameBuf = d.screens[i].name; }
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(10)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(12), px(7)));
            if (ImGui::BeginPopupContextItem("##tabctx")) {
                pushFont(F.ui, 15);
                if (ImGui::MenuItem("이름 바꾸기")) { renameSheet = i; renameBuf = d.screens[i].name; }
                if (ImGui::MenuItem("복제")) dupSheet = i;
                if (ImGui::MenuItem("삭제", nullptr, false, d.screens.size() > 1)) removeSheet = i;
                ImGui::PopFont();
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar(2);
            if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            tooltip("두 번 눌러 이름 바꾸기 · 오른쪽 클릭 메뉴", "Ctrl+Tab");
            if (hv && !on) dl->AddRectFilled(a + ImVec2(0, px(6)), b - ImVec2(0, px(6)), col::hover);
            textAt(dl, F.mono, 11, ImVec2(x + px(12), cy - px(6)), on ? col::red : col::ink3, num);
            textAt(dl, F.med, 15, ImVec2(x + px(12) + nw + px(6), cy - px(10)), on || hv ? col::ink : col::ink2,
                   d.screens[i].name.c_str());
        }
        if (on) dl->AddRectFilled(ImVec2(a.x + px(6), b.y - px(3)), ImVec2(b.x - px(6), b.y), col::ink);
        x = b.x;
        ImGui::PopID();
    }
    {
        ImGui::SetCursorScreenPos(ImVec2(x + px(4), p.y + px(10)));
        ImVec2 a = ImGui::GetCursorScreenPos();
        float s = sz.y - px(20);
        if (ImGui::InvisibleButton("##addsheet", ImVec2(s, s))) {
            d.screens.push_back({});
            d.screens.back().name = "screen_" + std::to_string(d.screens.size());
            g.prevScreen = g.screen;
            g.screen = (int)d.screens.size() - 1;
            g.sel = 0;
            commit(d.screens.back().name + " 시트 추가");
        }
        bool hv = ImGui::IsItemHovered();
        if (hv) { dl->AddRectFilled(a, a + ImVec2(s, s), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        ImVec2 c = a + ImVec2(s, s) * 0.5f;
        dl->AddLine(c - ImVec2(px(5), 0), c + ImVec2(px(5), 0), hv ? col::ink : col::ink2, 1.3f);
        dl->AddLine(c - ImVec2(0, px(5)), c + ImVec2(0, px(5)), hv ? col::ink : col::ink2, 1.3f);
        tooltip("시트 추가");
    }
    if (dupSheet >= 0) {
        Screen cp = d.screens[dupSheet];
        for (auto& w : cp.widgets) {
            int old = w.id;
            w.id = d.nextId++;
            for (auto& c : cp.widgets) if (c.parent == old) c.parent = -w.id;
        }
        for (auto& w : cp.widgets) if (w.parent < 0) w.parent = -w.parent;
        cp.name += "_copy";
        d.screens.insert(d.screens.begin() + dupSheet + 1, cp);
        g.screen = dupSheet + 1;
        g.sel = 0;
        commit(cp.name + " 시트 복제");
    }
    if (removeSheet >= 0) {
        std::string n = d.screens[removeSheet].name;
        d.screens.erase(d.screens.begin() + removeSheet);
        g.screen = std::min(g.screen, (int)d.screens.size() - 1);
        g.sel = 0;
        commit(n + " 시트 삭제");
    }

    // 오른쪽: 되돌리기 / 다시 / 내보내기
    float rx = p.x + sz.x - px(18);
    ImGui::SetCursorScreenPos(ImVec2(rx - px(132), p.y + px(8)));
    if (lineButton("내보내기", ImVec2(px(132), sz.y - px(16)), 1, "Ctrl E")) openExportDialog();
    tooltip("LVGL C 코드로 내보내기");
    rx -= px(132) + px(8);
    ImGui::SetCursorScreenPos(ImVec2(rx - px(104), p.y + px(8)));
    if (lineButton("시운전", ImVec2(px(104), sz.y - px(16)), 0, "F5")) simToggle();
    tooltip("실제로 눌러 보기");
    rx -= px(104) + px(14);
    for (int k = 0; k < 2; k++) {
        bool isRedo = k == 0;
        float s = sz.y - px(16);
        ImVec2 a(rx - s, p.y + px(8));
        ImGui::SetCursorScreenPos(a);
        bool en = isRedo ? g.hist.canRedo() : g.hist.canUndo();
        ImGui::PushID(k);
        if (ImGui::InvisibleButton("##ur", ImVec2(s, s)) && en) { if (isRedo) redo(); else undo(); }
        bool hv = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hv && en) { dl->AddRectFilled(a, a + ImVec2(s, s), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        iconTurn(dl, a + ImVec2(s, s) * 0.5f + ImVec2(0, px(1)), en ? (hv ? col::ink : col::ink2) : col::hair2, isRedo);
        if (isRedo) tooltip("다시 진행", "Ctrl+Shift+Z");
        else tooltip("되돌리기", "Ctrl+Z");
        rx -= s + px(2);
    }
    rx -= px(12);
    // REV 칸: 지금 개정. 누르면 이력 전체
    {
        std::string rv = "REV " + g.hist.label(g.hist.cur);
        float w = textW(F.mono, 12, rv.c_str()) + px(16), h = px(22);
        ImVec2 a = ImFloor(ImVec2(rx - w, cy - h * 0.5f));
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##revbox", ImVec2(w, h))) g.openRevs = true;
        bool hv = ImGui::IsItemHovered();
        if (hv) { dl->AddRectFilled(a, a + ImVec2(w, h), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        float fresh = 1.f - (float)ImClamp((g.now - g.revAnimT) / 0.8, 0.0, 1.0);
        dl->AddRect(a, a + ImVec2(w, h), fresh > 0 ? col::red : col::ink);
        textAt(dl, F.mono, 12, a + ImVec2(px(8), px(4)), fresh > 0 ? col::red : col::ink, rv.c_str());
        tooltip("개정 이력 전체 보기");
        rx = a.x - px(16);
    }
    drawScaleBox(rx, cy);
    hline(dl, p.x, p.x + sz.x, p.y + sz.y - 1, col::ink);
}

// ───────────────────────── 왼쪽: 템플릿 자 ─────────────────────────
static void drawCutout(ImDrawList* dl, WType t, ImVec2 c, ImU32 line, ImU32 hole) {
    auto R = [&](float w, float h, float r = 0) {
        dl->AddRectFilled(c - ImVec2(w, h) * 0.5f, c + ImVec2(w, h) * 0.5f, hole, r);
        dl->AddRect(c - ImVec2(w, h) * 0.5f, c + ImVec2(w, h) * 0.5f, line, r);
    };
    const float u = g.S;
    switch (t) {
    case WType::Container:
        R(40 * u, 26 * u, 2 * u);
        dl->AddRect(c - ImVec2(14 * u, 7 * u), c + ImVec2(2 * u, 5 * u), line);
        break;
    case WType::Label: {
        float w = textW(F.med, 21, "Aa");
        textAt(dl, F.med, 21, c - ImVec2(w * 0.5f, px(16)), line, "Aa");
        dl->AddLine(c + ImVec2(-w * 0.5f, px(10)), c + ImVec2(w * 0.5f, px(10)), alpha(line, 0.5f));
        break;
    }
    case WType::Button:
        R(42 * u, 18 * u, 6 * u);
        dl->AddLine(c - ImVec2(9 * u, 0), c + ImVec2(9 * u, 0), alpha(line, 0.6f));
        break;
    case WType::Switch:
        R(32 * u, 16 * u, 8 * u);
        dl->AddCircle(c + ImVec2(8 * u, 0), 5 * u, line, 16);
        break;
    case WType::Checkbox:
        dl->AddRectFilled(c - ImVec2(20 * u, 6 * u), c - ImVec2(8 * u, -6 * u), hole, 2 * u);
        dl->AddRect(c - ImVec2(20 * u, 6 * u), c - ImVec2(8 * u, -6 * u), line, 2 * u);
        dl->AddLine(c + ImVec2(-17 * u, 0), c + ImVec2(-14.5f * u, 3 * u), line, 1.4f);
        dl->AddLine(c + ImVec2(-14.5f * u, 3 * u), c + ImVec2(-10.5f * u, -3 * u), line, 1.4f);
        dl->AddLine(c + ImVec2(-3 * u, 0), c + ImVec2(20 * u, 0), alpha(line, 0.6f));
        break;
    case WType::Slider:
        dl->AddLine(c - ImVec2(22 * u, 0), c + ImVec2(22 * u, 0), line);
        dl->AddCircleFilled(c + ImVec2(4 * u, 0), 6 * u, hole);
        dl->AddCircle(c + ImVec2(4 * u, 0), 6 * u, line, 16);
        break;
    case WType::Bar:
        R(44 * u, 10 * u, 5 * u);
        dl->AddRectFilled(c - ImVec2(22 * u, 5 * u) + ImVec2(1, 1), c + ImVec2(2 * u, 5 * u) - ImVec2(0, 1), alpha(line, 0.35f), 5 * u);
        break;
    case WType::Arc:
        dl->PathArcTo(c + ImVec2(0, 2 * u), 13 * u, 3.14159f * 0.75f, 3.14159f * 2.25f, 24);
        dl->PathStroke(line, 1.f);
        dl->PathArcTo(c + ImVec2(0, 2 * u), 9 * u, 3.14159f * 0.75f, 3.14159f * 2.25f, 24);
        dl->PathStroke(line, 1.f);
        break;
    case WType::Dropdown:
        R(44 * u, 18 * u, 2 * u);
        dl->AddTriangle(c + ImVec2(12 * u, -2 * u), c + ImVec2(18 * u, -2 * u), c + ImVec2(15 * u, 2 * u), line);
        break;
    case WType::Roller:
        R(30 * u, 30 * u, 2 * u);
        dl->AddRect(c - ImVec2(15 * u, 5 * u), c + ImVec2(15 * u, 5 * u), line);
        dl->AddLine(c + ImVec2(-8 * u, -10 * u), c + ImVec2(8 * u, -10 * u), alpha(line, 0.5f));
        dl->AddLine(c + ImVec2(-8 * u, 10 * u), c + ImVec2(8 * u, 10 * u), alpha(line, 0.5f));
        break;
    case WType::Textarea:
        R(44 * u, 20 * u, 2 * u);
        dl->AddLine(c + ImVec2(-15 * u, -5 * u), c + ImVec2(-15 * u, 5 * u), line, 1.3f);
        break;
    case WType::Led:
        dl->AddCircleFilled(c, 7 * u, hole, 20);
        dl->AddCircle(c, 7 * u, line, 20);
        for (int k = 0; k < 8; k++) {
            float a = k * 3.14159f / 4;
            dl->AddLine(c + ImVec2(cosf(a), sinf(a)) * 10 * u, c + ImVec2(cosf(a), sinf(a)) * 13 * u, alpha(line, 0.6f));
        }
        break;
    case WType::Spinner:
        dl->AddCircle(c, 11 * u, alpha(line, 0.35f), 24);
        dl->PathArcTo(c, 11 * u, -1.2f, 1.4f, 16);
        dl->PathStroke(line, 2.2f);
        break;
    case WType::Image:
        R(40 * u, 28 * u);
        dl->AddLine(c - ImVec2(20 * u, 14 * u), c + ImVec2(20 * u, 14 * u), alpha(line, 0.45f));
        dl->AddLine(c + ImVec2(-20 * u, 14 * u), c + ImVec2(20 * u, -14 * u), alpha(line, 0.45f));
        break;
    default: break;
    }
}

void drawLeft(ImVec2 p, ImVec2 sz) {
    ImGui::SetCursorScreenPos(p);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild("##left", sz, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Screen& s = curScreen();

    sectionHeader("01", "템플릿", g.armed >= 0 ? "그리기 도구 · Esc" : "끌어 놓기");
    {
        ImVec2 pp = ImGui::GetCursorScreenPos();
        float W = ImGui::GetContentRegionAvail().x;
        const int cols = 3;
        const float pad = px(8), cellH = px(60);
        int n = (int)WType::Count;
        int rows = (n + cols - 1) / cols;
        float cw = (W - pad * 2) / cols;
        float H = rows * cellH + pad * 2;
        dl->AddRectFilled(pp, pp + ImVec2(W, H), col::plate, px(3));
        dl->AddRect(pp, pp + ImVec2(W, H), col::hair2, px(3));
        // 템플릿 자의 인쇄 문구
        {   // 남는 칸에 각인
            ImVec2 e = pp + ImVec2(pad + 2 * cw, pad + (rows - 1) * cellH);
            float w1 = textW(F.mono, 11, "LVGL v9"), w2 = textW(F.ui, 14, "템플릿 자");
            dl->AddCircle(e + ImVec2(cw * 0.5f, px(13)), px(4), col::hair2, 16);
            textAt(dl, F.mono, 11, e + ImVec2((cw - w1) * 0.5f, px(23)), col::ink3, "LVGL v9");
            textAt(dl, F.ui, 14, e + ImVec2((cw - w2) * 0.5f, px(38)), col::ink3, "템플릿 자");
        }
        for (int k = 0; k * px(4) < W - px(12); k++) {   // 자 눈금
            float x = pp.x + px(6) + k * px(4), tl = k % 10 == 0 ? px(7) : k % 5 == 0 ? px(5) : px(3);
            vline(dl, x, pp.y + 1, pp.y + tl, alpha(col::ink3, k % 5 == 0 ? 0.75f : 0.45f));
        }
        for (int i = 0; i < n; i++) {
            WType t = (WType)i;
            const TypeInfo& ti = typeInfo(t);
            ImVec2 a = pp + ImVec2(pad + (i % cols) * cw, pad + (i / cols) * cellH);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##tpl", ImVec2(cw, cellH));
            bool hv = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
            if (act && g.drag == Drag::None && ImGui::IsMouseDragging(0, px(5))) { g.drag = Drag::Template; g.tplType = i; g.armed = -1; }
            if (ImGui::IsItemDeactivated() && g.drag != Drag::Template &&
                ImGui::GetIO().MouseDragMaxDistanceSqr[0] < px(5) * px(5))
                g.armed = g.armed == i ? -1 : i;
            ImGui::PopID();
            bool armed = g.armed == i || (g.drag == Drag::Template && g.tplType == i);
            if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImU32 line = armed ? col::red : hv ? col::ink : col::ink2;
            ImU32 hole = armed ? IM_COL32(0xF8, 0xEC, 0xE6, 255) : col::sheet;
            drawCutout(dl, t, a + ImVec2(cw * 0.5f, px(23)), line, hole);
            float lw = textW(F.ui, 14, ti.ko);
            textAt(dl, F.ui, 14, a + ImVec2((cw - lw) * 0.5f, px(40)), armed ? col::red : hv ? col::ink : col::ink2, ti.ko);
            if (hv && g.drag == Drag::None) {
                char tip[128];
                snprintf(tip, sizeof tip, "%s · 끌어서 놓거나, 누른 뒤 시트에 그리기", ti.creator);
                tooltip(tip);
            }
        }
        ImGui::SetCursorScreenPos(pp);
        ImGui::Dummy(ImVec2(W, H + px(14)));
    }

    char cnt[32];
    snprintf(cnt, sizeof cnt, "%d개", (int)s.widgets.size());
    sectionHeader("02", "부품표", cnt);
    ImGui::BeginChild("##bom", ImVec2(0, 0), 0, 0);
    {
        ImDrawList* bl = ImGui::GetWindowDrawList();
        const float rowH = px(26), indent = px(14);
        float RW = ImGui::GetContentRegionAvail().x;
        std::vector<bool> more;
        int pendingId = 0, pendingTarget = 0, pendingMode = 0;
        if (s.widgets.empty()) {
            pushFont(F.ui, 14);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::ink3));
            ImGui::TextWrapped("아직 부품이 없습니다. 위 템플릿에서 부품을 시트로 끌어 놓으세요.");
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        for (int i = 0; i < (int)s.widgets.size(); i++) {
            Widget& w = s.widgets[i];
            int depth = widgetDepth(s, w.id);
            bool last = isLastSibling(s, i);
            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImGui::PushID(w.id);
            bool on = w.id == g.sel;
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##row", ImVec2(RW, rowH));
            bool hv = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) selectWidget(w.id);
            if (hv && ImGui::IsMouseDoubleClicked(0)) { g.renameId = w.id; g.renameBuf = w.name; }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
                ImGui::SetDragDropPayload("BOM", &w.id, sizeof(int));
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                float fy = (ImGui::GetIO().MousePos.y - rp.y) / rowH;
                bool canIn = typeInfo(w.type).canParent;
                int mode = fy < 0.3f ? 0 : (fy > 0.7f || !canIn) ? 1 : 2;
                if (mode == 2) bl->AddRect(rp, rp + ImVec2(RW, rowH), col::red);
                else {
                    float y = mode == 0 ? rp.y : rp.y + rowH;
                    bl->AddLine(ImVec2(rp.x + px(24), y), ImVec2(rp.x + RW, y), col::red, 2.f);
                }
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("BOM", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    pendingId = *(const int*)pl->Data;
                    pendingTarget = w.id;
                    pendingMode = mode;
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsDragDropActive() && ImGui::GetDragDropPayload() &&
                *(const int*)ImGui::GetDragDropPayload()->Data == w.id)
                bl->AddRectFilled(rp, rp + ImVec2(RW, rowH), alpha(col::blue, 0.18f));
            if (hv && !on) bl->AddRectFilled(rp, rp + ImVec2(RW, rowH), col::hover);
            // 번호
            char num[8];
            snprintf(num, sizeof num, "%02d", i + 1);
            textAt(bl, F.mono, 11, rp + ImVec2(px(2), px(7)), on ? col::red : col::ink3, num);
            float bx = rp.x + px(26);
            // 트리 연결선
            if ((int)more.size() < depth + 1) more.resize(depth + 1, false);
            for (int k = 1; k < depth; k++)
                if (more[k]) bl->AddLine(ImVec2(bx + (k - 1) * indent + px(4), rp.y), ImVec2(bx + (k - 1) * indent + px(4), rp.y + rowH), col::hair2);
            if (depth > 0) {
                float lx = bx + (depth - 1) * indent + px(4);
                bl->AddLine(ImVec2(lx, rp.y), ImVec2(lx, last ? rp.y + rowH * 0.5f : rp.y + rowH), col::hair2);
                bl->AddLine(ImVec2(lx, rp.y + rowH * 0.5f), ImVec2(lx + px(7), rp.y + rowH * 0.5f), col::hair2);
            }
            more[depth] = !last;
            float nx = bx + depth * indent + (depth > 0 ? px(6) : 0.f);
            bool err = false;
            for (auto& is : g.issues) if (is.widget == w.id && is.error) err = true;
            if (g.renameId == w.id) {
                ImGui::SetCursorScreenPos(ImVec2(nx - px(2), rp.y + px(1)));
                pushFont(F.ui, 15);
                ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(RW - (nx - rp.x) - px(30));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(2), px(3)));
                bool enter = ImGui::InputText("##rename", &g.renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleVar();
                ImGui::PopFont();
                ImVec2 ia = ImGui::GetItemRectMin(), ib = ImGui::GetItemRectMax();
                bl->AddLine(ImVec2(ia.x, ib.y), ib, col::red);
                if (enter || ImGui::IsItemDeactivated()) {
                    if (!ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        std::string nn = identOrWarn(g.renameBuf);
                        if (!nn.empty()) nn = uniqueName(s, nn, w.id);
                        if (!nn.empty() && nn != w.name) { std::string old = w.name; w.name = nn; commit(old + " → " + nn); }
                    }
                    g.renameId = 0;
                }
            } else {
                ImU32 nc = on ? col::red : w.hidden ? col::ink3 : col::ink;
                std::string nm = ellipsize(F.ui, 15, w.name, RW - (nx - rp.x) - px(58));
                textAt(bl, on ? F.med : F.ui, 15, ImVec2(nx, rp.y + px(4)), nc, nm.c_str());
                if (w.hidden) {
                    float tw = textW(F.ui, 15, nm.c_str());
                    bl->AddLine(ImVec2(nx, rp.y + rowH * 0.5f + px(1)), ImVec2(nx + tw, rp.y + rowH * 0.5f + px(1)), col::ink3);
                }
                if (err) bl->AddCircleFilled(ImVec2(nx - px(5), rp.y + rowH * 0.5f), px(2.2f), col::red);
            }
            // 오른쪽: 형식 약어, 호버 시 숨김 토글
            if (hv && g.renameId != w.id) {
                const char* lab = w.hidden ? "보이기" : "숨기기";
                float lw = textW(F.ui, 14, lab);
                ImVec2 ha(rp.x + RW - lw - px(8), rp.y + px(3));
                ImGui::SetCursorScreenPos(ha);
                if (ImGui::InvisibleButton("##hide", ImVec2(lw + px(6), rowH - px(6)))) {
                    w.hidden = !w.hidden;
                    commit(w.name + (w.hidden ? " 숨김" : " 보임"));
                }
                bool hh = ImGui::IsItemHovered();
                textAt(bl, F.ui, 14, ha + ImVec2(px(3), px(2)), hh ? col::red : col::ink2, lab);
            } else {
                const char* ab = typeInfo(w.type).abbr;
                float aw = textW(F.mono, 11, ab);
                textAt(bl, F.mono, 11, ImVec2(rp.x + RW - aw - px(4), rp.y + px(7)), on ? col::red : col::ink3, ab);
            }
            hline(bl, rp.x, rp.x + RW, rp.y + rowH - 1, on ? col::red : col::hair);
            if (on && !hv) {   // 빨간 연필로 짚은 자리
                ImVec2 t(rp.x + RW - textW(F.mono, 11, typeInfo(w.type).abbr) - px(14), rp.y + rowH * 0.5f);
                bl->AddTriangleFilled(t + ImVec2(-px(5), 0), t + ImVec2(px(1), -px(4)), t + ImVec2(px(1), px(4)), col::red);
            }
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH));
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(RW, px(8)));
        if (pendingId) {
            const Widget* pw = findWidget(s, pendingId);
            std::string n = pw ? pw->name : "";
            if (moveSubtree(s, pendingId, pendingTarget, pendingMode))
                commit(n + (pendingMode == 2 ? " 넣기" : " 순서 변경"));
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();

    // 템플릿을 끄는 중: 커서 옆 꼬리표
    if (g.drag == Drag::Template && g.tplType >= 0) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 m = ImGui::GetIO().MousePos + ImVec2(px(14), px(14));
        const char* n = typeInfo((WType)g.tplType).ko;
        float w = textW(F.med, 14, n);
        fg->AddRectFilled(m, m + ImVec2(w + px(12), px(22)), col::sheet);
        fg->AddRect(m, m + ImVec2(w + px(12), px(22)), col::blueInk);
        textAt(fg, F.med, 14, m + ImVec2(px(6), px(3)), col::blueInk, n);
    }
}

// ───────────────────────── 오른쪽: 속성 ─────────────────────────
static void gap(float h) { ImGui::Dummy(ImVec2(1, px(h))); }

static void noteText(const char* t) {
    pushFont(F.ui, 14);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::ink2));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(t);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static void handleField(int r, const std::string& note) {
    if (r == 1) { g.needsBuild = true; g.live = true; g.liveNote = note; g.liveSel = g.sel; }
    if (r == 2) commit(note);
}

// 글자 맨 앞의 기호를 고른다. 이미 있으면 바꾼다
static bool symbolPick(const char* id, std::string* text) {
    ImGui::PushID(id);
    bool changed = false;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(p + ImVec2(0, px(4)));
    if (textLink("기호")) ImGui::OpenPopup("##sym");
    tooltip("LVGL 내장 기호 넣기");
    ImVec2 bmax = ImGui::GetItemRectMax();
    ImGui::SetNextWindowPos(ImVec2(bmax.x, bmax.y + px(6)), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    if (ImGui::BeginPopup("##sym")) {
        std::vector<uint32_t> cps = fontgen::decode(*text);
        const bool has = !cps.empty() && symbolOf(cps[0]);
        auto setSymbol = [&](uint32_t cp) {
            if (has) { cps.erase(cps.begin()); if (!cps.empty() && cps[0] == ' ') cps.erase(cps.begin()); }
            std::string rest;
            for (uint32_t c : cps) rest += fontgen::encode(c);
            *text = cp ? fontgen::encode(cp) + (rest.empty() ? "" : " " + rest) : rest;
            changed = true;
            ImGui::CloseCurrentPopup();
        };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cell = px(32);
        ImVec2 o = ImGui::GetCursorScreenPos();
        for (int i = 0; i < kSymbolCount; i++) {
            ImVec2 a = o + ImVec2((i % 10) * cell, (i / 10) * cell);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##s", ImVec2(cell, cell))) setSymbol(kSymbols[i].cp);
            bool hv = ImGui::IsItemHovered();
            ImGui::PopID();
            bool on = has && cps.size() && cps[0] == kSymbols[i].cp;
            if (hv) { dl->AddRectFilled(a, a + ImVec2(cell, cell), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
            if (on) dl->AddRect(a, a + ImVec2(cell, cell), col::red);
            std::string g1 = fontgen::encode(kSymbols[i].cp);
            float w = textW(F.ui, 17, g1.c_str());
            textAt(dl, F.ui, 17, a + ImVec2((cell - w) * 0.5f, (cell - textH(F.ui, 17)) * 0.5f), on ? col::red : col::ink, g1.c_str());
            if (hv) tooltip(kSymbols[i].ko, kSymbols[i].macro);
        }
        ImGui::SetCursorScreenPos(o);
        ImGui::Dummy(ImVec2(cell * 10, cell * ((kSymbolCount + 9) / 10) + px(4)));
        if (has && textLink("기호 없애기", col::ink2)) setSymbol(0);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

static double luma(int c) { return 0.2126 * (c >> 16 & 255) + 0.7152 * (c >> 8 & 255) + 0.0722 * (c & 255); }

// 움직임: 시트가 뜰 때 부품이 움직이는 순서. 도면은 움직임 없이 놓인 모습이고 시운전과 내보낸 코드에서 움직인다
static void drawMotions(Widget& w, float W) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char b[128];
    int remove = -1;
    const float ind = px(24), iw = W - ind;
    auto indent = [&] { ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(ind, 0)); };
    for (int k = 0; k < (int)w.motions.size(); k++) {
        Motion& m = w.motions[k];
        const MotionInfo& mi = motionInfo(m.kind);
        ImGui::PushID(5000 + k);
        ImVec2 rp = ImGui::GetCursorScreenPos();
        snprintf(b, sizeof b, "%d.", k + 1);
        textAt(dl, F.mono, 13, rp + ImVec2(0, px(5)), col::red, b);
        ImGui::SetCursorScreenPos(rp + ImVec2(ind, 0));
        pushFont(F.ui, 15);
        if (comboField("##kind", mi.ko, iw - px(26))) {
            for (int c = 0; c < MO_COUNT; c++) {
                if (!motionApplies(c, w.type)) continue;
                const MotionInfo& ci = motionInfo(c);
                snprintf(b, sizeof b, "%s  · %s", ci.ko, ci.enter ? "들어오기" : ci.exit ? "나가기" : "되풀이");
                if (ImGui::Selectable(b, c == m.kind) && c != m.kind) {
                    Motion nm = defaultMotion(c);
                    nm.delay = m.delay;
                    m = nm;
                    commit(w.name + " 움직임 " + ci.ko);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        ImGui::SameLine(0, px(4));
        ImVec2 xp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##del", ImVec2(px(22), px(26)))) remove = k;
        const bool dh = ImGui::IsItemHovered();
        const ImVec2 c = xp + ImVec2(px(11), px(13));
        const float kk = px(3.5f);
        dl->AddLine(c - ImVec2(kk, kk), c + ImVec2(kk, kk), dh ? col::red : col::ink3, 1.2f);
        dl->AddLine(c + ImVec2(-kk, kk), c + ImVec2(kk, -kk), dh ? col::red : col::ink3, 1.2f);
        if (dh) tooltip("움직임 삭제");

        const float hw = (iw - px(12)) * 0.5f;
        indent();
        int r1 = scrubInt("##delay", "시작 ms", &m.delay, 0, 600000, hw);
        ImGui::SameLine(0, px(12));
        int r2 = scrubInt("##dur", "길이 ms", &m.dur, 1, 60000, hw);
        handleField(std::max(r1, r2), w.name + " 움직임 시간");

        indent();
        {
            ImVec2 gp = ImGui::GetCursorScreenPos();
            textAt(dl, F.ui, 14, gp + ImVec2(0, px(4)), col::ink3, "느낌");
            ImGui::SetCursorScreenPos(gp + ImVec2(px(38), 0));
            pushFont(F.ui, 15);
            if (comboField("##ease", motionEaseKo(m.ease), iw - px(38))) {
                for (int e = 0; e < ME_COUNT; e++)
                    if (ImGui::Selectable(motionEaseKo(e), e == m.ease) && e != m.ease) { m.ease = e; commit(w.name + " 움직임 " + motionEaseKo(e)); }
                ImGui::EndCombo();
            }
            ImGui::PopFont();
        }
        if (mi.usesDir) {
            indent();
            ImVec2 gp = ImGui::GetCursorScreenPos();
            textAt(dl, F.ui, 14, gp + ImVec2(0, px(4)), col::ink3, m.kind == MO_SLIDE_OUT ? "어디로" : m.kind == MO_WIPE ? "어디부터" : "어디서");
            ImGui::SetCursorScreenPos(gp + ImVec2(px(58), 0));
            const char* dirs[4] = {"왼쪽", "오른쪽", "위", "아래"};
            int dsel = m.dir;
            if (segTabs("##dir", dirs, 4, &dsel) && dsel != m.dir) { m.dir = dsel; commit(w.name + " 움직임 " + motionDirKo(m.kind, dsel)); }
        }
        if (mi.amountKo) {
            indent();
            handleField(scrubInt("##amt", mi.amountKo, &m.amount, mi.amountMin, mi.amountMax, hw), w.name + " 움직임 " + mi.amountKo);
            ImGui::SameLine(0, px(12));
        } else indent();
        {
            ImVec2 gp = ImGui::GetCursorScreenPos();
            if (toggleField("##rep", &m.repeat)) commit(w.name + (m.repeat ? " 움직임 되풀이" : " 움직임 한 번"));
            textAt(dl, F.ui, 14, gp + ImVec2(px(40), px(4)), m.repeat ? col::ink : col::ink3, "되풀이");
        }
        gap(8);
        ImGui::PopID();
    }
    if (remove >= 0) { w.motions.erase(w.motions.begin() + remove); commit(w.name + " 움직임 삭제"); }
    if (textLink("+ 움직임 추가")) {
        int end = 0;
        for (auto& m : w.motions) end = std::max(end, motionEnd(m));
        Motion nm = defaultMotion(w.motions.empty() ? MO_FADE_IN : MO_FADE_OUT);
        nm.delay = w.motions.empty() ? 0 : end + 1500;   // 두 번째부터는 잠시 머물렀다가 나간다
        w.motions.push_back(nm);
        commit(w.name + " 움직임 추가");
    }
    if (!w.motions.empty()) {
        gap(4);
        noteText("도면에는 움직임 없이 놓인 모습이 그려집니다. 시트의 움직임 표에서 눈금을 끌면 그 순간을, 시운전(F5)에서는 움직이는 모습을 봅니다(Ctrl+R 처음부터).");
    }
}

// 움직임 공정표: 시트의 움직임을 시간 축에 늘어놓는다. 줄을 누르면 그 부품을 고른다
static void drawMotionChart(Screen& s, float W) {
    std::vector<const Widget*> rows;
    for (auto& w : s.widgets) if (!w.motions.empty()) rows.push_back(&w);
    const bool next = !s.next.empty() && s.nextMs > 0;
    if (rows.empty() && !next) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char b[128];
    int T = std::max(sheetMotionEnd(s), next ? s.nextMs : 0);
    T = std::max(1000, (T + 999) / 1000 * 1000);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float nameW = px(86), x0 = p.x + nameW, tw = W - nameW - px(6), headH = px(18), rowH = px(20);
    int step = 500;   // 눈금 글자가 겹치지 않을 만큼 띄운다
    for (int c : {500, 1000, 2000, 5000, 10000, 30000, 60000}) { step = c; if (tw * (float)c / (float)T >= px(30)) break; }
    const float y0 = p.y + headH, y1 = y0 + rowH * (float)std::max<size_t>(rows.size(), 1);
    auto X = [&](int ms) { return x0 + tw * (float)std::min(ms, T) / (float)T; };
    for (int t = 0; t <= T; t += step) {
        const float x = floorf(X(t)) + 0.5f;
        vline(dl, x, y0 - px(3), y1, t == 0 ? col::hair2 : col::hair);
        if (t % 1000) snprintf(b, sizeof b, "%.1f", t / 1000.0);
        else snprintf(b, sizeof b, "%ds", t / 1000);
        const float lw = textW(F.mono, 11, b);
        textAt(dl, F.mono, 11, ImVec2(std::min(x - lw * 0.5f, p.x + W - lw), p.y), col::ink3, b);
    }
    hline(dl, p.x, p.x + W, y1, col::hair2);
    {   // 눈금 줄을 누르거나 끌면 그 순간의 모습을 도면에 그린다
        ImGui::SetCursorScreenPos(ImVec2(x0 - px(4), p.y));
        ImGui::InvisibleButton("##scrub", ImVec2(tw + px(8), headH));
        if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); tooltip("끌어서 그 순간의 모습 보기"); }
        if (ImGui::IsItemActive()) {
            int t = (int)lroundf((ImGui::GetIO().MousePos.x - x0) / tw * (float)T / 50.f) * 50;
            t = ImClamp(t, 0, T);
            if (t != g.motionT) { g.motionT = t; g.needsBuild = true; }
        }
    }
    if (g.motionT >= 0) {
        const float x = floorf(X(g.motionT)) + 0.5f;
        vline(dl, x, p.y + px(12), y1, col::red, 1.4f);
        dl->AddTriangleFilled(ImVec2(x - px(4), p.y + px(10)), ImVec2(x + px(4), p.y + px(10)), ImVec2(x, p.y + px(15)), col::red);
    }
    std::string tip;
    for (size_t i = 0; i < rows.size(); i++) {
        const Widget& w = *rows[i];
        const float y = y0 + rowH * (float)i;
        ImGui::SetCursorScreenPos(ImVec2(p.x, y));
        ImGui::PushID(w.id);
        if (ImGui::InvisibleButton("##row", ImVec2(W, rowH))) selectWidget(w.id);
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) { dl->AddRectFilled(ImVec2(p.x - px(4), y), ImVec2(p.x + W, y + rowH), alpha(col::hover, 0.9f)); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        const bool sel = g.sel == w.id;
        textAt(dl, F.ui, 13, ImVec2(p.x, y + px(3)), sel ? col::red : col::ink2, ellipsize(F.ui, 13, w.name, nameW - px(8)).c_str());
        for (const Motion& m : w.motions) {
            const MotionInfo& mi = motionInfo(m.kind);
            const float a = X(m.delay), e = std::max(X(motionEnd(m)), a + px(3));
            const ImVec2 ta(a, y + px(5)), tb(e, y + rowH - px(5));
            const ImU32 c = sel ? col::red : col::ink;
            if (mi.exit) dl->AddRect(ta, tb, c, 0.f, 1.2f);
            else dl->AddRectFilled(ta, tb, mi.enter ? c : alpha(c, 0.45f));
            if (m.repeat) dashLine(dl, ImVec2(e, (ta.y + tb.y) * 0.5f), ImVec2(x0 + tw, (ta.y + tb.y) * 0.5f), alpha(c, 0.6f), px(3), px(3));
            const ImVec2 mp = ImGui::GetIO().MousePos;
            if (hov && mp.x >= a - px(2) && mp.x <= e + px(2)) {
                snprintf(b, sizeof b, "%s · %.2f–%.2f초%s", mi.ko, m.delay / 1000.0, motionEnd(m) / 1000.0, m.repeat ? " · 되풀이" : "");
                tip = w.name + "  " + b;
            }
        }
    }
    if (rows.empty()) textAt(dl, F.ui, 13, ImVec2(p.x, y0 + px(3)), col::ink3, "움직이는 부품 없음");
    if (next) {   // 다음 시트로 넘어가는 때
        const float x = floorf(X(s.nextMs)) + 0.5f;
        vline(dl, x, y0 - px(3), y1 + px(4), col::ink);
        snprintf(b, sizeof b, "→ %s", s.next.c_str());
        const float bw = textW(F.med, 13, b);
        textAt(dl, F.med, 13, ImVec2(std::min(x - bw * 0.5f, p.x + W - bw), y1 + px(6)), col::ink, b);
    }
    if (!tip.empty()) tooltip(tip.c_str());
    ImGui::SetCursorScreenPos(ImVec2(p.x, y1 + (next ? px(26) : px(6))));
    ImGui::Dummy(ImVec2(W, 1));
    if (g.motionT >= 0) {
        snprintf(b, sizeof b, "도면이 %.2f초의 모습입니다.", g.motionT / 1000.0);
        textAt(dl, F.ui, 14, ImGui::GetCursorScreenPos() + ImVec2(0, px(2)), col::red, b);
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(textW(F.ui, 14, b) + px(10), 0));
        if (textLink("놓인 모습으로", col::ink2)) { g.motionT = -1; g.needsBuild = true; }
    } else noteText("눈금 줄을 끌면 그 순간의 모습이 도면에 그려집니다.");
}

// 라벨의 긴 글 처리와 글자 꾸밈
static void drawTextFx(Widget& w, float W) {
    char b[120];
    if (!w.autoSize) {
        fieldLabel("긴 글");
        const char* modes[4] = {"줄바꿈", "점줄임", "흐르기", "자르기"};
        int m = w.x2.longMode;
        if (segTabs("##long", modes, 4, &m) && m != w.x2.longMode) { w.x2.longMode = m; commit(w.name + " 긴 글 " + modes[m]); }
    }
    fieldLabel("글자 꾸밈");
    const char* fxs[4] = {"없음", "그림자", "외곽선", "번짐"};
    int f = w.x2.fx;
    if (segTabs("##fx", fxs, 4, &f) && f != w.x2.fx) {
        // 글자색의 반대 밝기로 시작한다
        const int tc = lvhost::styleValue(w.id, SP_TEXT_COLOR);
        if (w.x2.fx == 0) w.x2.fxColor = luma(tc) > 128 ? 0x000000 : 0xFFFFFF;
        w.x2.fx = f;
        if (f == 1) { w.x2.fxDx = 1; w.x2.fxDy = 2; w.x2.fxOpa = 200; }
        if (f == 2) { w.x2.fxSize = 2; w.x2.fxOpa = 255; }
        if (f == 3) { w.x2.fxSize = 5; w.x2.fxDx = 0; w.x2.fxDy = 1; w.x2.fxOpa = 255; }
        commit(w.name + " 글자 꾸밈 " + fxs[f]);
    }
    if (w.x2.fx) {
        fieldLabel("꾸밈 색");
        int cr = colorField("##fxc", &w.x2.fxColor, true, w.x2.fxColor, ImGui::GetContentRegionAvail().x);
        if (cr == 3) w.x2.fxColor = 0;
        handleField(cr == 3 ? 2 : cr, w.name + " 꾸밈 색");
        const float hw = (W - px(16)) * 0.5f;
        auto two = [&](const char* id1, const char* l1, int* v1, int a1, int b1, const char* id2, const char* l2, int* v2, int a2, int b2) {
            int r1 = scrubInt(id1, l1, v1, a1, b1, hw);
            ImGui::SameLine(0, px(16));
            int r2 = scrubInt(id2, l2, v2, a2, b2, hw);
            handleField(std::max(r1, r2), w.name + " 글자 꾸밈");
        };
        int pct = (int)lround(w.x2.fxOpa / 2.55);
        const int before = pct;
        if (w.x2.fx == 2) two("##fs", "두께", &w.x2.fxSize, 1, 6, "##fo", "세기 %", &pct, 0, 100);
        else {
            if (w.x2.fx == 3) two("##fs", "반경", &w.x2.fxSize, 1, 12, "##fo", "세기 %", &pct, 0, 100);
            else { int r = scrubInt("##fo", "세기 %", &pct, 0, 100, hw); handleField(r, w.name + " 글자 꾸밈"); }
            two("##fx", "가로", &w.x2.fxDx, -20, 20, "##fy", "세로", &w.x2.fxDy, -20, 20);
        }
        if (pct != before) w.x2.fxOpa = ImClamp((int)lround(pct * 2.55), 0, 255);
        if (w.x2.fx == 3) noteText("번짐은 글자를 49번 그립니다. 느린 장치에서는 외곽선이 가볍습니다.");
        if (w.x2.longMode == 2) noteText("흐르는 글자에는 꾸밈이 적용되지 않습니다.");
    }
    // 글자 밑판: 스타일의 배경을 한 번에
    fieldLabel("밑판");
    StyleVal* st = w.style[ST_DEFAULT];
    int cur = !st[SP_BG_OPA].set || st[SP_BG_OPA].v == 0 ? 0 : !st[SP_BG_COLOR].set ? -1 : luma(st[SP_BG_COLOR].v) < 60 ? 1 : st[SP_BG_COLOR].v == 0xFFDF5A ? 3 : luma(st[SP_BG_COLOR].v) > 230 ? 2 : -1;
    const char* plates[4] = {"없음", "어둡게", "밝게", "형광펜"};
    const int was = cur;
    if (segTabs("##plate", plates, 4, &cur) && cur != was) {
        for (int p : {SP_BG_COLOR, SP_BG_OPA, SP_RADIUS, SP_PAD}) st[p].set = false;
        if (cur > 0) {
            static const int color[4] = {0, 0x000000, 0xFFFFFF, 0xFFDF5A}, opa[4] = {0, 150, 190, 255}, rad[4] = {0, 6, 6, 2}, pad[4] = {0, 6, 6, 3};
            st[SP_BG_COLOR] = {true, color[cur]};
            st[SP_BG_OPA] = {true, opa[cur]};
            st[SP_RADIUS] = {true, rad[cur]};
            st[SP_PAD] = {true, pad[cur]};
            const int tc = lvhost::styleValue(w.id, SP_TEXT_COLOR);
            if ((cur == 1) != (luma(tc) > 128)) st[SP_TEXT_COLOR] = {true, cur == 1 ? 0xFFFFFF : 0x1B1B1B};   // 판 위에서 읽히는 글자색
        }
        commit(w.name + " 밑판 " + plates[cur]);
    }
    (void)b;
}

// 그림 부품의 사양
static void drawImageProps(Widget& w, float W) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char b[200];
    if (g.focusName == w.id) { g.focusName = 0; importImage(w); }
    const auto* pa = image::find(g.doc, w.text);
    const std::shared_ptr<const ImageAsset> a = pa ? *pa : nullptr;
    // 견본
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float H = px(112);
    if (ImGui::InvisibleButton("##thumb", ImVec2(W, H))) importImage(w);
    const bool hv = ImGui::IsItemHovered();
    if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    tooltip(a ? "눌러서 다른 그림으로 바꾸기" : "눌러서 그림 불러오기");
    dl->AddRectFilled(p, p + ImVec2(W, H), col::sheet);
    dl->AddRect(p, p + ImVec2(W, H), hv ? col::ink2 : col::hair2);
    if (a) {
        const float k = std::min({(W - px(16)) / a->w, (H - px(16)) / a->h, 4.f * g.S});
        const int tw = std::max(1, (int)(a->w * k)), th = std::max(1, (int)(a->h * k));
        static std::weak_ptr<const ImageAsset> shown;
        static int sw = 0, sh = 0;
        if (shown.lock() != a || sw != tw || sh != th) {
            const std::vector<uint32_t> px2 = image::fitted(a, tw, th, image::FIT_STRETCH);
            platformUploadCanvas(px2.data(), tw, th, 1, 3);
            shown = a;
            sw = tw;
            sh = th;
        }
        const ImVec2 o = ImFloor(p + (ImVec2(W, H) - ImVec2((float)tw, (float)th)) * 0.5f);
        if (void* tex = platformCanvasTexture(3)) dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex), o, o + ImVec2((float)tw, (float)th));
        dl->AddRect(o - ImVec2(1, 1), o + ImVec2(tw + 1.f, th + 1.f), col::hair2);
    } else {
        const char* t1 = "그림을 끌어다 놓거나 눌러서 불러오세요";
        const char* t2 = "PNG · JPG · BMP · GIF";
        textAt(dl, F.ui, 15, ImFloor(p + ImVec2((W - textW(F.ui, 15, t1)) * 0.5f, H * 0.5f - px(22))), hv ? col::ink : col::ink2, t1);
        textAt(dl, F.mono, 12, ImFloor(p + ImVec2((W - textW(F.mono, 12, t2)) * 0.5f, H * 0.5f + px(6))), col::ink3, t2);
    }
    gap(4);
    if (a) {
        snprintf(b, sizeof b, "%s  ·  %d × %d", a->file.c_str(), a->w, a->h);
        const ImVec2 ip = ImGui::GetCursorScreenPos();
        textAt(dl, F.ui, 14, ip + ImVec2(0, px(3)), col::ink2, ellipsize(F.ui, 14, b, W - px(100)).c_str());
        ImGui::SetCursorScreenPos(ip + ImVec2(W - textW(F.med, 14, "그림 크기대로"), px(3)));
        if (textLink("그림 크기대로")) { sizeToImage(w, *a, 1.0); commit(w.name + " 그림 크기대로"); }
        tooltip("부품을 그림의 실제 크기로(화면보다 크면 화면에 맞춰)");
        ImGui::SetCursorScreenPos(ip + ImVec2(0, px(28)));
        if (g.doc.images.size() > 1) {
            fieldLabel("그림 고르기");
            if (comboField("##pickimg", w.text.c_str(), ImGui::GetContentRegionAvail().x)) {
                for (auto& x : g.doc.images)
                    if (ImGui::Selectable((x->name + "   " + x->file).c_str(), x->name == w.text)) { w.text = x->name; commit(w.name + " 그림 " + x->file); }
                endComboField();
            }
        }
        fieldLabel("맞추기");
        const char* fits[image::FIT_COUNT];
        for (int i = 0; i < image::FIT_COUNT; i++) fits[i] = image::fitKo(i);
        int f = image::fit(w);
        if (segTabs("##fit", fits, image::FIT_COUNT, &f) && f != w.minv) { w.minv = f; commit(w.name + " 맞추기 " + fits[f]); }
        ImageFx& x = w.x2.img;
        const float hw = (W - px(16)) * 0.5f;
        auto two = [&](const char* l1, int* v1, int a1, int b1, const char* l2, int* v2, int a2, int b2) {
            ImGui::PushID(l1);
            const int r1 = scrubInt("##a", l1, v1, a1, b1, hw);
            ImGui::SameLine(0, px(16));
            const int r2 = scrubInt("##b", l2, v2, a2, b2, hw);
            ImGui::PopID();
            handleField(std::max(r1, r2), w.name + " 그림 손보기");
        };
        if (f == image::FIT_COVER || f == image::FIT_CENTER) two("자리 가로%", &x.focusX, 0, 100, "자리 세로%", &x.focusY, 0, 100);
        two("흐리게", &x.blur, 0, 40, "밝기", &x.bright, -100, 100);
        two("채도", &x.sat, -100, 100, "그늘 %", &x.shade, 0, 100);
        if (x.shade > 0) {
            fieldLabel("그늘 자리");
            const char* dirs[3] = {"아래", "위", "전체"};
            int sd = x.shadeDir;
            if (segTabs("##shade", dirs, 3, &sd) && sd != x.shadeDir) { x.shadeDir = sd; commit(w.name + " 그늘 " + dirs[sd]); }
        }
        {
            fieldLabel("도트 그림");
            bool on = x.crisp != 0;
            if (toggleField("##crisp", &on)) { x.crisp = on; commit(w.name + (on ? " 도트 또렷하게" : " 도트 부드럽게")); }
            tooltip("확대해도 픽셀 모서리를 또렷하게");
            fieldLabel("배경 지우기");
            on = x.cut > 0;
            const float tx = ImGui::GetCursorScreenPos().x;
            if (toggleField("##cut", &on)) { x.cut = on ? 25 : 0; commit(w.name + (on ? " 배경 지움" : " 배경 되살림")); }
            tooltip("가장자리와 이어진 단색·체크무늬 배경을 투명하게");
            if (x.cut > 0) {
                ImGui::SameLine(0, px(10));
                (void)tx;
                handleField(scrubInt("##cutv", "범위", &x.cut, 1, 100, ImGui::GetContentRegionAvail().x), w.name + " 배경 지우기");
            }
        }
    } else {
        fieldLabel("C 이름");
        handleField(fieldText("##src", &w.text, ImGui::GetContentRegionAvail().x, "이미 가진 lv_image_dsc_t"), w.name + " 소스");
    }
    fieldLabel("보관");
    const char* places[2] = {"플래시", "SD 카드"};
    int pl = image::onSd(w) ? 1 : 0;
    if (segTabs("##place", places, 2, &pl) && pl != w.value) { w.value = pl; commit(w.name + " 보관 " + places[pl]); }
    if (image::onSd(w)) {
        fieldLabel("SD 경로");
        pushFont(F.mono, 13);
        const std::string hint = a ? image::sdPath(g.doc, w) : "/사진.bin";
        handleField(fieldText("##sdpath", &w.text2, ImGui::GetContentRegionAvail().x, hint.c_str()), w.name + " SD 경로");
        ImGui::PopFont();
    }
    if (a) {
        int iw, ih;
        image::size(g.doc, w, iw, ih);
        const auto v = image::variant(a, iw, ih, image::fit(w), g.doc.depth, w.x2.img);
        snprintf(b, sizeof b, "%s  ·  %d × %d  ·  %.1f KB%s", v->cfName() + 16, iw, ih, v->data.size() / 1024.0, image::onSd(w) ? " (카드에 .bin으로)" : "");
        noteText(b);
    } else if (image::onSd(w)) noteText("SD에 이미 있는 LVGL .bin 파일의 경로를 적으세요.");
}

// 스타일 한 줄
static void styleRow(Widget& w, int st, int p) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char b[200];
    const StylePropInfo& pi = stylePropInfo(p);
    StyleVal& sv = w.style[st][p];
    ImGui::PushID(p);
    fieldLabel(pi.ko, sv.set);
    const float fw = ImGui::GetContentRegionAvail().x;
    const int themeVal = lvhost::styleValue(w.id, p);
    snprintf(b, sizeof b, "%s %s %s", w.name.c_str(), stateKo(st), pi.ko);
    auto combo = [&](const char* preview) {
        pushFont(F.ui, 15);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(sv.set ? col::ink : col::ink3));
        const bool open = comboField("##combo", preview, fw);
        ImGui::PopStyleColor();
        return open;
    };
    auto resetButton = [&](float rw2) {
        ImGui::SameLine(0, 0);
        ImVec2 rp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##rs", ImVec2(rw2, px(26)))) { sv.set = false; commit(b + std::string(" 기본값")); }
        const bool rh = ImGui::IsItemHovered();
        const ImVec2 c = rp + ImVec2(rw2 * 0.5f, px(13));
        const float k = px(3.5f);
        dl->AddLine(c - ImVec2(k, k), c + ImVec2(k, k), rh ? col::red : col::ink3, 1.2f);
        dl->AddLine(c + ImVec2(-k, k), c + ImVec2(k, -k), rh ? col::red : col::ink3, 1.2f);
        if (rh) tooltip("테마 기본값으로");
    };
    if (pi.kind == SK_COLOR) {
        int v = sv.set ? sv.v : themeVal;
        const int r = colorField("##c", &v, sv.set, themeVal, fw);
        if (r == 1 || r == 2) {
            if (!sv.set && p == SP_BG_COLOR && !w.style[st][SP_BG_OPA].set && lvhost::styleValue(w.id, SP_BG_OPA) == 0)
                w.style[st][SP_BG_OPA] = {true, 255};  // 투명한 배경에 색만 주면 안 보이므로
            if (!sv.set && p == SP_IMG_TINT && !w.style[st][SP_IMG_TINT_OPA].set) w.style[st][SP_IMG_TINT_OPA] = {true, 110};
            sv.set = true;
            sv.v = v;
        }
        if (r == 3) sv.set = false;
        handleField(r == 3 ? 2 : r, b);
    } else if (pi.kind == SK_FONT) {
        int fs, ff;
        resolveFont(curScreen(), w, st, fs, ff);
        char pv[32];
        snprintf(pv, sizeof pv, "%d px", sv.set ? sv.v : fs);
        if (combo(pv)) {
            if (ImGui::Selectable("기본값", !sv.set)) { sv.set = false; commit(b); }
            for (int k = 0; k < kFontSizeCount; k++) {
                char it[32];
                snprintf(it, sizeof it, "%d px", kFontSizes[k]);
                if (ImGui::Selectable(it, sv.set && sv.v == kFontSizes[k])) { sv = {true, kFontSizes[k]}; commit(b); }
            }
            endComboField();
        }
        ImGui::PopFont();
    } else if (pi.kind == SK_FACE) {
        int fs, ff;
        resolveFont(curScreen(), w, st, fs, ff);
        const auto* cur = fontface::find(g.doc, ff);
        if (combo(cur ? (*cur)->name.c_str() : "기본 (Montserrat · Plex)")) {
            auto set = [&](int id, const std::string& nm) { sv = {true, id}; commit(b + std::string(" ") + nm); };
            if (ImGui::Selectable("기본 (Montserrat · Plex)", !sv.set || sv.v == 0)) { sv.set = w.parent != 0; sv.v = 0; commit(b + std::string(" 기본")); }
            for (int i = 0; i < fontface::kBundledCount; i++) {
                const std::string key = std::string("@") + fontface::kBundled[i].file;
                if (ImGui::Selectable(fontface::kBundled[i].name, cur && (*cur)->key == key)) { const int id = fontface::add(g.doc, key, 0, fontface::kBundled[i].name); if (id) set(id, fontface::kBundled[i].name); }
            }
            bool head = false;
            for (size_t i = 0; i < g.doc.fonts.size(); i++) {
                const auto f = g.doc.fonts[i];
                if (!f->key.empty() && f->key[0] == '@') continue;
                if (!head) { ImGui::Separator(); head = true; }
                if (ImGui::Selectable((f->name + (f->file ? "" : "  (이 PC에 없음)") + "##d" + std::to_string(f->id)).c_str(), f->id == ff)) set(f->id, f->name);
            }
            ImGui::Separator();
            if (ImGui::Selectable("PC 글꼴에서 고르기…")) { g.openFonts = true; g.fontFor = w.id; g.fontState = st; }
            if (ImGui::Selectable("글꼴 파일 불러오기…")) {
                std::wstring path;
                if (platformOpenFont(path)) {
                    const std::string file = wideToUtf8(path.substr(path.find_last_of(L"\\/") + 1));
                    const int id = fontface::add(g.doc, wideToUtf8(path), 0, file.substr(0, file.find_last_of('.')));
                    if (id) set(id, file);
                    else showToast("글꼴 파일을 열 수 없습니다 · " + file);
                }
            }
            endComboField();
        }
        ImGui::PopFont();
        if (cur && (*cur)->restricted) noteText("이 글꼴은 내장을 제한하고 있습니다. 배포 전에 사용 조건을 확인하세요.");
    } else if (pi.kind == SK_ALIGN || pi.kind == SK_DECOR) {
        static const char* al[4] = {"자동", "왼쪽", "가운데", "오른쪽"};
        static const char* dc[4] = {"없음", "밑줄", "취소선", "밑줄 · 취소선"};
        const char** names = pi.kind == SK_ALIGN ? al : dc;
        if (combo(names[sv.set ? sv.v & 3 : 0])) {
            for (int i = 0; i < 4; i++)
                if (ImGui::Selectable(names[i], sv.set ? sv.v == i : i == 0)) { sv = {i != 0, i}; commit(b + std::string(" ") + names[i]); }
            endComboField();
        }
        ImGui::PopFont();
    } else if (pi.kind == SK_PCT) {
        int v = (int)lround((sv.set ? sv.v : themeVal) / 2.55);
        const float rw2 = sv.set ? px(22) : 0.f;
        // 막대: 투명할수록 비어 보인다
        const ImVec2 bp = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(bp + ImVec2(0, px(22)), bp + ImVec2((fw - rw2) * v / 100.f, px(25)), alpha(col::ink, sv.set ? 0.35f : 0.12f));
        const int r = scrubInt("##n", "", &v, 0, 100, fw - rw2 - px(16), !sv.set);
        textAt(dl, F.mono, 12, ImVec2(bp.x + fw - rw2 - px(13), bp.y + px(5)), sv.set ? col::ink2 : col::ink3, "%");
        if (r) { sv.set = true; sv.v = ImClamp((int)lround(v * 2.55), 0, 255); }
        handleField(r, b);
        if (sv.set) {
            if (r == 0) { ImGui::SameLine(0, px(16)); ImGui::SetCursorScreenPos(ImVec2(bp.x + fw - rw2, bp.y)); }
            ImGui::SetCursorScreenPos(ImVec2(bp.x + fw - rw2, bp.y));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::SameLine(0, 0);
            ImGui::SetCursorScreenPos(ImVec2(bp.x + fw - rw2, bp.y));
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##rs", ImVec2(rw2, px(26)))) { sv.set = false; commit(b + std::string(" 기본값")); }
            const bool rh = ImGui::IsItemHovered();
            const ImVec2 c = rp + ImVec2(rw2 * 0.5f, px(13));
            const float k = px(3.5f);
            dl->AddLine(c - ImVec2(k, k), c + ImVec2(k, k), rh ? col::red : col::ink3, 1.2f);
            dl->AddLine(c + ImVec2(-k, k), c + ImVec2(k, -k), rh ? col::red : col::ink3, 1.2f);
            if (rh) tooltip("테마 기본값으로");
        }
    } else {
        int v = sv.set ? sv.v : themeVal;
        const float rw2 = sv.set ? px(22) : 0.f;
        const int r = scrubInt("##n", "", &v, pi.minv, pi.maxv, fw - rw2, !sv.set);
        if (r) { sv.set = true; sv.v = v; }
        handleField(r, b);
        if (sv.set) resetButton(rw2);
    }
    ImGui::PopID();
}

static void drawWidgetProps(Widget& w) {
    Screen& s = curScreen();
    const TypeInfo& ti = typeInfo(w.type);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x;
    char b[160];

    // 머리: 부품 번호, 형식, 이름
    {
        ImVec2 hp = ImGui::GetCursorScreenPos();
        char num[8];
        snprintf(num, sizeof num, "%02d", widgetNumber(w.id));
        // 말풍선 번호
        ImVec2 c = hp + ImVec2(px(11), px(11));
        dl->AddCircle(c, px(10.5f), col::red, 24, 1.2f);
        float nw = textW(F.mono, 11, num);
        textAt(dl, F.mono, 11, c - ImVec2(nw * 0.5f, px(7)), col::red, num);
        snprintf(b, sizeof b, "%s · %s", ti.ko, ti.creator);
        textAt(dl, F.mono, 11, hp + ImVec2(px(30), px(4)), col::ink2, b);
        gap(26);
    }
    {
        static std::string before;
        pushFont(F.med, 24);
        if (g.focusName == w.id && w.type != WType::Label && w.type != WType::Button && w.type != WType::Checkbox) {
            ImGui::SetKeyboardFocusHere();
            g.focusName = 0;
        }
        int r = fieldText("##name", &w.name, W);
        if (ImGui::IsItemActivated()) before = w.name;
        if (r == 1) { g.nameEdit = w.id; g.nameBefore = before; g.needsBuild = true; }
        ImGui::PopFont();
        if (r == 2) {
            std::string nn = identOrWarn(w.name);
            nn = nn.empty() ? before : uniqueName(s, nn, w.id);
            w.name = nn;
            g.live = false;
            if (nn != before) commit(before + " → " + nn);
            else g.needsBuild = true;
        }
        tooltip("C 변수 이름이 됩니다");
    }
    gap(14);

    // 01 치수
    snprintf(b, sizeof b, "%s 기준", alignKo(w.align));
    sectionHeader("01", "치수", b);
    {
        const Rect* r = nullptr;
        auto it = lvhost::coords().find(w.id);
        if (it != lvhost::coords().end()) r = &it->second;
        float half = (W - px(16)) * 0.5f;
        int res;
        res = scrubInt("##x", "X", &w.x, -4096, 4096, half);
        snprintf(b, sizeof b, "%s X %d", w.name.c_str(), w.x);
        handleField(res, b);
        ImGui::SameLine(0, px(16));
        res = scrubInt("##y", "Y", &w.y, -4096, 4096, half);
        snprintf(b, sizeof b, "%s Y %d", w.name.c_str(), w.y);
        handleField(res, b);
        int ww = (w.autoSize && r) ? rw(*r) : w.w, hh = (w.autoSize && r) ? rh(*r) : w.h;
        res = scrubInt("##w", "W", &ww, 1, 4096, half, w.autoSize);
        if (res) { w.h = hh; w.w = ww; w.autoSize = false; }
        snprintf(b, sizeof b, "%s 폭 %d", w.name.c_str(), ww);
        handleField(res, b);
        ImGui::SameLine(0, px(16));
        res = scrubInt("##h", "H", &hh, 1, 4096, half, w.autoSize);
        if (res) { w.w = ww; w.h = hh; w.autoSize = false; }
        snprintf(b, sizeof b, "%s 높이 %d", w.name.c_str(), hh);
        handleField(res, b);
        gap(8);
        int a = w.align;
        if (alignPicker("##align", &a) && a != w.align) {
            Rect c = lvhost::contentRect(w.parent);
            int aw = r ? rw(*r) : w.w, ah = r ? rh(*r) : w.h;
            w.x += alignAnchor(w.align, false, rw(c), aw) - alignAnchor(a, false, rw(c), aw);
            w.y += alignAnchor(w.align, true, rh(c), ah) - alignAnchor(a, true, rh(c), ah);
            w.align = a;
            commit(w.name + " 기준 " + alignKo(a));
        }
        ImGui::SameLine(0, px(14));
        ImGui::BeginGroup();
        ImVec2 gp = ImGui::GetCursorScreenPos();
        textAt(dl, F.ui, 14, gp, col::ink3, "기준점");
        textAt(dl, F.med, 15, gp + ImVec2(0, px(16)), col::ink, alignKo(w.align));
        gap(36);
        bool autoOk = w.type == WType::Label || w.type == WType::Checkbox || w.type == WType::Button || w.type == WType::Container;
        if (autoOk) {
            bool v = w.autoSize;
            if (toggleField("##auto", &v)) {
                if (!v && r) { w.w = rw(*r); w.h = rh(*r); }
                w.autoSize = v;
                commit(w.name + (v ? " 내용 크기" : " 고정 크기"));
            }
            ImGui::SameLine(0, px(6));
            ImVec2 tp = ImGui::GetCursorScreenPos();
            textAt(dl, F.ui, 14, tp + ImVec2(0, px(4)), col::ink2, "내용에 맞춤");
            ImGui::Dummy(ImVec2(px(80), px(26)));
        }
        ImGui::EndGroup();
    }
    gap(14);

    // 02 사양
    sectionHeader("02", "사양", ti.key);
    {
        auto textRow = [&](const char* label, std::string* v, const char* note, bool focus, bool sym = false) {
            fieldLabel(label);
            if (focus && g.focusName == w.id) { ImGui::SetKeyboardFocusHere(); g.focusName = 0; }
            int r = fieldText((std::string("##") + label).c_str(), v, ImGui::GetContentRegionAvail().x - (sym ? textW(F.med, 14, "기호") + px(14) : 0.f));
            handleField(r, w.name + " " + note);
            if (sym) {
                ImGui::SameLine(0, px(14));
                if (symbolPick("##pick", v)) commit(w.name + " 기호");
            }
        };
        auto numRow = [&](const char* label, int* v, int mn, int mx) {
            fieldLabel(label);
            int r = scrubInt((std::string("##") + label).c_str(), "", v, mn, mx, ImGui::GetContentRegionAvail().x);
            char nb[96];
            snprintf(nb, sizeof nb, "%s %s %d", w.name.c_str(), label, *v);
            handleField(r, nb);
        };
        auto boolRow = [&](const char* label, bool* v) {
            fieldLabel(label);
            if (toggleField((std::string("##") + label).c_str(), v)) commit(w.name + " " + label + (*v ? " 켬" : " 끔"));
        };
        switch (w.type) {
        case WType::Label: {
            fieldLabel("글자");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textW(F.med, 14, "기호"));
            if (symbolPick("##pick", &w.text)) commit(w.name + " 기호");
            if (g.focusName == w.id) { ImGui::SetKeyboardFocusHere(); g.focusName = 0; }
            handleField(fieldMultiline("##text", &w.text, W, px(54)), w.name + " 글자");
            drawTextFx(w, W);
            break;
        }
        case WType::Button: textRow("글자", &w.text, "글자", true, true); break;
        case WType::Checkbox: textRow("글자", &w.text, "글자", true); boolRow("체크", &w.checked); break;
        case WType::Switch: boolRow("켜짐", &w.checked); break;
        case WType::Slider: case WType::Bar: case WType::Arc:
            numRow("값", &w.value, w.minv, w.maxv);
            numRow("최소", &w.minv, -100000, w.maxv - 1);
            numRow("최대", &w.maxv, w.minv + 1, 100000);
            if (w.type == WType::Arc) boolRow("게이지", &w.checked);
            break;
        case WType::Dropdown: case WType::Roller: {
            fieldLabel("선택지");
            textAt(dl, F.ui, 14, ImGui::GetCursorScreenPos() + ImVec2(0, px(5)), col::ink3, "한 줄에 하나씩");
            ImGui::NewLine();
            handleField(fieldMultiline("##opts", &w.text2, W, px(78)), w.name + " 선택지");
            if (w.type == WType::Roller) numRow("선택", &w.value, 0, 99);
            break;
        }
        case WType::Textarea:
            textRow("자리표시", &w.text2, "자리표시", false);
            textRow("글자", &w.text, "글자", true);
            boolRow("한 줄", &w.checked);
            break;
        case WType::Led: {
            fieldLabel("색");
            int cr = colorField("##led", &w.color, true, w.color, ImGui::GetContentRegionAvail().x);
            if (cr == 3) w.color = 0x2196F3;
            handleField(cr == 3 ? 2 : cr, w.name + " 색");
            boolRow("켜짐", &w.checked);
            break;
        }
        case WType::Image:
            drawImageProps(w, W);
            break;
        default: noteText("이 형식은 고유 속성이 없습니다."); break;
        }
        boolRow("숨김", &w.hidden);
        // 한글이 있으면 글꼴에 담긴다는 것을 알려 준다
        int extra = 0;
        for (const std::string* t : renderedTexts(w)) extra += fontgen::countExtra(*t);
        if (extra) {
            gap(2);
            snprintf(b, sizeof b, "기본 글꼴에 없는 %d자는 내보낼 때 글꼴에 담깁니다.", extra);
            noteText(b);
        }
    }
    gap(14);

    // 03 스타일
    snprintf(b, sizeof b, "%s 상태", stateKo(g.editState));
    sectionHeader("03", "스타일", b);
    {
        const char* states[ST_COUNT];
        bool marks[ST_COUNT];
        for (int i = 0; i < ST_COUNT; i++) {
            states[i] = stateKo(i);
            marks[i] = false;
            for (int p = 0; p < SP_COUNT; p++) if (w.style[i][p].set) marks[i] = true;
        }
        if (segTabs("##states", states, ST_COUNT, &g.editState, marks)) g.needsBuild = true;
        gap(4);
        int st = g.editState;
        struct Group { const char* title; std::vector<int> props; };
        static const Group text = {"글자", {SP_FACE, SP_TEXT_FONT, SP_TEXT_COLOR, SP_TEXT_OPA, SP_TALIGN, SP_LETTER, SP_LINE, SP_DECOR}};
        static const Group back = {"바탕", {SP_BG_COLOR, SP_BG_OPA, SP_RADIUS, SP_PAD, SP_SHADOW}};
        static const Group line = {"테두리", {SP_BORDER_COLOR, SP_BORDER_WIDTH}};
        static const Group pic = {"그림", {SP_IMG_OPA, SP_IMG_TINT, SP_IMG_TINT_OPA}};
        static const Group arc = {"호", {SP_ARC_COLOR, SP_ARC_WIDTH}};
        static const Group all = {"전체", {SP_OPA}};
        const bool textFirst = w.type == WType::Label || w.type == WType::Button || w.type == WType::Checkbox;
        std::vector<const Group*> order;
        if (textFirst) order = {&text, &back, &line, &all};
        else order = {&pic, &arc, &back, &line, &text, &all};
        for (const Group* gr : order) {
            bool any = false;
            for (int p : gr->props) any = any || styleShown(p, w.type) || w.style[st][p].set;
            if (!any) continue;
            const ImVec2 gp = ImGui::GetCursorScreenPos();
            textAt(dl, F.med, 14, gp + ImVec2(0, px(4)), col::ink3, gr->title, 1.f);
            hline(dl, gp.x + textW(F.med, 14, gr->title, 1.f) + px(10), gp.x + W, gp.y + px(14), col::hair);
            ImGui::Dummy(ImVec2(W, px(26)));
            for (int p : gr->props) {
                if (!styleShown(p, w.type) && !w.style[st][p].set) continue;
                styleRow(w, st, p);
            }
            gap(6);
        }
        gap(2);
        noteText(st == ST_DEFAULT ? "빨간 표시는 테마 값을 덮어쓴 속성입니다."
                                  : "이 상태로 미리 보는 중입니다. 기본 탭으로 돌아가면 평소 모습이 보입니다.");
    }
    gap(14);

    // 04 주기 (이벤트)
    snprintf(b, sizeof b, "%d건", (int)w.events.size());
    sectionHeader("04", "주기", b);
    {
        int remove = -1;
        for (int k = 0; k < (int)w.events.size(); k++) {
            EventBind& e = w.events[k];
            ImGui::PushID(k);
            ImVec2 rp = ImGui::GetCursorScreenPos();
            char nb[16];
            snprintf(nb, sizeof nb, "%d.", k + 1);
            textAt(dl, F.mono, 13, rp + ImVec2(0, px(5)), col::red, nb);
            ImGui::SetCursorScreenPos(rp + ImVec2(px(24), 0));
            pushFont(F.ui, 15);
            if (comboField("##code", eventKo(e.code), W - px(24) - px(26))) {
                for (int c = 0; c < EV_COUNT; c++) {
                    char it[64];
                    snprintf(it, sizeof it, "%s  %s", eventKo(c), eventLv(c));
                    if (ImGui::Selectable(it, c == e.code)) { e.code = c; commit(w.name + " 주기 " + eventKo(c)); }
                }
                endComboField();
            }
            ImGui::PopFont();
            ImGui::SameLine(0, px(4));
            ImVec2 xp = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##del", ImVec2(px(22), px(26)))) remove = k;
            bool dh = ImGui::IsItemHovered();
            ImVec2 c = xp + ImVec2(px(11), px(13));
            float kk = px(3.5f);
            dl->AddLine(c - ImVec2(kk, kk), c + ImVec2(kk, kk), dh ? col::red : col::ink3, 1.2f);
            dl->AddLine(c + ImVec2(-kk, kk), c + ImVec2(kk, -kk), dh ? col::red : col::ink3, 1.2f);
            if (dh) tooltip("주기 삭제");
            ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(px(24), 0));
            ImVec2 ap = ImGui::GetCursorScreenPos();
            textAt(dl, F.mono, 13, ap + ImVec2(0, px(5)), col::ink3, "→");
            ImGui::SetCursorScreenPos(ap + ImVec2(px(18), 0));
            pushFont(F.mono, 13);
            handleField(fieldText("##cb", &e.cb, W - px(24) - px(18), "on_clicked"), w.name + " 콜백 " + e.cb);
            ImGui::PopFont();
            if (g.doc.screens.size() > 1 || !e.go.empty()) {   // 다른 시트로 옮겨 가기
                ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(px(24), 0));
                ImVec2 gp = ImGui::GetCursorScreenPos();
                const char* gl = "이동";
                textAt(dl, F.ui, 14, gp + ImVec2(0, px(4)), col::ink3, gl);
                ImGui::SetCursorScreenPos(gp + ImVec2(px(38), 0));
                pushFont(F.ui, 15);
                if (comboField("##go", e.go.empty() ? "없음" : e.go.c_str(), W - px(24) - px(38))) {
                    if (ImGui::Selectable("없음", e.go.empty())) { e.go.clear(); commit(w.name + " 이동 없음"); }
                    for (int si = 0; si < (int)g.doc.screens.size(); si++) {
                        if (si == g.screen) continue;
                        char it[96];
                        snprintf(it, sizeof it, "%02d  %s", si + 1, g.doc.screens[si].name.c_str());
                        if (ImGui::Selectable(it, g.doc.screens[si].name == e.go)) { e.go = g.doc.screens[si].name; commit(w.name + " 이동 → " + e.go); }
                    }
                    endComboField();
                }
                ImGui::PopFont();
            }
            gap(6);
            ImGui::PopID();
        }
        if (remove >= 0) { w.events.erase(w.events.begin() + remove); commit(w.name + " 주기 삭제"); }
        if (textLink("+ 주기 추가")) {
            int code = (w.type == WType::Button) ? EV_CLICKED : EV_VALUE_CHANGED;
            w.events.push_back({code, "on_" + w.name + (code == EV_CLICKED ? "_clicked" : "_changed")});
            commit(w.name + " 주기 추가");
        }
    }
    gap(14);

    // 05 움직임
    if (w.motions.empty()) snprintf(b, sizeof b, "없음");
    else snprintf(b, sizeof b, "%d개 · %.1f초", (int)w.motions.size(), [&] { int e = 0; for (auto& m : w.motions) e = std::max(e, motionEnd(m)); return e / 1000.0; }());
    sectionHeader("05", "움직임", b);
    drawMotions(w, W);
    gap(24);
}

static void drawSheetProps() {
    Screen& s = curScreen();
    Document& d = g.doc;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x;
    char b[160];
    {
        ImVec2 hp = ImGui::GetCursorScreenPos();
        snprintf(b, sizeof b, "SHT %02d", g.screen + 1);
        textAt(dl, F.mono, 12, hp + ImVec2(0, px(4)), col::red, b);
        textAt(dl, F.mono, 11, hp + ImVec2(textW(F.mono, 12, b) + px(10), px(5)), col::ink2, "시트 · lv_obj_create(NULL)");
        gap(26);
        static std::string before;
        pushFont(F.med, 24);
        int r = fieldText("##sname", &s.name, W);
        if (ImGui::IsItemActivated()) before = s.name;
        if (r == 1) { g.sheetEdit = g.screen; g.nameBefore = before; }
        ImGui::PopFont();
        if (r == 2) {
            std::string nn = identOrWarn(s.name);
            s.name = nn.empty() ? before : nn;
            if (s.name != before) { renameSheetRefs(g.doc, before, s.name); commit("시트 이름 " + s.name); }
        }
    }
    gap(10);
    // 빈 선택 안내
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = px(64);
        if (g.now - g.flashSheetT < 0.6) dl->AddRect(p - ImVec2(px(4), px(4)), p + ImVec2(W + px(4), h + px(4)), alpha(col::red, 1.f - (float)((g.now - g.flashSheetT) / 0.6)), 0, 1.4f);
        dashRect(dl, p, p + ImVec2(W, h), col::hair2, px(4), px(3));
        textAt(dl, F.med, 15, p + ImVec2(px(12), px(10)), col::ink, "선택된 부품 없음");
        textAt(dl, F.ui, 14, p + ImVec2(px(12), px(32)), col::ink2, "시트나 부품표에서 부품을 누르세요.");
        ImGui::Dummy(ImVec2(W, h));
    }
    gap(16);

    sectionHeader("01", "시트");
    fieldLabel("배경색", s.bgSet);
    {
        int v = s.bgSet ? s.bg : lvhost::styleValue(0, SP_BG_COLOR);
        int r = colorField("##sbg", &v, s.bgSet, lvhost::styleValue(0, SP_BG_COLOR), ImGui::GetContentRegionAvail().x);
        if (r == 1 || r == 2) { s.bgSet = true; s.bg = v; }
        if (r == 3) s.bgSet = false;
        handleField(r == 3 ? 2 : r, "시트 배경색");
    }
    if (d.screens.size() > 1 || !s.next.empty()) {   // 시간이 되면 다음 시트로(소개 화면 등)
        gap(6);
        fieldLabel("저절로 넘어가기", !s.next.empty());
        pushFont(F.ui, 15);
        if (comboField("##snext", s.next.empty() ? "없음" : s.next.c_str(), W)) {
            if (ImGui::Selectable("없음", s.next.empty())) { s.next.clear(); commit("저절로 넘어가기 없음"); }
            for (int si = 0; si < (int)d.screens.size(); si++) {
                if (si == g.screen) continue;
                snprintf(b, sizeof b, "%02d  %s", si + 1, d.screens[si].name.c_str());
                if (ImGui::Selectable(b, d.screens[si].name == s.next)) {
                    if (s.nextMs <= 0) s.nextMs = std::max(3000, (sheetMotionEnd(s) + 999) / 1000 * 1000 + 500);
                    s.next = d.screens[si].name;
                    commit("저절로 넘어가기 → " + s.next);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        if (!s.next.empty()) {
            if (screenByName(d, s.next) < 0) noteText("이 이름의 시트가 없어 넘어가지 않습니다.");
            handleField(scrubInt("##snms", "뜬 뒤 ms", &s.nextMs, 100, 600000, W), "저절로 넘어가기 시간");
        }
    }
    {
        bool any = !s.next.empty();
        for (auto& w : s.widgets) any = any || !w.motions.empty();
        if (any) {
            gap(6);
            fieldLabel("움직임 표");
            gap(2);
            drawMotionChart(s, W);
        }
    }
    gap(14);

    sectionHeader("02", "도면");
    fieldLabel("이름");
    handleField(fieldText("##proj", &d.project, ImGui::GetContentRegionAvail().x), "도면 이름 " + d.project);
    fieldLabel("해상도");
    {
        float half = (ImGui::GetContentRegionAvail().x - px(12)) * 0.5f;
        int r = scrubInt("##rw", "W", &d.resW, 16, 2048, half);
        handleField(r, "해상도 폭 " + std::to_string(d.resW));
        ImGui::SameLine(0, px(12));
        r = scrubInt("##rh", "H", &d.resH, 16, 2048, half);
        handleField(r, "해상도 높이 " + std::to_string(d.resH));
    }
    {
        static const int presets[][2] = {{240, 240}, {320, 240}, {480, 272}, {480, 320}, {800, 480}, {1024, 600}};
        ImVec2 gp = ImGui::GetCursorScreenPos() + ImVec2(px(96), px(6));
        float avail = W - px(96), bw = floorf((avail - px(6)) * 0.5f), bh = px(24);
        for (int i = 0; i < 6; i++) {
            snprintf(b, sizeof b, "%d × %d", presets[i][0], presets[i][1]);
            ImVec2 a = ImFloor(gp + ImVec2((i % 2) * (bw + px(6)), (i / 2) * (bh + px(5))));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            bool on = d.resW == presets[i][0] && d.resH == presets[i][1];
            if (ImGui::InvisibleButton("##pre", ImVec2(bw, bh)) && !on) {
                d.resW = presets[i][0]; d.resH = presets[i][1];
                g.needFit = true;
                commit(std::string("해상도 ") + b);
            }
            bool hv = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hv) { dl->AddRectFilled(a, a + ImVec2(bw, bh), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
            dl->AddRect(a, a + ImVec2(bw, bh), on ? col::red : col::hair2);
            float tw = textW(F.mono, 12, b);
            textAt(dl, F.mono, 12, a + ImVec2((bw - tw) * 0.5f, px(5)), on ? col::red : col::ink2, b);
        }
        ImGui::SetCursorScreenPos(gp - ImVec2(px(96), px(6)));
        ImGui::Dummy(ImVec2(W, px(6) + 3 * (bh + px(5)) + px(4)));
    }
    if (d.resW == d.resH) {
        fieldLabel("원형 화면");
        if (toggleField("##round", &d.round)) commit(std::string("원형 화면 ") + (d.round ? "켬" : "끔"));
    }
    fieldLabel("색 깊이");
    {
        static const char* items[] = {"RGB565", "XRGB8888"};
        int v = d.depth == 16 ? 0 : 1;
        if (segTabs("##depth", items, 2, &v)) { d.depth = v ? 32 : 16; commit(std::string("색 깊이 ") + items[v]); }
    }
    fieldLabel("테마");
    {
        static const char* items[] = {"밝음", "어둠"};
        int v = d.dark ? 1 : 0;
        if (segTabs("##theme", items, 2, &v)) { d.dark = v == 1; commit(std::string("테마 ") + items[v]); }
    }
    fieldLabel("주 색");
    {
        int r = colorField("##primary", &d.primary, true, d.primary, ImGui::GetContentRegionAvail().x);
        if (r == 3) d.primary = 0x2196F3;
        handleField(r == 3 ? 2 : r, "테마 주 색");
    }
    gap(14);

    // 03 글꼴: 내보낼 때 함께 담기는 글자
    {
        const fontgen::Plan pl = fontgen::plan(d);
        int total = 0;
        for (auto& [size, cps] : pl.sizes) { total += pl.bytes(size); (void)cps; }
        for (auto& [k, cps] : pl.faces) total += fontgen::faceBytes(d, k.first, k.second, cps);
        if (total) snprintf(b, sizeof b, "%.1f KB", total / 1024.0);
        sectionHeader("03", "글꼴", total ? b : nullptr);
        for (auto& [k, cps] : pl.faces) {
            const auto* f = fontface::find(d, k.first);
            if (!f) continue;
            ImVec2 p = ImGui::GetCursorScreenPos();
            snprintf(b, sizeof b, "%d", k.second);
            textAt(dl, F.mono, 13, p + ImVec2(0, px(6)), col::ink, b);
            snprintf(b, sizeof b, "%d자", (int)cps.size());
            const float cw = textW(F.ui, 14, b);
            textAt(dl, F.ui, 14, p + ImVec2(W - cw, px(4)), col::ink2, b);
            textAt(dl, F.ui, 15, p + ImVec2(px(36), px(3)), col::ink, ellipsize(F.ui, 15, (*f)->name, W - px(36) - cw - px(14)).c_str());
            hline(dl, p.x, p.x + W, p.y + px(25), col::hair);
            ImGui::Dummy(ImVec2(W, px(26)));
        }
        if (!pl.any()) noteText("영문과 숫자만 있어 LVGL 기본 글꼴로 충분합니다.");
        for (auto& [size, cps] : pl.sizes) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            snprintf(b, sizeof b, "%d", size);
            textAt(dl, F.mono, 13, p + ImVec2(0, px(6)), col::ink, b);
            snprintf(b, sizeof b, "%d자", (int)cps.size());
            float cw = textW(F.ui, 14, b);
            textAt(dl, F.ui, 14, p + ImVec2(W - cw, px(4)), col::ink2, b);
            std::string sample;   // 활자 견본
            for (uint32_t cp : cps) sample += fontgen::encode(cp);
            textAt(dl, F.ui, 15, p + ImVec2(px(36), px(3)), col::ink2, ellipsize(F.ui, 15, sample, W - px(36) - cw - px(14)).c_str());
            hline(dl, p.x, p.x + W, p.y + px(25), col::hair);
            ImGui::Dummy(ImVec2(W, px(26)));
        }
    }
    gap(14);

    sectionHeader("04", "손에 익히기");
    {
        static const char* keys[][2] = {
            {"Alt", "선택한 부품에서 거리 재기"}, {"Space + 끌기", "시트 옮기기"}, {"휠", "척도 바꾸기"},
            {"Shift + 끌기", "한 축으로만"}, {"Ctrl + 끌기", "스냅 끄기"}, {"G", "격자 스냅"},
            {"N", "부품 번호 모두 보기"}, {"T", "다른 시트 비쳐 보기"}, {"P", "장치 픽셀 그대로 보기"}, {"F5", "시운전"}, {"Ctrl+Shift+N", "표준도에서 시작"}, {"Tab", "다음 부품"}, {"0", "화면에 맞추기"}, {"Ctrl + E", "내보내기"}};
        for (auto& k : keys) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            textAt(dl, F.mono, 12, p + ImVec2(0, px(3)), col::ink, k[0]);
            textAt(dl, F.ui, 14, p + ImVec2(px(104), px(1)), col::ink2, k[1]);
            dl->AddLine(ImVec2(p.x, p.y + px(22)), ImVec2(p.x + W, p.y + px(22)), col::hair);
            ImGui::Dummy(ImVec2(W, px(23)));
        }
    }
    gap(24);
}

void drawRight(ImVec2 p, ImVec2 sz) {
    ImGui::SetCursorScreenPos(p);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(20), px(16)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(2)));
    ImGui::BeginChild("##right", sz, ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    pushFont(F.ui, 15);
    if (Widget* w = selWidget()) {
        ImGui::PushID(w->id);
        drawWidgetProps(*w);
        ImGui::PopID();
    } else {
        drawSheetProps();
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

// ───────────────────────── 상태 막대 ─────────────────────────
bool statusToggle(const char* id, const char* label, bool on, const char* tip, const char* key) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = textW(F.ui, 14, label) + px(22);
    bool r = ImGui::InvisibleButton(id, ImVec2(w, px(22)));
    bool hv = ImGui::IsItemHovered();
    if (hv) { dl->AddRectFilled(p, p + ImVec2(w, px(22)), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
    ImVec2 b = p + ImVec2(px(5), px(7));
    if (on) dl->AddRectFilled(b, b + ImVec2(px(8), px(8)), col::ink);
    else dl->AddRect(b, b + ImVec2(px(8), px(8)), col::ink2);
    textAt(dl, F.ui, 14, p + ImVec2(px(17), px(2)), on || hv ? col::ink : col::ink2, label);
    tooltip(tip, key);
    return r;
}

void drawStatus(ImVec2 p, ImVec2 sz) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + sz, col::paper);
    hline(dl, p.x, p.x + sz.x, p.y, col::ink);
    const float ty = p.y + px(5);
    char b[160];
    ImVec2 md;
    if (canvasMouseDev(md)) snprintf(b, sizeof b, "x %4d   y %4d", (int)floorf(md.x), (int)floorf(md.y));
    else snprintf(b, sizeof b, "x    —   y    —");
    float x = p.x + px(16);
    textAt(dl, F.mono, 12, ImVec2(x, ty + px(1)), col::ink2, b);
    x += px(132);
    dl->AddLine(ImVec2(x, p.y + px(6)), ImVec2(x, p.y + sz.y - px(6)), col::hair2);
    x += px(12);
    if (Widget* w = selWidget()) {
        auto it = lvhost::coords().find(w->id);
        int ww = it != lvhost::coords().end() ? rw(it->second) : w->w, hh = it != lvhost::coords().end() ? rh(it->second) : w->h;
        snprintf(b, sizeof b, "%02d", widgetNumber(w->id));
        textAt(dl, F.mono, 12, ImVec2(x, ty + px(1)), col::red, b);
        x += textW(F.mono, 12, b) + px(8);
        snprintf(b, sizeof b, "%s  ·  %d × %d", w->name.c_str(), ww, hh);
        textAt(dl, F.ui, 14, ImVec2(x, ty), col::ink, b);
    } else {
        snprintf(b, sizeof b, "SHT %02d %s  ·  부품 %d개", g.screen + 1, curScreen().name.c_str(), (int)curScreen().widgets.size());
        textAt(dl, F.ui, 14, ImVec2(x, ty), col::ink2, b);
    }

    // 오른쪽: 격자 · 번호 · 검도
    int errors = 0, advice = 0;
    for (auto& is : g.issues) (is.error ? errors : advice)++;
    float rx = p.x + sz.x - px(12);
    {
        if (errors) snprintf(b, sizeof b, "검도 오류 %d", errors);
        else if (advice) snprintf(b, sizeof b, "검도 통과 · 권고 %d", advice);
        else snprintf(b, sizeof b, "검도 통과");
        float w = textW(F.med, 14, b) + px(16);
        ImVec2 a(rx - w, p.y + px(3));
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##lintbtn", ImVec2(w, sz.y - px(6)))) { g.openLint = true; g.lintAnchor = ImVec2(a.x + w, p.y - px(4)); }
        bool hv = ImGui::IsItemHovered();
        if (hv) { dl->AddRectFilled(a, a + ImVec2(w, sz.y - px(6)), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        if (errors) dl->AddRect(a, a + ImVec2(w, sz.y - px(6)), col::red);
        textAt(dl, F.med, 14, a + ImVec2(px(8), px(2)), errors ? col::red : col::ink, b);
        rx = a.x - px(10);
    }
    {
        snprintf(b, sizeof b, "격자 %d", g.grid);
        float w = textW(F.ui, 14, "번호") + px(22);
        ImGui::SetCursorScreenPos(ImVec2(rx - w, p.y + px(3)));
        if (statusToggle("##bal", "번호", g.balloons, "부품 번호를 모두 보기", "N")) g.balloons = !g.balloons;
        rx -= w + px(4);
        w = textW(F.ui, 14, "비침") + px(22);
        ImGui::SetCursorScreenPos(ImVec2(rx - w, p.y + px(3)));
        if (statusToggle("##trace", "비침", g.trace, "다른 시트를 밑그림으로 비쳐 보기", "T")) toggleTrace();
        rx -= w + px(4);
        w = textW(F.ui, 14, "픽셀") + px(22);
        ImGui::SetCursorScreenPos(ImVec2(rx - w, p.y + px(3)));
        if (statusToggle("##pixel", "픽셀", g.pixelView, "장치에 찍힐 픽셀을 그대로 키워 보기", "P")) g.pixelView = !g.pixelView;
        rx -= w + px(4);
        w = textW(F.ui, 14, b) + px(22);
        ImGui::SetCursorScreenPos(ImVec2(rx - w, p.y + px(3)));
        if (statusToggle("##grid", b, g.snapGrid, "격자에 맞춰 붙이기", "G")) g.snapGrid = !g.snapGrid;
        rx -= w + px(14);
    }
    // 가운데: 지금 할 수 있는 일
    const char* hint;
    if (g.drag == Drag::Move) hint = "Shift 한 축으로 · Ctrl 스냅 끄기";
    else if (g.drag == Drag::Resize) hint = "Shift 비율 유지 · Ctrl 스냅 끄기";
    else if (g.drag == Drag::Create || g.armed >= 0) hint = "끌어서 크기 정하기 · 누르면 기본 크기 · Shift 도구 유지 · Esc 취소";
    else if (g.drag == Drag::Template) hint = "시트에 놓으면 추가 · 컨테이너 위에 놓으면 안으로";
    else if (g.sel) hint = "Alt 거리 재기 · 방향키 1px · Shift+방향키 10px · Del 삭제";
    else hint = "부품을 누르거나 템플릿을 끌어 놓으세요 · Space+끌기 이동 · 휠 척도";
    float hw = textW(F.ui, 14, hint);
    if (x + textW(F.ui, 14, "                                        ") < rx - hw)
        textAt(dl, F.ui, 14, ImVec2(rx - hw, ty), col::ink3, hint);
}

// ───────────────────────── 창: 검도 · 개정 · 내보내기 · 확인 ─────────────────────────
static void drawCodeView(const std::string& text, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.985f, 0.98f, 0.965f, 1.f));
    ImGui::BeginChild("##code", size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::vector<std::pair<size_t, size_t>> lines;
    size_t st = 0;
    for (size_t i = 0; i <= text.size(); i++)
        if (i == text.size() || text[i] == '\n') { lines.push_back({st, i}); st = i + 1; }
    // 여러 줄 주석 상태를 미리 계산
    std::vector<char> inC(lines.size(), 0);
    bool c = false;
    for (size_t li = 0; li < lines.size(); li++) {
        inC[li] = c;
        for (size_t i = lines[li].first; i + 1 < lines[li].second; i++) {
            if (!c && text[i] == '/' && text[i + 1] == '*') c = true;
            else if (c && text[i] == '*' && text[i + 1] == '/') c = false;
        }
    }
    const float lh = px(21);
    ImGuiListClipper clip;
    clip.Begin((int)lines.size(), lh);
    while (clip.Step()) {
        for (int li = clip.DisplayStart; li < clip.DisplayEnd; li++) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            char nb[8];
            snprintf(nb, sizeof nb, "%3d", li + 1);
            textAt(dl, F.mono, 12, p + ImVec2(0, px(2)), col::ink3, nb);
            float x = p.x + px(40);
            bool comment = inC[li], str = false;
            size_t i = lines[li].first, e = lines[li].second, seg = i;
            auto flush = [&](size_t to, ImU32 cc) {
                if (to <= seg) return;
                std::string s(text.begin() + seg, text.begin() + to);
                textAt(dl, F.mono, 13, ImVec2(x, p.y + px(1)), cc, s.c_str());
                x += textW(F.mono, 13, s.c_str());
                seg = to;
            };
            for (; i < e; i++) {
                if (!comment && !str && i + 1 < e && text[i] == '/' && text[i + 1] == '*') { flush(i, col::ink); comment = true; }
                else if (comment && i + 1 < e && text[i] == '*' && text[i + 1] == '/') { flush(i + 2, col::ink2); comment = false; i++; }
                else if (!comment && text[i] == '"') {
                    if (!str) { flush(i, col::ink); str = true; }
                    else { flush(i + 1, col::blueInk); str = false; }
                }
            }
            flush(e, comment ? col::ink2 : str ? col::blueInk : col::ink);
            ImGui::Dummy(ImVec2(x - p.x + px(20), lh));
        }
    }
    ImGui::EndChild();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(a, b, col::hair2);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

static void regenerateKeepTab() {
    const std::string name = g.genTab < (int)g.gen.size() ? g.gen[g.genTab].name : "";
    regenerate();
    for (int i = 0; i < (int)g.gen.size(); i++) if (g.gen[i].name == name) g.genTab = i;
}

// 아두이노 보드 설정. 바뀌면 true
static bool drawBoard() {
    Board& bd = g.doc.board;
    bool changed = false;
    char b[160];
    const float W = ImGui::GetContentRegionAvail().x - px(8);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(2)));
    auto done = [&](const char* note) { commit(std::string("보드 ") + note); changed = true; };
    auto combo = [&](const char* label, int* v, int n, const char* (*name)(int)) {
        fieldLabel(label);
        if (comboField((std::string("##") + label).c_str(), name(*v), W - px(96))) {
            for (int i = 0; i < n; i++) if (ImGui::Selectable(name(i), i == *v) && i != *v) { *v = i; done(label); }
            endComboField();
        }
    };
    auto num = [&](const char* label, int* v, int mn, int mx) {
        fieldLabel(label);
        if (scrubInt((std::string("##n") + label).c_str(), "", v, mn, mx, W - px(96)) == 2) done(label);
    };
    // 핀은 한 줄에 둘씩
    auto pins = [&](const char* l1, int* v1, const char* l2, int* v2) {
        const float w = (W - px(16)) * 0.5f;
        ImGui::PushID((const void*)v1);
        if (scrubInt("##a", l1, v1, -1, 99, w) == 2) done(l1);
        if (v2) {
            ImGui::SameLine(0, px(16));
            if (scrubInt("##b", l2, v2, -1, 99, w) == 2) done(l2);
        }
        ImGui::PopID();
    };
    auto flag = [&](const char* label, int* v) {
        fieldLabel(label);
        bool on = *v != 0;
        if (toggleField((std::string("##f") + label).c_str(), &on)) { *v = on ? 1 : 0; done(label); }
    };

    // 알려진 보드는 한 번에 채운다
    fieldLabel("보드");
    if (comboField("##preset", "알려진 보드로 채우기…", W - px(96))) {
        if (ImGui::Selectable("CYD  ·  ESP32-2432S028 (2.8인치 320×240)")) {
            const int keepBuf = bd.buf, keepDbl = bd.dbl, keepPer = bd.period;
            bd = Board();
            bd.driver = 1; bd.touch = 1;
            bd.buf = keepBuf; bd.dbl = keepDbl; bd.period = keepPer;
            done("CYD");
        }
        if (ImGui::Selectable("JC1060P470  ·  ESP32-P4 (7인치 1024×600)")) {
            boardPresetJC1060(bd);
            done("JC1060P470");
        }
        endComboField();
    }
    gap(10);
    sectionHeader("01", "화면", "핀 -1은 안 씀");
    combo("드라이버", &bd.driver, 5, boardDriverKo);
    combo("방향", &bd.rotation, 4, [](int i) { static const char* k[] = {"0  세로", "1  가로", "2  세로 뒤집힘", "3  가로 뒤집힘"}; return k[i & 3]; });
    auto warn = [&](const char* t) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::red));
        pushFont(F.ui, 14);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(t);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::PopStyleColor();
    };
    if (bd.driver == 1 && !(g.doc.resW * g.doc.resH == 320 * 240 && std::max(g.doc.resW, g.doc.resH) == 320)) {
        snprintf(b, sizeof b, "ILI9341은 320 × 240입니다. 지금 도면은 %d × %d라 맞지 않습니다.", g.doc.resW, g.doc.resH);
        warn(b);
    }
    if (bd.driver == 2 && (std::max(g.doc.resW, g.doc.resH) > 320 || std::min(g.doc.resW, g.doc.resH) > 240)) {
        snprintf(b, sizeof b, "ST7789는 240 × 320까지입니다. 지금 도면은 %d × %d입니다.", g.doc.resW, g.doc.resH);
        warn(b);
    }
    if (bd.driver == 4) {
        const bool land = (bd.rotation & 1) != 0;
        const int ew = land ? 1024 : 600, eh = land ? 600 : 1024;
        if (g.doc.resW != ew || g.doc.resH != eh) {
            snprintf(b, sizeof b, "이 화면은 %d × %d입니다(방향 %d). 지금 도면은 %d × %d입니다.", ew, eh, bd.rotation, g.doc.resW, g.doc.resH);
            warn(b);
            snprintf(b, sizeof b, "도면을 %d × %d로 바꾸기", ew, eh);
            if (textLink(b, col::red)) {
                g.doc.resW = ew;
                g.doc.resH = eh;
                g.needFit = true;
                done(b);
            }
        }
    } else if (g.doc.resW != g.doc.resH && (g.doc.resW > g.doc.resH) != ((bd.rotation & 1) != 0))
        warn(g.doc.resW > g.doc.resH ? "가로 도면입니다. 방향 1이나 3을 고르세요." : "세로 도면입니다. 방향 0이나 2를 고르세요.");
    if (bd.driver == 0) noteText("칩과 핀은 TFT_eSPI의 User_Setup.h를 따릅니다.");
    if (bd.driver == 1 || bd.driver == 2) {
        pins("SCK", &bd.sck, "MOSI", &bd.mosi);
        pins("MISO", &bd.miso, "CS", &bd.cs);
        pins("DC", &bd.dc, "RST", &bd.rst);
        pins("백라이트", &bd.bl, "MHz", &bd.mhz);
    }
    if (bd.driver == 4) {
        pins("RST", &bd.rst, "백라이트", &bd.bl);
        noteText("7인치 1024 × 600, MIPI-DSI 2레인. LVGL은 내부 RAM에 그리고 DMA2D가 PSRAM 화면 버퍼로 옮깁니다. 방향 1이 기본 가로입니다.");
    }
    if (bd.driver < 3) flag("색 반전", &bd.invert);
    gap(12);
    sectionHeader("02", "터치");
    combo("방식", &bd.touch, 4, boardTouchKo);
    if (bd.touch == 3) {
        pins("SDA", &bd.tsda, "SCL", &bd.tscl);
        pins("RST", &bd.trst, "INT", &bd.tint);
        if (bd.driver != 4) flag("X·Y 바꿈", &bd.tswap);
        noteText("다섯 손가락까지 읽습니다. 부품은 첫 손가락으로 움직이고, 나머지는 higenis_touch_point()로 읽습니다.");
    }
    if (bd.touch == 1) {
        pins("CS", &bd.tcs, "IRQ", &bd.tirq);
        pins("SCK", &bd.tsck, "MOSI", &bd.tmosi);
        pins("MISO", &bd.tmiso, nullptr, nullptr);
        const float w = (W - px(16)) * 0.5f;
        if (scrubInt("##tx0", "X 처음", &bd.tx0, 0, 4095, w) == 2) done("보정");
        ImGui::SameLine(0, px(16));
        if (scrubInt("##tx1", "X 끝", &bd.tx1, 0, 4095, w) == 2) done("보정");
        if (scrubInt("##ty0", "Y 처음", &bd.ty0, 0, 4095, w) == 2) done("보정");
        ImGui::SameLine(0, px(16));
        if (scrubInt("##ty1", "Y 끝", &bd.ty1, 0, 4095, w) == 2) done("보정");
        flag("X·Y 바꿈", &bd.tswap);
    }
    if (bd.touch == 2 && bd.driver != 0) noteText("TFT_eSPI 드라이버를 쓸 때만 됩니다.");
    gap(12);
    const int lines = boardBufLines(g.doc);
    const double kb = g.doc.resW * 2.0 * lines * (bd.dbl ? 2 : 1) / 1024.0;
    snprintf(b, sizeof b, bd.driver == 4 ? "RAM %.1f KB + PSRAM 1.2 MB" : "RAM %.1f KB", kb);
    sectionHeader("03", "프레임", b);
    combo("그리기 버퍼", &bd.buf, 4, boardBufKo);
    flag("두 장 쓰기", &bd.dbl);
    flag("버퍼를 PSRAM에", &bd.psram);
    fieldLabel("화면 주기");
    snprintf(b, sizeof b, "%d ms  ·  초당 %d장", bd.period, 1000 / std::max(1, bd.period));
    if (comboField("##period", b, W - px(96))) {
        for (int p : {16, 20, 33, 50, 100}) {
            snprintf(b, sizeof b, "%d ms  ·  초당 %d장", p, 1000 / p);
            if (ImGui::Selectable(b, p == bd.period) && p != bd.period) { bd.period = p; done("화면 주기"); }
        }
        endComboField();
    }
    if (!bd.psram && kb > (bd.driver == 4 ? 480 : 160)) noteText("내부 RAM에는 클 수 있습니다. 버퍼를 줄이거나 PSRAM을 쓰세요.");
    gap(12);
    const bool need = usesSd(g.doc);
    sectionHeader("04", "SD 카드", need ? "그림이 씀" : bd.sd ? "씀" : "안 씀");
    if (!need) flag("카드 쓰기", &bd.sd);
    if (need || bd.sd) {
        combo("연결", &bd.sdmmc, 2, [](int i) { return i ? "SDMMC 4비트 (ESP32-P4·S3)" : "SPI"; });
        if (bd.sdmmc) {
            pins("CLK", &bd.sdclk, "CMD", &bd.sdcmd);
            pins("D0", &bd.sdd0, "D1", &bd.sdd1);
            pins("D2", &bd.sdd2, "D3", &bd.sdd3);
        } else {
            pins("CS", &bd.sdcs, "MHz", &bd.sdmhz);
            pins("SCK", &bd.sdsck, "MOSI", &bd.sdmosi);
            pins("MISO", &bd.sdmiso, nullptr, nullptr);
        }
        noteText("내보낸 sd 폴더 안의 것을 카드 맨 위에 복사하세요. LVGL에서는 S: 드라이브입니다.");
    }
    ImGui::PopStyleVar();
    return changed;
}

static void exportToFolder() {
    std::wstring dir;
    const bool ard = g.genMode == 1;
    if (!platformPickFolder(dir, ard ? L"스케치를 만들 폴더 (그 안에 스케치 폴더가 생깁니다)" : L"내보낼 폴더")) return;
    if (ard) dir += L"\\" + utf8ToWide(sketchName(g.doc));
    int kept = 0;
    const int ok = writeFiles(g.gen, dir, kept);
    if (ok == (int)g.gen.size()) {
        g.stampT = g.now;
        const int gone = removeStale(g.gen, dir);   // 다 쓴 뒤에만 옛 파일을 치운다
        showToast("내보내기 완료 · 파일 " + std::to_string(ok - kept) + "개 → " + wideToUtf8(dir) + (kept ? " (.ino는 그대로 둠)" : "") +
                  (gone ? " · 옛 파일 " + std::to_string(gone) + "개는 휴지통으로" : ""));
    } else {
        showToast("일부 파일을 쓰지 못함 · " + std::to_string(ok) + "/" + std::to_string(g.gen.size()));
    }
}

// 프로그램 정보와 함께 쓴 것들의 라이선스
static void drawAbout(ImGuiViewport* vp) {
    if (g.openAbout) { g.openAbout = false; ImGui::OpenPopup("##about"); }
    const ImVec2 es(std::min(px(620), vp->Size.x - px(80)), std::min(px(600), vp->Size.y - px(60)));
    ImGui::SetNextWindowPos(vp->Pos + vp->Size * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(es);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.f);
    if (ImGui::BeginPopupModal("##about", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, wp + ws, col::sheet);
        dl->AddRect(ImFloor(wp + ImVec2(px(10), px(10))), ImFloor(wp + ws - ImVec2(px(10), px(10))), col::ink);
        const float L = wp.x + px(32), R = wp.x + ws.x - px(32);
        float y = wp.y + px(28);
        textAt(dl, F.med, 26, ImVec2(L, y), col::ink, "HIGENIS", 8.f);
        textAt(dl, F.mono, 13, ImVec2(L + textW(F.med, 26, "HIGENIS", 8.f) + px(12), y + px(14)), col::ink2, "studio for LVGL");
        y += px(46);
        textAt(dl, F.ui, 15, ImVec2(L, y), col::ink, "LVGL 화면을 그리고, 눌러 보고, C 코드와 아두이노 스케치로 내보내는 편집기");
        y += px(26);
        textAt(dl, F.mono, 12, ImVec2(L, y), col::ink2, "버전 1.0.0  ·  © 2026 Higenis");
        y += px(30);
        hline(dl, L, R, y, col::ink);
        y += px(14);
        textAt(dl, F.med, 15, ImVec2(L, y), col::ink, "함께 쓴 것");
        y += px(28);
        static const char* rows[][2] = {
            {"LVGL 9.6", "MIT · 미리보기와 시운전의 화면"},
            {"Dear ImGui 1.93", "MIT · 편집기 화면"},
            {"stb_truetype · stb_image_write", "MIT 또는 퍼블릭 도메인"},
            {"IBM Plex Sans KR", "SIL OFL 1.1"},
            {"Barlow Condensed · Chivo Mono · Montserrat", "SIL OFL 1.1"},
            {"Font Awesome 5 Free (기호)", "글꼴 SIL OFL 1.1 · 아이콘 CC BY 4.0"},
        };
        for (auto& r : rows) {
            textAt(dl, F.ui, 15, ImVec2(L, y), col::ink, r[0]);
            textAt(dl, F.ui, 14, ImVec2(R - textW(F.ui, 14, r[1]), y + px(1)), col::ink2, r[1]);
            y += px(22);
            hline(dl, L, R, y + px(2), col::hair);
            y += px(8);
        }
        y += px(6);
        ImGui::SetCursorScreenPos(ImVec2(L, y));
        ImGui::PushTextWrapPos(R - wp.x);   // 창 기준 좌표
        pushFont(F.ui, 14);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col::ink2));
        ImGui::TextWrapped("LVGL은 해당 권리자의 상표이며, 이 제품은 LVGL 프로젝트와 관계가 없습니다. "
                           "표준도는 공개 프로젝트의 화면 배치만 참고했습니다(각 표준도에 출처). "
                           "라이선스 전문은 assets\\THIRD_PARTY_NOTICES.txt와 assets\\fonts에 있습니다.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        const float by = wp.y + ws.y - px(64);
        ImGui::SetCursorScreenPos(ImVec2(R - px(120), by));
        if (lineButton("닫기", ImVec2(px(120), px(34)), 1) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) ImGui::CloseCurrentPopup();
        ImGui::SetCursorScreenPos(ImVec2(R - px(120) - px(10) - px(170), by));
        if (lineButton("라이선스 전문 보기", ImVec2(px(170), px(34)))) platformOpenNotices();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// PC 글꼴 고르기
static void drawFontPicker(ImGuiViewport* vp) {
    static std::string query, text;
    static bool onlyKo = true;
    static int sel = -1;
    const int SW = (int)px(330), SH = (int)px(34), ROWS = 40;
    struct Entry { int font = -1; bool tried = false, ok = false, ko = false; std::string koName; int slot = -1; };
    static std::vector<Entry> info;
    static std::vector<uint32_t> atlas;
    static std::vector<int> slotOwner;
    static std::string sampled;
    static int atlasW = 0;
    if (g.openFonts) {
        g.openFonts = false;
        sel = -1;
        const Widget* w = findWidget(curScreen(), g.fontFor);
        text = w && !w->text.empty() && !symbolOf(fontgen::decode(w->text).empty() ? 0 : fontgen::decode(w->text)[0]) ? w->text.substr(0, w->text.find('\n')) : "다람쥐 헌 쳇바퀴 Abc 123";
        ImGui::OpenPopup("##fonts");
    }
    g.fontsPending = false;
    const ImVec2 es(std::min(px(860), vp->Size.x - px(80)), std::min(px(700), vp->Size.y - px(60)));
    ImGui::SetNextWindowPos(vp->Pos + vp->Size * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(es);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.f);
    if (ImGui::BeginPopupModal("##fonts", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
        const auto& fonts = fontface::installed();
        if (info.size() != fonts.size()) info.assign(fonts.size(), {});
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, wp + ws, col::sheet);
        dl->AddRect(ImFloor(wp + ImVec2(px(10), px(10))), ImFloor(wp + ws - ImVec2(px(10), px(10))), col::ink);
        const float L = wp.x + px(32), R = wp.x + ws.x - px(32), T = wp.y + px(26);
        textAt(dl, F.head, 26, ImVec2(L, T - px(8)), col::ink, "글꼴", 7.f);
        char b[200];
        // 견본 글자가 바뀌면 다시 그린다
        ImGui::SetCursorScreenPos(ImVec2(L + px(110), T - px(2)));
        pushFont(F.ui, 15);
        fieldText("##q", &query, px(170), "이름 찾기");
        ImGui::SetCursorScreenPos(ImVec2(L + px(300), T - px(2)));
        fieldText("##t", &text, px(240), "견본 글자");
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(L + px(560), T));
        toggleField("##ko", &onlyKo);
        textAt(dl, F.ui, 15, ImVec2(L + px(598), T + px(4)), col::ink2, "한글 되는 것만");
        {
            ImVec2 cp(wp.x + ws.x - px(52), wp.y + px(22));
            ImGui::SetCursorScreenPos(cp);
            if (ImGui::InvisibleButton("##close", ImVec2(px(26), px(26))) || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive())) ImGui::CloseCurrentPopup();
            const bool hv = ImGui::IsItemHovered();
            const ImVec2 c = cp + ImVec2(px(13), px(13));
            const float k = px(6);
            dl->AddLine(c - ImVec2(k, k), c + ImVec2(k, k), hv ? col::red : col::ink, 1.4f);
            dl->AddLine(c + ImVec2(-k, k), c + ImVec2(k, -k), hv ? col::red : col::ink, 1.4f);
        }
        hline(dl, L, R, T + px(40), col::ink);
        if (sampled != text || atlasW != SW) {
            sampled = text;
            atlasW = SW;
            atlas.assign((size_t)SW * SH * ROWS, 0);
            slotOwner.assign(ROWS, -1);
            for (Entry& e : info) { e.slot = -1; if (e.ok) e.tried = false; }
        }
        // 목록
        std::string ql = query;
        for (char& c : ql) c = (char)tolower((unsigned char)c);
        std::vector<int> shown;
        for (int i = 0; i < (int)fonts.size(); i++) {
            const Entry& e = info[i];
            if (e.tried && !e.ok) continue;
            if (onlyKo && e.tried && !e.ko) continue;
            if (!ql.empty()) {
                std::string n = fonts[i].name + " " + e.koName;
                for (char& c : n) c = (char)tolower((unsigned char)c);
                if (n.find(ql) == std::string::npos) continue;
            }
            shown.push_back(i);
        }
        ImGui::SetCursorScreenPos(ImVec2(L, T + px(46)));
        ImGui::BeginChild("##list", ImVec2(R - L, wp.y + ws.y - px(86) - (T + px(46))), 0, 0);
        ImDrawList* ll = ImGui::GetWindowDrawList();
        const float rowH = px(42), LW = ImGui::GetContentRegionAvail().x - px(8);
        bool dirty = false, start = false;
        int budget = 2;   // 한 프레임에 두 개까지만 읽는다(멈칫하지 않게)
        ImGuiListClipper clip;
        clip.Begin((int)shown.size(), rowH);
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                const int i = shown[r];
                Entry& e = info[i];
                const ImVec2 rp = ImGui::GetCursorScreenPos();
                ImGui::PushID(i);
                if (ImGui::InvisibleButton("##row", ImVec2(LW, rowH))) sel = i;
                const bool hv = ImGui::IsItemHovered();
                if (hv && ImGui::IsMouseDoubleClicked(0)) { sel = i; start = true; }
                ImGui::PopID();
                if (hv) { ll->AddRectFilled(rp, rp + ImVec2(LW, rowH - 1), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
                if ((!e.tried || (e.ok && e.slot < 0)) && budget > 0) {
                    budget--;
                    std::vector<uint8_t> bm;
                    bool ko = false;
                    e.tried = true;
                    e.ok = fontface::sample(fonts[i], sampled, SW, SH, px(21), bm, ko, e.koName);
                    e.ko = ko;
                    if (e.ok) {
                        // 화면에서 가장 먼 자리를 비운다
                        int slot = -1;
                        for (int k = 0; k < ROWS && slot < 0; k++) if (slotOwner[k] < 0) slot = k;
                        if (slot < 0) {
                            for (int k = 0; k < ROWS && slot < 0; k++) {
                                bool vis = false;
                                for (int q = clip.DisplayStart; q < clip.DisplayEnd; q++) if (shown[q] == slotOwner[k]) vis = true;
                                if (!vis) slot = k;
                            }
                            if (slot >= 0) info[slotOwner[slot]].slot = -1;
                        }
                        if (slot >= 0) {
                            slotOwner[slot] = i;
                            e.slot = slot;
                            for (int y = 0; y < SH; y++)
                                for (int x = 0; x < SW; x++) atlas[((size_t)slot * SH + y) * SW + x] = (uint32_t)bm[(size_t)y * SW + x] << 24 | 0x002A2926u;
                            dirty = true;
                        }
                    }
                }
                if (!e.tried || (e.ok && e.slot < 0)) g.fontsPending = true;
                const bool on = sel == i;
                const std::string& nm = e.koName.empty() ? fonts[i].name : e.koName;
                textAt(ll, on ? F.med : F.ui, 15, rp + ImVec2(px(6), px(10)), on ? col::red : col::ink, ellipsize(F.ui, 15, nm, LW - SW - px(40)).c_str());
                if (e.slot >= 0)
                    if (void* tex = platformCanvasTexture(4)) {
                        const ImVec2 o = ImFloor(rp + ImVec2(LW - SW - px(6), (rowH - SH) * 0.5f));
                        ll->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex), o, o + ImVec2((float)SW, (float)SH), ImVec2(0, (float)e.slot / ROWS), ImVec2(1, (float)(e.slot + 1) / ROWS));
                    }
                hline(ll, rp.x, rp.x + LW, rp.y + rowH - 1, on ? col::red : col::hair);
            }
        ImGui::EndChild();
        if (dirty) platformUploadCanvas(atlas.data(), SW, SH * ROWS, 1, 4);
        // 아래
        const float by = wp.y + ws.y - px(66);
        hline(dl, L, R, by - px(10), col::ink);
        snprintf(b, sizeof b, "%d개  ·  도면에는 쓴 글자의 윤곽만 담깁니다. 배포할 때는 글꼴의 사용 조건을 확인하세요.", (int)shown.size());
        textAt(dl, F.ui, 14, ImVec2(L, by + px(8)), col::ink2, ellipsize(F.ui, 14, b, R - L - px(170)).c_str());
        ImGui::SetCursorScreenPos(ImVec2(R - px(150), by));
        if (lineButton("이 글꼴 쓰기", ImVec2(px(150), px(34)), sel >= 0 ? 1 : 2)) start = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && sel >= 0) start = true;
        if (start && sel >= 0 && sel < (int)fonts.size()) {
            if (Widget* w = findWidget(curScreen(), g.fontFor)) {
                const std::string nm = info[sel].koName.empty() ? fonts[sel].name : info[sel].koName;
                const int id = fontface::add(g.doc, wideToUtf8(fonts[sel].path), fonts[sel].index, nm);
                if (id) {
                    w->style[ImClamp(g.fontState, 0, ST_COUNT - 1)][SP_FACE] = {true, id};
                    commit(w->name + " 글꼴 " + nm);
                } else showToast("글꼴을 열 수 없습니다 · " + nm);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// 표준도 고르기: 견본은 LVGL로 바로 그려 한 장의 텍스처에 모은다
struct StdThumb { int w = 0, h = 0; ImVec2 uv0, uv1; };
static std::vector<StdThumb> g_stdThumbs;
static void buildStandardThumbs(int tw, int th) {
    const auto& st = standards();
    const int cols = 3, rows = std::max(1, ((int)st.size() + cols - 1) / cols), W = cols * tw, H = rows * th;
    std::vector<uint32_t> atlas((size_t)W * H, 0), one;
    g_stdThumbs.assign(st.size(), {});
    for (int i = 0; i < (int)st.size(); i++) {
        StdThumb& t = g_stdThumbs[i];
        lvhost::thumbnail(st[i].doc, tw, th, one, t.w, t.h);
        const int ox = (i % cols) * tw, oy = (i / cols) * th;
        for (int y = 0; y < t.h; y++) memcpy(&atlas[(size_t)(oy + y) * W + ox], &one[(size_t)y * t.w], (size_t)t.w * 4);
        t.uv0 = ImVec2((float)ox / W, (float)oy / H);
        t.uv1 = ImVec2((float)(ox + t.w) / W, (float)(oy + t.h) / H);
    }
    platformUploadCanvas(atlas.data(), W, H, 1, 2);
    g.traceKey.clear();
    rebuildNow();   // 화면 객체를 지금 도면으로 되돌린다
}

static void drawStandards(ImGuiViewport* vp) {
    static double hoverT = 0;
    static int hoverIdx = -1;
    static int builtW = 0;
    const ImVec2 es(std::min(px(1060), vp->Size.x - px(60)), std::min(px(800), vp->Size.y - px(40)));
    const float padX = px(32), gapc = px(16);
    const float matH = px(152), ch = matH + px(40), footH = px(88);
    const bool scroll = ((standardCount() + 2) / 3) * (ch + px(12)) > es.y - footH - px(78);   // 다 보이면 오른쪽 끝까지 쓴다
    const float cw = floorf((es.x - padX * 2 - gapc * 2 - (scroll ? px(12) : 0)) / 3);
    const int tw = (int)(cw - px(56)), th = (int)(matH - px(40));
    if (g.openStd) {
        g.openStd = false;
        if (standardCount() == 0) showToast("표준도 폴더가 비어 있습니다");
        else {
            if (builtW != tw) { buildStandardThumbs(tw, th); builtW = tw; }
            g.stdSel = ImClamp(g.stdSel, 0, standardCount() - 1);
            ImGui::OpenPopup("##std");
        }
    }
    ImGui::SetNextWindowPos(vp->Pos + vp->Size * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(es);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.f);
    if (ImGui::BeginPopupModal("##std", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto& st = standards();
        const int n = (int)st.size();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, wp + ws, col::sheet);
        dl->AddRect(ImFloor(wp + ImVec2(px(10), px(10))), ImFloor(wp + ws - ImVec2(px(10), px(10))), col::ink);
        char b[160];
        const float L = wp.x + padX, T = wp.y + px(26), R = wp.x + ws.x - padX;
        textAt(dl, F.head, 26, ImVec2(L, T - px(8)), col::ink, "표준도", 7.f);
        snprintf(b, sizeof b, "기준 도면에서 시작합니다  ·  %d장", n);
        textAt(dl, F.ui, 15, ImVec2(L + px(124), T + px(5)), col::ink2, b);
        {
            ImVec2 cp(wp.x + ws.x - px(54), wp.y + px(22));
            ImGui::SetCursorScreenPos(cp);
            if (ImGui::InvisibleButton("##close", ImVec2(px(26), px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            bool hv = ImGui::IsItemHovered();
            ImVec2 c = cp + ImVec2(px(13), px(13));
            float k = px(6);
            dl->AddLine(c - ImVec2(k, k), c + ImVec2(k, k), hv ? col::red : col::ink, 1.4f);
            dl->AddLine(c + ImVec2(-k, k), c + ImVec2(k, -k), hv ? col::red : col::ink, 1.4f);
        }
        hline(dl, L, R, T + px(40), col::ink);
        // 방향키로 고르기
        int sel = g.stdSel;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) sel = std::min(n - 1, sel + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) sel = std::max(0, sel - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) sel = std::min(n - 1, sel + 3);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) sel = std::max(0, sel - 3);
        const bool moved = sel != g.stdSel;
        g.stdSel = sel;
        bool start = ImGui::IsKeyPressed(ImGuiKey_Enter), add = false;

        ImGui::SetCursorScreenPos(ImVec2(L, T + px(52)));
        ImGui::BeginChild("##stdgrid", ImVec2(R - L, wp.y + ws.y - footH - (T + px(52))), 0, 0);
        ImDrawList* gl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetCursorScreenPos();
        int hov = -1;
        for (int i = 0; i < n; i++) {
            ImVec2 a = ImFloor(o + ImVec2((i % 3) * (cw + gapc), (i / 3) * (ch + px(12))));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##card", ImVec2(cw, ch))) g.stdSel = i;
            bool hv = ImGui::IsItemHovered();
            if (hv && ImGui::IsMouseDoubleClicked(0)) start = true;
            ImGui::PopID();
            if (moved && i == g.stdSel) ImGui::ScrollToItem();
            if (hv) { hov = i; ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
            const bool on = i == g.stdSel;
            const Document& d = st[i].doc;
            gl->AddRectFilled(a, a + ImVec2(cw, matH), col::paper);
            gl->AddRect(a, a + ImVec2(cw, matH), on ? col::red : hv ? col::ink2 : col::hair2);
            const StdThumb& t = g_stdThumbs[i];
            ImVec2 ia = ImFloor(a + ImVec2((cw - t.w) * 0.5f + px(6), (matH - t.h) * 0.5f + px(6))), ib = ia + ImVec2((float)t.w, (float)t.h);
            if (void* tex = platformCanvasTexture(2)) gl->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex), ia, ib, t.uv0, t.uv1);
            if (d.round) gl->AddCircle((ia + ib) * 0.5f, t.w * 0.5f + 0.5f, on ? col::red : col::ink, 120, 1.f);
            else gl->AddRect(ia - ImVec2(1, 1), ib + ImVec2(1, 1), on ? col::red : col::ink);
            // 올려놓거나 고르면 치수선이 그려진다
            if (hv || on) {
                float tm = hv && hoverIdx == i ? (float)ImClamp((g.now - hoverT) / 0.25, 0.0, 1.0) : 1.f;
                snprintf(b, sizeof b, "%d", d.resW);
                dimH(gl, ia.x, ib.x, ia.y - px(13), ia.y, b, col::red, tm);
                snprintf(b, sizeof b, "%d", d.resH);
                dimV(gl, ia.y, ib.y, ia.x - px(13), ia.x, b, col::red, tm);
            }
            snprintf(b, sizeof b, "%02d", i + 1);
            textAt(gl, F.mono, 12, a + ImVec2(0, matH + px(9)), col::red, b);
            textAt(gl, F.med, 15, a + ImVec2(px(26), matH + px(6)), on ? col::red : col::ink, ellipsize(F.med, 15, d.project, cw - px(120)).c_str());
            int parts = 0;
            for (auto& sc : d.screens) parts += (int)sc.widgets.size();
            snprintf(b, sizeof b, "%s부품 %d", d.round ? "원형 · " : "", parts);
            textAt(gl, F.ui, 14, a + ImVec2(cw - textW(F.ui, 14, b), matH + px(8)), col::ink2, b);
        }
        if (hov != hoverIdx) { hoverIdx = hov; hoverT = g.now; }
        ImGui::SetCursorScreenPos(o);
        ImGui::Dummy(ImVec2(1, ((n + 2) / 3) * (ch + px(12))));
        ImGui::EndChild();

        // 아래: 설명과 단추
        const Document& d = st[g.stdSel].doc;
        const float by = wp.y + ws.y - footH + px(12);
        hline(dl, L, R, by - px(8), col::ink);
        textAt(dl, F.ui, 15, ImVec2(L, by + px(2)), col::ink, ellipsize(F.ui, 15, d.about, R - L - px(340)).c_str());
        textAt(dl, F.ui, 14, ImVec2(L, by + px(26)), col::ink2, ellipsize(F.ui, 14, d.credit, R - L - px(340)).c_str());
        const bool same = d.resW == g.doc.resW && d.resH == g.doc.resH;
        ImGui::SetCursorScreenPos(ImVec2(R - px(150), by + px(6)));
        if (lineButton("이 도면으로 시작", ImVec2(px(150), px(34)), 1)) start = true;
        ImGui::SetCursorScreenPos(ImVec2(R - px(150) - px(10) - px(130), by + px(6)));
        if (lineButton("시트로 더하기", ImVec2(px(130), px(34)), same ? 0 : 2)) add = true;
        if (!same) {
            snprintf(b, sizeof b, "지금 도면은 %d × %d라서 더할 수 없습니다", g.doc.resW, g.doc.resH);
            tooltip(b);
        }
        if (add && same) { ImGui::CloseCurrentPopup(); addStandardSheet(g.stdSel); }
        else if (add) { snprintf(b, sizeof b, "해상도가 달라 시트로 더할 수 없습니다 · 지금 %d × %d", g.doc.resW, g.doc.resH); showToast(b); }
        if (start) { ImGui::CloseCurrentPopup(); startStandard(g.stdSel); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void drawModals() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    char b[256];

    // 검도
    if (g.openLint) { ImGui::OpenPopup("##lint"); g.openLint = false; }
    g.lintOpen = false;
    g.lintHover = 0;
    ImGui::SetNextWindowPos(g.lintAnchor, ImGuiCond_Appearing, ImVec2(1, 1));
    ImGui::SetNextWindowSize(ImVec2(px(400), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    if (ImGui::BeginPopup("##lint")) {
        g.lintOpen = true;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int errors = 0, advice = 0;
        for (auto& is : g.issues) (is.error ? errors : advice)++;
        snprintf(b, sizeof b, "오류 %d · 권고 %d", errors, advice);
        {
            char t[160];
            snprintf(t, sizeof t, "%s  ·  %s", curScreen().name.c_str(), b);
            sectionHeader("", "검도", t);
        }
        if (g.issues.empty()) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            stamp(dl, p + ImVec2(px(34), px(34)), px(28), "검도", todayStamp().c_str() + 2, -0.18f, 1.f);
            textAt(dl, F.med, 16, p + ImVec2(px(76), px(16)), col::ink, "문제 없음");
            textAt(dl, F.ui, 14, p + ImVec2(px(76), px(38)), col::ink2, "이대로 내보내도 좋습니다.");
            ImGui::Dummy(ImVec2(px(300), px(72)));
        }
        for (int i = 0; i < (int)g.issues.size(); i++) {
            const Issue& is = g.issues[i];
            const Widget* w = findWidget(curScreen(), is.widget);
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float W = ImGui::GetContentRegionAvail().x;
            float h = px(42);
            if (ImGui::InvisibleButton("##is", ImVec2(W, h))) { selectWidget(is.widget); ImGui::CloseCurrentPopup(); }
            bool hv = ImGui::IsItemHovered();
            if (hv) { dl->AddRectFilled(p, p + ImVec2(W, h), col::hover); g.lintHover = is.widget; ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
            ImVec2 c = p + ImVec2(px(7), px(13));
            if (is.error) dl->AddCircleFilled(c, px(3.5f), col::red);
            else dl->AddCircle(c, px(3.5f), col::ink2, 12, 1.2f);
            snprintf(b, sizeof b, "%02d", widgetNumber(is.widget));
            textAt(dl, F.mono, 12, p + ImVec2(px(18), px(5)), is.error ? col::red : col::ink2, b);
            textAt(dl, F.med, 14, p + ImVec2(px(42), px(3)), col::ink, w ? w->name.c_str() : "?");
            textAt(dl, F.ui, 14, p + ImVec2(px(42), px(21)), col::ink2, ellipsize(F.ui, 14, is.msg, W - px(48)).c_str());
            dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + W, p.y + h - 0.5f), col::hair);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // 개정 이력 전체
    if (g.openRevs) { ImGui::OpenPopup("##revs"); g.openRevs = false; }
    ImGui::SetNextWindowPos(vp->Pos + vp->Size * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(480), std::min(px(76) + (float)g.hist.revs.size() * px(26), px(520))));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(18), px(16)));
    if (ImGui::BeginPopup("##revs")) {
        snprintf(b, sizeof b, "%d건", (int)g.hist.revs.size());
        sectionHeader("", "개정 이력", b);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::BeginChild("##revlist", ImVec2(0, 0));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = (int)g.hist.revs.size() - 1; i >= 0; i--) {
            const Revision& r = g.hist.revs[i];
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float W = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##r", ImVec2(W, px(26)))) { gotoRevision(i); ImGui::CloseCurrentPopup(); }
            bool hv = ImGui::IsItemHovered();
            if (hv) dl->AddRectFilled(p, p + ImVec2(W, px(26)), col::hover);
            bool cur = i == g.hist.cur, fut = i > g.hist.cur;
            textAt(dl, F.mono, 12, p + ImVec2(px(4), px(5)), cur ? col::red : fut ? col::ink3 : col::ink2, g.hist.label(i).c_str());
            textAt(dl, F.ui, 15, p + ImVec2(px(48), px(3)), fut ? col::ink3 : col::ink, ellipsize(F.ui, 15, r.note, W - px(140)).c_str());
            textAt(dl, F.mono, 11, p + ImVec2(W - px(80), px(6)), col::ink3, r.time.c_str());
            dl->AddLine(ImVec2(p.x, p.y + px(26) - 0.5f), ImVec2(p.x + W, p.y + px(26) - 0.5f), col::hair);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // 내보내기
    if (g.openExport) { ImGui::OpenPopup("##export"); g.openExport = false; }
    ImVec2 es(std::min(px(g.genMode == 1 ? 1200.f : 1000.f), vp->Size.x - px(80)), std::min(px(g.genMode == 1 ? 780.f : 680.f), vp->Size.y - px(60)));
    ImGui::SetNextWindowPos(vp->Pos + vp->Size * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(es);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.f);
    if (ImGui::BeginPopupModal("##export", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, wp + ws, col::sheet);
        dl->AddRect(ImFloor(wp + ImVec2(px(10), px(10))), ImFloor(wp + ws - ImVec2(px(10), px(10))), col::ink);
        float L = wp.x + px(32), T = wp.y + px(28);
        textAt(dl, F.head, 26, ImVec2(L, T - px(8)), col::ink, "내보내기", 7.f);
        // 줄 수는 파일이 바뀔 때만 센다
        static std::vector<int> lineCount;
        static const void* counted = nullptr;
        static size_t countedN = 0;
        if (counted != (const void*)g.gen.data() || countedN != g.gen.size() || lineCount.size() != g.gen.size()) {
            lineCount.clear();
            for (auto& f : g.gen) lineCount.push_back(f.binary ? 0 : (int)std::count(f.text.begin(), f.text.end(), '\n'));
            counted = g.gen.data();
            countedN = g.gen.size();
        }
        {
            const char* modes[2] = {"C 코드", "아두이노"};
            int m = g.genMode;
            ImGui::SetCursorScreenPos(ImVec2(L + px(166), T - px(2)));
            ImGui::PushClipRect(ImVec2(L, T - px(8)), ImVec2(L + px(370), T + px(40)), true);
            ImGui::BeginChild("##modes", ImVec2(px(190), px(36)), 0, ImGuiWindowFlags_NoScrollbar);
            if (segTabs("##mode", modes, 2, &m) && m != g.genMode) { g.genMode = m; regenerate(); counted = nullptr; }
            ImGui::EndChild();
            ImGui::PopClipRect();
        }
        snprintf(b, sizeof b, "파일 %d개  ·  개정 %s", (int)g.gen.size(), g.hist.label(g.hist.cur).c_str());
        textAt(dl, F.ui, 15, ImVec2(L + px(380), T + px(4)), col::ink2, b);
        // 닫기
        {
            ImVec2 cp(wp.x + ws.x - px(52), wp.y + px(24));
            ImGui::SetCursorScreenPos(cp);
            if (ImGui::InvisibleButton("##close", ImVec2(px(26), px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            bool hv = ImGui::IsItemHovered();
            ImVec2 c = cp + ImVec2(px(13), px(13));
            float k = px(6);
            dl->AddLine(c - ImVec2(k, k), c + ImVec2(k, k), hv ? col::red : col::ink, 1.4f);
            dl->AddLine(c + ImVec2(-k, k), c + ImVec2(k, -k), hv ? col::red : col::ink, 1.4f);
        }
        float top = T + px(52);
        hline(dl, L, wp.x + ws.x - px(32), top - px(10), col::ink);
        // 파일 목록
        float listW = px(g.genMode == 1 ? 210.f : 200.f);
        const float boardW = g.genMode == 1 ? px(372) : 0.f;
        ImGui::SetCursorScreenPos(ImVec2(L, top));
        ImGui::BeginChild("##files", ImVec2(listW + px(8), wp.y + ws.y - px(72) - top), 0, 0);
        ImDrawList* mainDl = dl;
        dl = ImGui::GetWindowDrawList();
        const ImVec2 fo = ImGui::GetCursorScreenPos();
        for (int i = 0; i < (int)g.gen.size(); i++) {
            ImGui::PushID(i);
            ImVec2 a(fo.x, fo.y + i * px(28));
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##f", ImVec2(listW, px(28)))) g.genTab = i;
            bool hv = ImGui::IsItemHovered(), on = i == g.genTab;
            if (hv && !on) dl->AddRectFilled(a, a + ImVec2(listW, px(28)), col::hover);
            textAt(dl, F.mono, 13, a + ImVec2(px(6), px(6)), on ? col::red : col::ink, ellipsize(F.mono, 13, g.gen[i].name, listW - px(54)).c_str());
            const int ln = i < (int)lineCount.size() ? lineCount[i] : 0;
            if (g.gen[i].binary) snprintf(b, sizeof b, "%.0fK", std::max(1.0, g.gen[i].text.size() / 1024.0));
            else snprintf(b, sizeof b, "%d", ln);
            textAt(dl, F.mono, 11, a + ImVec2(listW - textW(F.mono, 11, b) - px(6), px(8)), col::ink3, b);
            if (on) dl->AddLine(ImVec2(a.x, a.y + px(28) - 0.5f), ImVec2(a.x + listW, a.y + px(28) - 0.5f), col::red);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(fo);
        ImGui::Dummy(ImVec2(1, g.gen.size() * px(28)));
        ImGui::EndChild();
        dl = mainDl;
        float codeX = L + listW + px(18);
        float bottomH = px(64);
        ImGui::SetCursorScreenPos(ImVec2(codeX, top));
        if (!g.gen.empty()) {
            g.genTab = ImClamp(g.genTab, 0, (int)g.gen.size() - 1);
            const GenFile& gf = g.gen[g.genTab];
            static std::string shown;
            static const void* shownOf = nullptr;
            static size_t shownLen = 0;
            if (shownOf != (const void*)gf.text.data() || shownLen != gf.text.size()) {   // 그림 배열처럼 긴 것은 앞부분만
                shownOf = gf.text.data();
                shownLen = gf.text.size();
                if (gf.binary) shown = "이진 파일 " + std::to_string(gf.text.size()) + " 바이트\n\nLVGL 그림 파일(.bin)입니다.\nSD 카드의 " + gf.name.substr(2) + " 에 두세요.";
                else shown = gf.text.size() > 120000 ? gf.text.substr(0, 4000) + "\n… 이하 생략" : gf.text;
            }
            drawCodeView(shown, ImVec2(wp.x + ws.x - px(32) - codeX - (boardW > 0 ? boardW + px(18) : 0.f), wp.y + ws.y - bottomH - top - px(8)));
        }
        if (g.genMode == 1) {
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - px(32) - boardW, top));
            ImGui::BeginChild("##board", ImVec2(boardW, wp.y + ws.y - bottomH - top - px(8)), 0, 0);
            if (drawBoard()) { regenerateKeepTab(); counted = nullptr; }
            ImGui::EndChild();
        }
        // 아래 단추
        float by = wp.y + ws.y - bottomH + px(8);
        textAt(dl, F.ui, 14, ImVec2(L, by - px(2)), col::ink2, g.genMode == 1 ? "LVGL 9.1 이상이 필요합니다. .ino는 처음 한 번만 만들고 다시 덮어쓰지 않습니다." : "lv_conf.h에 필요한 설정은 ui.h 머리말에 적혀 있습니다.");
        {
            const fontgen::Plan pl = fontgen::plan(g.doc);
            if (!pl.sizes.empty()) {
                int bytes = 0;
                std::string sz;
                for (auto& [size, cps] : pl.sizes) { bytes += pl.bytes(size); sz += (sz.empty() ? "" : " · ") + std::to_string(size); (void)cps; }
                snprintf(b, sizeof b, "한글 글꼴 ui_font.c  ·  %s px  ·  %.1f KB", sz.c_str(), bytes / 1024.0);
                textAt(dl, F.ui, 14, ImVec2(L, by + px(17)), col::ink2, b);
            }
        }
        float bx = wp.x + ws.x - px(32);
        ImGui::SetCursorScreenPos(ImVec2(bx - px(150), by));
        if (lineButton(g.genMode == 1 ? "스케치 저장…" : "폴더에 저장…", ImVec2(px(150), px(32)), 1)) exportToFolder();
        ImGui::SetCursorScreenPos(ImVec2(bx - px(150) - px(10) - px(110), by));
        if (lineButton("복사", ImVec2(px(110), px(32)), 0, g.gen.empty() ? nullptr : "현재 파일") && !g.gen.empty()) {
            ImGui::SetClipboardText(g.gen[g.genTab].text.c_str());
            showToast("복사됨 · " + g.gen[g.genTab].name);
        }
        // 내보내기 도장
        if (g.stampT > 0) {
            float t = (float)(g.now - g.stampT);
            float k = easeOut(t / 0.22f);
            float r = px(50) * (1.f + 0.5f * (1.f - k));
            stamp(dl, ImVec2(wp.x + ws.x - px(118), wp.y + px(64)), r, "완료", todayStamp().c_str(), -0.21f, std::min(1.f, t / 0.1f));
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);

    drawStandards(vp);
    drawFontPicker(vp);
    drawAbout(vp);

    // 저장 확인
    if (g.pending && !ImGui::IsPopupOpen("##confirm")) ImGui::OpenPopup("##confirm");
    ImGui::SetNextWindowPos(vp->Pos + vp->Size * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(440), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(24), px(20)));
    if (ImGui::BeginPopupModal("##confirm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        textAt(dl, F.med, 19, p, col::ink, "저장하지 않은 개정이 있습니다");
        int unsaved = std::max(1, g.hist.cur);
        snprintf(b, sizeof b, "%s 도면의 개정 %s까지가 아직 파일에 없습니다.", g.doc.project.c_str(), g.hist.label(g.hist.cur).c_str());
        textAt(dl, F.ui, 15, p + ImVec2(0, px(32)), col::ink2, b);
        (void)unsaved;
        ImGui::Dummy(ImVec2(px(380), px(66)));
        float W = ImGui::GetContentRegionAvail().x;
        ImVec2 bp = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(bp.x + W - px(110), bp.y));
        if (lineButton("저장", ImVec2(px(110), px(32)), 1)) {
            int a = g.pending;
            if (saveCurrent(false)) { g.pending = 0; ImGui::CloseCurrentPopup(); doAction(a); }
        }
        ImGui::SetCursorScreenPos(ImVec2(bp.x + W - px(230), bp.y));
        if (lineButton("저장 안 함", ImVec2(px(110), px(32)))) {
            int a = g.pending;
            g.pending = 0;
            g.dirty = false;
            ImGui::CloseCurrentPopup();
            doAction(a);
        }
        ImGui::SetCursorScreenPos(ImVec2(bp.x, bp.y));
        if (lineButton("취소", ImVec2(px(80), px(32)), 2) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { g.pending = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // 쪽지(알림)
    if (g.now - g.toastT < 2.6 && !g.toast.empty()) {
        float t = (float)(g.now - g.toastT);
        float a = t < 0.15f ? t / 0.15f : t > 2.2f ? (2.6f - t) / 0.4f : 1.f;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        float w = textW(F.med, 15, g.toast.c_str()) + px(32);
        // 제도판 가운데 아래. 좁은 창에서 표제란과 겹치면 왼쪽으로 비킨다
        const ImVec2 cv = canvasCenter(), tl = canvasTitleLeft();
        float tx = cv.x - w * 0.5f;
        if (tx + w > tl.x - px(10)) tx = tl.x - px(10) - w;
        ImVec2 p = ImFloor(ImVec2(std::max(tx, vp->Pos.x + px(10)), vp->Pos.y + vp->Size.y - px(28) - px(62) + (1 - a) * px(6)));
        fg->AddRectFilled(p, p + ImVec2(w, px(36)), alpha(col::sheet, a));
        fg->AddRect(p, p + ImVec2(w, px(36)), alpha(col::ink, a), 0, 1.2f);
        fg->AddLine(p + ImVec2(px(3), px(36) + px(2)), p + ImVec2(w + px(2), px(36) + px(2)), alpha(col::hair2, a));
        fg->AddLine(p + ImVec2(w + px(2), px(3)), p + ImVec2(w + px(2), px(36) + px(2)), alpha(col::hair2, a));
        textAt(fg, F.med, 15, p + ImVec2(px(16), px(8)), alpha(col::ink, a), g.toast.c_str());
    }
}

// ───────────────────────── 프레임 ─────────────────────────
bool uiWantsAnimation() {
    return g.now - g.selAnimT < 0.4 || fabsf(g.zoom - g.zoomTarget) > 1e-4f || g.now - g.stampT < 0.5 ||
           g.now - g.toastT < 2.7 || g.now - g.flashSheetT < 0.7 || g.drag != Drag::None || g.now - g.revAnimT < 0.6 ||
           !g.ghosts.empty() || g.fontsPending || (g.sim && (g.simDown || g.now < g.simBusy));
}

void uiFrame() {
    ImGuiIO& io = ImGui::GetIO();
    g.now = ImGui::GetTime();
    g.dt = io.DeltaTime;
    lvhost::update();
    if (g.sim) {
        simFrame();
        if (g.sim && wantView() != g.viewK) { lvhost::setView(wantView()); lvhost::redraw(); }
        if (g.sim && (lvhost::takeFrame() || (g.viewK == 0.f && wantPrescale() != g.texPre))) uploadCanvas();
    }
    if (!g.sim) {
        if (g.needsBuild || viewStale()) rebuildNow();
        else if (lvhost::takeFrame()) uploadCanvas();
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus);
    pushFont(F.ui, 15);
    shortcuts();
    const float W = vp->Size.x, H = vp->Size.y;
    const float topH = px(48), stH = px(30), leftW = px(260), rightW = px(328);
    ImVec2 o = vp->Pos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (g.sim) {
        drawSimTop(o, ImVec2(W, topH));
        drawSimLeft(o + ImVec2(0, topH), ImVec2(leftW, H - topH - stH));
    } else {
        drawTopBar(o, ImVec2(W, topH));
        drawLeft(o + ImVec2(0, topH), ImVec2(leftW, H - topH - stH));
    }
    ImVec2 cp = o + ImVec2(leftW, topH), cs(W - leftW - rightW, H - topH - stH);
    ImGui::SetCursorScreenPos(cp);
    ImGui::BeginChild("##canvaswin", cs, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas(cp, cs);
    ImGui::EndChild();
    if (g.sim) {
        drawSimRight(o + ImVec2(W - rightW, topH), ImVec2(rightW, H - topH - stH));
        drawSimStatus(o + ImVec2(0, H - stH), ImVec2(W, stH));
    } else {
        drawRight(o + ImVec2(W - rightW, topH), ImVec2(rightW, H - topH - stH));
        drawStatus(o + ImVec2(0, H - stH), ImVec2(W, stH));
    }
    if ((g.live || g.nameEdit || g.sheetEdit >= 0) && !ImGui::IsAnyItemActive() && GImGui->OpenPopupStack.Size == 0) flushLive();
    vline(dl, o.x + leftW - 1, o.y + topH, o.y + H - stH, col::ink);
    vline(dl, o.x + W - rightW, o.y + topH, o.y + H - stH, col::ink);
    drawModals();
    ImGui::PopFont();
    ImGui::End();

    static std::wstring lastTitle;
    std::wstring t = utf8ToWide(g.doc.project) + (g.dirty ? L" ●" : L"") + L" — Higenis Studio";
    if (t != lastTitle) { platformSetTitle(t); lastTitle = t; }
}
