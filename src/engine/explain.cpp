// Plain-language explanations for each technique (teaching hints).
#include <cctype>

#include "logic.h"

namespace sudoku {
namespace {

std::string ds(int d) { return std::string(1, char('0' + d)); }

std::string cap(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string cellList(const CellSet& set) {
    std::string out;
    const int n = set.count();
    int i = 0;
    set.forEach([&](int c) {
        if (i > 0) out += (i == n - 1) ? " and " : ", ";
        out += cellName(c);
        ++i;
    });
    return out;
}

CellSet elimCells(const Step& s) {
    CellSet r;
    for (int c = 0; c < 81; ++c)
        if (s.elim[c]) r.set(c);
    return r;
}

// "rows 2, 5 and 8" from a 9-bit line mask.
std::string lineList(unsigned bits, bool rows) {
    const int n = std::popcount(bits);
    std::string out = rows ? (n == 1 ? "row " : "rows ") : (n == 1 ? "column " : "columns ");
    int i = 0;
    for (unsigned m = bits; m; m &= m - 1) {
        if (i > 0) out += (i == n - 1) ? " and " : ", ";
        out += char('1' + std::countr_zero(m));
        ++i;
    }
    return out;
}

const char* sizeWord(int k) { return k == 2 ? "two" : k == 3 ? "three" : "four"; }

int subsetSize(Tech t) {
    switch (t) {
    case Tech::NakedPair:
    case Tech::HiddenPair: return 2;
    case Tech::NakedTriple:
    case Tech::HiddenTriple: return 3;
    default: return 4;
    }
}

int fishSize(Tech t) {
    switch (t) {
    case Tech::XWing:
    case Tech::FinnedXWing: return 2;
    case Tech::Swordfish:
    case Tech::FinnedSwordfish: return 3;
    default: return 4;
    }
}

std::string xChainText(const Step& s) {
    std::string out;
    for (size_t i = 0; i < s.chain.size(); ++i) {
        if (i > 0) out += (i % 2 == 1) ? " = " : " – ";
        out += cellName(s.chain[i].cell);
    }
    return out;
}

std::string xyChainText(const Step& s) {
    std::string out;
    int prevOn = s.contraDigit;  // the chain starts by assuming the first cell is not x
    for (size_t i = 0; i < s.chain.size(); ++i) {
        if (i > 0) out += " – ";
        out += cellName(s.chain[i].cell) + "(" + ds(prevOn) + "=" + ds(s.chain[i].digit) + ")";
        prevOn = s.chain[i].digit;
    }
    return out;
}

std::string traceText(const Step& s) {
    std::string out;
    const size_t n = s.chain.size();
    for (size_t i = 0; i < n; ++i) {
        if (n > 9 && i == 6) {
            out += " → …";
            i = n - 2;
            continue;
        }
        if (i > 0) out += " → ";
        out += cellName(s.chain[i].cell) + "=" + ds(s.chain[i].digit);
    }
    return out;
}

}  // namespace

Explanation explain(const Step& s) {
    Explanation e;
    e.title = techName(s.tech);
    const int d = s.digits ? lowestDigit(s.digits) : 0;
    const CellSet targets = elimCells(s);
    const std::string tl = cellList(targets);
    const bool plural = targets.count() > 1;

    switch (s.tech) {
    case Tech::FullHouse:
        e.nudge = cap(unitName(s.unit)) + " has only one empty cell left.";
        e.detail = cellName(s.placeCell) + " is the last empty cell in " + unitName(s.unit) +
                   ", so it must be the missing digit, " + ds(s.placeDigit) + ".";
        break;

    case Tech::HiddenSingle:
        e.nudge = "Look at " + unitName(s.unit) + ": one digit has only one place it can go.";
        e.detail = "In " + unitName(s.unit) + ", " + ds(s.placeDigit) + " can only go in " + cellName(s.placeCell) +
                   ". Every other empty cell there already sees a " + ds(s.placeDigit) +
                   " in its row, column or box.";
        break;

    case Tech::NakedSingle:
        e.nudge = "Look in " + unitName(boxUnit(boxOf(s.placeCell))) + ": one cell has only one possible digit.";
        e.detail = cellName(s.placeCell) + " has only one candidate left: " + ds(s.placeDigit) +
                   ". Every other digit is ruled out by its row, column or box.";
        break;

    case Tech::Pointing:
        e.nudge = "Look at where " + ds(d) + " can go in " + unitName(s.unit) + ".";
        e.detail = "In " + unitName(s.unit) + ", " + ds(d) + " can only go in " + unitName(s.unit2) +
                   ". Whichever of those cells gets it, that is also the " + ds(d) + " for " + unitName(s.unit2) +
                   " - so " + ds(d) + " can be removed from the rest of " + unitName(s.unit2) + " (" + tl + ").";
        break;

    case Tech::Claiming:
        e.nudge = "Look at where " + ds(d) + " can go in " + unitName(s.unit) + ".";
        e.detail = "In " + unitName(s.unit) + ", " + ds(d) + " can only go inside " + unitName(s.unit2) + ". So " +
                   unitName(s.unit2) + " gets its " + ds(d) + " from " + unitName(s.unit) + ", and " + ds(d) +
                   " can be removed from the other cells of " + unitName(s.unit2) + " (" + tl + ").";
        break;

    case Tech::NakedPair:
    case Tech::NakedTriple:
    case Tech::NakedQuad: {
        const int k = subsetSize(s.tech);
        e.nudge = std::string("Look for ") + sizeWord(k) + " cells in " + unitName(s.unit) + " that share only " +
                  sizeWord(k) + " candidates.";
        e.detail = cellList(s.cells) + " in " + unitName(s.unit) + " can only contain " + digitList(s.digits) +
                   ". Those " + sizeWord(k) + " digits must fill those " + sizeWord(k) +
                   " cells, so they can be removed from the other cells of " + unitName(s.unit) + " (" + tl + ").";
        break;
    }

    case Tech::HiddenPair:
    case Tech::HiddenTriple:
    case Tech::HiddenQuad: {
        const int k = subsetSize(s.tech);
        e.nudge = std::string("In ") + unitName(s.unit) + ", " + sizeWord(k) + " digits are confined to the same " +
                  sizeWord(k) + " cells.";
        e.detail = "In " + unitName(s.unit) + ", the digits " + digitList(s.digits) + " can only go in " +
                   cellList(s.cells) + ". Those cells must hold exactly these digits, so every other candidate "
                   "can be removed from them.";
        break;
    }

    case Tech::XWing:
    case Tech::Swordfish:
    case Tech::Jellyfish: {
        const int n = fishSize(s.tech);
        const bool rowsBase = s.unit == 0;
        const unsigned rowBits = s.units & 0x1FF, colBits = (s.units >> 9) & 0x1FF;
        const std::string base = lineList(rowsBase ? rowBits : colBits, rowsBase);
        const std::string cover = lineList(rowsBase ? colBits : rowBits, !rowsBase);
        e.nudge = "Look at the " + ds(d) + "s in " + base + ".";
        e.detail = "In " + base + ", every possible " + ds(d) + " lies in " + cover + ". Each of those " +
                   sizeWord(n) + (rowsBase ? " rows" : " columns") + " needs its own " + ds(d) +
                   ", and they can only take them from these " + sizeWord(n) + (rowsBase ? " columns" : " rows") +
                   " - so no other cell in " + cover + " can be " + ds(d) + " (" + tl + ").";
        break;
    }

    case Tech::FinnedXWing:
    case Tech::FinnedSwordfish: {
        const bool rowsBase = s.unit == 0;
        const unsigned rowBits = s.units & 0x1FF, colBits = (s.units >> 9) & 0x1FF;
        const std::string base = lineList(rowsBase ? rowBits : colBits, rowsBase);
        const std::string cover = lineList(rowsBase ? colBits : rowBits, !rowsBase);
        const std::string fishName = s.tech == Tech::FinnedXWing ? "X-Wing" : "Swordfish";
        e.nudge = "Look at the " + ds(d) + "s in " + base + ": they almost form a " + fishName + ".";
        e.detail = "In " + base + ", " + ds(d) + " is confined to " + cover + ", except for the fin" +
                   (s.cells2.count() > 1 ? "s at " : " at ") + cellList(s.cells2) + ". If no fin is " + ds(d) +
                   ", the " + fishName + " removes " + ds(d) + " from the rest of " + cover + "; if a fin is " +
                   ds(d) + ", it removes " + ds(d) + " from its own box. Either way, " + tl + " can't be " +
                   ds(d) + ".";
        break;
    }

    case Tech::Skyscraper: {
        const bool rows = isRowUnit(s.unit);
        e.nudge = "Look at the " + ds(d) + "s: two " + (rows ? "rows" : "columns") + " each have exactly two places for " +
                  ds(d) + ".";
        e.detail = cap(unitName(s.unit)) + " and " + unitName(s.unit2) + " each have only two places for " + ds(d) +
                   ", and one place of each lines up in the same " + (rows ? "column" : "row") +
                   ". Those two can't both be " + ds(d) + ", so at least one of the other ends - " +
                   cellName(s.chain[0].cell) + " or " + cellName(s.chain[3].cell) + " - is " + ds(d) +
                   ". Any cell that sees both of them (" + tl + ") can't be " + ds(d) + ".";
        break;
    }

    case Tech::TwoStringKite:
        e.nudge = "Look at the " + ds(d) + "s in " + unitName(s.unit) + " and " + unitName(s.unit2) + ".";
        e.detail = ds(d) + " has only two places in " + unitName(s.unit) + " and two in " + unitName(s.unit2) +
                   ". One end of each sits in " + unitName(boxUnit(boxOf(s.chain[1].cell))) +
                   ", and those two can't both be " + ds(d) + " - so " + cellName(s.chain[0].cell) + " or " +
                   cellName(s.chain[3].cell) + " must be " + ds(d) + ". " + tl + " sees both, so it can't be " +
                   ds(d) + ".";
        break;

    case Tech::XYWing: {
        const int P = s.chain[0].cell, A = s.chain[1].cell, B = s.chain[2].cell;
        const int x = s.chain[1].digit, y = s.chain[2].digit, z = s.contraDigit;
        e.nudge = "Look for a two-candidate pivot cell that sees two other two-candidate cells.";
        e.detail = cellName(P) + " must be " + ds(x) + " or " + ds(y) + ". If it's " + ds(x) + ", then " + cellName(A) +
                   " must be " + ds(z) + "; if it's " + ds(y) + ", then " + cellName(B) + " must be " + ds(z) +
                   ". Either way one of them is " + ds(z) + ", so " + tl + (plural ? ", which see" : ", which sees") +
                   " both, can't be " + ds(z) + ".";
        break;
    }

    case Tech::XYZWing: {
        const int P = s.chain[0].cell, A = s.chain[1].cell, B = s.chain[2].cell;
        const int a = s.chain[1].digit, b = s.chain[2].digit, z = s.contraDigit;
        e.nudge = "Look for a three-candidate cell that sees two two-candidate cells.";
        e.detail = cellName(P) + " holds " + digitList(s.digits) + ", while " + cellName(A) + " holds " + ds(a) +
                   " and " + ds(z) + ", and " + cellName(B) + " holds " + ds(b) + " and " + ds(z) +
                   ". Whatever " + cellName(P) + " turns out to be, one of these three cells must be " + ds(z) +
                   " - so " + tl + (plural ? ", which see" : ", which sees") + " all three, can't be " + ds(z) + ".";
        break;
    }

    case Tech::WWing: {
        const int A = s.chain[0].cell, la = s.chain[1].cell, lb = s.chain[2].cell, B = s.chain[3].cell;
        const int x = s.chain[0].digit, y = s.contraDigit;
        e.nudge = "Look for two far-apart cells with the same two candidates, " + ds(x) + " and " + ds(y) + ".";
        e.detail = cellName(A) + " and " + cellName(B) + " both contain only " + ds(x) + " and " + ds(y) + ". In " +
                   unitName(s.unit) + ", " + ds(x) + " can only go in " + cellName(la) + " or " + cellName(lb) +
                   ", which see " + cellName(A) + " and " + cellName(B) + ". If " + cellName(A) + " weren't " + ds(y) +
                   " it would be " + ds(x) + ", which would push " + ds(x) + " to " + cellName(lb) + " and make " +
                   cellName(B) + " a " + ds(y) + ". So one of them is " + ds(y) + ", and " + tl + " can't be " +
                   ds(y) + ".";
        break;
    }

    case Tech::SimpleColoring:
        e.nudge = "Follow the " + ds(d) + "s that have exactly two places in a row, column or box.";
        if (s.unit2 == 1) {
            e.detail = "Chaining the " + ds(d) + "s that have exactly two places in a unit splits them into two "
                       "alternating colours - one colour must be all " + ds(d) + "s. Two cells of the same colour "
                       "see each other, so that colour can't be " + ds(d) + ": remove " + ds(d) + " from " + tl + ".";
        } else {
            e.detail = "Chaining the " + ds(d) + "s that have exactly two places in a unit splits them into two "
                       "alternating colours - one colour must be all " + ds(d) + "s. " + tl +
                       (plural ? " see cells of both colours, so they can't be " : " sees cells of both colours, so it can't be ") +
                       ds(d) + ".";
        }
        break;

    case Tech::UniqueRectangle: {
        const int odd = s.contraCell;
        const int a = lowestDigit(s.digits), b = 32 - std::countl_zero(unsigned(s.digits));
        e.nudge = "Look for four cells forming a rectangle over two boxes that share the candidates " + ds(a) +
                  " and " + ds(b) + ".";
        e.detail = cellList(s.cells) + " and " + cellName(odd) + " form a rectangle across two boxes, and three of "
                   "them contain only " + ds(a) + " and " + ds(b) + ". If " + cellName(odd) + " were also " + ds(a) +
                   " or " + ds(b) + ", the four cells could swap those digits and the puzzle would have two "
                   "solutions. A proper puzzle has exactly one, so " + ds(a) + " and " + ds(b) +
                   " can be removed from " + cellName(odd) + ".";
        break;
    }

    case Tech::BUG:
        e.nudge = "Almost every unsolved cell has exactly two candidates.";
        e.detail = "Every unsolved cell has exactly two candidates except " + cellName(s.placeCell) + ". If " +
                   cellName(s.placeCell) + " weren't " + ds(s.placeDigit) +
                   ", every candidate would appear exactly twice in each row, column and box - a 'deadly' pattern "
                   "with more than one solution. So " + cellName(s.placeCell) + " must be " + ds(s.placeDigit) + ".";
        break;

    case Tech::XChain:
        e.nudge = "Follow a chain of " + ds(d) + "s, alternating between strong and weak links.";
        e.detail = "Follow this chain of " + ds(d) + "s: " + xChainText(s) + "  ('=' marks the only two places for " +
                   ds(d) + " in a unit; '–' marks two cells that see each other). If " +
                   cellName(s.chain.front().cell) + " isn't " + ds(d) + ", the chain forces " +
                   cellName(s.chain.back().cell) + " to be " + ds(d) + ". So one of them is " + ds(d) + ", and " + tl +
                   " can't be " + ds(d) + ".";
        break;

    case Tech::XYChain: {
        const int x = s.contraDigit;
        e.nudge = "Follow a chain of two-candidate cells that starts and ends with " + ds(x) + ".";
        e.detail = "Follow this chain of two-candidate cells: " + xyChainText(s) + ". If " +
                   cellName(s.chain.front().cell) + " isn't " + ds(x) + ", each cell in turn is forced until " +
                   cellName(s.chain.back().cell) + " becomes " + ds(x) + ". So one of them is " + ds(x) + ", and " +
                   tl + " can't be " + ds(x) + ".";
        break;
    }

    case Tech::ForcingChain: {
        const int c = s.chain.front().cell, v = s.chain.front().digit;
        std::string contra;
        if (s.contraCell >= 0) contra = cellName(s.contraCell) + " would have no candidates left";
        else contra = unitName(s.contraUnit) + " would have nowhere to put a " + ds(s.contraDigit);
        e.nudge = "Try assuming " + cellName(c) + " is " + ds(v) + " and follow the consequences.";
        e.detail = "Suppose " + cellName(c) + " were " + ds(v) + ". Following the singles: " + traceText(s) +
                   ". Then " + contra + " - impossible. So " + cellName(c) + " can't be " + ds(v) + ".";
        break;
    }

    default:
        e.nudge = "Look closely at the highlighted cells.";
        e.detail = "Apply " + e.title + ".";
        break;
    }
    return e;
}

}  // namespace sudoku
