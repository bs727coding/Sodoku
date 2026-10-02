// Basic fish (X-Wing, Swordfish, Jellyfish) and finned fish.
//
// Orientation: `rowsBase` => base units are rows and cover units are columns.
// For a base line, a.pos gives the cross index (the column for a row, the row for a column).
// Step::unit records the orientation for the explanation (0 = row-based, 9 = column-based).
#include "techniques.h"

namespace sudoku::detail {
namespace {

bool basicFish(const LogicGrid& g, const Analysis& a, int n, Tech tech, Step& s) {
    for (int d = 1; d <= 9; ++d) {
        for (int orient = 0; orient < 2; ++orient) {
            const int baseOff = orient == 0 ? 0 : 9;
            const int coverOff = orient == 0 ? 9 : 0;
            int lines[9], nl = 0;
            for (int i = 0; i < 9; ++i) {
                const int cnt = bitCount(a.pos[baseOff + i][d]);
                if (cnt >= 2 && cnt <= n) lines[nl++] = i;
            }
            if (nl < n) continue;
            int idx[4];
            auto rec = [&](auto& self, int start, int depth, Mask uni) -> bool {
                if (depth == n) {
                    if (bitCount(uni) != n) return false;
                    unsigned baseLines = 0;
                    for (int i = 0; i < n; ++i) baseLines |= 1u << lines[idx[i]];
                    Step t{};
                    bool any = false;
                    for (Mask m = uni; m; m &= m - 1) {
                        const int cu = coverOff + std::countr_zero(unsigned(m));
                        for (int k = 0; k < 9; ++k) {
                            if (baseLines >> k & 1) continue;
                            const int c = kT.unit[cu][k];
                            if (!g.val[c] && (g.cand[c] & digitBit(d))) {
                                t.elim[c] |= digitBit(d);
                                any = true;
                            }
                        }
                        t.units |= 1u << cu;
                    }
                    if (!any) return false;
                    for (int i = 0; i < n; ++i) {
                        const int bu = baseOff + lines[idx[i]];
                        t.units |= 1u << bu;
                        t.cells |= cellsOf(bu, a.pos[bu][d]);
                    }
                    t.tech = tech;
                    t.digits = digitBit(d);
                    t.unit = uint8_t(baseOff);
                    s = std::move(t);
                    return true;
                }
                for (int i = start; i < nl; ++i) {
                    const Mask nu = uni | a.pos[baseOff + lines[i]][d];
                    if (bitCount(nu) > n) continue;
                    idx[depth] = i;
                    if (self(self, i + 1, depth + 1, nu)) return true;
                }
                return false;
            };
            if (rec(rec, 0, 0, 0)) return true;
        }
    }
    return false;
}

bool finnedFish(const LogicGrid& g, const Analysis& a, int n, Tech tech, Step& s) {
    for (int d = 1; d <= 9; ++d) {
        for (int orient = 0; orient < 2; ++orient) {
            const int baseOff = orient == 0 ? 0 : 9;
            const int coverOff = orient == 0 ? 9 : 0;
            int lines[9], nl = 0;
            for (int i = 0; i < 9; ++i) {
                const int cnt = bitCount(a.pos[baseOff + i][d]);
                if (cnt >= 1 && cnt <= n + 2) lines[nl++] = i;
            }
            if (nl < n) continue;
            int idx[4];
            auto tryCombo = [&](Mask uni) -> bool {
                const int ub = bitCount(uni);
                if (ub <= n || ub > n + 2) return false;
                unsigned baseLines = 0;
                for (int i = 0; i < n; ++i) baseLines |= 1u << lines[idx[i]];
                for (Mask cov = uni; cov; cov = Mask((cov - 1) & uni)) {
                    if (bitCount(cov) != n) continue;
                    CellSet fins, body;
                    bool everyLineCovered = true;
                    for (int i = 0; i < n; ++i) {
                        const int bu = baseOff + lines[idx[i]];
                        const Mask p = a.pos[bu][d];
                        if (!(p & cov)) everyLineCovered = false;
                        fins |= cellsOf(bu, Mask(p & ~cov));
                        body |= cellsOf(bu, Mask(p & cov));
                    }
                    if (!everyLineCovered || fins.empty()) continue;
                    const int finBox = boxOf(fins.first());
                    bool oneBox = true;
                    fins.forEach([&](int c) { oneBox &= boxOf(c) == finBox; });
                    if (!oneBox) continue;
                    Step t{};
                    bool any = false;
                    for (Mask m = cov; m; m &= m - 1) {
                        const int cu = coverOff + std::countr_zero(unsigned(m));
                        for (int k = 0; k < 9; ++k) {
                            if (baseLines >> k & 1) continue;
                            const int c = kT.unit[cu][k];
                            if (boxOf(c) == finBox && !g.val[c] && (g.cand[c] & digitBit(d))) {
                                t.elim[c] |= digitBit(d);
                                any = true;
                            }
                        }
                        t.units |= 1u << cu;
                    }
                    if (!any) continue;
                    for (int i = 0; i < n; ++i) t.units |= 1u << (baseOff + lines[idx[i]]);
                    t.tech = tech;
                    t.digits = digitBit(d);
                    t.cells = body;
                    t.cells2 = fins;
                    t.unit = uint8_t(baseOff);
                    t.unit2 = uint8_t(boxUnit(finBox));
                    s = std::move(t);
                    return true;
                }
                return false;
            };
            auto rec = [&](auto& self, int start, int depth, Mask uni) -> bool {
                if (depth == n) return tryCombo(uni);
                for (int i = start; i < nl; ++i) {
                    const Mask nu = uni | a.pos[baseOff + lines[i]][d];
                    if (bitCount(nu) > n + 2) continue;
                    idx[depth] = i;
                    if (self(self, i + 1, depth + 1, nu)) return true;
                }
                return false;
            };
            if (rec(rec, 0, 0, 0)) return true;
        }
    }
    return false;
}

}  // namespace

bool findXWing(const LogicGrid& g, const Analysis& a, Step& s) { return basicFish(g, a, 2, Tech::XWing, s); }
bool findSwordfish(const LogicGrid& g, const Analysis& a, Step& s) { return basicFish(g, a, 3, Tech::Swordfish, s); }
bool findJellyfish(const LogicGrid& g, const Analysis& a, Step& s) { return basicFish(g, a, 4, Tech::Jellyfish, s); }
bool findFinnedXWing(const LogicGrid& g, const Analysis& a, Step& s) { return finnedFish(g, a, 2, Tech::FinnedXWing, s); }
bool findFinnedSwordfish(const LogicGrid& g, const Analysis& a, Step& s) {
    return finnedFish(g, a, 3, Tech::FinnedSwordfish, s);
}

}  // namespace sudoku::detail
