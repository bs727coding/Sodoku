#include "logic.h"

#include "techniques.h"

namespace sudoku {

namespace {

struct TechInfo {
    const char* name;
    int tier;
};

constexpr TechInfo kTechInfo[kTechCount] = {
    {"Full House", 1},        {"Hidden Single", 1},     {"Naked Single", 1},     {"Pointing", 2},
    {"Claiming", 2},          {"Naked Pair", 3},        {"Hidden Pair", 3},      {"Naked Triple", 3},
    {"Hidden Triple", 3},     {"X-Wing", 4},            {"Skyscraper", 4},       {"2-String Kite", 4},
    {"XY-Wing", 4},           {"XYZ-Wing", 4},          {"W-Wing", 4},           {"Swordfish", 4},
    {"Simple Coloring", 4},   {"Unique Rectangle", 4},  {"Naked Quad", 4},       {"Hidden Quad", 4},
    {"BUG+1", 5},             {"Finned X-Wing", 5},     {"Finned Swordfish", 5}, {"Jellyfish", 5},
    {"XY-Chain", 5},          {"X-Chain", 5},           {"Forcing Chain", 6},
};

struct LadderEntry {
    Tech tech;
    detail::Finder find;
};

// Ordered from easiest to hardest; findStep returns the first technique that applies.
constexpr LadderEntry kLadder[] = {
    {Tech::FullHouse, detail::findFullHouse},
    {Tech::HiddenSingle, detail::findHiddenSingle},
    {Tech::NakedSingle, detail::findNakedSingle},
    {Tech::Pointing, detail::findPointing},
    {Tech::Claiming, detail::findClaiming},
    {Tech::NakedPair, detail::findNakedPair},
    {Tech::HiddenPair, detail::findHiddenPair},
    {Tech::NakedTriple, detail::findNakedTriple},
    {Tech::HiddenTriple, detail::findHiddenTriple},
    {Tech::XWing, detail::findXWing},
    {Tech::Skyscraper, detail::findSkyscraper},
    {Tech::TwoStringKite, detail::findTwoStringKite},
    {Tech::XYWing, detail::findXYWing},
    {Tech::XYZWing, detail::findXYZWing},
    {Tech::WWing, detail::findWWing},
    {Tech::Swordfish, detail::findSwordfish},
    {Tech::SimpleColoring, detail::findSimpleColoring},
    {Tech::UniqueRectangle, detail::findUniqueRectangle},
    {Tech::NakedQuad, detail::findNakedQuad},
    {Tech::HiddenQuad, detail::findHiddenQuad},
    {Tech::BUG, detail::findBUG},
    {Tech::FinnedXWing, detail::findFinnedXWing},
    {Tech::FinnedSwordfish, detail::findFinnedSwordfish},
    {Tech::Jellyfish, detail::findJellyfish},
    {Tech::XYChain, detail::findXYChain},
    {Tech::XChain, detail::findXChain},
    {Tech::ForcingChain, detail::findForcingChain},
};

}  // namespace

int techTier(Tech t) { return t < Tech::Count ? kTechInfo[int(t)].tier : 7; }
const char* techName(Tech t) { return t < Tech::Count ? kTechInfo[int(t)].name : "Guess"; }

bool Step::hasElims() const {
    for (Mask m : elim)
        if (m) return true;
    return false;
}

int Step::elimCount() const {
    int n = 0;
    for (Mask m : elim) n += bitCount(m);
    return n;
}

LogicGrid LogicGrid::fromGrid(const Grid& g) {
    LogicGrid lg;
    lg.val = g;
    lg.empty = 0;
    for (int c = 0; c < 81; ++c) {
        if (g[c]) {
            lg.cand[c] = 0;
            continue;
        }
        ++lg.empty;
        Mask used = 0;
        for (int p : kT.peers[c])
            if (g[p]) used |= digitBit(g[p]);
        lg.cand[c] = Mask(kAllDigits & ~used);
    }
    return lg;
}

void LogicGrid::place(int c, int d) {
    if (val[c]) return;
    val[c] = uint8_t(d);
    cand[c] = 0;
    --empty;
    const Mask keep = Mask(~digitBit(d));
    for (int p : kT.peers[c]) cand[p] &= keep;
}

void LogicGrid::apply(const Step& s) {
    if (s.placeCell >= 0) place(s.placeCell, s.placeDigit);
    for (int c = 0; c < 81; ++c)
        if (s.elim[c]) cand[c] &= Mask(~s.elim[c]);
}

bool findStep(const LogicGrid& g, Step& out, int maxTier) {
    if (g.solved()) return false;
    const detail::Analysis a(g);
    for (const auto& e : kLadder) {
        if (techTier(e.tech) > maxTier) break;
        if (e.find(g, a, out)) {
            out.tech = e.tech;
            return true;
        }
    }
    return false;
}

Rating rate(const Grid& puzzle, int maxTier) {
    Rating r;
    LogicGrid g = LogicGrid::fromGrid(puzzle);
    Step s;
    while (!g.solved()) {
        if (!findStep(g, s, maxTier)) {
            r.tier = maxTier + 1;
            return r;
        }
        // Guard against a step that changes nothing (would loop forever).
        bool progress = s.placeCell >= 0;
        for (int c = 0; c < 81 && !progress; ++c) progress = (g.cand[c] & s.elim[c]) != 0;
        if (!progress) {
            r.tier = maxTier + 1;
            return r;
        }
        const int t = techTier(s.tech);
        if (t > r.tier) {
            r.tier = t;
            r.hardest = s.tech;
        }
        ++r.uses[int(s.tech)];
        ++r.steps;
        g.apply(s);
    }
    r.solved = true;
    return r;
}

}  // namespace sudoku
