// Achievement definitions, progress evaluation (from Stats) and persistence of unlock times.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "history.h"

namespace sudoku {

struct AchievementDef {
    const char* id;
    const wchar_t* name;
    const wchar_t* description;
    wchar_t icon;  // Segoe Fluent Icons code point
    int target;
};

extern const AchievementDef kAchievements[];
extern const int kAchievementCount;

int achievementProgress(int index, const Stats& st);  // 0..target

struct AchievementState {
    std::map<std::string, int64_t> unlocked;  // id -> unix time

    bool isUnlocked(int index) const;
    int64_t unlockedAt(int index) const;
    std::string serialize() const;
    void parse(std::string_view text);
};

// Unlocks newly earned achievements; returns their indices.
std::vector<int> updateAchievements(const Stats& st, AchievementState& state, int64_t nowUtc);

}  // namespace sudoku
