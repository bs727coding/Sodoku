// Skyscraper, 2-String Kite, XY-Wing, XYZ-Wing, W-Wing, Simple Coloring,
// Unique Rectangle (type 1) and BUG+1.
#include <array>

#include "techniques.h"

namespace sudoku::detail {

// ------------------------------------------------------------- skyscraper

bool findSkyscraper(const LogicGrid&, const Analysis& a, Step& s) {
    for (int d = 1; d <= 9; ++d) {
        for (int orient = 0; orient < 2; ++orient) {
            const int baseOff = orient == 0 ? 0 : 9;
            const int coverOff = orient == 0 ? 9 : 0;
            for (int l1 = 0; l1 < 9; ++l1) {
                const Mask p1 = a.pos[baseOff + l1][d];
                if (bitCount(p1) != 2) continue;
                for (int l2 = l1 + 1; l2 < 9; ++l2) {
                    const Mask p2 = a.pos[baseOff + l2][d];
                    if (bitCount(p2) != 2) continue;
                    const Mask common = p1 & p2;
                    if (bitCount(common) != 1) continue;
                    const int baseIdx = std::countr_zero(unsigned(common));
                    const int u1 = baseOff + l1, u2 = baseOff + l2;
                    const int b1 = kT.unit[u1][baseIdx], b2 = kT.unit[u2][baseIdx];
                    const int e1 = kT.unit[u1][std::countr_zero(unsigned(p1 & ~common))];
                    const int e2 = kT.unit[u2][std::countr_zero(unsigned(p2 & ~common))];
                    CellSet targets = kT.peerSet[e1] & kT.peerSet[e2] & a.digitCells[d];
                    targets.reset(b1);
                    targets.reset(b2);
                    if (targets.empty()) continue;
                    s = Step{};
                    s.tech = Tech::Skyscraper;
                    eliminate(s, targets, d);
                    s.digits = digitBit(d);
                    s.cells.set(e1);
                    s.cells.set(e2);
                    s.cells2.set(b1);
                    s.cells2.set(b2);
                    s.units = (1u << u1) | (1u << u2) | (1u << (coverOff + baseIdx));
                    s.unit = uint8_t(u1);
                    s.unit2 = uint8_t(u2);
                    s.chain = {{uint8_t(e1), uint8_t(d)}, {uint8_t(b1), uint8_t(d)},
                               {uint8_t(b2), uint8_t(d)}, {uint8_t(e2), uint8_t(d)}};
                    return true;
                }
            }
        }
    }
    return false;
}

// -------------------------------------------------------- 2-string kite

bool findTwoStringKite(const LogicGrid& g, const Analysis& a, Step& s) {
    for (int d = 1; d <= 9; ++d) {
        for (int r = 0; r < 9; ++r) {
            const Mask pr = a.pos[rowUnit(r)][d];
            if (bitCount(pr) != 2) continue;
            const int rc[2] = {kT.unit[rowUnit(r)][std::countr_zero(unsigned(pr))],
                               kT.unit[rowUnit(r)][31 - std::countl_zero(unsigned(pr))]};
            for (int col = 0; col < 9; ++col) {
                const Mask pc = a.pos[colUnit(col)][d];
                if (bitCount(pc) != 2) continue;
                const int cc[2] = {kT.unit[colUnit(col)][std::countr_zero(unsigned(pc))],
                                   kT.unit[colUnit(col)][31 - std::countl_zero(unsigned(pc))]};
                for (int ri = 0; ri < 2; ++ri) {
                    for (int ci = 0; ci < 2; ++ci) {
                        const int x = rc[ri], y = cc[ci];
                        if (x == y || boxOf(x) != boxOf(y)) continue;
                        const int rOther = rc[1 - ri], cOther = cc[1 - ci];
                        if (rOther == cOther || rOther == y || cOther == x) continue;
                        if (boxOf(rOther) == boxOf(x) || boxOf(cOther) == boxOf(x)) continue;
                        const int target = cellAt(rowOf(cOther), colOf(rOther));
                        if (target == x || target == y || target == rOther || target == cOther) continue;
                        if (g.val[target] || !(g.cand[target] & digitBit(d))) continue;
                        s = Step{};
                        s.tech = Tech::TwoStringKite;
                        s.elim[target] = digitBit(d);
                        s.digits = digitBit(d);
                        s.cells.set(rOther);
                        s.cells.set(cOther);
                        s.cells2.set(x);
                        s.cells2.set(y);
                        s.units = (1u << rowUnit(r)) | (1u << colUnit(col)) | (1u << boxUnit(boxOf(x)));
                        s.unit = uint8_t(rowUnit(r));
                        s.unit2 = uint8_t(colUnit(col));
                        s.chain = {{uint8_t(rOther), uint8_t(d)}, {uint8_t(x), uint8_t(d)},
                                   {uint8_t(y), uint8_t(d)}, {uint8_t(cOther), uint8_t(d)}};
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------- wings

bool findXYWing(const LogicGrid& g, const Analysis& a, Step& s) {
    for (int p = 0; p < 81; ++p) {
        if (g.val[p] || bitCount(g.cand[p]) != 2) continue;
        const Mask cp = g.cand[p];
        for (int ai = 0; ai < 20; ++ai) {
            const int A = kT.peers[p][ai];
            if (g.val[A] || bitCount(g.cand[A]) != 2) continue;
            const Mask shared = g.cand[A] & cp;
            if (bitCount(shared) != 1) continue;
            const Mask z = g.cand[A] & Mask(~shared);
            const Mask need = Mask((cp & ~shared) | z);
            for (int bi = 0; bi < 20; ++bi) {
                const int B = kT.peers[p][bi];
                if (B == A || g.val[B] || g.cand[B] != need) continue;
                const int zd = lowestDigit(z);
                CellSet targets = kT.peerSet[A] & kT.peerSet[B] & a.digitCells[zd];
                targets.reset(p);
                if (targets.empty()) continue;
                s = Step{};
                s.tech = Tech::XYWing;
                eliminate(s, targets, zd);
                s.cells.set(p);
                s.cells2.set(A);
                s.cells2.set(B);
                s.digits = Mask(cp | z);
                s.chain = {{uint8_t(p), 0}, {uint8_t(A), uint8_t(lowestDigit(shared))},
                           {uint8_t(B), uint8_t(lowestDigit(cp & ~shared))}};
                s.contraDigit = uint8_t(zd);
                return true;
            }
        }
    }
    return false;
}

bool findXYZWing(const LogicGrid& g, const Analysis& a, Step& s) {
    for (int p = 0; p < 81; ++p) {
        if (g.val[p] || bitCount(g.cand[p]) != 3) continue;
        const Mask cp = g.cand[p];
        for (int ai = 0; ai < 20; ++ai) {
            const int A = kT.peers[p][ai];
            if (g.val[A] || bitCount(g.cand[A]) != 2 || (g.cand[A] & ~cp)) continue;
            for (int bi = ai + 1; bi < 20; ++bi) {
                const int B = kT.peers[p][bi];
                if (g.val[B] || bitCount(g.cand[B]) != 2 || (g.cand[B] & ~cp)) continue;
                if ((g.cand[A] | g.cand[B]) != cp) continue;
                const Mask z = g.cand[A] & g.cand[B];
                if (bitCount(z) != 1) continue;
                const int zd = lowestDigit(z);
                const CellSet targets = kT.peerSet[p] & kT.peerSet[A] & kT.peerSet[B] & a.digitCells[zd];
                if (targets.empty()) continue;
                s = Step{};
                s.tech = Tech::XYZWing;
                eliminate(s, targets, zd);
                s.cells.set(p);
                s.cells2.set(A);
                s.cells2.set(B);
                s.digits = cp;
                s.chain = {{uint8_t(p), 0},
                           {uint8_t(A), uint8_t(lowestDigit(Mask(g.cand[A] & ~z)))},
                           {uint8_t(B), uint8_t(lowestDigit(Mask(g.cand[B] & ~z)))}};
                s.contraDigit = uint8_t(zd);
                return true;
            }
        }
    }
    return false;
}

bool findWWing(const LogicGrid& g, const Analysis& a, Step& s) {
    for (int A = 0; A < 81; ++A) {
        if (g.val[A] || bitCount(g.cand[A]) != 2) continue;
        for (int B = A + 1; B < 81; ++B) {
            if (g.val[B] || g.cand[B] != g.cand[A] || sees(A, B)) continue;
            for (int xi = 0; xi < 2; ++xi) {
                const Mask pair = g.cand[A];
                const int x = xi == 0 ? lowestDigit(pair) : 32 - std::countl_zero(unsigned(pair));
                const int y = lowestDigit(Mask(pair & ~digitBit(x)));
                const CellSet targets = kT.peerSet[A] & kT.peerSet[B] & a.digitCells[y];
                if (targets.empty()) continue;
                for (int u = 0; u < 27; ++u) {
                    const Mask pm = a.pos[u][x];
                    if (bitCount(pm) != 2) continue;
                    const int l1 = kT.unit[u][std::countr_zero(unsigned(pm))];
                    const int l2 = kT.unit[u][31 - std::countl_zero(unsigned(pm))];
                    if (l1 == A || l1 == B || l2 == A || l2 == B) continue;
                    int la = -1, lb = -1;
                    if (sees(A, l1) && sees(B, l2)) {
                        la = l1;
                        lb = l2;
                    } else if (sees(A, l2) && sees(B, l1)) {
                        la = l2;
                        lb = l1;
                    } else {
                        continue;
                    }
                    s = Step{};
                    s.tech = Tech::WWing;
                    eliminate(s, targets, y);
                    s.cells.set(A);
                    s.cells.set(B);
                    s.cells2.set(la);
                    s.cells2.set(lb);
                    s.units = 1u << u;
                    s.unit = uint8_t(u);
                    s.digits = pair;
                    s.chain = {{uint8_t(A), uint8_t(x)}, {uint8_t(la), uint8_t(x)},
                               {uint8_t(lb), uint8_t(x)}, {uint8_t(B), uint8_t(x)}};
                    s.contraDigit = uint8_t(y);
                    return true;
                }
            }
        }
    }
    return false;
}

// ------------------------------------------------------- simple coloring

bool findSimpleColoring(const LogicGrid&, const Analysis& a, Step& s) {
    for (int d = 1; d <= 9; ++d) {
        // Conjugate links: units where d has exactly two places.
        std::array<uint8_t, 81 * 3> adj{};
        std::array<uint8_t, 81> deg{};
        for (int u = 0; u < 27; ++u) {
            const Mask pm = a.pos[u][d];
            if (bitCount(pm) != 2) continue;
            const int x = kT.unit[u][std::countr_zero(unsigned(pm))];
            const int y = kT.unit[u][31 - std::countl_zero(unsigned(pm))];
            bool dup = false;
            for (int i = 0; i < deg[x]; ++i) dup |= adj[x * 3 + i] == y;
            if (dup) continue;
            adj[x * 3 + deg[x]++] = uint8_t(y);
            adj[y * 3 + deg[y]++] = uint8_t(x);
        }
        std::array<int8_t, 81> color;
        color.fill(-1);
        for (int start = 0; start < 81; ++start) {
            if (!deg[start] || color[start] >= 0) continue;
            CellSet col[2];
            uint8_t queue[81];
            int qh = 0, qt = 0;
            queue[qt++] = uint8_t(start);
            color[start] = 0;
            col[0].set(start);
            while (qh < qt) {
                const int c = queue[qh++];
                for (int i = 0; i < deg[c]; ++i) {
                    const int n = adj[c * 3 + i];
                    if (color[n] >= 0) continue;
                    color[n] = int8_t(1 - color[c]);
                    col[color[n]].set(n);
                    queue[qt++] = uint8_t(n);
                }
            }
            if (qt < 3) continue;  // a single conjugate pair is not a coloring chain
            // Color wrap: two cells of the same colour see each other.
            for (int k = 0; k < 2; ++k) {
                bool wrap = false;
                col[k].forEach([&](int c) { wrap |= !(kT.peerSet[c] & col[k]).empty(); });
                if (!wrap) continue;
                s = Step{};
                s.tech = Tech::SimpleColoring;
                eliminate(s, col[k], d);
                s.cells = col[1 - k];
                s.cells2 = col[k];
                s.digits = digitBit(d);
                s.unit2 = 1;  // explanation: wrap
                return true;
            }
            // Color trap: an outside cell sees both colours.
            CellSet seen[2];
            for (int k = 0; k < 2; ++k) col[k].forEach([&](int c) { seen[k] |= kT.peerSet[c]; });
            const CellSet targets = (seen[0] & seen[1] & a.digitCells[d]) - col[0] - col[1];
            if (targets.empty()) continue;
            s = Step{};
            s.tech = Tech::SimpleColoring;
            eliminate(s, targets, d);
            s.cells = col[0];
            s.cells2 = col[1];
            s.digits = digitBit(d);
            s.unit2 = 0;  // explanation: trap
            return true;
        }
    }
    return false;
}

// ------------------------------------------------- unique rectangle type 1

bool findUniqueRectangle(const LogicGrid& g, const Analysis&, Step& s) {
    for (int r1 = 0; r1 < 9; ++r1) {
        for (int r2 = r1 + 1; r2 < 9; ++r2) {
            for (int c1 = 0; c1 < 9; ++c1) {
                for (int c2 = c1 + 1; c2 < 9; ++c2) {
                    const bool sameBand = r1 / 3 == r2 / 3, sameStack = c1 / 3 == c2 / 3;
                    if (sameBand == sameStack) continue;  // need exactly two boxes
                    const int cells[4] = {cellAt(r1, c1), cellAt(r1, c2), cellAt(r2, c1), cellAt(r2, c2)};
                    Mask pair = 0;
                    int bivalue = 0, odd = -1;
                    bool ok = true;
                    for (int c : cells) {
                        if (g.val[c]) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) continue;
                    for (int c : cells) {
                        if (bitCount(g.cand[c]) == 2) {
                            if (!pair) pair = g.cand[c];
                            if (g.cand[c] == pair) {
                                ++bivalue;
                                continue;
                            }
                        }
                        if (odd >= 0) {
                            ok = false;
                            break;
                        }
                        odd = c;
                    }
                    if (!ok || bivalue != 3 || odd < 0) continue;
                    if ((g.cand[odd] & pair) != pair || g.cand[odd] == pair) continue;
                    s = Step{};
                    s.tech = Tech::UniqueRectangle;
                    s.elim[odd] = pair;
                    for (int c : cells) {
                        if (c == odd) s.cells2.set(c);
                        else s.cells.set(c);
                    }
                    s.digits = pair;
                    s.placeCell = -1;
                    s.contraCell = int8_t(odd);
                    return true;
                }
            }
        }
    }
    return false;
}

// ------------------------------------------------------------------ BUG+1

bool findBUG(const LogicGrid& g, const Analysis& a, Step& s) {
    int tri = -1;
    for (int c = 0; c < 81; ++c) {
        if (g.val[c]) continue;
        const int n = bitCount(g.cand[c]);
        if (n == 2) continue;
        if (n == 3 && tri < 0) {
            tri = c;
            continue;
        }
        return false;
    }
    if (tri < 0) return false;
    int found = 0, digit = 0;
    forEachDigit(g.cand[tri], [&](int d) {
        for (int u = 0; u < 27; ++u) {
            const bool inUnit = kT.unitSet[u].has(tri);
            for (int dd = 1; dd <= 9; ++dd) {
                int cnt = bitCount(a.pos[u][dd]);
                if (inUnit && dd == d) --cnt;
                if (cnt != 0 && cnt != 2) return;
            }
        }
        ++found;
        digit = d;
    });
    if (found != 1) return false;
    s = Step{};
    s.tech = Tech::BUG;
    s.placeCell = int8_t(tri);
    s.placeDigit = uint8_t(digit);
    s.cells.set(tri);
    s.digits = g.cand[tri];
    return true;
}

}  // namespace sudoku::detail
