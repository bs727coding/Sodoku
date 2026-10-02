// A game session: board state, notes, undo/redo, mistakes, hints and (de)serialization.
// Pure C++ (no Windows headers) so it can be unit tested.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "engine/generator.h"
#include "engine/logic.h"

namespace sudoku {

enum class GameMode : uint8_t { Classic, Daily };
enum class GameStatus : uint8_t { Playing, Won, Lost };

struct Rules {
    bool checkMistakes = true;  // compare each entry with the solution immediately
    int mistakeLimit = 3;       // 0 = unlimited
    bool autoRemoveNotes = true;
};

struct CellChange {
    uint8_t cell;
    uint8_t oldValue, newValue;
    Mask oldNotes, newNotes;
};

// What happened as a result of a player action (drives animations and sounds).
struct MoveResult {
    bool changed = false;
    bool placed = false;
    bool mistake = false;
    bool blocked = false;         // tried to change a given or a locked (correct) cell
    uint32_t completedUnits = 0;  // units completed by this move (bit u)
    Mask completedDigits = 0;     // digits whose nine copies are now all placed
    bool won = false;
    bool lost = false;
    bool fullWithErrors = false;  // board full but not the solution (mistake checking off)
};

enum class HintKind : uint8_t { None, Mistake, BadNotes, Logic, Reveal };

struct Hint {
    HintKind kind = HintKind::None;
    int cell = -1;     // focus cell for Mistake / BadNotes / Reveal
    Step step;         // for Logic
    Explanation text;  // title / nudge / detail (UTF-8)
};

class Game {
public:
    GameMode mode = GameMode::Classic;
    Difficulty difficulty = Difficulty::Easy;
    int dailyDate = 0;  // yyyymmdd for daily games
    Grid givens{}, solution{}, values{};
    std::array<Mask, 81> notes{};
    int64_t elapsedMs = 0;
    int mistakes = 0;
    int hintsUsed = 0;
    int moves = 0;
    int extraLives = 0;  // second chances granted
    bool autoNotesUsed = false;
    GameStatus status = GameStatus::Playing;
    bool active = false;  // a game is loaded
    int64_t startedUtc = 0;

    void start(const Puzzle& p, GameMode mode, int dailyDate, int64_t nowUtc);
    void restart();
    void clear();

    bool isGiven(int c) const { return givens[c] != 0; }
    bool isWrong(int c) const { return values[c] && values[c] != solution[c]; }
    bool isLocked(int c, const Rules& r) const;
    bool isPlaying() const { return active && status == GameStatus::Playing; }
    bool hasProgress() const { return moves > 0; }
    int filledCount() const;
    int placedCount(int d, const Rules& r) const;  // copies of d counting toward completion
    Mask candidatesAt(int c) const;                 // digits not used by the cell's peers
    CellSet conflicts() const;                      // cells duplicating a peer's value
    uint32_t completeUnits(const Rules& r) const;
    Mask completeDigits(const Rules& r) const;

    MoveResult setValue(int c, int d, const Rules& r);
    MoveResult toggleNote(int c, int d);
    MoveResult erase(int c, const Rules& r);
    MoveResult fillAllNotes();
    MoveResult clearAllNotes();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    MoveResult undo();
    MoveResult redo();

    Hint computeHint() const;
    MoveResult applyHint(const Hint& h, const Rules& r);

    void grantSecondChance();
    int mistakeAllowance(const Rules& r) const { return r.mistakeLimit > 0 ? r.mistakeLimit + extraLives : 0; }

    std::string serialize() const;
    bool deserialize(std::string_view text);

private:
    std::vector<std::vector<CellChange>> undo_, redo_;
    void commit(std::vector<CellChange>&& changes);
    void apply(const std::vector<CellChange>& changes, bool forward);
};

}  // namespace sudoku
