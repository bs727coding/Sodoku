// Game history (append-only CSV log) and the statistics derived from it.
// The log is the single source of truth for every statistic, streak and the daily calendar.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "game.h"

namespace sudoku {

enum class GameResult : uint8_t { Won, Lost, Abandoned };

struct GameRecord {
    int64_t endUtc = 0;   // unix seconds
    int localDate = 0;    // yyyymmdd (local time) when the game ended
    int localMinute = 0;  // minutes after local midnight when the game ended
    GameMode mode = GameMode::Classic;
    Difficulty difficulty = Difficulty::Easy;
    int dailyDate = 0;
    GameResult result = GameResult::Won;
    int64_t timeMs = 0;
    int mistakes = 0;
    int hints = 0;
    bool secondChance = false;
    std::string givens;

    bool perfect() const { return result == GameResult::Won && mistakes == 0 && hints == 0 && !secondChance; }
};

std::string historyHeader();
std::string formatRecord(const GameRecord& r);  // one CSV line, including '\n'
std::vector<GameRecord> parseHistory(std::string_view text);

struct LevelStats {
    int played = 0, won = 0, perfect = 0, hints = 0;
    int64_t bestMs = 0, totalWonMs = 0, totalMs = 0;
    int currentStreak = 0, bestStreak = 0;
    std::vector<std::pair<int64_t, int64_t>> recentWins;  // (endUtc, timeMs), oldest first, last 30

    int64_t averageMs() const { return won ? totalWonMs / won : 0; }
    double winRate() const { return played ? double(won) / played : 0.0; }
};

struct DailyDay {
    bool solved = false;
    bool onTime = false;  // solved on the day itself
    int64_t bestMs = 0;
};

struct Stats {
    LevelStats level[kDifficultyCount];
    LevelStats all;    // every classic game
    LevelStats daily;  // every daily game
    std::map<int, DailyDay> days;
    int dailyStreak = 0, dailyBestStreak = 0, dailySolved = 0;
    int64_t totalPlayMs = 0;
    int totalWins = 0, perfectWins = 0;
    // Per difficulty across classic *and* daily games (used by achievements).
    int winsAnyMode[kDifficultyCount]{};
    int perfectAnyMode[kDifficultyCount]{};
    int64_t bestAnyMode[kDifficultyCount]{};
    bool nightOwl = false;     // won between 00:00 and 05:00
    bool secondWind = false;   // won after a second chance
    bool selfReliant = false;  // won Hard or harder without hints
    bool fullMonth = false;    // every daily of some month solved
};

Stats computeStats(const std::vector<GameRecord>& records, int today);

}  // namespace sudoku
