// Engine test-suite and benchmarks.   Run:  .\build.ps1 -Test   (add -TestArgs "--full" for more)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/date.h"
#include "engine/generator.h"
#include "engine/logic.h"
#include "engine/solver.h"
#include "engine/techniques.h"

#include "check.h"

using namespace sudoku;
using Clock = std::chrono::steady_clock;

void runGameTests();  // game_tests.cpp

static double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

static Grid parse(const char* s) {
    Grid g{};
    std::string str;
    for (const char* p = s; *p; ++p)
        if ((*p >= '0' && *p <= '9') || *p == '.') str += *p;
    fromString(str, g);
    return g;
}

static bool agrees(const Grid& puzzle, const Grid& solution) {
    for (int i = 0; i < 81; ++i)
        if (puzzle[i] && puzzle[i] != solution[i]) return false;
    return true;
}

// ------------------------------------------------------------------ tables

static void testTables() {
    std::printf("[tables]\n");
    for (int c = 0; c < 81; ++c) {
        CHECK(kT.peerSet[c].count() == 20);
        CHECK(!kT.peerSet[c].has(c));
        for (int u : kT.unitsOf[c]) CHECK(kT.unitSet[u].has(c));
    }
    for (int u = 0; u < 27; ++u) CHECK(kT.unitSet[u].count() == 9);
    CHECK(boxOf(cellAt(4, 4)) == 4);
    CHECK(boxOf(cellAt(8, 0)) == 6);
    CHECK(cellName(cellAt(2, 4)) == "R3C5");
    CHECK(digitList(Mask(digitBit(3) | digitBit(5) | digitBit(7))) == "3, 5 and 7");
}

// ------------------------------------------------------------------ dates

static void testDates() {
    std::printf("[dates]\n");
    CHECK(weekday(20261001) == 3);   // Thursday
    CHECK(weekday(20240101) == 0);   // Monday
    CHECK(addDays(20261231, 1) == 20270101);
    CHECK(addDays(20240301, -1) == 20240229);
    CHECK(daysInMonth(2026, 2) == 28);
    CHECK(daysInMonth(2028, 2) == 29);
    for (int d = 0; d < 3000; d += 7) CHECK(dateToDays(civilFromDays(d)) == d);
}

// ------------------------------------------------------------------ solver

static void testSolver() {
    std::printf("[solver]\n");
    const Grid wiki = parse("530070000600195000098000060800060003400803001700020006060000280000419005000080079");
    const Grid wikiSol = parse("534678912672195348198342567859761423426853791713924856961537284287419635345286179");
    Grid sol{};
    CHECK(countSolutions(wiki, 2, &sol) == 1);
    CHECK(sol == wikiSol);

    const char* hard[] = {
        "100007090030020008009600500005300900010080002600004000300000010040000007007000300",  // AI Escargot
        "800000000003600000070090200050007000000045700000100030001000068008500010090000400",  // Inkala 2012
        "000000010400000000020000000000050407008000300001090000300400200050100000000806000",  // 17 clues
    };
    for (const char* h : hard) {
        const Grid p = parse(h);
        Grid s{};
        const auto t0 = Clock::now();
        const int n = countSolutions(p, 2, &s);
        const double ms = msSince(t0);
        CHECK(n == 1);
        CHECK(isCompleteSolution(s));
        CHECK(agrees(p, s));
        std::printf("    unique-check %.3f ms  (%s...)\n", ms, std::string(h, 18).c_str());
    }

    Grid empty{};
    CHECK(countSolutions(empty, 2) == 2);
    Grid multi = wiki;
    for (int c = 0; c < 81; ++c)
        if (c % 2) multi[c] = 0;
    CHECK(countSolutions(multi, 2) == 2);
    Grid bad = wiki;
    bad[2] = 5;  // duplicate 5 in row 1
    CHECK(countSolutions(bad, 2) == 0);

    // On a unique puzzle no solution avoids the solution digit; on an ambiguous one some does.
    for (int c = 0; c < 81; ++c) {
        if (wiki[c]) continue;
        CHECK(!hasSolutionWithout(wiki, c, wikiSol[c]));
    }
    int ambiguousCells = 0;
    for (int c = 0; c < 81; ++c)
        if (!multi[c] && hasSolutionWithout(multi, c, wikiSol[c])) ++ambiguousCells;
    CHECK(ambiguousCells > 0);

    Rng rng(7);
    for (int i = 0; i < 50; ++i) CHECK(isCompleteSolution(randomSolution(rng)));

    // Benchmark: solve a set of generated minimal puzzles.
    std::vector<Grid> bench;
    for (int i = 0; i < 40; ++i) {
        Puzzle p;
        generate(Difficulty::Hard, rng, p);
        bench.push_back(p.givens);
    }
    bench.push_back(parse(hard[0]));
    bench.push_back(parse(hard[1]));
    bench.push_back(parse(hard[2]));
    const auto t0 = Clock::now();
    int reps = 0;
    for (int r = 0; r < 25; ++r)
        for (const Grid& g : bench) {
            CHECK(countSolutions(g, 2) == 1);
            ++reps;
        }
    std::printf("    uniqueness check: %.1f us/puzzle avg over %d runs\n", msSince(t0) * 1000.0 / reps, reps);
}

// ----------------------------------------------------------- logic soundness

struct Coverage {
    std::array<long, kTechCount> fired{};
    long steps = 0, unsound = 0;
};

// Walks the ladder on `g`, checking every step against the known solution.
static bool walk(LogicGrid g, const Grid& solution, Coverage& cov, int maxTier, bool checkExplain) {
    Step s;
    for (int guard = 0; guard < 400 && !g.solved(); ++guard) {
        if (!findStep(g, s, maxTier)) return false;
        ++cov.fired[int(s.tech)];
        ++cov.steps;
        bool ok = true;
        if (s.placeCell >= 0 && solution[s.placeCell] != s.placeDigit) ok = false;
        bool progress = s.placeCell >= 0;
        for (int c = 0; c < 81; ++c) {
            if (s.elim[c] && (s.elim[c] & digitBit(solution[c]))) ok = false;
            if (s.elim[c] & g.cand[c]) progress = true;
            if (s.elim[c] & ~g.cand[c]) ok = false;  // must only remove existing candidates
        }
        if (!progress) ok = false;
        if (!ok) {
            ++cov.unsound;
            if (cov.unsound <= 5) {
                std::printf("    UNSOUND %s\n", techName(s.tech));
                std::printf("      grid  %s\n", toString(g.val, '.').c_str());
                std::printf("      sol   %s\n", toString(solution).c_str());
                std::printf("      %s\n", explain(s).detail.c_str());
            }
            return false;
        }
        if (checkExplain) {
            const Explanation e = explain(s);
            CHECK(!e.title.empty() && !e.nudge.empty() && !e.detail.empty());
        }
        g.apply(s);
    }
    return g.solved();
}

static void testGeneratorAndLogic(bool full) {
    std::printf("[generator + logic]\n");
    const int perLevel[kDifficultyCount] = {full ? 60 : 20, full ? 60 : 20, full ? 60 : 20,
                                            full ? 40 : 12, full ? 25 : 6,  full ? 12 : 3};
    Rng rng(20261001);
    Coverage cov;
    for (int lv = 0; lv < kDifficultyCount; ++lv) {
        const Difficulty d = Difficulty(lv);
        double totalMs = 0;
        long attempts = 0;
        int givensMin = 81, givensMax = 0;
        for (int i = 0; i < perLevel[lv]; ++i) {
            Puzzle p;
            int a = 0;
            const auto t0 = Clock::now();
            CHECK(generate(d, rng, p, nullptr, &a));
            totalMs += msSince(t0);
            attempts += a;
            Grid sol{};
            CHECK(countSolutions(p.givens, 2, &sol) == 1);
            CHECK(sol == p.solution);
            CHECK(agrees(p.givens, p.solution));
            const int gv = countFilled(p.givens);
            givensMin = std::min(givensMin, gv);
            givensMax = std::max(givensMax, gv);
            const Rating r = rate(p.givens);
            CHECK(r.solved);
            if (lv >= 2) CHECK(r.tier == lv + 1);  // Hard = tier 3 ... Extreme = tier 6
            if (lv == 0) CHECK(r.tier == 1);
            if (lv == 1) CHECK(r.tier <= 2);
            CHECK(walk(LogicGrid::fromGrid(p.givens), p.solution, cov, kMaxTier, i < 3));
        }
        std::printf("    %-8s %3d puzzles  givens %d-%d  %7.1f ms/puzzle  %6.1f attempts/puzzle\n",
                    difficultyName(d), perLevel[lv], givensMin, givensMax, totalMs / perLevel[lv],
                    double(attempts) / perLevel[lv]);
    }

    // Stress the techniques on unusual candidate states: randomly strip candidates that
    // are not the solution digit (as a player's notes would), then walk the ladder.
    Rng noise(99);
    const int variants = full ? 600 : 150;
    for (int i = 0; i < variants; ++i) {
        Puzzle p;
        generate(Difficulty(3 + i % 3), rng, p);
        LogicGrid g = LogicGrid::fromGrid(p.givens);
        for (int c = 0; c < 81; ++c) {
            if (g.val[c]) continue;
            for (Mask m = g.cand[c]; m; m &= m - 1) {
                const int d = lowestDigit(m);
                if (d != p.solution[c] && noise.below(100) < 12) g.cand[c] &= Mask(~digitBit(d));
            }
        }
        walk(g, p.solution, cov, kMaxTier, false);
    }

    std::printf("    ladder: %ld steps checked, %ld unsound\n", cov.steps, cov.unsound);
    CHECK(cov.unsound == 0);
}

// Calls every finder directly (ignoring ladder order) on every intermediate state of many
// puzzles, so each technique is exercised and checked for soundness on its own.
static void testEveryTechnique(bool full) {
    std::printf("[every technique, isolated]\n");
    using namespace sudoku::detail;
    struct Entry {
        Tech tech;
        Finder find;
    };
    const Entry finders[] = {
        {Tech::FullHouse, findFullHouse},         {Tech::HiddenSingle, findHiddenSingle},
        {Tech::NakedSingle, findNakedSingle},     {Tech::Pointing, findPointing},
        {Tech::Claiming, findClaiming},           {Tech::NakedPair, findNakedPair},
        {Tech::HiddenPair, findHiddenPair},       {Tech::NakedTriple, findNakedTriple},
        {Tech::HiddenTriple, findHiddenTriple},   {Tech::XWing, findXWing},
        {Tech::Skyscraper, findSkyscraper},       {Tech::TwoStringKite, findTwoStringKite},
        {Tech::XYWing, findXYWing},               {Tech::XYZWing, findXYZWing},
        {Tech::WWing, findWWing},                 {Tech::Swordfish, findSwordfish},
        {Tech::SimpleColoring, findSimpleColoring}, {Tech::UniqueRectangle, findUniqueRectangle},
        {Tech::NakedQuad, findNakedQuad},         {Tech::HiddenQuad, findHiddenQuad},
        {Tech::BUG, findBUG},                     {Tech::FinnedXWing, findFinnedXWing},
        {Tech::FinnedSwordfish, findFinnedSwordfish}, {Tech::Jellyfish, findJellyfish},
        {Tech::XYChain, findXYChain},             {Tech::XChain, findXChain},
        {Tech::ForcingChain, findForcingChain},
    };
    std::array<long, kTechCount> hits{}, bad{};
    Rng rng(4242);
    const int puzzles = full ? 300 : 80;
    long states = 0;
    for (int i = 0; i < puzzles; ++i) {
        Puzzle p;
        generate(Difficulty(2 + i % 4), rng, p);
        LogicGrid g = LogicGrid::fromGrid(p.givens);
        for (int guard = 0; guard < 200 && !g.solved(); ++guard) {
            const Analysis a(g);
            ++states;
            for (const Entry& e : finders) {
                Step s;
                if (!e.find(g, a, s)) continue;
                ++hits[int(e.tech)];
                bool ok = !(s.placeCell >= 0 && p.solution[s.placeCell] != s.placeDigit);
                bool progress = s.placeCell >= 0;
                for (int c = 0; c < 81; ++c) {
                    if (s.elim[c] & digitBit(p.solution[c])) ok = false;
                    if (s.elim[c] & ~g.cand[c]) ok = false;
                    if (s.elim[c] & g.cand[c]) progress = true;
                }
                if (!ok || !progress) {
                    if (++bad[int(e.tech)] <= 2) {
                        std::printf("    UNSOUND %s  grid %s\n      %s\n", techName(e.tech),
                                    toString(g.val, '.').c_str(), explain(s).detail.c_str());
                    }
                }
                s.tech = e.tech;
                const Explanation ex = explain(s);
                CHECK(!ex.detail.empty());
            }
            Step next;
            if (!findStep(g, next)) break;
            g.apply(next);
        }
    }
    std::printf("    %ld states x %d finders\n", states, int(std::size(finders)));
    int missing = 0;
    for (int t = 0; t < kTechCount; ++t) {
        std::printf("      %-18s %8ld found  %ld unsound%s\n", techName(Tech(t)), hits[t], bad[t],
                    hits[t] ? "" : "   (never applicable)");
        CHECK(bad[t] == 0);
        missing += hits[t] == 0;
    }
    if (missing) std::printf("    note: %d technique(s) never applicable in this corpus\n", missing);
}

// ------------------------------------------------------------------- daily

static void testDaily() {
    std::printf("[daily]\n");
    const auto t0 = Clock::now();
    const Puzzle a = generateDaily(20261001);
    const double ms = msSince(t0);
    const Puzzle b = generateDaily(20261001);
    const Puzzle c = generateDaily(20261002);
    CHECK(a.givens == b.givens);
    CHECK(a.solution == b.solution);
    CHECK(a.givens != c.givens);
    CHECK(a.difficulty == dailyDifficulty(20261001));
    CHECK(countSolutions(a.givens, 2) == 1);
    std::printf("    2026-10-01 (%s) generated in %.1f ms: %s\n", difficultyName(a.difficulty), ms,
                toString(a.givens, '.').c_str());
    for (int i = 0; i < 7; ++i) {
        const int date = addDays(20261005, i);  // Monday..Sunday
        const auto t1 = Clock::now();
        const Puzzle p = generateDaily(date);
        CHECK(p.difficulty == dailyDifficulty(date));
        std::printf("    %d %-7s %6.1f ms\n", date, difficultyName(p.difficulty), msSince(t1));
    }
}

int main(int argc, char** argv) {
    bool full = false;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--full")) full = true;
    const auto t0 = Clock::now();
    testTables();
    testDates();
    testSolver();
    testGeneratorAndLogic(full);
    testEveryTechnique(full);
    testDaily();
    runGameTests();
    std::printf("\n%d checks, %d failures, %.1f s\n", g_checks, g_failures, msSince(t0) / 1000.0);
    return g_failures ? 1 : 0;
}
