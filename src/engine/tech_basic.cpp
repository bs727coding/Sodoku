// Singles, locked candidates (intersections) and naked/hidden subsets.
#include "techniques.h"

namespace sudoku::detail {

Analysis::Analysis(const LogicGrid& g) {
    for (int u = 0; u < 27; ++u) {
        for (int i = 0; i < 9; ++i) {
            const int c = kT.unit[u][i];
            if (g.val[c]) {
                placed[u] |= digitBit(g.val[c]);
                continue;
            }
            for (Mask m = g.cand[c]; m; m &= m - 1) pos[u][lowestDigit(m)] |= Mask(1u << i);
        }
    }
    for (int c = 0; c < 81; ++c) {
        if (g.val[c]) continue;
        emptyCells.set(c);
        forEachDigit(g.cand[c], [&](int d) { digitCells[d].set(c); });
    }
}

// ---------------------------------------------------------------- singles

bool findFullHouse(const LogicGrid& g, const Analysis& a, Step& s) {
    for (int u : kBoxesFirst) {
        int empties = 0, cell = -1;
        for (int c : kT.unit[u]) {
            if (!g.val[c]) {
                ++empties;
                cell = c;
            }
        }
        if (empties != 1) continue;
        const Mask missing = Mask(kAllDigits & ~a.placed[u]);
        if (bitCount(missing) != 1 || !(g.cand[cell] & missing)) continue;
        s = Step{};
        s.tech = Tech::FullHouse;
        s.placeCell = int8_t(cell);
        s.placeDigit = uint8_t(lowestDigit(missing));
        s.unit = uint8_t(u);
        s.units = 1u << u;
        s.cells.set(cell);
        s.digits = missing;
        return true;
    }
    return false;
}

bool findHiddenSingle(const LogicGrid& g, const Analysis& a, Step& s) {
    for (int u : kBoxesFirst) {
        for (int d = 1; d <= 9; ++d) {
            const Mask m = a.pos[u][d];
            if (bitCount(m) != 1) continue;
            const int cell = kT.unit[u][std::countr_zero(unsigned(m))];
            s = Step{};
            s.tech = Tech::HiddenSingle;
            s.placeCell = int8_t(cell);
            s.placeDigit = uint8_t(d);
            s.unit = uint8_t(u);
            s.units = 1u << u;
            s.cells.set(cell);
            s.digits = digitBit(d);
            // The placed copies of d that block the other empty cells of this unit.
            for (int c : kT.unit[u]) {
                if (g.val[c] || c == cell) continue;
                for (int p : kT.peers[c]) {
                    if (g.val[p] == d && !kT.unitSet[u].has(p)) {
                        s.cells2.set(p);
                        break;
                    }
                }
            }
            return true;
        }
    }
    return false;
}

bool findNakedSingle(const LogicGrid& g, const Analysis&, Step& s) {
    for (int c = 0; c < 81; ++c) {
        if (g.val[c] || bitCount(g.cand[c]) != 1) continue;
        s = Step{};
        s.tech = Tech::NakedSingle;
        s.placeCell = int8_t(c);
        s.placeDigit = uint8_t(lowestDigit(g.cand[c]));
        s.cells.set(c);
        s.digits = g.cand[c];
        s.unit = uint8_t(boxUnit(boxOf(c)));
        for (int u : kT.unitsOf[c]) s.units |= 1u << u;
        return true;
    }
    return false;
}

// ---------------------------------------------------------- intersections

bool findPointing(const LogicGrid&, const Analysis& a, Step& s) {
    for (int b = 0; b < 9; ++b) {
        const int u = boxUnit(b);
        for (int d = 1; d <= 9; ++d) {
            const Mask m = a.pos[u][d];
            if (bitCount(m) < 2) continue;
            int line = -1;
            if (!(m & ~0x007)) line = rowUnit((b / 3) * 3 + 0);
            else if (!(m & ~0x038)) line = rowUnit((b / 3) * 3 + 1);
            else if (!(m & ~0x1C0)) line = rowUnit((b / 3) * 3 + 2);
            else if (!(m & ~0x049)) line = colUnit((b % 3) * 3 + 0);
            else if (!(m & ~0x092)) line = colUnit((b % 3) * 3 + 1);
            else if (!(m & ~0x124)) line = colUnit((b % 3) * 3 + 2);
            if (line < 0) continue;
            const CellSet targets = (kT.unitSet[line] - kT.unitSet[u]) & a.digitCells[d];
            if (targets.empty()) continue;
            s = Step{};
            s.tech = Tech::Pointing;
            eliminate(s, targets, d);
            s.unit = uint8_t(u);
            s.unit2 = uint8_t(line);
            s.units = (1u << u) | (1u << line);
            s.digits = digitBit(d);
            s.cells = cellsOf(u, m);
            return true;
        }
    }
    return false;
}

bool findClaiming(const LogicGrid& g, const Analysis& a, Step& s) {
    (void)g;
    for (int u = 0; u < 18; ++u) {
        for (int d = 1; d <= 9; ++d) {
            const Mask m = a.pos[u][d];
            if (bitCount(m) < 2) continue;
            int seg = -1;
            if (!(m & ~0x007)) seg = 0;
            else if (!(m & ~0x038)) seg = 1;
            else if (!(m & ~0x1C0)) seg = 2;
            if (seg < 0) continue;
            const int b = isRowUnit(u) ? (u / 3) * 3 + seg : seg * 3 + (u - 9) / 3;
            const int bu = boxUnit(b);
            const CellSet targets = (kT.unitSet[bu] - kT.unitSet[u]) & a.digitCells[d];
            if (targets.empty()) continue;
            s = Step{};
            s.tech = Tech::Claiming;
            eliminate(s, targets, d);
            s.unit = uint8_t(u);
            s.unit2 = uint8_t(bu);
            s.units = (1u << u) | (1u << bu);
            s.digits = digitBit(d);
            s.cells = cellsOf(u, m);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- subsets

namespace {

bool nakedSubset(const LogicGrid& g, int k, Tech tech, Step& s) {
    for (int u : kLinesFirst) {
        int cells[9], n = 0, empties = 0;
        for (int c : kT.unit[u]) {
            if (g.val[c]) continue;
            ++empties;
            const int cnt = bitCount(g.cand[c]);
            if (cnt >= 2 && cnt <= k) cells[n++] = c;
        }
        if (n < k || empties <= k) continue;
        int idx[4];
        auto rec = [&](auto& self, int start, int depth, Mask uni) -> bool {
            if (depth == k) {
                if (bitCount(uni) != k) return false;
                CellSet chosen;
                for (int i = 0; i < k; ++i) chosen.set(cells[idx[i]]);
                Step t{};
                bool any = false;
                for (int c : kT.unit[u]) {
                    if (g.val[c] || chosen.has(c)) continue;
                    const Mask e = g.cand[c] & uni;
                    if (e) {
                        t.elim[c] = e;
                        any = true;
                    }
                }
                if (!any) return false;
                t.tech = tech;
                t.unit = uint8_t(u);
                t.units = 1u << u;
                t.digits = uni;
                t.cells = chosen;
                s = std::move(t);
                return true;
            }
            for (int i = start; i < n; ++i) {
                const Mask nu = uni | g.cand[cells[i]];
                if (bitCount(nu) > k) continue;
                idx[depth] = i;
                if (self(self, i + 1, depth + 1, nu)) return true;
            }
            return false;
        };
        if (rec(rec, 0, 0, 0)) return true;
    }
    return false;
}

bool hiddenSubset(const LogicGrid& g, const Analysis& a, int k, Tech tech, Step& s) {
    for (int u : kLinesFirst) {
        int empties = 0;
        for (int c : kT.unit[u]) empties += g.val[c] == 0;
        if (empties <= k) continue;
        int digits[9], n = 0;
        for (int d = 1; d <= 9; ++d) {
            const int cnt = bitCount(a.pos[u][d]);
            if (cnt >= 2 && cnt <= k) digits[n++] = d;
        }
        if (n < k) continue;
        int idx[4];
        auto rec = [&](auto& self, int start, int depth, Mask uni) -> bool {
            if (depth == k) {
                if (bitCount(uni) != k) return false;
                Mask dm = 0;
                for (int i = 0; i < k; ++i) dm |= digitBit(digits[idx[i]]);
                Step t{};
                bool any = false;
                for (Mask m = uni; m; m &= m - 1) {
                    const int c = kT.unit[u][std::countr_zero(unsigned(m))];
                    const Mask extra = g.cand[c] & Mask(~dm);
                    if (extra) {
                        t.elim[c] = extra;
                        any = true;
                    }
                }
                if (!any) return false;
                t.tech = tech;
                t.unit = uint8_t(u);
                t.units = 1u << u;
                t.digits = dm;
                t.cells = cellsOf(u, uni);
                s = std::move(t);
                return true;
            }
            for (int i = start; i < n; ++i) {
                const Mask nu = uni | a.pos[u][digits[i]];
                if (bitCount(nu) > k) continue;
                idx[depth] = i;
                if (self(self, i + 1, depth + 1, nu)) return true;
            }
            return false;
        };
        if (rec(rec, 0, 0, 0)) return true;
    }
    return false;
}

}  // namespace

bool findNakedPair(const LogicGrid& g, const Analysis&, Step& s) { return nakedSubset(g, 2, Tech::NakedPair, s); }
bool findNakedTriple(const LogicGrid& g, const Analysis&, Step& s) { return nakedSubset(g, 3, Tech::NakedTriple, s); }
bool findNakedQuad(const LogicGrid& g, const Analysis&, Step& s) { return nakedSubset(g, 4, Tech::NakedQuad, s); }
bool findHiddenPair(const LogicGrid& g, const Analysis& a, Step& s) { return hiddenSubset(g, a, 2, Tech::HiddenPair, s); }
bool findHiddenTriple(const LogicGrid& g, const Analysis& a, Step& s) { return hiddenSubset(g, a, 3, Tech::HiddenTriple, s); }
bool findHiddenQuad(const LogicGrid& g, const Analysis& a, Step& s) { return hiddenSubset(g, a, 4, Tech::HiddenQuad, s); }

}  // namespace sudoku::detail
