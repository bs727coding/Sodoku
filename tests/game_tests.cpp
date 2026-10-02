// Tests for the game layer: moves, undo/redo, mistakes, notes, hints, saves, history, stats,
// achievements and settings.
#include <cstdio>

#include "check.h"
#include "engine/date.h"
#include "engine/generator.h"
#include "game/achievements.h"
#include "game/game.h"
#include "game/history.h"
#include "game/settings.h"

using namespace sudoku;

namespace {

Puzzle samplePuzzle(Difficulty d = Difficulty::Medium, uint64_t seed = 321) {
    Rng rng(seed);
    Puzzle p;
    generate(d, rng, p);
    return p;
}

int firstEmpty(const Game& g, int from = 0) {
    for (int c = from; c < 81; ++c)
        if (!g.values[c]) return c;
    return -1;
}

void testMoves() {
    std::printf("[game: moves]\n");
    const Puzzle p = samplePuzzle();
    const Rules r;  // check mistakes, limit 3, auto-remove notes
    Game g;
    g.start(p, GameMode::Classic, 0, 1000);
    CHECK(g.isPlaying());

    const int e = firstEmpty(g);
    const int wrong = p.solution[e] % 9 + 1;
    MoveResult m = g.setValue(e, wrong, r);
    CHECK(m.placed && m.mistake && g.mistakes == 1 && g.isWrong(e) && !g.isLocked(e, r));

    m = g.setValue(e, p.solution[e], r);
    CHECK(m.placed && !m.mistake && g.isLocked(e, r));
    CHECK(g.erase(e, r).blocked);

    CHECK(g.undo().changed && g.values[e] == wrong);
    CHECK(g.redo().changed && g.values[e] == p.solution[e]);
    CHECK(g.mistakes == 1);  // undo never refunds mistakes

    int given = 0;
    while (!g.givens[given]) ++given;
    CHECK(g.setValue(given, g.values[given] % 9 + 1, r).blocked);

    // Placing a digit removes it from the notes of every peer.
    int a = -1, b = -1;
    for (int c = 0; c < 81 && b < 0; ++c) {
        if (g.values[c]) continue;
        for (int pr : kT.peers[c]) {
            if (!g.values[pr]) {
                a = c;
                b = pr;
                break;
            }
        }
    }
    CHECK(a >= 0 && b >= 0);
    const int d = p.solution[a];
    CHECK(g.toggleNote(b, d).changed);
    CHECK(g.notes[b] & digitBit(d));
    g.setValue(a, d, r);
    CHECK(!(g.notes[b] & digitBit(d)));
    g.undo();
    CHECK(g.notes[b] & digitBit(d));  // undo restores the note too

    // Auto notes equal the candidates implied by the board.
    CHECK(g.fillAllNotes().changed && g.autoNotesUsed);
    for (int c = 0; c < 81; ++c)
        if (!g.values[c]) CHECK(g.notes[c] == g.candidatesAt(c));
    CHECK(g.clearAllNotes().changed);
}

void testMistakeLimit() {
    std::printf("[game: mistake limit]\n");
    const Puzzle p = samplePuzzle();
    const Rules r;
    Game g;
    g.start(p, GameMode::Classic, 0, 0);
    int cell = -1;
    MoveResult m;
    for (int i = 0; i < 3; ++i) {
        cell = firstEmpty(g, cell + 1);
        m = g.setValue(cell, p.solution[cell] % 9 + 1, r);
    }
    CHECK(m.lost && g.status == GameStatus::Lost);
    CHECK(!g.setValue(firstEmpty(g), 1, r).changed);  // no moves after losing
    g.grantSecondChance();
    CHECK(g.isPlaying() && g.mistakeAllowance(r) == 4);
    cell = firstEmpty(g, cell + 1);
    CHECK(g.setValue(cell, p.solution[cell] % 9 + 1, r).lost);

    // Unlimited mode never loses, and with checking off nothing counts as a mistake.
    Rules relaxed;
    relaxed.checkMistakes = false;
    relaxed.mistakeLimit = 0;
    Game h;
    h.start(p, GameMode::Classic, 0, 0);
    for (int c = 0; c < 81; ++c)
        if (!h.values[c]) h.setValue(c, p.solution[c] % 9 + 1, relaxed);
    CHECK(h.isPlaying() && h.mistakes == 0);
    CHECK(h.filledCount() == 81);
    CHECK(!h.conflicts().empty());
}

void testWinAndHints() {
    std::printf("[game: hints and winning]\n");
    const Puzzle p = samplePuzzle(Difficulty::Hard, 77);
    const Rules r;
    Game g;
    g.start(p, GameMode::Daily, 20261001, 0);

    Hint h = g.computeHint();
    CHECK(h.kind == HintKind::Logic && !h.text.detail.empty());

    const int e = firstEmpty(g);
    g.setValue(e, p.solution[e] % 9 + 1, r);
    h = g.computeHint();
    CHECK(h.kind == HintKind::Mistake && h.cell == e);
    g.applyHint(h, r);
    CHECK(g.values[e] == 0);

    g.toggleNote(e, p.solution[e] % 9 + 1);  // notes that miss the real digit
    h = g.computeHint();
    CHECK(h.kind == HintKind::BadNotes && h.cell == e);
    g.applyHint(h, r);
    CHECK(g.notes[e] == g.candidatesAt(e));

    // Follow hints to the end: every applied hint must keep the board consistent with the solution.
    int guard = 0;
    while (g.isPlaying() && guard++ < 2000) {
        h = g.computeHint();
        CHECK(h.kind == HintKind::Logic || h.kind == HintKind::Reveal);
        const MoveResult m = g.applyHint(h, r);
        CHECK(m.changed);
        for (int c = 0; c < 81; ++c) {
            if (g.values[c]) CHECK(g.values[c] == p.solution[c]);
            else if (g.notes[c]) CHECK(g.notes[c] & digitBit(p.solution[c]));
        }
        if (!m.changed) break;
    }
    CHECK(g.status == GameStatus::Won);
    CHECK(g.values == p.solution);
    std::printf("    solved a Hard puzzle purely with hints in %d steps\n", guard);
}

void testSerialization() {
    std::printf("[game: save/load]\n");
    const Puzzle p = samplePuzzle();
    const Rules r;
    Game g;
    g.start(p, GameMode::Daily, 20261001, 12345);
    const int e = firstEmpty(g);
    g.setValue(e, p.solution[e] % 9 + 1, r);
    const int f = firstEmpty(g, e + 1);
    g.toggleNote(f, 3);
    g.toggleNote(f, 7);
    g.fillAllNotes();
    g.undo();
    g.elapsedMs = 98765;
    g.hintsUsed = 2;

    Game copy;
    CHECK(copy.deserialize(g.serialize()));
    CHECK(copy.mode == GameMode::Daily && copy.dailyDate == 20261001);
    CHECK(copy.difficulty == p.difficulty);
    CHECK(copy.givens == g.givens && copy.solution == g.solution && copy.values == g.values);
    CHECK(copy.notes == g.notes);
    CHECK(copy.elapsedMs == 98765 && copy.mistakes == 1 && copy.hintsUsed == 2);
    CHECK(copy.startedUtc == 12345);
    CHECK(copy.canUndo() && copy.canRedo());
    copy.redo();
    g.redo();
    CHECK(copy.notes == g.notes);
    copy.undo();
    copy.undo();
    g.undo();
    g.undo();
    CHECK(copy.values == g.values && copy.notes == g.notes);

    Game junk;
    CHECK(!junk.deserialize("garbage"));
    CHECK(!junk.deserialize(""));
}

GameRecord rec(GameMode mode, Difficulty d, GameResult res, int64_t ms, int mistakes = 0, int hints = 0,
               int localDate = 20261001, int dailyDate = 0, int minute = 600) {
    GameRecord r;
    r.endUtc = 1790000000 + localDate;
    r.localDate = localDate;
    r.localMinute = minute;
    r.mode = mode;
    r.difficulty = d;
    r.dailyDate = dailyDate;
    r.result = res;
    r.timeMs = ms;
    r.mistakes = mistakes;
    r.hints = hints;
    r.givens = std::string(81, '0');
    return r;
}

void testHistoryAndStats() {
    std::printf("[history + stats]\n");
    std::vector<GameRecord> recs = {
        rec(GameMode::Classic, Difficulty::Easy, GameResult::Won, 300000),
        rec(GameMode::Classic, Difficulty::Easy, GameResult::Lost, 50000, 3),
        rec(GameMode::Classic, Difficulty::Easy, GameResult::Won, 200000, 1),
        rec(GameMode::Classic, Difficulty::Expert, GameResult::Won, 800000, 0, 0, 20261001, 0, 120),
        rec(GameMode::Daily, Difficulty::Hard, GameResult::Won, 400000, 0, 1, 20260929, 20260929),
        rec(GameMode::Daily, Difficulty::Hard, GameResult::Won, 350000, 0, 0, 20260930, 20260930),
        rec(GameMode::Daily, Difficulty::Easy, GameResult::Won, 150000, 0, 0, 20261001, 20261001),
        rec(GameMode::Daily, Difficulty::Easy, GameResult::Won, 100000, 0, 0, 20261001, 20260915),  // solved late
    };
    std::string text = historyHeader();
    for (const auto& r : recs) text += formatRecord(r);
    const std::vector<GameRecord> parsed = parseHistory(text);
    CHECK(parsed.size() == recs.size());
    for (size_t i = 0; i < parsed.size() && i < recs.size(); ++i) {
        CHECK(parsed[i].endUtc == recs[i].endUtc && parsed[i].timeMs == recs[i].timeMs);
        CHECK(parsed[i].mode == recs[i].mode && parsed[i].difficulty == recs[i].difficulty);
        CHECK(parsed[i].result == recs[i].result && parsed[i].dailyDate == recs[i].dailyDate);
        CHECK(parsed[i].givens == recs[i].givens);
    }

    const Stats st = computeStats(parsed, 20261001);
    const LevelStats& easy = st.level[int(Difficulty::Easy)];
    CHECK(easy.played == 3 && easy.won == 2 && easy.perfect == 1);
    CHECK(easy.bestMs == 200000 && easy.averageMs() == 250000);
    CHECK(easy.currentStreak == 1 && easy.bestStreak == 1);
    CHECK(st.all.played == 4 && st.all.won == 3 && st.all.currentStreak == 2);
    CHECK(st.daily.played == 4 && st.dailySolved == 4);
    CHECK(st.dailyStreak == 3 && st.dailyBestStreak == 3);
    CHECK(st.days.at(20260915).solved && !st.days.at(20260915).onTime);
    CHECK(st.nightOwl);  // the Expert win at 02:00
    CHECK(st.totalWins == 7);
    CHECK(st.winsAnyMode[int(Difficulty::Hard)] == 2);

    // Streak survives until today's daily is solved; a gap breaks it.
    const Stats tomorrow = computeStats(parsed, 20261002);
    CHECK(tomorrow.dailyStreak == 3);
    const Stats later = computeStats(parsed, 20261003);
    CHECK(later.dailyStreak == 0 && later.dailyBestStreak == 3);

    // Achievements
    AchievementState ach;
    std::vector<int> fresh = updateAchievements(st, ach, 555);
    auto unlocked = [&](const char* id) { return ach.unlocked.count(id) == 1; };
    CHECK(unlocked("first_win") && unlocked("win_easy") && unlocked("win_expert") && unlocked("win_hard"));
    CHECK(unlocked("perfect") && unlocked("perfect_expert") && unlocked("daily_first") && unlocked("night_owl"));
    CHECK(unlocked("speed_easy"));     // the 1:40 daily Easy counts toward it
    CHECK(!unlocked("speed_medium"));  // no Medium wins at all
    CHECK(!unlocked("daily_7") && !unlocked("wins_25") && !unlocked("win_master"));
    CHECK(!fresh.empty());
    CHECK(updateAchievements(st, ach, 556).empty());  // nothing new the second time
    CHECK(achievementProgress(20, st) == 3);           // week warrior: 3 of 7
    AchievementState round;
    round.parse(ach.serialize());
    CHECK(round.unlocked == ach.unlocked);
}

void testSettings() {
    std::printf("[settings]\n");
    Settings s;
    s.theme = ThemeMode::Dark;
    s.accent = AccentChoice::Teal;
    s.backdrop = Backdrop::MicaAlt;
    s.mistakeLimit = 5;
    s.checkMistakes = false;
    s.sound = true;
    s.volume = 3;
    s.lastDifficulty = Difficulty::Master;
    s.window = {true, 10, 20, 1500, 1000, true};
    Settings t;
    t.parse(s.serialize());
    CHECK(t.theme == ThemeMode::Dark && t.accent == AccentChoice::Teal && t.backdrop == Backdrop::MicaAlt);
    CHECK(t.mistakeLimit == 5 && !t.checkMistakes && t.sound && t.volume == 3);
    CHECK(t.lastDifficulty == Difficulty::Master);
    CHECK(t.window.valid && t.window.x == 10 && t.window.w == 1500 && t.window.maximized);
    Settings u;
    u.parse("mistake_limit=7\nvolume=99\ntheme=-4\n");
    CHECK(u.mistakeLimit == 3 && u.volume == 3 && u.theme == ThemeMode::System);
}

}  // namespace

void runGameTests() {
    testMoves();
    testMistakeLimit();
    testWinAndHints();
    testSerialization();
    testHistoryAndStats();
    testSettings();
}
