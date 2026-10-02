// App drawing: window frame, navigation rail, the game page and the new-game picker.
#include <algorithm>
#include <cmath>

#include "app.h"
#include "engine/date.h"
#include "ui/anim.h"
#include "util.h"

namespace sudoku {

using namespace ui;

namespace {

std::wstring shortDate(int date) {
    static const wchar_t* months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                      L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    return std::wstring(months[(dateMonth(date) - 1) % 12]) + L" " + std::to_wstring(dateDay(date));
}

}  // namespace

std::wstring App::formatTime(int64_t ms) const { return formatDuration(ms); }

// ================================================================ frame

void App::drawFrame() {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float W = float(r.width()), H = float(r.height());
    if (micaActive_ && !offscreen_) r.clear({0, 0, 0, 0});
    else r.clear(p.backdrop);

    const float railW = ui_.px(80);
    drawRail({0, 0, railW, H});

    const float rad = ui_.px(8);
    const Rect layer{railW, 0, W - railW + rad * 2, H + rad * 2};
    r.fillRound(layer, rad, p.layer);
    r.strokeRound(layer, rad, p.layerStroke, std::max(1.0f, ui_.px(1)));

    const Rect content{railW, 0, W - railW, H};
    float slide = 0;
    if (animationsOn()) {
        const float t = progress(ui_.time, pageChangedAt_, 0.22);
        if (t >= 0) slide = std::round((1.0f - easeOutCubic(t)) * ui_.dp(16));
    }

    if (page_ == Page::Play && game().active) {
        drawGame(content.offset(0, slide));
    } else {
        viewport_ = content;
        float& sc = scroll_[int(page_)];
        const float maxScroll = std::max(0.0f, contentHeight_[int(page_)] - content.h);
        sc = std::clamp(sc, 0.0f, maxScroll);
        r.pushClip(content);
        ui_.pushClip(content);
        const Rect area{content.x, content.y - std::round(sc) + slide, content.w, content.h};
        float h = 0;
        switch (page_) {
        case Page::Play: h = drawPicker(area); break;
        case Page::Daily: h = drawDaily(area); break;
        case Page::Stats: h = drawStats(area); break;
        case Page::Achievements: h = drawAchievements(area); break;
        case Page::Settings: h = drawSettings(area); break;
        default: break;
        }
        ui_.popClip();
        r.popClip();
        contentHeight_[int(page_)] = h;
        if (maxScroll > 0) {
            const float trackH = content.h - ui_.px(16);
            const float thumbH = std::max(ui_.px(32), trackH * content.h / h);
            const float ty = content.y + ui_.px(8) + (trackH - thumbH) * (sc / maxScroll);
            r.fillRound({content.r() - ui_.px(7), ty, ui_.px(3), thumbH}, ui_.px(1.5f), p.text3);
        }
    }

    drawToasts();
    if (flyout_ != Flyout::None) drawFlyout();
    if (dialog_ != Dialog::None) drawDialog();
    drawConfetti();
    ui_.drawTooltip();
}

void App::drawRail(const Rect& rail) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    struct Item {
        Page page;
        wchar_t icon;
        const wchar_t* label;
        const wchar_t* tip;
    };
    static const Item items[] = {
        {Page::Play, 0xE80A, L"Play", L"Play (Ctrl+1)"},
        {Page::Daily, 0xE787, L"Daily", L"Daily challenge (Ctrl+2)"},
        {Page::Stats, 0xE9D2, L"Stats", L"Statistics (Ctrl+3)"},
        {Page::Achievements, 0xE734, L"Awards", L"Achievements (Ctrl+4)"},
    };
    const Item settingsItem{Page::Settings, 0xE713, L"Settings", L"Settings (Ctrl+5)"};
    const float itemW = ui_.px(64), itemH = ui_.px(58);

    auto drawItem = [&](const Item& it, float y) {
        const Rect rc{std::round(rail.cx() - itemW * 0.5f), y, itemW, itemH};
        const Action a{ActNav, int16_t(it.page)};
        const bool sel = page_ == it.page;
        const bool hover = ui_.isHot(a), down = ui_.isPressed(a);
        Color bg = sel ? p.subtleHover : Color{0, 0, 0, 0};
        if (down) bg = p.subtlePressed;
        else if (hover) bg = sel ? withAlpha(p.subtleHover, p.subtleHover.a * 1.6f) : p.subtleHover;
        if (bg.a > 0) r.fillRound(rc, ui_.dp(5), bg);
        if (sel) r.fillRound({rc.x, std::round(rc.cy() - ui_.dp(9)), ui_.px(3), ui_.px(18)}, ui_.dp(1.5f), p.accent);
        r.icon(it.icon, rc.cx(), rc.y + rc.h * 0.38f, ui_.dp(18), sel ? p.accent : p.text);
        r.text(it.label, {rc.x, rc.y + rc.h * 0.62f, rc.w, rc.h * 0.3f}, {Font::Text, ui_.dp(11)},
               sel ? p.text : p.text2, Align::Center, Align::Center);
        if (ui_.hasFocus(a)) ui_.focusRing(rc, ui_.dp(5));
        ui_.add(a, rc, true, it.tip);
    };
    float y = rail.y + ui_.px(6);
    for (const Item& it : items) {
        drawItem(it, y);
        y += itemH + ui_.px(4);
    }
    drawItem(settingsItem, rail.b() - ui_.px(6) - itemH);
}

// ================================================================ game page

void App::drawGame(Rect area) {
    const float pad = ui_.px(area.w < ui_.dp(720) ? 16 : 28);
    Rect inner = area.inset(pad);
    const Rect top = inner.takeTop(ui_.px(44));
    inner.takeTop(ui_.px(12));
    const float gap = ui_.px(24);
    const float panelMin = ui_.px(260), panelMax = ui_.px(340);
    const bool landscape = inner.w >= inner.h * 1.1f && inner.w - panelMin - gap >= ui_.px(300);

    if (landscape) {
        const float panelW = std::clamp(std::floor(inner.w * 0.36f), panelMin, panelMax);
        const float boardMax = std::min(inner.h, inner.w - panelW - gap);
        boardGeom_ = layoutBoard({0, 0, boardMax, boardMax}, ui_.scale);
        const float bs = float(boardGeom_.size);
        const float groupW = bs + gap + panelW;
        const float ox = std::round(inner.x + (inner.w - groupW) * 0.5f);
        boardGeom_.x = ox;
        boardGeom_.y = std::round(inner.y);
        drawTopBar({ox, top.y, groupW, top.h});
        drawSidePanel({ox + bs + gap, boardGeom_.y, panelW, bs});
    } else {
        const float ctrlH = ui_.px(60) + ui_.px(10) + ui_.px(60);
        const float boardMax = std::max(ui_.px(200), std::min(inner.w, inner.h - ctrlH - ui_.px(16)));
        boardGeom_ = layoutBoard({0, 0, boardMax, boardMax}, ui_.scale);
        const float bs = float(boardGeom_.size);
        boardGeom_.x = std::round(inner.cx() - bs * 0.5f);
        boardGeom_.y = std::round(inner.y);
        drawTopBar({boardGeom_.x, top.y, bs, top.h});
        const float by = boardGeom_.y + bs + ui_.px(16);
        drawPortraitControls({boardGeom_.x, by, bs, inner.b() - by});
    }

    BoardState st;
    st.game = &game();
    st.settings = &settings_;
    st.selected = selected_;
    st.digitLock = digitLock_;
    st.paused = paused_;
    st.revealWrong = checkFlashUntil_ > ui_.time;
    st.hint = hintActive_ ? &hint_ : nullptr;
    st.hintStage = hintStage_;
    st.anims = &anims_;
    boardView_.draw(ui_, boardGeom_, st);
    ui_.add(Action{ActBoard}, boardGeom_.bounds(), false);
    if (paused_) drawPausedOverlay();
}

void App::drawTopBar(const Rect& bar) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const Game& g = game();
    const bool compact = bar.w < ui_.dp(560);

    // Right cluster: [New game] [pause] timer, then mistakes.
    float rx = bar.r();
    const float bh = ui_.px(32);
    const float nbw = compact ? ui_.px(40) : ui_.px(122);
    const Rect nb{rx - nbw, std::round(bar.cy() - bh * 0.5f), nbw, bh};
    ui_.button(Action{ActNewGameMenu}, nb, compact ? L"" : L"New game", ButtonKind::Standard, 0xE710, true,
               L"New game (Ctrl+N)");
    rx = nb.x - ui_.px(8);
    const Rect pb{rx - bh, nb.y, bh, bh};
    ui_.iconButton(Action{paused_ ? ActResume : ActPause}, pb, paused_ ? 0xE768 : 0xE769,
                   paused_ ? L"Resume (P)" : L"Pause (P)", g.isPlaying());
    rx = pb.x - ui_.px(4);
    if (settings_.showTimer) {
        const std::wstring t = formatTime(elapsedOf(g));
        const TextStyle ts = ui_.subtitle();
        const float tw = std::max(r.textWidth(t, ts), r.textWidth(L"00:00", ts));
        r.text(t, {rx - tw, bar.y, tw, bar.h}, ts, paused_ ? p.text3 : p.text, Align::End, Align::Center);
        rx -= tw + ui_.px(20);
    }
    if (settings_.checkMistakes) {
        const int allowance = g.mistakeAllowance(rules());
        std::wstring m = std::to_wstring(g.mistakes);
        if (allowance) m += L"/" + std::to_wstring(allowance);
        const std::wstring label = compact ? L"" : L"Mistakes ";
        const TextStyle ts = ui_.body();
        const float vw = r.textWidth(m, ui_.bodyStrong()), lw = label.empty() ? 0 : r.textWidth(label, ts);
        const Color vc = (allowance && g.mistakes >= allowance - 1 && g.mistakes > 0) ? p.critical : p.text;
        r.text(m, {rx - vw, bar.y, vw + 1, bar.h}, ui_.bodyStrong(), vc, Align::Start, Align::Center);
        if (!label.empty()) r.text(label, {rx - vw - lw, bar.y, lw, bar.h}, ts, p.text2, Align::Start, Align::Center);
        rx -= vw + lw + ui_.px(16);
    }

    // Left: difficulty (and daily date).
    float x = bar.x;
    if (g.mode == GameMode::Daily) {
        const std::wstring chip = L"Daily · " + shortDate(g.dailyDate);
        const float cw = r.textWidth(chip, ui_.caption()) + ui_.px(20);
        const Rect cr{x, std::round(bar.cy() - ui_.dp(12)), cw, ui_.px(24)};
        r.fillRound(cr, ui_.dp(12), p.accentSoft);
        r.text(chip, cr, {Font::Text, ui_.dp(12), DWRITE_FONT_WEIGHT_SEMI_BOLD}, p.accentText, Align::Center,
               Align::Center);
        x += cw + ui_.px(10);
    }
    r.text(widen(difficultyName(g.difficulty)), {x, bar.y, std::max(0.0f, rx - x), bar.h}, ui_.subtitle(), p.text,
           Align::Start, Align::Center);
}

void App::drawActionButton(const Action& a, const Rect& rc, wchar_t icon, std::wstring_view label, bool toggled,
                           bool enabled, std::wstring_view tip) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const bool hover = enabled && ui_.isHot(a), down = enabled && ui_.isPressed(a);
    Color bg = toggled ? p.accentSoft : Color{0, 0, 0, 0};
    if (down) bg = p.subtlePressed;
    else if (hover) bg = toggled ? withAlpha(p.accentSoft, std::min(1.0f, p.accentSoft.a * 1.6f)) : p.subtleHover;
    if (bg.a > 0) r.fillRound(rc, ui_.dp(6), bg);
    const Color fg = !enabled ? p.textDisabled : toggled ? p.accent : p.text;
    const float iy = rc.y + rc.h * 0.38f;
    r.icon(icon, rc.cx(), iy, ui_.dp(20), fg);
    r.text(label, {rc.x, rc.y + rc.h * 0.62f, rc.w, rc.h * 0.30f}, {Font::Text, ui_.dp(12)},
           !enabled ? p.textDisabled : toggled ? p.accent : p.text2, Align::Center, Align::Center);
    if (a.kind == ActNotes) {
        const Rect pill = Rect::centered(std::round(rc.cx() + ui_.dp(15)), std::round(iy - ui_.dp(11)), ui_.px(26),
                                         ui_.px(14));
        r.fillRound(pill, pill.h * 0.5f, notesMode_ ? p.accent : (p.dark ? rgb(0x5A5A5A) : rgb(0xD8D8D8)));
        r.text(notesMode_ ? L"ON" : L"OFF", pill, {Font::Text, ui_.dp(8.5f), DWRITE_FONT_WEIGHT_BOLD},
               notesMode_ ? p.onAccent : p.text2, Align::Center, Align::Center);
    }
    if (ui_.hasFocus(a)) ui_.focusRing(rc, ui_.dp(6));
    if (enabled) ui_.add(a, rc, true, tip);
}

void App::drawNumpadKey(int d, const Rect& rc, bool compact) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const Game& g = game();
    const int remaining = std::max(0, 9 - g.placedCount(d, rules()));
    const bool done = remaining == 0 && settings_.hideCompleted;
    const Action a{ActDigit, int16_t(d)};
    const bool enabled = g.isPlaying() && !paused_;
    const bool locked = digitLock_ == d;
    const bool hover = enabled && ui_.isHot(a), down = enabled && ui_.isPressed(a);
    const float rad = ui_.dp(6);
    if (locked) {
        r.fillRound(rc, rad, down ? p.accentPressed : hover ? p.accentHover : p.accent);
    } else {
        r.fillRound(rc, rad, down ? p.controlPressed : hover ? p.controlHover : p.control);
        r.strokeRound(rc, rad, p.controlStroke, std::max(1.0f, ui_.px(1)));
    }
    const Color fg = locked ? p.onAccent : (done || !enabled) ? p.textDisabled : notesMode_ ? p.accent : p.text;
    const bool showCount = settings_.showCounts && !done && rc.h >= ui_.dp(44);
    const float size = compact ? std::min(rc.h * 0.46f, ui_.dp(24)) : std::min(rc.h * 0.44f, ui_.dp(32));
    const TextStyle ts{Font::Display, std::round(size),
                       notesMode_ && !locked ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_BOLD};
    const wchar_t digit[2] = {wchar_t(L'0' + d), 0};
    r.text(digit, {rc.x, rc.y, rc.w, showCount ? rc.h * 0.80f : rc.h}, ts, fg, Align::Center, Align::Center);
    if (showCount) {
        r.text(std::to_wstring(remaining), {rc.x, rc.y + rc.h * 0.62f, rc.w, rc.h * 0.30f},
               {Font::Text, std::round(compact ? ui_.dp(10) : ui_.dp(11))}, locked ? withAlpha(p.onAccent, 0.75f) : p.text3,
               Align::Center, Align::Center);
    }
    if (notesMode_ && !compact && rc.w > ui_.dp(48)) r.icon(0xE70F, rc.r() - ui_.dp(12), rc.y + ui_.dp(12), ui_.dp(10), locked ? p.onAccent : p.accent);
    if (ui_.hasFocus(a)) ui_.focusRing(rc, rad);
    if (enabled) ui_.add(a, rc, true);
}

void App::drawInfoCard(const Rect& rc) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const Game& g = game();
    ui_.card(rc);
    const Rect in = rc.inset(ui_.px(16), ui_.px(14));
    const int toFill = 81 - countFilled(g.givens);
    const int filled = std::max(0, g.filledCount() - countFilled(g.givens));
    const int pct = toFill ? filled * 100 / toFill : 100;
    r.text(L"Progress", {in.x, in.y, in.w, ui_.px(20)}, ui_.caption(), p.text2);
    r.text(std::to_wstring(pct) + L"%", {in.x, in.y, in.w, ui_.px(20)}, ui_.bodyStrong(), p.text, Align::End);
    ui_.progressBar({in.x, in.y + ui_.px(28), in.w, ui_.px(4)}, float(pct) / 100.0f, p.accent);
    const LevelStats& ls = g.mode == GameMode::Daily ? stats_.daily : stats_.level[int(g.difficulty)];
    const std::wstring vals[3] = {ls.bestMs ? formatTime(ls.bestMs) : L"—",
                                  ls.won ? formatTime(ls.averageMs()) : L"—",
                                  std::to_wstring(g.mode == GameMode::Daily ? stats_.dailyStreak : ls.currentStreak)};
    const wchar_t* labels[3] = {L"Best", L"Average", L"Streak"};
    const float cw = in.w / 3.0f, sy = in.y + ui_.px(46);
    if (sy + ui_.px(40) > rc.b()) return;
    for (int i = 0; i < 3; ++i) {
        r.text(vals[i], {in.x + cw * float(i), sy, cw, ui_.px(22)}, ui_.bodyStrong(), p.text);
        r.text(labels[i], {in.x + cw * float(i), sy + ui_.px(22), cw, ui_.px(18)}, ui_.caption(), p.text2);
    }
}

void App::drawSidePanel(Rect panel) {
    const Game& g = game();
    const float gapS = ui_.px(8), gap = ui_.px(16), bh = ui_.px(64);
    // Bottom-aligned controls (number pad, then the action row above it); the top slot shows
    // the hint card when a hint is open, otherwise a small progress/records card.
    const float kw = std::floor((panel.w - 2 * gapS) / 3.0f);
    const float infoH = ui_.px(112);
    const float topH = hintActive_ ? drawHintCard({panel.x, panel.y, panel.w, 0}, true) : infoH;
    const float padRoom = panel.h - topH - gap - bh - gap;
    const float kh = std::max(ui_.px(40), std::floor(std::min(kw * 0.86f, (padRoom - 2 * gapS) / 3.0f)));
    const float padH = kh * 3 + gapS * 2;
    const float actY = std::max(panel.b() - padH - gap - bh, panel.y + topH + gap);
    if (hintActive_) drawHintCard({panel.x, panel.y, panel.w, topH}, false);
    else if (actY - gap - panel.y >= ui_.px(72)) drawInfoCard({panel.x, panel.y, panel.w, std::min(infoH, actY - gap - panel.y)});
    float y = actY;
    const bool playing = g.isPlaying() && !paused_;
    struct B {
        Act act;
        wchar_t icon;
        const wchar_t* label;
        const wchar_t* tip;
        bool toggled, enabled;
    };
    const B buttons[] = {
        {ActUndo, 0xE7A7, L"Undo", L"Undo (Ctrl+Z)", false, playing && g.canUndo()},
        {ActErase, 0xE75C, L"Erase", L"Erase (Del)", false, playing && selected_ >= 0},
        {ActNotes, 0xE70F, L"Notes", L"Notes mode (N)", notesMode_, playing},
        {ActHint, 0xEA80, L"Hint", L"Hint (H)", hintActive_, playing},
        {ActMore, 0xE712, L"More", L"More options", flyout_ == Flyout::More, g.active && !paused_},
    };
    const int n = int(std::size(buttons));
    const float bw = std::floor((panel.w - gapS * float(n - 1)) / float(n));
    for (int i = 0; i < n; ++i) {
        const B& b = buttons[i];
        drawActionButton(Action{uint16_t(b.act)}, {panel.x + float(i) * (bw + gapS), y, bw, bh}, b.icon, b.label,
                         b.toggled, b.enabled, b.tip);
    }
    y += bh + gap;
    for (int d = 1; d <= 9; ++d) {
        const int i = d - 1;
        drawNumpadKey(d, {panel.x + float(i % 3) * (kw + gapS), y + float(i / 3) * (kh + gapS), kw, kh}, false);
    }
}

void App::drawPortraitControls(Rect area) {
    const Game& g = game();
    if (hintActive_) {
        const float h = drawHintCard({area.x, area.y, area.w, 0}, true);
        drawHintCard({area.x, area.y, area.w, h}, false);
        return;
    }
    const bool playing = g.isPlaying() && !paused_;
    const float gapS = ui_.px(6), bh = ui_.px(60);
    struct B {
        Act act;
        wchar_t icon;
        const wchar_t* label;
        bool toggled, enabled;
    };
    const B buttons[] = {
        {ActUndo, 0xE7A7, L"Undo", false, playing && g.canUndo()},
        {ActErase, 0xE75C, L"Erase", false, playing && selected_ >= 0},
        {ActNotes, 0xE70F, L"Notes", notesMode_, playing},
        {ActHint, 0xEA80, L"Hint", hintActive_, playing},
        {ActMore, 0xE712, L"More", flyout_ == Flyout::More, g.active && !paused_},
    };
    const float bw = std::floor((area.w - gapS * 4) / 5.0f);
    for (int i = 0; i < 5; ++i)
        drawActionButton(Action{uint16_t(buttons[i].act)}, {area.x + float(i) * (bw + gapS), area.y, bw, bh},
                         buttons[i].icon, buttons[i].label, buttons[i].toggled, buttons[i].enabled, {});
    const float ky = area.y + bh + ui_.px(10);
    const float kw = std::floor((area.w - gapS * 8) / 9.0f), kh = std::min(ui_.px(60), std::max(ui_.px(36), area.b() - ky));
    for (int d = 1; d <= 9; ++d) drawNumpadKey(d, {area.x + float(d - 1) * (kw + gapS), ky, kw, kh}, true);
}

float App::drawHintCard(const Rect& rc, bool measureOnly) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float padIn = ui_.px(16);
    const std::wstring title = widen(hint_.text.title);
    const std::wstring body = widen(hintStage_ == 0 ? hint_.text.nudge : hint_.text.detail);
    const float textW = rc.w - padIn * 2;
    const float titleH = ui_.px(24), btnH = ui_.px(32);
    const float bodyH = std::ceil(r.textHeight(body, ui_.body(), textW));
    const float total = padIn + titleH + ui_.px(8) + bodyH + ui_.px(14) + btnH + padIn;
    if (measureOnly) return total;

    const Rect card{rc.x, rc.y, rc.w, total};
    r.fillRound(card, ui_.dp(8), p.dark ? rgb(0x2D2D2D) : rgb(0xFFFFFF));
    r.strokeRound(card, ui_.dp(8), p.cardStroke, std::max(1.0f, ui_.px(1)));
    r.fillRound({card.x, card.y + ui_.px(12), ui_.px(3), total - ui_.px(24)}, ui_.dp(1.5f), p.accent);
    float y = card.y + padIn;
    r.icon(0xEA80, card.x + padIn + ui_.dp(9), y + titleH * 0.5f, ui_.dp(16), p.accent);
    r.text(title, {card.x + padIn + ui_.px(26), y, textW - ui_.px(60), titleH}, ui_.bodyStrong(), p.text);
    ui_.iconButton(Action{ActHintClose}, {card.r() - padIn - ui_.px(28) + ui_.px(6), y - ui_.px(2), ui_.px(28), ui_.px(28)},
                   0xE711, L"Close hint (Esc)", true, false, 12);
    y += titleH + ui_.px(8);
    r.text(body, {card.x + padIn, y, textW, bodyH + 2}, ui_.body(), p.text, Align::Start, Align::Start, true);
    y += bodyH + ui_.px(14);

    const wchar_t* label = L"Show me";
    Act act = ActHintNext;
    if (hintStage_ >= 1) {
        act = ActHintApply;
        switch (hint_.kind) {
        case HintKind::Mistake: label = L"Remove it"; break;
        case HintKind::BadNotes: label = L"Fix notes"; break;
        case HintKind::Reveal: label = L"Reveal"; break;
        default: label = hint_.step.placeCell >= 0 ? L"Place it" : L"Apply"; break;
        }
    }
    const float bw = std::max(ui_.px(96), r.textWidth(label, ui_.body()) + ui_.px(32));
    ui_.button(Action{uint16_t(act)}, {card.r() - padIn - bw, y, bw, btnH}, label, ButtonKind::Accent, 0, true,
               hintStage_ ? L"Apply (Enter)" : L"Show the full explanation (H)");
    r.text(hintStage_ == 0 ? L"Step 1 of 2" : L"Step 2 of 2", {card.x + padIn, y, textW - bw, btnH}, ui_.caption(),
           p.text3);
    return total;
}

void App::drawPausedOverlay() {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const Rect b = boardGeom_.bounds();
    const Action a{ActResume};
    const float cw = std::min(b.w - ui_.px(32), ui_.px(320)), ch = ui_.px(196);
    const Rect card = Rect::centered(std::round(b.cx()), std::round(b.cy()), std::round(cw), ch);
    r.shadow(card, ui_.dp(8), ui_.dp(16), ui_.dp(6), p.dark ? 0.45f : 0.16f);
    r.fillRound(card, ui_.dp(8), p.surface);
    r.strokeRound(card, ui_.dp(8), p.surfaceStroke, std::max(1.0f, ui_.px(1)));
    const float rad = ui_.px(32);
    const float cy = card.y + ui_.px(24) + rad;
    r.fillEllipse(card.cx(), cy, rad, rad, ui_.isHot(a) ? p.accentHover : p.accent);
    r.icon(0xE768, card.cx() + ui_.dp(2), cy, ui_.dp(24), p.onAccent);
    r.text(L"Paused", {card.x, cy + rad + ui_.dp(10), card.w, ui_.dp(30)}, ui_.subtitle(), p.text, Align::Center,
           Align::Center);
    r.text(L"Click the board or press P to resume", {card.x, cy + rad + ui_.dp(40), card.w, ui_.dp(22)}, ui_.body(),
           p.text2, Align::Center, Align::Center);
    ui_.add(a, b, false);
}

// ================================================================ picker

float App::drawPicker(Rect area) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float pad = ui_.px(area.w < ui_.dp(600) ? 20 : 36);
    Rect col{area.x + pad, area.y + ui_.px(28), area.w - pad * 2, 0};
    col.w = std::min(col.w, ui_.px(900));
    col.x = std::round(area.x + (area.w - col.w) * 0.5f);
    float y = col.y;

    r.text(L"New game", {col.x, y, col.w, ui_.px(40)}, ui_.title(), p.text);
    y += ui_.px(42);
    r.text(L"Pick a difficulty. Every puzzle has exactly one solution and is graded by the techniques it needs.",
           {col.x, y, col.w, ui_.px(22)}, ui_.body(), p.text2);
    y += ui_.px(22) + ui_.px(22);

    // Continue cards
    for (GameMode m : {GameMode::Classic, GameMode::Daily}) {
        const Game& g = m == GameMode::Daily ? daily_ : classic_;
        if (!g.active) continue;
        const Rect card{col.x, y, col.w, ui_.px(72)};
        const Action a{ActContinue, int16_t(m)};
        ui_.card(card, a);
        const float cx = card.x + ui_.px(38);
        r.fillEllipse(cx, card.cy(), ui_.px(18), ui_.px(18), p.accent);
        r.icon(0xE768, cx + ui_.dp(1), card.cy(), ui_.dp(14), p.onAccent);
        std::wstring title = m == GameMode::Daily ? L"Continue daily challenge · " + shortDate(g.dailyDate)
                                                  : L"Continue " + widen(difficultyName(g.difficulty)) + L" game";
        const int filled = g.filledCount() - countFilled(g.givens), toFill = 81 - countFilled(g.givens);
        std::wstring sub = formatTime(elapsedOf(g)) + L" played · " +
                           std::to_wstring(toFill ? filled * 100 / toFill : 0) + L"% complete";
        if (g.status == GameStatus::Lost) sub = L"Out of mistakes · decide whether to take a second chance";
        r.text(title, {card.x + ui_.px(68), card.y + ui_.px(14), card.w - ui_.px(120), ui_.px(22)}, ui_.bodyStrong(), p.text);
        r.text(sub, {card.x + ui_.px(68), card.y + ui_.px(36), card.w - ui_.px(120), ui_.px(20)}, ui_.caption(), p.text2);
        r.icon(0xE76C, card.r() - ui_.px(24), card.cy(), ui_.dp(12), p.text2);
        y += card.h + ui_.px(12);
    }
    y += ui_.px(10);

    // Difficulty cards
    const int cols = col.w >= ui_.px(640) ? 3 : (col.w >= ui_.px(420) ? 2 : 1);
    const float gap = ui_.px(12);
    const float cw = std::floor((col.w - gap * float(cols - 1)) / float(cols)), ch = ui_.px(118);
    for (int i = 0; i < kDifficultyCount; ++i) {
        const Rect card{col.x + float(i % cols) * (cw + gap), y + float(i / cols) * (ch + gap), cw, ch};
        const Action a{ActNewGame, int16_t(i)};
        ui_.card(card, a);
        const Rect in = card.inset(ui_.px(18), ui_.px(16));
        r.text(widen(difficultyName(Difficulty(i))), {in.x, in.y, in.w, ui_.px(26)}, ui_.subtitle(), p.text);
        r.text(widen(difficultyBlurb(Difficulty(i))), {in.x, in.y + ui_.px(28), in.w, ui_.px(20)}, ui_.body(), p.text2);
        for (int k = 0; k < kDifficultyCount; ++k) {
            const Rect pip{in.x + float(k) * ui_.px(14), in.b() - ui_.px(6), ui_.px(10), ui_.px(4)};
            r.fillRound(pip, ui_.dp(2), k <= i ? p.accent : p.divider);
        }
        const LevelStats& ls = stats_.level[i];
        const std::wstring info = ls.won ? L"Best " + formatTime(ls.bestMs) + L" · " + std::to_wstring(ls.won) +
                                               (ls.won == 1 ? L" win" : L" wins")
                                         : L"Not solved yet";
        r.text(info, {in.x, in.b() - ui_.px(26), in.w, ui_.px(16)}, ui_.caption(), p.text3, Align::End, Align::Center);
        if (settings_.lastDifficulty == Difficulty(i) && ls.played)
            r.icon(0xE735, in.r() - ui_.dp(6), in.y + ui_.dp(10), ui_.dp(10), p.accent);
    }
    const int rows = (kDifficultyCount + cols - 1) / cols;
    y += float(rows) * (ch + gap) + ui_.px(12);

    // Daily teaser
    {
        const Rect card{col.x, y, col.w, ui_.px(84)};
        const Action a{ActNav, int16_t(Page::Daily)};
        ui_.card(card, a);
        const float cx = card.x + ui_.px(40);
        r.fillEllipse(cx, card.cy(), ui_.px(20), ui_.px(20), p.accentSoft);
        r.icon(0xE787, cx, card.cy(), ui_.dp(18), p.accent);
        const auto it = stats_.days.find(today_);
        const bool solved = it != stats_.days.end() && it->second.solved;
        const std::wstring when = std::wstring(weekdayName(weekday(today_))) + L", " + monthName(dateMonth(today_)) +
                                  L" " + std::to_wstring(dateDay(today_)) + L" · " +
                                  widen(difficultyName(dailyDifficulty(today_)));
        r.text(L"Daily challenge", {card.x + ui_.px(72), card.y + ui_.px(18), card.w - ui_.px(260), ui_.px(24)},
               ui_.bodyStrong(), p.text);
        r.text(when, {card.x + ui_.px(72), card.y + ui_.px(44), card.w - ui_.px(260), ui_.px(20)}, ui_.body(), p.text2);
        std::wstring status = solved ? L"Solved ✓" : L"Play today's puzzle";
        if (stats_.dailyStreak > 0) status += L"  ·  " + std::to_wstring(stats_.dailyStreak) + L"-day streak";
        r.text(status, {card.r() - ui_.px(300), card.y, ui_.px(260), card.h}, ui_.body(), solved ? p.success : p.accentText,
               Align::End, Align::Center);
        r.icon(0xE76C, card.r() - ui_.px(22), card.cy(), ui_.dp(12), p.text2);
        y += card.h;
    }
    return y + pad - area.y;
}

}  // namespace sudoku
