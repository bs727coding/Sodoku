// GPU renderer: D3D11 device -> premultiplied-alpha composition swap chain -> DirectComposition
// visual (so the Mica backdrop shows through transparent pixels) -> Direct2D device context.
// Also supports an offscreen target for PNG screenshots. All coordinates are physical pixels.
#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <string>
#include <string_view>
#include <unordered_map>

#include "ui/theme.h"

namespace sudoku::ui {

using Microsoft::WRL::ComPtr;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    constexpr float r() const { return x + w; }
    constexpr float b() const { return y + h; }
    constexpr float cx() const { return x + w * 0.5f; }
    constexpr float cy() const { return y + h * 0.5f; }
    constexpr bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    constexpr Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    constexpr Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    constexpr Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    Rect takeTop(float t) { Rect o{x, y, w, t}; y += t; h -= t; return o; }
    Rect takeBottom(float t) { Rect o{x, b() - t, w, t}; h -= t; return o; }
    Rect takeLeft(float t) { Rect o{x, y, t, h}; x += t; w -= t; return o; }
    Rect takeRight(float t) { Rect o{r() - t, y, t, h}; w -= t; return o; }
    D2D1_RECT_F d2d() const { return {x, y, x + w, y + h}; }
    static constexpr Rect centered(float cx, float cy, float w, float h) { return {cx - w * 0.5f, cy - h * 0.5f, w, h}; }
};

enum class Font : uint8_t { Text, Display, Icons };
enum class Align : uint8_t { Start, Center, End };

struct TextStyle {
    Font font = Font::Text;
    float size = 14.0f;  // pixels
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
};

class Renderer {
public:
    ~Renderer();
    bool initWindow(HWND hwnd);
    bool initOffscreen(UINT w, UINT h);
    void resize(UINT w, UINT h);
    bool beginDraw();
    void endDraw();
    bool savePng(const std::wstring& path);
    void waitForFrame();
    void trimMemory();  // release cached GPU resources (e.g. when minimized)

    UINT width() const { return width_; }
    UINT height() const { return height_; }
    uint32_t generation() const { return generation_; }  // bumps when device resources are recreated
    ID2D1DeviceContext* dc() const { return dc_.Get(); }
    IDWriteFactory* dwrite() const { return dwrite_.Get(); }
    ID2D1Factory1* factory() const { return factory_.Get(); }

    void clear(Color c);
    void fill(const Rect& r, Color c);
    void fillRound(const Rect& r, float radius, Color c);
    void strokeRound(const Rect& r, float radius, Color c, float width);
    void fillEllipse(float cx, float cy, float rx, float ry, Color c);
    void strokeEllipse(float cx, float cy, float rx, float ry, Color c, float width);
    void line(float x0, float y0, float x1, float y1, Color c, float width, bool dashed = false);
    void arc(float cx, float cy, float radius, float startDeg, float sweepDeg, Color c, float width);
    void text(std::wstring_view s, const Rect& r, const TextStyle& st, Color c, Align h = Align::Start,
              Align v = Align::Center, bool wrap = false);
    float textWidth(std::wstring_view s, const TextStyle& st);
    float textHeight(std::wstring_view s, const TextStyle& st, float maxWidth);
    void icon(wchar_t glyph, float cx, float cy, float size, Color c);
    // Soft drop shadow under a rounded rectangle. Only call with no clips/layers pushed.
    void shadow(const Rect& r, float radius, float blur, float dy, float alpha);

    void pushClip(const Rect& r);
    void popClip();
    void pushLayer(float opacity, const Rect* roundClip = nullptr, float radius = 0);
    void popLayer();
    void setTransform(const D2D1_MATRIX_3X2_F& m) { dc_->SetTransform(m); }
    void resetTransform() { dc_->SetTransform(D2D1::Matrix3x2F::Identity()); }

    IDWriteTextFormat* format(const TextStyle& st);
    float capHeight(Font f) const { return capHeight_[int(f)]; }  // fraction of the em size
    const wchar_t* family(Font f) const { return families_[int(f)].c_str(); }
    ID2D1SolidColorBrush* brush(Color c);

private:
    struct FormatEntry {
        ComPtr<IDWriteTextFormat> format;
        ComPtr<IDWriteInlineObject> ellipsis;
    };
    struct LayoutEntry {
        ComPtr<IDWriteTextLayout> layout;
        uint64_t lastFrame = 0;
    };

    bool createDeviceIndependent();
    bool createDevice();
    void releaseDevice();
    bool createTarget();
    void handleDeviceLost();
    FormatEntry* entry(const TextStyle& st);
    IDWriteTextLayout* cachedLayout(std::wstring_view s, const TextStyle& st, float w, float h, Align ha, Align va,
                                    bool wrap);
    // Anti-aliased shapes are drawn from small cached alpha masks (corner / circle quads) instead of
    // tessellated geometry: cheaper per frame, and it keeps the GPU driver from growing large
    // vertex-buffer pools.
    ID2D1Bitmap* shapeMask(float radius, float stroke, bool circle, int& half);
    void maskQuad(ID2D1Bitmap* mask, float x, float y, float w, float h, float sx, float sy, Color c);

    HWND hwnd_ = nullptr;
    bool offscreen_ = false;
    UINT width_ = 1, height_ = 1;
    uint32_t generation_ = 0;
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<IDXGISwapChain2> swap_;
    HANDLE frameWait_ = nullptr;
    ComPtr<IDCompositionDevice> dcomp_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> dash_;
    ComPtr<ID2D1Effect> shadowFx_;
    std::unordered_map<uint64_t, FormatEntry> formats_;
    std::unordered_map<std::wstring, LayoutEntry> layouts_;
    std::unordered_map<std::wstring, float> metrics_;
    std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap>> masks_;
    uint64_t frame_ = 0;
    std::wstring families_[3];
    float capHeight_[3] = {0.7f, 0.7f, 0.7f};
};

}  // namespace sudoku::ui
