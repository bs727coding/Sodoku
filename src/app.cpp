// App: lifecycle, input handling, game flow, timer accounting and persistence.
#include "app.h"

#include <algorithm>
#include <cmath>

#include "engine/date.h"
#include "game/storage.h"
#include "ui/anim.h"
#include "util.h"

namespace sudoku {

using namespace ui;

namespace {

constexpr UINT_PTR kTimerClock = 1, kTimerSave = 2, kTimerTooltip = 3, kTimerToast = 4, kTimerDialog = 5,
                   kTimerFlash = 6;
constexpr double kToastDuration = 4.0;

bool fileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

}  // namespace

// ================================================================ lifecycle

double App::now() const {
    if (fixedTime_ >= 0) return fixedTime_;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - qpcStart_.QuadPart) / double(qpcFreq_.QuadPart);
}

void App::loadSettingsEarly() {
    std::string text;
    if (storage::readText(storage::file(L"settings.ini"), text)) settings_.parse(text);
}

bool App::init(HWND hwnd) {
    hwnd_ = hwnd;
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&qpcStart_);
    dpi_ = GetDpiForWindow(hwnd);
    ui_.scale = float(dpi_) / 96.0f;
    ui_.r = &renderer_;
    systemAnimations_ = systemAnimationsEnabled();
    loadAll();
    applyTheme();
    if (!renderer_.initWindow(hwnd)) return false;
    sound_.configure(settings_.sound, settings_.volume);
    pool_.start(hwnd, WM_APP_POOL, storage::file(L"pool.txt"));
    pool_.requestDaily(today_);
    syncTimer();
    return true;
}

void App::loadAll() {
    today_ = storage::todayLocal();
    std::string text;
    if (storage::readText(storage::file(L"history.csv"), text)) history_ = parseHistory(text);
    text.clear();
    if (storage::readText(storage::file(L"achievements.ini"), text)) achievements_.parse(text);
    text.clear();
    if (storage::readText(storage::file(L"save_classic.txt"), text) &&
        (!classic_.deserialize(text) || classic_.mode != GameMode::Classic))
        classic_.clear();
    text.clear();
    if (storage::readText(storage::file(L"save_daily.txt"), text) &&
        (!daily_.deserialize(text) || daily_.mode != GameMode::Daily))
        daily_.clear();
    if (classic_.status == GameStatus::Won) classic_.clear();
    if (daily_.status == GameStatus::Won) daily_.clear();
    current_ = classic_.active || !daily_.active ? GameMode::Classic : GameMode::Daily;
    refreshStats(false);
    dailyMonth_ = today_ / 100;
    dailySelected_ = today_;
    if (game().active && game().status == GameStatus::Lost) dialog_ = Dialog::GameOver;
}

void App::shutdown() {
    if (offscreen_) return;
    if (timerRunning_) {
        timerGame_->elapsedMs += int64_t((now() - timerStart_) * 1000.0);
        timerRunning_ = false;
        timerGame_ = nullptr;
    }
    if (hwnd_) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (fullscreen_) wp = savedPlacement_;
        else GetWindowPlacement(hwnd_, &wp);
        const RECT& rc = wp.rcNormalPosition;
        settings_.window = {true, int(rc.left), int(rc.top), int(rc.right - rc.left), int(rc.bottom - rc.top),
                            wp.showCmd == SW_SHOWMAXIMIZED};
    }
    saveSettings();
    saveNow();
    pool_.stop();
}

void App::onEndSession() {
    if (timerRunning_) {
        timerGame_->elapsedMs += int64_t((now() - timerStart_) * 1000.0);
        timerStart_ = now();
    }
    saveSettings();
    saveNow();
}

void App::applyTheme() {
    const bool dark =
        settings_.theme == ThemeMode::Dark || (settings_.theme == ThemeMode::System && systemUsesDarkTheme());
    ui_.pal = makePalette(dark, accentFor(settings_.accent));
    if (hwnd_) micaActive_ = applyWindowFrame(hwnd_, ui_.pal, settings_.backdrop);
    invalidate();
}

// ================================================================ events

void App::onSize(UINT w, UINT h) {
    renderer_.resize(w, h);
    invalidate();
}

void App::onDpiChanged(UINT dpi) {
    dpi_ = dpi;
    ui_.scale = float(dpi) / 96.0f;
    invalidate();
}

void App::onActivate(bool active) {
    active_ = active;
    if (!active) {
        flyout_ = Flyout::None;
        ui_.hot = {};
        ui_.pressed = {};
        if (settings_.autoPause && inGame() && game().isPlaying() && dialog_ == Dialog::None) paused_ = true;
        saveNow();
    }
    syncTimer();
    invalidate();
}

void App::onMinimized(bool minimized) {
    if (minimized == minimized_) return;
    minimized_ = minimized;
    if (minimized && inGame() && game().isPlaying()) {
        paused_ = true;
        saveNow();
    }
    if (minimized) renderer_.trimMemory();  // hand cached GPU memory back while hidden
    syncTimer();
    invalidate();
}

void App::onSystemSettingsChanged() {
    systemAnimations_ = systemAnimationsEnabled();
    applyTheme();
}

void App::updateHot() {
    const Region* reg = ui_.hitTest(ui_.mouseX, ui_.mouseY);
    const Action h = reg ? reg->action : Action{};
    if (!(h == ui_.hot)) {
        ui_.hot = h;
        ui_.hotSince = now();
        invalidate();
        if (hwnd_ && reg && !reg->tooltip.empty()) SetTimer(hwnd_, kTimerTooltip, 650, nullptr);
    }
}

void App::onMouseMove(float x, float y) {
    if (x == ui_.mouseX && y == ui_.mouseY) return;
    const bool hadTooltip = ui_.hot.valid() && now() - ui_.hotSince > 0.6;
    ui_.mouseX = x;
    ui_.mouseY = y;
    updateHot();
    if (hadTooltip) invalidate();  // tooltip follows the pointer
}

void App::onMouseLeave() {
    ui_.mouseX = ui_.mouseY = -1;
    if (ui_.hot.valid()) {
        ui_.hot = {};
        invalidate();
    }
}

void App::onMouseDown(float x, float y) {
    onMouseMove(x, y);
    ui_.pressed = ui_.hot;
    ui_.focusVisible = false;
    if (hwnd_) SetCapture(hwnd_);
    if (ui_.pressed.kind == ActBoard) {
        // Act immediately on press so selection feels instant.
        trigger(ui_.pressed);
        ui_.pressed = {};
    }
    invalidate();
}

void App::onMouseUp(float x, float y) {
    onMouseMove(x, y);
    const Action p = ui_.pressed;
    ui_.pressed = {};
    if (hwnd_) ReleaseCapture();
    if (p.valid() && p == ui_.hot) {
        ui_.focus = p;
        trigger(p);
    }
    invalidate();
}

void App::onMouseWheel(float notches) {
    if (dialog_ != Dialog::None || flyout_ != Flyout::None) return;
    if (page_ == Page::Play && game().active) return;
    scroll_[int(page_)] -= notches * ui_.dp(72);
    invalidate();
}

void App::onTimer(UINT_PTR id) {
    if (hwnd_) KillTimer(hwnd_, id);
    switch (id) {
    case kTimerClock:
        if (timerRunning_) scheduleClockTick();
        break;
    case kTimerSave: saveNow(); break;
    case kTimerFlash: checkFlashUntil_ = 0; break;
    default: break;
    }
    invalidate();
}

void App::onPool(WPARAM wp, LPARAM lp) {
    if (dialog_ != Dialog::Generating) return;
    Puzzle p;
    if (wp == 1 && generatingDaily_ && int(lp) == generatingDaily_) {
        if (pool_.takeDaily(generatingDaily_, p)) {
            const int date = generatingDaily_;
            generatingDaily_ = 0;
            dialog_ = Dialog::None;
            beginGame(GameMode::Daily, p, date);
        }
    } else if (wp == 0 && !generatingDaily_ && int(lp) == int(pendingDifficulty_)) {
        if (pool_.take(pendingDifficulty_, p)) {
            dialog_ = Dialog::None;
            beginGame(GameMode::Classic, p, 0);
        }
    }
    invalidate();
}

// ================================================================ game flow

void App::startClassic(Difficulty d, bool confirmed) {
    flyout_ = Flyout::None;
    if (classic_.isPlaying() && classic_.hasProgress() && !confirmed) {
        pendingDifficulty_ = d;
        dialog_ = Dialog::ConfirmNewGame;
        return;
    }
    if (classic_.isPlaying() && classic_.hasProgress()) {
        recordResult(classic_, GameResult::Abandoned);
        refreshStats(true);
    }
    settings_.lastDifficulty = d;
    saveSettings();
    Puzzle p;
    if (pool_.take(d, p)) {
        beginGame(GameMode::Classic, p, 0);
        return;
    }
    pendingDifficulty_ = d;
    generatingDaily_ = 0;
    dialog_ = Dialog::Generating;
    dialogAt_ = now();
    pool_.request(d);
}

void App::playDaily(int date, bool confirmed) {
    if (date > today_ || date <= 0) return;
    if (daily_.isPlaying() && daily_.dailyDate == date) {
        showGame(GameMode::Daily);
        return;
    }
    if (daily_.isPlaying() && daily_.hasProgress() && !confirmed) {
        pendingDaily_ = date;
        dialog_ = Dialog::ConfirmDaily;
        return;
    }
    if (daily_.isPlaying() && daily_.hasProgress()) {
        recordResult(daily_, GameResult::Abandoned);
        refreshStats(true);
    }
    Puzzle p;
    if (pool_.takeDaily(date, p)) {
        beginGame(GameMode::Daily, p, date);
        return;
    }
    generatingDaily_ = date;
    dialog_ = Dialog::Generating;
    dialogAt_ = now();
    pool_.requestDaily(date);
}

void App::beginGame(GameMode mode, const Puzzle& p, int date) {
    Game& g = mode == GameMode::Daily ? daily_ : classic_;
    if (timerRunning_ && timerGame_ == &g) {  // the old game's time was already recorded
        timerRunning_ = false;
        timerGame_ = nullptr;
    }
    g.start(p, mode, date, storage::nowUtc());
    current_ = mode;
    if (page_ != Page::Play) pageChangedAt_ = now();
    page_ = Page::Play;
    dialog_ = Dialog::None;
    resetPlayUi();
    saveNow();
    syncTimer();
    invalidate();
}

void App::showGame(GameMode mode) {
    current_ = mode;
    navigate(Page::Play);
    resetPlayUi();
    syncTimer();
}

void App::resetPlayUi() {
    selected_ = -1;
    digitLock_ = 0;
    notesMode_ = false;
    paused_ = false;
    closeHint();
    anims_.clear();
    checkFlashUntil_ = 0;
    flyout_ = Flyout::None;
}

void App::recordResult(Game& g, GameResult result) {
    if (!g.active) return;
    GameRecord r;
    r.endUtc = storage::nowUtc();
    r.localDate = storage::todayLocal();
    r.localMinute = storage::minuteOfDay();
    r.mode = g.mode;
    r.difficulty = g.difficulty;
    r.dailyDate = g.dailyDate;
    r.result = result;
    r.timeMs = elapsedOf(g);
    r.mistakes = g.mistakes;
    r.hints = g.hintsUsed;
    r.secondChance = g.extraLives > 0;
    r.givens = toString(g.givens);
    history_.push_back(r);
    if (!offscreen_) {
        const std::wstring path = storage::file(L"history.csv");
        const bool exists = fileExists(path);
        storage::appendText(path, (exists ? std::string() : historyHeader()) + formatRecord(r));
    }
}

void App::endGame(Game& g) {
    if (timerRunning_ && timerGame_ == &g) {
        timerRunning_ = false;
        timerGame_ = nullptr;
    }
    const bool daily = &g == &daily_;
    g.clear();
    if (!offscreen_) storage::removeFile(storage::file(daily ? L"save_daily.txt" : L"save_classic.txt"));
    if (daily && current_ == GameMode::Daily) current_ = GameMode::Classic;
    resetPlayUi();
}

void App::refreshStats(bool announce) {
    today_ = storage::todayLocal();
    stats_ = computeStats(history_, today_);
    const std::vector<int> fresh = updateAchievements(stats_, achievements_, storage::nowUtc());
    if (fresh.empty()) return;
    if (!offscreen_) storage::writeAtomic(storage::file(L"achievements.ini"), achievements_.serialize());
    if (!announce) return;
    for (int i : fresh) showToast(L"Achievement unlocked", kAchievements[i].name, kAchievements[i].icon);
}

void App::onWin() {
    Game& g = game();
    const LevelStats& before = g.mode == GameMode::Daily ? stats_.daily : stats_.level[int(g.difficulty)];
    const int64_t prevBest = before.bestMs;
    syncTimer();
    recordResult(g, GameResult::Won);
    refreshStats(true);
    const LevelStats& after = g.mode == GameMode::Daily ? stats_.daily : stats_.level[int(g.difficulty)];
    win_.mode = g.mode;
    win_.difficulty = g.difficulty;
    win_.timeMs = g.elapsedMs;
    win_.mistakes = g.mistakes;
    win_.hints = g.hintsUsed;
    win_.dailyDate = g.dailyDate;
    win_.perfect = g.mistakes == 0 && g.hintsUsed == 0 && g.extraLives == 0;
    win_.newBest = prevBest > 0 && g.elapsedMs < prevBest;
    win_.bestMs = after.bestMs;
    win_.averageMs = after.averageMs();
    win_.streak = g.mode == GameMode::Daily ? stats_.dailyStreak : stats_.all.currentStreak;

    const double t = now();
    anims_.win = animationsOn() ? t : -1;
    anims_.winOrigin = selected_ >= 0 ? selected_ : 40;
    if (animationsOn()) startConfetti();
    sound_.play(Sfx::Win);
    selected_ = -1;
    digitLock_ = 0;
    closeHint();
    if (!offscreen_)
        storage::removeFile(storage::file(g.mode == GameMode::Daily ? L"save_daily.txt" : L"save_classic.txt"));
    dialog_ = Dialog::Win;
    dialogAt_ = t + (animationsOn() ? 0.9 : 0.0);
    if (hwnd_ && animationsOn()) SetTimer(hwnd_, kTimerDialog, 920, nullptr);
}

void App::onLost() {
    syncTimer();
    dialog_ = Dialog::GameOver;
    dialogAt_ = now();
    scheduleSave();
}

void App::restartPuzzle(bool confirmed) {
    Game& g = game();
    if (!g.active) return;
    if (!confirmed && g.hasProgress()) {
        dialog_ = Dialog::ConfirmRestart;
        return;
    }
    if (timerRunning_ && timerGame_ == &g) {
        timerRunning_ = false;
        timerGame_ = nullptr;
    }
    g.restart();
    resetPlayUi();
    scheduleSave();
    syncTimer();
}

void App::pause() {
    if (!inGame() || !game().isPlaying()) return;
    paused_ = true;
    flyout_ = Flyout::None;
    syncTimer();
    saveNow();
}

void App::resume() {
    paused_ = false;
    syncTimer();
}

void App::navigate(Page p) {
    flyout_ = Flyout::None;
    if (p == page_) return;
    page_ = p;
    pageChangedAt_ = now();
    closeHint();
    ui_.focus = {};
    if (p == Page::Daily) {
        today_ = storage::todayLocal();
        if (!dailySelected_) dailySelected_ = today_;
    }
    saveNow();
    syncTimer();
}

void App::showToast(std::wstring title, std::wstring text, wchar_t icon) {
    toasts_.push_back({std::move(title), std::move(text), icon, now() + 0.35 * double(toasts_.size())});
    if (hwnd_) SetTimer(hwnd_, kTimerToast, UINT((kToastDuration - 0.3) * 1000), nullptr);
    invalidate();
}

void App::startConfetti() {
    confetti_.clear();
    confettiStart_ = now();
    Rng rng(uint64_t(confettiStart_ * 1000.0) + 17);
    const float w = float(renderer_.width()), s = ui_.scale;
    const Color colors[] = {ui_.pal.accent, ui_.pal.success, rgb(0xF7630C), rgb(0xE3008C), rgb(0xFFB900),
                            rgb(0x8764B8)};
    auto frand = [&] { return float(rng.below(10000)) / 10000.0f; };
    for (int i = 0; i < 160; ++i) {
        Particle p;
        p.x = w * (0.05f + 0.9f * frand());
        p.y = -s * (10 + 220 * frand());
        p.vx = (frand() - 0.5f) * 160.0f * s;
        p.vy = (60 + 260 * frand()) * s;
        p.rot = frand() * 360.0f;
        p.vr = (frand() - 0.5f) * 900.0f;
        p.w = (6 + 6 * frand()) * s;
        p.h = (3 + 4 * frand()) * s;
        p.color = colors[rng.below(6)];
        confetti_.push_back(p);
    }
}

// ================================================================ input

void App::trigger(const Action& a) {
    Game& g = game();
    switch (a.kind) {
    case ActNav: navigate(Page(a.a)); break;
    case ActBoard: {
        const int c = boardGeom_.hit(ui_.mouseX, ui_.mouseY);
        if (c >= 0) clickCell(c);
        break;
    }
    case ActDigit: pressDigit(a.a, true, false); break;
    case ActUndo: applyMove(g.undo(), -1, Sfx::Erase); break;
    case ActRedo: applyMove(g.redo(), -1, Sfx::Place); break;
    case ActErase: eraseSelected(); break;
    case ActNotes: notesMode_ = !notesMode_; break;
    case ActHint: hintAction(); break;
    case ActMore: openFlyout(Flyout::More, a); break;
    case ActPause: pause(); break;
    case ActResume: resume(); break;
    case ActNewGameMenu: openFlyout(Flyout::NewGame, a); break;
    case ActNewGame: startClassic(Difficulty(a.a), false); break;
    case ActContinue: showGame(GameMode(a.a)); break;
    case ActHintNext: hintStage_ = 1; break;
    case ActHintApply: applyHint(); break;
    case ActHintClose: closeHint(); break;
    case ActMenu: menuItem(a.a); break;
    case ActCloseFlyout: flyout_ = Flyout::None; break;
    case ActDialog: dialogButton(a.a); break;
    case ActDailyDay: dailySelected_ = a.b; break;
    case ActDailyMonth: {
        int y = dailyMonth_ / 100, m = dailyMonth_ % 100 + a.a;
        if (m < 1) { m = 12; --y; }
        if (m > 12) { m = 1; ++y; }
        const int ym = y * 100 + m;
        if (ym <= today_ / 100 && ym >= 202001) dailyMonth_ = ym;
        break;
    }
    case ActDailyPlay: playDaily(a.b, false); break;
    case ActSegment: segment(a.a, a.b); break;
    case ActToggle: toggleSetting(a.a); break;
    case ActAccent:
        settings_.accent = AccentChoice(a.a);
        applyTheme();
        saveSettings();
        break;
    case ActOpenData: storage::openFolder(storage::dataDir()); break;
    case ActResetStats: dialog_ = Dialog::ResetStats; break;
    case ActResetAchievements: dialog_ = Dialog::ResetAchievements; break;
    case ActShortcuts: dialog_ = Dialog::Help; break;
    default: break;
    }
    syncTimer();
    invalidate();
}

void App::clickCell(int c) {
    Game& g = game();
    flyout_ = Flyout::None;
    if (paused_) {
        resume();
        return;
    }
    if (!g.isPlaying()) {
        selected_ = c;
        return;
    }
    if (digitLock_) {
        if (!g.isLocked(c, rules())) {
            if (notesMode_) {
                if (!g.values[c]) applyMove(g.toggleNote(c, digitLock_), c, Sfx::Note);
            } else if (g.values[c] == digitLock_) {
                applyMove(g.erase(c, rules()), c, Sfx::Erase);
            } else {
                applyMove(g.setValue(c, digitLock_, rules()), c);
            }
        }
        return;
    }
    selected_ = c;
}

void App::pressDigit(int d, bool fromNumpad, bool asNote) {
    Game& g = game();
    if (!g.isPlaying() || paused_) return;
    if (fromNumpad) {
        if (digitLock_ == d) {
            digitLock_ = 0;
            return;
        }
        if (digitLock_ || selected_ < 0 || g.isLocked(selected_, rules())) {
            digitLock_ = d;
            selected_ = -1;
            return;
        }
    } else if (selected_ < 0) {
        digitLock_ = digitLock_ == d ? 0 : d;
        return;
    }
    if (asNote || notesMode_) {
        if (g.values[selected_]) {
            MoveResult blocked;
            blocked.blocked = true;
            applyMove(blocked, selected_);
            return;
        }
        applyMove(g.toggleNote(selected_, d), selected_, Sfx::Note);
    } else {
        applyMove(g.setValue(selected_, d, rules()), selected_);
    }
}

void App::eraseSelected() {
    if (selected_ < 0 || paused_) return;
    applyMove(game().erase(selected_, rules()), selected_, Sfx::Erase);
}

void App::moveSelection(int dr, int dc) {
    digitLock_ = 0;
    if (selected_ < 0) {
        selected_ = 40;
        return;
    }
    const int r = (rowOf(selected_) + dr + 9) % 9, c = (colOf(selected_) + dc + 9) % 9;
    selected_ = cellAt(r, c);
}

void App::applyMove(const MoveResult& m, int cell, Sfx sfx) {
    const double t = now();
    const bool anim = animationsOn();
    if (m.blocked) {
        if (cell >= 0 && anim) anims_.shake[cell] = t;
        invalidate();
        return;
    }
    if (!m.changed) return;
    if (hintActive_) closeHint();
    if (m.placed && cell >= 0) {
        if (m.mistake) {
            if (anim) anims_.shake[cell] = t;
            sound_.play(Sfx::Error);
        } else {
            if (anim) anims_.pop[cell] = t;
            sound_.play(m.completedUnits ? Sfx::Unit : Sfx::Place);
        }
    } else if (sfx != Sfx::Count) {
        sound_.play(sfx);
    }
    if (anim && cell >= 0 && (m.completedUnits || m.completedDigits)) {
        for (int u = 0; u < 27; ++u) {
            if (!(m.completedUnits >> u & 1)) continue;
            int origin = 0;
            for (int i = 0; i < 9; ++i)
                if (kT.unit[u][i] == cell) origin = i;
            for (int i = 0; i < 9; ++i) {
                const int c = kT.unit[u][i];
                const double start = t + 0.04 * std::abs(i - origin);
                if (anims_.wash[c] < 0 || anims_.wash[c] < t) anims_.wash[c] = start;
                else anims_.wash[c] = std::min(anims_.wash[c], start);
            }
        }
        forEachDigit(m.completedDigits, [&](int d) {
            const Game& g = game();
            for (int c = 0; c < 81; ++c)
                if (g.values[c] == d && anims_.wash[c] < t) anims_.wash[c] = t + 0.02 * (std::abs(rowOf(c) - rowOf(cell)) + std::abs(colOf(c) - colOf(cell)));
        });
    }
    if (m.won) onWin();
    else if (m.lost) onLost();
    else if (m.fullWithErrors) showToast(L"Not quite", L"The board is full, but some numbers are wrong.", 0xE7BA);
    scheduleSave();
    invalidate();
}

void App::hintAction() {
    Game& g = game();
    if (!g.isPlaying() || paused_) return;
    flyout_ = Flyout::None;
    if (!hintActive_) {
        hint_ = g.computeHint();
        if (hint_.kind == HintKind::None) return;
        hintActive_ = true;
        hintStage_ = 0;
        ++g.hintsUsed;
        digitLock_ = 0;
        scheduleSave();
        return;
    }
    if (hintStage_ == 0) {
        hintStage_ = 1;
        return;
    }
    applyHint();
}

void App::applyHint() {
    if (!hintActive_) return;
    const Hint h = hint_;
    closeHint();
    const int cell = h.kind == HintKind::Logic ? h.step.placeCell : h.cell;
    if (cell >= 0) selected_ = cell;
    applyMove(game().applyHint(h, rules()), cell, Sfx::Note);
}

void App::closeHint() {
    hintActive_ = false;
    hintStage_ = 0;
    hint_ = Hint{};
}

void App::openFlyout(Flyout f, const Action& from) {
    if (flyout_ == f) {
        flyout_ = Flyout::None;
        return;
    }
    flyout_ = f;
    if (const Region* reg = ui_.find(from)) flyoutAnchor_ = reg->rect;
    ui_.focus = {};
}

void App::menuItem(int item) {
    flyout_ = Flyout::None;
    if (item >= 0 && item < kDifficultyCount) {
        startClassic(Difficulty(item), false);
        return;
    }
    Game& g = game();
    switch (item) {
    case MenuRestart: restartPuzzle(false); break;
    case MenuAutoNotes: applyMove(g.fillAllNotes(), -1, Sfx::Note); break;
    case MenuClearNotes: applyMove(g.clearAllNotes(), -1, Sfx::Erase); break;
    case MenuCheckBoard:
        if (g.isPlaying()) {
            ++g.hintsUsed;
            checkFlashUntil_ = now() + 3.0;
            if (hwnd_) SetTimer(hwnd_, kTimerFlash, 3000, nullptr);
            scheduleSave();
        }
        break;
    default: break;
    }
}

void App::dialogButton(int index) {
    const Dialog d = dialog_;
    dialog_ = Dialog::None;
    switch (d) {
    case Dialog::ConfirmNewGame:
        if (index == 0) startClassic(pendingDifficulty_, true);
        break;
    case Dialog::ConfirmDaily:
        if (index == 0) playDaily(pendingDaily_, true);
        break;
    case Dialog::ConfirmRestart:
        if (index == 0) restartPuzzle(true);
        break;
    case Dialog::GameOver: {
        Game& g = game();
        if (index == 0) {
            g.grantSecondChance();
            scheduleSave();
            break;
        }
        const GameMode mode = g.mode;
        const Difficulty diff = g.difficulty;
        recordResult(g, GameResult::Lost);
        endGame(g);
        refreshStats(true);
        if (index == 1 && mode == GameMode::Classic) startClassic(diff, true);
        else if (mode == GameMode::Daily) navigate(Page::Daily);
        break;
    }
    case Dialog::Win: {
        const GameMode mode = win_.mode;
        const Difficulty diff = win_.difficulty;
        endGame(game());
        if (index == 0 && mode == GameMode::Classic) startClassic(diff, true);
        else if (mode == GameMode::Daily) navigate(Page::Daily);
        break;
    }
    case Dialog::ResetStats:
        if (index == 0) {
            history_.clear();
            storage::removeFile(storage::file(L"history.csv"));
            refreshStats(false);
        }
        break;
    case Dialog::ResetAchievements:
        if (index == 0) {
            history_.clear();
            achievements_ = AchievementState{};
            storage::removeFile(storage::file(L"history.csv"));
            storage::removeFile(storage::file(L"achievements.ini"));
            refreshStats(false);
        }
        break;
    case Dialog::Generating: generatingDaily_ = 0; break;
    default: break;
    }
    ui_.focus = {};
    syncTimer();
    invalidate();
}

void App::closeDialog() {
    switch (dialog_) {
    case Dialog::GameOver: return;  // must choose explicitly
    case Dialog::Win: dialogButton(1); return;
    default: dialogButton(-1); return;
    }
}

void App::segment(int group, int index) {
    switch (group) {
    case SegTheme:
        settings_.theme = ThemeMode(index);
        applyTheme();
        break;
    case SegBackdrop:
        settings_.backdrop = Backdrop(index);
        applyTheme();
        break;
    case SegMistakes: settings_.mistakeLimit = index == 0 ? 0 : index == 1 ? 3 : 5; break;
    case SegVolume:
        settings_.volume = index + 1;
        sound_.configure(settings_.sound, settings_.volume);
        sound_.play(Sfx::Place);
        break;
    case SegStatsTab: statsTab_ = index; return;
    default: return;
    }
    saveSettings();
}

void App::toggleSetting(int id) {
    Settings& s = settings_;
    switch (id) {
    case TogAnimations: s.animations = !s.animations; break;
    case TogCheckMistakes: s.checkMistakes = !s.checkMistakes; break;
    case TogAutoNotes: s.autoRemoveNotes = !s.autoRemoveNotes; break;
    case TogAutoPause: s.autoPause = !s.autoPause; break;
    case TogShowTimer: s.showTimer = !s.showTimer; break;
    case TogHighlightRegion: s.highlightRegion = !s.highlightRegion; break;
    case TogHighlightSame: s.highlightSame = !s.highlightSame; break;
    case TogHighlightConflicts: s.highlightConflicts = !s.highlightConflicts; break;
    case TogHideCompleted: s.hideCompleted = !s.hideCompleted; break;
    case TogShowCounts: s.showCounts = !s.showCounts; break;
    case TogSound:
        s.sound = !s.sound;
        sound_.configure(s.sound, s.volume);
        sound_.play(Sfx::Place);
        break;
    default: return;
    }
    saveSettings();
}

bool App::onKeyDown(UINT vk, bool shift, bool ctrl, bool alt) {
    if (alt && vk == VK_F4) return false;
    if (vk == VK_F11) {
        toggleFullscreen();
        return true;
    }
    if (vk == VK_F1) {
        dialog_ = dialog_ == Dialog::Help ? Dialog::None : (dialog_ == Dialog::None ? Dialog::Help : dialog_);
        syncTimer();
        invalidate();
        return true;
    }
    if (vk == VK_TAB) {
        ui_.moveFocus(shift ? -1 : 1);
        invalidate();
        return true;
    }
    if (dialog_ != Dialog::None) {
        // Ignore activation keys until the dialog has been on screen briefly, so a key press
        // meant for the game (e.g. Enter on the final hint) can't dismiss the win screen unseen.
        const bool settled = now() >= dialogAt_ + 0.35;
        if (vk == VK_ESCAPE) {
            if (settled) closeDialog();
        } else if ((vk == VK_RETURN || vk == VK_SPACE) && !settled) {
            // swallow
        } else if (vk == VK_RETURN || vk == VK_SPACE) {
            if (ui_.focusVisible && ui_.focus.valid()) trigger(ui_.focus);
            else if (dialog_ != Dialog::Generating) dialogButton(0);
        } else if (vk == VK_LEFT || vk == VK_RIGHT) {
            ui_.moveFocus(vk == VK_RIGHT ? 1 : -1);
        }
        invalidate();
        return true;
    }
    if (flyout_ != Flyout::None) {
        if (vk == VK_ESCAPE) flyout_ = Flyout::None;
        else if (vk == VK_DOWN || vk == VK_UP) ui_.moveFocus(vk == VK_DOWN ? 1 : -1);
        else if ((vk == VK_RETURN || vk == VK_SPACE) && ui_.focus.valid()) trigger(ui_.focus);
        invalidate();
        return true;
    }
    if (ctrl && vk >= '1' && vk <= '5') {
        navigate(Page(vk - '1'));
        invalidate();
        return true;
    }
    if ((vk == VK_RETURN || vk == VK_SPACE) && ui_.focusVisible && ui_.focus.valid()) {
        trigger(ui_.focus);
        return true;
    }
    if (vk == VK_ESCAPE && ui_.focusVisible) {
        ui_.focusVisible = false;
        invalidate();
        return true;
    }
    if (inGame() && handleGameKey(vk, shift, ctrl, alt)) {
        syncTimer();
        invalidate();
        return true;
    }
    if (ctrl && vk == 'N') {
        navigate(Page::Play);
        if (game().active) openFlyout(Flyout::NewGame, Action{ActNewGameMenu});
        invalidate();
        return true;
    }
    if (!(page_ == Page::Play && game().active)) {
        float& sc = scroll_[int(page_)];
        const float page = viewport_.h * 0.85f;
        switch (vk) {
        case VK_DOWN: sc += ui_.dp(48); break;
        case VK_UP: sc -= ui_.dp(48); break;
        case VK_NEXT: sc += page; break;
        case VK_PRIOR: sc -= page; break;
        case VK_HOME: sc = 0; break;
        case VK_END: sc = 1e9f; break;
        default: return false;
        }
        invalidate();
        return true;
    }
    return false;
}

bool App::handleGameKey(UINT vk, bool shift, bool ctrl, bool alt) {
    Game& g = game();
    if (paused_) {
        if (vk == 'P' || vk == VK_ESCAPE || vk == VK_SPACE || vk == VK_RETURN) {
            resume();
            return true;
        }
        return false;
    }
    if (ctrl) {
        if (vk == 'Z') {
            applyMove(shift ? g.redo() : g.undo(), -1, shift ? Sfx::Place : Sfx::Erase);
            return true;
        }
        if (vk == 'Y') {
            applyMove(g.redo(), -1, Sfx::Place);
            return true;
        }
        if (vk == 'N') {
            openFlyout(Flyout::NewGame, Action{ActNewGameMenu});
            return true;
        }
        return false;
    }
    int digit = 0;
    if (vk >= '1' && vk <= '9') digit = int(vk - '0');
    else if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) digit = int(vk - VK_NUMPAD0);
    if (digit) {
        pressDigit(digit, false, shift || alt);
        return true;
    }
    switch (vk) {
    case VK_LEFT: moveSelection(0, -1); return true;
    case VK_RIGHT: moveSelection(0, 1); return true;
    case VK_UP: moveSelection(-1, 0); return true;
    case VK_DOWN: moveSelection(1, 0); return true;
    case '0':
    case VK_NUMPAD0:
    case VK_DELETE:
    case VK_BACK:
    case VK_DECIMAL: eraseSelected(); return true;
    case 'N':
    case VK_SPACE: notesMode_ = !notesMode_; return true;
    case 'H': hintAction(); return true;
    case VK_RETURN:
        if (hintActive_) {
            if (hintStage_ == 0) hintStage_ = 1;
            else applyHint();
        }
        return true;
    case 'P': pause(); return true;
    case 'A': applyMove(g.fillAllNotes(), -1, Sfx::Note); return true;
    case VK_ESCAPE:
        if (hintActive_) closeHint();
        else if (digitLock_) digitLock_ = 0;
        else pause();
        return true;
    default: return false;
    }
}

void App::toggleFullscreen() {
    if (!hwnd_) return;
    const LONG style = GetWindowLongW(hwnd_, GWL_STYLE);
    if (!fullscreen_) {
        savedPlacement_.length = sizeof(savedPlacement_);
        GetWindowPlacement(hwnd_, &savedPlacement_);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd_, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        fullscreen_ = true;
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &savedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = false;
    }
    invalidate();
}

// ================================================================ timer & saving

void App::syncTimer() {
    const bool run = !offscreen_ && page_ == Page::Play && game().isPlaying() && !paused_ &&
                     dialog_ == Dialog::None && !minimized_ && (active_ || !settings_.autoPause);
    Game* target = run ? &game() : nullptr;
    if (timerRunning_ && timerGame_ != target) {
        timerGame_->elapsedMs += int64_t((now() - timerStart_) * 1000.0);
        timerRunning_ = false;
        timerGame_ = nullptr;
        if (hwnd_) KillTimer(hwnd_, kTimerClock);
    }
    if (run && !timerRunning_) {
        timerRunning_ = true;
        timerGame_ = target;
        timerStart_ = now();
        scheduleClockTick();
    }
}

int64_t App::elapsedOf(const Game& g) const {
    return g.elapsedMs + ((timerRunning_ && timerGame_ == &g) ? int64_t((now() - timerStart_) * 1000.0) : 0);
}

void App::scheduleClockTick() {
    if (!hwnd_ || !timerRunning_) return;
    const int64_t e = elapsedOf(*timerGame_);
    const UINT ms = UINT(1000 - (e % 1000)) + 2;
    SetCoalescableTimer(hwnd_, kTimerClock, ms, nullptr, 15);
}

void App::scheduleSave() {
    saveDirty_ = true;
    if (hwnd_) SetTimer(hwnd_, kTimerSave, 1500, nullptr);
}

void App::saveNow() {
    if (offscreen_) return;
    if (hwnd_) KillTimer(hwnd_, kTimerSave);
    saveDirty_ = false;
    auto saveSlot = [&](Game& g, const wchar_t* name) {
        const std::wstring path = storage::file(name);
        if (!g.active || g.status == GameStatus::Won) {
            storage::removeFile(path);
            return;
        }
        const int64_t elapsed = elapsedOf(g), stored = g.elapsedMs;
        g.elapsedMs = elapsed;
        const std::string text = g.serialize();
        g.elapsedMs = stored;
        storage::writeAtomic(path, text);
    };
    saveSlot(classic_, L"save_classic.txt");
    saveSlot(daily_, L"save_daily.txt");
}

void App::saveSettings() {
    if (!offscreen_) storage::writeAtomic(storage::file(L"settings.ini"), settings_.serialize());
}

// ================================================================ frame loop

bool App::needsFrame() const {
    if (dirty_) return true;
    const double t = now();
    if (dialog_ == Dialog::Generating) return true;
    if (dialog_ == Dialog::Win && t < dialogAt_ + 0.3) return true;
    if (!animationsOn()) return false;
    if (anims_.active(t)) return true;
    if (pageChangedAt_ >= 0 && t - pageChangedAt_ < 0.25) return true;
    if (confettiStart_ >= 0 && t - confettiStart_ < 3.4) return true;
    for (const Toast& toast : toasts_) {
        const double age = t - toast.start;
        if (age < 0.3 || (age > kToastDuration - 0.35 && age < kToastDuration + 0.05)) return true;
    }
    if (hintActive_ && hintStage_ == 1) return false;
    return false;
}

void App::render() {
    if (!renderer_.dc()) return;
    if (!offscreen_) renderer_.waitForFrame();
    ui_.time = now();
    ui_.beginFrame();
    if (!renderer_.beginDraw()) return;
    drawFrame();
    renderer_.endDraw();
    ui_.endFrame();
    ui_.ensureFocusValid();
    dirty_ = false;
    if (!offscreen_) {
        // The layout may have moved under the pointer.
        const Region* reg = ui_.hitTest(ui_.mouseX, ui_.mouseY);
        const Action h = reg ? reg->action : Action{};
        if (!(h == ui_.hot)) {
            ui_.hot = h;
            ui_.hotSince = now();
            dirty_ = true;
        }
    }
}

}  // namespace sudoku
