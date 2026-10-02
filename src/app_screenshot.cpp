// Dev aid: render a page offscreen with demo data and save it as a PNG.
//   Sudoku.exe --screenshot <page> <light|dark> <width>x<height> <scale> <out.png>
// Pages: play, hint, paused, notes, win, gameover, help, flyout, picker, daily, stats, achievements, settings.
// Nothing is read from or written to the user's data folder.
#include "app.h"
#include "engine/date.h"
#include "game/storage.h"

namespace sudoku {

using namespace ui;

bool App::runScreenshot(const ScreenshotSpec& spec) {
    offscreen_ = true;
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&qpcStart_);
    fixedTime_ = 1000.0;
    dpi_ = UINT(96.0f * spec.scale + 0.5f);
    ui_.scale = spec.scale;
    ui_.r = &renderer_;
    settings_ = Settings{};
    settings_.theme = spec.dark ? ThemeMode::Dark : ThemeMode::Light;
    settings_.animations = false;
    ui_.pal = makePalette(spec.dark, accentFor(AccentChoice::Windows));
    if (!renderer_.initOffscreen(UINT(spec.width), UINT(spec.height))) return false;
    today_ = storage::todayLocal();

    // ---- demo history: a few weeks of classic games and daily challenges
    Rng rng(99);
    const int64_t now = storage::nowUtc();
    const int64_t base[kDifficultyCount] = {240000, 420000, 690000, 1080000, 1500000, 2100000};
    for (int i = 0; i < 48; ++i) {
        GameRecord rec;
        rec.endUtc = now - int64_t(48 - i) * 40000;
        rec.localDate = addDays(today_, -(48 - i) / 3);
        rec.localMinute = 600 + int(rng.below(600));
        rec.mode = GameMode::Classic;
        rec.difficulty = Difficulty(i % 5);
        rec.result = i % 9 == 4 ? GameResult::Lost : (i % 13 == 7 ? GameResult::Abandoned : GameResult::Won);
        rec.timeMs = int64_t(double(base[int(rec.difficulty)]) * (0.7 + 0.6 * rng.below(1000) / 1000.0));
        rec.mistakes = i % 3 == 0 ? 1 : 0;
        rec.hints = i % 5 == 0 ? 1 : 0;
        history_.push_back(rec);
    }
    for (int d = 12; d >= 0; --d) {
        if (d == 6) continue;  // a gap in the streak
        GameRecord rec;
        const int date = addDays(today_, -d);
        rec.endUtc = now - int64_t(d) * 86400;
        rec.localDate = date;
        rec.localMinute = 480;
        rec.mode = GameMode::Daily;
        rec.difficulty = dailyDifficulty(date);
        rec.dailyDate = date;
        rec.result = GameResult::Won;
        rec.timeMs = int64_t(double(base[int(rec.difficulty)]) * (0.8 + 0.4 * rng.below(1000) / 1000.0));
        if (d == 0 && spec.page != L"daily") continue;  // keep today's daily open on other pages
        history_.push_back(rec);
    }
    refreshStats(false);

    // ---- demo game in progress
    Rng gen(2024);
    Puzzle puzzle;
    generate(Difficulty::Hard, gen, puzzle);
    classic_.start(puzzle, GameMode::Classic, 0, now);
    int empties[81], n = 0;
    for (int c = 0; c < 81; ++c)
        if (!puzzle.givens[c]) empties[n++] = c;
    gen.shuffle(empties, n);
    const int fill = n * 2 / 5;
    for (int i = 0; i < fill; ++i) classic_.values[empties[i]] = puzzle.solution[empties[i]];
    for (int i = fill; i < fill + 12 && i < n; ++i) classic_.notes[empties[i]] = classic_.candidatesAt(empties[i]);
    classic_.elapsedMs = 754000;
    classic_.moves = 40;
    current_ = GameMode::Classic;
    page_ = Page::Play;
    selected_ = empties[fill + 1];
    pageChangedAt_ = -1;

    const std::wstring& pg = spec.page;
    if (pg == L"play") {
        const int wrongCell = empties[fill + 14];
        classic_.values[wrongCell] = uint8_t(puzzle.solution[wrongCell] % 9 + 1);
        classic_.mistakes = 1;
    } else if (pg == L"hint") {
        hint_ = classic_.computeHint();
        hintActive_ = hint_.kind != HintKind::None;
        hintStage_ = 1;
        selected_ = -1;
    } else if (pg == L"paused") {
        paused_ = true;
    } else if (pg == L"notes") {
        notesMode_ = true;
        digitLock_ = puzzle.solution[empties[0]];
        selected_ = -1;
    } else if (pg == L"win") {
        win_.mode = GameMode::Classic;
        win_.difficulty = Difficulty::Hard;
        win_.timeMs = 612000;
        win_.bestMs = 598000;
        win_.averageMs = 701000;
        win_.streak = 4;
        win_.perfect = true;
        win_.newBest = false;
        dialog_ = Dialog::Win;
    } else if (pg == L"gameover") {
        classic_.mistakes = 3;
        classic_.status = GameStatus::Lost;
        dialog_ = Dialog::GameOver;
    } else if (pg == L"help") {
        dialog_ = Dialog::Help;
    } else if (pg == L"picker") {
        classic_.clear();
    } else if (pg == L"daily") {
        page_ = Page::Daily;
        dailySelected_ = today_;
        dailyMonth_ = today_ / 100;
    } else if (pg == L"stats") {
        page_ = Page::Stats;
    } else if (pg == L"achievements") {
        page_ = Page::Achievements;
    } else if (pg == L"settings") {
        page_ = Page::Settings;
    }

    render();
    if (pg == L"flyout") {
        if (const Region* reg = ui_.find(Action{ActNewGameMenu})) flyoutAnchor_ = reg->rect;
        flyout_ = Flyout::NewGame;
    }
    render();  // second pass: scroll extents and anchors from the first
    return renderer_.savePng(spec.out);
}

}  // namespace sudoku
