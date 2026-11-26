#include "image.h"

#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "stb_image_write.h"

namespace {
const uint32_t kMaxSide = 2048;

struct Rgba { int w = 0, h = 0; std::vector<uint32_t> px; };   // BGRA, 곱하지 않은 알파

bool decode(const std::vector<uint8_t>& b, Rgba& out) {
    if (b.empty()) return false;
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) return false;
    IWICStream* st = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* cv = nullptr;
    IWICBitmapFlipRotator* rot = nullptr;
    bool ok = false;
    UINT w = 0, h = 0;
    if (SUCCEEDED(fac->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)b.data(), (DWORD)b.size())) &&
        SUCCEEDED(fac->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(fac->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        // 사진의 회전 정보(EXIF)
        WICBitmapTransformOptions opt = WICBitmapTransformRotate0;
        IWICMetadataQueryReader* mq = nullptr;
        if (SUCCEEDED(fr->GetMetadataQueryReader(&mq))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(mq->GetMetadataByName(L"/app1/ifd/{ushort=274}", &v)) && v.vt == VT_UI2) {
                if (v.uiVal == 3) opt = WICBitmapTransformRotate180;
                if (v.uiVal == 6) opt = WICBitmapTransformRotate90;
                if (v.uiVal == 8) opt = WICBitmapTransformRotate270;
            }
            PropVariantClear(&v);
            mq->Release();
        }
        IWICBitmapSource* src = cv;
        if (opt != WICBitmapTransformRotate0 && SUCCEEDED(fac->CreateBitmapFlipRotator(&rot)) && SUCCEEDED(rot->Initialize(cv, opt))) src = rot;
        if (SUCCEEDED(src->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 20000 && h <= 20000 && (uint64_t)w * h <= 100000000ull) {
            out.w = (int)w; out.h = (int)h;
            out.px.resize((size_t)w * h);
            ok = SUCCEEDED(src->CopyPixels(nullptr, w * 4, (UINT)(out.px.size() * 4), (BYTE*)out.px.data()));
        }
    }
    if (rot) rot->Release();
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (st) st->Release();
    fac->Release();
    return ok;
}

// 삼각 필터로 크기를 바꾼다(줄일 때는 넓게 평균). 알파를 곱한 채로 섞는다
struct Weights { int first; std::vector<float> w; };
std::vector<Weights> makeWeights(double s0, double s1, int srcN, int dstN) {
    std::vector<Weights> out((size_t)dstN);
    const double ratio = (s1 - s0) / dstN, sup = std::max(1.0, ratio);
    for (int i = 0; i < dstN; i++) {
        const double c = s0 + (i + 0.5) * ratio - 0.5;
        const int a = (int)ceil(c - sup), b = (int)floor(c + sup);
        Weights& W = out[i];
        W.first = std::clamp(a, 0, srcN - 1);
        const int last = std::clamp(b, 0, srcN - 1);
        W.w.assign((size_t)(last - W.first + 1), 0.f);
        double sum = 0;
        for (int k = a; k <= b; k++) {
            const double t = 1.0 - fabs(k - c) / sup;
            if (t <= 0) continue;
            W.w[std::clamp(k, 0, srcN - 1) - W.first] += (float)t;
            sum += t;
        }
        if (sum <= 0) { W.w[0] = 1.f; sum = 1; }
        for (float& v : W.w) v = (float)(v / sum);
    }
    return out;
}
Rgba resample(const uint32_t* px, int sw, int sh, double x0, double y0, double x1, double y1, int W, int H, bool crisp = false) {
    Rgba o;
    o.w = W; o.h = H;
    o.px.assign((size_t)W * H, 0);
    if (crisp) {   // 도트 그림: 가장 가까운 픽셀
        for (int y = 0; y < H; y++) {
            const int sy = std::clamp((int)floor(y0 + (y + 0.5) * (y1 - y0) / H), 0, sh - 1);
            for (int x = 0; x < W; x++) o.px[(size_t)y * W + x] = px[(size_t)sy * sw + std::clamp((int)floor(x0 + (x + 0.5) * (x1 - x0) / W), 0, sw - 1)];
        }
        return o;
    }
    const auto wx = makeWeights(x0, x1, sw, W), wy = makeWeights(y0, y1, sh, H);
    int r0 = sh, r1 = -1;
    for (const Weights& w : wy) { r0 = std::min(r0, w.first); r1 = std::max(r1, w.first + (int)w.w.size() - 1); }
    if (r1 < r0) return o;
    std::vector<float> tmp((size_t)(r1 - r0 + 1) * W * 4);
    for (int y = r0; y <= r1; y++)
        for (int x = 0; x < W; x++) {
            float c[4] = {0, 0, 0, 0};
            const Weights& w = wx[x];
            for (size_t k = 0; k < w.w.size(); k++) {
                const uint32_t p = px[(size_t)y * sw + w.first + k];
                const float a = (p >> 24) / 255.f * w.w[k];
                c[0] += (p & 255) * a; c[1] += ((p >> 8) & 255) * a; c[2] += ((p >> 16) & 255) * a; c[3] += a;
            }
            memcpy(&tmp[((size_t)(y - r0) * W + x) * 4], c, sizeof c);
        }
    for (int y = 0; y < H; y++) {
        const Weights& w = wy[y];
        for (int x = 0; x < W; x++) {
            float c[4] = {0, 0, 0, 0};
            for (size_t k = 0; k < w.w.size(); k++) {
                const float* t = &tmp[((size_t)(w.first + k - r0) * W + x) * 4];
                for (int i = 0; i < 4; i++) c[i] += t[i] * w.w[k];
            }
            const float a = c[3];
            auto q = [&](float v) { return (uint32_t)std::clamp((int)lroundf(a > 1e-5f ? v / a : 0.f), 0, 255); };
            o.px[(size_t)y * W + x] = (uint32_t)std::clamp((int)lroundf(a * 255.f), 0, 255) << 24 | q(c[2]) << 16 | q(c[1]) << 8 | q(c[0]);
        }
    }
    return o;
}

void appendBytes(void* ctx, void* data, int n) {
    auto* v = (std::vector<uint8_t>*)ctx;
    v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + n);
}
std::vector<uint8_t> encodePng(const Rgba& r) {
    std::vector<uint8_t> rgba((size_t)r.w * r.h * 4), out;
    for (size_t i = 0; i < r.px.size(); i++) {
        const uint32_t p = r.px[i];
        rgba[i * 4] = (uint8_t)(p >> 16); rgba[i * 4 + 1] = (uint8_t)(p >> 8); rgba[i * 4 + 2] = (uint8_t)p; rgba[i * 4 + 3] = (uint8_t)(p >> 24);
    }
    stbi_write_png_to_func(appendBytes, &out, r.w, r.h, 4, rgba.data(), r.w * 4);
    return out;
}
}  // namespace

namespace image {

const char* fitKo(int f) {
    static const char* k[FIT_COUNT] = {"채움", "맞춤", "늘림", "원본"};
    return k[std::clamp(f, 0, FIT_COUNT - 1)];
}

std::string base64(const std::vector<uint8_t>& b) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((b.size() + 2) / 3 * 4);
    for (size_t i = 0; i < b.size(); i += 3) {
        const uint32_t v = b[i] << 16 | (i + 1 < b.size() ? b[i + 1] << 8 : 0) | (i + 2 < b.size() ? b[i + 2] : 0);
        o += t[v >> 18]; o += t[v >> 12 & 63];
        o += i + 1 < b.size() ? t[v >> 6 & 63] : '=';
        o += i + 2 < b.size() ? t[v & 63] : '=';
    }
    return o;
}
std::vector<uint8_t> unbase64(const std::string& s) {
    std::vector<uint8_t> o;
    o.reserve(s.size() / 4 * 3);
    uint32_t v = 0;
    int n = 0;
    for (char c : s) {
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (d < 0) continue;
        v = v << 6 | (uint32_t)d;
        if (++n == 4) { o.push_back((uint8_t)(v >> 16)); o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); n = 0; v = 0; }
    }
    if (n == 3) { o.push_back((uint8_t)(v >> 10)); o.push_back((uint8_t)(v >> 2)); }
    if (n == 2) o.push_back((uint8_t)(v >> 4));
    return o;
}

std::shared_ptr<const ImageAsset> fromBytes(std::vector<uint8_t> bytes, const std::string& name, const std::string& file) {
    Rgba r;
    if (!decode(bytes, r)) return nullptr;
    auto a = std::make_shared<ImageAsset>();
    if ((uint32_t)std::max(r.w, r.h) > kMaxSide) {   // 도면 파일이 커지지 않게 줄여서 담는다
        const double k = (double)kMaxSide / std::max(r.w, r.h);
        r = resample(r.px.data(), r.w, r.h, 0, 0, r.w, r.h, std::max(1, (int)lround(r.w * k)), std::max(1, (int)lround(r.h * k)));
        bytes = encodePng(r);
    }
    a->name = name;
    a->file = file;
    a->w = r.w;
    a->h = r.h;
    a->px.swap(r.px);
    for (uint32_t p : a->px) if ((p >> 24) != 255) { a->alpha = true; break; }
    a->b64 = base64(bytes);
    a->bytes = (int)bytes.size();
    return a;
}

std::shared_ptr<const ImageAsset> load(const std::wstring& path, const Document& d, std::string& err) {
    std::vector<uint8_t> b;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        uint8_t buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && b.size() < 64u * 1024 * 1024) b.insert(b.end(), buf, buf + n);
        fclose(f);
    }
    if (b.empty()) { err = "파일을 읽을 수 없습니다"; return nullptr; }
    const std::string file = wideToUtf8(path.substr(path.find_last_of(L"\\/") + 1));
    std::string base = sanitizeIdent(file.substr(0, file.find_last_of('.')));
    if (base.empty()) base = "image";
    std::string name = base;
    for (int i = 2; find(d, name); i++) name = base + "_" + std::to_string(i);
    auto a = fromBytes(std::move(b), name, file);
    if (!a) err = "그림 파일이 아니거나 지원하지 않는 형식입니다 (PNG·JPG·BMP·GIF)";
    return a;
}

// 배경 지우기: 가장자리에 많은 색(체크무늬처럼 두세 가지여도 된다)과 이어진 곳을 투명하게
static std::vector<uint32_t> cutBackground(const ImageAsset& a, int cut) {
    std::vector<uint32_t> px = a.px;
    const int W = a.w, H = a.h, tol = 6 + cut * 8 / 10;
    std::map<uint32_t, int> hist;
    std::vector<int> border;
    for (int x = 0; x < W; x++) { border.push_back(x); border.push_back((H - 1) * W + x); }
    for (int y = 1; y + 1 < H; y++) { border.push_back(y * W); border.push_back(y * W + W - 1); }
    for (int i : border) if ((px[i] >> 24) > 8) hist[px[i] & 0xF0F0F0]++;
    std::vector<std::pair<int, uint32_t>> top;
    for (auto& [c, n] : hist) top.push_back({n, c | 0x080808});
    std::sort(top.rbegin(), top.rend());
    std::vector<uint32_t> seeds;
    for (auto& [n, c] : top) if (seeds.size() < 4 && n * 100 >= (int)border.size() * 6) seeds.push_back(c);
    auto isBg = [&](uint32_t p) {
        if ((p >> 24) <= 8) return true;
        for (uint32_t s : seeds)
            if (abs((int)(p & 255) - (int)(s & 255)) <= tol && abs((int)(p >> 8 & 255) - (int)(s >> 8 & 255)) <= tol && abs((int)(p >> 16 & 255) - (int)(s >> 16 & 255)) <= tol) return true;
        return false;
    };
    std::vector<uint8_t> seen(px.size(), 0);
    std::vector<int> st;
    for (int i : border) if (!seen[i] && isBg(px[i])) { seen[i] = 1; st.push_back(i); }
    while (!st.empty()) {
        const int i = st.back(), x = i % W, y = i / W;
        st.pop_back();
        auto go = [&](int j) { if (!seen[j] && isBg(px[j])) { seen[j] = 1; st.push_back(j); } };
        if (x > 0) go(i - 1);
        if (x + 1 < W) go(i + 1);
        if (y > 0) go(i - W);
        if (y + 1 < H) go(i + W);
    }
    for (size_t i = 0; i < px.size(); i++) if (seen[i]) px[i] &= 0x00FFFFFFu;
    return px;
}
// 상자 흐림을 세 번 겹친다(알파를 곱한 채로)
static void blurPixels(std::vector<uint32_t>& px, int W, int H, double radius) {
    const int r = std::max(1, (int)lround(radius * 0.58));
    std::vector<float> f((size_t)W * H * 4), t(f.size());
    for (size_t i = 0; i < px.size(); i++) {
        const float a = (px[i] >> 24) / 255.f;
        f[i * 4] = (px[i] & 255) * a; f[i * 4 + 1] = (px[i] >> 8 & 255) * a; f[i * 4 + 2] = (px[i] >> 16 & 255) * a; f[i * 4 + 3] = a;
    }
    auto pass = [&](std::vector<float>& src, std::vector<float>& dst, int n, int lines, int step, int lineStep) {
        for (int l = 0; l < lines; l++)
            for (int c = 0; c < 4; c++) {
                const float* s = &src[(size_t)l * lineStep * 4 + c];
                float* d = &dst[(size_t)l * lineStep * 4 + c];
                float sum = 0;
                for (int k = -r; k <= r; k++) sum += s[(size_t)std::clamp(k, 0, n - 1) * step * 4];
                for (int i = 0; i < n; i++) {
                    d[(size_t)i * step * 4] = sum / (2 * r + 1);
                    sum += s[(size_t)std::min(i + r + 1, n - 1) * step * 4] - s[(size_t)std::max(i - r, 0) * step * 4];
                }
            }
    };
    for (int i = 0; i < 3; i++) { pass(f, t, W, H, 1, W); pass(t, f, H, W, W, 1); }
    for (size_t i = 0; i < px.size(); i++) {
        const float a = std::clamp(f[i * 4 + 3], 0.f, 1.f);
        auto q = [&](float v) { return (uint32_t)std::clamp((int)lroundf(a > 1e-4f ? v / a : 0.f), 0, 255); };
        px[i] = (uint32_t)lroundf(a * 255.f) << 24 | q(f[i * 4 + 2]) << 16 | q(f[i * 4 + 1]) << 8 | q(f[i * 4]);
    }
}
static void adjust(std::vector<uint32_t>& px, int W, int H, const ImageFx& fx) {
    if (!fx.bright && !fx.sat && !fx.shade) return;
    const float sat = fx.sat >= 0 ? 1.f + fx.sat / 50.f : 1.f + fx.sat / 100.f;
    for (int y = 0; y < H; y++) {
        const float v = H > 1 ? (float)y / (H - 1) : 0.f;
        const float g = fx.shadeDir == 2 ? 1.f : std::clamp(((fx.shadeDir == 1 ? 1.f - v : v) - 0.25f) / 0.75f, 0.f, 1.f);
        const float k = 1.f - fx.shade / 100.f * g;
        for (int x = 0; x < W; x++) {
            uint32_t& p = px[(size_t)y * W + x];
            float c[3] = {(float)(p & 255), (float)(p >> 8 & 255), (float)(p >> 16 & 255)};
            if (fx.sat) { const float l = c[0] * 0.114f + c[1] * 0.587f + c[2] * 0.299f; for (float& t : c) t = l + (t - l) * sat; }
            if (fx.bright > 0) for (float& t : c) t += (255.f - t) * fx.bright / 100.f;
            if (fx.bright < 0) for (float& t : c) t *= 1.f + fx.bright / 100.f;
            uint32_t o = p & 0xFF000000u;
            for (int i = 0; i < 3; i++) o |= (uint32_t)std::clamp((int)lroundf(c[i] * k), 0, 255) << (i * 8);
            p = o;
        }
    }
}

std::vector<uint32_t> fitted(const std::shared_ptr<const ImageAsset>& ap, int W, int H, int fit, double k, const ImageFx& fx) {
    const ImageAsset& a = *ap;
    std::vector<uint32_t> out((size_t)std::max(0, W) * std::max(0, H), 0);
    if (W <= 0 || H <= 0 || a.w <= 0 || a.h <= 0) return out;
    const uint32_t* src = a.px.data();
    if (fx.cut > 0) {
        struct Cut { std::weak_ptr<const ImageAsset> own; int cut; std::vector<uint32_t> px; };
        static std::vector<Cut> cuts;
        Cut* hit = nullptr;
        for (Cut& c : cuts) if (c.own.lock() == ap && c.cut == fx.cut) hit = &c;
        if (!hit) {
            if (cuts.size() > 5) cuts.erase(cuts.begin());
            cuts.push_back({ap, fx.cut, cutBackground(a, fx.cut)});
            hit = &cuts.back();
        }
        src = hit->px.data();
    }
    const bool crisp = fx.crisp != 0;
    if (fit == FIT_STRETCH) out = resample(src, a.w, a.h, 0, 0, a.w, a.h, W, H, crisp).px;
    else if (fit == FIT_COVER) {
        const double s = std::max((double)W / a.w, (double)H / a.h), vw = W / s, vh = H / s;
        const double x0 = (a.w - vw) * fx.focusX / 100.0, y0 = (a.h - vh) * fx.focusY / 100.0;
        out = resample(src, a.w, a.h, x0, y0, x0 + vw, y0 + vh, W, H, crisp).px;
    } else {
        int dw, dh;
        if (fit == FIT_CONTAIN) {
            const double s = std::min((double)W / a.w, (double)H / a.h);
            dw = std::clamp((int)lround(a.w * s), 1, W);
            dh = std::clamp((int)lround(a.h * s), 1, H);
        } else {
            dw = std::max(1, (int)lround(a.w * k));
            dh = std::max(1, (int)lround(a.h * k));
        }
        const Rgba r = resample(src, a.w, a.h, 0, 0, a.w, a.h, std::min(dw, 4096), std::min(dh, 4096), crisp);
        const int ox = (int)floor((W - r.w) * (fit == FIT_CENTER ? fx.focusX / 100.0 : 0.5)), oy = (int)floor((H - r.h) * (fit == FIT_CENTER ? fx.focusY / 100.0 : 0.5));
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++)
                if (x + ox >= 0 && x + ox < W && y + oy >= 0 && y + oy < H) out[(size_t)(y + oy) * W + x + ox] = r.px[(size_t)y * r.w + x];
    }
    if (fx.blur > 0) blurPixels(out, W, H, fx.blur * k);
    adjust(out, W, H, fx);
    return out;
}

const char* Variant::cfName() const {
    switch (cf) {
    case 0x12: return "LV_COLOR_FORMAT_RGB565";
    case 0x14: return "LV_COLOR_FORMAT_RGB565A8";
    case 0x11: return "LV_COLOR_FORMAT_XRGB8888";
    default: return "LV_COLOR_FORMAT_ARGB8888";
    }
}
std::vector<uint8_t> Variant::bin() const {
    std::vector<uint8_t> o;
    auto u16 = [&](int v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); };
    o.push_back(0x19);
    o.push_back((uint8_t)cf);
    u16(0); u16(w); u16(h); u16(stride); u16(0);
    o.insert(o.end(), data.begin(), data.end());
    return o;
}

std::shared_ptr<const Variant> variant(const std::shared_ptr<const ImageAsset>& a, int W, int H, int fit, int depth, const ImageFx& fx) {
    struct Key { const ImageAsset* a; int W, H, fit, depth; ImageFx fx; };
    struct Entry { Key k; std::weak_ptr<const ImageAsset> own; std::shared_ptr<const Variant> v; };
    static std::vector<Entry> cache;   // 최근 것 몇 개
    for (size_t i = 0; i < cache.size(); i++) {
        Entry& e = cache[i];
        if (e.k.a == a.get() && !e.own.expired() && e.k.W == W && e.k.H == H && e.k.fit == fit && e.k.depth == depth && e.k.fx == fx) {
            std::rotate(cache.begin() + i, cache.begin() + i + 1, cache.end());
            return cache.back().v;
        }
    }
    auto v = std::make_shared<Variant>();
    v->w = W; v->h = H;
    std::vector<uint32_t> px = fitted(a, W, H, fit, 1.0, fx);
    for (uint32_t p : px) if ((p >> 24) != 255) { v->alpha = true; break; }
    if (depth == 16) {
        v->cf = v->alpha ? 0x14 : 0x12;
        v->stride = W * 2;
        v->data.assign((size_t)W * H * 2 + (v->alpha ? (size_t)W * H : 0), 0);
        // 색이 많은 사진은 오차를 이웃에 나눠 띠가 지지 않게 한다(로고처럼 색이 적으면 그대로)
        std::set<uint32_t> colors;
        for (uint32_t p : px) { colors.insert(p & 0xFFFFFF); if (colors.size() > 400) break; }
        const bool dither = colors.size() > 400;
        std::vector<float> err((size_t)(W + 2) * 2 * 3, 0.f);
        for (int y = 0; y < H; y++) {
            float* cur = &err[(size_t)(y & 1) * (W + 2) * 3];
            float* nxt = &err[(size_t)(~y & 1) * (W + 2) * 3];
            std::fill(nxt, nxt + (size_t)(W + 2) * 3, 0.f);
            for (int x = 0; x < W; x++) {
                const uint32_t p = px[(size_t)y * W + x];
                int q[3];
                for (int c = 0; c < 3; c++) {   // B G R
                    const int bits = c == 1 ? 6 : 5, mx = (1 << bits) - 1;
                    const float want = (float)((p >> (c * 8)) & 255) + (dither ? cur[(x + 1) * 3 + c] : 0.f);
                    q[c] = std::clamp((int)lroundf(want * mx / 255.f), 0, mx);
                    if (dither && (p >> 24) > 0) {
                        const float e = std::clamp(want - q[c] * 255.f / mx, -24.f, 24.f);
                        cur[(x + 2) * 3 + c] += e * 7 / 16; nxt[x * 3 + c] += e * 3 / 16;
                        nxt[(x + 1) * 3 + c] += e * 5 / 16; nxt[(x + 2) * 3 + c] += e / 16;
                    }
                }
                const uint16_t u = (uint16_t)(q[2] << 11 | q[1] << 5 | q[0]);
                v->data[((size_t)y * W + x) * 2] = (uint8_t)u;
                v->data[((size_t)y * W + x) * 2 + 1] = (uint8_t)(u >> 8);
                if (v->alpha) v->data[(size_t)W * H * 2 + (size_t)y * W + x] = (uint8_t)(p >> 24);
            }
        }
    } else {
        v->cf = v->alpha ? 0x10 : 0x11;
        v->stride = W * 4;
        v->data.resize((size_t)W * H * 4);
        memcpy(v->data.data(), px.data(), v->data.size());
    }
    cache.push_back({{a.get(), W, H, fit, depth, fx}, a, v});
    if (cache.size() > 48) cache.erase(cache.begin());
    return v;
}

const std::shared_ptr<const ImageAsset>* find(const Document& d, const std::string& name) {
    for (auto& a : d.images) if (a->name == name) return &a;
    return nullptr;
}
void size(const Document& d, const Widget& w, int& W, int& H) {
    W = w.w; H = w.h;
    if (w.autoSize) if (auto* a = find(d, w.text)) { W = (*a)->w; H = (*a)->h; }
    W = std::clamp(W, 1, 2048);
    H = std::clamp(H, 1, 2048);
}
std::string ident(const Document& d, const Widget& w) {
    int W, H;
    size(d, w, W, H);
    static const char* tag[FIT_COUNT] = {"", "_fit", "_fill", "_raw"};
    std::string id = "img_" + w.text + "_" + std::to_string(W) + "x" + std::to_string(H) + tag[fit(w)];
    const ImageFx& x = w.x2.img;
    if (!(x == ImageFx())) {   // 손본 그림은 값마다 다른 이름
        uint32_t h = 2166136261u;
        for (int v : {x.focusX, x.focusY, x.blur, x.bright, x.sat, x.shade, x.shadeDir, x.crisp, x.cut}) { h ^= (uint32_t)(v + 1000); h *= 16777619u; }
        char b[16];
        snprintf(b, sizeof b, "_%05x", h & 0xFFFFF);
        id += b;
    }
    return id;
}
std::string sdPath(const Document& d, const Widget& w) {
    if (!w.text2.empty()) return w.text2[0] == '/' ? w.text2 : "/" + w.text2;
    return "/higenis/" + ident(d, w).substr(4) + ".bin";
}

static double lum(uint32_t c) {
    auto f = [](double v) { v /= 255; return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * f(c >> 16 & 255) + 0.7152 * f(c >> 8 & 255) + 0.0722 * f(c & 255);
}
double hardToRead(const Document& d, const Screen& s, const Widget& w, const std::map<int, Rect>& coords) {
    auto rectOf = [&](int id) -> const Rect* { auto it = coords.find(id); return it == coords.end() ? nullptr : &it->second; };
    const Rect* r = rectOf(w.id);
    if (!r || w.text.empty()) return 0;
    const StyleVal& bo = w.style[ST_DEFAULT][SP_BG_OPA];
    if (bo.set && bo.v >= 110) return 0;   // 글자 밑에 판이 있다
    const uint32_t tc = (uint32_t)lvhost::styleValue(w.id, SP_TEXT_COLOR);
    const double lt = lum(tc);
    long total = 0, hard = 0;
    for (const Widget& im : s.widgets) {
        if (im.id == w.id) break;          // 라벨보다 위에 그려지는 것은 보지 않는다
        if (im.type != WType::Image || im.hidden) continue;
        const auto* a = find(d, im.text);
        const Rect* q = rectOf(im.id);
        if (!a || !q) continue;
        const int x0 = std::max(r->x1, q->x1), y0 = std::max(r->y1, q->y1), x1 = std::min(r->x2, q->x2), y1 = std::min(r->y2, q->y2);
        if (x1 <= x0 || y1 <= y0) continue;
        int W, H;
        size(d, im, W, H);
        const std::vector<uint32_t> px = fitted(*a, W, H, fit(im), 1.0, im.x2.img);
        total = hard = 0;                  // 겹치면 맨 위 그림만
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                const int ix = x - q->x1, iy = y - q->y1;
                if (ix < 0 || iy < 0 || ix >= W || iy >= H) continue;
                const uint32_t p = px[(size_t)iy * W + ix];
                total++;
                if ((p >> 24) < 60) continue;
                const double lp = lum(p), c = (std::max(lp, lt) + 0.05) / (std::min(lp, lt) + 0.05);
                if (c < 2.6) hard++;
            }
    }
    return total > 50 ? (double)hard / total : 0;
}

}  // namespace image
