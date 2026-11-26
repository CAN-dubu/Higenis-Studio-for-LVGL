// 도면 캔버스: 자, 시트 테두리, 실제 LVGL 화면, 선택·치수선·스냅·측정, 개정 이력표, 표제란
#include "image.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

const float kScales[] = {0.5f, 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f};
const char* kScaleTxt[] = {"1 : 2", "1 : 1", "3 : 2", "2 : 1", "3 : 1", "4 : 1", "6 : 1", "8 : 1"};
const int kScaleCount = 8;

namespace {
ImVec2 C0, C1;      // 캔버스 영역
ImVec2 FR0, FR1;    // 시트 테두리 안쪽
float Z = 1.f;      // 척도
ImVec2 O;           // 장치 원점(화면 좌표)
float hudAlphaRev = 1.f, hudAlphaTitle = 1.f;
const float kMiniTitle = 30.f, kMiniRev = 26.f, kHudGap = 14.f;   // 접힌 표제란·개정표의 높이와 여백(96dpi)
const float kRevW = 296.f, kTitleW = 318.f;

ImVec2 toScr(float x, float y) { return ImVec2(O.x + x * Z, O.y + y * Z); }
ImVec2 toDev(ImVec2 s) { return ImVec2((s.x - O.x) / Z, (s.y - O.y) / Z); }
void scrRect(const Rect& r, ImVec2& a, ImVec2& b) {
    a = ImFloor(toScr((float)r.x1, (float)r.y1) + ImVec2(0.5f, 0.5f));           // 포함
    b = ImFloor(toScr((float)r.x2 + 1, (float)r.y2 + 1) + ImVec2(0.5f, 0.5f));   // 제외
}
const Rect* rectOf(int id) {
    auto& m = lvhost::coords();
    auto it = m.find(id);
    return it == m.end() ? nullptr : &it->second;
}
bool hiddenChain(const Screen& s, int id) {
    const Widget* w = findWidget(s, id);
    while (w) {
        if (w->hidden) return true;
        w = w->parent ? findWidget(s, w->parent) : nullptr;
    }
    return false;
}
Rect visibleRect(const Screen& s, int id) {
    const Rect* r0 = rectOf(id);
    if (!r0) return Rect{};
    Rect r = *r0;
    const Widget* w = findWidget(s, id);
    while (w && w->parent) {
        if (const Rect* p = rectOf(w->parent)) {
            r.x1 = std::max(r.x1, p->x1); r.y1 = std::max(r.y1, p->y1);
            r.x2 = std::min(r.x2, p->x2); r.y2 = std::min(r.y2, p->y2);
        }
        w = findWidget(s, w->parent);
    }
    return r;
}
int hitTest(ImVec2 d, bool containersOnly) {
    Screen& s = curScreen();
    for (int i = (int)s.widgets.size() - 1; i >= 0; i--) {
        const Widget& w = s.widgets[i];
        if (containersOnly && !typeInfo(w.type).canParent) continue;
        if (hiddenChain(s, w.id)) continue;
        Rect r = visibleRect(s, w.id);
        if (d.x >= r.x1 && d.x < r.x2 + 1 && d.y >= r.y1 && d.y < r.y2 + 1) return w.id;
    }
    return 0;
}

// ── 스냅 ──
struct EdgeCand { float v, a, b; };
void gatherCands(int selfId, int parent, bool xAxis, std::vector<EdgeCand>& out) {
    Screen& s = curScreen();
    auto add = [&](const Rect& r) {
        if (xAxis) {
            float a = (float)r.y1, b = (float)r.y2 + 1;
            out.push_back({(float)r.x1, a, b});
            out.push_back({(r.x1 + r.x2 + 1) * 0.5f, a, b});
            out.push_back({(float)r.x2 + 1, a, b});
        } else {
            float a = (float)r.x1, b = (float)r.x2 + 1;
            out.push_back({(float)r.y1, a, b});
            out.push_back({(r.y1 + r.y2 + 1) * 0.5f, a, b});
            out.push_back({(float)r.y2 + 1, a, b});
        }
    };
    add(lvhost::contentRect(parent));
    for (auto& w : s.widgets)
        if (w.parent == parent && w.id != selfId && !w.hidden)
            if (const Rect* r = rectOf(w.id)) add(*r);
}
bool snapValue(const std::vector<EdgeCand>& c, const float* e, int ne, float& delta) {
    const float thr = px(6) / Z;
    float best = 1e9f;
    for (auto& k : c)
        for (int i = 0; i < ne; i++)
            if (fabsf(k.v - e[i]) < fabsf(best)) best = k.v - e[i];
    if (fabsf(best) <= thr) { delta = best; return true; }
    return false;
}
void addGuides(const std::vector<EdgeCand>& c, const float* e, int ne, bool xAxis, float sa, float sb) {
    for (auto& k : c)
        for (int i = 0; i < ne; i++)
            if (fabsf(k.v - e[i]) < 0.51f) {
                bool dup = false;
                for (auto& gd : g.guides)
                    if (gd.vertical == xAxis && fabsf(gd.pos - k.v) < 0.1f) {
                        gd.a = std::min({gd.a, sa, k.a});
                        gd.b = std::max({gd.b, sb, k.b});
                        dup = true;
                    }
                if (!dup) g.guides.push_back({xAxis, k.v, std::min(sa, k.a), std::max(sb, k.b)});
            }
}
float gridSnap(float v, float origin) { return origin + roundf((v - origin) / g.grid) * g.grid; }

// ── 그리기 도우미 ──
void balloon(ImDrawList* dl, ImVec2 corner, int num, const char* name, ImU32 c, bool withName) {
    float r = px(9.5f);
    ImVec2 cen = corner + ImVec2(-px(16), -px(16));
    cen.x = std::max(cen.x, FR0.x + r + px(2));
    cen.y = std::max(cen.y, FR0.y + r + px(2));
    ImVec2 d = corner - cen;
    float len = sqrtf(d.x * d.x + d.y * d.y);
    if (len > r + 1) dl->AddLine(cen + d * (r / len), corner, c);
    dl->AddCircleFilled(corner, px(1.6f), c);
    dl->AddCircleFilled(cen, r, col::sheet);
    dl->AddCircle(cen, r, c, 24, 1.f);
    char nb[8];
    snprintf(nb, sizeof nb, "%02d", num);
    float tw = textW(F.mono, 11, nb);
    textAt(dl, F.mono, 11, ImVec2(cen.x - tw * 0.5f, cen.y - px(7)), c, nb);
    if (withName && name) {
        float nw = textW(F.med, 13, name);
        ImVec2 tp(cen.x + r + px(5), cen.y - px(9));
        dl->AddRectFilled(tp - ImVec2(px(3), 0), tp + ImVec2(nw + px(3), px(18)), alpha(col::sheet, 0.92f));
        textAt(dl, F.med, 13, tp, c, name);
    }
}

void hatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c) {
    dl->PushClipRect(a, b, true);
    float step = px(7);
    for (float x = a.x - (b.y - a.y); x < b.x; x += step) dl->AddLine(ImVec2(x, b.y), ImVec2(x + (b.y - a.y), a.y), c);
    dl->PopClipRect();
    dl->AddRect(a, b, c);
}

void drawRulers(ImDrawList* dl) {
    const float RB = FR0.x - C0.x;
    ImVec2 m = ImGui::GetIO().MousePos;
    int minor = 1;
    for (int k : {1, 2, 4, 8, 16, 32, 64, 128})
        if (k * Z >= px(6)) { minor = k; break; }
    // 선택 범위 띠
    if (const Rect* r = g.sel ? rectOf(g.sel) : nullptr) {
        ImVec2 a = toScr((float)r->x1, (float)r->y1), b = toScr((float)r->x2 + 1, (float)r->y2 + 1);
        float x0 = std::max(a.x, FR0.x), x1 = std::min(b.x, FR1.x);
        if (x1 > x0) dl->AddRectFilled(ImVec2(x0, FR0.y - px(5)), ImVec2(x1, FR0.y), col::redWash);
        float y0 = std::max(a.y, FR0.y), y1 = std::min(b.y, FR1.y);
        if (y1 > y0) dl->AddRectFilled(ImVec2(FR0.x - px(5), y0), ImVec2(FR0.x, y1), col::redWash);
    }
    // 위 자
    float d0 = floorf(toDev(FR0).x / minor) * minor, d1 = toDev(FR1).x;
    for (float v = d0; v <= d1; v += minor) {
        float sx = floorf(O.x + v * Z) + 0.5f;
        if (sx < FR0.x || sx > FR1.x) continue;
        int k = (int)lroundf(v / minor);
        float len = (k % 8 == 0) ? px(9) : (k % 4 == 0) ? px(6) : px(3);
        vline(dl, sx, FR0.y - len, FR0.y - 1, k % 8 == 0 ? col::ink2 : col::hair2);
        if (k % 8 == 0) {
            char b[16];
            snprintf(b, sizeof b, "%d", (int)v);
            if (sx + px(3) + textW(F.mono, 11, b) < FR1.x) textAt(dl, F.mono, 11, ImVec2(sx + px(3), C0.y + px(1)), col::ink3, b);
        }
    }
    // 왼쪽 자 (숫자는 세워서)
    d0 = floorf(toDev(FR0).y / minor) * minor;
    d1 = toDev(FR1).y;
    for (float v = d0; v <= d1; v += minor) {
        float sy = floorf(O.y + v * Z) + 0.5f;
        if (sy < FR0.y || sy > FR1.y) continue;
        int k = (int)lroundf(v / minor);
        float len = (k % 8 == 0) ? px(9) : (k % 4 == 0) ? px(6) : px(3);
        hline(dl, FR0.x - len, FR0.x - 1, sy, k % 8 == 0 ? col::ink2 : col::hair2);
        if (k % 8 == 0) {
            char b[16];
            snprintf(b, sizeof b, "%d", (int)v);
            float tw = textW(F.mono, 11, b);
            if (sy - px(3) - tw > FR0.y + px(2)) textRot(dl, F.mono, 11, ImVec2(C0.x + px(7), sy - px(3) - tw * 0.5f), col::ink3, b, -1.5707963f);
        }
    }
    // 커서 위치 표시
    if (m.x > FR0.x && m.x < FR1.x && m.y > FR0.y && m.y < FR1.y) {
        float s = px(4);
        dl->AddTriangleFilled(ImVec2(m.x, FR0.y), ImVec2(m.x - s, FR0.y - s * 1.4f), ImVec2(m.x + s, FR0.y - s * 1.4f), col::red);
        dl->AddTriangleFilled(ImVec2(FR0.x, m.y), ImVec2(FR0.x - s * 1.4f, m.y - s), ImVec2(FR0.x - s * 1.4f, m.y + s), col::red);
    }
    // 구역 표시: 커서가 있는 구역을 빨갛게
    const bool inFrame = m.x > FR0.x && m.x < FR1.x && m.y > FR0.y && m.y < FR1.y;
    const int NC = 6, NR = 4;
    for (int i = 0; i < NC; i++) {
        float x0 = FR0.x + (FR1.x - FR0.x) * i / NC, x1 = FR0.x + (FR1.x - FR0.x) * (i + 1) / NC;
        char b[4] = {(char)('1' + i), 0};
        bool on = inFrame && m.x >= x0 && m.x < x1;
        textAt(dl, F.mono, 11, ImVec2((x0 + x1) * 0.5f - textW(F.mono, 11, b) * 0.5f, FR1.y + px(3)), on ? col::red : col::ink3, b);
        if (i) vline(dl, x0, FR1.y + 1, FR1.y + px(8), col::hair2);
    }
    for (int i = 0; i < NR; i++) {
        float y0 = FR0.y + (FR1.y - FR0.y) * i / NR, y1 = FR0.y + (FR1.y - FR0.y) * (i + 1) / NR;
        char b[4] = {(char)('A' + i), 0};
        bool on = inFrame && m.y >= y0 && m.y < y1;
        textAt(dl, F.mono, 11, ImVec2(FR1.x + px(6), (y0 + y1) * 0.5f - px(7)), on ? col::red : col::ink3, b);
        if (i) hline(dl, FR1.x + 1, FR1.x + px(8), y0, col::hair2);
    }
    // 모서리 칸: 단위
    float tw = textW(F.mono, 11, "px");
    textAt(dl, F.mono, 11, ImVec2(C0.x + (RB - tw) * 0.5f, C0.y + px(4)), col::ink3, "px");
}

// ── 개정 이력표 (오른쪽 위) ──
void drawRevTable(ImDrawList* dl, ImVec2 p, float W, int rows) {
    const float rowH = px(22), headH = px(25);
    int n = (int)g.hist.revs.size();
    if (rows == 0) {   // 접힘: 지금 개정 한 줄만
        float a = hudAlphaRev;
        ImVec2 q = p + ImVec2(W, px(26));
        dl->AddRectFilled(p, q, alpha(col::sheet, a));
        dl->AddRect(p, q, alpha(col::ink, a));
        ImGui::SetCursorScreenPos(p);
        if (ImGui::InvisibleButton("##revmini", q - p)) g.openRevs = true;
        bool hv = ImGui::IsItemHovered();
        if (hv) { dl->AddRectFilled(p + ImVec2(1, 1), q - ImVec2(1, 1), alpha(col::hover, a)); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        tooltip("개정 이력 전체 보기");
        ImVec2 t0(p.x + px(14), p.y + px(14));
        float s = px(5);
        dl->AddTriangle(ImVec2(t0.x, t0.y - s), ImVec2(t0.x - s, t0.y + s * 0.75f), ImVec2(t0.x + s, t0.y + s * 0.75f), alpha(col::red, a), 1.2f);
        std::string lab = g.hist.label(g.hist.cur);
        textAt(dl, F.mono, 12, p + ImVec2(px(26), px(6)), alpha(col::red, a), lab.c_str());
        float x = px(26) + textW(F.mono, 12, lab.c_str()) + px(12);
        char cnt[32];
        snprintf(cnt, sizeof cnt, "전체 %d", n);
        float cw = textW(F.ui, 13, cnt);
        if (g.hist.cur >= 0) textAt(dl, F.ui, 14, p + ImVec2(x, px(4)), alpha(col::ink, a), ellipsize(F.ui, 14, g.hist.revs[g.hist.cur].note, W - x - cw - px(20)).c_str());
        textAt(dl, F.ui, 13, ImVec2(q.x - cw - px(8), p.y + px(5)), alpha(col::ink2, a), cnt);
        return;
    }
    int show = std::min(rows, n);
    int first = std::max(0, std::min(n - show, g.hist.cur - show + 2));
    float H = headH + rowH * (show + 1);
    ImVec2 q = p + ImVec2(W, H);
    float a = hudAlphaRev;
    dl->AddRectFilled(p, q, alpha(col::sheet, a));
    dl->AddRect(p, q, alpha(col::ink, a));
    textAt(dl, F.head, 13, p + ImVec2(px(8), px(2)), alpha(col::ink, a), "개정 이력", 2.f);
    char cnt[32];
    snprintf(cnt, sizeof cnt, "전체 %d", n);
    float cw = textW(F.ui, 13, cnt);
    ImGui::SetCursorScreenPos(ImVec2(q.x - cw - px(8), p.y + px(4)));
    {
        ImVec2 lp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##allrevs", ImVec2(cw, px(18)))) g.openRevs = true;
        bool hv = ImGui::IsItemHovered();
        textAt(dl, F.ui, 13, lp, alpha(hv ? col::red : col::ink2, a), cnt);
        if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    float y = p.y + headH;
    hline(dl, p.x, q.x, y, alpha(col::ink, a));
    const float c1 = px(44), c3 = px(96);
    // 열 머리
    textAt(dl, F.ui, 12, ImVec2(p.x + px(8), y + px(3)), alpha(col::ink3, a), "기호");
    textAt(dl, F.ui, 12, ImVec2(p.x + c1 + px(6), y + px(3)), alpha(col::ink3, a), "내용");
    textAt(dl, F.ui, 12, ImVec2(q.x - c3 + px(6), y + px(3)), alpha(col::ink3, a), "시각");
    y += rowH;
    hline(dl, p.x, q.x, y, alpha(col::hair2, a));
    for (int i = first; i < first + show; i++) {
        const Revision& r = g.hist.revs[i];
        ImGui::SetCursorScreenPos(ImVec2(p.x, y));
        ImGui::PushID(i);
        bool click = ImGui::InvisibleButton("##rev", ImVec2(W, rowH));
        bool hv = ImGui::IsItemHovered();
        ImGui::PopID();
        if (click) gotoRevision(i);
        bool cur = i == g.hist.cur, future = i > g.hist.cur;
        if (hv) { dl->AddRectFilled(ImVec2(p.x + 1, y), ImVec2(q.x - 1, y + rowH), alpha(col::hover, a)); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
        ImU32 tc = alpha(future ? col::ink3 : cur ? col::ink : col::ink2, a);
        std::string lab = g.hist.label(i);
        if (cur) {
            // 개정 삼각형
            ImVec2 tc0(p.x + px(12), y + rowH * 0.5f + px(1));
            float s = px(5);
            dl->AddTriangle(ImVec2(tc0.x, tc0.y - s), ImVec2(tc0.x - s, tc0.y + s * 0.75f), ImVec2(tc0.x + s, tc0.y + s * 0.75f), alpha(col::red, a), 1.2f);
            textAt(dl, F.mono, 12, ImVec2(p.x + px(22), y + px(3)), alpha(col::red, a), lab.c_str());
        } else {
            textAt(dl, F.mono, 12, ImVec2(p.x + px(22), y + px(3)), tc, lab.c_str());
        }
        // 내용(잘라서)
        std::string note = ellipsize(F.ui, 14, r.note, W - c1 - c3 - px(10));
        // 새 개정은 왼쪽에서 오른쪽으로 써지고, 빨간 글씨가 흑연으로 마른다
        float wt = (cur && i == n - 1) ? (float)ImClamp((g.now - g.revAnimT) / 0.45, 0.0, 1.0) : 1.f;
        ImU32 nc = wt < 1.f ? alpha(col::red, a) : tc;
        dl->PushClipRect(ImVec2(p.x + c1, y), ImVec2(p.x + c1 + (W - c1 - c3) * easeOut(wt), y + rowH), true);
        textAt(dl, F.ui, 14, ImVec2(p.x + c1 + px(6), y + px(2)), nc, note.c_str());
        dl->PopClipRect();
        textAt(dl, F.mono, 11, ImVec2(q.x - c3 + px(6), y + px(4)), alpha(col::ink3, a), r.time.c_str());
        if (hv && !cur) tooltip(future ? "이 개정으로 다시 진행" : "이 개정으로 되돌아가기");
        y += rowH;
        if (i < first + show - 1) hline(dl, p.x + 1, q.x - 1, y, alpha(col::hair, a));
    }
    vline(dl, p.x + c1, p.y + headH + 1, q.y - 1, alpha(col::hair2, a));
    vline(dl, q.x - c3, p.y + headH + 1, q.y - 1, alpha(col::hair2, a));
}

// ── 표제란 (오른쪽 아래) ──
void drawTitleBlock(ImDrawList* dl, ImVec2 p, float W, bool mini) {
    const float rh1 = px(38), rh2 = px(36), rh3 = px(40);
    float H = mini ? px(kMiniTitle) : rh1 + rh2 + rh3;
    ImVec2 q = p + ImVec2(W, H);
    float a = hudAlphaTitle;
    bool flash = g.now - g.flashSheetT < 0.6;
    dl->AddRectFilled(p, q, alpha(col::sheet, a));
    dl->AddRect(p, q, alpha(col::ink, a));
    char res[32], sht[32];
    snprintf(res, sizeof res, "%d × %d", g.doc.resW, g.doc.resH);
    snprintf(sht, sizeof sht, "%02d / %02d", g.screen + 1, (int)g.doc.screens.size());
    std::string rev = g.hist.label(g.hist.cur) + "  " + (g.hist.cur >= 0 ? g.hist.revs[g.hist.cur].time : "");
    int errors = 0, advice = 0;
    for (auto& is : g.issues) (is.error ? errors : advice)++;

    auto cell = [&](ImVec2 a0, ImVec2 a1, const char* label, const char* value, ImFont* f, float fs, int action, const char* tip) {
        ImGui::SetCursorScreenPos(a0);
        ImGui::PushID(label);
        bool click = ImGui::InvisibleButton("##cell", a1 - a0);
        bool hv = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hv) {
            dl->AddRectFilled(a0 + ImVec2(1, 1), a1 - ImVec2(1, 1), alpha(col::hover, a));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            tooltip(tip);
        }
        if (mini) {   // 한 줄: 이름표와 값을 나란히
            const float mid = (a0.y + a1.y) * 0.5f;
            textAt(dl, F.ui, 12, ImVec2(a0.x + px(8), mid - px(8)), alpha(col::ink2, a), label);
            if (value) {
                const float vx = a0.x + px(8) + textW(F.ui, 12, label) + px(8);
                const std::string v = ellipsize(f, fs, value, a1.x - px(6) - vx);
                textAt(dl, f, fs, ImVec2(vx, mid - px(fs) * 0.66f), alpha(col::ink, a), v.c_str());
            }
        } else {
            textAt(dl, F.ui, 12, a0 + ImVec2(px(7), px(1)), alpha(col::ink2, a), label);
            if (value) textAt(dl, f, fs, a0 + ImVec2(px(7), px(16)), alpha(col::ink, a), value);
        }
        if (click) {
            if (action == 0) { g.sel = 0; g.flashSheetT = g.now; g.needsBuild = true; }
            if (action == 1) fitView();
            if (action == 2) { g.openLint = true; g.lintAnchor = ImVec2(a1.x, a0.y); }
            if (action == 3) g.openRevs = true;
        }
    };
    if (mini) {   // 접힘: 도면·척도·검도만
        float a1 = floorf(p.x + W * 0.50f), a2 = floorf(p.x + W * 0.76f);
        cell(p, ImVec2(a1, q.y), "도면", g.doc.project.c_str(), F.med, 16, 0, "도면 설정 열기");
        cell(ImVec2(a1, p.y), ImVec2(a2, q.y), "척도", kScaleTxt[g.scaleIdx], F.mono, 13, 1, "화면에 맞추기 · 0");
        cell(ImVec2(a2, p.y), q, "검도", nullptr, F.mono, 13, 2, "검도 결과 보기");
        if (errors == 0) stamp(dl, ImVec2(q.x - px(20), p.y + H * 0.5f), px(11), "검", nullptr, -0.2f, a);
        else {
            char b[24];
            snprintf(b, sizeof b, "오류 %d", errors);
            textAt(dl, F.med, 15, ImVec2(q.x - px(8) - textW(F.med, 15, b), p.y + px(5)), alpha(col::red, a), b);
        }
        vline(dl, a1, p.y + 1, q.y - 1, alpha(col::hair2, a));
        vline(dl, a2, p.y + 1, q.y - 1, alpha(col::hair2, a));
        if (flash) dl->AddRect(p - ImVec2(px(3), px(3)), q + ImVec2(px(3), px(3)), alpha(col::red, (float)(1 - (g.now - g.flashSheetT) / 0.6) * a), 0, 1.5f);
        return;
    }
    float y1 = p.y + rh1, y2 = y1 + rh2;
    float cA = p.x + W * 0.62f;
    cell(p, ImVec2(cA, y1), "도면", g.doc.project.c_str(), F.med, 17, 0, "도면 설정 열기");
    cell(ImVec2(cA, p.y), ImVec2(q.x, y1), "시트", sht, F.mono, 14, 0, "도면 설정 열기");
    float c2a = p.x + W * 0.36f, c2b = p.x + W * 0.68f;
    cell(ImVec2(p.x, y1), ImVec2(c2a, y2), "해상도", res, F.mono, 13, 0, "도면 설정 열기");
    cell(ImVec2(c2a, y1), ImVec2(c2b, y2), "색", g.doc.depth == 16 ? "RGB565" : "XRGB8888", F.mono, 13, 0, "도면 설정 열기");
    cell(ImVec2(c2b, y1), ImVec2(q.x, y2), "척도", kScaleTxt[g.scaleIdx], F.mono, 13, 1, "화면에 맞추기 · 0");
    float c3a = p.x + W * 0.68f;
    cell(ImVec2(p.x, y2), ImVec2(c3a, q.y), "개정", rev.c_str(), F.mono, 13, 3, "개정 이력 전체 보기");
    cell(ImVec2(c3a, y2), q, "검도", nullptr, F.mono, 13, 2, "검도 결과 보기");
    // 검도 칸: 오류가 없으면 도장
    ImVec2 sc(q.x - px(24), y2 + rh3 * 0.5f);
    if (errors == 0) {
        stamp(dl, sc, px(15), "검", nullptr, -0.2f, a);
        if (advice) {
            char b[16];
            snprintf(b, sizeof b, "권고 %d", advice);
            textAt(dl, F.ui, 12, ImVec2(c3a + px(7), y2 + px(20)), alpha(col::ink2, a), b);
        }
    } else {
        char b[16];
        snprintf(b, sizeof b, "%d", errors);
        textAt(dl, F.mono, 17, ImVec2(c3a + px(7), y2 + px(15)), alpha(col::red, a), b);
        textAt(dl, F.ui, 13, ImVec2(c3a + px(7) + textW(F.mono, 17, b) + px(5), y2 + px(19)), alpha(col::red, a), "오류");
    }
    // 칸 나눔 선
    hline(dl, p.x, q.x, y1, alpha(col::ink, a));
    hline(dl, p.x, q.x, y2, alpha(col::ink, a));
    vline(dl, cA, p.y + 1, y1, alpha(col::hair2, a));
    vline(dl, c2a, y1 + 1, y2, alpha(col::hair2, a));
    vline(dl, c2b, y1 + 1, y2, alpha(col::hair2, a));
    vline(dl, c3a, y2 + 1, q.y - 1, alpha(col::hair2, a));
    if (flash) {
        float t = (float)((g.now - g.flashSheetT) / 0.6);
        dl->AddRect(p - ImVec2(px(3), px(3)), q + ImVec2(px(3), px(3)), alpha(col::red, (1 - t) * a), 0, 1.5f);
    }
}

bool rectsOverlap(ImVec2 a0, ImVec2 a1, ImVec2 b0, ImVec2 b1) {
    return a0.x < b1.x && b0.x < a1.x && a0.y < b1.y && b0.y < a1.y;
}
}  // namespace

// ───────────────────────── 보기 ─────────────────────────
void fitView() {
    ImVec2 sz = C1 - C0;
    if (sz.x <= 0) { g.needFit = true; return; }
    // 시트 테두리 안을 되도록 가득 쓰되, 접은 표제란·개정표와 시트 이름표 자리는 남긴다
    const ImVec2 fr = sz - ImVec2(px(42), px(42));
    const float edge = px(7);
    float top = 0, slack = 0;
    int idx = 0;
    for (int i = kScaleCount - 1; i >= 0; i--) {
        const float w = g.doc.resW * kScales[i], h = g.doc.resH * kScales[i];
        const bool under = (fr.x + w) * 0.5f > fr.x - px(kHudGap + kTitleW) - px(8);   // 오른쪽 구석까지 닿는가
        top = under ? px(kHudGap + kMiniRev) + edge : px(40);
        const float bottom = under ? px(kHudGap + kMiniTitle) + edge : px(30);
        slack = fr.y - h - top - bottom;
        idx = i;
        if (w <= fr.x - px(48) && slack >= 0) break;
    }
    slack = std::max(0.f, slack);
    g.scaleIdx = idx;
    g.zoom = g.zoomTarget = kScales[idx];
    ImVec2 dev(g.doc.resW * g.zoom, g.doc.resH * g.zoom);
    g.org = ImFloor(ImVec2(px(22) + (fr.x - dev.x) * 0.5f, px(22) + top + slack * 0.48f));
    g.needFit = false;
}

ImVec2 canvasDevToScreen(float x, float y) { return toScr(x, y); }
ImVec2 canvasCenter() { return (C0 + C1) * 0.5f; }
ImVec2 canvasTitleLeft() { return ImVec2(FR1.x - px(kHudGap + kTitleW), FR1.y); }
bool canvasMouseDev(ImVec2& out) {
    ImVec2 m = ImGui::GetIO().MousePos;
    if (m.x < FR0.x || m.y < FR0.y || m.x > FR1.x || m.y > FR1.y) return false;
    out = toDev(m);
    return true;
}

void setScale(int idx, ImVec2 anchor) {
    idx = ImClamp(idx, 0, kScaleCount - 1);
    g.scaleIdx = idx;
    g.zoomTarget = kScales[idx];
    g.zoomAnchor = anchor - C0;
}

// ───────────────────────── 캔버스 ─────────────────────────
void drawCanvas(ImVec2 pos, ImVec2 size) {
    ImGuiIO& io = ImGui::GetIO();
    C0 = pos;
    C1 = pos + size;
    const float RB = px(22);
    FR0 = ImFloor(C0 + ImVec2(RB, RB));
    FR1 = ImFloor(C1 - ImVec2(px(20), px(20)));

    // 창 크기가 바뀌면 화면 중심 유지
    if (g.lastCanvas.x > 0 && (g.lastCanvas.x != size.x || g.lastCanvas.y != size.y))
        g.org += (size - g.lastCanvas) * 0.5f;
    g.lastCanvas = size;
    if (g.needFit) fitView();

    // 부드러운 척도 변경 (기준점 고정)
    if (fabsf(g.zoom - g.zoomTarget) > 1e-4f) {
        float nz = g.zoom + (g.zoomTarget - g.zoom) * std::min(1.f, g.dt * 18.f);
        if (fabsf(nz - g.zoomTarget) < 0.002f) nz = g.zoomTarget;
        ImVec2 dev = (g.zoomAnchor - g.org) / g.zoom;
        g.org = g.zoomAnchor - dev * nz;
        g.zoom = nz;
        if (nz == g.zoomTarget) g.org = ImFloor(g.org + ImVec2(0.5f, 0.5f));
    }
    Z = g.zoom;
    O = C0 + g.org;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    Screen& s = curScreen();
    ImVec2 m = io.MousePos;
    ImVec2 md = toDev(m);

    // HUD 위치
    const float revW = px(kRevW), tbW = px(kTitleW);
    ImVec2 revP(FR1.x - px(14) - revW, FR0.y + px(14));
    const ImVec2 tbP0 = ImFloor(ImVec2(FR1.x - px(14) - tbW, FR1.y - px(14) - px(114)));
    revP = ImFloor(revP);
    // 장치 화면을 가리면 줄 수를 줄이고, 그래도 가리면 한 줄로 접는다
    const ImVec2 dv0 = C0 + g.org, dv1 = dv0 + ImVec2(g.doc.resW * g.zoom, g.doc.resH * g.zoom);
    int revRows = std::min(5, (int)g.hist.revs.size());
    if (dv1.x > revP.x - px(8)) {
        float room = dv0.y - px(34) - revP.y;   // 시트 이름표까지 비워 둔다
        revRows = std::min(revRows, (int)floorf((room - px(25)) / px(22)) - 1);
        if (revRows < 1) revRows = 0;
    }
    float revH = revRows ? px(25) + px(22) * (revRows + 1) : px(26);
    const bool tbMini = dv1.x > tbP0.x - px(8) && dv1.y > tbP0.y - px(24);
    const float tbH = tbMini ? px(kMiniTitle) : px(114);
    const ImVec2 tbP(tbP0.x, FR1.y - px(14) - tbH);
    auto inHud = [&](ImVec2 p) {
        if (g.sim) return false;
        return (p.x >= revP.x && p.x <= revP.x + revW && p.y >= revP.y && p.y <= revP.y + revH && hudAlphaRev > 0.5f) ||
               (p.x >= tbP.x && p.x <= tbP.x + tbW && p.y >= tbP.y && p.y <= tbP.y + tbH && hudAlphaTitle > 0.5f);
    };

    // 입력 영역
    ImGui::SetCursorScreenPos(C0);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHover = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                             m.x > FR0.x && m.y > FR0.y && m.x < FR1.x && m.y < FR1.y && !inHud(m);
    const bool space = ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput && !lvhost::simWantsText();
    const bool alt = !g.sim && (io.KeyAlt || g.fakeAlt);

    // 휠: 척도
    if (canvasHover && io.MouseWheel != 0 && g.drag == Drag::None)
        setScale(g.scaleIdx + (io.MouseWheel > 0 ? 1 : -1), m);

    // 호버
    g.hover = (canvasHover && g.drag == Drag::None && !g.sim) ? hitTest(md, false) : 0;

    // 선택 손잡이
    int handleHover = -1;
    ImVec2 sa, sb;
    const Rect* selR = g.sel ? rectOf(g.sel) : nullptr;
    Widget* selW = selWidget();
    if (selR) scrRect(*selR, sa, sb);
    ImVec2 handles[8];
    if (selR) {
        ImVec2 c = (sa + sb) * 0.5f;
        handles[0] = sa; handles[1] = ImVec2(c.x, sa.y); handles[2] = ImVec2(sb.x, sa.y); handles[3] = ImVec2(sb.x, c.y);
        handles[4] = sb; handles[5] = ImVec2(c.x, sb.y); handles[6] = ImVec2(sa.x, sb.y); handles[7] = ImVec2(sa.x, c.y);
        if (canvasHover && g.drag == Drag::None && g.armed < 0)
            for (int i = 0; i < 8; i++) {
                bool edge = i % 2 == 1;
                if (edge && ((i == 1 || i == 5) ? (sb.x - sa.x) < px(26) : (sb.y - sa.y) < px(26))) continue;
                if (fabsf(m.x - handles[i].x) <= px(5) && fabsf(m.y - handles[i].y) <= px(5)) handleHover = i;
            }
    }
    auto handleCursor = [](int h) {
        switch (h) {
        case 0: case 4: return ImGuiMouseCursor_ResizeNWSE;
        case 2: case 6: return ImGuiMouseCursor_ResizeNESW;
        case 1: case 5: return ImGuiMouseCursor_ResizeNS;
        default: return ImGuiMouseCursor_ResizeEW;
        }
    };

    // ── 마우스 누름 ──
    if (canvasHover && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (space && ImGui::IsMouseClicked(0)))) {
        g.drag = Drag::Pan;
    } else if (canvasHover && !g.sim && ImGui::IsMouseClicked(0) && g.drag == Drag::None) {
        if (g.armed >= 0) {
            g.drag = Drag::Create;
            ImVec2 p = ImFloor(md + ImVec2(0.5f, 0.5f));
            if (g.snapGrid && !io.KeyCtrl) p = ImVec2(gridSnap(p.x, 0), gridSnap(p.y, 0));
            g.create0 = p;
        } else if (handleHover >= 0 && selW) {
            g.drag = Drag::Resize;
            g.handle = handleHover;
            g.mouse0 = m; g.w0 = *selW; g.r0 = *selR; g.cont0 = lvhost::contentRect(selW->parent);
            g.moved = false;
        } else {
            int hit = hitTest(md, false);
            if (hit != g.sel) selectWidget(hit);
            if (hit) {
                Widget* w = selWidget();
                const Rect* r = rectOf(hit);
                if (w && r) {
                    g.drag = Drag::Move;
                    g.mouse0 = m; g.w0 = *w; g.r0 = *r; g.cont0 = lvhost::contentRect(w->parent);
                    g.moved = false;
                }
            }
        }
    }
    if (canvasHover && ImGui::IsMouseDoubleClicked(0) && g.sel && g.armed < 0) g.focusName = g.sel;
    if (canvasHover && !g.sim && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && g.drag == Drag::None) {
        int hit = hitTest(md, false);
        if (hit != g.sel) selectWidget(hit);
        if (hit) ImGui::OpenPopup("##ctx");
    }

    // ── 끌기 ──
    g.guides.clear();
    if (g.drag != Drag::None && ImGui::IsKeyPressed(ImGuiKey_Escape)) {   // 끌던 것을 취소
        if ((g.drag == Drag::Move || g.drag == Drag::Resize) && selW && selW->id == g.w0.id) { *selW = g.w0; g.needsBuild = true; }
        g.drag = Drag::None;
        g.tplType = -1;
    }
    if (g.drag == Drag::Pan) {
        g.org += io.MouseDelta;
        O = C0 + g.org;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(0)) {
            g.drag = Drag::None;
            g.org = ImFloor(g.org + ImVec2(0.5f, 0.5f));
        }
    } else if (g.drag == Drag::Move && selW) {
        ImVec2 dd = (m - g.mouse0) / Z;
        if (!g.moved && ImLengthSqr(m - g.mouse0) > px(3) * px(3)) g.moved = true;
        if (g.moved) {
            bool lockX = false, lockY = false;
            if (io.KeyShift) { if (fabsf(dd.x) > fabsf(dd.y)) { dd.y = 0; lockY = true; } else { dd.x = 0; lockX = true; } }
            float w = (float)rw(g.r0), h = (float)rh(g.r0);
            float nx = g.r0.x1 + dd.x, ny = g.r0.y1 + dd.y;
            std::vector<EdgeCand> cx, cy;
            if (!io.KeyCtrl) {
                gatherCands(selW->id, selW->parent, true, cx);
                gatherCands(selW->id, selW->parent, false, cy);
                float d;
                float ex[3] = {nx, nx + w * 0.5f, nx + w};
                if (!lockX) { if (snapValue(cx, ex, 3, d)) nx += d; else if (g.snapGrid) nx = gridSnap(nx, (float)g.cont0.x1); }
                float ey[3] = {ny, ny + h * 0.5f, ny + h};
                if (!lockY) { if (snapValue(cy, ey, 3, d)) ny += d; else if (g.snapGrid) ny = gridSnap(ny, (float)g.cont0.y1); }
            }
            nx = roundf(nx); ny = roundf(ny);
            if (!io.KeyCtrl) {
                float fx[3] = {nx, nx + w * 0.5f, nx + w}, fy[3] = {ny, ny + h * 0.5f, ny + h};
                addGuides(cx, fx, 3, true, ny, ny + h);
                addGuides(cy, fy, 3, false, nx, nx + w);
            }
            int nxI = g.w0.x + ((int)nx - g.r0.x1), nyI = g.w0.y + ((int)ny - g.r0.y1);
            if (nxI != selW->x || nyI != selW->y) { selW->x = nxI; selW->y = nyI; g.needsBuild = true; }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (!ImGui::IsMouseDown(0)) {
            if (g.moved && (selW->x != g.w0.x || selW->y != g.w0.y)) {
                char b[96];
                snprintf(b, sizeof b, "%s 이동 (%+d, %+d)", selW->name.c_str(), selW->x - g.w0.x, selW->y - g.w0.y);
                commit(b);
            }
            g.drag = Drag::None;
        }
    } else if (g.drag == Drag::Resize && selW) {
        ImVec2 dd = (m - g.mouse0) / Z;
        int h = g.handle;
        bool mL = h == 0 || h == 6 || h == 7, mR = h == 2 || h == 3 || h == 4;
        bool mT = h == 0 || h == 1 || h == 2, mB = h == 4 || h == 5 || h == 6;
        float L = (float)g.r0.x1, T = (float)g.r0.y1, R = (float)g.r0.x2 + 1, B = (float)g.r0.y2 + 1;
        if (mL) L += dd.x;
        if (mR) R += dd.x;
        if (mT) T += dd.y;
        if (mB) B += dd.y;
        if (io.KeyShift && h % 2 == 0) {
            float ar = (float)rw(g.r0) / std::max(1, rh(g.r0));
            float nw = R - L, nh = B - T;
            if (fabsf(nw / ar) > fabsf(nh)) nh = nw / ar; else nw = nh * ar;
            if (mL) L = R - nw; else R = L + nw;
            if (mT) T = B - nh; else B = T + nh;
        }
        std::vector<EdgeCand> cx, cy;
        if (!io.KeyCtrl) {
            gatherCands(selW->id, selW->parent, true, cx);
            gatherCands(selW->id, selW->parent, false, cy);
            float d;
            auto snapE = [&](float& e, std::vector<EdgeCand>& c, float origin) {
                if (snapValue(c, &e, 1, d)) e += d;
                else if (g.snapGrid) e = gridSnap(e, origin);
            };
            if (mL) snapE(L, cx, (float)g.cont0.x1);
            if (mR) snapE(R, cx, (float)g.cont0.x1);
            if (mT) snapE(T, cy, (float)g.cont0.y1);
            if (mB) snapE(B, cy, (float)g.cont0.y1);
        }
        L = roundf(L); R = roundf(R); T = roundf(T); B = roundf(B);
        if (R - L < 4) { if (mL) L = R - 4; else R = L + 4; }
        if (B - T < 4) { if (mT) T = B - 4; else B = T + 4; }
        if (!io.KeyCtrl) {
            float ex[2] = {mL ? L : R, mR ? R : L};
            float ey[2] = {mT ? T : B, mB ? B : T};
            addGuides(cx, ex, (mL || mR) ? 1 : 0, true, T, B);
            addGuides(cy, ey, (mT || mB) ? 1 : 0, false, L, R);
        }
        int nw = (int)(R - L), nh = (int)(B - T);
        int nx = (int)L - g.cont0.x1 - alignAnchor(selW->align, false, rw(g.cont0), nw);
        int ny = (int)T - g.cont0.y1 - alignAnchor(selW->align, true, rh(g.cont0), nh);
        if (nw != selW->w || nh != selW->h || nx != selW->x || ny != selW->y || selW->autoSize) {
            selW->w = nw; selW->h = nh; selW->x = nx; selW->y = ny; selW->autoSize = false;
            g.needsBuild = true;
        }
        ImGui::SetMouseCursor(handleCursor(h));
        if (!ImGui::IsMouseDown(0)) {
            if (selW->w != rw(g.r0) || selW->h != rh(g.r0) || selW->x != g.w0.x || selW->y != g.w0.y) {
                char b[96];
                snprintf(b, sizeof b, "%s 크기 %d×%d", selW->name.c_str(), selW->w, selW->h);
                commit(b);
            }
            g.drag = Drag::None;
        }
    } else if (g.drag == Drag::Create) {
        if (!ImGui::IsMouseDown(0)) {
            ImVec2 p1 = ImFloor(md + ImVec2(0.5f, 0.5f));
            if (g.snapGrid && !io.KeyCtrl) p1 = ImVec2(gridSnap(p1.x, 0), gridSnap(p1.y, 0));
            WType t = (WType)g.armed;
            const TypeInfo& ti = typeInfo(t);
            float L = std::min(g.create0.x, p1.x), T = std::min(g.create0.y, p1.y);
            float R = std::max(g.create0.x, p1.x), B = std::max(g.create0.y, p1.y);
            bool clickOnly = (R - L) < 4 && (B - T) < 4;
            if (clickOnly) { L = g.create0.x; T = g.create0.y; R = L + ti.defW; B = T + ti.defH; }
            int parent = hitTest(ImVec2(L, T), true);
            Rect cont = lvhost::contentRect(parent);
            int id = addWidget(g.doc, s, t, parent, (int)L - cont.x1, (int)T - cont.y1, (int)(R - L), (int)(B - T));
            if (!clickOnly) findWidget(s, id)->autoSize = false;
            selectWidget(id);
            commit(findWidget(s, id)->name + " 추가");
            if (!io.KeyShift) g.armed = -1;
            g.drag = Drag::None;
        }
    }
    // 템플릿에서 끌어 놓기
    if (g.drag == Drag::Template && !ImGui::IsMouseDown(0)) {
        if (canvasHover && g.tplType >= 0) {
            const TypeInfo& ti = typeInfo((WType)g.tplType);
            ImVec2 p = ImFloor(md - ImVec2(ti.defW * 0.5f, ti.defH * 0.5f) + ImVec2(0.5f, 0.5f));
            if (g.snapGrid && !io.KeyCtrl) p = ImVec2(gridSnap(p.x, 0), gridSnap(p.y, 0));
            int parent = hitTest(md, true);
            Rect cont = lvhost::contentRect(parent);
            int id = addWidget(g.doc, s, (WType)g.tplType, parent, (int)p.x - cont.x1, (int)p.y - cont.y1, ti.defW, ti.defH);
            selectWidget(id);
            commit(findWidget(s, id)->name + " 추가");
        }
        g.drag = Drag::None;
        g.tplType = -1;
    }

    // 모델이 바뀌었으면 즉시 다시 렌더 (겹쳐 그리는 표시가 한 프레임 밀리지 않게)
    if (g.needsBuild && !g.sim) rebuildNow();
    selW = selWidget();
    selR = g.sel ? rectOf(g.sel) : nullptr;
    if (selR) scrRect(*selR, sa, sb);

    // 커서
    if (g.drag == Drag::None && canvasHover) {
        if (space) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        else if (g.armed >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        else if (handleHover >= 0) ImGui::SetMouseCursor(handleCursor(handleHover));
        else if (g.hover && g.hover == g.sel) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    if (g.drag == Drag::Create) ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    // ───── 그리기 ─────
    dl->AddRectFilled(C0, C1, col::sheet);
    dl->AddRect(FR0 - ImVec2(1, 1), FR1 + ImVec2(1, 1), col::ink);
    dl->PushClipRect(FR0 + ImVec2(1, 1), FR1 - ImVec2(1, 1), true);

    // 장치 화면
    ImVec2 devA = toScr(0, 0), devB = toScr((float)g.doc.resW, (float)g.doc.resH);
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    if (void* tex = platformCanvasTexture()) {
        bool nearest = g.viewK == 0.f && g.texPre == 1 && fabsf(Z - roundf(Z)) < 1e-3f && Z >= 1.f;
        if (nearest && pio.DrawCallback_SetSamplerNearest) dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
        dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex), ImFloor(devA + ImVec2(0.5f, 0.5f)), ImFloor(devB + ImVec2(0.5f, 0.5f)));
        if (nearest && pio.DrawCallback_SetSamplerLinear) dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    }
    // 비침: 다른 시트를 무사진 청색 밑그림으로 겹쳐 본다
    if (g.trace && !g.sim && traceSheet() >= 0) {
        if (void* tex = platformCanvasTexture(1)) {
            bool nearest = g.zoomTarget != 1.5f && fabsf(Z - roundf(Z)) < 1e-3f && Z >= 1.f;
            if (nearest && pio.DrawCallback_SetSamplerNearest) dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
            dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex), ImFloor(devA + ImVec2(0.5f, 0.5f)), ImFloor(devB + ImVec2(0.5f, 0.5f)));
            if (nearest && pio.DrawCallback_SetSamplerLinear) dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
        }
    }
    const ImVec2 DA = ImFloor(devA + ImVec2(0.5f, 0.5f)), DB = ImFloor(devB + ImVec2(0.5f, 0.5f));
    if (g.doc.round && g.doc.resW == g.doc.resH) {   // 원형 화면: 네 귀를 종이로 덮는다
        const ImVec2 c = (DA + DB) * 0.5f;
        const float r = (DB.x - DA.x) * 0.5f, pi = 3.14159265f;
        const ImVec2 corner[4] = {DA, ImVec2(DB.x, DA.y), DB, ImVec2(DA.x, DB.y)};
        const float a0[4] = {pi, pi * 1.5f, 0.f, pi * 0.5f};
        // 귀퉁이에서 호까지 부채꼴로 채운다. 조각 사이가 비치지 않게 가장자리 다듬기는 끈다
        dl->PushDrawFlag(ImDrawFlags_AAFill, false);
        for (int i = 0; i < 4; i++)
            for (int k = 0; k < 64; k++) {
                const float t0 = a0[i] + pi * 0.5f * k / 64, t1 = a0[i] + pi * 0.5f * (k + 1) / 64;
                dl->AddTriangleFilled(corner[i], c + ImVec2(cosf(t0), sinf(t0)) * (r - 0.5f), c + ImVec2(cosf(t1), sinf(t1)) * (r - 0.5f), col::sheet);
            }
        dl->PopDrawFlag();
        dashRect(dl, DA - ImVec2(1, 1), DB + ImVec2(1, 1), col::hair2, px(4), px(4));
        dl->AddCircle(c, r, col::ink, 180, 1.f);
    } else {
        dl->AddRect(DA - ImVec2(1, 1), DB + ImVec2(1, 1), col::ink);
    }
    // 재단선·중심 표시와 시트 이름
    {
        const float o = px(5), l = px(12);
        ImVec2 A = DA - ImVec2(1, 1), B = DB;
        for (int i = 0; i < 4; i++) {
            float x = (i == 1 || i == 2) ? B.x : A.x, y = i >= 2 ? B.y : A.y;
            float sx = (i == 1 || i == 2) ? 1.f : -1.f, sy = i >= 2 ? 1.f : -1.f;
            hline(dl, x + sx * o, x + sx * (o + l), y, col::ink3);
            vline(dl, x, y + sy * o, y + sy * (o + l), col::ink3);
        }
        float cx = (A.x + B.x) * 0.5f, cy = (A.y + B.y) * 0.5f;
        vline(dl, cx, A.y - o, A.y - o - px(7), col::ink3);
        vline(dl, cx, B.y + o, B.y + o + px(7), col::ink3);
        hline(dl, A.x - o, A.x - o - px(7), cy, col::ink3);
        hline(dl, B.x + o, B.x + o + px(7), cy, col::ink3);
        char lab[96];
        snprintf(lab, sizeof lab, "SHT %02d", g.screen + 1);
        float ty = A.y - px(27);
        textAt(dl, F.mono, 12, ImVec2(A.x + px(10), ty + px(2)), col::red, lab);
        float nx = A.x + px(10) + textW(F.mono, 12, lab) + px(10);
        textAt(dl, F.med, 15, ImVec2(nx, ty - px(1)), col::ink, s.name.c_str(), 0.4f);
        if (g.trace && !g.sim && traceSheet() >= 0) {
            nx += textW(F.med, 15, s.name.c_str(), 0.4f) + px(16);
            snprintf(lab, sizeof lab, "비침 SHT %02d", traceSheet() + 1);
            textAt(dl, F.mono, 12, ImVec2(nx, ty + px(2)), col::blueInk, lab);
            textAt(dl, F.ui, 15, ImVec2(nx + textW(F.mono, 12, lab) + px(8), ty - px(1)), col::blueInk, g.doc.screens[traceSheet()].name.c_str());
        }
    }
    if (g.sim) {   // 시운전: 누름을 LVGL로 보내고 도면 표시는 그리지 않는다
        simCanvas(dl, canvasHover && !space && g.drag == Drag::None, md, DA, DB, Z);
        dl->PopClipRect();
        drawRulers(dl);
        return;
    }
    // 빈 시트: 무엇을 하면 되는지 시트 위에 적어 둔다
    if (s.widgets.empty() && g.drag == Drag::None && g.armed < 0) {
        ImVec2 c = (DA + DB) * 0.5f;
        // 어두운 시트에서는 밝은 글씨로 적는다
        const uint32_t bg = s.bgSet ? s.bg : g.doc.dark ? 0x15171A : 0xF5F5F5;
        const bool darkBg = ((bg >> 16 & 255) * 3 + (bg >> 8 & 255) * 6 + (bg & 255)) < 1280;
        const ImU32 tc = darkBg ? IM_COL32(236, 230, 218, 255) : col::ink2, rc = darkBg ? IM_COL32(255, 138, 122, 255) : col::red;
        const char* t1 = "빈 시트";
        const char* t2 = "왼쪽 템플릿에서 부품을 끌어 놓으세요";
        textAt(dl, F.head, 24, ImVec2(c.x - textW(F.head, 24, t1, 6.f) * 0.5f, c.y - px(34)), tc, t1, 6.f);
        textAt(dl, F.ui, 15, ImVec2(c.x - textW(F.ui, 15, t2) * 0.5f, c.y + px(10)), tc, t2);
        if (standardCount() > 0) {
            const char* t3 = "또는 표준도에서 시작하기";
            const float tw = textW(F.med, 15, t3);
            ImVec2 tp(c.x - tw * 0.5f, c.y + px(36));
            ImGui::SetCursorScreenPos(tp);
            if (ImGui::InvisibleButton("##emptystd", ImVec2(tw, px(24)))) g.openStd = true;
            const bool th = ImGui::IsItemHovered();
            textAt(dl, F.med, 15, tp, rc, t3);
            hline(dl, tp.x, tp.x + tw, tp.y + px(20), th ? rc : alpha(rc, 0.4f));
            if (th) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }

    // 끄는 동안 격자
    bool dragging = g.drag == Drag::Move || g.drag == Drag::Resize || g.drag == Drag::Create || g.drag == Drag::Template;
    if (dragging && g.snapGrid && g.grid * Z >= px(7)) {
        Rect pc = (g.drag == Drag::Move || g.drag == Drag::Resize) ? g.cont0 : Rect{0, 0, g.doc.resW - 1, g.doc.resH - 1};
        for (int y = pc.y1; y <= pc.y2 + 1; y += g.grid)
            for (int x = pc.x1; x <= pc.x2 + 1; x += g.grid) {
                ImVec2 p = toScr((float)x, (float)y);
                dl->AddRectFilled(p - ImVec2(0.5f, 0.5f), p + ImVec2(0.5f, 0.5f), alpha(col::blue, 0.55f));
            }
    }

    // 숨김 위젯과 이미지 자리표시
    for (auto& w : s.widgets) {
        const Rect* r = rectOf(w.id);
        if (!r) continue;
        ImVec2 a, b;
        scrRect(*r, a, b);
        if (hiddenChain(s, w.id)) dashRect(dl, a, b, col::ink3, px(3), px(3));
        else if (w.type == WType::Image && !image::find(g.doc, w.text)) {
            hatch(dl, a, b, alpha(col::ink2, 0.35f));
            const char* t = w.text.empty() && w.text2.empty() ? "그림 없음 · 두 번 눌러 불러오기" : w.text2.empty() ? w.text.c_str() : w.text2.c_str();
            const float tw = textW(F.ui, 14, t), th = textH(F.ui, 14);
            if (tw + px(12) < b.x - a.x && th + px(2) < b.y - a.y) {
                const ImVec2 tp = ImFloor((a + b) * 0.5f - ImVec2(tw, th) * 0.5f);
                dl->AddRectFilled(tp - ImVec2(px(5), 0), tp + ImVec2(tw + px(5), th), alpha(col::sheet, 0.95f));
                textAt(dl, F.ui, 14, tp, col::ink2, t);
            }
        }
    }

    // 지운 자리: 연필 자국이 잠깐 남았다가 사라진다
    for (size_t i = 0; i < g.ghosts.size();) {
        float t = (float)((g.now - g.ghosts[i].t) / 0.55);
        if (t >= 1.f) { g.ghosts.erase(g.ghosts.begin() + i); continue; }
        ImVec2 a, b;
        scrRect(g.ghosts[i].r, a, b);
        dashRect(dl, a, b, alpha(col::ink2, (1 - t) * 0.9f), px(4), px(2) + px(10) * t);
        i++;
    }

    // 검도 표시
    for (auto& is : g.issues) {
        if (!is.error && !g.lintOpen) continue;
        const Rect* r = rectOf(is.widget);
        if (!r) continue;
        ImVec2 a, b;
        scrRect(*r, a, b);
        if (g.lintOpen || g.lintHover == is.widget) {
            revisionCloud(dl, a - ImVec2(px(3), px(3)), b + ImVec2(px(3), px(3)), is.error ? col::red : col::ink2, px(5),
                          g.lintHover == is.widget ? 2.f : 1.1f);
        } else if (is.error) {
            ImVec2 c(b.x + px(2), a.y - px(2));
            dl->AddCircleFilled(c, px(6), col::sheet);
            dl->AddCircle(c, px(6), col::red, 16, 1.2f);
            dl->AddLine(c - ImVec2(0, px(3)), c + ImVec2(0, px(0.8f)), col::red, 1.4f);
            dl->AddCircleFilled(c + ImVec2(0, px(2.8f)), px(0.9f), col::red);
        }
    }

    // 부품 번호(전체)
    if (g.balloons)
        for (auto& w : s.widgets) {
            const Rect* r = rectOf(w.id);
            if (!r || w.id == g.sel) continue;
            ImVec2 a, b;
            scrRect(*r, a, b);
            balloon(dl, a, widgetNumber(w.id), nullptr, col::ink2, false);
        }

    // 호버
    if (g.hover && g.hover != g.sel && g.drag == Drag::None && g.armed < 0) {
        if (const Widget* hw = findWidget(s, g.hover)) {
            Rect r = visibleRect(s, g.hover);
            ImVec2 a, b;
            scrRect(r, a, b);
            dl->AddRect(a, b, alt && g.sel ? col::red : col::ink);
            if (!alt) balloon(dl, a, widgetNumber(hw->id), hw->name.c_str(), col::ink, true);
        }
    }

    // 선택
    if (selR && selW) {
        if (g.animSel != g.sel) { g.animSel = g.sel; g.selAnimT = g.now; }
        float t = (float)ImClamp((g.now - g.selAnimT) / 0.22, 0.0, 1.0);
        dl->AddRect(sa, sb, col::red);
        char wb[16], hb[16];
        snprintf(wb, sizeof wb, "%d", rw(*selR));
        snprintf(hb, sizeof hb, "%d", rh(*selR));
        bool showDims = !(alt && g.drag == Drag::None) && g.drag != Drag::Move && !g.lintOpen;
        if (showDims) {
            float yAbove = sa.y - px(18);
            bool above = yAbove > FR0.y + px(12);
            dimH(dl, sa.x, sb.x, above ? yAbove : sb.y + px(18), above ? sa.y : sb.y, wb, col::red, t);
            float xLeft = sa.x - px(18);
            bool left = xLeft > FR0.x + px(12);
            dimV(dl, sa.y, sb.y, left ? xLeft : sb.x + px(18), left ? sa.x : sb.x, hb, col::red, t);
        }
        if (g.drag == Drag::Move && g.moved) {
            // 이동 중: 부모 안쪽 기준 거리
            Rect pc = g.cont0;
            ImVec2 pa = toScr((float)pc.x1, (float)pc.y1);
            char xb[32], yb[32];
            snprintf(xb, sizeof xb, "%d", selR->x1 - pc.x1);
            snprintf(yb, sizeof yb, "%d", selR->y1 - pc.y1);
            float cy = (sa.y + sb.y) * 0.5f, cx = (sa.x + sb.x) * 0.5f;
            if (sa.x - pa.x > px(4)) dimH(dl, pa.x, sa.x, cy, cy, xb, alpha(col::red, 0.8f), 1.f);
            if (sa.y - pa.y > px(4)) dimV(dl, pa.y, sa.y, cx, cx, yb, alpha(col::red, 0.8f), 1.f);
        }
        if (g.drag != Drag::Move && !(alt && g.drag == Drag::None)) {
            for (int i = 0; i < 8; i++) {
                bool edge = i % 2 == 1;
                if (edge && ((i == 1 || i == 5) ? (sb.x - sa.x) < px(26) : (sb.y - sa.y) < px(26))) continue;
                float k = px(3.5f);
                ImVec2 c = ImFloor(handles[i]) + ImVec2(0.5f, 0.5f);
                bool hv = i == handleHover || (g.drag == Drag::Resize && g.handle == i);
                dl->AddRectFilled(c - ImVec2(k, k), c + ImVec2(k, k), hv ? col::red : col::sheet);
                dl->AddRect(c - ImVec2(k, k), c + ImVec2(k, k), col::red);
            }
        }
        if (g.drag != Drag::Move && !alt && !g.lintOpen) balloon(dl, sa, widgetNumber(g.sel), nullptr, col::red, false);
    }

    // Alt: 거리 측정
    if (alt && selR && g.drag == Drag::None) {
        int tgt = (g.hover && g.hover != g.sel) ? g.hover : (selW ? selW->parent : 0);
        Rect b = tgt ? *rectOf(tgt) : Rect{0, 0, g.doc.resW - 1, g.doc.resH - 1};
        Rect a = *selR;
        ImU32 c = col::red;
        ImVec2 ta, tb;
        scrRect(b, ta, tb);
        if (tgt == (selW ? selW->parent : 0) || (a.x1 >= b.x1 && a.x2 <= b.x2 && a.y1 >= b.y1 && a.y2 <= b.y2)) {
            if (!tgt) dashRect(dl, ta, tb, col::red, px(4), px(3));
            float cy = (sa.y + sb.y) * 0.5f, cx = (sa.x + sb.x) * 0.5f;
            char t1[16], t2[16], t3[16], t4[16];
            snprintf(t1, sizeof t1, "%d", a.x1 - b.x1); snprintf(t2, sizeof t2, "%d", b.x2 - a.x2);
            snprintf(t3, sizeof t3, "%d", a.y1 - b.y1); snprintf(t4, sizeof t4, "%d", b.y2 - a.y2);
            if (a.x1 > b.x1) dimH(dl, ta.x, sa.x, cy, cy, t1, c, 1.f);
            if (b.x2 > a.x2) dimH(dl, sb.x, tb.x, cy, cy, t2, c, 1.f);
            if (a.y1 > b.y1) dimV(dl, ta.y, sa.y, cx, cx, t3, c, 1.f);
            if (b.y2 > a.y2) dimV(dl, sb.y, tb.y, cx, cx, t4, c, 1.f);
        } else {
            dashRect(dl, ta, tb, col::red, px(4), px(3));
            char t[16];
            if (a.x2 < b.x1 || b.x2 < a.x1) {
                bool right = a.x2 < b.x1;
                snprintf(t, sizeof t, "%d", right ? b.x1 - a.x2 - 1 : a.x1 - b.x2 - 1);
                float y = (std::max(sa.y, ta.y) + std::min(sb.y, tb.y)) * 0.5f;
                if (std::max(sa.y, ta.y) > std::min(sb.y, tb.y)) y = (sa.y + sb.y) * 0.5f;
                // 기준선: 치수선이 대상에 닿지 않으면 가장자리를 이어 준다
                float ex = right ? ta.x : tb.x - 1;
                if (y < ta.y) dashLine(dl, ImVec2(ex, y - px(6)), ImVec2(ex, ta.y), col::red, px(3), px(3));
                if (y > tb.y) dashLine(dl, ImVec2(ex, tb.y), ImVec2(ex, y + px(6)), col::red, px(3), px(3));
                if (right) dimH(dl, sb.x, ta.x, y, y, t, c, 1.f); else dimH(dl, tb.x, sa.x, y, y, t, c, 1.f);
            } else if (a.x1 != b.x1) {
                snprintf(t, sizeof t, "%d", abs(a.x1 - b.x1));
                float y = std::max(sb.y, tb.y) + px(14);
                dimH(dl, std::min(sa.x, ta.x), std::max(sa.x, ta.x), y, std::max(sb.y, tb.y), t, c, 1.f);
            }
            if (a.y2 < b.y1 || b.y2 < a.y1) {
                bool down = a.y2 < b.y1;
                snprintf(t, sizeof t, "%d", down ? b.y1 - a.y2 - 1 : a.y1 - b.y2 - 1);
                float x = (std::max(sa.x, ta.x) + std::min(sb.x, tb.x)) * 0.5f;
                if (std::max(sa.x, ta.x) > std::min(sb.x, tb.x)) x = (sa.x + sb.x) * 0.5f;
                float ey = down ? ta.y : tb.y - 1;
                if (x < ta.x) dashLine(dl, ImVec2(x - px(6), ey), ImVec2(ta.x, ey), col::red, px(3), px(3));
                if (x > tb.x) dashLine(dl, ImVec2(tb.x, ey), ImVec2(x + px(6), ey), col::red, px(3), px(3));
                if (down) dimV(dl, sb.y, ta.y, x, x, t, c, 1.f); else dimV(dl, tb.y, sa.y, x, x, t, c, 1.f);
            }
        }
    }

    // 스냅 가이드 (무사진 청색)
    for (auto& gd : g.guides) {
        if (gd.vertical) {
            float x = floorf(O.x + gd.pos * Z + 0.5f) + 0.5f;
            float y0 = O.y + gd.a * Z - px(10), y1 = O.y + gd.b * Z + px(10);
            dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), col::blue);
            for (float y : {y0 + px(10), y1 - px(10)}) {
                float k = px(3);
                dl->AddLine(ImVec2(x - k, y - k), ImVec2(x + k, y + k), col::blue);
                dl->AddLine(ImVec2(x - k, y + k), ImVec2(x + k, y - k), col::blue);
            }
        } else {
            float y = floorf(O.y + gd.pos * Z + 0.5f) + 0.5f;
            float x0 = O.x + gd.a * Z - px(10), x1 = O.x + gd.b * Z + px(10);
            dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), col::blue);
            for (float x : {x0 + px(10), x1 - px(10)}) {
                float k = px(3);
                dl->AddLine(ImVec2(x - k, y - k), ImVec2(x + k, y + k), col::blue);
                dl->AddLine(ImVec2(x - k, y + k), ImVec2(x + k, y - k), col::blue);
            }
        }
    }

    // 그리기 도구: 제도기 팔(십자선) + 새 부품 윤곽
    auto ghost = [&](ImVec2 a, ImVec2 b, const char* name) {
        a = ImFloor(a + ImVec2(0.5f, 0.5f));
        b = ImFloor(b + ImVec2(0.5f, 0.5f));
        dl->AddRectFilled(a, b, col::blueWash);
        dl->AddRect(a, b, col::blueInk);
        char wb[16], hb[16];
        snprintf(wb, sizeof wb, "%d", (int)roundf((b.x - a.x) / Z));
        snprintf(hb, sizeof hb, "%d", (int)roundf((b.y - a.y) / Z));
        dimH(dl, a.x, b.x, a.y - px(16), a.y, wb, col::blueInk, 1.f);
        dimV(dl, a.y, b.y, a.x - px(16), a.x, hb, col::blueInk, 1.f);
        if (name) textAt(dl, F.med, 13, ImVec2(a.x, b.y + px(5)), col::blueInk, name);
    };
    if ((g.armed >= 0 || g.drag == Drag::Create) && (canvasHover || g.drag == Drag::Create)) {
        ImVec2 p = ImFloor(md + ImVec2(0.5f, 0.5f));
        if (g.snapGrid && !io.KeyCtrl) p = ImVec2(gridSnap(p.x, 0), gridSnap(p.y, 0));
        ImVec2 sp = ImFloor(toScr(p.x, p.y) + ImVec2(0.5f, 0.5f)) + ImVec2(0.5f, 0.5f);
        dl->AddLine(ImVec2(FR0.x, sp.y), ImVec2(FR1.x, sp.y), alpha(col::blue, 0.8f));
        dl->AddLine(ImVec2(sp.x, FR0.y), ImVec2(sp.x, FR1.y), alpha(col::blue, 0.8f));
        dl->AddCircle(sp, px(4), col::blueInk, 16, 1.f);
        char cb[32];
        snprintf(cb, sizeof cb, "%d, %d", (int)p.x, (int)p.y);
        textAt(dl, F.mono, 11, sp + ImVec2(px(8), px(6)), col::blueInk, cb);
        if (g.drag == Drag::Create) {
            ImVec2 a = toScr(std::min(g.create0.x, p.x), std::min(g.create0.y, p.y));
            ImVec2 b = toScr(std::max(g.create0.x, p.x), std::max(g.create0.y, p.y));
            if (b.x - a.x > 2 || b.y - a.y > 2) ghost(a, b, typeInfo((WType)g.armed).ko);
        } else {
            textAt(dl, F.med, 13, sp + ImVec2(px(8), -px(20)), col::blueInk, typeInfo((WType)g.armed).ko);
        }
    }
    if (g.drag == Drag::Template && g.tplType >= 0 && canvasHover) {
        const TypeInfo& ti = typeInfo((WType)g.tplType);
        ImVec2 p = ImFloor(md - ImVec2(ti.defW * 0.5f, ti.defH * 0.5f) + ImVec2(0.5f, 0.5f));
        if (g.snapGrid && !io.KeyCtrl) p = ImVec2(gridSnap(p.x, 0), gridSnap(p.y, 0));
        ghost(toScr(p.x, p.y), toScr(p.x + ti.defW, p.y + ti.defH), ti.ko);
    }

    // HUD: 끄는 물체가 밑에 오면 비켜 준다(흐려짐)
    {
        ImVec2 da = sa, db = sb;
        bool any = dragging && selR;
        if (g.drag == Drag::Create || g.drag == Drag::Template) { da = m - ImVec2(px(40), px(40)); db = m + ImVec2(px(40), px(40)); any = true; }
        float tr = (any && rectsOverlap(da, db, revP, revP + ImVec2(revW, revH))) ? 0.12f : 1.f;
        float tt = (any && rectsOverlap(da, db, tbP, tbP + ImVec2(tbW, tbH))) ? 0.12f : 1.f;
        hudAlphaRev += (tr - hudAlphaRev) * std::min(1.f, g.dt * 14.f);
        hudAlphaTitle += (tt - hudAlphaTitle) * std::min(1.f, g.dt * 14.f);
    }
    drawRevTable(dl, revP, revW, revRows);
    drawTitleBlock(dl, tbP, tbW, tbMini);

    dl->PopClipRect();
    drawRulers(dl);

    // 오른쪽 클릭 메뉴
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(12), px(7)));
    if (ImGui::BeginPopup("##ctx")) {
        ImGui::Dummy(ImVec2(px(170), 0));
        if (Widget* w = selWidget()) {
            pushFont(F.mono, 12);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::red), "%02d", widgetNumber(w->id));
            ImGui::PopFont();
            ImGui::SameLine();
            pushFont(F.med, 15);
            ImGui::TextUnformatted(w->name.c_str());
            ImGui::PopFont();
            ImGui::Separator();
            pushFont(F.ui, 15);
            if (ImGui::MenuItem("복제", "Ctrl+D")) duplicateSelected();
            if (ImGui::MenuItem(w->hidden ? "보이기" : "숨기기", "H")) { w->hidden = !w->hidden; commit(w->name + (w->hidden ? " 숨김" : " 보임")); }
            ImGui::Separator();
            if (ImGui::MenuItem("앞으로", "]")) { moveZ(s, w->id, 1); commit(w->name + " 앞으로"); }
            if (ImGui::MenuItem("뒤로", "[")) { moveZ(s, w->id, -1); commit(w->name + " 뒤로"); }
            if (ImGui::MenuItem("맨 앞으로", "Shift+]")) { moveZ(s, w->id, 2); commit(w->name + " 맨 앞으로"); }
            if (ImGui::MenuItem("맨 뒤로", "Shift+[")) { moveZ(s, w->id, -2); commit(w->name + " 맨 뒤로"); }
            ImGui::Separator();
            if (ImGui::MenuItem("삭제", "Del")) deleteSelected();
            ImGui::PopFont();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}
