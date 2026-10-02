#include "history.h"

#include <algorithm>
#include <charconv>

#include "engine/date.h"

namespace sudoku {
namespace {

constexpr size_t kRecentWins = 30;

template <class T>
bool parseNum(std::string_view s, T& out) {
    return std::from_chars(s.data(), s.data() + s.size(), out).ec == std::errc{};
}

const char* resultName(GameResult r) {
    switch (r) {
    case GameResult::Won: return "won";
    case GameResult::Lost: return "lost";
    default: return "abandoned";
    }
}

void addRecord(LevelStats& l, const GameRecord& r) {
    ++l.played;
    l.totalMs += r.timeMs;
    l.hints += r.hints;
    if (r.result == GameResult::Won) {
        ++l.won;
        l.totalWonMs += r.timeMs;
        if (!l.bestMs || r.timeMs < l.bestMs) l.bestMs = r.timeMs;
        if (r.perfect()) ++l.perfect;
        ++l.currentStreak;
        l.bestStreak = std::max(l.bestStreak, l.currentStreak);
        l.recentWins.push_back({r.endUtc, r.timeMs});
        if (l.recentWins.size() > kRecentWins) l.recentWins.erase(l.recentWins.begin());
    } else {
        l.currentStreak = 0;
    }
}

}  // namespace

std::string historyHeader() {
    return "# sudoku history v1\n"
           "end_utc,local_date,local_minute,mode,difficulty,daily_date,result,time_ms,mistakes,hints,second_chance,givens\n";
}

std::string formatRecord(const GameRecord& r) {
    std::string s;
    s += std::to_string(r.endUtc) + ',';
    s += std::to_string(r.localDate) + ',';
    s += std::to_string(r.localMinute) + ',';
    s += (r.mode == GameMode::Daily ? "daily," : "classic,");
    s += std::to_string(int(r.difficulty)) + ',';
    s += std::to_string(r.dailyDate) + ',';
    s += resultName(r.result);
    s += ',';
    s += std::to_string(r.timeMs) + ',';
    s += std::to_string(r.mistakes) + ',';
    s += std::to_string(r.hints) + ',';
    s += r.secondChance ? "1," : "0,";
    s += r.givens;
    s += '\n';
    return s;
}

std::vector<GameRecord> parseHistory(std::string_view text) {
    std::vector<GameRecord> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#' || line.starts_with("end_utc")) continue;

        std::string_view f[12];
        int n = 0;
        size_t p = 0;
        while (n < 12) {
            const size_t comma = line.find(',', p);
            if (comma == std::string_view::npos || n == 11) {
                f[n++] = line.substr(p);
                break;
            }
            f[n++] = line.substr(p, comma - p);
            p = comma + 1;
        }
        if (n < 11) continue;

        GameRecord r;
        int diff = 0;
        if (!parseNum(f[0], r.endUtc) || !parseNum(f[1], r.localDate) || !parseNum(f[2], r.localMinute) ||
            !parseNum(f[4], diff) || !parseNum(f[5], r.dailyDate) || !parseNum(f[7], r.timeMs) ||
            !parseNum(f[8], r.mistakes) || !parseNum(f[9], r.hints))
            continue;
        if (diff < 0 || diff >= kDifficultyCount) continue;
        r.mode = f[3] == "daily" ? GameMode::Daily : GameMode::Classic;
        r.difficulty = Difficulty(diff);
        r.result = f[6] == "won" ? GameResult::Won : f[6] == "lost" ? GameResult::Lost : GameResult::Abandoned;
        r.secondChance = f[10] == "1";
        if (n > 11) r.givens = std::string(f[11]);
        out.push_back(std::move(r));
    }
    return out;
}

Stats computeStats(const std::vector<GameRecord>& records, int today) {
    Stats st;
    for (const GameRecord& r : records) {
        if (r.mode == GameMode::Classic) {
            addRecord(st.level[int(r.difficulty)], r);
            addRecord(st.all, r);
        } else {
            addRecord(st.daily, r);
        }
        st.totalPlayMs += r.timeMs;
        if (r.result != GameResult::Won) continue;
        ++st.totalWins;
        if (r.perfect()) ++st.perfectWins;
        const int lv = int(r.difficulty);
        ++st.winsAnyMode[lv];
        if (r.perfect()) ++st.perfectAnyMode[lv];
        if (!st.bestAnyMode[lv] || r.timeMs < st.bestAnyMode[lv]) st.bestAnyMode[lv] = r.timeMs;
        if (r.localMinute < 5 * 60) st.nightOwl = true;
        if (r.secondChance) st.secondWind = true;
        if (r.difficulty >= Difficulty::Hard && r.hints == 0) st.selfReliant = true;
        if (r.mode == GameMode::Daily && r.dailyDate) {
            DailyDay& day = st.days[r.dailyDate];
            day.solved = true;
            if (r.localDate == r.dailyDate) day.onTime = true;
            if (!day.bestMs || r.timeMs < day.bestMs) day.bestMs = r.timeMs;
        }
    }

    // Daily streaks: consecutive days whose puzzle was solved on the day itself.
    std::vector<int> onTime;
    for (const auto& [date, day] : st.days) {
        if (day.solved) ++st.dailySolved;
        if (day.onTime) onTime.push_back(dateToDays(date));
    }
    int run = 0, prev = -1000000;
    for (int dn : onTime) {
        run = dn == prev + 1 ? run + 1 : 1;
        prev = dn;
        st.dailyBestStreak = std::max(st.dailyBestStreak, run);
    }
    const int t = dateToDays(today);
    auto has = [&](int dn) { return std::binary_search(onTime.begin(), onTime.end(), dn); };
    // A streak is still alive if today's puzzle isn't solved yet but yesterday's was.
    const int from = has(t) ? t : t - 1;
    for (int dn = from; has(dn); --dn) ++st.dailyStreak;

    // Any calendar month with every daily solved.
    std::map<int, int> perMonth;  // yyyymm -> days solved
    for (const auto& [date, day] : st.days)
        if (day.solved) ++perMonth[date / 100];
    for (const auto& [ym, count] : perMonth)
        if (count == daysInMonth(ym / 100, ym % 100)) st.fullMonth = true;
    return st;
}

}  // namespace sudoku
