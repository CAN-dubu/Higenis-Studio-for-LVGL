// Higenis Studio 진입점: Win32 창, Direct3D 11, 파일 대화상자
//
//   HigenisStudio.exe [도면.hg]                         도면 열기
//   HigenisStudio.exe 도면.hg --export 폴더              창 없이 C 코드 내보내기 (빌드 스크립트용)
//   HigenisStudio.exe --script 파일 [--size 1600x1000]   입력을 재생하고 화면을 PNG로 저장 (자체 점검용)
#include "fontface.h"
#include "fontgen.h"
#include "image.h"
#include "ui.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <windows.h>
#include <algorithm>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <imm.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
HWND g_hwnd = nullptr;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
struct Tex { ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; int w = 0, h = 0; } g_texs[5];
UINT g_resizeW = 0, g_resizeH = 0;
bool g_ready = false, g_quit = false, g_occluded = false;
std::string g_license;

// 한글 입력기: 글자 칸에 있을 때만 켠다(그래야 한글 모드에서도 G·N·T 단축키가 먹는다)
bool g_imeOn = true, g_composing = false;
struct Deferred { UINT msg; WPARAM w; LPARAM l; };
std::vector<Deferred> g_deferred;   // 조합 중에 눌린 마우스: 글자를 먼저 넣고 다음 프레임에 처리

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

void createRtv() {
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&bb)))) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
    }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE t : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, t, nullptr, 0, want, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) return false;
    createRtv();
    return true;
}

void renderFrame();

bool isMouseButton(UINT m) {
    return m == WM_LBUTTONDOWN || m == WM_LBUTTONUP || m == WM_LBUTTONDBLCLK || m == WM_RBUTTONDOWN || m == WM_RBUTTONUP ||
           m == WM_RBUTTONDBLCLK || m == WM_MBUTTONDOWN || m == WM_MBUTTONUP || m == WM_MBUTTONDBLCLK;
}
void completeComposition(HWND h) {
    if (HIMC c = ImmGetContext(h)) {
        ImmNotifyIME(c, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
        ImmReleaseContext(h, c);
    }
    g_composing = false;
}
void syncIme(bool want) {
    if (want == g_imeOn) return;
    if (!want && g_composing) completeComposition(g_hwnd);
    ImmAssociateContextEx(g_hwnd, nullptr, want ? IACE_DEFAULT : 0);
    g_imeOn = want;
}

LRESULT WINAPI wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_IME_STARTCOMPOSITION) g_composing = true;
    if (msg == WM_IME_ENDCOMPOSITION) g_composing = false;
    if (g_ready && isMouseButton(msg) && (g_composing || !g_deferred.empty())) {
        // 조합 중인 글자를 확정해 글자 칸에 먼저 넣고, 누른 것은 한 프레임 뒤에 처리한다
        if (g_composing) completeComposition(h);
        g_deferred.push_back({msg, w, l});
        return 0;
    }
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return true;
    switch (msg) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) { g_resizeW = LOWORD(l); g_resizeH = HIWORD(l); }
        return 0;
    case WM_ENTERSIZEMOVE: SetTimer(h, 1, 16, nullptr); return 0;
    case WM_EXITSIZEMOVE: KillTimer(h, 1); return 0;
    case WM_TIMER: if (g_ready) renderFrame(); return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize = {(LONG)(1180 * g.S), (LONG)(720 * g.S)};
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU) return 0;   // Alt는 거리 재기에 쓴다
        break;
    case WM_DROPFILES: {
        HDROP d = (HDROP)w;
        POINT pt;
        DragQueryPoint(d, &pt);
        wchar_t path[2048];
        if (g_ready && DragQueryFileW(d, 0, path, 2048)) dropFile(path, ImVec2((float)pt.x, (float)pt.y));
        DragFinish(d);
        return 0;
    }
    case WM_CLOSE: if (g_ready) requestAction(1); else g_quit = true; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// ── PNG ──
void pngWrite(void* ctx, void* data, int size) { fwrite(data, 1, size, (FILE*)ctx); }
bool saveBackbuffer(const std::wstring& path) {
    ID3D11Texture2D *bb = nullptr, *st = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&bb)))) return false;
    D3D11_TEXTURE2D_DESC d;
    bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    bool ok = false;
    if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &st))) {
        g_ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            std::vector<uint8_t> px((size_t)d.Width * d.Height * 4);
            for (UINT y = 0; y < d.Height; y++) {
                memcpy(&px[(size_t)y * d.Width * 4], (uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)d.Width * 4);
                for (UINT x = 0; x < d.Width; x++) px[((size_t)y * d.Width + x) * 4 + 3] = 255;
            }
            g_ctx->Unmap(st, 0);
            if (FILE* f = _wfopen(path.c_str(), L"wb")) {
                ok = stbi_write_png_to_func(pngWrite, f, (int)d.Width, (int)d.Height, 4, px.data(), (int)d.Width * 4) != 0;
                fclose(f);
            }
        }
        st->Release();
    }
    bb->Release();
    return ok;
}

// ── 입력 재생(자체 점검) ──
struct Script {
    std::vector<std::string> lines;
    size_t at = 0;
    int wait = 0;
    bool on = false;
    FILE* log = nullptr;
    std::wstring shot;
    std::string dir;
    int checks = 0, fails = 0;
} g_script;

ImGuiKey keyByName(const std::string& n) {
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return (ImGuiKey)(ImGuiKey_A + (n[0] - 'a'));
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return (ImGuiKey)(ImGuiKey_0 + (n[0] - '0'));
    static const std::pair<const char*, ImGuiKey> k[] = {
        {"ctrl", ImGuiMod_Ctrl}, {"shift", ImGuiMod_Shift}, {"alt", ImGuiMod_Alt}, {"space", ImGuiKey_Space},
        {"esc", ImGuiKey_Escape}, {"f5", ImGuiKey_F5}, {"del", ImGuiKey_Delete}, {"enter", ImGuiKey_Enter}, {"tab", ImGuiKey_Tab},
        {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow}, {"up", ImGuiKey_UpArrow},
        {"down", ImGuiKey_DownArrow}, {"[", ImGuiKey_LeftBracket}, {"]", ImGuiKey_RightBracket}, {"back", ImGuiKey_Backspace}};
    for (auto& e : k) if (n == e.first) return e.second;
    return ImGuiKey_None;
}

void dumpState(FILE* f, const std::string& tag) {
    fprintf(f, "== %s rev=%s(%d/%d) screen=%d sel=%d dirty=%d armed=%d drag=%d widgets=%d errors=", tag.c_str(),
            g.hist.label(g.hist.cur).c_str(), g.hist.cur, (int)g.hist.revs.size(), g.screen, g.sel, g.dirty, g.armed,
            (int)g.drag, (int)curScreen().widgets.size());
    int e = 0;
    for (auto& i : g.issues) e += i.error;
    fprintf(f, "%d\n", e);
    for (auto& w : curScreen().widgets) {
        auto it = lvhost::coords().find(w.id);
        Rect r = it == lvhost::coords().end() ? Rect{} : it->second;
        fprintf(f, "  %d p%d %s %s xy=%d,%d wh=%d,%d al=%d auto=%d hid=%d abs=[%d,%d..%d,%d]\n", w.id, w.parent,
                typeInfo(w.type).key, w.name.c_str(), w.x, w.y, w.w, w.h, w.align, w.autoSize, w.hidden, r.x1, r.y1, r.x2, r.y2);
    }
    if (g.hist.cur >= 0) fprintf(f, "  note: %s\n", g.hist.revs[g.hist.cur].note.c_str());
    fflush(f);
}

// expect <무엇> <값>: 지금 상태가 값과 같은지 따진다. 다르면 종료 코드가 1
void expectState(const std::string& arg) {
    size_t sp = arg.find(' ');
    std::string key = arg.substr(0, sp), want = sp == std::string::npos ? "" : arg.substr(sp + 1), got = "?";
    std::string name;
    size_t co = key.find(':');
    if (co != std::string::npos) { name = key.substr(co + 1); key = key.substr(0, co); }
    const Widget* w = nullptr;
    for (auto& x : curScreen().widgets) if (x.name == name) w = &x;
    Rect r;
    if (w) { auto it = lvhost::coords().find(w->id); if (it != lvhost::coords().end()) r = it->second; }
    char b[96];
    if (key == "note") got = g.hist.cur >= 0 ? g.hist.revs[g.hist.cur].note : "";
    else if (key == "rev") got = std::to_string(g.hist.cur);
    else if (key == "revs") got = std::to_string(g.hist.revs.size());
    else if (key == "sel") { const Widget* sw = selWidget(); got = sw ? sw->name : "-"; }
    else if (key == "dirty") got = g.dirty ? "1" : "0";
    else if (key == "res") { snprintf(b, sizeof b, "%dx%d", g.doc.resW, g.doc.resH); got = b; }
    else if (key == "project") got = g.doc.project;
    else if (key == "round") got = g.doc.round ? "1" : "0";
    else if (key == "standards") got = std::to_string(standardCount());
    else if (key == "stdsel") got = std::to_string(g.stdSel);
    else if (key == "advice") { int n = 0; for (auto& i : g.issues) n += !i.error; got = std::to_string(n); }
    else if (key == "screen") got = std::to_string(g.screen);
    else if (key == "screens") got = std::to_string(g.doc.screens.size());
    else if (key == "sheet") got = curScreen().name;
    else if (key == "next") got = curScreen().next + "/" + std::to_string(curScreen().nextMs);
    else if (key == "widgets") got = std::to_string(curScreen().widgets.size());
    else if (key == "armed") got = std::to_string(g.armed);
    else if (key == "drag") got = std::to_string((int)g.drag);
    else if (key == "popup") got = std::to_string(ImGui::GetCurrentContext()->OpenPopupStack.Size);
    else if (key == "trace") got = g.trace ? "1" : "0";
    else if (key == "errors") { int e = 0; for (auto& i : g.issues) e += i.error; got = std::to_string(e); }
    else if (key == "ime") { HIMC ic = ImmGetContext(g_hwnd); got = ic ? "1" : "0"; if (ic) ImmReleaseContext(g_hwnd, ic); }
    else if (key == "fonts") { got.clear(); for (auto& [sz, cps] : fontgen::plan(g.doc).sizes) got += (got.empty() ? "" : ",") + std::to_string(sz) + ":" + std::to_string(cps.size()); }
    else if (key == "sim") got = g.sim ? "1" : "0";
    else if (key == "simsheet") got = g.sim ? g.doc.screens[lvhost::simScreen()].name : "-";
    else if (key == "simlog") got = std::to_string(lvhost::simLog().size());
    else if (key == "simtext") got = lvhost::simWantsText() ? "1" : "0";
    else if (key == "simlast") {   // 마지막 기록: 부품/사건/콜백/이동/값/횟수>1
        got = "-";
        if (!lvhost::simLog().empty()) { auto& e = lvhost::simLog().back(); got = e.name + "/" + (e.code < 0 ? "after" : eventKey(e.code)) + "/" + e.cb + "/" + e.go + "/" + e.value; }
    }
    else if (key == "images") got = std::to_string(g.doc.images.size());
    else if (key == "gen") got = std::to_string(g.gen.size());
    else if (key == "board") { got = "?"; for (int i = 0; i < kBoardFieldCount; i++) if (name == kBoardFields[i].key) got = std::to_string(g.doc.board.*kBoardFields[i].p); }
    else if (key == "view") { snprintf(b, sizeof b, "%g", g.viewK); got = b; }
    else if (key == "exists") got = w ? "1" : "0";
    else if (!w) got = "(no widget)";
    else if (key == "text") got = w->text;
    else if (key == "text2") got = w->text2;
    else if (key == "xy") { snprintf(b, sizeof b, "%d,%d", w->x, w->y); got = b; }
    else if (key == "wh") { snprintf(b, sizeof b, "%d,%d", w->w, w->h); got = b; }
    else if (key == "abs") { snprintf(b, sizeof b, "%d,%d,%d,%d", r.x1, r.y1, rw(r), rh(r)); got = b; }
    else if (key == "align") got = std::to_string(w->align);
    else if (key == "hidden") got = w->hidden ? "1" : "0";
    else if (key == "parent") { const Widget* p = findWidget(curScreen(), w->parent); got = p ? p->name : "-"; }
    else if (key == "index") got = std::to_string(widgetIndex(curScreen(), w->id));
    else if (key == "pressedbg") { const StyleVal& v = w->style[ST_PRESSED][SP_BG_COLOR]; got = v.set ? (v.v == 0x0D47A1 ? "original" : "changed") : "unset"; }
    else if (key == "events") got = std::to_string(w->events.size());
    else if (key == "motions") got = std::to_string(w->motions.size());
    else if (key == "image") { const auto* as = image::find(g.doc, w->text); got = as ? (*as)->file + " " + std::to_string((*as)->w) + "x" + std::to_string((*as)->h) : "-"; }
    else if (key == "font") {   // 실제로 쓰는 글꼴 이름과 크기
        int fs, ff;
        resolveFont(curScreen(), *w, ST_DEFAULT, fs, ff);
        const auto* fa = fontface::find(g.doc, ff);
        got = (fa ? (*fa)->name : std::string("기본")) + " " + std::to_string(fs);
    }
    else if (key.compare(0, 2, "x.") == 0) { Extra ex = w->x2; for (int i = 0; i < kExtraFieldCount; i++) if (key.substr(2) == kExtraFields[i].key) got = std::to_string(kExtraFields[i].get(ex)); }
    else if (key == "place") got = std::to_string(w->value);
    else if (key == "fit") got = std::to_string(w->minv);
    else if (key == "go") { got = w->events.empty() ? "-" : w->events[0].go; }
    else if (key == "checked") got = w->checked ? "1" : "0";
    else if (key.compare(0, 3, "st.") == 0) {   // 기본 상태의 속성 값
        for (int p = 0; p < SP_COUNT; p++)
            if (key.substr(3) == stylePropInfo(p).key) { const StyleVal& v = w->style[ST_DEFAULT][p]; got = v.set ? std::to_string(v.v) : "unset"; }
    }
    g_script.checks++;
    bool ok = got == want;
    if (!ok) g_script.fails++;
    if (g_script.log) { fprintf(g_script.log, "%s %s%s%s = '%s'%s\n", ok ? "PASS" : "FAIL", key.c_str(), name.empty() ? "" : ":", name.c_str(), got.c_str(), ok ? "" : (" (want '" + want + "')").c_str()); fflush(g_script.log); }
}

// @로 시작하면: 그림은 재생 파일 옆, tmp는 임시 폴더
std::string atScript(const std::string& f) {
    if (f.empty() || f[0] != '@') return f;
    if (f.compare(0, 4, "@tmp") == 0) {
        wchar_t t[MAX_PATH];
        GetTempPathW(MAX_PATH, t);
        return wideToUtf8(t) + "higenis_" + f.substr(1);
    }
    return g_script.dir + f.substr(1);
}

void stepScript() {
    ImGuiIO& io = ImGui::GetIO();
    if (g_script.wait > 0) { g_script.wait--; return; }
    while (g_script.at < g_script.lines.size()) {
        std::string ln = g_script.lines[g_script.at++];
        if (ln.empty() || ln[0] == '#') continue;
        size_t sp = ln.find(' ');
        std::string c = ln.substr(0, sp), a = sp == std::string::npos ? "" : ln.substr(sp + 1);
        if (c == "move") { float x = 0, y = 0; sscanf(a.c_str(), "%f %f", &x, &y); io.AddMousePosEvent(x, y); }
        else if (c == "dev") {   // 장치 좌표로 이동
            float x = 0, y = 0; sscanf(a.c_str(), "%f %f", &x, &y);
            ImVec2 p = canvasDevToScreen(x, y);
            io.AddMousePosEvent(p.x, p.y);
        }
        else if (c == "down") io.AddMouseButtonEvent(atoi(a.c_str()), true);
        else if (c == "up") io.AddMouseButtonEvent(atoi(a.c_str()), false);
        else if (c == "wheel") io.AddMouseWheelEvent(0, (float)atof(a.c_str()));
        else if (c == "keydown") io.AddKeyEvent(keyByName(a), true);
        else if (c == "keyup") io.AddKeyEvent(keyByName(a), false);
        else if (c == "text") io.AddInputCharactersUTF8(a.c_str());
        else if (c == "wait") g_script.wait = atoi(a.c_str());
        else if (c == "select") {
            for (auto& w : curScreen().widgets) if (w.name == a) selectWidget(w.id);
        }
        else if (c == "state") { g.editState = atoi(a.c_str()); g.needsBuild = true; }
        else if (c == "ime" && g_script.log) {
            HIMC ic = ImmGetContext(g_hwnd);
            fprintf(g_script.log, "ime %s: on=%d context=%d composing=%d deferred=%d want=%d\n", a.c_str(), g_imeOn, ic != nullptr, g_composing, (int)g_deferred.size(), io.WantTextInput);
            if (ic) ImmReleaseContext(g_hwnd, ic);
        }
        else if (c == "composestart") SendMessageW(g_hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
        else if (c == "click") {   // 실제 창 메시지로 누르기
            float x = 0, y = 0; sscanf(a.c_str(), "%f %f", &x, &y);
            LPARAM lp = MAKELPARAM((int)x, (int)y);
            SendMessageW(g_hwnd, WM_MOUSEMOVE, 0, lp);
            SendMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
            SendMessageW(g_hwnd, WM_LBUTTONUP, 0, lp);
        }
        else if (c == "standard") startStandard(atoi(a.c_str()));
        else if (c == "addstandard") addStandardSheet(atoi(a.c_str()));
        else if (c == "openstd") g.openStd = true;
        else if (c == "openabout") g.openAbout = true;
        else if (c == "openfonts") { g.openFonts = true; g.fontFor = g.sel; g.fontState = ST_DEFAULT; }
        else if (c == "stamp") g.stampT = g.now;
        else if (c == "scale") setScale(atoi(a.c_str()), canvasCenter());
        else if (c == "screen") switchScreen(atoi(a.c_str()));
        else if (c == "save") { std::string e; saveDocument(g.doc, utf8ToWide(atScript(a)), e); }
        else if (c == "load") { Document d; std::string e; if (loadDocument(d, utf8ToWide(atScript(a)), e)) { g.doc = d; g.screen = 0; g.sel = 0; g.hist.reset(d, 0, 0, "열기"); g.dirty = false; g.needsBuild = true; } else if (g_script.log) fprintf(g_script.log, "load failed: %s\n", e.c_str()); }
        else if (c == "pixels") {   // 미리보기 원본 픽셀(BGRA)을 그대로 저장
            if (FILE* f = _wfopen(utf8ToWide(a).c_str(), L"wb")) { fwrite(lvhost::pixels(), 4, (size_t)lvhost::width() * lvhost::height(), f); fclose(f); }
        }
        else if (c == "smooth") {   // 매끈하게 그린 픽셀(BGRA)
            if (FILE* f = _wfopen(utf8ToWide(a).c_str(), L"wb")) { if (lvhost::smoothOn()) fwrite(lvhost::smoothPixels(), 4, (size_t)lvhost::smoothWidth() * lvhost::smoothHeight(), f); fclose(f); }
        }
        else if (c == "setgo") {   // setgo 부품 시트
            size_t q = a.find(' ');
            for (auto& w : curScreen().widgets) if (w.name == a.substr(0, q) && !w.events.empty()) { w.events[0].go = a.substr(q + 1); commit("이동"); }
        }
        else if (c == "drop") {   // drop x y 파일
            float x = 0, y = 0; int used = 0;
            sscanf(a.c_str(), "%f %f %n", &x, &y, &used);
            dropFile(utf8ToWide(atScript(a.substr(used))), ImVec2(x, y));
        }
        else if (c == "setface") {   // setface 부품 글꼴키
            size_t q = a.find(' ');
            const int id = q == std::string::npos ? 0 : fontface::add(g.doc, a.substr(q + 1), 0, a.substr(q + 1));
            for (auto& w : curScreen().widgets) if (w.name == a.substr(0, q)) { w.style[ST_DEFAULT][SP_FACE] = {true, id}; commit("글꼴"); }
        }
        else if (c == "setx") {      // setx 부품 이름 값
            char n[64] = "", k[64] = ""; int v = 0;
            sscanf(a.c_str(), "%63s %63s %d", n, k, &v);
            for (auto& w : curScreen().widgets) if (w.name == n) for (int i = 0; i < kExtraFieldCount; i++) if (!strcmp(k, kExtraFields[i].key)) { kExtraFields[i].get(w.x2) = v; commit(k); }
        }
        else if (c == "pixelview") g.pixelView = atoi(a.c_str()) != 0;
        else if (c == "motiont") { g.motionT = atoi(a.c_str()); g.needsBuild = true; }   // 움직임 표의 바늘(ms, -1 끝)
        else if (c == "dump" && g_script.log) dumpState(g_script.log, a);
        else if (c == "expect") { expectState(a); continue; }
        else if (c == "shot") g_script.shot = utf8ToWide(atScript(a));
        else if (c == "quit") g_quit = true;
        return;   // 한 프레임에 한 줄
    }
    g_quit = true;
}

void renderFrame() {
    if (g_resizeW && g_resizeH) {
        release(g_rtv);
        g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
        g_resizeW = g_resizeH = 0;
        createRtv();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (g_script.on) {
        ImGui::GetIO().DeltaTime = 1.f / 60.f;   // 재생은 고정 시간 간격으로
        stepScript();
    }
    ImGui::NewFrame();
    uiFrame();
    ImGui::Render();
    const float cc[4] = {0xEC / 255.f, 0xE8 / 255.f, 0xDE / 255.f, 1.f};
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, cc);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (!g_script.shot.empty()) {
        bool ok = saveBackbuffer(g_script.shot);
        if (g_script.log) fprintf(g_script.log, "shot %s %s\n", wideToUtf8(g_script.shot).c_str(), ok ? "ok" : "FAILED");
        g_script.shot.clear();
    }
    HRESULT hr = g_swap->Present(g_script.on ? 0 : 1, 0);
    g_occluded = hr == DXGI_STATUS_OCCLUDED;
    std::vector<Deferred> later;
    later.swap(g_deferred);
    for (const Deferred& m : later) ImGui_ImplWin32_WndProcHandler(g_hwnd, m.msg, m.w, m.l);
    syncIme(ImGui::GetIO().WantTextInput);
}

std::wstring exeDir() {
    wchar_t b[MAX_PATH];
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    std::wstring s = b;
    return s.substr(0, s.find_last_of(L"\\/"));
}

std::string readAll(const std::wstring& p) {
    std::string d;
    if (FILE* f = _wfopen(p.c_str(), L"rb")) {
        char b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof b, f)) > 0) d.append(b, n);
        fclose(f);
    }
    return d;
}
}  // namespace

// ───────────────────────── 플랫폼 구현 ─────────────────────────
std::wstring utf8ToWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string wideToUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

void platformUploadCanvas(const uint32_t* px, int w, int h, int k, int slot) {
    if (!g_dev) return;
    Tex& T = g_texs[slot];
    const int tw = w * k, th = h * k;
    if (!T.tex || tw != T.w || th != T.h) {
        release(T.srv);
        release(T.tex);
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = tw; d.Height = th; d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &T.tex))) return;
        g_dev->CreateShaderResourceView(T.tex, nullptr, &T.srv);
        T.w = tw; T.h = th;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->Map(T.tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    for (int y = 0; y < h; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)m.pData + (size_t)y * k * m.RowPitch);
        const uint32_t* src = px + (size_t)y * w;
        if (k == 1) {
            memcpy(row, src, (size_t)w * 4);
        } else {
            for (int x = 0; x < w; x++)
                for (int i = 0; i < k; i++) row[x * k + i] = src[x];
            for (int i = 1; i < k; i++) memcpy((uint8_t*)row + (size_t)i * m.RowPitch, row, (size_t)tw * 4);
        }
    }
    g_ctx->Unmap(T.tex, 0);
}
void* platformCanvasTexture(int slot) { return g_texs[slot].srv; }
void platformSetTitle(const std::wstring& t) { SetWindowTextW(g_hwnd, t.c_str()); }
void platformRequestClose() { g_quit = true; }
std::wstring platformAssetsDir() { return exeDir() + L"\\assets"; }
bool replaceFile(const std::wstring& from, const std::wstring& to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
std::string fontLicenseText() { return g_license; }

bool platformOpenImage(std::wstring& path) {
    wchar_t buf[2048] = {};
    OPENFILENAMEW o = {sizeof o};
    o.hwndOwner = g_hwnd;
    o.lpstrFilter = L"그림 (*.png *.jpg *.bmp *.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0모든 파일\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = 2048;
    o.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&o)) return false;
    path = buf;
    return true;
}
void platformOpenNotices() {
    ShellExecuteW(g_hwnd, L"open", (platformAssetsDir() + L"\\THIRD_PARTY_NOTICES.txt").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
bool platformOpenFont(std::wstring& path) {
    wchar_t buf[2048] = {};
    OPENFILENAMEW o = {sizeof o};
    o.hwndOwner = g_hwnd;
    o.lpstrFilter = L"글꼴 (*.ttf *.otf *.ttc)\0*.ttf;*.otf;*.ttc\0모든 파일\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = 2048;
    o.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&o)) return false;
    path = buf;
    return true;
}
static bool fileDialog(std::wstring& path, bool save) {
    wchar_t buf[2048] = {};
    wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    OPENFILENAMEW o = {sizeof o};
    o.hwndOwner = g_hwnd;
    o.lpstrFilter = save ? L"Higenis Studio 도면 (*.hg)\0*.hg\0" : L"Higenis Studio 도면 (*.hg, *.vellum)\0*.hg;*.vellum\0모든 파일\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = 2048;
    o.lpstrDefExt = L"hg";
    o.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return false;
    path = buf;
    return true;
}
bool platformSaveDialog(std::wstring& path) { return fileDialog(path, true); }
bool platformOpenDialog(std::wstring& path) { path.clear(); return fileDialog(path, false); }
bool platformPickFolder(std::wstring& path, const wchar_t* title) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(title);
    bool ok = false;
    if (SUCCEEDED(dlg->Show(g_hwnd))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; CoTaskMemFree(p); ok = true; }
            it->Release();
        }
    }
    dlg->Release();
    return ok;
}

// ───────────────────────── 시작 ─────────────────────────
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring openPath, exportDir, scriptPath, logPath;
    int winW = 1520, winH = 940;
    float dpi = 0;
    bool arduino = false;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--export") exportDir = next();
        else if (a == L"--arduino") { exportDir = next(); arduino = true; }
        else if (a == L"--script") scriptPath = next();
        else if (a == L"--log") logPath = next();
        else if (a == L"--dpi") dpi = (float)_wtof(next().c_str());
        else if (a == L"--size") swscanf_s(next().c_str(), L"%dx%d", &winW, &winH);
        else openPath = a;
    }
    LocalFree(argv);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    fontface::setDir(exeDir() + L"\\assets\\fonts");
    Document doc = makeDemoDocument();
    if (!openPath.empty()) {
        std::string err;
        if (!loadDocument(doc, openPath, err)) {
            if (exportDir.empty()) MessageBoxW(nullptr, (L"도면을 열 수 없습니다.\n" + openPath + L"\n" + utf8ToWide(err)).c_str(), L"Higenis Studio", MB_ICONWARNING);
            return 2;
        }
    }
    std::wstring assets = exeDir() + L"\\assets";
    const bool fontOk = fontgen::load(assets + L"\\fonts\\IBMPlexSansKR-Regular.ttf");
    fontface::setDir(assets + L"\\fonts");
    fontgen::loadView(assets + L"\\fonts\\Montserrat-Medium.ttf", assets + L"\\fonts\\HigenisSymbols.ttf");
    g_license = readAll(assets + L"\\fonts\\OFL-plexkr.txt");
    if (!exportDir.empty()) {   // 창 없이 내보내기
        if (!fontOk) return 4;  // 한글 글꼴 없이 내보내면 장치에서 글자가 빈다
        if (arduino) {
            const std::vector<GenFile> fs = generateArduino(doc, "A");
            int kept = 0;
            const std::wstring sk = exportDir + L"\\" + utf8ToWide(sketchName(doc));
            if (writeFiles(fs, sk, kept) != (int)fs.size()) return 3;
            removeStale(fs, sk);
            return 0;
        }
        const std::vector<GenFile> fs = generateCode(doc, "A");
        int kept = 0;
        if (writeFiles(fs, exportDir, kept) != (int)fs.size()) return 3;
        removeStale(fs, exportDir);
        return 0;
    }

    if (!fontOk || GetFileAttributesW((assets + L"\\fonts\\BarlowCondensed-Regular.ttf").c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, (L"글꼴을 찾을 수 없습니다. HigenisStudio.exe 옆에 assets 폴더가 있어야 합니다.\n" + assets).c_str(), L"Higenis Studio", MB_ICONERROR);
        return 1;
    }

    const bool scripted = !scriptPath.empty();
    if (scripted) {
        g_script.on = true;
        const std::string sp8 = wideToUtf8(scriptPath);
        g_script.dir = sp8.substr(0, sp8.find_last_of("\\/") + 1);
        std::string t = readAll(scriptPath);
        size_t st = 0;
        while (st < t.size()) {
            size_t e = t.find('\n', st);
            if (e == std::string::npos) e = t.size();
            std::string ln = t.substr(st, e - st);
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            g_script.lines.push_back(ln);
            st = e + 1;
        }
        if (!logPath.empty()) g_script.log = _wfopen(logPath.c_str(), L"wb");
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    if (dpi <= 0) dpi = scripted ? 1.f : ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    g.S = dpi;

    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    WNDCLASSEXW wc = {sizeof wc, CS_CLASSDC, wndProc, 0, 0, inst, icon, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"HigenisStudio", icon};
    RegisterClassExW(&wc);
    RECT rc = {0, 0, scripted ? winW : (LONG)(winW * dpi), scripted ? winH : (LONG)(winH * dpi)};
    DWORD style = scripted ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    if (!scripted) {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        rc.right = std::min<LONG>(rc.right, wa.right - wa.left - 40);
        rc.bottom = std::min<LONG>(rc.bottom, wa.bottom - wa.top - 60);
    }
    AdjustWindowRect(&rc, style, FALSE);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Higenis Studio", style, scripted ? 0 : CW_USEDEFAULT, scripted ? 0 : CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    DragAcceptFiles(g_hwnd, TRUE);
    // 제목 막대도 트레이싱지 색으로 (Windows 11)
    COLORREF cap = RGB(0xEC, 0xE8, 0xDE), txt = RGB(0x2A, 0x29, 0x26), brd = RGB(0x8C, 0x87, 0x7C);
    DwmSetWindowAttribute(g_hwnd, 35, &cap, sizeof cap);
    DwmSetWindowAttribute(g_hwnd, 36, &txt, sizeof txt);
    DwmSetWindowAttribute(g_hwnd, 34, &brd, sizeof brd);

    if (!createDevice(g_hwnd)) {
        MessageBoxW(nullptr, L"Direct3D 11을 시작할 수 없습니다.", L"Higenis Studio", MB_ICONERROR);
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    uiInit(dpi, wideToUtf8(assets));

    if (scripted) lvhost::manualClock();
    lvhost::init(doc.resW, doc.resH, doc.depth);
    g.doc = doc;
    g.path = openPath;
    g.hist.reset(g.doc, 0, 0, openPath.empty() ? "예제 도면" : "도면 열기");

    if (!scripted) { ShowWindow(g_hwnd, SW_SHOWDEFAULT); UpdateWindow(g_hwnd); }
    g_ready = true;

    // 할 일이 없으면 잔다: 입력이 끝난 뒤 0.4초, 애니메이션 중에만 다시 그린다
    ULONGLONG last = GetTickCount64();
    while (!g_quit) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
            last = GetTickCount64();
            if (m.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;
        if (!g_script.on) {
            bool busy = GetTickCount64() - last < 400 || !g_deferred.empty() || uiWantsAnimation() || g.needsBuild || g.pending;
            if (!busy) {
                bool anim = lvhost::animating() || lvhost::simMoving(), caret = ImGui::GetIO().WantTextInput;
                DWORD wait = anim ? (g.sim ? 16 : 33) : caret ? 250 : INFINITE;
                DWORD r = MsgWaitForMultipleObjectsEx(0, nullptr, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (r == WAIT_TIMEOUT && !anim && !caret) continue;
                if (r != WAIT_TIMEOUT) continue;   // 메시지부터 처리
            }
            if (IsIconic(g_hwnd)) { WaitMessage(); continue; }
            if (g_occluded && g_swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) { Sleep(30); continue; }
        }
        renderFrame();
    }

    if (g_script.log) {
        fprintf(g_script.log, "RESULT %d checks, %d fails\n", g_script.checks, g_script.fails);
        fclose(g_script.log);
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    for (Tex& t : g_texs) { release(t.srv); release(t.tex); }
    release(g_rtv); release(g_swap); release(g_ctx); release(g_dev);
    DestroyWindow(g_hwnd);
    CoUninitialize();
    return g_script.fails ? 1 : 0;
}
