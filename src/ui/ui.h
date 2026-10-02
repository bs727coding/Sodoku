// Immediate-mode UI helpers. Each frame, widgets draw themselves and register a hit region;
// input is resolved against the regions of the last rendered frame (what the user sees).
#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "ui/renderer.h"

namespace sudoku::ui {

// What activating a region does. The meaning of kind/a/b is defined by the app.
struct Action {
    uint16_t kind = 0;
    int16_t a = 0;
    int32_t b = 0;
    constexpr bool valid() const { return kind != 0; }
    constexpr bool operator==(const Action&) const = default;
};

struct Region {
    Action action;
    Rect rect;
    Rect clip;
    int layer = 0;
    bool focusable = true;
    std::wstring tooltip;
};

enum class ButtonKind : uint8_t { Standard, Accent, Subtle, Danger };

class Ui {
public:
    Renderer* r = nullptr;
    Palette pal;
    float scale = 1.0f;
    double time = 0;  // seconds, for animations

    float mouseX = -1, mouseY = -1;
    Action hot, pressed, focus;
    bool focusVisible = false;
    double hotSince = 0;

    float px(float dip) const { return std::round(dip * scale); }
    float dp(float dip) const { return dip * scale; }

    // Windows 11 type ramp
    TextStyle caption() const { return {Font::Text, dp(12)}; }
    TextStyle body() const { return {Font::Text, dp(14)}; }
    TextStyle bodyStrong() const { return {Font::Text, dp(14), DWRITE_FONT_WEIGHT_SEMI_BOLD}; }
    TextStyle bodyLarge() const { return {Font::Text, dp(18)}; }
    TextStyle subtitle() const { return {Font::Display, dp(20), DWRITE_FONT_WEIGHT_SEMI_BOLD}; }
    TextStyle title() const { return {Font::Display, dp(28), DWRITE_FONT_WEIGHT_SEMI_BOLD}; }
    TextStyle titleLarge() const { return {Font::Display, dp(40), DWRITE_FONT_WEIGHT_SEMI_BOLD}; }

    // Frame & regions
    void beginFrame();
    void setLayer(int layer) { layer_ = layer; }
    int layer() const { return layer_; }
    void pushClip(const Rect& clip);
    void popClip();
    void add(const Action& a, const Rect& rect, bool focusable = true, std::wstring_view tooltip = {});
    const Region* hitTest(float x, float y) const;
    const Region* find(const Action& a) const;
    int topLayer() const;
    bool moveFocus(int dir);  // Tab traversal within the top layer
    void ensureFocusValid();

    bool isHot(const Action& a) const { return a.valid() && hot == a && (!pressed.valid() || pressed == a); }
    bool isPressed(const Action& a) const { return a.valid() && pressed == a && hot == a; }
    bool hasFocus(const Action& a) const { return focusVisible && a.valid() && focus == a; }

    // Widgets
    void button(const Action& a, const Rect& rc, std::wstring_view label, ButtonKind kind = ButtonKind::Standard,
                wchar_t icon = 0, bool enabled = true, std::wstring_view tip = {});
    void iconButton(const Action& a, const Rect& rc, wchar_t icon, std::wstring_view tip, bool enabled = true,
                    bool toggled = false, float iconDip = 16);
    void toggle(const Action& a, const Rect& rc, bool on);  // switch, right-aligned inside rc
    void segmented(uint16_t kind, int16_t group, const Rect& rc, const std::vector<std::wstring>& labels, int selected);
    float segmentedWidth(const std::vector<std::wstring>& labels);
    void card(const Rect& rc, const Action& a = {}, float radiusDip = 8);
    void focusRing(const Rect& rc, float radius);
    void progressRing(float cx, float cy, float radius, float width);
    void progressBar(const Rect& rc, float fraction, Color fillColor);
    void drawTooltip();

private:
    std::vector<Region> regions_;
    std::vector<Region> drawn_;  // regions of the last completed frame
    std::vector<Rect> clips_;
    int layer_ = 0;

public:
    void endFrame() { drawn_.swap(regions_); }
    const std::vector<Region>& regions() const { return drawn_; }
};

}  // namespace sudoku::ui
