// User preferences, persisted as a simple key=value file.
#pragma once

#include <string>
#include <string_view>

#include "game.h"

namespace sudoku {

enum class ThemeMode : uint8_t { System, Light, Dark };
enum class Backdrop : uint8_t { Mica, MicaAlt, Solid };
enum class AccentChoice : uint8_t { Windows, Blue, Teal, Green, Purple, Orange, Rose, Count };

struct WindowRect {
    bool valid = false;
    int x = 0, y = 0, w = 0, h = 0;
    bool maximized = false;
};

struct Settings {
    // Appearance
    ThemeMode theme = ThemeMode::System;
    AccentChoice accent = AccentChoice::Windows;
    Backdrop backdrop = Backdrop::Mica;
    bool animations = true;
    // Gameplay
    int mistakeLimit = 3;  // 0 (off), 3 or 5
    bool checkMistakes = true;
    bool autoRemoveNotes = true;
    bool autoPause = true;
    bool showTimer = true;
    // Assists
    bool highlightRegion = true;
    bool highlightSame = true;
    bool highlightConflicts = true;
    bool hideCompleted = true;
    bool showCounts = true;
    // Sound
    bool sound = false;
    int volume = 2;  // 1..3
    // Remembered state
    Difficulty lastDifficulty = Difficulty::Easy;
    WindowRect window;

    Rules rules() const { return {checkMistakes, mistakeLimit, autoRemoveNotes}; }
    std::string serialize() const;
    void parse(std::string_view text);
};

}  // namespace sudoku
