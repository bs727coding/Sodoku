#include "grid.h"

namespace sudoku {

std::string toString(const Grid& g, char empty) {
    std::string s(81, empty);
    for (int i = 0; i < 81; ++i)
        if (g[i]) s[i] = char('0' + g[i]);
    return s;
}

bool fromString(std::string_view s, Grid& g) {
    if (s.size() != 81) return false;
    for (int i = 0; i < 81; ++i) {
        char ch = s[i];
        if (ch == '.' || ch == '0') g[i] = 0;
        else if (ch >= '1' && ch <= '9') g[i] = uint8_t(ch - '0');
        else return false;
    }
    return true;
}

bool isConsistent(const Grid& g) {
    for (int u = 0; u < 27; ++u) {
        Mask seen = 0;
        for (int c : kT.unit[u]) {
            if (!g[c]) continue;
            if (g[c] > 9) return false;
            Mask b = digitBit(g[c]);
            if (seen & b) return false;
            seen |= b;
        }
    }
    return true;
}

bool isCompleteSolution(const Grid& g) {
    for (uint8_t v : g)
        if (v < 1 || v > 9) return false;
    return isConsistent(g);
}

int countFilled(const Grid& g) {
    int n = 0;
    for (uint8_t v : g) n += v != 0;
    return n;
}

std::string cellName(int c) {
    std::string s = "R0C0";
    s[1] = char('1' + rowOf(c));
    s[3] = char('1' + colOf(c));
    return s;
}

std::string unitName(int u) {
    if (isRowUnit(u)) return "row " + std::to_string(u + 1);
    if (isColUnit(u)) return "column " + std::to_string(u - 8);
    return "box " + std::to_string(u - 17);
}

std::string digitList(Mask m) {
    std::string s;
    int n = bitCount(m), i = 0;
    forEachDigit(m, [&](int d) {
        if (i > 0) s += (i == n - 1) ? " and " : ", ";
        s += char('0' + d);
        ++i;
    });
    return s;
}

}  // namespace sudoku
