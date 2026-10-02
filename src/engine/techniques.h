// Internal: technique finders shared by the logic ladder.
#pragma once

#include "logic.h"

namespace sudoku::detail {

// Per-call snapshot of where each digit can go.
struct Analysis {
    Mask pos[27][10]{};     // pos[u][d]: bit i => cell kT.unit[u][i] has candidate d
    Mask placed[27]{};      // digits already placed in each unit
    CellSet digitCells[10]; // cells holding candidate d
    CellSet emptyCells;

    explicit Analysis(const LogicGrid& g);
};

using Finder = bool (*)(const LogicGrid&, const Analysis&, Step&);

// Singles & intersections & subsets (tech_basic.cpp)
bool findFullHouse(const LogicGrid&, const Analysis&, Step&);
bool findHiddenSingle(const LogicGrid&, const Analysis&, Step&);
bool findNakedSingle(const LogicGrid&, const Analysis&, Step&);
bool findPointing(const LogicGrid&, const Analysis&, Step&);
bool findClaiming(const LogicGrid&, const Analysis&, Step&);
bool findNakedPair(const LogicGrid&, const Analysis&, Step&);
bool findHiddenPair(const LogicGrid&, const Analysis&, Step&);
bool findNakedTriple(const LogicGrid&, const Analysis&, Step&);
bool findHiddenTriple(const LogicGrid&, const Analysis&, Step&);
bool findNakedQuad(const LogicGrid&, const Analysis&, Step&);
bool findHiddenQuad(const LogicGrid&, const Analysis&, Step&);

// Fish (tech_fish.cpp)
bool findXWing(const LogicGrid&, const Analysis&, Step&);
bool findSwordfish(const LogicGrid&, const Analysis&, Step&);
bool findJellyfish(const LogicGrid&, const Analysis&, Step&);
bool findFinnedXWing(const LogicGrid&, const Analysis&, Step&);
bool findFinnedSwordfish(const LogicGrid&, const Analysis&, Step&);

// Single-digit patterns, wings, uniqueness (tech_patterns.cpp)
bool findSkyscraper(const LogicGrid&, const Analysis&, Step&);
bool findTwoStringKite(const LogicGrid&, const Analysis&, Step&);
bool findXYWing(const LogicGrid&, const Analysis&, Step&);
bool findXYZWing(const LogicGrid&, const Analysis&, Step&);
bool findWWing(const LogicGrid&, const Analysis&, Step&);
bool findSimpleColoring(const LogicGrid&, const Analysis&, Step&);
bool findUniqueRectangle(const LogicGrid&, const Analysis&, Step&);
bool findBUG(const LogicGrid&, const Analysis&, Step&);

// Chains (tech_chains.cpp)
bool findXChain(const LogicGrid&, const Analysis&, Step&);
bool findXYChain(const LogicGrid&, const Analysis&, Step&);
bool findForcingChain(const LogicGrid&, const Analysis&, Step&);

// Unit traversal order used by most finders: boxes, then rows, then columns.
inline constexpr int kBoxesFirst[27] = {18, 19, 20, 21, 22, 23, 24, 25, 26, 0,  1,  2,  3, 4,
                                        5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17};
inline constexpr int kLinesFirst[27] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                                        14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};

inline int unitCell(int u, int i) { return kT.unit[u][i]; }

// Cells of unit u selected by a 9-bit position mask.
inline CellSet cellsOf(int u, Mask positions) {
    CellSet s;
    for (Mask m = positions; m; m &= m - 1) s.set(kT.unit[u][std::countr_zero(unsigned(m))]);
    return s;
}

// Adds `d` as an elimination for every cell of `targets`; returns true if any.
inline bool eliminate(Step& s, const CellSet& targets, int d) {
    bool any = false;
    targets.forEach([&](int c) {
        s.elim[c] |= digitBit(d);
        any = true;
    });
    return any;
}

}  // namespace sudoku::detail
