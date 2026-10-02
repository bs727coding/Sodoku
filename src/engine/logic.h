// Human-style solving techniques. Used to grade puzzle difficulty and to produce
// step-by-step teaching hints with explanations.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "grid.h"

namespace sudoku {

enum class Tech : uint8_t {
    // Tier 1 - singles
    FullHouse,
    HiddenSingle,
    NakedSingle,
    // Tier 2 - intersections
    Pointing,
    Claiming,
    // Tier 3 - subsets
    NakedPair,
    HiddenPair,
    NakedTriple,
    HiddenTriple,
    // Tier 4 - expert patterns
    XWing,
    Skyscraper,
    TwoStringKite,
    XYWing,
    XYZWing,
    WWing,
    Swordfish,
    SimpleColoring,
    UniqueRectangle,
    NakedQuad,
    HiddenQuad,
    // Tier 5 - master patterns
    BUG,
    FinnedXWing,
    FinnedSwordfish,
    Jellyfish,
    XYChain,
    XChain,
    // Tier 6 - forcing
    ForcingChain,
    Count
};

inline constexpr int kTechCount = int(Tech::Count);
inline constexpr int kMaxTier = 6;

int techTier(Tech t);          // 1..6
const char* techName(Tech t);  // "Hidden Single"

struct Node {
    uint8_t cell;
    uint8_t digit;
};

// One deduction: a placement and/or candidate eliminations plus what to highlight.
struct Step {
    Tech tech = Tech::Count;
    int8_t placeCell = -1;
    uint8_t placeDigit = 0;
    std::array<Mask, 81> elim{};  // candidates removed per cell
    CellSet cells;                 // primary pattern cells
    CellSet cells2;                // secondary cells (fins, pincers, second colour...)
    uint32_t units = 0;            // bit u => highlight unit u
    Mask digits = 0;               // pattern digits
    uint8_t unit = 0xFF;           // units referenced by the explanation
    uint8_t unit2 = 0xFF;
    std::vector<Node> chain;       // chain / forcing trace, in order
    int8_t contraCell = -1;        // forcing: cell left without candidates
    uint8_t contraUnit = 0xFF;     // forcing: unit left without a place for contraDigit
    uint8_t contraDigit = 0;

    bool hasElims() const;
    int elimCount() const;  // number of (cell, digit) eliminations
};

struct LogicGrid {
    Grid val{};
    std::array<Mask, 81> cand{};
    int empty = 81;

    static LogicGrid fromGrid(const Grid& g);  // candidates derived from placed digits
    void place(int c, int d);
    void apply(const Step& s);
    bool solved() const { return empty == 0; }
};

// Finds the simplest available step using techniques up to `maxTier`.
bool findStep(const LogicGrid& g, Step& out, int maxTier = kMaxTier);

struct Rating {
    bool solved = false;  // the technique ladder (up to maxTier) solved the puzzle
    int tier = 0;         // hardest tier needed; maxTier + 1 if unsolved
    Tech hardest = Tech::FullHouse;
    int steps = 0;
    std::array<uint16_t, kTechCount> uses{};
};

Rating rate(const Grid& puzzle, int maxTier = kMaxTier);

struct Explanation {
    std::string title;   // technique name
    std::string nudge;   // stage 1: where to look
    std::string detail;  // stage 2: the full reasoning
};

Explanation explain(const Step& s);

}  // namespace sudoku
