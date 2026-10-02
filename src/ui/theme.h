// Windows 11 (Fluent) colour tokens for light and dark themes, accent ramps, and the DWM
// window-frame attributes (Mica backdrop, dark title bar, caption colour).
#pragma once

#include <windows.h>
#include <d2d1_1.h>

#include "game/settings.h"

namespace sudoku::ui {

using Color = D2D1_COLOR_F;

constexpr Color rgb(uint32_t v, float a = 1.0f) {
    return {float((v >> 16) & 0xFF) / 255.0f, float((v >> 8) & 0xFF) / 255.0f, float(v & 0xFF) / 255.0f, a};
}
constexpr Color withAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }
Color mix(Color a, Color b, float t);  // linear blend, alpha included
Color over(Color top, Color bottom);    // composite top over an opaque bottom

struct AccentRamp {
    Color light3, light2, light1, base, dark1, dark2, dark3;
};

struct Palette {
    bool dark = false;
    // Window & surfaces
    Color backdrop;  // solid background (no Mica, screenshots, title bar caption colour)
    Color layer, layerStroke;
    Color card, cardHover, cardPressed, cardStroke;
    Color surface, surfaceFooter, surfaceStroke;  // dialogs and flyouts
    // Text
    Color text, text2, text3, textDisabled;
    // Controls
    Color control, controlHover, controlPressed, controlDisabled, controlStroke, controlStrokeBottom;
    Color subtleHover, subtlePressed;
    Color accent, accentHover, accentPressed, accentDisabled, onAccent, accentText, accentSoft;
    Color divider, smoke, focusOuter, focusInner;
    Color success, caution, critical, criticalBg, cautionBg, successBg;
    // Board
    Color board, boardLine, boardBox, given, entry, wrong, note;
    Color cellSelected, cellPeer, cellSame, cellConflict, cellHint, cellHint2, unitHint;
};

bool systemUsesDarkTheme();
bool systemAnimationsEnabled();
AccentRamp windowsAccent();
AccentRamp presetAccent(AccentChoice c);
AccentRamp accentFor(AccentChoice c);
Palette makePalette(bool dark, const AccentRamp& ramp);

// Applies DWM attributes: dark mode, backdrop material and (for Solid) the caption colour.
// Returns true if a Mica-style backdrop is active (client area should be transparent).
bool applyWindowFrame(HWND hwnd, const Palette& p, Backdrop backdrop);

}  // namespace sudoku::ui
