#include "ui/theme.h"

#include <dwmapi.h>

namespace sudoku::ui {
namespace {

AccentRamp rampFrom(Color base) {
    const Color white = rgb(0xFFFFFF), black = rgb(0x000000);
    return {mix(base, white, 0.70f), mix(base, white, 0.45f), mix(base, white, 0.22f), base,
            mix(base, black, 0.14f), mix(base, black, 0.45f), mix(base, black, 0.70f)};
}

DWORD readDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD fallback) {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) return value;
    return fallback;
}

}  // namespace

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

Color over(Color top, Color bottom) {
    const float a = top.a;
    return {top.r * a + bottom.r * (1 - a), top.g * a + bottom.g * (1 - a), top.b * a + bottom.b * (1 - a), 1.0f};
}

bool systemUsesDarkTheme() {
    return readDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", 1) == 0;
}

bool systemAnimationsEnabled() {
    BOOL on = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
    return on != FALSE;
}

AccentRamp windowsAccent() {
    // AccentPalette: 8 RGBA entries - light3, light2, light1, accent, dark1, dark2, dark3, (unused).
    BYTE data[32];
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, nullptr, data, &size) == ERROR_SUCCESS &&
        size >= 28) {
        auto at = [&](int i) { return rgb((DWORD(data[i * 4]) << 16) | (DWORD(data[i * 4 + 1]) << 8) | data[i * 4 + 2]); };
        return {at(0), at(1), at(2), at(3), at(4), at(5), at(6)};
    }
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) return rampFrom(rgb(color & 0xFFFFFF));
    return presetAccent(AccentChoice::Blue);
}

AccentRamp presetAccent(AccentChoice c) {
    switch (c) {
    case AccentChoice::Teal: return rampFrom(rgb(0x00838F));
    case AccentChoice::Green: return rampFrom(rgb(0x107C10));
    case AccentChoice::Purple: return rampFrom(rgb(0x744DA9));
    case AccentChoice::Orange: return rampFrom(rgb(0xCA5010));
    case AccentChoice::Rose: return rampFrom(rgb(0xC2185B));
    default:  // Windows default blue
        return {rgb(0x99EBFF), rgb(0x4CC2FF), rgb(0x0091F8), rgb(0x0078D4), rgb(0x0067C0), rgb(0x003E92), rgb(0x001A68)};
    }
}

AccentRamp accentFor(AccentChoice c) { return c == AccentChoice::Windows ? windowsAccent() : presetAccent(c); }

Palette makePalette(bool dark, const AccentRamp& a) {
    Palette p;
    p.dark = dark;
    if (!dark) {
        p.backdrop = rgb(0xF3F3F3);
        p.layer = rgb(0xFFFFFF, 0.50f);
        p.layerStroke = rgb(0x000000, 0.06f);
        p.card = rgb(0xFFFFFF, 0.70f);
        p.cardHover = rgb(0xF9F9F9, 0.80f);
        p.cardPressed = rgb(0xF9F9F9, 0.45f);
        p.cardStroke = rgb(0x000000, 0.07f);
        p.surface = rgb(0xFFFFFF);
        p.surfaceFooter = rgb(0xF3F3F3);
        p.surfaceStroke = rgb(0x000000, 0.10f);
        p.text = rgb(0x000000, 0.894f);
        p.text2 = rgb(0x000000, 0.62f);
        p.text3 = rgb(0x000000, 0.447f);
        p.textDisabled = rgb(0x000000, 0.36f);
        p.control = rgb(0xFFFFFF, 0.70f);
        p.controlHover = rgb(0xF9F9F9, 0.50f);
        p.controlPressed = rgb(0xF9F9F9, 0.30f);
        p.controlDisabled = rgb(0xF9F9F9, 0.30f);
        p.controlStroke = rgb(0x000000, 0.0578f);
        p.controlStrokeBottom = rgb(0x000000, 0.1622f);
        p.subtleHover = rgb(0x000000, 0.0373f);
        p.subtlePressed = rgb(0x000000, 0.0241f);
        p.accent = a.dark1;
        p.accentHover = withAlpha(a.dark1, 0.90f);
        p.accentPressed = withAlpha(a.dark1, 0.80f);
        p.accentDisabled = rgb(0x000000, 0.2169f);
        p.onAccent = rgb(0xFFFFFF);
        p.accentText = a.dark2;
        p.accentSoft = withAlpha(a.base, 0.12f);
        p.divider = rgb(0x000000, 0.0803f);
        p.smoke = rgb(0x000000, 0.30f);
        p.focusOuter = rgb(0x000000, 0.894f);
        p.focusInner = rgb(0xFFFFFF);
        p.success = rgb(0x0F7B0F);
        p.caution = rgb(0x9D5D00);
        p.critical = rgb(0xC42B1C);
        p.criticalBg = rgb(0xFDE7E9);
        p.cautionBg = rgb(0xFFF4CE);
        p.successBg = rgb(0xDFF6DD);
        p.board = rgb(0xFFFFFF);
        p.boardLine = rgb(0xDCDCDC);
        p.boardBox = rgb(0x5C5C5C);
        p.given = rgb(0x1A1A1A);
        p.entry = a.dark1;
        p.wrong = rgb(0xC42B1C);
        p.note = rgb(0x6B6B6B);
        p.cellSelected = mix(p.board, a.light1, 0.36f);
        p.cellPeer = mix(p.board, a.base, 0.065f);
        p.cellSame = mix(p.board, a.light1, 0.22f);
        p.cellConflict = rgb(0xFDE7E9);
        p.cellHint = rgb(0xFFF1C2);
        p.cellHint2 = rgb(0xE3F4E1);
        p.unitHint = mix(p.board, rgb(0xFFD95A), 0.12f);
    } else {
        p.backdrop = rgb(0x202020);
        p.layer = rgb(0x3A3A3A, 0.30f);
        p.layerStroke = rgb(0xFFFFFF, 0.05f);
        p.card = rgb(0xFFFFFF, 0.051f);
        p.cardHover = rgb(0xFFFFFF, 0.084f);
        p.cardPressed = rgb(0xFFFFFF, 0.033f);
        p.cardStroke = rgb(0x000000, 0.10f);
        p.surface = rgb(0x2B2B2B);
        p.surfaceFooter = rgb(0x202020);
        p.surfaceStroke = rgb(0xFFFFFF, 0.08f);
        p.text = rgb(0xFFFFFF);
        p.text2 = rgb(0xFFFFFF, 0.786f);
        p.text3 = rgb(0xFFFFFF, 0.544f);
        p.textDisabled = rgb(0xFFFFFF, 0.363f);
        p.control = rgb(0xFFFFFF, 0.061f);
        p.controlHover = rgb(0xFFFFFF, 0.084f);
        p.controlPressed = rgb(0xFFFFFF, 0.033f);
        p.controlDisabled = rgb(0xFFFFFF, 0.042f);
        p.controlStroke = rgb(0xFFFFFF, 0.07f);
        p.controlStrokeBottom = rgb(0xFFFFFF, 0.093f);
        p.subtleHover = rgb(0xFFFFFF, 0.061f);
        p.subtlePressed = rgb(0xFFFFFF, 0.042f);
        p.accent = a.light2;
        p.accentHover = withAlpha(a.light2, 0.90f);
        p.accentPressed = withAlpha(a.light2, 0.80f);
        p.accentDisabled = rgb(0xFFFFFF, 0.1581f);
        p.onAccent = rgb(0x000000);
        p.accentText = a.light3;
        p.accentSoft = withAlpha(a.light2, 0.16f);
        p.divider = rgb(0xFFFFFF, 0.0837f);
        p.smoke = rgb(0x000000, 0.30f);
        p.focusOuter = rgb(0xFFFFFF);
        p.focusInner = rgb(0x000000, 0.70f);
        p.success = rgb(0x6CCB5F);
        p.caution = rgb(0xFCE100);
        p.critical = rgb(0xFF99A4);
        p.criticalBg = rgb(0x442726);
        p.cautionBg = rgb(0x433519);
        p.successBg = rgb(0x393D1B);
        p.board = rgb(0x292929);
        p.boardLine = rgb(0x3E3E3E);
        p.boardBox = rgb(0x8F8F8F);
        p.given = rgb(0xF2F2F2);
        p.entry = a.light2;
        p.wrong = rgb(0xFF99A4);
        p.note = rgb(0xA6A6A6);
        p.cellSelected = mix(p.board, a.light1, 0.36f);
        p.cellPeer = mix(p.board, a.light2, 0.07f);
        p.cellSame = mix(p.board, a.light1, 0.22f);
        p.cellConflict = rgb(0x4A2A2B);
        p.cellHint = rgb(0x4A3F1F);
        p.cellHint2 = rgb(0x2F4A2C);
        p.unitHint = mix(p.board, rgb(0xFCE100), 0.07f);
    }
    return p;
}

bool applyWindowFrame(HWND hwnd, const Palette& p, Backdrop backdrop) {
    const BOOL dark = p.dark;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    int type = backdrop == Backdrop::Mica ? DWMSBT_MAINWINDOW
             : backdrop == Backdrop::MicaAlt ? DWMSBT_TABBEDWINDOW
                                             : DWMSBT_NONE;
    const bool mica = type != DWMSBT_NONE &&
                      SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type)));
    if (!mica) {
        type = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    }
    // Solid: paint the caption with the app background so the title bar blends into the window.
    COLORREF caption = DWMWA_COLOR_DEFAULT;
    if (!mica) {
        caption = RGB(BYTE(p.backdrop.r * 255 + 0.5f), BYTE(p.backdrop.g * 255 + 0.5f), BYTE(p.backdrop.b * 255 + 0.5f));
    }
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    const MARGINS margins = mica ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    return mica;
}

}  // namespace sudoku::ui
