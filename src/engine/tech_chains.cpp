// X-Chains, XY-Chains and contradiction forcing chains.
#include <algorithm>
#include <array>

#include "techniques.h"

namespace sudoku::detail {
namespace {

constexpr int kMaxChainLinks = 13;

// Places d at c and propagates naked/hidden singles. Returns false when a contradiction
// appears (recorded in `info`); true when propagation stalls or `limit` placements are made.
bool propagate(LogicGrid& t, int c, int d, std::vector<Node>& trace, Step& info, int limit) {
    t.place(c, d);
    trace.push_back({uint8_t(c), uint8_t(d)});
    for (;;) {
        int single = -1, singleDigit = 0;
        for (int x = 0; x < 81; ++x) {
            if (t.val[x]) continue;
            const Mask m = t.cand[x];
            if (!m) {
                info.contraCell = int8_t(x);
                return false;
            }
            if (single < 0 && !(m & (m - 1))) {
                single = x;
                singleDigit = lowestDigit(m);
            }
        }
        int hsCell = -1, hsDigit = 0;
        for (int u = 0; u < 27; ++u) {
            Mask once = 0, twice = 0, placed = 0;
            for (int x : kT.unit[u]) {
                if (t.val[x]) {
                    placed |= digitBit(t.val[x]);
                } else {
                    twice |= once & t.cand[x];
                    once |= t.cand[x];
                }
            }
            const Mask missing = Mask(kAllDigits & ~(once | placed));
            if (missing) {
                info.contraUnit = uint8_t(u);
                info.contraDigit = uint8_t(lowestDigit(missing));
                return false;
            }
            if (single < 0 && hsCell < 0) {
                const Mask hs = Mask(once & ~twice);
                if (hs) {
                    const int dd = lowestDigit(hs);
                    for (int x : kT.unit[u]) {
                        if (!t.val[x] && (t.cand[x] & digitBit(dd))) {
                            hsCell = x;
                            hsDigit = dd;
                            break;
                        }
                    }
                }
            }
        }
        if (int(trace.size()) >= limit) return true;
        if (single >= 0) {
            t.place(single, singleDigit);
            trace.push_back({uint8_t(single), uint8_t(singleDigit)});
        } else if (hsCell >= 0) {
            t.place(hsCell, hsDigit);
            trace.push_back({uint8_t(hsCell), uint8_t(hsDigit)});
        } else {
            return true;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------- X-Chain
// Alternating chain on one digit: S =strong= N1 -weak- N2 =strong= ... = E.
// If S is not d then E is d, so cells seeing both S and E cannot be d.
bool findXChain(const LogicGrid&, const Analysis& a, Step& s) {
    int bestLinks = kMaxChainLinks + 1;
    bool found = false;
    for (int d = 1; d <= 9; ++d) {
        const CellSet& nodes = a.digitCells[d];
        if (nodes.count() < 4) continue;
        std::array<uint8_t, 81 * 3> sadj{};
        std::array<uint8_t, 81> sdeg{};
        for (int u = 0; u < 27; ++u) {
            const Mask pm = a.pos[u][d];
            if (bitCount(pm) != 2) continue;
            const int x = kT.unit[u][std::countr_zero(unsigned(pm))];
            const int y = kT.unit[u][31 - std::countl_zero(unsigned(pm))];
            bool dup = false;
            for (int i = 0; i < sdeg[x]; ++i) dup |= sadj[x * 3 + i] == y;
            if (dup) continue;
            sadj[x * 3 + sdeg[x]++] = uint8_t(y);
            sadj[y * 3 + sdeg[y]++] = uint8_t(x);
        }
        nodes.forEach([&](int S) {
            if (!sdeg[S]) return;
            std::array<int8_t, 81> parent;   // -2 unvisited, -1 root
            std::array<uint8_t, 81> depth{};  // links from S
            parent.fill(-2);
            uint8_t queue[81];
            int qh = 0, qt = 0;
            parent[S] = -1;
            queue[qt++] = uint8_t(S);
            while (qh < qt) {
                const int c = queue[qh++];
                const int dep = depth[c];
                if (dep + 1 >= bestLinks) continue;
                const bool on = dep & 1;  // even depth: assumed false; odd depth: forced true
                if (!on) {
                    for (int i = 0; i < sdeg[c]; ++i) {
                        const int n = sadj[c * 3 + i];
                        if (parent[n] != -2) continue;
                        parent[n] = int8_t(c);
                        depth[n] = uint8_t(dep + 1);
                        queue[qt++] = uint8_t(n);
                        if (dep + 1 < 3) continue;
                        CellSet targets = kT.peerSet[S] & kT.peerSet[n] & nodes;
                        if (targets.empty()) continue;
                        bestLinks = dep + 1;
                        found = true;
                        s = Step{};
                        s.tech = Tech::XChain;
                        eliminate(s, targets, d);
                        s.digits = digitBit(d);
                        std::vector<Node> chain;
                        for (int x = n; x >= 0; x = parent[x]) chain.push_back({uint8_t(x), uint8_t(d)});
                        std::reverse(chain.begin(), chain.end());
                        s.chain = std::move(chain);
                        s.cells.set(S);
                        s.cells.set(n);
                        for (size_t i = 1; i + 1 < s.chain.size(); ++i) s.cells2.set(s.chain[i].cell);
                    }
                } else {
                    (kT.peerSet[c] & nodes).forEach([&](int n) {
                        if (parent[n] != -2) return;
                        parent[n] = int8_t(c);
                        depth[n] = uint8_t(dep + 1);
                        queue[qt++] = uint8_t(n);
                    });
                }
            }
        });
    }
    return found;
}

// --------------------------------------------------------------- XY-Chain
// Chain of bivalue cells: A(x=y) - B(y=z) - ... - E(w=x). Either A or E is x.
bool findXYChain(const LogicGrid& g, const Analysis& a, Step& s) {
    int bestLinks = kMaxChainLinks + 1;
    bool found = false;
    for (int A = 0; A < 81; ++A) {
        if (g.val[A] || bitCount(g.cand[A]) != 2) continue;
        for (int xi = 0; xi < 2; ++xi) {
            const int x = xi == 0 ? lowestDigit(g.cand[A]) : 32 - std::countl_zero(unsigned(g.cand[A]));
            if ((a.digitCells[x] - kT.peerSet[A]).count() == a.digitCells[x].count()) {
                // no cell sees A with candidate x => no possible elimination
                continue;
            }
            std::array<int8_t, 81> parent;
            std::array<uint8_t, 81> onDigit{};
            std::array<uint8_t, 81> depth{};
            parent.fill(-2);
            uint8_t queue[81];
            int qh = 0, qt = 0;
            parent[A] = -1;
            onDigit[A] = uint8_t(lowestDigit(Mask(g.cand[A] & ~digitBit(x))));
            queue[qt++] = uint8_t(A);
            while (qh < qt) {
                const int c = queue[qh++];
                const int dep = depth[c];
                if (dep + 1 >= bestLinks) continue;
                const int v = onDigit[c];
                for (int n : kT.peers[c]) {
                    if (parent[n] != -2 || g.val[n] || bitCount(g.cand[n]) != 2) continue;
                    if (!(g.cand[n] & digitBit(v))) continue;
                    const int w = lowestDigit(Mask(g.cand[n] & ~digitBit(v)));
                    parent[n] = int8_t(c);
                    onDigit[n] = uint8_t(w);
                    depth[n] = uint8_t(dep + 1);
                    queue[qt++] = uint8_t(n);
                    if (w != x || dep + 1 < 2) continue;
                    const CellSet targets = kT.peerSet[A] & kT.peerSet[n] & a.digitCells[x];
                    if (targets.empty()) continue;
                    bestLinks = dep + 1;
                    found = true;
                    s = Step{};
                    s.tech = Tech::XYChain;
                    eliminate(s, targets, x);
                    s.digits = digitBit(x);
                    s.contraDigit = uint8_t(x);
                    std::vector<Node> chain;
                    for (int y = n; y >= 0; y = parent[y]) chain.push_back({uint8_t(y), onDigit[y]});
                    std::reverse(chain.begin(), chain.end());
                    s.chain = std::move(chain);
                    s.cells.set(A);
                    s.cells.set(n);
                    for (size_t i = 1; i + 1 < s.chain.size(); ++i) s.cells2.set(s.chain[i].cell);
                }
            }
        }
    }
    return found;
}

// ----------------------------------------------------------- forcing chain
// Assume cell = d, follow singles; a contradiction proves cell != d.
bool findForcingChain(const LogicGrid& g, const Analysis&, Step& s) {
    int best = 1 << 30;
    bool found = false;
    for (int k = 2; k <= 9 && !found; ++k) {
        for (int c = 0; c < 81; ++c) {
            if (g.val[c] || bitCount(g.cand[c]) != k) continue;
            for (Mask m = g.cand[c]; m; m &= m - 1) {
                const int d = lowestDigit(m);
                LogicGrid t = g;
                std::vector<Node> trace;
                Step info;
                const int limit = std::min(best - 1, 60);
                if (limit <= 0) break;
                if (propagate(t, c, d, trace, info, limit)) continue;
                if (int(trace.size()) >= best) continue;
                best = int(trace.size());
                found = true;
                s = Step{};
                s.tech = Tech::ForcingChain;
                s.elim[c] = digitBit(d);
                s.cells.set(c);
                s.digits = digitBit(d);
                s.chain = std::move(trace);
                for (size_t i = 1; i < s.chain.size(); ++i) s.cells2.set(s.chain[i].cell);
                s.contraCell = info.contraCell;
                s.contraUnit = info.contraUnit;
                s.contraDigit = info.contraDigit;
                if (s.contraUnit != 0xFF) s.units = 1u << s.contraUnit;
            }
        }
    }
    return found;
}

}  // namespace sudoku::detail
