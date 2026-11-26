// 내보내기: 도면 → LVGL v9 C 코드
#include "app.h"
#include "fontgen.h"
#include "fontface.h"
#include "image.h"
#include "higenis_fx_text.h"

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>

// 문자열 리터럴. LVGL 기호는 매크로로 쓴다: LV_SYMBOL_HOME " 집"
// 주석에 넣는 글: 주석을 끊는 글자와 줄바꿈을 없애고 길이를 줄인다
static std::string cmt(const std::string& s, int maxChars = 60) {
    std::string r;
    int n = 0;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if ((c & 0xC0) != 0x80 && ++n > maxChars) { r += "..."; break; }
        if (c < 0x20 || c == 0x7F) { r += ' '; continue; }
        if (!r.empty() && ((r.back() == '*' && c == '/') || (r.back() == '/' && c == '*'))) r += ' ';
        r += (char)c;
    }
    return r;
}
static std::string cstr(const std::string& s) {
    std::string o;
    bool open = false;
    for (uint32_t cp : fontgen::decode(s)) {
        if (const SymbolInfo* y = symbolOf(cp)) {
            if (open) { o += '"'; open = false; }
            if (!o.empty()) o += ' ';
            o += y->macro;
            continue;
        }
        if (!open) {
            if (!o.empty()) o += ' ';
            o += '"';
            open = true;
        }
        if (cp == '\\') o += "\\\\";
        else if (cp == '"') o += "\\\"";
        else if (cp == '\n') o += "\\n";
        else if (cp == '\r') o += "\\r";
        else if (cp == '\t') o += "\\t";
        else if (cp == '?') o += "\\?";   // 트라이그래프 방지
        else o += fontgen::encode(cp);
    }
    if (open) o += '"';
    return o.empty() ? "\"\"" : o;
}
static std::string hex(int v) {
    char b[16];
    snprintf(b, sizeof b, "0x%06X", v & 0xFFFFFF);
    return b;
}
static std::string upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

// 글꼴 파일: 쓴 글자만 담은 4bpp 비트맵
template <class GlyphFn>
static std::string fontTables(const std::string& n, const std::vector<uint32_t>& cps, GlyphFn glyphOf) {
    std::string c = "static const uint8_t bitmap_" + n + "[] = {\n", dsc, list;
    char b[256];
    size_t idx = 0;
    int col = 0;
    for (uint32_t cp : cps) {
        const fontgen::Glyph& g = glyphOf(cp);
        snprintf(b, sizeof b, "    /* U+%04X %s */\n   ", cp, cp == '/' || cp == '*' || cp == '\\' ? "" : fontgen::encode(cp).c_str());
        c += b;
        for (size_t i = 0; i < g.bits.size(); i++) {
            snprintf(b, sizeof b, " 0x%02x,", g.bits[i]);
            c += b;
            if (i % 16 == 15 && i + 1 < g.bits.size()) c += "\n   ";
        }
        c += "\n";
        snprintf(b, sizeof b, "    {.bitmap_index = %u, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},\n", (unsigned)idx, g.adv16, g.w, g.h, g.ox, g.oy);
        dsc += b;
        snprintf(b, sizeof b, " 0x%x,", cp - cps.front());
        if (col++ % 12 == 0) list += "\n   ";
        list += b;
        idx += g.bits.size();
    }
    if (idx == 0) c += "    0x00\n";
    c += "};\n\nstatic const lv_font_fmt_txt_glyph_dsc_t glyphs_" + n + "[] = {\n    {.bitmap_index = 0}, /* 0번은 비워 둔다 */\n" + dsc + "};\n\n";
    c += "static const uint16_t unicode_" + n + "[] = {" + list + "\n};\n\n";
    snprintf(b, sizeof b, "static const lv_font_fmt_txt_cmap_t cmaps_%s[] = {{\n    .range_start = 0x%x, .range_length = %u, .glyph_id_start = 1,\n", n.c_str(), cps.front(),
             (unsigned)(uint16_t)(cps.back() - cps.front() + 1));
    c += b;
    c += "    .unicode_list = unicode_" + n + ", .glyph_id_ofs_list = NULL, .list_length = " + std::to_string(cps.size()) + ",\n    .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,\n}};\n\n";
    c += "static const lv_font_fmt_txt_dsc_t dsc_" + n + " = {\n    .glyph_bitmap = bitmap_" + n + ",\n    .glyph_dsc = glyphs_" + n + ",\n    .cmaps = cmaps_" + n + ",\n    .cmap_num = 1,\n    .bpp = 4,\n};\n\n";
    return c;
}
static std::string fontObject(const std::string& decl, const std::string& n, int lh, int bl, const std::string& fallback) {
    return decl + " = {\n    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n    .line_height = " + std::to_string(lh) + ",\n    .base_line = " + std::to_string(bl) +
           ",\n#if LV_VERSION_CHECK(9, 3, 0)\n    .static_bitmap = 1,\n#endif\n    .dsc = &dsc_" + n + ",\n" + (fallback.empty() ? "" : "    .fallback = " + fallback + ",\n") + "};\n";
}
static std::string fontRef(const fontgen::Plan& pl, int size, int face) {
    if (pl.usesFace(face, size)) return "ui_face" + std::to_string(face) + "_" + std::to_string(size);
    return (pl.uses(size) ? "ui_font_" : "lv_font_montserrat_") + std::to_string(size);
}
static void generateFont(const Document& d, const fontgen::Plan& pl, const std::string& stamp, std::vector<GenFile>& files) {
    std::string h, c, notice;
    char b[256];
    if (!pl.sizes.empty())
        notice = " * 글리프는 IBM Plex Sans KR에서 뽑았습니다.\n"
                 " * Copyright (c) 2017 IBM Corp. with Reserved Font Name \"Plex\"\n"
                 " * SIL Open Font License 1.1 (전문: ui_font_OFL.txt)\n";
    int total = 0;
    for (auto& [size, cps] : pl.sizes) { total += pl.bytes(size); (void)cps; }
    for (auto& [k, cps] : pl.faces) total += fontgen::faceBytes(d, k.first, k.second, cps);
    for (auto& f : d.fonts) {
        bool used = false;
        for (auto& [k, cps] : pl.faces) if (k.first == f->id) used = true;
        if (!used) continue;
        const bool own = !f->key.empty() && f->key[0] == '@';
        notice += " * 글꼴 " + cmt(f->name) + (own ? " (SIL Open Font License 1.1)\n" : " — 배포하기 전에 이 글꼴의 사용 조건을 확인하세요\n");
    }
    h += "/**\n * ui_font.h — Higenis Studio에서 내보냄 " + stamp + "\n" + notice + " */\n#ifndef UI_FONT_H\n#define UI_FONT_H\n\n#include \"lvgl.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
    h += "/* 쓰기 전에 ui_font_init()을 부른다(여러 번 불러도 된다) */\n";
    for (auto& [size, cps] : pl.sizes) { h += "extern lv_font_t ui_font_" + std::to_string(size) + ";\n"; (void)cps; }
    for (auto& [k, cps] : pl.faces) { h += "extern const lv_font_t " + fontRef(pl, k.second, k.first) + ";\n"; (void)cps; }
    h += "\nvoid ui_font_init(void);\n\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n";

    snprintf(b, sizeof b, " · 약 %.1f KB\n", total / 1024.0);
    c += "/**\n * ui_font.c — Higenis Studio에서 내보냄 " + stamp + b + notice + " */\n#include \"ui_font.h\"\n";
    int lh, bl;
    for (auto& [size, cps] : pl.sizes) {
        const int sz = size;
        const std::string n = std::to_string(size);
        snprintf(b, sizeof b, "\n/* ───── %d px · %d자 · %d 바이트 ───── */\n", size, (int)cps.size(), pl.bytes(size));
        c += b + fontTables(n, cps, [&](uint32_t cp) -> const fontgen::Glyph& { return fontgen::glyph(sz, cp); });
        fontgen::metrics(size, lh, bl);
        c += fontObject("static const lv_font_t font_kr_" + n, n, lh, bl, "") + "\nlv_font_t ui_font_" + n + ";\n";
    }
    for (auto& [k, cps] : pl.faces) {
        const auto* f = fontface::find(d, k.first);
        if (!f) continue;
        const int sz = k.second;
        const std::string n = "f" + std::to_string(k.first) + "_" + std::to_string(sz);
        snprintf(b, sizeof b, "\n/* ───── %s · %d px · %d자 ───── */\n", cmt((*f)->name, 40).c_str(), sz, (int)cps.size());
        c += b + fontTables(n, cps, [&](uint32_t cp) -> const fontgen::Glyph& { return fontface::glyph(**f, sz, cp); });
        fontface::metrics(**f, sz, lh, bl);
        c += fontObject("const lv_font_t " + fontRef(pl, sz, k.first), n, lh, bl, "&" + fontRef(pl, sz, 0));
    }
    c += "\nvoid ui_font_init(void)\n{\n";
    for (auto& [size, cps] : pl.sizes) {
        const std::string n = std::to_string(size);
        c += "    ui_font_" + n + " = lv_font_montserrat_" + n + ";\n    ui_font_" + n + ".fallback = &font_kr_" + n + ";\n";
        (void)cps;
    }
    c += "}\n";
    files.push_back({"ui_font.c", c});
    files.push_back({"ui_font.h", h});
    if (!pl.sizes.empty()) files.push_back({"ui_font_OFL.txt", fontLicenseText()});
}

// 그림: 플래시에 둘 것은 C 배열, SD 카드에 둘 것은 LVGL .bin 파일
static void generateImages(const Document& d, const std::string& stamp, std::vector<GenFile>& files) {
    std::set<std::string> done;
    for (auto& s : d.screens)
        for (auto& w : s.widgets) {
            const auto* a = w.type == WType::Image ? image::find(d, w.text) : nullptr;
            if (!a) continue;
            int W, H;
            image::size(d, w, W, H);
            const auto v = image::variant(*a, W, H, image::fit(w), d.depth, w.x2.img);
            const std::string id = image::ident(d, w);
            if (image::onSd(w)) {
                const std::string path = "sd" + image::sdPath(d, w);
                if (!done.insert(path).second) continue;
                const std::vector<uint8_t> bin = v->bin();
                files.push_back({path, std::string(bin.begin(), bin.end()), true});
                continue;
            }
            if (!done.insert(id).second) continue;
            std::string c = "/**\n * " + id + ".c — Higenis Studio에서 내보냄 " + stamp + "\n * " + cmt((*a)->file) + " · " + std::to_string(W) + "x" + std::to_string(H) + " · " + v->cfName() + " · " +
                            std::to_string(v->data.size()) + " 바이트\n */\n#include \"lvgl.h\"\n\nstatic const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t map[] = {";
            static const char* hx = "0123456789abcdef";
            c.reserve(c.size() + v->data.size() * 5 + 1024);
            for (size_t i = 0; i < v->data.size(); i++) {
                if (i % 20 == 0) c += "\n    ";
                c += "0x"; c += hx[v->data[i] >> 4]; c += hx[v->data[i] & 15]; c += ',';
            }
            c += "\n};\n\nconst lv_image_dsc_t " + id + " = {\n    .header.magic = LV_IMAGE_HEADER_MAGIC,\n    .header.cf = " + v->cfName() + ",\n    .header.w = " + std::to_string(W) +
                 ",\n    .header.h = " + std::to_string(H) + ",\n    .header.stride = " + std::to_string(v->stride) + ",\n    .data_size = sizeof(map),\n    .data = map,\n};\n";
            files.push_back({id + ".c", c});
        }
}

int writeFiles(const std::vector<GenFile>& files, const std::wstring& dir, int& kept) {
    int ok = 0;
    kept = 0;
    for (auto& f : files) {
        std::wstring path = dir + L"\\" + utf8ToWide(f.name);
        for (wchar_t& ch : path) if (ch == L'/') ch = L'\\';
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        if (f.keep && std::filesystem::exists(path, ec)) { kept++; ok++; continue; }
        FILE* fp = _wfopen(path.c_str(), L"wb");
        if (!fp) continue;
        const bool w = fwrite(f.text.data(), 1, f.text.size(), fp) == f.text.size();
        if (fclose(fp) == 0 && w) ok++;
    }
    return ok;
}

// 머리말에 이 프로그램의 표시가 있는가 (옛 VELLUM 표시 포함) (사람이 만든 파일은 건드리지 않으려고)
static bool madeByUs(const std::filesystem::path& p) {
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    char b[640];
    const size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    const std::string h(b, n);
    for (const char* m : {"Higenis Studio에서 내보냄", "Higenis Studio가 만든 파일", "VELLUM에서 내보냄", "VELLUM이 만든 파일"})
        if (h.find(m) != std::string::npos) return true;
    return false;
}

int removeStale(const std::vector<GenFile>& files, const std::wstring& dir, std::vector<std::string>* gone) {
    auto lower = [](std::wstring s) { for (wchar_t& c : s) c = (wchar_t)towlower(c); return s; };
    std::set<std::wstring> now;
    for (auto& f : files) {
        std::wstring n = utf8ToWide(f.name);
        for (wchar_t& ch : n) if (ch == L'/') ch = L'\\';
        now.insert(lower(n));
    }
    std::error_code ec;
    // 예전(VELLUM) 스케치의 .ino는 "vellum_port.h"를 부른다. .ino는 사람 몫이라 고치지 않고, 그 이름을 새 머리 파일로 잇는다
    if (now.count(L"higenis_port.h")) {
        for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (lower(it->path().extension().wstring()) != L".ino") continue;
            std::ifstream in(it->path(), std::ios::binary);
            const std::string ino((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (ino.find("vellum_port.h") == std::string::npos) continue;
            std::ofstream(std::filesystem::path(dir) / L"vellum_port.h", std::ios::binary)
                << "/* Higenis Studio가 만든 파일입니다. 예전 이름을 쓰는 .ino를 위해 남겨 둡니다. */\n#include \"higenis_port.h\"\n";
            now.insert(L"vellum_port.h");
            break;
        }
        ec.clear();
    }
    std::wstring list;   // SHFileOperation: 널로 나누고 널 두 개로 끝낸다
    int n = 0;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe)) continue;
        const std::wstring ext = lower(it->path().extension().wstring());
        if (ext != L".c" && ext != L".h" && ext != L".cpp") continue;   // .ino·.txt·그림 원본은 그대로
        if (now.count(lower(it->path().filename().wstring()))) continue;
        if (!madeByUs(it->path())) continue;
        list += it->path().wstring();
        list.push_back(L'\0');
        if (gone) gone->push_back(it->path().filename().u8string());
        n++;
    }
    if (!n) return 0;
    list.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = list.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) {
        if (gone) gone->clear();
        return 0;
    }
    return n;
}

// 시트 이름 → 겹치지 않는 파일/함수 이름 ("ui"는 ui.h와 부딪히므로 피한다)
static std::vector<std::string> screenIdents(const Document& d) {
    std::vector<std::string> out;
    std::set<std::string> used = {"ui", "ui_font", "lvgl", "lv"};
    for (auto& s : d.screens) {
        std::string base = sanitizeIdent(s.name);
        if (base.empty()) base = "screen_" + std::to_string(out.size() + 1);
        std::string n = base;
        for (int i = 2; used.count(n); i++) n = base + "_" + std::to_string(i);
        used.insert(n);
        out.push_back(n);
    }
    return out;
}

std::vector<GenFile> generateCode(const Document& d, const std::string& rev) {
    const std::vector<std::string> idents = screenIdents(d);
    const fontgen::Plan plan = fontgen::plan(d);
    std::vector<GenFile> files;
    std::set<int> fonts;
    for (auto& [size, cps] : plan.sizes) { fonts.insert(size); (void)cps; }
    for (auto& [k, cps] : plan.faces) { fonts.insert(k.second); (void)cps; }
    bool usesFx = false;
    std::set<std::string> callbacks;
    const std::string stamp = todayStamp();
    const char* fmt = d.depth == 16 ? "RGB565" : "XRGB8888";

    for (size_t si = 0; si < d.screens.size(); si++) {
        const Screen& s = d.screens[si];
        const std::string sn = idents[si];
        auto var = [&](int id) -> std::string {
            if (id == 0) return "scr";
            const Widget* w = findWidget(s, id);
            if (!w) return "scr";
            std::string n = sanitizeIdent(w->name);
            return sn + "_" + (n.empty() ? "w" + std::to_string(id) : n);
        };

        std::string h, c;
        char b[512];
        snprintf(b, sizeof b, "%02d", (int)si + 1);
        const std::string where = " * 도면 " + cmt(d.project) + " · 시트 " + b + " " + cmt(s.name) + " · 개정 " + rev;
        h += "/**\n * " + sn + ".h — Higenis Studio에서 내보냄 " + stamp + "\n" + where + "\n */\n";
        h += "#ifndef UI_" + upper(sn) + "_H\n#define UI_" + upper(sn) + "_H\n\n#include \"lvgl.h\"\n\n";
        h += "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
        h += "lv_obj_t * " + sn + "_create(void);\n\n";
        for (auto& w : s.widgets) h += "extern lv_obj_t * " + var(w.id) + ";\n";

        std::set<std::string> localCb;
        for (auto& w : s.widgets)
            for (auto& e : w.events)
                if (validIdent(e.cb)) localCb.insert(e.cb);
        if (!localCb.empty()) {
            h += "\n/* 주기: 아래 콜백은 사용자 코드에서 구현 */\n";
            for (auto& cb : localCb) h += "void " + cb + "(lv_event_t * e);\n";
        }
        h += "\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n";

        snprintf(b, sizeof b, " · %dx%d %s\n */\n", d.resW, d.resH, fmt);
        c += "/**\n * " + sn + ".c — Higenis Studio에서 내보냄 " + stamp + "\n" + where + b;
        // 이 시트가 한글 글꼴을 쓰는가: 기본 글꼴을 바꿨거나, 글꼴 크기를 지정한 부품이 있거나
        bool usesFont = plan.base[si];
        for (auto& w : s.widgets)
            for (int st = 0; st < ST_COUNT; st++)
                if (w.style[st][SP_TEXT_FONT].set || w.style[st][SP_FACE].set) {
                    int fs, ff;
                    resolveFont(s, w, st, fs, ff);
                    if (plan.usesFace(ff, fs) || plan.uses(fs)) usesFont = true;
                }
        bool sheetFx = false;
        for (auto& w : s.widgets) if (w.type == WType::Label && w.x2.fx && w.x2.longMode != 2) sheetFx = usesFx = true;
        c += "#include \"" + sn + ".h\"\n";
        if (usesFont) c += "#include \"ui_font.h\"\n";
        if (sheetFx) c += "#include \"higenis_fx.h\"\n";
        c += "\n";
        std::set<int> targets;
        for (auto& w : s.widgets)
            for (auto& e : w.events) {
                const int t = e.go.empty() ? -1 : screenByName(d, e.go);
                if (t >= 0 && t != (int)si) targets.insert(t);
            }
        for (int t : targets) c += "#include \"" + idents[t] + ".h\"\n";
        if (!targets.empty()) c += "\n";
        for (auto& w : s.widgets) c += "lv_obj_t * " + var(w.id) + ";\n";
        for (int t : targets)
            c += "\nstatic void go_to_" + idents[t] + "(lv_event_t * e)\n{\n    LV_UNUSED(e);\n    lv_screen_load_anim(" + idents[t] +
                 "_create(), LV_SCR_LOAD_ANIM_NONE, 0, 0, true); /* 새로 만들고 지금 화면은 지운다 */\n}\n";
        c += "\nlv_obj_t * " + sn + "_create(void)\n{\n";
        if (usesFont) c += "    ui_font_init();\n\n";
        c += "    lv_obj_t * scr = lv_obj_create(NULL);\n";
        if (plan.base[si]) c += "    lv_obj_set_style_text_font(scr, &ui_font_14, 0);\n";
        if (s.bgSet) c += "    lv_obj_set_style_bg_color(scr, lv_color_hex(" + hex(s.bg) + "), 0);\n";

        int num = 0;
        for (auto& w : s.widgets) {
            num++;
            const TypeInfo& ti = typeInfo(w.type);
            const std::string v = var(w.id);
            snprintf(b, sizeof b, "\n    /* %02d %s · %s */\n", num, cmt(w.name).c_str(), ti.key);
            c += b;
            c += "    " + v + " = " + ti.creator + "(" + var(w.parent) + ");\n";
            switch (w.type) {
            case WType::Label: {
                c += "    lv_label_set_text(" + v + ", " + cstr(w.text) + ");\n";
                static const char* lm[] = {"WRAP", "DOT", "SCROLL_CIRCULAR", "CLIP"};
                if (w.x2.longMode) c += "    lv_label_set_long_mode(" + v + ", LV_LABEL_LONG_" + lm[w.x2.longMode & 3] + ");\n";
                if (w.x2.fx && w.x2.longMode != 2) {
                    static const char* kind[] = {"", "그림자", "외곽선", "번짐"};
                    c += "    higenis_fx(" + v + ", " + std::to_string(w.x2.fx) + ", " + std::to_string(w.x2.fxSize) + ", " + std::to_string(w.x2.fxDx) + ", " + std::to_string(w.x2.fxDy) + ", " + hex(w.x2.fxColor) + ", " +
                         std::to_string(w.x2.fxOpa) + "); /* " + kind[w.x2.fx & 3] + " */\n";
                }
                break;
            }
            case WType::Button:
                c += "    {\n        lv_obj_t * label = lv_label_create(" + v + ");\n";
                c += "        lv_label_set_text(label, " + cstr(w.text) + ");\n        lv_obj_center(label);\n    }\n";
                break;
            case WType::Switch:
                if (w.checked) c += "    lv_obj_add_state(" + v + ", LV_STATE_CHECKED);\n";
                break;
            case WType::Checkbox:
                c += "    lv_checkbox_set_text(" + v + ", " + cstr(w.text) + ");\n";
                if (w.checked) c += "    lv_obj_add_state(" + v + ", LV_STATE_CHECKED);\n";
                break;
            case WType::Slider:
                c += "    lv_slider_set_range(" + v + ", " + std::to_string(w.minv) + ", " + std::to_string(w.maxv) + ");\n";
                c += "    lv_slider_set_value(" + v + ", " + std::to_string(w.value) + ", LV_ANIM_OFF);\n";
                break;
            case WType::Bar:
                c += "    lv_bar_set_range(" + v + ", " + std::to_string(w.minv) + ", " + std::to_string(w.maxv) + ");\n";
                c += "    lv_bar_set_value(" + v + ", " + std::to_string(w.value) + ", LV_ANIM_OFF);\n";
                break;
            case WType::Arc:
                c += "    lv_arc_set_range(" + v + ", " + std::to_string(w.minv) + ", " + std::to_string(w.maxv) + ");\n";
                c += "    lv_arc_set_value(" + v + ", " + std::to_string(w.value) + ");\n";
                if (w.checked) {
                    c += "    lv_obj_remove_style(" + v + ", NULL, LV_PART_KNOB); /* 게이지: 손잡이 없음 */\n";
                    c += "    lv_obj_remove_flag(" + v + ", LV_OBJ_FLAG_CLICKABLE);\n";
                }
                break;
            case WType::Dropdown: c += "    lv_dropdown_set_options(" + v + ", " + cstr(w.text2) + ");\n"; break;
            case WType::Roller:
                c += "    lv_roller_set_options(" + v + ", " + cstr(w.text2) + ", LV_ROLLER_MODE_NORMAL);\n";
                if (w.value > 0) c += "    lv_roller_set_selected(" + v + ", " + std::to_string(w.value) + ", LV_ANIM_OFF);\n";
                break;
            case WType::Textarea:
                if (w.checked) c += "    lv_textarea_set_one_line(" + v + ", true);\n";
                if (!w.text2.empty()) c += "    lv_textarea_set_placeholder_text(" + v + ", " + cstr(w.text2) + ");\n";
                if (!w.text.empty()) c += "    lv_textarea_set_text(" + v + ", " + cstr(w.text) + ");\n";
                break;
            case WType::Led:
                c += "    lv_led_set_color(" + v + ", lv_color_hex(" + hex(w.color) + "));\n";
                c += std::string("    ") + (w.checked ? "lv_led_on(" : "lv_led_off(") + v + ");\n";
                break;
            case WType::Image:
                if (const auto* as = image::find(d, w.text)) {
                    (void)as;
                    const std::string id = image::ident(d, w);
                    if (image::onSd(w)) c += "    lv_image_set_src(" + v + ", " + cstr("S:" + image::sdPath(d, w)) + "); /* SD 카드 */\n";
                    else c += "    LV_IMAGE_DECLARE(" + id + ");\n    lv_image_set_src(" + v + ", &" + id + ");\n";
                } else if (image::onSd(w) && !w.text2.empty()) {
                    c += "    lv_image_set_src(" + v + ", " + cstr("S:" + image::sdPath(d, w)) + "); /* SD 카드에 이미 있는 파일 */\n";
                } else if (validIdent(w.text)) {
                    c += "    LV_IMAGE_DECLARE(" + w.text + ");\n";
                    c += "    lv_image_set_src(" + v + ", &" + w.text + ");\n";
                } else {
                    c += "    /* TODO: lv_image_set_src(" + v + ", &이미지_소스); */\n";
                }
                break;
            default: break;
            }
            if (w.autoSize) c += "    lv_obj_set_size(" + v + ", LV_SIZE_CONTENT, LV_SIZE_CONTENT);\n";
            else c += "    lv_obj_set_size(" + v + ", " + std::to_string(w.w) + ", " + std::to_string(w.h) + ");\n";
            if (w.align == 0) c += "    lv_obj_set_pos(" + v + ", " + std::to_string(w.x) + ", " + std::to_string(w.y) + ");\n";
            else c += "    lv_obj_align(" + v + ", " + alignLv(w.align) + ", " + std::to_string(w.x) + ", " + std::to_string(w.y) + ");\n";
            if (w.hidden) c += "    lv_obj_add_flag(" + v + ", LV_OBJ_FLAG_HIDDEN);\n";
            if (wantsClick(w)) c += "    lv_obj_add_flag(" + v + ", LV_OBJ_FLAG_CLICKABLE);\n";
            if (w.type == WType::Image) c += "    lv_obj_set_style_border_post(" + v + ", true, 0); /* 테두리를 그림 위에 */\n";

            for (int st = 0; st < ST_COUNT; st++) {
                std::string sel = st == ST_DEFAULT ? "0" : stateLv(st);   // LV_PART_MAIN == 0
                const std::string indSel = st == ST_DEFAULT ? std::string("LV_PART_INDICATOR") : std::string("(lv_style_selector_t)LV_PART_INDICATOR | (lv_style_selector_t)") + stateLv(st);
                if (w.style[st][SP_TEXT_FONT].set || w.style[st][SP_FACE].set) {
                    int fs, ff;
                    resolveFont(s, w, st, fs, ff);
                    fonts.insert(fs);
                    c += "    lv_obj_set_style_text_font(" + v + ", &" + fontRef(plan, fs, ff) + ", " + sel + ");\n";
                    if (st != ST_DEFAULT) c += "    lv_obj_set_style_translate_y(" + v + ", 0, " + sel + "); /* 글꼴이 바뀔 때 글자를 다시 배치하게 한다 */\n";
                }
                for (int p = 0; p < SP_COUNT; p++) {
                    const StyleVal& sv = w.style[st][p];
                    if (!sv.set) continue;
                    const std::string pre = "    lv_obj_set_style_";
                    switch (p) {
                    case SP_BG_COLOR: c += pre + "bg_color(" + v + ", lv_color_hex(" + hex(sv.v) + "), " + sel + ");\n"; break;
                    case SP_BG_OPA: c += pre + "bg_opa(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_BORDER_COLOR: c += pre + "border_color(" + v + ", lv_color_hex(" + hex(sv.v) + "), " + sel + ");\n"; break;
                    case SP_BORDER_WIDTH: c += pre + "border_width(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_RADIUS:
                        c += pre + "radius(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n";
                        if (radiusOnIndicator(w.type)) c += pre + "radius(" + v + ", " + std::to_string(sv.v) + ", " + indSel + ");\n";
                        break;
                    case SP_TEXT_COLOR: c += pre + "text_color(" + v + ", lv_color_hex(" + hex(sv.v) + "), " + sel + ");\n"; break;
                    case SP_OPA: c += pre + "opa(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_TEXT_OPA: c += pre + "text_opa(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_LETTER: c += pre + "text_letter_space(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_LINE: c += pre + "text_line_space(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_TALIGN: {
                        static const char* al[] = {"AUTO", "LEFT", "CENTER", "RIGHT"};
                        c += pre + "text_align(" + v + ", LV_TEXT_ALIGN_" + al[sv.v & 3] + ", " + sel + ");\n";
                        break;
                    }
                    case SP_DECOR: {
                        static const char* dc[] = {"LV_TEXT_DECOR_NONE", "LV_TEXT_DECOR_UNDERLINE", "LV_TEXT_DECOR_STRIKETHROUGH", "(lv_text_decor_t)(LV_TEXT_DECOR_UNDERLINE | LV_TEXT_DECOR_STRIKETHROUGH)"};
                        c += pre + "text_decor(" + v + ", " + dc[sv.v & 3] + ", " + sel + ");\n";
                        break;
                    }
                    case SP_IMG_OPA: c += pre + "image_opa(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_IMG_TINT: c += pre + "image_recolor(" + v + ", lv_color_hex(" + hex(sv.v) + "), " + sel + ");\n"; break;
                    case SP_IMG_TINT_OPA: c += pre + "image_recolor_opa(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_PAD: c += pre + "pad_all(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    case SP_ARC_COLOR: if (styleApplies(p, w.type)) c += pre + "arc_color(" + v + ", lv_color_hex(" + hex(sv.v) + "), " + sel + ");\n"; break;
                    case SP_ARC_WIDTH:
                        if (styleApplies(p, w.type)) {
                            c += pre + "arc_width(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n";
                            c += pre + "arc_width(" + v + ", " + std::to_string(sv.v) + ", " + indSel + ");\n";
                        }
                        break;
                    case SP_SHADOW: c += pre + "shadow_width(" + v + ", " + std::to_string(sv.v) + ", " + sel + ");\n"; break;
                    }
                }
            }
            for (auto& e : w.events) {
                if (validIdent(e.cb)) {
                    callbacks.insert(e.cb);
                    c += "    lv_obj_add_event_cb(" + v + ", " + e.cb + ", " + eventLv(e.code) + ", NULL);\n";
                }
                const int t = e.go.empty() ? -1 : screenByName(d, e.go);
                if (t >= 0 && t != (int)si)
                    c += "    lv_obj_add_event_cb(" + v + ", go_to_" + idents[t] + ", " + eventLv(e.code) + ", NULL);\n";
            }
        }
        c += "\n    return scr;\n}\n";
        files.push_back({sn + ".c", c});
        files.push_back({sn + ".h", h});
    }

    // ui.h / ui.c: 테마와 첫 화면
    std::string uh, uc;
    char b[512];
    uh += "/**\n * ui.h — Higenis Studio에서 내보냄 " + stamp + " · 도면 " + cmt(d.project) + " · 개정 " + rev + "\n";
    uh += " *\n * lv_conf.h 요구 사항\n";
    snprintf(b, sizeof b, " *   LV_COLOR_FORMAT_DEFAULT  LV_COLOR_FORMAT_%s  (화면 %dx%d)\n", fmt, d.resW, d.resH);
    uh += b;
    for (int f : fonts) {
        snprintf(b, sizeof b, " *   LV_FONT_MONTSERRAT_%-2d    1\n", f);
        uh += b;
    }
    if (d.round && d.resW == d.resH) uh += " *\n * 원형 화면\n";
    if (!d.credit.empty()) uh += " *\n * " + cmt(d.credit, 400) + "\n";
    if (usesSd(d)) uh += " *\n * SD 카드: sd 폴더의 내용을 카드 맨 위에 복사한다. LVGL 파일 드라이브 'S'가 필요하다\n";
    uh += " */\n#ifndef UI_H\n#define UI_H\n\n#include \"lvgl.h\"\n";
    if (plan.any()) uh += "#include \"ui_font.h\"\n";
    for (auto& n : idents) uh += "#include \"" + n + ".h\"\n";
    uh += "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\nvoid ui_init(void);\n\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n";

    uc += "#include \"ui.h\"\n\nvoid ui_init(void)\n{\n";
    uc += "    lv_display_t * disp = lv_display_get_default();\n";
    uc += "    lv_theme_t * theme = lv_theme_default_init(disp, lv_color_hex(" + hex(d.primary) +
          "),\n                                               lv_palette_main(LV_PALETTE_RED), " +
          (d.dark ? "true" : "false") + ", LV_FONT_DEFAULT);\n";
    uc += "    lv_display_set_theme(disp, theme);\n";
    if (!d.screens.empty()) uc += "    lv_screen_load(" + idents[0] + "_create());\n";
    uc += "}\n";
    if (plan.any()) generateFont(d, plan, stamp, files);
    if (usesFx) {
        files.push_back({"higenis_fx.c", kHigenisFxC});
        files.push_back({"higenis_fx.h", kHigenisFxH});
    }
    generateImages(d, stamp, files);
    files.insert(files.begin(), {"ui.c", uc});
    files.insert(files.begin(), {"ui.h", uh});
    (void)callbacks;
    return files;
}
