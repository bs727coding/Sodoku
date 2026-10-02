// Puzzle generation targeted at a difficulty level, plus deterministic daily puzzles.
#pragma once

#include <atomic>

#include "grid.h"
#include "logic.h"
#include "rng.h"

namespace sudoku {

enum class Difficulty : uint8_t { Easy, Medium, Hard, Expert, Master, Extreme };
inline constexpr int kDifficultyCount = 6;

const char* difficultyName(Difficulty d);
const char* difficultyBlurb(Difficulty d);  // one-line description of what it takes

struct Puzzle {
    Grid givens{};
    Grid solution{};
    Difficulty difficulty = Difficulty::Easy;
    Tech hardest = Tech::FullHouse;
    int steps = 0;
};

// Generates a unique-solution puzzle whose technique rating matches `d`.
// `cancel` (optional) is polled between attempts. `attempts` (optional) receives the attempt count.
bool generate(Difficulty d, Rng& rng, Puzzle& out, const std::atomic<bool>* cancel = nullptr, int* attempts = nullptr);

Difficulty dailyDifficulty(int date);  // by weekday: Mon Easy ... Sat/Sun Expert
uint64_t dailySeed(int date);
Puzzle generateDaily(int date);        // same date => same puzzle, on every machine

}  // namespace sudoku
