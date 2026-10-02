#include "solver.h"

namespace sudoku {
namespace {

struct State {
    Mask cand[81];     // candidates of empty cells (0 once filled)
    uint8_t val[81];   // placed digits
    int empties;
};

// Places `digit` in `cell` and propagates naked singles. Returns false on contradiction.
bool assign(State& s, int cell, int digit) {
    uint8_t qc[96], qd[96];
    int head = 0, tail = 0;
    qc[tail] = uint8_t(cell);
    qd[tail++] = uint8_t(digit);
    while (head < tail) {
        const int c = qc[head], d = qd[head];
        ++head;
        if (s.val[c]) {
            if (s.val[c] != d) return false;
            continue;
        }
        const Mask b = digitBit(d);
        if (!(s.cand[c] & b)) return false;
        s.val[c] = uint8_t(d);
        s.cand[c] = 0;
        --s.empties;
        for (int p : kT.peers[c]) {
            if (s.val[p]) {
                if (s.val[p] == d) return false;
                continue;
            }
            Mask m = s.cand[p];
            if (!(m & b)) continue;
            m &= Mask(~b);
            s.cand[p] = m;
            if (!m) return false;
            if (!(m & (m - 1))) {
                if (tail >= 96) return false;  // cannot happen: each cell is queued at most once
                qc[tail] = uint8_t(p);
                qd[tail++] = uint8_t(lowestDigit(m));
            }
        }
    }
    return true;
}

// One pass of hidden-single detection over all units.
// Returns -1 on contradiction, 1 if something was placed, 0 otherwise.
int hiddenSingles(State& s) {
    bool changed = false;
    for (int u = 0; u < 27; ++u) {
        Mask once = 0, twice = 0, placed = 0;
        for (int c : kT.unit[u]) {
            if (s.val[c]) {
                placed |= digitBit(s.val[c]);
            } else {
                const Mask m = s.cand[c];
                twice |= once & m;
                once |= m;
            }
        }
        if ((once | placed) != kAllDigits) return -1;  // some digit has nowhere to go
        Mask singles = once & Mask(~twice) & Mask(~placed);
        while (singles) {
            const int d = lowestDigit(singles);
            singles &= singles - 1;
            const Mask b = digitBit(d);
            int target = -1;
            bool alreadyPlaced = false;
            for (int c : kT.unit[u]) {
                if (s.val[c] == d) {
                    alreadyPlaced = true;
                    break;
                }
                if (!s.val[c] && (s.cand[c] & b)) target = c;
            }
            if (alreadyPlaced) continue;
            if (target < 0) return -1;
            if (!assign(s, target, d)) return -1;
            changed = true;
        }
    }
    return changed ? 1 : 0;
}

struct Search {
    int limit = 2;
    int count = 0;
    Grid* out = nullptr;
    Rng* rng = nullptr;
};

// Returns true when the search should stop (solution limit reached).
bool search(State& s, Search& ctx) {
    for (;;) {
        const int r = hiddenSingles(s);
        if (r < 0) return false;
        if (r == 0) break;
    }
    if (s.empties == 0) {
        if (ctx.count == 0 && ctx.out)
            for (int i = 0; i < 81; ++i) (*ctx.out)[i] = s.val[i];
        return ++ctx.count >= ctx.limit;
    }
    int best = -1, bestN = 10;
    for (int c = 0; c < 81; ++c) {
        if (s.val[c]) continue;
        const int n = bitCount(s.cand[c]);
        if (n < bestN) {
            bestN = n;
            best = c;
            if (n <= 2) break;
        }
    }
    int digits[9], nd = 0;
    for (Mask m = s.cand[best]; m; m &= m - 1) digits[nd++] = lowestDigit(m);
    if (ctx.rng) ctx.rng->shuffle(digits, nd);
    for (int i = 0; i < nd; ++i) {
        State t = s;
        if (assign(t, best, digits[i]) && search(t, ctx)) return true;
    }
    return false;
}

bool init(State& s, const Grid& g) {
    for (int i = 0; i < 81; ++i) {
        s.cand[i] = kAllDigits;
        s.val[i] = 0;
    }
    s.empties = 81;
    for (int i = 0; i < 81; ++i) {
        if (!g[i]) continue;
        if (g[i] > 9 || !assign(s, i, g[i])) return false;
    }
    return true;
}

}  // namespace

int countSolutions(const Grid& puzzle, int limit, Grid* firstSolution) {
    State s;
    if (!init(s, puzzle)) return 0;
    Search ctx;
    ctx.limit = limit;
    ctx.out = firstSolution;
    search(s, ctx);
    return ctx.count;
}

bool hasSolutionWithout(const Grid& puzzle, int cell, int digit) {
    State s;
    if (!init(s, puzzle)) return false;
    if (s.val[cell] == digit) return false;
    if (!s.val[cell]) {
        const Mask m = s.cand[cell] & Mask(~digitBit(digit));
        if (!m) return false;
        s.cand[cell] = m;
        if (!(m & (m - 1)) && !assign(s, cell, lowestDigit(m))) return false;
    }
    Search ctx;
    ctx.limit = 1;
    search(s, ctx);
    return ctx.count > 0;
}

Grid randomSolution(Rng& rng) {
    for (;;) {
        State s;
        Grid empty{};
        init(s, empty);
        Grid out{};
        Search ctx;
        ctx.limit = 1;
        ctx.out = &out;
        ctx.rng = &rng;
        search(s, ctx);
        if (ctx.count) return out;  // always succeeds for an empty grid
    }
}

}  // namespace sudoku
