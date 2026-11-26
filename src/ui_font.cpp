// ImGui 글꼴 로더: 편집기의 글자도 획을 픽셀에 맞춰 또렷하게 그린다
#include "fonthint.h"
#include "ui.h"

#include <cmath>

namespace {
struct Src { fonthint::Font* f; float scale; };

bool srcInit(ImFontAtlas*, ImFontConfig* c) {
    Src* s = IM_NEW(Src);
    s->f = fonthint::open(c->FontData, c->FontNo);
    if (!s->f) { IM_DELETE(s); return false; }
    c->FontLoaderData = s;
    const float ref = c->DstFont->Sources[0]->SizePixels;
    if (c->MergeMode && c->SizePixels == 0.f) c->SizePixels = ref;
    s->scale = fonthint::scaleForHeight(s->f, 1.f);
    if (c->MergeMode && c->SizePixels != 0.f && ref != 0.f) s->scale *= c->SizePixels / ref;
    s->scale *= c->ExtraSizeScale;
    return true;
}
void srcDestroy(ImFontAtlas*, ImFontConfig* c) {
    Src* s = (Src*)c->FontLoaderData;
    if (s) { fonthint::close(s->f); IM_DELETE(s); }
    c->FontLoaderData = nullptr;
}
int srcIndex(ImFontAtlas*, ImFontConfig* c, ImWchar cp) { return fonthint::glyphIndex(((Src*)c->FontLoaderData)->f, cp); }
bool srcHas(ImFontAtlas*, ImFontConfig* c, int g) { return g > 0 && g < fonthint::glyphCount(((Src*)c->FontLoaderData)->f); }
bool bakedInit(ImFontAtlas*, ImFontConfig* c, ImFontBaked* b, void*) {
    Src* s = (Src*)c->FontLoaderData;
    if (!c->MergeMode) {
        int a, d, gp;
        fonthint::vmetrics(s->f, a, d, gp);
        const float k = s->scale * b->Size / c->ExtraSizeScale;
        b->Ascent = ImCeil(a * k);
        b->Descent = ImFloor(d * k);
    }
    return true;
}
bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* c, ImFontBaked* b, void*, int g, ImFontGlyph* out, float* outAdv) {
    Src* s = (Src*)c->FontLoaderData;
    const float k = s->scale * b->Size;
    float adv = fonthint::advance(s->f, g) * k;
    if (g == fonthint::glyphIndex(s->f, 0x20)) adv *= 1.25f;
    if (outAdv) { *outAdv = adv; return true; }
    out->AdvanceX = adv;
    fonthint::Bitmap bm;
    fonthint::raster(s->f, g, k, fonthint::ALIGN_Y | fonthint::ALIGN_X, 0.f, 0.f, bm);
    if (bm.w <= 0 || bm.h <= 0) return true;
    ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, bm.w, bm.h);
    if (id == ImFontAtlasRectId_Invalid) return false;
    ImTextureRect* r = ImFontAtlasPackGetRect(atlas, id);
    const float top = IM_ROUND(b->Ascent);
    out->X0 = (float)bm.ox;
    out->Y0 = bm.oy + top;
    out->X1 = out->X0 + bm.w;
    out->Y1 = out->Y0 + bm.h;
    out->Visible = true;
    out->PackId = id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, b, c, out, r, bm.a.data(), ImTextureFormat_Alpha8, bm.w);
    return true;
}
}  // namespace

const ImFontLoader* vellumFontLoader() {
    static ImFontLoader l;
    l.Name = "higenis_hinted";
    l.FontSrcInit = srcInit;
    l.FontSrcDestroy = srcDestroy;
    l.FontSrcGetGlyphIndexFromCodepoint = srcIndex;
    l.FontSrcContainsGlyph = srcHas;
    l.FontBakedInit = bakedInit;
    l.FontBakedLoadGlyph = loadGlyph;
    return &l;
}
