#include "achievements.h"

#include <algorithm>
#include <charconv>

namespace sudoku {

const AchievementDef kAchievements[] = {
    {"first_win", L"First Steps", L"Solve your first puzzle.", 0xE726, 1},
    {"win_easy", L"Warming Up", L"Solve an Easy puzzle.", 0xE706, 1},
    {"win_medium", L"Getting Serious", L"Solve a Medium puzzle.", 0xE8E1, 1},
    {"win_hard", L"Hard Boiled", L"Solve a Hard puzzle.", 0xE7C1, 1},
    {"win_expert", L"Expert Mind", L"Solve an Expert puzzle.", 0xE7BE, 1},
    {"win_master", L"Master Class", L"Solve a Master puzzle.", 0xEB95, 1},
    {"win_extreme", L"Off the Charts", L"Solve an Extreme puzzle.", 0xEAFC, 1},
    {"perfect", L"Flawless", L"Solve a puzzle with no mistakes and no hints.", 0xE735, 1},
    {"perfect_expert", L"Flawless Expert", L"Solve an Expert puzzle with no mistakes and no hints.", 0xE794, 1},
    {"perfect_master", L"Flawless Master", L"Solve a Master puzzle with no mistakes and no hints.", 0xEC61, 1},
    {"perfect_10", L"Untouchable", L"Solve 10 puzzles with no mistakes and no hints.", 0xEA18, 10},
    {"speed_easy", L"Quick Draw", L"Solve an Easy puzzle in under 3 minutes.", 0xE916, 1},
    {"speed_medium", L"Fast Thinker", L"Solve a Medium puzzle in under 5 minutes.", 0xE823, 1},
    {"speed_hard", L"Speed Demon", L"Solve a Hard puzzle in under 10 minutes.", 0xE945, 1},
    {"speed_expert", L"Redline", L"Solve an Expert puzzle in under 15 minutes.", 0xEC4A, 1},
    {"streak_5", L"On a Roll", L"Win 5 classic games in a row.", 0xECAD, 5},
    {"streak_15", L"Unstoppable", L"Win 15 classic games in a row.", 0xEC24, 15},
    {"wins_25", L"Dedicated", L"Solve 25 puzzles.", 0xE8F1, 25},
    {"wins_100", L"Centurion", L"Solve 100 puzzles.", 0xE728, 100},
    {"daily_first", L"Daily Habit", L"Complete a daily challenge.", 0xE787, 1},
    {"daily_7", L"Week Warrior", L"Solve the daily challenge 7 days in a row.", 0xEC92, 7},
    {"daily_month", L"Month Master", L"Complete every daily challenge in a calendar month.", 0xEA89, 1},
    {"marathon", L"Marathon", L"Play for 10 hours in total.", 0xEC32, 600},
    {"night_owl", L"Night Owl", L"Solve a puzzle between midnight and 5 AM.", 0xE708, 1},
    {"self_reliant", L"Self-Reliant", L"Solve a Hard or harder puzzle without hints.", 0xEB50, 1},
    {"second_wind", L"Second Wind", L"Win a game after using a second chance.", 0xEB52, 1},
};

const int kAchievementCount = int(std::size(kAchievements));

namespace {

int under(int64_t bestMs, int64_t limitMs) { return bestMs > 0 && bestMs < limitMs ? 1 : 0; }

}  // namespace

int achievementProgress(int index, const Stats& st) {
    // Daily wins count toward the per-difficulty achievements as well.
    const auto wins = [&](Difficulty d) { return st.winsAnyMode[int(d)]; };
    const auto perfect = [&](Difficulty d) { return st.perfectAnyMode[int(d)]; };
    const auto best = [&](Difficulty d) { return st.bestAnyMode[int(d)]; };
    int value = 0;
    switch (index) {
    case 0: value = st.totalWins; break;
    case 1: case 2: case 3: case 4: case 5: case 6: value = wins(Difficulty(index - 1)); break;
    case 7: value = st.perfectWins; break;
    case 8: value = perfect(Difficulty::Expert); break;
    case 9: value = perfect(Difficulty::Master); break;
    case 10: value = st.perfectWins; break;
    case 11: value = under(best(Difficulty::Easy), 3 * 60'000); break;
    case 12: value = under(best(Difficulty::Medium), 5 * 60'000); break;
    case 13: value = under(best(Difficulty::Hard), 10 * 60'000); break;
    case 14: value = under(best(Difficulty::Expert), 15 * 60'000); break;
    case 15: case 16: value = st.all.bestStreak; break;
    case 17: case 18: value = st.totalWins; break;
    case 19: value = st.dailySolved; break;
    case 20: value = st.dailyBestStreak; break;
    case 21: value = st.fullMonth ? 1 : 0; break;
    case 22: value = int(st.totalPlayMs / 60'000); break;
    case 23: value = st.nightOwl ? 1 : 0; break;
    case 24: value = st.selfReliant ? 1 : 0; break;
    case 25: value = st.secondWind ? 1 : 0; break;
    default: break;
    }
    return std::clamp(value, 0, kAchievements[index].target);
}

bool AchievementState::isUnlocked(int index) const { return unlocked.count(kAchievements[index].id) != 0; }

int64_t AchievementState::unlockedAt(int index) const {
    auto it = unlocked.find(kAchievements[index].id);
    return it == unlocked.end() ? 0 : it->second;
}

std::string AchievementState::serialize() const {
    std::string out = "# sudoku achievements v1\n";
    for (const auto& [id, t] : unlocked) out += id + "=" + std::to_string(t) + "\n";
    return out;
}

void AchievementState::parse(std::string_view text) {
    unlocked.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        int64_t t = 0;
        const std::string_view v = line.substr(eq + 1);
        if (std::from_chars(v.data(), v.data() + v.size(), t).ec != std::errc{}) continue;
        unlocked[std::string(line.substr(0, eq))] = t;
    }
}

std::vector<int> updateAchievements(const Stats& st, AchievementState& state, int64_t nowUtc) {
    std::vector<int> fresh;
    for (int i = 0; i < kAchievementCount; ++i) {
        if (state.isUnlocked(i)) continue;
        if (achievementProgress(i, st) >= kAchievements[i].target) {
            state.unlocked[kAchievements[i].id] = nowUtc;
            fresh.push_back(i);
        }
    }
    return fresh;
}

}  // namespace sudoku
