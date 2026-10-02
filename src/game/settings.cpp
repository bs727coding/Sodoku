#include "settings.h"

#include <algorithm>
#include <charconv>

namespace sudoku {
namespace {

int toInt(std::string_view v, int fallback) {
    int out = 0;
    if (std::from_chars(v.data(), v.data() + v.size(), out).ec != std::errc{}) return fallback;
    return out;
}

void put(std::string& out, const char* key, int value) {
    out += key;
    out += '=';
    out += std::to_string(value);
    out += '\n';
}

}  // namespace

std::string Settings::serialize() const {
    std::string out = "# sudoku settings v1\n";
    put(out, "theme", int(theme));
    put(out, "accent", int(accent));
    put(out, "backdrop", int(backdrop));
    put(out, "animations", animations);
    put(out, "mistake_limit", mistakeLimit);
    put(out, "check_mistakes", checkMistakes);
    put(out, "auto_remove_notes", autoRemoveNotes);
    put(out, "auto_pause", autoPause);
    put(out, "show_timer", showTimer);
    put(out, "highlight_region", highlightRegion);
    put(out, "highlight_same", highlightSame);
    put(out, "highlight_conflicts", highlightConflicts);
    put(out, "hide_completed", hideCompleted);
    put(out, "show_counts", showCounts);
    put(out, "sound", sound);
    put(out, "volume", volume);
    put(out, "last_difficulty", int(lastDifficulty));
    if (window.valid) {
        out += "window=" + std::to_string(window.x) + "," + std::to_string(window.y) + "," + std::to_string(window.w) +
               "," + std::to_string(window.h) + "," + (window.maximized ? "1" : "0") + "\n";
    }
    return out;
}

void Settings::parse(std::string_view text) {
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
        const std::string_view k = line.substr(0, eq), v = line.substr(eq + 1);
        const auto flag = [&](bool& target) { target = toInt(v, target ? 1 : 0) != 0; };
        if (k == "theme") theme = ThemeMode(std::clamp(toInt(v, 0), 0, 2));
        else if (k == "accent") accent = AccentChoice(std::clamp(toInt(v, 0), 0, int(AccentChoice::Count) - 1));
        else if (k == "backdrop") backdrop = Backdrop(std::clamp(toInt(v, 0), 0, 2));
        else if (k == "animations") flag(animations);
        else if (k == "mistake_limit") {
            const int m = toInt(v, 3);
            mistakeLimit = (m == 0 || m == 3 || m == 5) ? m : 3;
        } else if (k == "check_mistakes") flag(checkMistakes);
        else if (k == "auto_remove_notes") flag(autoRemoveNotes);
        else if (k == "auto_pause") flag(autoPause);
        else if (k == "show_timer") flag(showTimer);
        else if (k == "highlight_region") flag(highlightRegion);
        else if (k == "highlight_same") flag(highlightSame);
        else if (k == "highlight_conflicts") flag(highlightConflicts);
        else if (k == "hide_completed") flag(hideCompleted);
        else if (k == "show_counts") flag(showCounts);
        else if (k == "sound") flag(sound);
        else if (k == "volume") volume = std::clamp(toInt(v, 2), 1, 3);
        else if (k == "last_difficulty") lastDifficulty = Difficulty(std::clamp(toInt(v, 0), 0, kDifficultyCount - 1));
        else if (k == "window") {
            int vals[5]{};
            size_t p = 0;
            int n = 0;
            while (n < 5 && p <= v.size()) {
                size_t c = v.find(',', p);
                if (c == std::string_view::npos) c = v.size();
                vals[n++] = toInt(v.substr(p, c - p), 0);
                p = c + 1;
            }
            if (n == 5 && vals[2] > 100 && vals[3] > 100) {
                window = {true, vals[0], vals[1], vals[2], vals[3], vals[4] != 0};
            }
        }
    }
}

}  // namespace sudoku
