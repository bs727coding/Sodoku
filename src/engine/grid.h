// Core Sudoku types: grids, candidate masks, cell sets and precomputed unit/peer tables.
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace sudoku {

using Mask = uint16_t;                 // bit (d-1) set => digit d
using Grid = std::array<uint8_t, 81>;  // 0 = empty, 1..9 = digit

inline constexpr Mask kAllDigits = 0x1FF;

constexpr int rowOf(int c) { return c / 9; }
constexpr int colOf(int c) { return c % 9; }
constexpr int boxOf(int c) { return (c / 27) * 3 + (c % 9) / 3; }
constexpr int cellAt(int r, int c) { return r * 9 + c; }
constexpr Mask digitBit(int d) { return Mask(1u << (d - 1)); }
constexpr int bitCount(unsigned m) { return std::popcount(m); }
constexpr int lowestDigit(Mask m) { return std::countr_zero(unsigned(m)) + 1; }

// Unit indices: 0-8 rows, 9-17 columns, 18-26 boxes.
constexpr int rowUnit(int r) { return r; }
constexpr int colUnit(int c) { return 9 + c; }
constexpr int boxUnit(int b) { return 18 + b; }
constexpr bool isRowUnit(int u) { return u < 9; }
constexpr bool isColUnit(int u) { return u >= 9 && u < 18; }
constexpr bool isBoxUnit(int u) { return u >= 18; }

// 81-bit set of cells.
struct CellSet {
    uint64_t lo = 0;  // cells 0..63
    uint64_t hi = 0;  // cells 64..80

    static constexpr uint64_t kHiMask = (1ull << 17) - 1;

    constexpr void set(int c) {
        if (c < 64) lo |= 1ull << c;
        else hi |= 1ull << (c - 64);
    }
    constexpr void reset(int c) {
        if (c < 64) lo &= ~(1ull << c);
        else hi &= ~(1ull << (c - 64));
    }
    constexpr bool has(int c) const { return c < 64 ? (lo >> c) & 1 : (hi >> (c - 64)) & 1; }
    constexpr bool empty() const { return (lo | hi) == 0; }
    constexpr int count() const { return std::popcount(lo) + std::popcount(hi); }
    constexpr int first() const { return lo ? std::countr_zero(lo) : 64 + std::countr_zero(hi); }

    constexpr CellSet operator&(const CellSet& o) const { return {lo & o.lo, hi & o.hi}; }
    constexpr CellSet operator|(const CellSet& o) const { return {lo | o.lo, hi | o.hi}; }
    constexpr CellSet operator-(const CellSet& o) const { return {lo & ~o.lo, hi & ~o.hi}; }
    constexpr CellSet& operator&=(const CellSet& o) { lo &= o.lo; hi &= o.hi; return *this; }
    constexpr CellSet& operator|=(const CellSet& o) { lo |= o.lo; hi |= o.hi; return *this; }
    constexpr CellSet& operator-=(const CellSet& o) { lo &= ~o.lo; hi &= ~o.hi; return *this; }
    constexpr bool operator==(const CellSet&) const = default;

    template <class F>
    constexpr void forEach(F&& f) const {
        for (uint64_t m = lo; m; m &= m - 1) f(std::countr_zero(m));
        for (uint64_t m = hi; m; m &= m - 1) f(64 + std::countr_zero(m));
    }

    static constexpr CellSet of(int c) { CellSet s; s.set(c); return s; }
};

struct Tables {
    uint8_t unit[27][9]{};     // cells of each unit
    uint8_t peers[81][20]{};   // the 20 peers of each cell
    uint8_t unitsOf[81][3]{};  // row, column and box unit of each cell
    CellSet unitSet[27]{};
    CellSet peerSet[81]{};
};

constexpr Tables makeTables() {
    Tables t{};
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            t.unit[rowUnit(i)][j] = uint8_t(i * 9 + j);
            t.unit[colUnit(i)][j] = uint8_t(j * 9 + i);
            t.unit[boxUnit(i)][j] = uint8_t((i / 3) * 27 + (i % 3) * 3 + (j / 3) * 9 + (j % 3));
        }
    }
    for (int u = 0; u < 27; ++u)
        for (int j = 0; j < 9; ++j) t.unitSet[u].set(t.unit[u][j]);
    for (int c = 0; c < 81; ++c) {
        t.unitsOf[c][0] = uint8_t(rowUnit(rowOf(c)));
        t.unitsOf[c][1] = uint8_t(colUnit(colOf(c)));
        t.unitsOf[c][2] = uint8_t(boxUnit(boxOf(c)));
        int n = 0;
        for (int o = 0; o < 81; ++o) {
            if (o != c && (rowOf(o) == rowOf(c) || colOf(o) == colOf(c) || boxOf(o) == boxOf(c))) {
                t.peers[c][n++] = uint8_t(o);
                t.peerSet[c].set(o);
            }
        }
    }
    return t;
}

inline constexpr Tables kT = makeTables();

constexpr bool sees(int a, int b) { return kT.peerSet[a].has(b); }

template <class F>
constexpr void forEachDigit(Mask m, F&& f) {
    for (; m; m &= m - 1) f(lowestDigit(m));
}

// Text helpers (ASCII/UTF-8).
std::string toString(const Grid& g, char empty = '0');
bool fromString(std::string_view s, Grid& g);  // exactly 81 chars of 0-9 or '.'
bool isConsistent(const Grid& g);              // no duplicate digits in any unit
bool isCompleteSolution(const Grid& g);        // filled and consistent
int countFilled(const Grid& g);
std::string cellName(int c);                   // "R3C5"
std::string unitName(int u);                   // "row 3", "column 5", "box 5"
std::string digitList(Mask m);                 // "3, 5 and 7"

}  // namespace sudoku
