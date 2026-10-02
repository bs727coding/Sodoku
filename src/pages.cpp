// App drawing: Daily challenge, Statistics, Achievements and Settings pages.
#include <algorithm>
#include <cmath>

#include "app.h"
#include "engine/date.h"
#include "util.h"

namespace sudoku {

using namespace ui;

namespace {

std::wstring longDate(int date) {
    return std::wstring(weekdayName(weekday(date))) + L", " + monthName(dateMonth(date)) + L" " +
           std::to_wstring(dateDay(date)) + L", " + std::to_wstring(dateYear(date));
}

std::wstring shortMonthDay(int date) {
    static const wchar_t* months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                      L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    return std::wstring(months[(dateMonth(date) - 1) % 12]) + L" " + std::to_wstring(dateDay(date));
}

std::wstring hoursMinutes(int64_t ms) {
    const int64_t minutes = ms / 60000;
    if (minutes < 60) return std::to_wstring(minutes) + L" min";
    return std::to_wstring(minutes / 60) + L" h " + std::to_wstring(minutes % 60) + L" min";
}

}  // namespace

float App::pageHeader(Rect& col, std::wstring_view title, std::wstring_view subtitle) {
    Renderer& r = renderer_;
    r.text(title, {col.x, col.y, col.w, ui_.px(40)}, ui_.title(), ui_.pal.text);
    col.y += ui_.px(42);
    if (!subtitle.empty()) {
        r.text(subtitle, {col.x, col.y, col.w, ui_.px(22)}, ui_.body(), ui_.pal.text2);
        col.y += ui_.px(22);
    }
    col.y += ui_.px(22);
    return col.y;
}

// ================================================================ daily

float App::drawDaily(Rect area) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float pad = ui_.px(area.w < ui_.dp(600) ? 20 : 36);
    Rect col{area.x + pad, area.y + ui_.px(28), std::min(area.w - pad * 2, ui_.px(900)), 0};
    col.x = std::round(area.x + (area.w - col.w) * 0.5f);

    // Streak chip, top right of the header.
    if (stats_.dailyStreak > 0) {
        const std::wstring s = std::to_wstring(stats_.dailyStreak) + L"-day streak";
        const float w = r.textWidth(s, ui_.bodyStrong()) + ui_.px(44);
        const Rect chip{col.r() - w, col.y + ui_.px(4), w, ui_.px(32)};
        r.fillRound(chip, chip.h * 0.5f, p.accentSoft);
        r.icon(0xECAD, chip.x + ui_.px(18), chip.cy(), ui_.dp(14), p.accent);
        r.text(s, {chip.x + ui_.px(32), chip.y, w - ui_.px(36), chip.h}, ui_.bodyStrong(), p.accentText);
    }
    pageHeader(col, L"Daily challenge", L"A new puzzle every day. Solve it on the day to grow your streak.");
    float y = col.y;

    const int yr = dailyMonth_ / 100, mo = dailyMonth_ % 100;
    const int first = weekday(packDate(yr, mo, 1));
    const int days = daysInMonth(yr, mo);
    const int weeks = (first + days + 6) / 7;
    const float cellH = ui_.px(44);

    const bool twoCol = col.w >= ui_.px(720);
    const float calW = twoCol ? ui_.px(392) : col.w;
    const Rect cal{col.x, y, calW, ui_.px(84) + cellH * float(weeks) + ui_.px(52)};
    const Rect detail = twoCol ? Rect{col.x + calW + ui_.px(16), y, col.w - calW - ui_.px(16), std::max(cal.h, ui_.px(330))}
                               : Rect{col.x, y + cal.h + ui_.px(16), col.w, ui_.px(330)};
    ui_.card(cal);

    // Month header with navigation
    r.text(std::wstring(monthName(mo)) + L" " + std::to_wstring(yr),
           {cal.x + ui_.px(20), cal.y + ui_.px(16), cal.w - ui_.px(120), ui_.px(28)}, ui_.subtitle(), p.text);
    const float nb = ui_.px(32);
    ui_.iconButton(Action{ActDailyMonth, -1}, {cal.r() - ui_.px(16) - nb * 2 - ui_.px(4), cal.y + ui_.px(14), nb, nb},
                   0xE76B, L"Previous month", dailyMonth_ > 202001, false, 12);
    ui_.iconButton(Action{ActDailyMonth, 1}, {cal.r() - ui_.px(16) - nb, cal.y + ui_.px(14), nb, nb}, 0xE76C,
                   L"Next month", dailyMonth_ < today_ / 100, false, 12);

    const float gx = cal.x + ui_.px(16), gw = cal.w - ui_.px(32);
    const float cellW = std::floor(gw / 7.0f);
    static const wchar_t* wd[] = {L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa", L"Su"};
    const float wy = cal.y + ui_.px(58);
    for (int i = 0; i < 7; ++i)
        r.text(wd[i], {gx + float(i) * cellW, wy, cellW, ui_.px(20)}, ui_.caption(), p.text3, Align::Center, Align::Center);
    const float gy = wy + ui_.px(26);
    const float rad = std::min(cellW, cellH) * 0.5f - ui_.px(3);
    for (int day = 1; day <= days; ++day) {
        const int idx = first + day - 1;
        const int date = packDate(yr, mo, day);
        const Rect cell{gx + float(idx % 7) * cellW, gy + float(idx / 7) * cellH, cellW, cellH};
        const float cx = cell.cx(), cy = cell.cy();
        const bool future = date > today_;
        const auto it = stats_.days.find(date);
        const bool solved = it != stats_.days.end() && it->second.solved;
        const bool onTime = solved && it->second.onTime;
        const bool inProgress = daily_.active && daily_.dailyDate == date;
        const bool selected = date == dailySelected_;
        const Action a{ActDailyDay, 0, date};
        const bool hover = !future && ui_.isHot(a);
        Color fg = future ? p.textDisabled : p.text;
        if (onTime) {
            r.fillEllipse(cx, cy, rad, rad, p.accent);
            fg = p.onAccent;
        } else if (solved) {
            r.fillEllipse(cx, cy, rad, rad, p.accentSoft);
            fg = p.accentText;
        } else if (hover || selected) {
            r.fillEllipse(cx, cy, rad, rad, selected ? p.subtleHover : withAlpha(p.subtleHover, p.subtleHover.a * 0.8f));
        }
        if (date == today_ && !onTime) r.strokeEllipse(cx, cy, rad - ui_.dp(1), rad - ui_.dp(1), p.accent, ui_.dp(2));
        if (selected) r.strokeEllipse(cx, cy, rad + ui_.dp(2.5f), rad + ui_.dp(2.5f), p.text2, ui_.dp(1.25f));
        r.text(std::to_wstring(day), Rect::centered(cx, cy, rad * 2, rad * 2),
               {Font::Text, ui_.dp(14), (solved || date == today_) ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL},
               fg, Align::Center, Align::Center);
        if (inProgress) r.fillEllipse(cx, cy + rad - ui_.dp(5), ui_.dp(2), ui_.dp(2), onTime ? p.onAccent : p.accent);
        if (ui_.hasFocus(a)) ui_.focusRing(Rect::centered(cx, cy, rad * 2, rad * 2), rad);
        if (!future) ui_.add(a, cell, true);
    }
    // Legend
    {
        float lx = cal.x + ui_.px(20);
        const float ly = cal.b() - ui_.px(24);
        auto legend = [&](Color fillC, bool ring, const wchar_t* text) {
            if (ring) r.strokeEllipse(lx + ui_.dp(5), ly, ui_.dp(5), ui_.dp(5), p.accent, ui_.dp(1.5f));
            else r.fillEllipse(lx + ui_.dp(5), ly, ui_.dp(5), ui_.dp(5), fillC);
            const float tw = r.textWidth(text, ui_.caption());
            r.text(text, {lx + ui_.px(14), ly - ui_.px(10), tw + 2, ui_.px(20)}, ui_.caption(), p.text2);
            lx += tw + ui_.px(30);
        };
        legend(p.accent, false, L"Solved on the day");
        legend(p.accentSoft, false, L"Solved later");
        legend(p.accent, true, L"Today");
    }

    // Detail card for the selected day
    ui_.card(detail);
    {
        const int date = dailySelected_;
        const Rect in = detail.inset(ui_.px(24), ui_.px(20));
        float dy = in.y;
        r.text(longDate(date), {in.x, dy, in.w, ui_.px(28)}, ui_.subtitle(), p.text);
        dy += ui_.px(34);
        const std::wstring diff = widen(difficultyName(dailyDifficulty(date)));
        const float pw = r.textWidth(diff, ui_.caption()) + ui_.px(20);
        r.fillRound({in.x, dy, pw, ui_.px(24)}, ui_.px(12), p.accentSoft);
        r.text(diff, {in.x, dy, pw, ui_.px(24)}, {Font::Text, ui_.dp(12), DWRITE_FONT_WEIGHT_SEMI_BOLD}, p.accentText,
               Align::Center, Align::Center);
        dy += ui_.px(38);
        const auto it = stats_.days.find(date);
        const bool solved = it != stats_.days.end() && it->second.solved;
        const bool inProgress = daily_.active && daily_.dailyDate == date;
        const bool future = date > today_;
        std::wstring status;
        if (future) status = L"This puzzle isn't available yet.";
        else if (inProgress) status = L"In progress · " + formatDuration(elapsedOf(daily_)) + L" so far";
        else if (solved)
            status = L"Solved in " + formatDuration(it->second.bestMs) + (it->second.onTime ? L", on the day." : L".");
        else if (date == today_) status = L"Today's puzzle is waiting for you.";
        else status = L"Not played yet. Past days don't count toward your streak.";
        r.text(status, {in.x, dy, in.w, ui_.px(40)}, ui_.body(), p.text2, Align::Start, Align::Start, true);
        dy += ui_.px(48);
        const wchar_t* label = inProgress ? L"Continue" : solved ? L"Play again" : L"Play";
        ui_.button(Action{ActDailyPlay, 0, date}, {in.x, dy, ui_.px(140), ui_.px(36)}, label, ButtonKind::Accent,
                   inProgress ? 0xE768 : 0, !future);
        dy += ui_.px(56);

        // Small stats
        struct S {
            const wchar_t* label;
            std::wstring value;
        };
        const S s[] = {
            {L"Days solved", std::to_wstring(stats_.dailySolved)},
            {L"Current streak", std::to_wstring(stats_.dailyStreak)},
            {L"Best streak", std::to_wstring(stats_.dailyBestStreak)},
            {L"Best time", stats_.daily.bestMs ? formatDuration(stats_.daily.bestMs) : L"—"},
        };
        const float sw = std::floor(in.w / 4.0f);
        if (dy + ui_.px(48) <= detail.b()) {
            r.fill({in.x, dy - ui_.px(12), in.w, std::max(1.0f, ui_.px(1))}, p.divider);
            for (int i = 0; i < 4; ++i) {
                r.text(s[i].value, {in.x + float(i) * sw, dy, sw, ui_.px(28)}, ui_.subtitle(), p.text);
                r.text(s[i].label, {in.x + float(i) * sw, dy + ui_.px(28), sw, ui_.px(18)}, ui_.caption(), p.text2);
            }
        }
    }
    const float bottom = twoCol ? std::max(cal.b(), detail.b()) : detail.b();
    return bottom + pad - area.y;
}

// ================================================================ statistics

float App::drawStats(Rect area) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float pad = ui_.px(area.w < ui_.dp(600) ? 20 : 36);
    Rect col{area.x + pad, area.y + ui_.px(28), std::min(area.w - pad * 2, ui_.px(960)), 0};
    col.x = std::round(area.x + (area.w - col.w) * 0.5f);
    pageHeader(col, L"Statistics");
    float y = col.y;

    static const std::vector<std::wstring> tabs = {L"All", L"Easy", L"Medium", L"Hard", L"Expert",
                                                   L"Master", L"Extreme", L"Daily"};
    const float segW = std::min(col.w, ui_.segmentedWidth(tabs));
    ui_.segmented(ActSegment, SegStatsTab, {col.x, y, segW, ui_.px(36)}, tabs, statsTab_);
    y += ui_.px(36) + ui_.px(20);

    const bool daily = statsTab_ == 7;
    const LevelStats& L = statsTab_ == 0 ? stats_.all : daily ? stats_.daily : stats_.level[statsTab_ - 1];
    auto timeOrDash = [&](int64_t ms) { return ms ? formatDuration(ms) : std::wstring(L"—"); };
    wchar_t rate[16], hints[16];
    swprintf(rate, 16, L"%d%%", int(std::lround(L.winRate() * 100)));
    swprintf(hints, 16, L"%.1f", L.played ? double(L.hints) / L.played : 0.0);
    struct Tile {
        const wchar_t* label;
        std::wstring value;
        wchar_t icon;
    };
    const Tile tiles[] = {
        {L"Games played", std::to_wstring(L.played), 0xE80A},
        {L"Wins", std::to_wstring(L.won), 0xE73E},
        {L"Win rate", rate, 0xE9D2},
        {L"Perfect wins", std::to_wstring(L.perfect), 0xE735},
        {L"Best time", timeOrDash(L.bestMs), 0xE916},
        {L"Average time", timeOrDash(L.averageMs()), 0xE823},
        {daily ? L"Daily streak" : L"Current streak", std::to_wstring(daily ? stats_.dailyStreak : L.currentStreak), 0xECAD},
        {daily ? L"Best daily streak" : L"Best streak", std::to_wstring(daily ? stats_.dailyBestStreak : L.bestStreak), 0xEC24},
        {L"Time played", hoursMinutes(L.totalMs), 0xEC32},
        {L"Hints per game", hints, 0xEA80},
    };
    const int cols = col.w >= ui_.px(780) ? 5 : (col.w >= ui_.px(520) ? 3 : 2);
    const float gap = ui_.px(12);
    const float tw = std::floor((col.w - gap * float(cols - 1)) / float(cols)), th = ui_.px(88);
    const int n = int(std::size(tiles));
    for (int i = 0; i < n; ++i) {
        const Rect t{col.x + float(i % cols) * (tw + gap), y + float(i / cols) * (th + gap), tw, th};
        ui_.card(t);
        r.icon(tiles[i].icon, t.x + ui_.px(26), t.y + ui_.px(26), ui_.dp(14), p.accent);
        r.text(tiles[i].label, {t.x + ui_.px(42), t.y + ui_.px(14), t.w - ui_.px(50), ui_.px(24)}, ui_.caption(), p.text2);
        r.text(tiles[i].value, {t.x + ui_.px(16), t.y + ui_.px(42), t.w - ui_.px(24), ui_.px(32)}, ui_.subtitle(), p.text);
    }
    y += float((n + cols - 1) / cols) * (th + gap) + ui_.px(8);

    // ---- solve time chart
    {
        const Rect card{col.x, y, col.w, ui_.px(250)};
        ui_.card(card);
        const Rect in = card.inset(ui_.px(20), ui_.px(16));
        r.text(L"Solve times", {in.x, in.y, in.w, ui_.px(22)}, ui_.bodyStrong(), p.text);
        const auto& wins = L.recentWins;
        std::wstring sub = wins.empty() ? L"No wins yet" : L"Last " + std::to_wstring(wins.size()) + L" wins, oldest first";
        r.text(sub, {in.x, in.y + ui_.px(22), in.w, ui_.px(18)}, ui_.caption(), p.text2);
        // legend
        {
            float lx = in.r() - ui_.px(250);
            const float ly = in.y + ui_.px(11);
            r.line(lx, ly, lx + ui_.px(18), ly, p.success, ui_.dp(2), true);
            r.text(L"Best", {lx + ui_.px(24), ly - ui_.px(10), ui_.px(60), ui_.px(20)}, ui_.caption(), p.text2);
            lx += ui_.px(80);
            r.line(lx, ly, lx + ui_.px(18), ly, p.text2, ui_.dp(2));
            r.text(L"5-game average", {lx + ui_.px(24), ly - ui_.px(10), ui_.px(140), ui_.px(20)}, ui_.caption(), p.text2);
        }
        const Rect plot{in.x + ui_.px(48), in.y + ui_.px(52), in.w - ui_.px(52), in.h - ui_.px(60)};
        if (wins.empty()) {
            r.text(L"Solve a puzzle to see your times here.", plot, ui_.body(), p.text3, Align::Center, Align::Center);
        } else {
            int64_t maxMs = 0;
            for (const auto& w : wins) maxMs = std::max(maxMs, w.second);
            const int64_t step = maxMs <= 5 * 60000 ? 60000 : maxMs <= 20 * 60000 ? 5 * 60000 : 15 * 60000;
            const int64_t top = std::max<int64_t>(step, (maxMs + step - 1) / step * step);
            auto yOf = [&](int64_t ms) { return plot.b() - float(double(ms) / double(top)) * plot.h; };
            for (int k = 0; k <= 2; ++k) {
                const int64_t v = top * k / 2;
                const float gy = std::round(yOf(v));
                r.fill({plot.x, gy, plot.w, std::max(1.0f, ui_.px(1))}, p.divider);
                r.text(formatDuration(v), {in.x, gy - ui_.px(9), ui_.px(42), ui_.px(18)}, ui_.caption(), p.text3, Align::End,
                       Align::Center);
            }
            const int count = int(wins.size());
            const float slot = plot.w / float(std::max(count, 10));
            const float bw = std::max(ui_.px(3), std::min(ui_.px(18), slot * 0.62f));
            std::vector<D2D1_POINT_2F> avg;
            for (int i = 0; i < count; ++i) {
                const float cx = plot.x + slot * (float(i) + 0.5f);
                const float by = yOf(wins[size_t(i)].second);
                const Rect bar{std::round(cx - bw * 0.5f), by, bw, plot.b() - by};
                const Action a{ActBlock, int16_t(i), 1000 + i};
                const bool hot = ui_.isHot(a);
                r.fillRound(bar, std::min(ui_.dp(3), bw * 0.5f), hot ? p.accent : withAlpha(p.accent, 0.62f));
                const int date = localDateFromUnix(wins[size_t(i)].first);
                ui_.add(a, {cx - slot * 0.5f, plot.y, slot, plot.h}, false,
                        (date ? shortMonthDay(date) + L" · " : L"") + formatDuration(wins[size_t(i)].second));
                int64_t sum = 0;
                int nAvg = 0;
                for (int k = std::max(0, i - 4); k <= i; ++k, ++nAvg) sum += wins[size_t(k)].second;
                avg.push_back({cx, yOf(sum / nAvg)});
            }
            for (size_t i = 1; i < avg.size(); ++i)
                r.line(avg[i - 1].x, avg[i - 1].y, avg[i].x, avg[i].y, p.text2, ui_.dp(1.5f));
            if (L.bestMs) {
                const float by = yOf(L.bestMs);
                r.line(plot.x, by, plot.r(), by, p.success, ui_.dp(1.5f), true);
            }
        }
        y += card.h + ui_.px(16);
    }

    // ---- recent games
    {
        std::vector<const GameRecord*> rows;
        for (auto it = history_.rbegin(); it != history_.rend() && rows.size() < 10; ++it) {
            const GameRecord& g = *it;
            if (daily ? g.mode != GameMode::Daily : g.mode != GameMode::Classic) continue;
            if (statsTab_ >= 1 && statsTab_ <= 6 && int(g.difficulty) != statsTab_ - 1) continue;
            rows.push_back(&g);
        }
        const float rowH = ui_.px(40);
        const float cardH = ui_.px(56) + (rows.empty() ? ui_.px(40) : rowH * float(rows.size())) + ui_.px(12);
        const Rect card{col.x, y, col.w, cardH};
        ui_.card(card);
        const Rect in = card.inset(ui_.px(20), ui_.px(16));
        r.text(L"Recent games", {in.x, in.y, in.w, ui_.px(22)}, ui_.bodyStrong(), p.text);
        const float colX[] = {0.0f, 0.30f, 0.52f, 0.70f, 0.85f};
        const wchar_t* headers[] = {L"Date", L"Puzzle", L"Result", L"Time", L"Mistakes · Hints"};
        float ry = in.y + ui_.px(32);
        for (int c = 0; c < 5; ++c)
            r.text(headers[c], {in.x + in.w * colX[c], ry, in.w * 0.2f, ui_.px(18)}, ui_.caption(), p.text3);
        ry += ui_.px(22);
        if (rows.empty()) r.text(L"No games yet.", {in.x, ry, in.w, ui_.px(30)}, ui_.body(), p.text3);
        for (const GameRecord* g : rows) {
            r.fill({in.x, ry, in.w, std::max(1.0f, ui_.px(1))}, p.divider);
            wchar_t when[48];
            swprintf(when, 48, L"%ls, %02d:%02d", shortMonthDay(g->localDate).c_str(), g->localMinute / 60, g->localMinute % 60);
            const std::wstring puzzle = g->mode == GameMode::Daily
                                            ? L"Daily " + shortMonthDay(g->dailyDate) + L" · " + widen(difficultyName(g->difficulty))
                                            : widen(difficultyName(g->difficulty));
            const Rect cell{in.x, ry, in.w, rowH};
            r.text(when, {cell.x, cell.y, in.w * 0.29f, rowH}, ui_.body(), p.text);
            r.text(puzzle, {cell.x + in.w * colX[1], cell.y, in.w * 0.21f, rowH}, ui_.body(), p.text);
            const wchar_t* res = g->result == GameResult::Won ? L"Won" : g->result == GameResult::Lost ? L"Lost" : L"Abandoned";
            const Color rc = g->result == GameResult::Won ? p.success : g->result == GameResult::Lost ? p.critical : p.text3;
            const float pw = r.textWidth(res, ui_.caption()) + ui_.px(16);
            const Rect pill{cell.x + in.w * colX[2], cell.cy() - ui_.px(11), pw, ui_.px(22)};
            r.fillRound(pill, pill.h * 0.5f, withAlpha(rc, 0.14f));
            r.text(res, pill, {Font::Text, ui_.dp(12), DWRITE_FONT_WEIGHT_SEMI_BOLD}, rc, Align::Center, Align::Center);
            r.text(formatDuration(g->timeMs), {cell.x + in.w * colX[3], cell.y, in.w * 0.14f, rowH}, ui_.body(), p.text);
            r.text(std::to_wstring(g->mistakes) + L" · " + std::to_wstring(g->hints),
                   {cell.x + in.w * colX[4], cell.y, in.w * 0.15f, rowH}, ui_.body(), p.text2);
            ry += rowH;
        }
        y += card.h + ui_.px(16);
    }

    ui_.button(Action{ActResetStats}, {col.x, y, ui_.px(170), ui_.px(32)}, L"Reset statistics…", ButtonKind::Danger,
               0xE74D, !history_.empty());
    y += ui_.px(32);
    return y + pad - area.y;
}

// ================================================================ achievements

float App::drawAchievements(Rect area) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float pad = ui_.px(area.w < ui_.dp(600) ? 20 : 36);
    Rect col{area.x + pad, area.y + ui_.px(28), std::min(area.w - pad * 2, ui_.px(960)), 0};
    col.x = std::round(area.x + (area.w - col.w) * 0.5f);
    int unlocked = 0;
    for (int i = 0; i < kAchievementCount; ++i) unlocked += achievements_.isUnlocked(i);
    const std::wstring sub = std::to_wstring(unlocked) + L" of " + std::to_wstring(kAchievementCount) + L" unlocked";
    pageHeader(col, L"Achievements", sub);
    float y = col.y - ui_.px(10);
    ui_.progressBar({col.x, y, std::min(col.w, ui_.px(360)), ui_.px(6)}, float(unlocked) / float(kAchievementCount),
                    p.accent);
    y += ui_.px(28);

    const int cols = col.w >= ui_.px(840) ? 3 : (col.w >= ui_.px(540) ? 2 : 1);
    const float gap = ui_.px(12);
    const float cw = std::floor((col.w - gap * float(cols - 1)) / float(cols)), ch = ui_.px(114);
    for (int i = 0; i < kAchievementCount; ++i) {
        const AchievementDef& def = kAchievements[i];
        const bool done = achievements_.isUnlocked(i);
        const int prog = achievementProgress(i, stats_);
        const Rect card{col.x + float(i % cols) * (cw + gap), y + float(i / cols) * (ch + gap), cw, ch};
        ui_.card(card);
        const float bx = card.x + ui_.px(40), by = card.y + ui_.px(40);
        if (done) {
            r.fillEllipse(bx, by, ui_.px(22), ui_.px(22), p.accent);
            r.icon(def.icon, bx, by, ui_.dp(18), p.onAccent);
        } else {
            r.fillEllipse(bx, by, ui_.px(22), ui_.px(22), p.dark ? rgb(0xFFFFFF, 0.06f) : rgb(0x000000, 0.05f));
            r.strokeEllipse(bx, by, ui_.px(22), ui_.px(22), p.divider, std::max(1.0f, ui_.px(1)));
            r.icon(def.icon, bx, by, ui_.dp(18), p.text3);
        }
        const float tx = card.x + ui_.px(76), tw = card.r() - tx - ui_.px(16);
        r.text(def.name, {tx, card.y + ui_.px(16), tw, ui_.px(22)}, ui_.bodyStrong(), done ? p.text : p.text2);
        r.text(def.description, {tx, card.y + ui_.px(40), tw, ui_.px(36)}, ui_.caption(), p.text2, Align::Start,
               Align::Start, true);
        const float fy = card.b() - ui_.px(24);
        if (done) {
            const int date = localDateFromUnix(achievements_.unlockedAt(i));
            const std::wstring when = date ? L"Unlocked " + std::wstring(monthName(dateMonth(date))) + L" " +
                                                 std::to_wstring(dateDay(date)) + L", " + std::to_wstring(dateYear(date))
                                           : L"Unlocked";
            r.text(when, {tx, fy - ui_.px(9), tw, ui_.px(18)}, ui_.caption(), p.accentText);
        } else if (def.target > 1) {
            const std::wstring t = std::to_wstring(prog) + L" / " + std::to_wstring(def.target);
            const float lw = r.textWidth(t, ui_.caption()) + ui_.px(10);
            ui_.progressBar({tx, fy - ui_.px(2), std::max(ui_.px(20), tw - lw), ui_.px(4)}, float(prog) / float(def.target),
                            p.accent);
            r.text(t, {card.r() - ui_.px(16) - lw, fy - ui_.px(9), lw, ui_.px(18)}, ui_.caption(), p.text3, Align::End);
        } else {
            r.text(L"Locked", {tx, fy - ui_.px(9), tw, ui_.px(18)}, ui_.caption(), p.text3);
        }
    }
    y += float((kAchievementCount + cols - 1) / cols) * (ch + gap);
    return y + pad - area.y;
}

// ================================================================ settings

float App::drawSettings(Rect area) {
    Renderer& r = renderer_;
    const Palette& p = ui_.pal;
    const float pad = ui_.px(area.w < ui_.dp(600) ? 20 : 36);
    Rect col{area.x + pad, area.y + ui_.px(28), std::min(area.w - pad * 2, ui_.px(860)), 0};
    col.x = std::round(area.x + (area.w - col.w) * 0.5f);
    pageHeader(col, L"Settings");
    float y = col.y;
    const Settings& s = settings_;

    auto section = [&](const wchar_t* title) {
        y += ui_.px(6);
        r.text(title, {col.x, y, col.w, ui_.px(22)}, ui_.bodyStrong(), p.text);
        y += ui_.px(30);
    };
    // A Windows 11 style settings card: icon, title, description; returns the control area.
    auto row = [&](wchar_t icon, const wchar_t* title, const wchar_t* desc, float controlW) -> Rect {
        const float h = desc ? ui_.px(68) : ui_.px(56);
        const Rect card{col.x, y, col.w, h};
        r.fillRound(card, ui_.dp(5), p.card);
        r.strokeRound(card, ui_.dp(5), p.cardStroke, std::max(1.0f, ui_.px(1)));
        r.icon(icon, card.x + ui_.px(30), card.cy(), ui_.dp(18), p.text);
        const float tx = card.x + ui_.px(60), tw = card.w - ui_.px(60) - controlW - ui_.px(36);
        if (desc) {
            r.text(title, {tx, card.y + ui_.px(13), tw, ui_.px(22)}, ui_.body(), p.text);
            r.text(desc, {tx, card.y + ui_.px(35), tw, ui_.px(20)}, ui_.caption(), p.text2);
        } else {
            r.text(title, {tx, card.y, tw, h}, ui_.body(), p.text);
        }
        y += h + ui_.px(4);
        return {card.r() - ui_.px(20) - controlW, card.y, controlW, h};
    };
    auto toggleRow = [&](wchar_t icon, const wchar_t* title, const wchar_t* desc, ToggleId id, bool value) {
        const Rect c = row(icon, title, desc, ui_.px(100));
        ui_.toggle(Action{ActToggle, int16_t(id)}, c, value);
    };
    auto segRow = [&](wchar_t icon, const wchar_t* title, const wchar_t* desc, SegmentGroup g,
                      const std::vector<std::wstring>& labels, int sel) {
        const float w = ui_.segmentedWidth(labels);
        const Rect c = row(icon, title, desc, w);
        ui_.segmented(ActSegment, g, {c.x, c.cy() - ui_.px(16), c.w, ui_.px(32)}, labels, sel);
    };

    section(L"Appearance");
    segRow(0xE790, L"Theme", L"Follow Windows, or always use light or dark", SegTheme, {L"System", L"Light", L"Dark"},
           int(s.theme));
    {
        const float sw = ui_.px(28), sg = ui_.px(10);
        const int n = int(AccentChoice::Count);
        const Rect c = row(0xE771, L"Accent color", L"Used for highlights, your numbers and buttons",
                           float(n) * sw + float(n - 1) * sg);
        static const wchar_t* names[] = {L"Windows accent", L"Blue", L"Teal", L"Green", L"Purple", L"Orange", L"Rose"};
        for (int i = 0; i < n; ++i) {
            const AccentRamp ramp = accentFor(AccentChoice(i));
            const float cx = c.x + float(i) * (sw + sg) + sw * 0.5f, cy = c.cy();
            const Action a{ActAccent, int16_t(i)};
            const bool sel = int(s.accent) == i;
            r.fillEllipse(cx, cy, sw * 0.5f, sw * 0.5f, p.dark ? ramp.light2 : ramp.dark1);
            if (i == 0) r.icon(0xE706, cx, cy, ui_.dp(11), p.dark ? rgb(0x000000) : rgb(0xFFFFFF));
            if (sel) r.strokeEllipse(cx, cy, sw * 0.5f + ui_.dp(3), sw * 0.5f + ui_.dp(3), p.text, ui_.dp(2));
            else if (ui_.isHot(a)) r.strokeEllipse(cx, cy, sw * 0.5f + ui_.dp(3), sw * 0.5f + ui_.dp(3), p.text3, ui_.dp(1.5f));
            if (ui_.hasFocus(a)) ui_.focusRing(Rect::centered(cx, cy, sw, sw), sw * 0.5f);
            ui_.add(a, Rect::centered(cx, cy, sw + sg, c.h), true, names[i]);
        }
    }
    segRow(0xE7F4, L"Window material", L"Mica shows a hint of your desktop wallpaper", SegBackdrop,
           {L"Mica", L"Mica Alt", L"Solid"}, int(s.backdrop));
    toggleRow(0xE945, L"Animations", L"Number pops, completion ripples and confetti", TogAnimations, s.animations);

    section(L"Gameplay");
    segRow(0xE7BA, L"Mistake limit", L"The game ends when you reach it (you can take a second chance)", SegMistakes,
           {L"Off", L"3", L"5"}, s.mistakeLimit == 0 ? 0 : s.mistakeLimit == 3 ? 1 : 2);
    toggleRow(0xE73E, L"Check numbers as you place them", L"Wrong numbers turn red and count as mistakes",
              TogCheckMistakes, s.checkMistakes);
    toggleRow(0xE70F, L"Remove notes automatically", L"Placing a number clears it from notes in its row, column and box",
              TogAutoNotes, s.autoRemoveNotes);
    toggleRow(0xE769, L"Pause when the window isn't active", L"Keeps your times fair when you switch away",
              TogAutoPause, s.autoPause);
    toggleRow(0xE916, L"Show timer", nullptr, TogShowTimer, s.showTimer);

    section(L"Assists");
    toggleRow(0xE8A9, L"Highlight row, column and box", nullptr, TogHighlightRegion, s.highlightRegion);
    toggleRow(0xE80A, L"Highlight matching numbers", nullptr, TogHighlightSame, s.highlightSame);
    toggleRow(0xE783, L"Highlight conflicts", L"Shows repeated numbers in a row, column or box", TogHighlightConflicts,
              s.highlightConflicts);
    toggleRow(0xE73D, L"Dim numbers that are complete", L"On the number pad, once all nine are placed", TogHideCompleted,
              s.hideCompleted);
    toggleRow(0xE8EF, L"Show remaining counts", L"How many of each number are left to place", TogShowCounts,
              s.showCounts);

    section(L"Sound");
    toggleRow(0xE767, L"Sound effects", nullptr, TogSound, s.sound);
    if (s.sound)
        segRow(0xE995, L"Volume", nullptr, SegVolume, {L"Low", L"Medium", L"High"}, s.volume - 1);

    section(L"Data");
    {
        Rect c = row(0xE838, L"Data folder", L"Saves, statistics and settings are stored here", ui_.px(120));
        ui_.button(Action{ActOpenData}, {c.x, c.cy() - ui_.px(16), c.w, ui_.px(32)}, L"Open folder");
        c = row(0xE74D, L"Reset statistics", L"Clears your game history. Achievements are kept.", ui_.px(120));
        ui_.button(Action{ActResetStats}, {c.x, c.cy() - ui_.px(16), c.w, ui_.px(32)}, L"Reset…", ButtonKind::Danger,
                   0, !history_.empty());
        c = row(0xE74D, L"Reset all progress", L"Clears history and achievements", ui_.px(120));
        ui_.button(Action{ActResetAchievements}, {c.x, c.cy() - ui_.px(16), c.w, ui_.px(32)}, L"Reset…",
                   ButtonKind::Danger);
    }

    section(L"About");
    {
        const Rect c = row(0xE946, L"Sudoku 1.0",
                           L"Native Windows on ARM64 · C++23, Direct2D and DirectWrite · built with clang",
                           ui_.px(170));
        ui_.button(Action{ActShortcuts}, {c.x, c.cy() - ui_.px(16), c.w, ui_.px(32)}, L"Keyboard shortcuts", ButtonKind::Standard,
                   0xE765);
    }
    return y + pad - area.y;
}

}  // namespace sudoku
