#include "ui/renderer.h"

#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <wincodec.h>

#include <cmath>

namespace sudoku::ui {

Renderer::~Renderer() { releaseDevice(); }

bool Renderer::initWindow(HWND hwnd) {
    hwnd_ = hwnd;
    offscreen_ = false;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    width_ = UINT(std::max<LONG>(1, rc.right - rc.left));
    height_ = UINT(std::max<LONG>(1, rc.bottom - rc.top));
    return createDeviceIndependent() && createDevice();
}

bool Renderer::initOffscreen(UINT w, UINT h) {
    offscreen_ = true;
    width_ = std::max<UINT>(1, w);
    height_ = std::max<UINT>(1, h);
    return createDeviceIndependent() && createDevice();
}

bool Renderer::createDeviceIndependent() {
    if (factory_) return true;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 reinterpret_cast<void**>(factory_.GetAddressOf()))))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;

    // Pick font families, falling back on older Windows versions.
    ComPtr<IDWriteFontCollection> fonts;
    dwrite_->GetSystemFontCollection(&fonts);
    auto pick = [&](int slot, const wchar_t* preferred, const wchar_t* fallback) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        fonts->FindFamilyName(preferred, &index, &exists);
        families_[slot] = preferred;
        if (!exists) {
            fonts->FindFamilyName(fallback, &index, &exists);
            families_[slot] = fallback;
        }
        if (!exists) return;
        ComPtr<IDWriteFontFamily> fam;
        ComPtr<IDWriteFont> font;
        if (SUCCEEDED(fonts->GetFontFamily(index, &fam)) &&
            SUCCEEDED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS m{};
            font->GetMetrics(&m);
            if (m.designUnitsPerEm) capHeight_[slot] = float(m.capHeight) / float(m.designUnitsPerEm);
        }
    };
    pick(int(Font::Text), L"Segoe UI Variable Text", L"Segoe UI");
    pick(int(Font::Display), L"Segoe UI Variable Display", L"Segoe UI");
    pick(int(Font::Icons), L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    return true;
}

bool Renderer::createDevice() {
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, UINT(std::size(levels)),
                                   D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, UINT(std::size(levels)),
                               D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
    if (FAILED(hr) || FAILED(d3d_.As(&dxgiDevice_))) return false;
    if (FAILED(factory_->CreateDevice(dxgiDevice_.Get(), &d2dDevice_))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return false;
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    dc_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_);
    const float dashes[] = {4.0f, 3.0f};
    factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                            D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
                                                            D2D1_DASH_STYLE_CUSTOM, 0.0f),
                                dashes, 2, &dash_);
    dc_->CreateEffect(CLSID_D2D1Shadow, &shadowFx_);

    if (!offscreen_) {
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> dxgiFactory;
        if (FAILED(dxgiDevice_->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory))))
            return false;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> sc;
        if (FAILED(dxgiFactory->CreateSwapChainForComposition(dxgiDevice_.Get(), &desc, nullptr, &sc))) return false;
        if (FAILED(sc.As(&swap_))) return false;
        swap_->SetMaximumFrameLatency(1);
        frameWait_ = swap_->GetFrameLatencyWaitableObject();
        if (FAILED(DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
        if (FAILED(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_))) return false;
        if (FAILED(dcomp_->CreateVisual(&visual_))) return false;
        visual_->SetContent(swap_.Get());
        dcompTarget_->SetRoot(visual_.Get());
        dcomp_->Commit();
    }
    return createTarget();
}

bool Renderer::createTarget() {
    dc_->SetTarget(nullptr);
    target_.Reset();
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    HRESULT hr;
    if (offscreen_) {
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        hr = dc_->CreateBitmap(D2D1::SizeU(width_, height_), nullptr, 0, &props, &target_);
    } else {
        ComPtr<IDXGISurface> surface;
        hr = swap_->GetBuffer(0, IID_PPV_ARGS(&surface));
        if (SUCCEEDED(hr)) hr = dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_);
    }
    if (FAILED(hr)) return false;
    dc_->SetTarget(target_.Get());
    dc_->SetDpi(96.0f, 96.0f);
    return true;
}

void Renderer::releaseDevice() {
    if (dc_) dc_->SetTarget(nullptr);
    masks_.clear();
    target_.Reset();
    shadowFx_.Reset();
    dash_.Reset();
    brush_.Reset();
    if (frameWait_) {
        CloseHandle(frameWait_);
        frameWait_ = nullptr;
    }
    visual_.Reset();
    dcompTarget_.Reset();
    dcomp_.Reset();
    swap_.Reset();
    dc_.Reset();
    d2dDevice_.Reset();
    dxgiDevice_.Reset();
    d3d_.Reset();
}

void Renderer::handleDeviceLost() {
    releaseDevice();
    createDevice();
    ++generation_;
}

void Renderer::resize(UINT w, UINT h) {
    if (!w || !h || (w == width_ && h == height_)) return;
    width_ = w;
    height_ = h;
    if (!dc_) return;
    if (offscreen_) {
        createTarget();
        return;
    }
    dc_->SetTarget(nullptr);
    target_.Reset();
    if (FAILED(swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) {
        handleDeviceLost();
        return;
    }
    createTarget();
}

void Renderer::trimMemory() {
    if (d2dDevice_) d2dDevice_->ClearResources(0);
    ComPtr<IDXGIDevice3> dxgi3;
    if (dxgiDevice_ && SUCCEEDED(dxgiDevice_.As(&dxgi3))) dxgi3->Trim();
}

void Renderer::waitForFrame() {
    if (frameWait_) WaitForSingleObjectEx(frameWait_, 100, TRUE);
}

bool Renderer::beginDraw() {
    if (!dc_ || !target_) return false;
    if (++frame_ % 240 == 0) {
        for (auto it = layouts_.begin(); it != layouts_.end();)
            it = it->second.lastFrame + 600 < frame_ ? layouts_.erase(it) : std::next(it);
    }
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

void Renderer::endDraw() {
    HRESULT hr = dc_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        handleDeviceLost();
        return;
    }
    if (!offscreen_ && swap_) {
        hr = swap_->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) handleDeviceLost();
    }
}

bool Renderer::savePng(const std::wstring& path) {
    if (!dc_ || !target_) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    ComPtr<ID2D1Bitmap1> cpu;
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU(width_, height_), nullptr, 0, &props, &cpu))) return false;
    const D2D1_POINT_2U origin{0, 0};
    const D2D1_RECT_U src{0, 0, width_, height_};
    if (FAILED(cpu->CopyFromBitmap(&origin, target_.Get(), &src))) return false;
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;

    bool ok = false;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(width_, height_))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        frame->SetPixelFormat(&fmt);
        ok = SUCCEEDED(frame->WritePixels(height_, mapped.pitch, mapped.pitch * height_, mapped.bits)) &&
             SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    }
    cpu->Unmap();
    return ok;
}

// ------------------------------------------------------------- primitives

ID2D1SolidColorBrush* Renderer::brush(Color c) {
    brush_->SetColor(c);
    return brush_.Get();
}

void Renderer::clear(Color c) { dc_->Clear(c); }

void Renderer::fill(const Rect& r, Color c) { dc_->FillRectangle(r.d2d(), brush(c)); }

namespace {

Rect snapped(const Rect& r) {
    const float x0 = std::round(r.x), y0 = std::round(r.y), x1 = std::round(r.r()), y1 = std::round(r.b());
    return {x0, y0, x1 - x0, y1 - y0};
}

bool inRoundRect(float x, float y, float x0, float y0, float x1, float y1, float rad) {
    if (x < x0 || y < y0 || x > x1 || y > y1) return false;
    const float dx = std::max({x0 + rad - x, 0.0f, x - (x1 - rad)});
    const float dy = std::max({y0 + rad - y, 0.0f, y - (y1 - rad)});
    return dx * dx + dy * dy <= rad * rad;
}

}  // namespace

// Alpha mask of a rounded square (or a circle) of side 2*half, optionally hollow (ring).
// Coverage is computed with 4x4 supersampling, so edges are properly anti-aliased.
ID2D1Bitmap* Renderer::shapeMask(float radius, float stroke, bool circle, int& half) {
    const float outer = circle ? radius + stroke * 0.5f : radius;
    half = std::max(1, int(std::ceil(outer)));
    const uint64_t key = (uint64_t(circle) << 63) | (uint64_t(std::lround(radius * 16)) << 24) |
                         uint64_t(std::lround(stroke * 16));
    if (auto it = masks_.find(key); it != masks_.end()) return it->second.Get();
    const int m = half * 2;
    std::vector<uint8_t> px(size_t(m) * size_t(m));
    constexpr int kSub = 4;
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < m; ++i) {
            int hits = 0;
            for (int sy = 0; sy < kSub; ++sy) {
                for (int sx = 0; sx < kSub; ++sx) {
                    const float x = float(i) + (float(sx) + 0.5f) / kSub, y = float(j) + (float(sy) + 0.5f) / kSub;
                    bool in;
                    if (circle) {
                        const float dx = x - float(half), dy = y - float(half), d2 = dx * dx + dy * dy;
                        if (stroke > 0) {
                            const float ro = radius + stroke * 0.5f, ri = std::max(0.0f, radius - stroke * 0.5f);
                            in = d2 <= ro * ro && d2 >= ri * ri;
                        } else {
                            in = d2 <= radius * radius;
                        }
                    } else {
                        in = inRoundRect(x, y, 0, 0, float(m), float(m), radius);
                        if (in && stroke > 0)
                            in = !inRoundRect(x, y, stroke, stroke, float(m) - stroke, float(m) - stroke,
                                              std::max(0.0f, radius - stroke));
                    }
                    hits += in;
                }
            }
            px[size_t(j) * size_t(m) + size_t(i)] = uint8_t((hits * 255 + kSub * kSub / 2) / (kSub * kSub));
        }
    }
    const D2D1_BITMAP_PROPERTIES props =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU(UINT32(m), UINT32(m)), px.data(), UINT32(m), &props, &bmp))) return nullptr;
    if (masks_.size() > 512) masks_.clear();
    return (masks_[key] = bmp).Get();
}

void Renderer::maskQuad(ID2D1Bitmap* mask, float x, float y, float w, float h, float sx, float sy, Color c) {
    const D2D1_RECT_F dst{x, y, x + w, y + h}, src{sx, sy, sx + w, sy + h};
    dc_->FillOpacityMask(mask, brush(c), &dst, &src);
}

void Renderer::fillRound(const Rect& rr, float radius, Color c) {
    const Rect r = snapped(rr);
    if (r.w <= 0 || r.h <= 0) return;
    radius = std::min({radius, r.w * 0.5f, r.h * 0.5f});
    if (std::ceil(radius) * 2 > std::min(r.w, r.h)) radius = std::floor(std::min(r.w, r.h) * 0.5f);
    int half = 0;
    ID2D1Bitmap* mask = radius >= 0.75f ? shapeMask(radius, 0, false, half) : nullptr;
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    if (!mask) {
        fill(r, c);
    } else {
        const float k = float(half);
        // Three non-overlapping bands (exact for translucent colours) plus four corner quads.
        if (r.w > 2 * k) fill({r.x + k, r.y, r.w - 2 * k, r.h}, c);
        if (r.h > 2 * k) {
            fill({r.x, r.y + k, k, r.h - 2 * k}, c);
            fill({r.r() - k, r.y + k, k, r.h - 2 * k}, c);
        }
        maskQuad(mask, r.x, r.y, k, k, 0, 0, c);
        maskQuad(mask, r.r() - k, r.y, k, k, k, 0, c);
        maskQuad(mask, r.x, r.b() - k, k, k, 0, k, c);
        maskQuad(mask, r.r() - k, r.b() - k, k, k, k, k, c);
    }
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Renderer::strokeRound(const Rect& rr, float radius, Color c, float width) {
    const Rect r = snapped(rr);
    if (r.w <= 0 || r.h <= 0) return;
    const float sw = std::max(1.0f, std::round(width));
    if (sw * 2 >= std::min(r.w, r.h)) {
        fillRound(r, radius, c);
        return;
    }
    radius = std::min({radius, r.w * 0.5f, r.h * 0.5f});
    if (std::ceil(radius) * 2 > std::min(r.w, r.h)) radius = std::floor(std::min(r.w, r.h) * 0.5f);
    int half = 0;
    ID2D1Bitmap* mask = radius >= 0.75f ? shapeMask(radius, sw, false, half) : nullptr;
    const float k = mask ? float(half) : 0.0f;
    if (mask && sw > k) {  // stroke wider than the corner: let Direct2D do it
        const Rect in = r.inset(sw * 0.5f);
        dc_->DrawRoundedRectangle(D2D1::RoundedRect(in.d2d(), radius, radius), brush(c), sw);
        return;
    }
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    if (!mask) {
        fill({r.x, r.y, r.w, sw}, c);
        fill({r.x, r.b() - sw, r.w, sw}, c);
        fill({r.x, r.y + sw, sw, r.h - 2 * sw}, c);
        fill({r.r() - sw, r.y + sw, sw, r.h - 2 * sw}, c);
    } else {
        if (r.w > 2 * k) {
            fill({r.x + k, r.y, r.w - 2 * k, sw}, c);
            fill({r.x + k, r.b() - sw, r.w - 2 * k, sw}, c);
        }
        if (r.h > 2 * k) {
            fill({r.x, r.y + k, sw, r.h - 2 * k}, c);
            fill({r.r() - sw, r.y + k, sw, r.h - 2 * k}, c);
        }
        maskQuad(mask, r.x, r.y, k, k, 0, 0, c);
        maskQuad(mask, r.r() - k, r.y, k, k, k, 0, c);
        maskQuad(mask, r.x, r.b() - k, k, k, 0, k, c);
        maskQuad(mask, r.r() - k, r.b() - k, k, k, k, k, c);
    }
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Renderer::fillEllipse(float cx, float cy, float rx, float ry, Color c) {
    int half = 0;
    ID2D1Bitmap* mask = (std::fabs(rx - ry) < 0.01f && rx <= 256) ? shapeMask(rx, 0, true, half) : nullptr;
    if (!mask) {
        dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush(c));
        return;
    }
    const float k = float(half);
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    maskQuad(mask, std::round(cx - k), std::round(cy - k), 2 * k, 2 * k, 0, 0, c);
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Renderer::strokeEllipse(float cx, float cy, float rx, float ry, Color c, float width) {
    int half = 0;
    ID2D1Bitmap* mask =
        (std::fabs(rx - ry) < 0.01f && rx <= 256) ? shapeMask(rx, std::max(0.5f, width), true, half) : nullptr;
    if (!mask) {
        dc_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush(c), width);
        return;
    }
    const float k = float(half);
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    maskQuad(mask, std::round(cx - k), std::round(cy - k), 2 * k, 2 * k, 0, 0, c);
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Renderer::line(float x0, float y0, float x1, float y1, Color c, float width, bool dashed) {
    // Horizontal and vertical lines become pixel-aligned rectangles (dashes included); only
    // diagonal lines need anti-aliased geometry.
    const bool horizontal = std::fabs(y1 - y0) < 0.01f, vertical = std::fabs(x1 - x0) < 0.01f;
    if (horizontal || vertical) {
        const float w = std::max(1.0f, std::round(width));
        const float a0 = horizontal ? std::min(x0, x1) : std::min(y0, y1);
        const float a1 = horizontal ? std::max(x0, x1) : std::max(y0, y1);
        const float across = std::round((horizontal ? y0 : x0) - w * 0.5f);
        const float dash = dashed ? std::round(w * 4) : a1 - a0, gap = std::round(w * 3);
        dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        for (float a = std::round(a0); a < a1; a += dash + gap) {
            const float len = std::min(dash, a1 - a);
            fill(horizontal ? Rect{a, across, len, w} : Rect{across, a, w, len}, c);
            if (!dashed) break;
        }
        dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        return;
    }
    dc_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush(c), width, dashed ? dash_.Get() : nullptr);
}

void Renderer::arc(float cx, float cy, float radius, float startDeg, float sweepDeg, Color c, float width) {
    if (std::fabs(sweepDeg) < 0.1f) return;
    if (std::fabs(sweepDeg) >= 359.9f) {
        strokeEllipse(cx, cy, radius, radius, c, width);
        return;
    }
    constexpr float kPi = 3.14159265f;
    const float a0 = (startDeg - 90.0f) * kPi / 180.0f, a1 = (startDeg + sweepDeg - 90.0f) * kPi / 180.0f;
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(cx + radius * std::cos(a0), cy + radius * std::sin(a0)), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + radius * std::cos(a1), cy + radius * std::sin(a1)),
                                  D2D1::SizeF(radius, radius), 0.0f,
                                  sweepDeg > 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                  std::fabs(sweepDeg) > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    ComPtr<ID2D1StrokeStyle> round;
    factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0,
                                &round);
    dc_->DrawGeometry(geo.Get(), brush(c), width, round.Get());
}

Renderer::FormatEntry* Renderer::entry(const TextStyle& st) {
    const uint64_t key = (uint64_t(st.font) << 48) | (uint64_t(st.weight) << 32) | uint64_t(uint32_t(st.size * 64.0f));
    auto it = formats_.find(key);
    if (it != formats_.end()) return &it->second;
    if (formats_.size() > 256) formats_.clear();
    FormatEntry e;
    if (FAILED(dwrite_->CreateTextFormat(families_[int(st.font)].c_str(), nullptr, st.weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, std::max(1.0f, st.size), L"en-us", &e.format)))
        return nullptr;
    dwrite_->CreateEllipsisTrimmingSign(e.format.Get(), &e.ellipsis);
    return &(formats_[key] = std::move(e));
}

IDWriteTextFormat* Renderer::format(const TextStyle& st) {
    FormatEntry* e = entry(st);
    return e ? e->format.Get() : nullptr;
}

// Text layouts are cached across frames (keyed by text, style, box and alignment); most UI
// text is static, so a frame mostly reuses shaped layouts instead of re-shaping every string.
IDWriteTextLayout* Renderer::cachedLayout(std::wstring_view s, const TextStyle& st, float w, float h, Align ha,
                                          Align va, bool wrap) {
    const int wi = std::max(1, int(std::lround(w))), hi = std::max(1, int(std::lround(h)));
    std::wstring key;
    key.reserve(s.size() + 8);
    key.push_back(wchar_t(int(st.font) | (int(ha) << 2) | (int(va) << 4) | (int(wrap) << 6)));
    key.push_back(wchar_t(st.weight));
    key.push_back(wchar_t(std::lround(st.size * 16.0f) & 0xFFFF));
    key.push_back(wchar_t(wi & 0xFFFF));
    key.push_back(wchar_t(hi & 0xFFFF));
    key.append(s);
    if (auto it = layouts_.find(key); it != layouts_.end()) {
        it->second.lastFrame = frame_;
        return it->second.layout.Get();
    }
    FormatEntry* e = entry(st);
    if (!e) return nullptr;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(s.data(), UINT32(s.size()), e->format.Get(), float(wi), float(hi), &layout)))
        return nullptr;
    layout->SetTextAlignment(ha == Align::Start    ? DWRITE_TEXT_ALIGNMENT_LEADING
                             : ha == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                   : DWRITE_TEXT_ALIGNMENT_TRAILING);
    layout->SetParagraphAlignment(va == Align::Start    ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : va == Align::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                        : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    const DWRITE_TRIMMING trim{wrap ? DWRITE_TRIMMING_GRANULARITY_NONE : DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    layout->SetTrimming(&trim, wrap ? nullptr : e->ellipsis.Get());
    if (layouts_.size() > 3000) layouts_.clear();
    return (layouts_[key] = {layout, frame_}).layout.Get();
}

void Renderer::text(std::wstring_view s, const Rect& r, const TextStyle& st, Color c, Align h, Align v, bool wrap) {
    if (s.empty() || r.w <= 0) return;
    if (IDWriteTextLayout* layout = cachedLayout(s, st, r.w, r.h, h, v, wrap))
        dc_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout, brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

float Renderer::textWidth(std::wstring_view s, const TextStyle& st) {
    if (s.empty()) return 0;
    std::wstring key;
    key.push_back(wchar_t(st.font));
    key.push_back(wchar_t(st.weight));
    key.push_back(wchar_t(std::lround(st.size * 16.0f) & 0xFFFF));
    key.append(s);
    if (auto it = metrics_.find(key); it != metrics_.end()) return it->second;
    IDWriteTextLayout* layout = cachedLayout(s, st, 60000.0f, 10000.0f, Align::Start, Align::Start, false);
    DWRITE_TEXT_METRICS m{};
    if (layout) layout->GetMetrics(&m);
    if (metrics_.size() > 4000) metrics_.clear();
    return metrics_[key] = m.widthIncludingTrailingWhitespace;
}

float Renderer::textHeight(std::wstring_view s, const TextStyle& st, float maxWidth) {
    if (s.empty()) return 0;
    IDWriteTextLayout* layout = cachedLayout(s, st, std::max(1.0f, maxWidth), 10000.0f, Align::Start, Align::Start, true);
    DWRITE_TEXT_METRICS m{};
    if (layout) layout->GetMetrics(&m);
    return m.height;
}

void Renderer::icon(wchar_t glyph, float cx, float cy, float size, Color c) {
    const wchar_t s[2] = {glyph, 0};
    text(std::wstring_view(s, 1), Rect::centered(cx, cy, size * 2, size * 2), {Font::Icons, size}, c, Align::Center,
         Align::Center);
}

void Renderer::shadow(const Rect& r, float radius, float blur, float dy, float alpha) {
    if (!shadowFx_) return;
    ComPtr<ID2D1CommandList> list;
    if (FAILED(dc_->CreateCommandList(&list))) return;
    ComPtr<ID2D1Image> old;
    dc_->GetTarget(&old);
    D2D1_MATRIX_3X2_F xf;
    dc_->GetTransform(&xf);
    dc_->SetTarget(list.Get());
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->FillRoundedRectangle(D2D1::RoundedRect(r.d2d(), radius, radius), brush(D2D1::ColorF(0, 0, 0, 1)));
    dc_->SetTarget(old.Get());
    list->Close();
    shadowFx_->SetInput(0, list.Get());
    shadowFx_->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, blur);
    shadowFx_->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0, 0, 0, alpha));
    const D2D1_POINT_2F offset{0, dy};
    dc_->DrawImage(shadowFx_.Get(), &offset);
    dc_->SetTransform(xf);
}

void Renderer::pushClip(const Rect& r) { dc_->PushAxisAlignedClip(r.d2d(), D2D1_ANTIALIAS_MODE_ALIASED); }
void Renderer::popClip() { dc_->PopAxisAlignedClip(); }

void Renderer::pushLayer(float opacity, const Rect* roundClip, float radius) {
    ComPtr<ID2D1RoundedRectangleGeometry> geo;
    if (roundClip) factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(roundClip->d2d(), radius, radius), &geo);
    const D2D1_LAYER_PARAMETERS1 params =
        D2D1::LayerParameters1(D2D1::InfiniteRect(), geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                               D2D1::IdentityMatrix(), opacity, nullptr, D2D1_LAYER_OPTIONS1_NONE);
    dc_->PushLayer(params, nullptr);
}

void Renderer::popLayer() { dc_->PopLayer(); }

}  // namespace sudoku::ui
