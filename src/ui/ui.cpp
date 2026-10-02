#include "ui/ui.h"

#include <algorithm>

namespace sudoku::ui {

void Ui::beginFrame() {
    regions_.clear();
    clips_.clear();
    layer_ = 0;
}

void Ui::pushClip(const Rect& clip) { clips_.push_back(clip); }

void Ui::popClip() {
    if (!clips_.empty()) clips_.pop_back();
}

void Ui::add(const Action& a, const Rect& rect, bool focusable, std::wstring_view tooltip) {
    if (!a.valid()) return;
    Region reg;
    reg.action = a;
    reg.rect = rect;
    reg.layer = layer_;
    reg.focusable = focusable;
    reg.tooltip = std::wstring(tooltip);
    reg.clip = clips_.empty() ? Rect{-1e7f, -1e7f, 2e7f, 2e7f} : clips_.back();
    regions_.push_back(std::move(reg));
}

int Ui::topLayer() const {
    int top = 0;
    for (const Region& g : drawn_) top = std::max(top, g.layer);
    return top;
}

const Region* Ui::hitTest(float x, float y) const {
    const int top = topLayer();
    for (auto it = drawn_.rbegin(); it != drawn_.rend(); ++it)
        if (it->layer == top && it->rect.contains(x, y) && it->clip.contains(x, y)) return &*it;
    return nullptr;
}

const Region* Ui::find(const Action& a) const {
    for (const Region& g : drawn_)
        if (g.action == a) return &g;
    return nullptr;
}

bool Ui::moveFocus(int dir) {
    const int top = topLayer();
    std::vector<const Region*> list;
    for (const Region& g : drawn_)
        if (g.layer == top && g.focusable) list.push_back(&g);
    if (list.empty()) return false;
    const int n = int(list.size());
    int idx = -1;
    for (int i = 0; i < n; ++i)
        if (list[i]->action == focus) idx = i;
    idx = idx < 0 ? (dir > 0 ? 0 : n - 1) : (idx + dir + n) % n;
    focus = list[idx]->action;
    focusVisible = true;
    return true;
}

void Ui::ensureFocusValid() {
    const int top = topLayer();
    for (const Region& g : drawn_)
        if (g.layer == top && g.action == focus) return;
    focus = {};
}

// ---------------------------------------------------------------- widgets

void Ui::focusRing(const Rect& rc, float radius) {
    r->strokeRound(rc.inset(-dp(3)), radius + dp(3), pal.focusOuter, dp(2));
    r->strokeRound(rc.inset(-dp(1)), radius + dp(1), pal.focusInner, std::max(1.0f, dp(1)));
}

void Ui::button(const Action& a, const Rect& rc, std::wstring_view label, ButtonKind kind, wchar_t icon, bool enabled,
                std::wstring_view tip) {
    const bool hover = enabled && isHot(a), down = enabled && isPressed(a);
    const float rad = dp(4);
    Color fillC{}, textC = pal.text;
    bool stroke = false;
    switch (kind) {
    case ButtonKind::Accent:
        fillC = !enabled ? pal.accentDisabled : down ? pal.accentPressed : hover ? pal.accentHover : pal.accent;
        textC = enabled ? pal.onAccent : (pal.dark ? rgb(0xFFFFFF, 0.53f) : rgb(0xFFFFFF));
        break;
    case ButtonKind::Subtle:
        fillC = down ? pal.subtlePressed : hover ? pal.subtleHover : Color{0, 0, 0, 0};
        textC = enabled ? pal.text : pal.textDisabled;
        break;
    case ButtonKind::Danger:
    case ButtonKind::Standard:
        fillC = !enabled ? pal.controlDisabled : down ? pal.controlPressed : hover ? pal.controlHover : pal.control;
        textC = !enabled ? pal.textDisabled : kind == ButtonKind::Danger ? pal.critical : down ? pal.text2 : pal.text;
        stroke = true;
        break;
    }
    if (fillC.a > 0) r->fillRound(rc, rad, fillC);
    if (stroke) {
        r->strokeRound(rc, rad, pal.controlStroke, std::max(1.0f, px(1)));
        if (!down && !pal.dark) r->fill({rc.x + rad, rc.b() - std::max(1.0f, px(1)), rc.w - 2 * rad, std::max(1.0f, px(1))}, pal.controlStrokeBottom);
    }
    const TextStyle ts = body();
    const float iconSize = dp(16);
    const float tw = label.empty() ? 0 : r->textWidth(label, ts);
    const float gap = (icon && !label.empty()) ? dp(8) : 0;
    const float total = std::min(rc.w - dp(16), (icon ? iconSize : 0) + gap + tw);
    float x = rc.cx() - total * 0.5f;
    if (icon) {
        r->icon(icon, x + iconSize * 0.5f, rc.cy(), iconSize, textC);
        x += iconSize + gap;
    }
    if (!label.empty()) r->text(label, {x, rc.y, rc.r() - x - dp(4), rc.h}, ts, textC, Align::Start, Align::Center);
    if (hasFocus(a)) focusRing(rc, rad);
    if (enabled) add(a, rc, true, tip);
}

void Ui::iconButton(const Action& a, const Rect& rc, wchar_t icon, std::wstring_view tip, bool enabled, bool toggled,
                    float iconDip) {
    const bool hover = enabled && isHot(a), down = enabled && isPressed(a);
    Color bg = toggled ? pal.accentSoft : Color{0, 0, 0, 0};
    if (down) bg = toggled ? withAlpha(pal.accentSoft, pal.accentSoft.a * 0.7f) : pal.subtlePressed;
    else if (hover) bg = toggled ? withAlpha(pal.accentSoft, pal.accentSoft.a * 1.5f) : pal.subtleHover;
    if (bg.a > 0) r->fillRound(rc, dp(4), bg);
    const Color ic = !enabled ? pal.textDisabled : toggled ? pal.accent : (down ? pal.text2 : pal.text);
    r->icon(icon, rc.cx(), rc.cy(), dp(iconDip), ic);
    if (hasFocus(a)) focusRing(rc, dp(4));
    if (enabled) add(a, rc, true, tip);
}

void Ui::toggle(const Action& a, const Rect& rc, bool on) {
    const float w = dp(40), h = dp(20);
    const Rect track{rc.r() - w, std::round(rc.cy() - h * 0.5f), w, h};
    const bool hover = isHot(a), down = isPressed(a);
    float kx;
    Color knob;
    if (on) {
        r->fillRound(track, h * 0.5f, down ? pal.accentPressed : hover ? pal.accentHover : pal.accent);
        knob = pal.onAccent;
        kx = track.r() - h * 0.5f;
    } else {
        r->fillRound(track, h * 0.5f, down ? pal.subtlePressed : hover ? pal.subtleHover : Color{0, 0, 0, 0});
        r->strokeRound(track, h * 0.5f, pal.text2, std::max(1.0f, px(1)));
        knob = pal.text2;
        kx = track.x + h * 0.5f;
    }
    const float kr = down ? dp(7) : hover ? dp(6.5f) : dp(6);
    if (down) kx += on ? -dp(2) : dp(2);
    r->fillEllipse(kx, track.cy(), kr, kr, knob);
    const Rect label{track.x - dp(52), rc.y, dp(40), rc.h};
    r->text(on ? L"On" : L"Off", label, body(), pal.text, Align::End, Align::Center);
    if (hasFocus(a)) focusRing(track, h * 0.5f);
    add(a, {label.x, rc.y, rc.r() - label.x, rc.h});
}

float Ui::segmentedWidth(const std::vector<std::wstring>& labels) {
    float w = 0;
    for (const auto& l : labels) w = std::max(w, r->textWidth(l, bodyStrong()) + dp(28));
    return w * float(labels.size()) + dp(4);
}

void Ui::segmented(uint16_t kind, int16_t group, const Rect& rc, const std::vector<std::wstring>& labels, int selected) {
    const int n = int(labels.size());
    if (!n) return;
    const float rad = dp(5);
    r->fillRound(rc, rad, pal.control);
    r->strokeRound(rc, rad, pal.controlStroke, std::max(1.0f, px(1)));
    const float segW = (rc.w - dp(4)) / float(n);
    for (int i = 0; i < n; ++i) {
        const Action a{kind, group, i};
        const Rect s{rc.x + dp(2) + segW * float(i), rc.y + dp(2), segW, rc.h - dp(4)};
        const bool sel = i == selected;
        if (sel) {
            r->fillRound(s, dp(4), pal.dark ? rgb(0xFFFFFF, 0.10f) : rgb(0xFFFFFF));
            r->strokeRound(s, dp(4), pal.controlStroke, std::max(1.0f, px(1)));
            r->fillRound(Rect::centered(s.cx(), s.b() - dp(3), dp(16), dp(3)), dp(1.5f), pal.accent);
        } else if (isHot(a)) {
            r->fillRound(s, dp(4), isPressed(a) ? pal.subtlePressed : pal.subtleHover);
        }
        r->text(labels[i], s.inset(dp(4), 0), sel ? bodyStrong() : body(), sel ? pal.text : pal.text2, Align::Center,
                Align::Center);
        if (hasFocus(a)) focusRing(s, dp(4));
        add(a, s);
    }
}

void Ui::card(const Rect& rc, const Action& a, float radiusDip) {
    const float rad = dp(radiusDip);
    Color bg = pal.card;
    if (a.valid()) {
        if (isPressed(a)) bg = pal.cardPressed;
        else if (isHot(a)) bg = pal.cardHover;
    }
    r->fillRound(rc, rad, bg);
    r->strokeRound(rc, rad, pal.cardStroke, std::max(1.0f, px(1)));
    if (a.valid()) {
        if (hasFocus(a)) focusRing(rc, rad);
        add(a, rc);
    }
}

void Ui::progressRing(float cx, float cy, float radius, float width) {
    const double t = time;
    const float start = float(std::fmod(t * 300.0, 360.0));
    const float sweep = 160.0f + 110.0f * float(std::sin(t * 2.6));
    r->arc(cx, cy, radius, start, sweep, pal.accent, width);
}

void Ui::progressBar(const Rect& rc, float fraction, Color fillColor) {
    r->fillRound(rc, rc.h * 0.5f, pal.dark ? rgb(0xFFFFFF, 0.12f) : rgb(0x000000, 0.10f));
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0) r->fillRound({rc.x, rc.y, std::max(rc.h, rc.w * f), rc.h}, rc.h * 0.5f, fillColor);
}

void Ui::drawTooltip() {
    if (!hot.valid() || pressed.valid() || time - hotSince < 0.6) return;
    const Region* reg = find(hot);
    if (!reg || reg->tooltip.empty()) return;
    const TextStyle ts = caption();
    const float tw = r->textWidth(reg->tooltip, ts);
    Rect box{mouseX - tw * 0.5f - dp(8), mouseY + dp(22), tw + dp(16), dp(28)};
    box.x = std::clamp(box.x, dp(4), float(r->width()) - box.w - dp(4));
    if (box.b() > float(r->height()) - dp(4)) box.y = mouseY - dp(36);
    box.x = std::round(box.x);
    box.y = std::round(box.y);
    r->shadow(box, dp(4), dp(4), dp(2), pal.dark ? 0.35f : 0.14f);
    r->fillRound(box, dp(4), pal.surface);
    r->strokeRound(box, dp(4), pal.surfaceStroke, std::max(1.0f, px(1)));
    r->text(reg->tooltip, box, ts, pal.text, Align::Center, Align::Center);
}

}  // namespace sudoku::ui
