// App drawing: flyout menus, dialogs, toasts and confetti.
#include <algorithm>
#include <cmath>

#include "app.h"
#include "engine/date.h"
#include "ui/anim.h"
#include "util.h"

namespace sudoku {

using namespace ui;

namespace {

constexpr double kToastDuration = 4.0;

}  // namespace

// ================================================================ flyouts

void App::drawFlyout() {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float W = float(r.width()), H = float(r.height());
    ui_.setLayer(1);
    ui_.add(Action{ActCloseFlyout}, {0, 0, W, H}, false);

    struct Item {
        int id;
        wchar_t icon;
        std::wstring label, hint;
        bool enabled, separatorBefore;
    };
    std::vector<Item> items;
    const Game& g = game();
    const bool newGame = flyout_ == Flyout::NewGame;
    if (newGame) {
        for (int i = 0; i < kDifficultyCount; ++i)
            items.push_back({i, 0, widen(difficultyName(Difficulty(i))), widen(difficultyBlurb(Difficulty(i))), true, false});
        items.push_back({MenuRestart, 0xE72C, L"Restart this puzzle", L"", g.active, true});
    } else {
        items.push_back({MenuAutoNotes, 0xE794, L"Fill in all notes", L"A", g.isPlaying(), false});
        items.push_back({MenuClearNotes, 0xE894, L"Clear all notes", L"", g.isPlaying(), false});
        if (!settings_.checkMistakes)
            items.push_back({MenuCheckBoard, 0xE73E, L"Check the board", L"Uses a hint", g.isPlaying(), false});
        items.push_back({MenuRestart, 0xE72C, L"Restart puzzle", L"", g.active, true});
    }
    const float itemH = ui_.px(40), sepH = ui_.px(9), w = ui_.px(newGame ? 300 : 250);
    float h = ui_.px(8);
    for (const Item& it : items) h += itemH + (it.separatorBefore ? sepH : 0);
    Rect box{flyoutAnchor_.r() - w, flyoutAnchor_.b() + ui_.px(6), w, h};
    if (box.b() > H - ui_.px(8)) box.y = flyoutAnchor_.y - ui_.px(6) - h;
    box.x = std::round(std::clamp(box.x, ui_.px(8), W - w - ui_.px(8)));
    box.y = std::round(std::max(ui_.px(8), box.y));

    r.shadow(box, ui_.dp(8), ui_.dp(14), ui_.dp(8), p.dark ? 0.45f : 0.18f);
    r.fillRound(box, ui_.dp(8), p.dark ? rgb(0x2C2C2C) : rgb(0xFCFCFC));
    r.strokeRound(box, ui_.dp(8), p.surfaceStroke, std::max(1.0f, ui_.px(1)));
    float y = box.y + ui_.px(4);
    for (const Item& it : items) {
        if (it.separatorBefore) {
            r.fill({box.x, std::round(y + sepH * 0.5f), box.w, std::max(1.0f, ui_.px(1))}, p.divider);
            y += sepH;
        }
        const Rect ir{box.x + ui_.px(4), y, box.w - ui_.px(8), itemH};
        const Action a{ActMenu, int16_t(it.id)};
        if (it.enabled && (ui_.isHot(a) || ui_.hasFocus(a)))
            r.fillRound(ir, ui_.dp(4), ui_.isPressed(a) ? p.subtlePressed : p.subtleHover);
        const Color fg = it.enabled ? p.text : p.textDisabled;
        float tx = ir.x + ui_.px(12);
        if (newGame && it.id < kDifficultyCount) {
            for (int k = 0; k < kDifficultyCount; ++k)
                r.fillRound({tx + float(k) * ui_.px(5), ir.cy() - ui_.px(6) + float(kDifficultyCount - 1 - k) * 0,
                             ui_.px(3), ui_.px(12)},
                            ui_.dp(1.5f), k <= it.id ? p.accent : p.divider);
            tx += ui_.px(40);
        } else if (it.icon) {
            r.icon(it.icon, tx + ui_.dp(8), ir.cy(), ui_.dp(16), fg);
            tx += ui_.px(32);
        }
        r.text(it.label, {tx, ir.y, ir.r() - tx, ir.h}, ui_.body(), fg);
        if (!it.hint.empty())
            r.text(it.hint, {tx, ir.y, ir.r() - tx - ui_.px(10), ir.h}, ui_.caption(), p.text3, Align::End);
        if (it.enabled) ui_.add(a, ir);
        y += itemH;
    }
    ui_.setLayer(0);
}

// ================================================================ dialogs

void App::drawDialog() {
    const double t = now();
    if (t < dialogAt_) return;
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float W = float(r.width()), H = float(r.height());
    ui_.setLayer(2);
    const float fade = animationsOn() ? clamp01((t - dialogAt_) / 0.18) : 1.0f;
    r.fill({0, 0, W, H}, withAlpha(p.smoke, p.smoke.a * fade));
    ui_.add(Action{ActBlock}, {0, 0, W, H}, false);

    struct Btn {
        std::wstring label;
        ButtonKind kind;
    };
    std::wstring title, body;
    std::vector<Btn> buttons;
    float width = ui_.px(440), customH = 0;
    const Game& g = game();
    switch (dialog_) {
    case Dialog::ConfirmNewGame:
        title = L"Start a new game?";
        body = L"Your current game will be recorded as abandoned and counts as a loss in your statistics.";
        buttons = {{L"Start new game", ButtonKind::Accent}, {L"Cancel", ButtonKind::Standard}};
        break;
    case Dialog::ConfirmDaily:
        title = L"Switch daily puzzle?";
        body = L"You have another daily puzzle in progress. It will be recorded as abandoned.";
        buttons = {{L"Switch", ButtonKind::Accent}, {L"Cancel", ButtonKind::Standard}};
        break;
    case Dialog::ConfirmRestart:
        title = L"Restart this puzzle?";
        body = L"All your numbers, notes and time on this puzzle will be cleared.";
        buttons = {{L"Restart", ButtonKind::Accent}, {L"Cancel", ButtonKind::Standard}};
        break;
    case Dialog::GameOver:
        title = L"Out of mistakes";
        body = L"You've made " + std::to_wstring(g.mistakes) +
               L" mistakes. Take a second chance to keep going, or start again.";
        if (g.mode == GameMode::Daily)
            buttons = {{L"Second chance", ButtonKind::Accent}, {L"Back to calendar", ButtonKind::Standard}};
        else
            buttons = {{L"Second chance", ButtonKind::Accent}, {L"New game", ButtonKind::Standard}, {L"Give up", ButtonKind::Standard}};
        break;
    case Dialog::ResetStats:
        title = L"Reset statistics?";
        body = L"This permanently deletes your game history, best times and streaks. Unlocked achievements are kept.";
        buttons = {{L"Reset statistics", ButtonKind::Accent}, {L"Cancel", ButtonKind::Standard}};
        break;
    case Dialog::ResetAchievements:
        title = L"Reset all progress?";
        body = L"This permanently deletes your game history and every achievement.";
        buttons = {{L"Reset everything", ButtonKind::Accent}, {L"Cancel", ButtonKind::Standard}};
        break;
    case Dialog::Help:
        title = L"Keyboard shortcuts";
        width = ui_.px(560);
        customH = ui_.px(30) * 14;
        buttons = {{L"Close", ButtonKind::Accent}};
        break;
    case Dialog::Generating:
        title = L"Creating your puzzle";
        body = generatingDaily_ ? L"Generating the daily challenge…"
                                : L"Generating a unique " + widen(difficultyName(pendingDifficulty_)) + L" puzzle…";
        customH = ui_.px(56);
        buttons = {{L"Cancel", ButtonKind::Standard}};
        break;
    case Dialog::Win:
        width = ui_.px(420);
        customH = ui_.px(300);
        buttons = {{win_.mode == GameMode::Daily ? L"Back to calendar" : L"New game", ButtonKind::Accent},
                   {L"Close", ButtonKind::Standard}};
        break;
    default: break;
    }
    width = std::min(width, W - ui_.px(32));
    const float pad = ui_.px(24);
    const float textW = width - pad * 2;
    const float titleH = title.empty() ? 0 : ui_.px(36);
    const float bodyH = body.empty() ? 0 : std::ceil(r.textHeight(body, ui_.body(), textW)) + ui_.px(4);
    const float footerH = ui_.px(80);
    const float height = pad + titleH + bodyH + customH + ui_.px(8) + footerH;
    const float slide = animationsOn() ? (1.0f - easeOutCubic(fade)) * ui_.dp(16) : 0;
    const Rect box{std::round((W - width) * 0.5f), std::round((H - height) * 0.5f + slide), width, height};

    r.shadow(box, ui_.dp(8), ui_.dp(24), ui_.dp(12), (p.dark ? 0.5f : 0.22f) * fade);
    r.fillRound(box, ui_.dp(8), p.surface);
    const Rect footer{box.x, box.b() - footerH, box.w, footerH};
    // Footer: rounded at the bottom (shares the dialog's corners), square where it meets the body.
    r.fillRound(footer, ui_.dp(8), p.surfaceFooter);
    r.fill({footer.x, footer.y, footer.w, ui_.px(10)}, p.surfaceFooter);
    r.fill({footer.x, footer.y, footer.w, std::max(1.0f, ui_.px(1))}, p.divider);
    r.strokeRound(box, ui_.dp(8), p.surfaceStroke, std::max(1.0f, ui_.px(1)));

    float y = box.y + pad;
    if (!title.empty()) {
        r.text(title, {box.x + pad, y, textW, ui_.px(28)}, ui_.subtitle(), p.text);
        y += titleH;
    }
    if (!body.empty()) {
        r.text(body, {box.x + pad, y, textW, bodyH}, ui_.body(), p.text, Align::Start, Align::Start, true);
        y += bodyH;
    }

    if (dialog_ == Dialog::Help) {
        struct K {
            const wchar_t* keys;
            const wchar_t* what;
        };
        static const K rows[] = {
            {L"Arrow keys", L"Move the selection"},
            {L"1 – 9", L"Place a number (a note in notes mode)"},
            {L"Shift / Alt + 1 – 9", L"Toggle a note"},
            {L"0 · Del · Backspace", L"Erase"},
            {L"N · Space", L"Notes mode on / off"},
            {L"A", L"Fill in all notes"},
            {L"Ctrl+Z · Ctrl+Y", L"Undo · redo"},
            {L"H", L"Hint (press again for the explanation)"},
            {L"Enter", L"Apply the hint"},
            {L"P · Esc", L"Pause"},
            {L"Ctrl+N", L"New game"},
            {L"Ctrl+1 – 5", L"Switch page"},
            {L"F11", L"Full screen"},
            {L"Tab · Enter", L"Move between and press buttons"},
        };
        const float rowH = ui_.px(30), keyW = textW * 0.42f;
        for (const K& k : rows) {
            const float kw = std::min(keyW - ui_.px(8), r.textWidth(k.keys, ui_.caption()) + ui_.px(16));
            const Rect cap{box.x + pad, y + ui_.px(4), kw, rowH - ui_.px(8)};
            r.fillRound(cap, ui_.dp(4), p.control);
            r.strokeRound(cap, ui_.dp(4), p.controlStroke, std::max(1.0f, ui_.px(1)));
            r.text(k.keys, cap, {Font::Text, ui_.dp(12), DWRITE_FONT_WEIGHT_SEMI_BOLD}, p.text, Align::Center, Align::Center);
            r.text(k.what, {box.x + pad + keyW, y, textW - keyW, rowH}, ui_.body(), p.text2);
            y += rowH;
        }
    } else if (dialog_ == Dialog::Generating) {
        ui_.progressRing(box.cx(), y + ui_.px(26), ui_.px(16), ui_.dp(3));
        y += customH;
    } else if (dialog_ == Dialog::Win) {
        const float cx = box.cx();
        const float iconY = y + ui_.px(30);
        r.fillEllipse(cx, iconY, ui_.px(30), ui_.px(30), p.accent);
        r.icon(0xE73E, cx, iconY, ui_.dp(26), p.onAccent);
        y = iconY + ui_.px(42);
        r.text(L"Puzzle solved!", {box.x, y, box.w, ui_.px(36)}, ui_.title(), p.text, Align::Center, Align::Center);
        y += ui_.px(38);
        std::wstring what = widen(difficultyName(win_.difficulty));
        if (win_.mode == GameMode::Daily)
            what = L"Daily challenge · " + std::wstring(monthName(dateMonth(win_.dailyDate))) + L" " +
                   std::to_wstring(dateDay(win_.dailyDate)) + L" · " + what;
        r.text(what, {box.x, y, box.w, ui_.px(20)}, ui_.body(), p.text2, Align::Center, Align::Center);
        y += ui_.px(26);
        r.text(formatTime(win_.timeMs), {box.x, y, box.w, ui_.px(56)}, {Font::Display, ui_.dp(48), DWRITE_FONT_WEIGHT_SEMI_BOLD},
               p.text, Align::Center, Align::Center);
        y += ui_.px(60);
        std::vector<std::pair<std::wstring, Color>> badges;
        if (win_.newBest) badges.push_back({L"New best time", p.success});
        if (win_.perfect) badges.push_back({L"Perfect – no mistakes, no hints", p.accentText});
        else badges.push_back({std::to_wstring(win_.mistakes) + (win_.mistakes == 1 ? L" mistake · " : L" mistakes · ") +
                                   std::to_wstring(win_.hints) + (win_.hints == 1 ? L" hint" : L" hints"),
                               p.text2});
        float bwTotal = 0;
        for (auto& b : badges) bwTotal += r.textWidth(b.first, ui_.caption()) + ui_.px(24) + ui_.px(8);
        float bx = cx - (bwTotal - ui_.px(8)) * 0.5f;
        for (auto& b : badges) {
            const float bw = r.textWidth(b.first, ui_.caption()) + ui_.px(24);
            const Rect pill{std::round(bx), y, bw, ui_.px(26)};
            r.fillRound(pill, pill.h * 0.5f, withAlpha(b.second, 0.14f));
            r.text(b.first, pill, {Font::Text, ui_.dp(12), DWRITE_FONT_WEIGHT_SEMI_BOLD}, b.second, Align::Center,
                   Align::Center);
            bx += bw + ui_.px(8);
        }
        y += ui_.px(40);
        const std::wstring vals[3] = {formatTime(win_.bestMs), formatTime(win_.averageMs), std::to_wstring(win_.streak)};
        const wchar_t* labels[3] = {L"Best", L"Average", win_.mode == GameMode::Daily ? L"Day streak" : L"Win streak"};
        const float cw = textW / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const Rect c{box.x + pad + cw * float(i), y, cw, ui_.px(48)};
            r.text(vals[i], {c.x, c.y, c.w, ui_.px(26)}, ui_.subtitle(), p.text, Align::Center, Align::Center);
            r.text(labels[i], {c.x, c.y + ui_.px(26), c.w, ui_.px(18)}, ui_.caption(), p.text2, Align::Center, Align::Center);
        }
    }

    // Footer buttons, right-aligned; first button is the primary action.
    const float bh = ui_.px(32), gap = ui_.px(8);
    const int n = int(buttons.size());
    const float bw = n == 1 ? std::min(ui_.px(160), textW) : std::floor((textW - gap * float(n - 1)) / float(n));
    for (int i = 0; i < n; ++i) {
        const Rect br{box.x + pad + float(i) * (bw + gap) + (n == 1 ? textW - bw : 0), footer.y + (footerH - bh) * 0.5f, bw, bh};
        ui_.button(Action{ActDialog, int16_t(dialog_ == Dialog::Generating ? 1 : i)}, br, buttons[size_t(i)].label,
                   buttons[size_t(i)].kind);
    }
    ui_.setLayer(0);
}

// ================================================================ toasts & confetti

void App::drawToasts() {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const double t = now();
    while (!toasts_.empty() && t - toasts_.front().start > kToastDuration) toasts_.pop_front();
    if (toasts_.empty()) return;
    const float railW = ui_.px(80);
    const float W = float(r.width()), H = float(r.height());
    const float w = std::min(ui_.px(360), W - railW - ui_.px(32)), h = ui_.px(64);
    float y = H - ui_.px(24);
    int shown = 0;
    for (auto it = toasts_.begin(); it != toasts_.end() && shown < 3; ++it) {
        const double age = t - it->start;
        if (age < 0) continue;
        ++shown;
        const bool anim = animationsOn();
        const float in = anim ? easeOutCubic(clamp01(age / 0.25)) : 1.0f;
        const float out = anim ? clamp01((kToastDuration - age) / 0.3) : 1.0f;
        const float a = std::min(in, out);
        y -= h + ui_.px(8);
        const Rect box{std::round(railW + (W - railW - w) * 0.5f), std::round(y + (1.0f - in) * ui_.dp(24)), w, h};
        auto fade = [&](Color c) { return withAlpha(c, c.a * a); };
        r.shadow(box, ui_.dp(8), ui_.dp(12), ui_.dp(6), (p.dark ? 0.45f : 0.18f) * a);
        r.fillRound(box, ui_.dp(8), fade(p.surface));
        r.strokeRound(box, ui_.dp(8), fade(p.surfaceStroke), std::max(1.0f, ui_.px(1)));
        const float cx = box.x + ui_.px(36);
        r.fillEllipse(cx, box.cy(), ui_.px(18), ui_.px(18), fade(p.accent));
        r.icon(it->icon, cx, box.cy(), ui_.dp(16), fade(p.onAccent));
        r.text(it->title, {box.x + ui_.px(66), box.y + ui_.px(12), box.w - ui_.px(80), ui_.px(18)}, ui_.caption(),
               fade(p.text2));
        r.text(it->text, {box.x + ui_.px(66), box.y + ui_.px(30), box.w - ui_.px(80), ui_.px(22)}, ui_.bodyStrong(),
               fade(p.text));
    }
}

void App::drawConfetti() {
    if (confettiStart_ < 0) return;
    const double t = now() - confettiStart_;
    if (t > 3.4 || !animationsOn()) {
        confetti_.clear();
        confettiStart_ = -1;
        return;
    }
    Renderer& r = renderer_;
    const float H = float(r.height()), s = ui_.scale;
    const float gravity = 420.0f * s;
    const float ft = float(t);
    // Aliased: tiny fast-moving pieces don't need anti-aliasing, and it keeps them cheap quads.
    r.dc()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (const Particle& pt : confetti_) {
        const float x = pt.x + pt.vx * ft + std::sin(ft * 3.0f + pt.rot) * 16.0f * s;
        const float y = pt.y + pt.vy * ft + 0.5f * gravity * ft * ft;
        if (y > H + 20 * s) continue;
        const float alpha = ft > 2.6f ? std::max(0.0f, (3.4f - ft) / 0.8f) : 1.0f;
        const float flip = std::fabs(std::cos(ft * 5.0f + pt.rot));
        r.setTransform(D2D1::Matrix3x2F::Rotation(pt.rot + pt.vr * ft, D2D1::Point2F(x, y)));
        r.fill(Rect::centered(x, y, pt.w, std::max(1.0f, pt.h * flip)), withAlpha(pt.color, alpha));
    }
    r.resetTransform();
    r.dc()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

}  // namespace sudoku
