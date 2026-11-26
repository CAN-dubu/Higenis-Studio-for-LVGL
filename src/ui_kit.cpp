// UI 키트: 글꼴, ImGui 스타일, 제도 도구(치수선·개정 구름·도장), 컨트롤
#include "fontgen.h"
#include "ui.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

Fonts F;
static const float IM_PI_F = 3.14159265f;

ImU32 alpha(ImU32 c, float a) {
    int al = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * ImClamp(a, 0.f, 1.f));
    return (c & ~IM_COL32_A_MASK) | ((ImU32)al << IM_COL32_A_SHIFT);
}
float easeOut(float t) {
    t = ImClamp(t, 0.f, 1.f);
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}

// ───────────────────────── 글꼴 ─────────────────────────
// size는 CSS처럼 em(px)로 다룬다. ImGui는 줄높이로 크기를 재므로 글꼴별 비율을 곱한다.
static float emScale(ImFont* f) { return f == F.mono ? 1.19f : f == F.head ? 1.50f : 1.20f; }

static std::vector<char> readFile(const std::string& path) {
    std::vector<char> d;
    if (FILE* f = _wfopen(utf8ToWide(path).c_str(), L"rb")) {
        fseek(f, 0, SEEK_END);
        d.resize(ftell(f));
        fseek(f, 0, SEEK_SET);
        if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
        fclose(f);
    }
    return d;
}

// 라틴 글꼴에 한글을 합친다. krEm: 라틴 em 대비 한글 em 비율
static ImFont* addPair(const std::string& latin, std::vector<char>& kr, std::vector<char>& sym, float latinRatio, float krEm, bool krSpace = true) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig a;
    a.OversampleH = 2;
    // 한글 사이의 빈칸이 너무 좁지 않게 빈칸은 한글 글꼴 것을 쓴다
    static const ImWchar noSpace[] = {0x20, 0x20, 0};
    if (krSpace) a.GlyphExcludeRanges = noSpace;
    ImFont* f = io.Fonts->AddFontFromFileTTF(latin.c_str(), 16.f, &a);
    ImFontConfig b;
    b.MergeMode = true;
    b.PixelSnapH = true;              // 한글은 획과 자리를 픽셀에 맞춘다(번지지 않게)
    b.FontLoader = vellumFontLoader();
    b.FontDataOwnedByAtlas = false;   // 한글 글꼴은 메모리에 한 벌만
    float size = 16.f * (1.5f * krEm) / latinRatio;
    io.Fonts->AddFontFromMemoryTTF(kr.data(), (int)kr.size(), size, &b);
    b.FontLoader = nullptr;
    b.PixelSnapH = false;
    if (!sym.empty()) io.Fonts->AddFontFromMemoryTTF(sym.data(), (int)sym.size(), 16.f * 0.98f / latinRatio, &b);   // LVGL 기호
    return f;
}

void uiInit(float dpi, const std::string& assets) {
    g.S = dpi;
    const std::string dir = assets + "/fonts/";
    static std::vector<char> krLight, sym;
    sym = readFile(dir + "HigenisSymbols.ttf");
    std::vector<char>& krRegular = fontgen::data();   // 내보내기용으로 읽어 둔 것을 함께 쓴다
    krLight = readFile(dir + "IBMPlexSansKR-Light.ttf");
    F.ui = addPair(dir + "BarlowCondensed-Regular.ttf", krRegular, sym, 1.20f, 0.96f);
    F.med = addPair(dir + "BarlowCondensed-Medium.ttf", krRegular, sym, 1.20f, 0.96f);
    F.mono = addPair(dir + "ChivoMono-Regular.ttf", krRegular, sym, 1.19f, 1.04f, false);
    ImFontConfig c;
    c.FontDataOwnedByAtlas = false;
    c.PixelSnapH = true;
    c.FontLoader = vellumFontLoader();
    F.head = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(krLight.data(), (int)krLight.size(), 16.f, &c);
    ImGui::GetIO().FontDefault = F.ui;
    styleImGui();
}

static ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

void styleImGui() {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.FontSizeBase = 18.f;   // Barlow 15em
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(6, 4);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.WindowRounding = s.ChildRounding = s.FrameRounding = s.PopupRounding = 0;
    s.ScrollbarRounding = s.GrabRounding = s.TabRounding = 0;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.ScrollbarSize = 6;
    s.GrabMinSize = 18;
    s.SelectableTextAlign = ImVec2(0, 0.5f);
    s.DisabledAlpha = 0.45f;
    s.ScaleAllSizes(g.S);
    s.FontScaleDpi = g.S;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = v4(col::ink);
    c[ImGuiCol_TextDisabled] = v4(col::ink3);
    c[ImGuiCol_WindowBg] = v4(col::paper);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v4(col::sheet);
    c[ImGuiCol_Border] = v4(col::ink);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBgHovered] = v4(alpha(col::hover, 0.6f));
    c[ImGuiCol_FrameBgActive] = v4(col::sheet);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = v4(col::hair);
    c[ImGuiCol_ScrollbarGrabHovered] = v4(col::hair2);
    c[ImGuiCol_ScrollbarGrabActive] = v4(col::ink3);
    c[ImGuiCol_CheckMark] = v4(col::red);
    c[ImGuiCol_SliderGrab] = v4(col::ink);
    c[ImGuiCol_SliderGrabActive] = v4(col::red);
    c[ImGuiCol_Button] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ButtonHovered] = v4(col::hover);
    c[ImGuiCol_ButtonActive] = v4(col::press);
    c[ImGuiCol_Header] = v4(col::hover);
    c[ImGuiCol_HeaderHovered] = v4(col::hover);
    c[ImGuiCol_HeaderActive] = v4(col::press);
    c[ImGuiCol_Separator] = v4(col::hair);
    c[ImGuiCol_InputTextCursor] = v4(col::red);
    c[ImGuiCol_TextSelectedBg] = v4(alpha(col::blue, 0.35f));
    c[ImGuiCol_NavCursor] = v4(col::red);
    c[ImGuiCol_ModalWindowDimBg] = v4(IM_COL32(0x2A, 0x29, 0x26, 70));
    c[ImGuiCol_DragDropTarget] = v4(col::red);
}

// ───────────────────────── 글자 ─────────────────────────
float textH(ImFont* f, float size) { return size * g.S * emScale(f); }
void pushFont(ImFont* f, float size) { ImGui::PushFont(f, size * emScale(f)); }

float textW(ImFont* f, float size, const char* s, float tracking) {
    float w = f->CalcTextSizeA(size * g.S * emScale(f), FLT_MAX, 0.f, s).x;
    if (tracking > 0) {
        int n = 0;
        for (const char* p = s; *p; p++)
            if ((*p & 0xC0) != 0x80) n++;
        w += tracking * g.S * (n > 0 ? n - 1 : 0);
    }
    return w;
}

std::string ellipsize(ImFont* f, float size, const std::string& s, float maxw) {
    if (textW(f, size, s.c_str()) <= maxw) return s;
    std::string o = s;
    while (!o.empty()) {
        do o.pop_back(); while (!o.empty() && (o.back() & 0xC0) == 0x80);
        if (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0xC0) o.pop_back();
        if (textW(f, size, (o + "…").c_str()) <= maxw) break;
    }
    return o + "…";
}

void textAt(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const char* s, float tracking) {
    const float sz = size * g.S * emScale(f);
    if (tracking <= 0) {
        dl->AddText(f, sz, ImFloor(pos), c, s);
        return;
    }
    float x = pos.x;
    const char* p = s;
    while (*p) {
        const char* q = p + 1;
        while (*q && (*q & 0xC0) == 0x80) q++;
        dl->AddText(f, sz, ImFloor(ImVec2(x, pos.y)), c, p, q);
        x += f->CalcTextSizeA(sz, FLT_MAX, 0.f, p, q).x + tracking * g.S;
        p = q;
    }
}

void rotateVerts(ImDrawList* dl, int start, ImVec2 center, float angle) {
    const float cs = cosf(angle), sn = sinf(angle);
    for (int i = start; i < dl->VtxBuffer.Size; i++) {
        ImVec2 p = dl->VtxBuffer[i].pos - center;
        dl->VtxBuffer[i].pos = ImVec2(center.x + p.x * cs - p.y * sn, center.y + p.x * sn + p.y * cs);
    }
}

void textRot(ImDrawList* dl, ImFont* f, float size, ImVec2 center, ImU32 c, const char* s, float angle) {
    const float sz = size * g.S * emScale(f);
    ImVec2 ts = f->CalcTextSizeA(sz, FLT_MAX, 0.f, s);
    int start = dl->VtxBuffer.Size;
    dl->AddText(f, sz, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), c, s);
    rotateVerts(dl, start, center, angle);
}

// ───────────────────────── 선 ─────────────────────────
void hline(ImDrawList* dl, float x0, float x1, float y, ImU32 c, float th) {
    y = floorf(y) + 0.5f;
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), c, th);
}
void vline(ImDrawList* dl, float x, float y0, float y1, ImU32 c, float th) {
    x = floorf(x) + 0.5f;
    dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), c, th);
}
void dashLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float dash, float gap, float th) {
    ImVec2 d = b - a;
    float len = sqrtf(d.x * d.x + d.y * d.y);
    if (len < 0.5f) return;
    ImVec2 u = d / len;
    if (fabsf(d.x) < 0.01f) a.x = b.x = floorf(a.x) + 0.5f;
    if (fabsf(d.y) < 0.01f) a.y = b.y = floorf(a.y) + 0.5f;
    for (float t = 0; t < len; t += dash + gap) dl->AddLine(a + u * t, a + u * ImMin(t + dash, len), c, th);
}
void dashRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float dash, float gap) {
    b -= ImVec2(1, 1);
    dashLine(dl, a, ImVec2(b.x, a.y), c, dash, gap);
    dashLine(dl, ImVec2(b.x, a.y), b, c, dash, gap);
    dashLine(dl, b, ImVec2(a.x, b.y), c, dash, gap);
    dashLine(dl, ImVec2(a.x, b.y), a, c, dash, gap);
}

void arrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, ImU32 c) {
    // 제도용 가는 화살촉 (길이 : 폭 = 3 : 1)
    const float L = px(7.f), W = px(1.25f);
    ImVec2 n(-dir.y, dir.x);
    ImVec2 base = tip - dir * L;
    dl->AddTriangleFilled(tip, base + n * W, base - n * W, c);
}

// 가로 치수선: x1..x2 사이, y 높이에. 보조선은 extFrom(물체 가장자리)에서 시작
static void dimHalo(ImDrawList* dl, ImVec2 center, float w, float h, float a) {
    dl->AddRectFilled(center - ImVec2(w * 0.5f + px(2), h * 0.5f - px(2)), center + ImVec2(w * 0.5f + px(2), h * 0.5f - px(2)),
                      alpha(col::sheet, 0.97f * a));
}
void dimH(ImDrawList* dl, float x1, float x2, float y, float extFrom, const char* txt, ImU32 c, float t) {
    t = easeOut(t);
    x1 = floorf(x1); x2 = floorf(x2); y = floorf(y) + 0.5f; extFrom = floorf(extFrom);
    const float gapObj = px(3), over = px(4);
    const float dirY = (y < extFrom) ? -1.f : 1.f;
    ImU32 cl = alpha(c, 0.85f);
    // 보조선
    dl->AddLine(ImVec2(x1 + 0.5f, extFrom + dirY * gapObj), ImVec2(x1 + 0.5f, y + dirY * over * t), cl);
    dl->AddLine(ImVec2(x2 - 0.5f, extFrom + dirY * gapObj), ImVec2(x2 - 0.5f, y + dirY * over * t), cl);
    const float mid = (x1 + x2) * 0.5f;
    const float half = (x2 - x1) * 0.5f * t;
    const float tw = textW(F.mono, 12, txt);
    const float th = textH(F.mono, 12);
    const bool roomy = (x2 - x1) > tw + px(26);
    if (roomy) {
        float gl = mid - tw * 0.5f - px(4), gr = mid + tw * 0.5f + px(4);
        float l = mid - half, r = mid + half;
        if (l < gl) dl->AddLine(ImVec2(l, y), ImVec2(ImMin(gl, mid), y), c);
        if (r > gr) dl->AddLine(ImVec2(ImMax(gr, mid), y), ImVec2(r, y), c);
        if (t > 0.92f) {
            arrowHead(dl, ImVec2(x1 + 0.5f, y), ImVec2(-1, 0), c);
            arrowHead(dl, ImVec2(x2 - 0.5f, y), ImVec2(1, 0), c);
        }
        dimHalo(dl, ImVec2(mid, y), tw, th, ImClamp((t - 0.35f) / 0.65f, 0.f, 1.f));
        textAt(dl, F.mono, 12, ImVec2(mid - tw * 0.5f, y - th * 0.5f), alpha(c, ImClamp((t - 0.35f) / 0.65f, 0.f, 1.f)), txt);
    } else {
        // 좁으면 화살촉은 바깥, 글자는 선 위
        dl->AddLine(ImVec2(mid - half, y), ImVec2(mid + half, y), c);
        if (t > 0.92f) {
            dl->AddLine(ImVec2(x1 - px(10), y), ImVec2(x1, y), c);
            dl->AddLine(ImVec2(x2, y), ImVec2(x2 + px(10), y), c);
            arrowHead(dl, ImVec2(x1 + 0.5f, y), ImVec2(1, 0), c);
            arrowHead(dl, ImVec2(x2 - 0.5f, y), ImVec2(-1, 0), c);
        }
        float ty = dirY < 0 ? y - th - px(2) : y + px(2);
        dimHalo(dl, ImVec2(mid, ty + th * 0.5f), tw, th, t);
        textAt(dl, F.mono, 12, ImVec2(mid - tw * 0.5f, ty), alpha(c, ImClamp((t - 0.35f) / 0.65f, 0.f, 1.f)), txt);
    }
}

// 세로 치수선: 글자는 아래에서 위로 읽게 회전
void dimV(ImDrawList* dl, float y1, float y2, float x, float extFrom, const char* txt, ImU32 c, float t) {
    t = easeOut(t);
    y1 = floorf(y1); y2 = floorf(y2); x = floorf(x) + 0.5f; extFrom = floorf(extFrom);
    const float gapObj = px(3), over = px(4);
    const float dirX = (x < extFrom) ? -1.f : 1.f;
    ImU32 cl = alpha(c, 0.85f);
    dl->AddLine(ImVec2(extFrom + dirX * gapObj, y1 + 0.5f), ImVec2(x + dirX * over * t, y1 + 0.5f), cl);
    dl->AddLine(ImVec2(extFrom + dirX * gapObj, y2 - 0.5f), ImVec2(x + dirX * over * t, y2 - 0.5f), cl);
    const float mid = (y1 + y2) * 0.5f;
    const float half = (y2 - y1) * 0.5f * t;
    const float tw = textW(F.mono, 12, txt);
    const float th = textH(F.mono, 12);
    const bool roomy = (y2 - y1) > tw + px(26);
    ImU32 tc = alpha(c, ImClamp((t - 0.35f) / 0.65f, 0.f, 1.f));
    if (roomy) {
        float gt = mid - tw * 0.5f - px(4), gb = mid + tw * 0.5f + px(4);
        float tp = mid - half, bt = mid + half;
        if (tp < gt) dl->AddLine(ImVec2(x, tp), ImVec2(x, ImMin(gt, mid)), c);
        if (bt > gb) dl->AddLine(ImVec2(x, ImMax(gb, mid)), ImVec2(x, bt), c);
        if (t > 0.92f) {
            arrowHead(dl, ImVec2(x, y1 + 0.5f), ImVec2(0, -1), c);
            arrowHead(dl, ImVec2(x, y2 - 0.5f), ImVec2(0, 1), c);
        }
        dimHalo(dl, ImVec2(x, mid), th, tw + px(4), ImClamp((t - 0.35f) / 0.65f, 0.f, 1.f));
        textRot(dl, F.mono, 12, ImVec2(x, mid), tc, txt, -IM_PI_F * 0.5f);
    } else {
        dl->AddLine(ImVec2(x, mid - half), ImVec2(x, mid + half), c);
        if (t > 0.92f) {
            dl->AddLine(ImVec2(x, y1 - px(10)), ImVec2(x, y1), c);
            dl->AddLine(ImVec2(x, y2), ImVec2(x, y2 + px(10)), c);
            arrowHead(dl, ImVec2(x, y1 + 0.5f), ImVec2(0, 1), c);
            arrowHead(dl, ImVec2(x, y2 - 0.5f), ImVec2(0, -1), c);
        }
        float tx = dirX < 0 ? x - th * 0.5f - px(3) : x + th * 0.5f + px(3);
        textRot(dl, F.mono, 12, ImVec2(tx, mid), tc, txt, -IM_PI_F * 0.5f);
    }
}

// 개정 구름: 도면에서 문제/변경 부위를 표시하는 물결 테두리
void revisionCloud(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float r, float th) {
    a -= ImVec2(r, r);
    b += ImVec2(r, r);
    auto side = [&](ImVec2 from, ImVec2 to, float a0) {
        ImVec2 d = to - from;
        float len = sqrtf(d.x * d.x + d.y * d.y);
        int n = ImMax(1, (int)(len / (r * 2.f)));
        float step = len / n;
        ImVec2 u = d / len;
        for (int i = 0; i < n; i++) {
            ImVec2 cen = from + u * (step * (i + 0.5f));
            dl->PathArcTo(cen, step * 0.5f, a0, a0 + IM_PI_F, 10);
        }
    };
    side(a, ImVec2(b.x, a.y), IM_PI_F);                   // 위: 위로 볼록
    side(ImVec2(b.x, a.y), b, -IM_PI_F * 0.5f);           // 오른쪽
    side(b, ImVec2(a.x, b.y), 0.f);                       // 아래
    side(ImVec2(a.x, b.y), a, IM_PI_F * 0.5f);            // 왼쪽
    dl->PathStroke(c, th, ImDrawFlags_Closed);
}

// 도장: 이중 원 + 글자, 살짝 기울어짐
void stamp(ImDrawList* dl, ImVec2 cen, float r, const char* top, const char* bottom, float angle, float a) {
    ImU32 c = alpha(col::red, 0.9f * a);
    int start = dl->VtxBuffer.Size;
    dl->AddCircle(cen, r, c, 48, ImMax(1.f, r * 0.055f));
    dl->AddCircle(cen, r - ImMax(px(2.5f), r * 0.1f), c, 48, px(0.8f));
    const float u = r / g.S;
    if (bottom) {
        float fs = u * 0.46f;
        float tw = textW(F.med, fs, top, u * 0.06f);
        textAt(dl, F.med, fs, ImVec2(cen.x - tw * 0.5f, cen.y - r * 0.08f - textH(F.med, fs)), c, top, u * 0.06f);
        dl->AddLine(ImVec2(cen.x - r * 0.72f, cen.y), ImVec2(cen.x + r * 0.72f, cen.y), c, px(0.8f));
        float bs = u * 0.215f, bw = textW(F.mono, bs, bottom);
        textAt(dl, F.mono, bs, ImVec2(cen.x - bw * 0.5f, cen.y + r * 0.1f), c, bottom);
    } else {
        float fs = u * 0.92f;
        float tw = textW(F.med, fs, top);
        textAt(dl, F.med, fs, ImVec2(cen.x - tw * 0.5f + px(0.5f), cen.y - textH(F.med, fs) * 0.52f), c, top);
    }
    rotateVerts(dl, start, cen, angle);
}

// ───────────────────────── 컨트롤 ─────────────────────────
void tooltip(const char* text, const char* shortcut) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8), px(5)));
    ImGui::BeginTooltip();
    pushFont(F.ui, 14);
    ImGui::TextUnformatted(text);
    if (shortcut) {
        ImGui::SameLine();
        pushFont(F.mono, 12);
        ImGui::TextColored(v4(col::ink3), "%s", shortcut);
        ImGui::PopFont();
    }
    ImGui::PopFont();
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}

// 섹션 머리: "01 치수" 가는 한글, 넓은 자간, 흑연 실선
void sectionHeader(const char* num, const char* title, const char* aside) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = px(28);
    const float base = p.y + px(20);   // 공통 글줄
    float nx = p.x;
    if (num && *num) {
        textAt(dl, F.mono, 11, ImVec2(nx, base - px(11.5f)), col::red, num);
        nx += textW(F.mono, 11, num) + px(9);
    }
    textAt(dl, F.med, 15, ImVec2(nx, base - px(15.5f)), col::ink, title, 1.6f);
    if (aside) {
        float aw = textW(F.ui, 14, aside);
        textAt(dl, F.ui, 14, ImVec2(p.x + w - aw, base - px(14)), col::ink3, aside);
    }
    float ly = floorf(p.y + h) - 0.5f;
    dl->AddLine(ImVec2(p.x, ly), ImVec2(p.x + w, ly), col::ink);
    ImGui::Dummy(ImVec2(w, h + px(6)));
}

// 필드 이름: 왼쪽 열. mark = 기본값을 덮어쓴 속성(빨간 표시)
void fieldLabel(const char* label, bool mark) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = px(26);
    if (mark) dl->AddRectFilled(ImVec2(p.x - px(8), p.y + px(8)), ImVec2(p.x - px(6), p.y + h - px(8)), col::red);
    textAt(dl, F.ui, 15, ImVec2(p.x, p.y + px(4)), mark ? col::ink : col::ink2, label);
    ImGui::Dummy(ImVec2(px(96), h));
    ImGui::SameLine(0, 0);
}

// 끌어서 바꾸는 숫자 필드. 반환: 0 없음, 1 변경 중, 2 확정
int scrubInt(const char* id, const char* label, int* v, int vmin, int vmax, float width, bool dim) {
    static ImGuiID editing = 0, dragging = 0;
    static int startVal = 0;
    static float accum = 0;
    static char buf[32];
    static bool focusSet = false;
    static int editFrame = 0;
    if (editing && ImGui::GetFrameCount() - editFrame > 1) editing = 0;   // 필드가 사라졌으면 입력 상태도 버린다
    ImGuiID gid = ImGui::GetID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(26);
    const float lw = label && *label ? textW(F.med, 14, label) + px(10) : 0.f;
    int result = 0;

    if (editing == gid) {
        editFrame = ImGui::GetFrameCount();
        ImGui::SetCursorScreenPos(ImVec2(p.x + lw, p.y));
        ImGui::SetNextItemWidth(width - lw);
        if (!focusSet) { ImGui::SetKeyboardFocusHere(); focusSet = true; }
        pushFont(F.mono, 13);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(2), px(5)));
        bool enter = ImGui::InputText("##edit", buf, sizeof buf,
                                      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific |
                                          ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        ImGui::PopFont();
        bool done = enter || ImGui::IsItemDeactivated();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { editing = 0; done = false; }
        if (done) {
            int nv = ImClamp(atoi(buf), vmin, vmax);
            if (nv != *v) { *v = nv; result = 2; }
            editing = 0;
        }
        textAt(dl, F.med, 14, ImVec2(p.x, p.y + px(5)), col::ink2, label);
        dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + width, p.y + h - 0.5f), col::red);
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(width, h));
        return result;
    }

    ImGui::InvisibleButton(id, ImVec2(width, h));
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActivated()) { startVal = *v; accum = 0; dragging = 0; }
    if (act) {
        float dx = ImGui::GetIO().MouseDelta.x;
        if (dragging != gid && fabsf(ImGui::GetMouseDragDelta(0).x) > px(3)) dragging = gid;
        if (dragging == gid) {
            accum += dx / px(3.f) * (ImGui::GetIO().KeyShift ? 10.f : 1.f);
            int nv = ImClamp(startVal + (int)accum, vmin, vmax);
            if (nv != *v) { *v = nv; result = 1; }
        }
    }
    if (ImGui::IsItemDeactivated()) {
        if (dragging == gid) {
            if (*v != startVal) result = 2;
            dragging = 0;
        } else {
            editing = gid;
            editFrame = ImGui::GetFrameCount();
            focusSet = false;
            snprintf(buf, sizeof buf, "%d", *v);
        }
    }
    // 그리기
    ImU32 lc = dim ? col::ink3 : col::ink2;
    if (lw > 0) textAt(dl, F.med, 14, ImVec2(p.x, p.y + px(5)), lc, label);
    char vb[32];
    snprintf(vb, sizeof vb, "%d", *v);
    float vw = textW(F.mono, 13, vb);
    textAt(dl, F.mono, 13, ImVec2(p.x + width - vw - px(2), p.y + px(5)), dim ? col::ink3 : col::ink, vb);
    ImU32 uc = act ? col::red : hov ? col::ink2 : col::hair2;
    dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + width, p.y + h - 0.5f), uc);
    if (hov || act) {
        // 스케일 자 눈금: 값에 따라 미끄러진다
        float step = px(4);
        float off = fmodf((float)*v * px(1.5f), step);
        if (off < 0) off += step;
        for (float x = p.x + lw + off; x < p.x + width; x += step) {
            int k = (int)((x - p.x - lw - off) / step + 0.5f);
            float tl = (k % 5 == 0) ? px(4) : px(2);
            dl->AddLine(ImVec2(x, p.y + h - 1 - tl), ImVec2(x, p.y + h - 1), alpha(act ? col::red : col::ink3, 0.7f));
        }
    }
    return result;
}

// 가는 선 버튼. kind: 0 보통, 1 주요(빨간 연필), 2 테두리 없음
bool lineButton(const char* label, ImVec2 size, int kind, const char* hint) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImFont* f = F.med;
    float tw = textW(f, 15, label);
    float hw = hint ? textW(F.mono, 11, hint) + px(8) : 0.f;
    if (size.x <= 0) size.x = tw + hw + px(24);
    if (size.y <= 0) size.y = px(30);
    bool r = ImGui::InvisibleButton(label, size);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    float push = act ? px(1) : 0.f;
    ImU32 border = kind == 1 ? col::red : (hov ? col::ink2 : col::hair2);
    ImU32 text = kind == 1 ? col::red : col::ink;
    if (hov) dl->AddRectFilled(p, p + size, act ? col::press : col::hover);
    if (kind != 2) dl->AddRect(p + ImVec2(0, push), p + size + ImVec2(0, push), border, 0, kind == 1 ? px(1.4f) : 1.f);
    float th = textH(f, 15);
    float x = p.x + (size.x - tw - hw) * 0.5f;
    textAt(dl, f, 15, ImVec2(x, p.y + (size.y - th) * 0.5f + push), text, label);
    if (hint) textAt(dl, F.mono, 11, ImVec2(x + tw + px(8), p.y + (size.y - th) * 0.5f + px(3) + push), col::ink3, hint);
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return r;
}

bool textLink(const char* label, ImU32 c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float tw = textW(F.med, 14, label);
    float th = textH(F.med, 14);
    bool r = ImGui::InvisibleButton(label, ImVec2(tw, th + px(4)));
    bool hov = ImGui::IsItemHovered();
    textAt(dl, F.med, 14, p, hov ? col::red : c, label);
    if (hov) {
        dl->AddLine(ImVec2(p.x, p.y + th + px(1)), ImVec2(p.x + tw, p.y + th + px(1)), col::red);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    return r;
}

// 색 고르기 판: 명도·채도 판, 색상 띠, 이 도면에 쓴 색, LVGL 팔레트
static int colorBoard(int* rgb) {
    static float H = 0.58f, S = 1, V = 1;
    static int last = -1;
    static bool dragging = false;
    int result = 0;
    if (last != *rgb) {
        float h2, s2, v2;
        ImGui::ColorConvertRGBtoHSV(((*rgb >> 16) & 255) / 255.f, ((*rgb >> 8) & 255) / 255.f, (*rgb & 255) / 255.f, h2, s2, v2);
        if (s2 > 0.001f && v2 > 0.001f) H = h2;   // 무채색이어도 색상 위치는 기억
        S = s2; V = v2; last = *rgb;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = px(248);
    bool changed = false;
    // 명도·채도
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(W, px(132));
    ImGui::InvisibleButton("##sv", sz);
    ImVec2 m = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActive()) {
        S = ImClamp((m.x - p.x) / sz.x, 0.f, 1.f);
        V = 1.f - ImClamp((m.y - p.y) / sz.y, 0.f, 1.f);
        changed = true;
    }
    bool svEnd = ImGui::IsItemDeactivated();
    float hr, hg, hb;
    ImGui::ColorConvertHSVtoRGB(H, 1, 1, hr, hg, hb);
    ImU32 hue = IM_COL32((int)(hr * 255), (int)(hg * 255), (int)(hb * 255), 255);
    dl->AddRectFilledMultiColor(p, p + sz, IM_COL32_WHITE, hue, hue, IM_COL32_WHITE);
    dl->AddRectFilledMultiColor(p, p + sz, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, IM_COL32_BLACK);
    dl->AddRect(p, p + sz, col::ink);
    ImVec2 c = ImFloor(p + ImVec2(S * sz.x, (1 - V) * sz.y));
    ImU32 cross = V > 0.55f && S < 0.5f ? alpha(col::ink, 0.55f) : IM_COL32(255, 255, 255, 150);
    hline(dl, p.x + 1, p.x + sz.x - 1, ImClamp(c.y, p.y + 1, p.y + sz.y - 2), cross);
    vline(dl, ImClamp(c.x, p.x + 1, p.x + sz.x - 2), p.y + 1, p.y + sz.y - 1, cross);
    float k = px(4);
    dl->AddRectFilled(c - ImVec2(k, k), c + ImVec2(k + 1, k + 1), IM_COL32((*rgb >> 16) & 255, (*rgb >> 8) & 255, *rgb & 255, 255));
    dl->AddRect(c - ImVec2(k, k), c + ImVec2(k + 1, k + 1), col::sheet);
    dl->AddRect(c - ImVec2(k + 1, k + 1), c + ImVec2(k + 2, k + 2), col::ink);
    // 색상 띠
    ImGui::Dummy(ImVec2(0, px(4)));
    ImVec2 q = ImGui::GetCursorScreenPos();
    ImVec2 hs(W, px(14));
    ImGui::InvisibleButton("##hue", hs);
    if (ImGui::IsItemActive()) { H = ImClamp((m.x - q.x) / hs.x, 0.f, 0.9999f); changed = true; }
    bool hueEnd = ImGui::IsItemDeactivated();
    for (int i = 0; i < 6; i++) {
        float r0, g0, b0, r1, g1, b1;
        ImGui::ColorConvertHSVtoRGB(i / 6.f, 0.85f, 0.95f, r0, g0, b0);
        ImGui::ColorConvertHSVtoRGB((i + 1) / 6.f, 0.85f, 0.95f, r1, g1, b1);
        ImU32 ca = IM_COL32((int)(r0 * 255), (int)(g0 * 255), (int)(b0 * 255), 255), cb = IM_COL32((int)(r1 * 255), (int)(g1 * 255), (int)(b1 * 255), 255);
        dl->AddRectFilledMultiColor(q + ImVec2(W * i / 6, 0), q + ImVec2(W * (i + 1) / 6, hs.y), ca, cb, cb, ca);
    }
    dl->AddRect(q, q + hs, col::ink);
    float hx = floorf(q.x + H * W);
    dl->AddTriangleFilled(ImVec2(hx, q.y + hs.y + px(1)), ImVec2(hx - px(4), q.y + hs.y + px(7)), ImVec2(hx + px(4), q.y + hs.y + px(7)), col::ink);
    vline(dl, hx, q.y, q.y + hs.y, col::sheet);
    if (changed) {
        float r, gg, bb;
        ImGui::ColorConvertHSVtoRGB(H, S, V, r, gg, bb);
        int nv = ((int)(r * 255 + 0.5f) << 16) | ((int)(gg * 255 + 0.5f) << 8) | (int)(bb * 255 + 0.5f);
        if (nv != *rgb) { *rgb = nv; dragging = true; result = 1; }
        last = *rgb;
    }
    if ((svEnd || hueEnd) && dragging) { dragging = false; result = 2; }
    ImGui::Dummy(ImVec2(0, px(8)));
    // 16진 값
    {
        ImVec2 hp = ImGui::GetCursorScreenPos();
        textAt(dl, F.mono, 13, hp + ImVec2(0, px(5)), col::ink3, "0x");
        ImGui::SetCursorScreenPos(hp + ImVec2(px(20), 0));
        char hexs[8];
        snprintf(hexs, sizeof hexs, "%06X", *rgb & 0xFFFFFF);
        pushFont(F.mono, 13);
        ImGui::SetNextItemWidth(px(92));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(2), px(5)));
        if (ImGui::InputText("##hex", hexs, sizeof hexs, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_AutoSelectAll) && strlen(hexs) == 6) {
            *rgb = (int)strtol(hexs, nullptr, 16) & 0xFFFFFF;
            result = 1;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) result = 2;
        ImGui::PopStyleVar();
        ImVec2 ia = ImGui::GetItemRectMin(), ib = ImGui::GetItemRectMax();
        hline(dl, ia.x, ib.x, ib.y - 1, ImGui::IsItemActive() ? col::red : col::hair2);
        ImGui::PopFont();
    }
    // 견본 줄
    auto row = [&](const char* title, const int* cols, int n) {
        if (!n) return;
        ImVec2 t = ImGui::GetCursorScreenPos();
        textAt(dl, F.ui, 14, t + ImVec2(0, px(2)), col::ink2, title);
        ImGui::Dummy(ImVec2(W, px(20)));
        ImVec2 o = ImGui::GetCursorScreenPos();
        const int per = 10;
        const float cell = floorf(W / per), sw = cell - px(4);
        for (int i = 0; i < n; i++) {
            ImVec2 a = o + ImVec2((i % per) * cell, (i / per) * cell);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(title);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##sw", ImVec2(sw, sw))) { *rgb = cols[i]; result = 2; }
            bool hv = ImGui::IsItemHovered();
            char tip[16];
            snprintf(tip, sizeof tip, "%06X", cols[i]);
            tooltip(tip);
            ImGui::PopID();
            ImGui::PopID();
            dl->AddRectFilled(a, a + ImVec2(sw, sw), IM_COL32((cols[i] >> 16) & 255, (cols[i] >> 8) & 255, cols[i] & 255, 255));
            dl->AddRect(a, a + ImVec2(sw, sw), cols[i] == *rgb ? col::red : hv ? col::ink : col::hair2);
            if (cols[i] == *rgb) dl->AddRect(a - ImVec2(1, 1), a + ImVec2(sw + 1, sw + 1), col::red);
            if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::SetCursorScreenPos(o);
        ImGui::Dummy(ImVec2(W, ((n + per - 1) / per) * cell + px(4)));
    };
    ImGui::Dummy(ImVec2(0, px(2)));
    int used[20], nu = 0;
    auto addUsed = [&](int v) {
        for (int i = 0; i < nu; i++) if (used[i] == v) return;
        if (nu < 20) used[nu++] = v;
    };
    addUsed(g.doc.primary);
    for (auto& sc : g.doc.screens) {
        if (sc.bgSet) addUsed(sc.bg);
        for (auto& w : sc.widgets) {
            if (w.type == WType::Led) addUsed(w.color);
            for (int st = 0; st < ST_COUNT; st++)
                for (int pr = 0; pr < SP_COUNT; pr++)
                    if (stylePropInfo(pr).color && w.style[st][pr].set) addUsed(w.style[st][pr].v);
        }
    }
    row("이 도면에 쓴 색", used, nu);
    static const int lv[] = {0xF44336, 0xE91E63, 0x9C27B0, 0x673AB7, 0x3F51B5, 0x2196F3, 0x03A9F4, 0x00BCD4, 0x009688, 0x4CAF50,
                             0x8BC34A, 0xCDDC39, 0xFFEB3B, 0xFFC107, 0xFF9800, 0xFF5722, 0x795548, 0x607D8B, 0x9E9E9E, 0xFFFFFF};
    row("LVGL 팔레트", lv, 20);
    return result;
}

// 색 필드: 견본 + 16진 값. 반환 0 없음, 1 변경 중, 2 확정, 3 기본값으로 되돌림
int colorField(const char* id, int* rgb, bool set, int themeVal, float width) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(26);
    int shown = set ? *rgb : themeVal;
    int result = 0;
    float resetW = set ? px(22) : 0.f;
    bool clicked = ImGui::InvisibleButton("##sw", ImVec2(width - resetW, h));
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 sa(p.x, p.y + px(5)), sb(p.x + px(16), p.y + px(21));
    ImU32 sc = IM_COL32((shown >> 16) & 255, (shown >> 8) & 255, shown & 255, 255);
    dl->AddRectFilled(sa, sb, sc);
    dl->AddRect(sa, sb, set ? col::ink : col::ink3);
    if (!set) dl->AddLine(ImVec2(sa.x + 1, sb.y - 1), ImVec2(sb.x - 1, sa.y + 1), alpha(col::ink3, 0.9f));
    char hb[16];
    snprintf(hb, sizeof hb, "%06X", shown & 0xFFFFFF);
    textAt(dl, F.mono, 13, ImVec2(p.x + px(24), p.y + px(5)), set ? col::ink : col::ink3, hb);
    if (!set) textAt(dl, F.ui, 14, ImVec2(p.x + px(24) + textW(F.mono, 13, hb) + px(8), p.y + px(5)), col::ink3, "테마");
    dl->AddLine(ImVec2(p.x, p.y + h - 0.5f), ImVec2(p.x + width, p.y + h - 0.5f), hov ? col::ink2 : col::hair2);
    if (set) {
        ImGui::SameLine(0, 0);
        ImVec2 rp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##reset", ImVec2(resetW, h))) result = 3;
        bool rh = ImGui::IsItemHovered();
        ImVec2 c = rp + ImVec2(resetW * 0.5f, h * 0.5f);
        float k = px(3.5f);
        dl->AddLine(c - ImVec2(k, k), c + ImVec2(k, k), rh ? col::red : col::ink3, 1.2f);
        dl->AddLine(c + ImVec2(-k, k), c + ImVec2(k, -k), rh ? col::red : col::ink3, 1.2f);
        if (rh) tooltip("테마 기본값으로");
    }
    if (clicked) ImGui::OpenPopup("##picker");   // 여는 것만으로는 값을 바꾸지 않는다
    {
        ImVec2 vs = ImGui::GetMainViewport()->Pos + ImGui::GetMainViewport()->Size;
        bool up = p.y + h + px(380) > vs.y;   // 아래가 모자라면 위로 연다
        ImGui::SetNextWindowPos(ImVec2(p.x + width, up ? p.y - px(4) : p.y + h + px(4)), ImGuiCond_Always, ImVec2(1.f, up ? 1.f : 0.f));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(12)));
    if (ImGui::BeginPopup("##picker")) {
        int r = colorBoard(rgb);
        if (r) result = r;
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return result;
}

// 상태 탭: 선택은 흑연 + 빨간 밑줄, 덮어쓴 상태엔 빨간 점
bool segTabs(const char* id, const char* const* items, int n, int* sel, const bool* marks) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    ImVec2 start = ImGui::GetCursorScreenPos();
    float total = ImGui::GetContentRegionAvail().x;
    float x = start.x;
    const float h = px(28);
    for (int i = 0; i < n; i++) {
        float tw = textW(F.med, 14, items[i]);
        float w = tw + px(16);
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##t", ImVec2(w, h))) { *sel = i; changed = true; }
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        bool on = *sel == i;
        textAt(dl, F.med, 14, ImVec2(x + px(8), start.y + px(5)), on ? col::ink : hov ? col::ink : col::ink2, items[i]);
        if (on) dl->AddRectFilled(ImVec2(x + px(4), start.y + h - px(2)), ImVec2(x + w - px(4), start.y + h), col::red);
        if (marks && marks[i]) dl->AddCircleFilled(ImVec2(x + w - px(4), start.y + px(7)), px(2.2f), col::red);
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        x += w;
    }
    dl->AddLine(ImVec2(start.x, start.y + h - 0.5f), ImVec2(start.x + total, start.y + h - 0.5f), col::hair);
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(total, h + px(4)));
    ImGui::PopID();
    return changed;
}

// 제도 스위치: 가는 사각 트랙 + 사각 노브
bool toggleField(const char* id, bool* v) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(26), tw = px(30), th = px(14);
    bool r = ImGui::InvisibleButton(id, ImVec2(tw + px(4), h));
    if (r) *v = !*v;
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 a = ImFloor(ImVec2(p.x, p.y + (h - th) * 0.5f)), b = a + ImVec2(tw, th);
    dl->AddRect(a, b, hov ? col::ink : col::ink2);
    float k = th - px(4);
    float kx = *v ? b.x - px(2) - k : a.x + px(2);
    kx = floorf(kx);
    if (*v) dl->AddRectFilled(ImVec2(kx, a.y + px(2)), ImVec2(kx + k, a.y + px(2) + k), col::ink);
    else dl->AddRect(ImVec2(kx, a.y + px(2)), ImVec2(kx + k, a.y + px(2) + k), col::ink2);
    return r;
}

// 정렬 기준점 고르기: 3×3 점, 선택점에 빨간 기준선
bool alignPicker(const char* id, int* a) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = px(52);
    ImGui::InvisibleButton(id, ImVec2(s, s));
    bool hov = ImGui::IsItemHovered();
    bool changed = false;
    ImVec2 m = ImGui::GetIO().MousePos;
    int hi = -1;
    const float pad = px(9), step = (s - pad * 2) * 0.5f;
    for (int i = 0; i < 9; i++) {
        ImVec2 c(p.x + pad + (i % 3) * step, p.y + pad + (i / 3) * step);
        if (hov && fabsf(m.x - c.x) < step * 0.5f && fabsf(m.y - c.y) < step * 0.5f) hi = i;
    }
    if (hi >= 0 && ImGui::IsItemClicked()) { *a = hi; changed = true; }
    dl->AddRect(p, p + ImVec2(s, s), hov ? col::ink2 : col::hair2);
    ImVec2 sc(p.x + pad + (*a % 3) * step, p.y + pad + (*a / 3) * step);
    dl->AddLine(ImVec2(p.x + 1, sc.y), ImVec2(p.x + s - 1, sc.y), alpha(col::red, 0.5f));
    dl->AddLine(ImVec2(sc.x, p.y + 1), ImVec2(sc.x, p.y + s - 1), alpha(col::red, 0.5f));
    for (int i = 0; i < 9; i++) {
        ImVec2 c(p.x + pad + (i % 3) * step, p.y + pad + (i / 3) * step);
        if (i == *a) dl->AddCircleFilled(c, px(3.5f), col::red);
        else dl->AddCircleFilled(c, i == hi ? px(3.f) : px(1.8f), i == hi ? col::ink : col::ink3);
    }
    if (hi >= 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (hi != *a) tooltip(alignKo(hi));
    }
    return changed;
}

static void underline(bool active) {
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    bool hov = ImGui::IsItemHovered();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f),
                                        active ? col::red : hov ? col::ink2 : col::hair2);
}

int fieldText(const char* id, std::string* s, float width, const char* hint) {
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(2), px(5)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(col::ink3));
    bool edited = hint ? ImGui::InputTextWithHint(id, hint, s) : ImGui::InputText(id, s);
    int r = ImGui::IsItemDeactivatedAfterEdit() ? 2 : edited ? 1 : 0;
    underline(ImGui::IsItemActive());
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return r;
}

int fieldMultiline(const char* id, std::string* s, float width, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(6), px(5)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(col::sheet));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(col::sheet));
    bool edited = ImGui::InputTextMultiline(id, s, ImVec2(width, height));
    int done = ImGui::IsItemDeactivatedAfterEdit() ? 2 : edited ? 1 : 0;
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(a, b, ImGui::IsItemActive() ? col::red : col::hair2);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    return done;
}

bool comboField(const char* id, const char* preview, float width) {
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(2), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(10)));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge | ImGuiComboFlags_NoArrowButton);
    if (!open) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool hv = ImGui::IsItemHovered();
        hline(dl, a.x, b.x, b.y - 1, hv ? col::ink2 : col::hair2);
        ImVec2 t(b.x - px(8), (a.y + b.y) * 0.5f);
        dl->AddTriangleFilled(t + ImVec2(-px(3.5f), -px(1.5f)), t + ImVec2(px(3.5f), -px(1.5f)), t + ImVec2(0, px(2.5f)), hv ? col::ink : col::ink2);
        if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return open;
}
