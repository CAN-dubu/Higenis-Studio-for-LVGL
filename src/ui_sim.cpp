// 시운전: 도면을 살아 있는 LVGL 화면으로 돌려 실제로 눌러 본다
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
size_t g_seen = 0;       // 기록에 새 줄이 생겼는지
double g_newT = -10;
int g_seenCount = 0;

void gap(float h) { ImGui::Dummy(ImVec2(1, px(h))); }

void jump(int i) {
    lvhost::simShow(i);
    g.simBusy = g.now + 1.0;
}
void exitSim() {
    if (!g.sim) return;
    g.sim = false;
    g.simDown = false;
    g.simFrameNo = ImGui::GetFrameCount();
    lvhost::simStop();
    g.screen = ImClamp(g.simBack, 0, (int)g.doc.screens.size() - 1);
    g.needsBuild = true;
}
}  // namespace

void simToggle() {
    if (g.sim) { exitSim(); return; }
    flushLive();
    g.simBack = g.screen;
    g.sel = 0;
    g.armed = -1;
    g.hover = 0;
    g.renameId = 0;
    g.editState = ST_DEFAULT;
    g.drag = Drag::None;
    g.simDown = false;
    g.simSlow = false;
    if (g.needsBuild) rebuildNow();
    g.sim = true;
    g_seen = 0;
    g_seenCount = 0;
    lvhost::simStart(g.doc, g.screen);
    g.simBusy = g.now + 1.0;
}

void simFrame() {
    ImGuiIO& io = ImGui::GetIO();
    if (GImGui->OpenPopupStack.Size > 0) return;
    const bool typing = lvhost::simWantsText();
    if (typing) {   // 한글 입력기를 켜고, 들어온 글자를 입력창에 넣는다
        GImGui->WantTextInputNextFrame = 1;
        std::string u;
        for (ImWchar c : io.InputQueueCharacters) {
            if (c < 0x20 || c == 0x7F) continue;
            char b[8];
            ImTextCharToUtf8(b, c);
            u += b;
            if (!lvhost::simHas(c)) showToast(std::string("‘") + b + "’는 내보낼 글꼴에 없어 장치에서도 빈 네모로 보입니다");
        }
        if (!u.empty() && !io.KeyCtrl) lvhost::simType(u);
        auto R = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
        if (R(ImGuiKey_Backspace)) lvhost::simKey(lvhost::SK_BACK);
        if (R(ImGuiKey_Delete)) lvhost::simKey(lvhost::SK_DEL);
        if (R(ImGuiKey_Enter) || R(ImGuiKey_KeypadEnter)) lvhost::simKey(lvhost::SK_ENTER);
        if (R(ImGuiKey_LeftArrow)) lvhost::simKey(lvhost::SK_LEFT);
        if (R(ImGuiKey_RightArrow)) lvhost::simKey(lvhost::SK_RIGHT);
        if (R(ImGuiKey_Home)) lvhost::simKey(lvhost::SK_HOME);
        if (R(ImGuiKey_End)) lvhost::simKey(lvhost::SK_END);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { exitSim(); return; }
    if (io.KeyCtrl) {
        const int n = (int)g.doc.screens.size();
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) jump((lvhost::simScreen() + (io.KeyShift ? n - 1 : 1)) % n);
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) jump(lvhost::simScreen());
    } else if (!typing) {
        if (ImGui::IsKeyPressed(ImGuiKey_P, false)) g.pixelView = !g.pixelView;
        if (ImGui::IsKeyPressed(ImGuiKey_0, false)) fitView();
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) setScale(1, canvasCenter());
        if (ImGui::IsKeyPressed(ImGuiKey_Equal)) setScale(g.scaleIdx + 1, canvasCenter());
        if (ImGui::IsKeyPressed(ImGuiKey_Minus)) setScale(g.scaleIdx - 1, canvasCenter());
    }
    g.screen = lvhost::simScreen();
    auto& log = lvhost::simLog();
    const int cnt = log.empty() ? 0 : log.back().count;
    if (log.size() != g_seen || cnt != g_seenCount) { g_seen = log.size(); g_seenCount = cnt; g_newT = g.now; g.simBusy = std::max(g.simBusy, g.now + 0.7); }
    // 손을 뗀 뒤에도 관성 스크롤과 애니메이션이 끝날 때까지 읽는다
    if (!g.simDown) lvhost::simPointer((int)g.simPt.x, (int)g.simPt.y, false);
}

void simCanvas(ImDrawList* dl, bool hover, ImVec2 dev, ImVec2 devA, ImVec2 devB, float) {
    const bool round = g.doc.round && g.doc.resW == g.doc.resH;
    bool inside = dev.x >= 0 && dev.y >= 0 && dev.x < g.doc.resW && dev.y < g.doc.resH;
    if (inside && round) {
        const float c = g.doc.resW * 0.5f;
        inside = (dev.x - c) * (dev.x - c) + (dev.y - c) * (dev.y - c) <= c * c;
    }
    const bool mouse = ImGui::IsMouseDown(0);
    if (hover && inside && ImGui::IsMouseClicked(0)) g.simDown = true;
    if (!mouse && g.simDown) {
        g.simDown = false;
        g.simBusy = g.now + 1.2;
        lvhost::simPointer((int)dev.x, (int)dev.y, false);
    }
    if (g.simDown) {
        g.simPt = dev;
        lvhost::simPointer((int)floorf(dev.x), (int)floorf(dev.y), true);
        if (g.simTouch) {   // 손가락 끝(약 7 mm)이 덮는 자리
            const ImVec2 m = ImGui::GetIO().MousePos;
            dl->AddCircleFilled(m, px(17), alpha(col::sheet, 0.35f), 48);
            dl->AddCircle(m, px(17), col::sheet, 48, px(3.f));   // 어느 색 위에서도 보이게 겹선으로
            dl->AddCircle(m, px(17), col::ink, 48, px(1.2f));
        }
    }
    if (hover && inside) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    // 시운전 중임을 시트에 남긴다: 화면 둘레의 겹선
    const float o = px(4);
    if (round) dl->AddCircle((devA + devB) * 0.5f, (devB.x - devA.x) * 0.5f + o, col::ink, 180, 1.f);
    else dl->AddRect(devA - ImVec2(o + 1, o + 1), devB + ImVec2(o + 1, o + 1), col::ink);
}

void drawSimTop(ImVec2 p, ImVec2 sz) {
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
    x += px(20);
    textAt(dl, F.head, 17, ImVec2(x, cy - px(13)), col::ink, "시운전", 4.f);
    x += textW(F.head, 17, "시운전", 4.f) + px(18);
    textAt(dl, F.med, 16, ImVec2(x, cy - px(10)), col::ink2, g.doc.project.c_str());

    float rx = p.x + sz.x - px(18);
    ImGui::SetCursorScreenPos(ImVec2(rx - px(150), p.y + px(8)));
    if (lineButton("도면으로 돌아가기", ImVec2(px(150), sz.y - px(16)), 0, nullptr)) exitSim();
    tooltip("시운전 끝내기", "Esc");
    rx -= px(150) + px(14);
    drawScaleBox(rx, cy);
    hline(dl, p.x, p.x + sz.x, p.y + sz.y - 1, col::ink);
}

void drawSimLeft(ImVec2 p, ImVec2 sz) {
    ImGui::SetCursorScreenPos(p);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild("##simleft", sz, ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetContentRegionAvail().x;
    char b[96];

    snprintf(b, sizeof b, "%d장", (int)g.doc.screens.size());
    sectionHeader("01", "시트", b);
    for (int i = 0; i < (int)g.doc.screens.size(); i++) {
        ImGui::PushID(i);
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        const float h = px(28);
        if (ImGui::InvisibleButton("##sheet", ImVec2(W, h))) jump(i);
        const bool hv = ImGui::IsItemHovered(), on = i == lvhost::simScreen();
        if (hv) { dl->AddRectFilled(rp, rp + ImVec2(W, h), col::hover); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        snprintf(b, sizeof b, "%02d", i + 1);
        textAt(dl, F.mono, 11, rp + ImVec2(px(2), px(8)), on ? col::red : col::ink3, b);
        textAt(dl, on ? F.med : F.ui, 15, rp + ImVec2(px(26), px(5)), on ? col::red : col::ink, ellipsize(F.ui, 15, g.doc.screens[i].name, W - px(60)).c_str());
        if (on) textAt(dl, F.ui, 14, ImVec2(rp.x + W - textW(F.ui, 14, "지금"), rp.y + px(6)), col::red, "지금");
        hline(dl, rp.x, rp.x + W, rp.y + h - 1, on ? col::red : col::hair);
        tooltip("이 시트를 처음 상태로 띄우기");
        ImGui::PopID();
    }
    gap(8);
    if (lineButton("이 시트 처음부터", ImVec2(W, px(32)), 0, "Ctrl R")) jump(lvhost::simScreen());
    gap(22);

    sectionHeader("02", "보기");
    auto row = [&](const char* label, const char* id, bool* v, const char* tip) {
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        textAt(dl, F.ui, 15, rp + ImVec2(0, px(4)), col::ink, label);
        ImGui::SetCursorScreenPos(rp + ImVec2(W - px(34), 0));
        const bool r = toggleField(id, v);
        tooltip(tip);
        ImGui::SetCursorScreenPos(rp + ImVec2(0, px(30)));
        return r;
    };
    if (row("느리게 (¼)", "##slow", &g.simSlow, "애니메이션을 네 배 느리게")) lvhost::simSpeed(g.simSlow ? 0.25f : 1.f);
    row("손가락 자국", "##touch", &g.simTouch, "누르는 동안 손끝이 덮는 자리");
    row("장치 픽셀", "##px", &g.pixelView, "장치에 찍힐 픽셀을 그대로 키워 보기");
    gap(18);

    sectionHeader("03", "손에 익히기");
    const char* keys[][2] = {{"누르기", "버튼·스위치·목록"}, {"끌기", "슬라이더·스크롤"}, {"키보드", "입력창을 누른 뒤"}, {"휠", "척도 바꾸기"},
                             {"Space+끌기", "시트 옮기기"}, {"Ctrl+Tab", "다음 시트"}, {"Esc", "도면으로"}};
    for (auto& k : keys) {
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        textAt(dl, F.mono, 12, rp + ImVec2(0, px(5)), col::ink, k[0]);
        textAt(dl, F.ui, 14, rp + ImVec2(W - textW(F.ui, 14, k[1]), px(3)), col::ink2, k[1]);
        ImGui::Dummy(ImVec2(W, px(26)));
        hline(dl, rp.x, rp.x + W, rp.y + px(26) - 1, col::hair);
    }
    ImGui::EndChild();
}

void drawSimRight(ImVec2 p, ImVec2 sz) {
    ImGui::SetCursorScreenPos(p);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(20), px(16)));
    ImGui::BeginChild("##simright", sz, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto& log = lvhost::simLog();
    char b[160];
    snprintf(b, sizeof b, "%d건", (int)log.size());
    sectionHeader("04", "기록", b);
    if (log.empty()) {
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        textAt(dl, F.ui, 15, rp + ImVec2(0, px(6)), col::ink, "화면을 눌러 보세요.");
        textAt(dl, F.ui, 14, rp + ImVec2(0, px(32)), col::ink2, "어떤 콜백이 불리는지 여기에 적힙니다.");
        ImGui::EndChild();
        return;
    }
    ImGui::BeginChild("##simlog", ImVec2(0, -px(30)), 0, 0);
    dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetContentRegionAvail().x - px(6), rowH = px(48);
    // 새것이 위로
    for (int i = (int)log.size() - 1; i >= 0; i--) {
        const lvhost::SimEvent& e = log[i];
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, rowH));
        if (!ImGui::IsItemVisible()) continue;
        const bool last = i == (int)log.size() - 1;
        const float fresh = last ? 1.f - (float)ImClamp((g.now - g_newT) / 0.6, 0.0, 1.0) : 0.f;
        if (fresh > 0) dl->AddRectFilled(rp - ImVec2(px(4), 0), rp + ImVec2(W, rowH - 1), alpha(col::press, fresh));
        snprintf(b, sizeof b, "%02d:%04.1f", (int)(e.t / 60), fmod(e.t, 60.0));
        textAt(dl, F.mono, 11, rp + ImVec2(0, px(6)), col::ink3, b);
        const float nx = rp.x + px(58);
        std::string val = e.value;
        if (e.count > 1) { snprintf(b, sizeof b, "  ×%d", e.count); val += b; }
        val = ellipsize(F.mono, 12, val, px(110));
        const float vw = val.empty() ? 0.f : textW(F.mono, 12, val.c_str());
        textAt(dl, F.med, 15, ImVec2(nx, rp.y + px(3)), col::ink, ellipsize(F.med, 15, e.name, W - px(58) - vw - px(10)).c_str());
        if (vw > 0) textAt(dl, F.mono, 12, ImVec2(rp.x + W - vw, rp.y + px(6)), col::ink, val.c_str());
        // 둘째 줄: 사건 → 콜백 · 이동
        float x = nx;
        const char* ek = e.code < 0 ? "시간이 되어" : eventKo(e.code);   // 저절로 넘어가기
        textAt(dl, F.ui, 14, ImVec2(x, rp.y + px(25)), col::ink2, ek);
        x += textW(F.ui, 14, ek) + px(8);
        const float room = rp.x + W - x;
        if (!e.cb.empty() || !e.go.empty()) {
            std::string t = e.cb.empty() ? "" : "→ " + e.cb;
            textAt(dl, F.mono, 12, ImVec2(x, rp.y + px(25)), col::ink, ellipsize(F.mono, 12, t, e.go.empty() ? room : room * 0.6f).c_str());
            if (!e.go.empty()) {
                if (!t.empty()) x += textW(F.mono, 12, ellipsize(F.mono, 12, t, room * 0.6f).c_str()) + px(8);
                t = "시트 " + e.go;
                textAt(dl, F.med, 14, ImVec2(x, rp.y + px(25)), col::blueInk, ellipsize(F.med, 14, t, rp.x + W - x).c_str());
            }
        } else textAt(dl, F.ui, 14, ImVec2(x, rp.y + px(25)), col::ink3, "연결한 콜백 없음");
        hline(dl, rp.x, rp.x + W, rp.y + rowH - 1, col::hair);
    }
    ImGui::EndChild();
    gap(4);
    if (textLink("기록 지우기")) { log.clear(); g_seen = 0; }
    ImGui::EndChild();
}

void drawSimStatus(ImVec2 p, ImVec2 sz) {
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
    snprintf(b, sizeof b, "SHT %02d %s", lvhost::simScreen() + 1, g.doc.screens[lvhost::simScreen()].name.c_str());
    textAt(dl, F.ui, 14, ImVec2(x, ty), col::ink2, b);
    const char* hint = lvhost::simWantsText() ? "키보드로 입력하세요 · Esc 도면으로" : "눌러 보세요 · 도면은 바뀌지 않습니다 · Esc 도면으로";
    textAt(dl, F.ui, 14, ImVec2(p.x + sz.x - px(20) - textW(F.ui, 14, hint), ty), col::ink3, hint);
}
