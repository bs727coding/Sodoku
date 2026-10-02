// Draws the 9x9 board: highlights, digits, notes, hint overlays and cell animations.
#pragma once

#include <array>

#include "game/game.h"
#include "game/settings.h"
#include "ui/ui.h"

namespace sudoku::ui {

// Integer-pixel board geometry so every grid line lands exactly on device pixels.
struct BoardGeom {
    float x = 0, y = 0;
    int size = 0, cell = 0, thin = 1, thick = 2;
    int pos[10]{};  // offset of each row/column's first pixel; pos[9] == size

    Rect bounds() const { return {x, y, float(size), float(size)}; }
    Rect cellRect(int c) const { return {x + float(pos[colOf(c)]), y + float(pos[rowOf(c)]), float(cell), float(cell)}; }
    int hit(float px, float py) const;
    D2D1_POINT_2F candidateCenter(int c, int d) const;
};

BoardGeom layoutBoard(const Rect& area, float scale);

// Animation start times in seconds (< 0 = none). Owned by the app.
struct BoardAnims {
    std::array<double, 81> pop{}, shake{}, wash{};
    double win = -1;
    int winOrigin = 40;
    BoardAnims() { clear(); }
    void clear() {
        pop.fill(-1);
        shake.fill(-1);
        wash.fill(-1);
        win = -1;
    }
    bool active(double now) const;
};

struct BoardState {
    const Game* game = nullptr;
    const Settings* settings = nullptr;
    int selected = -1;
    int digitLock = 0;
    bool paused = false;
    bool revealWrong = false;  // "check board" flash
    const Hint* hint = nullptr;
    int hintStage = 0;
    const BoardAnims* anims = nullptr;
};

class BoardView {
public:
    void draw(Ui& ui, const BoardGeom& g, const BoardState& st);

private:
    void ensureLayouts(Renderer& r, int cell, float scale);
    void drawDigit(Renderer& r, IDWriteTextLayout* layout, float x, float y, Color c);

    ComPtr<IDWriteTextLayout> big_[2][10];   // [0] given, [1] entry
    ComPtr<IDWriteTextLayout> note_[2][10];  // [0] normal, [1] emphasized
    int layoutCell_ = 0;
};

}  // namespace sudoku::ui
