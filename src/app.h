// The application: owns the models (games, settings, history, achievements, puzzle pool),
// the renderer and UI state; turns window messages into game actions and draws every frame.
#pragma once

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

#include "game/achievements.h"
#include "game/game.h"
#include "game/history.h"
#include "game/pool.h"
#include "game/settings.h"
#include "sound.h"
#include "ui/board_view.h"
#include "ui/renderer.h"
#include "ui/ui.h"

namespace sudoku {

inline constexpr UINT WM_APP_POOL = WM_APP + 1;
inline constexpr wchar_t kWindowClass[] = L"Sudoku.ARM64.Window";

enum class Page : uint8_t { Play, Daily, Stats, Achievements, Settings, Count };

// Region action kinds (ui::Action::kind).
enum Act : uint16_t {
    ActNone = 0,
    ActNav,          // a = page
    ActBoard,        // cell resolved from the pointer
    ActDigit,        // a = digit
    ActUndo,
    ActRedo,
    ActErase,
    ActNotes,
    ActHint,
    ActMore,
    ActPause,
    ActResume,
    ActNewGameMenu,
    ActNewGame,      // a = difficulty
    ActContinue,     // a = GameMode
    ActHintNext,
    ActHintApply,
    ActHintClose,
    ActMenu,         // a = MenuItem
    ActCloseFlyout,
    ActDialog,       // a = button index
    ActBlock,
    ActDailyDay,     // b = date
    ActDailyMonth,   // a = -1 / +1
    ActDailyPlay,    // b = date
    ActSegment,      // a = SegmentGroup, b = index
    ActToggle,       // a = ToggleId
    ActAccent,       // a = AccentChoice
    ActOpenData,
    ActResetStats,
    ActResetAchievements,
    ActShortcuts,
};

enum SegmentGroup : int16_t { SegTheme, SegBackdrop, SegMistakes, SegVolume, SegStatsTab };
enum ToggleId : int16_t {
    TogAnimations,
    TogCheckMistakes,
    TogAutoNotes,
    TogAutoPause,
    TogShowTimer,
    TogHighlightRegion,
    TogHighlightSame,
    TogHighlightConflicts,
    TogHideCompleted,
    TogShowCounts,
    TogSound,
};
enum MenuItem : int16_t { MenuRestart = 10, MenuAutoNotes, MenuClearNotes, MenuCheckBoard };

enum class Dialog : uint8_t {
    None,
    ConfirmNewGame,
    ConfirmDaily,
    ConfirmRestart,
    GameOver,
    Win,
    ResetStats,
    ResetAchievements,
    Help,
    Generating,
};
enum class Flyout : uint8_t { None, NewGame, More };

struct ScreenshotSpec {
    std::wstring page;
    bool dark = false;
    int width = 1060, height = 720;
    float scale = 1.0f;
    std::wstring out;
};

class App {
public:
    // Window mode
    void loadSettingsEarly();
    const Settings& settings() const { return settings_; }
    bool init(HWND hwnd);
    void shutdown();
    // Offscreen screenshot mode (dev aid)
    bool runScreenshot(const ScreenshotSpec& spec);

    void onSize(UINT w, UINT h);
    void onDpiChanged(UINT dpi);
    void onActivate(bool active);
    void onMinimized(bool minimized);
    void onMouseMove(float x, float y);
    void onMouseLeave();
    void onMouseDown(float x, float y);
    void onMouseUp(float x, float y);
    void onMouseWheel(float notches);
    bool onKeyDown(UINT vk, bool shift, bool ctrl, bool alt);
    void onTimer(UINT_PTR id);
    void onPool(WPARAM wp, LPARAM lp);
    void onSystemSettingsChanged();
    void onEndSession();

    bool needsFrame() const;
    void render();
    void invalidate() { dirty_ = true; }

private:
    using Rect = ui::Rect;
    using Action = ui::Action;

    // ------------------------------------------------------------- helpers
    double now() const;
    Game& game() { return current_ == GameMode::Daily ? daily_ : classic_; }
    const Game& game() const { return current_ == GameMode::Daily ? daily_ : classic_; }
    Rules rules() const { return settings_.rules(); }
    bool animationsOn() const { return settings_.animations && systemAnimations_; }
    bool inGame() const { return page_ == Page::Play && game().active; }

    // ---------------------------------------------------------------- flow
    void applyTheme();
    void loadAll();
    void startClassic(Difficulty d, bool confirmed);
    void beginGame(GameMode mode, const Puzzle& p, int date);
    void playDaily(int date, bool confirmed);
    void showGame(GameMode mode);
    void resetPlayUi();
    void recordResult(Game& g, GameResult result);
    void endGame(Game& g);
    void onWin();
    void onLost();
    void restartPuzzle(bool confirmed);
    void pause();
    void resume();
    void navigate(Page p);
    void refreshStats(bool announce);
    void showToast(std::wstring title, std::wstring text, wchar_t icon);
    void startConfetti();

    // --------------------------------------------------------------- input
    void trigger(const Action& a);
    void updateHot();
    void clickCell(int c);
    void pressDigit(int d, bool fromNumpad, bool asNote);
    void eraseSelected();
    void moveSelection(int dr, int dc);
    void applyMove(const MoveResult& m, int cell, Sfx sfx = Sfx::Count);
    void hintAction();
    void applyHint();
    void closeHint();
    void dialogButton(int index);
    void closeDialog();
    void menuItem(int item);
    void openFlyout(Flyout f, const Action& from);
    void segment(int group, int index);
    void toggleSetting(int id);
    bool handleGameKey(UINT vk, bool shift, bool ctrl, bool alt);
    void toggleFullscreen();

    // -------------------------------------------------- timer & persistence
    void syncTimer();
    int64_t elapsedOf(const Game& g) const;
    void scheduleClockTick();
    void scheduleSave();
    void saveNow();
    void saveSettings();

    // ------------------------------------------------------------- drawing
    void drawFrame();
    void drawRail(const Rect& r);
    void drawGame(Rect area);
    void drawTopBar(const Rect& bar);
    void drawSidePanel(Rect panel);
    void drawPortraitControls(Rect area);
    void drawActionButton(const Action& a, const Rect& rc, wchar_t icon, std::wstring_view label, bool toggled,
                          bool enabled, std::wstring_view tip);
    void drawNumpadKey(int d, const Rect& rc, bool compact);
    float drawHintCard(const Rect& rc, bool measureOnly);
    void drawInfoCard(const Rect& rc);
    void drawPausedOverlay();
    float drawPicker(Rect area);
    float drawDaily(Rect area);
    float drawStats(Rect area);
    float drawAchievements(Rect area);
    float drawSettings(Rect area);
    void drawFlyout();
    void drawDialog();
    void drawToasts();
    void drawConfetti();
    float pageHeader(Rect& area, std::wstring_view title, std::wstring_view subtitle = {});
    std::wstring formatTime(int64_t ms) const;

    // -------------------------------------------------------------- state
    HWND hwnd_ = nullptr;
    bool offscreen_ = false;
    UINT dpi_ = 96;
    bool active_ = true, minimized_ = false, micaActive_ = false, systemAnimations_ = true;
    bool fullscreen_ = false;
    WINDOWPLACEMENT savedPlacement_{};
    LARGE_INTEGER qpcFreq_{}, qpcStart_{};
    double fixedTime_ = -1;  // screenshot mode clock

    ui::Renderer renderer_;
    ui::Ui ui_;
    ui::BoardView boardView_;
    ui::BoardGeom boardGeom_;
    ui::BoardAnims anims_;
    bool dirty_ = true;

    Settings settings_;
    Game classic_, daily_;
    GameMode current_ = GameMode::Classic;
    std::vector<GameRecord> history_;
    Stats stats_;
    AchievementState achievements_;
    PuzzlePool pool_;
    SoundPlayer sound_;
    int today_ = 0;

    Page page_ = Page::Play;
    double pageChangedAt_ = -1;
    int selected_ = -1, digitLock_ = 0;
    bool notesMode_ = false, paused_ = false;
    Hint hint_;
    bool hintActive_ = false;
    int hintStage_ = 0;
    double checkFlashUntil_ = 0;

    bool timerRunning_ = false;
    Game* timerGame_ = nullptr;
    double timerStart_ = 0;

    Dialog dialog_ = Dialog::None;
    double dialogAt_ = 0;
    Flyout flyout_ = Flyout::None;
    Rect flyoutAnchor_;
    Difficulty pendingDifficulty_ = Difficulty::Easy;
    int pendingDaily_ = 0;
    int generatingDaily_ = 0;

    struct WinInfo {
        GameMode mode = GameMode::Classic;
        Difficulty difficulty = Difficulty::Easy;
        int64_t timeMs = 0, bestMs = 0, averageMs = 0;
        int mistakes = 0, hints = 0, streak = 0, dailyDate = 0;
        bool newBest = false, perfect = false;
    } win_;

    struct Toast {
        std::wstring title, text;
        wchar_t icon;
        double start;
    };
    std::deque<Toast> toasts_;

    struct Particle {
        float x, y, vx, vy, rot, vr, w, h;
        ui::Color color;
    };
    std::vector<Particle> confetti_;
    double confettiStart_ = -1;

    int dailyMonth_ = 0;     // yyyymm shown in the calendar
    int dailySelected_ = 0;  // yyyymmdd
    int statsTab_ = 0;       // 0 = all, 1..6 = difficulty, 7 = daily

    float scroll_[int(Page::Count)]{};
    float contentHeight_[int(Page::Count)]{};
    Rect viewport_;
    bool saveDirty_ = false;
};

}  // namespace sudoku
