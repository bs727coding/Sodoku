#include "generator.h"

#include "date.h"
#include "solver.h"

namespace sudoku {
namespace {

struct Spec {
    int minGivens, maxGivens;
    bool digToMinimal;  // keep removing clues while the solution stays unique
    int minTier, maxTier;
};

constexpr Spec kSpecs[kDifficultyCount] = {
    {36, 42, false, 1, 1},  // Easy: singles only, plenty of clues
    {30, 35, false, 1, 2},  // Medium: up to locked candidates
    {17, 30, true, 3, 3},   // Hard: needs pairs / triples
    {17, 30, true, 4, 4},   // Expert: fish, wings, coloring, quads, rectangles
    {17, 30, true, 5, 5},   // Master: chains, finned fish, jellyfish, BUG
    {17, 30, true, 6, 6},   // Extreme: forcing chains
};

constexpr const char* kNames[kDifficultyCount] = {"Easy", "Medium", "Hard", "Expert", "Master", "Extreme"};
constexpr const char* kBlurbs[kDifficultyCount] = {
    "Singles only",
    "Locked candidates",
    "Pairs and triples",
    "X-Wings and Y-Wings",
    "Chains and finned fish",
    "Forcing chains",
};

}  // namespace

const char* difficultyName(Difficulty d) { return kNames[int(d) % kDifficultyCount]; }
const char* difficultyBlurb(Difficulty d) { return kBlurbs[int(d) % kDifficultyCount]; }

bool generate(Difficulty diff, Rng& rng, Puzzle& out, const std::atomic<bool>* cancel, int* attempts) {
    const Spec& sp = kSpecs[int(diff)];
    for (int attempt = 1;; ++attempt) {
        if (attempts) *attempts = attempt;
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;

        const Grid sol = randomSolution(rng);
        Grid puz = sol;
        const int target = sp.digToMinimal ? 0 : rng.range(sp.minGivens, sp.maxGivens);

        // Remove clues in 180-degree symmetric pairs (c, 80 - c), in random order.
        int groups[41];
        for (int c = 0; c <= 40; ++c) groups[c] = c;
        rng.shuffle(groups, 41);
        int givens = 81;
        for (int i = 0; i < 41 && givens > target; ++i) {
            const int a = groups[i], b = 80 - a;
            const int n = a == b ? 1 : 2;
            if (givens - n < target) continue;
            puz[a] = 0;
            puz[b] = 0;
            // The old puzzle was unique, so any new solution must differ at a or b.
            const bool unique = !hasSolutionWithout(puz, a, sol[a]) && (a == b || !hasSolutionWithout(puz, b, sol[b]));
            if (unique) {
                givens -= n;
            } else {
                puz[a] = sol[a];
                puz[b] = sol[b];
            }
        }
        if (givens < sp.minGivens || givens > sp.maxGivens) continue;

        const Rating r = rate(puz, sp.maxTier);
        if (!r.solved || r.tier < sp.minTier) continue;

        out.givens = puz;
        out.solution = sol;
        out.difficulty = diff;
        out.hardest = r.hardest;
        out.steps = r.steps;
        return true;
    }
}

Difficulty dailyDifficulty(int date) {
    static constexpr Difficulty kByWeekday[7] = {
        Difficulty::Easy, Difficulty::Medium, Difficulty::Medium, Difficulty::Hard,
        Difficulty::Hard, Difficulty::Expert, Difficulty::Expert,
    };
    return kByWeekday[weekday(date)];
}

uint64_t dailySeed(int date) {
    uint64_t x = 0xDA11C0DE5EEDull ^ (uint64_t(uint32_t(date)) * 0x9E3779B97F4A7C15ull);
    return splitmix64(x);
}

Puzzle generateDaily(int date) {
    Rng rng(dailySeed(date));
    Puzzle p;
    generate(dailyDifficulty(date), rng, p);
    return p;
}

}  // namespace sudoku
