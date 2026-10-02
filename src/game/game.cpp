#include "game.h"

#include <charconv>

namespace sudoku {
namespace {

constexpr size_t kMaxUndo = 2000;
constexpr char kHex[] = "0123456789abcdef";

void putHex(std::string& out, unsigned v, int digits) {
    for (int i = digits - 1; i >= 0; --i) out += kHex[(v >> (i * 4)) & 0xF];
}

bool getHex(std::string_view s, size_t pos, int digits, unsigned& v) {
    if (pos + size_t(digits) > s.size()) return false;
    v = 0;
    for (int i = 0; i < digits; ++i) {
        const char ch = s[pos + size_t(i)];
        unsigned n;
        if (ch >= '0' && ch <= '9') n = unsigned(ch - '0');
        else if (ch >= 'a' && ch <= 'f') n = unsigned(ch - 'a' + 10);
        else return false;
        v = (v << 4) | n;
    }
    return true;
}

template <class T>
bool parseInt(std::string_view s, T& out) {
    return std::from_chars(s.data(), s.data() + s.size(), out).ec == std::errc{};
}

void encodeStack(std::string& out, const std::vector<std::vector<CellChange>>& stack) {
    const size_t start = stack.size() > 500 ? stack.size() - 500 : 0;
    for (size_t i = start; i < stack.size(); ++i) {
        if (i > start) out += ';';
        for (const CellChange& c : stack[i]) {
            putHex(out, c.cell, 2);
            putHex(out, c.oldValue, 1);
            putHex(out, c.newValue, 1);
            putHex(out, c.oldNotes, 3);
            putHex(out, c.newNotes, 3);
        }
    }
}

bool decodeStack(std::string_view s, std::vector<std::vector<CellChange>>& stack) {
    stack.clear();
    if (s.empty()) return true;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find(';', pos);
        if (end == std::string_view::npos) end = s.size();
        const std::string_view entry = s.substr(pos, end - pos);
        if (entry.size() % 10 != 0 || entry.empty()) return false;
        std::vector<CellChange> changes;
        for (size_t i = 0; i < entry.size(); i += 10) {
            unsigned cell, ov, nv, on, nn;
            if (!getHex(entry, i, 2, cell) || !getHex(entry, i + 2, 1, ov) || !getHex(entry, i + 3, 1, nv) ||
                !getHex(entry, i + 4, 3, on) || !getHex(entry, i + 7, 3, nn))
                return false;
            if (cell > 80 || ov > 9 || nv > 9 || on > kAllDigits || nn > kAllDigits) return false;
            changes.push_back({uint8_t(cell), uint8_t(ov), uint8_t(nv), Mask(on), Mask(nn)});
        }
        stack.push_back(std::move(changes));
        pos = end + 1;
    }
    return true;
}

}  // namespace

// ------------------------------------------------------------------ setup

void Game::start(const Puzzle& p, GameMode m, int date, int64_t nowUtc) {
    clear();
    mode = m;
    difficulty = p.difficulty;
    dailyDate = m == GameMode::Daily ? date : 0;
    givens = p.givens;
    solution = p.solution;
    values = p.givens;
    active = true;
    startedUtc = nowUtc;
}

void Game::restart() {
    values = givens;
    notes = {};
    elapsedMs = 0;
    mistakes = 0;
    hintsUsed = 0;
    moves = 0;
    extraLives = 0;
    autoNotesUsed = false;
    status = GameStatus::Playing;
    undo_.clear();
    redo_.clear();
}

void Game::clear() {
    *this = Game{};
}

// ---------------------------------------------------------------- queries

bool Game::isLocked(int c, const Rules& r) const {
    return givens[c] != 0 || (r.checkMistakes && values[c] != 0 && values[c] == solution[c]);
}

int Game::filledCount() const { return countFilled(values); }

int Game::placedCount(int d, const Rules& r) const {
    int n = 0;
    for (int c = 0; c < 81; ++c)
        if (values[c] == d && (!r.checkMistakes || solution[c] == d)) ++n;
    return n;
}

Mask Game::candidatesAt(int c) const {
    Mask used = 0;
    for (int p : kT.peers[c])
        if (values[p]) used |= digitBit(values[p]);
    return Mask(kAllDigits & ~used);
}

CellSet Game::conflicts() const {
    CellSet out;
    for (int c = 0; c < 81; ++c) {
        if (!values[c]) continue;
        for (int p : kT.peers[c]) {
            if (values[p] == values[c]) {
                out.set(c);
                break;
            }
        }
    }
    return out;
}

uint32_t Game::completeUnits(const Rules& r) const {
    uint32_t out = 0;
    for (int u = 0; u < 27; ++u) {
        bool done = true;
        Mask seen = 0;
        for (int c : kT.unit[u]) {
            const int v = values[c];
            if (!v || (r.checkMistakes && v != solution[c]) || (seen & digitBit(v))) {
                done = false;
                break;
            }
            seen |= digitBit(v);
        }
        if (done) out |= 1u << u;
    }
    return out;
}

Mask Game::completeDigits(const Rules& r) const {
    Mask out = 0;
    for (int d = 1; d <= 9; ++d)
        if (placedCount(d, r) == 9) out |= digitBit(d);
    return out;
}

// ---------------------------------------------------------------- actions

void Game::apply(const std::vector<CellChange>& changes, bool forward) {
    if (forward) {
        for (const CellChange& ch : changes) {
            values[ch.cell] = ch.newValue;
            notes[ch.cell] = ch.newNotes;
        }
    } else {
        for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
            values[it->cell] = it->oldValue;
            notes[it->cell] = it->oldNotes;
        }
    }
}

void Game::commit(std::vector<CellChange>&& changes) {
    redo_.clear();
    undo_.push_back(std::move(changes));
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
}

MoveResult Game::setValue(int c, int d, const Rules& r) {
    MoveResult res;
    if (!isPlaying() || c < 0 || c > 80 || d < 1 || d > 9) return res;
    if (isLocked(c, r)) {
        res.blocked = true;
        return res;
    }
    if (values[c] == d) return res;
    const uint32_t unitsBefore = completeUnits(r);
    const Mask digitsBefore = completeDigits(r);
    const bool wrong = d != solution[c];

    std::vector<CellChange> ch;
    ch.push_back({uint8_t(c), values[c], uint8_t(d), notes[c], 0});
    if (r.autoRemoveNotes && !(r.checkMistakes && wrong)) {
        const Mask b = digitBit(d);
        for (int p : kT.peers[c])
            if (!values[p] && (notes[p] & b)) ch.push_back({uint8_t(p), 0, 0, notes[p], Mask(notes[p] & ~b)});
    }
    apply(ch, true);
    commit(std::move(ch));
    ++moves;
    res.changed = res.placed = true;

    if (r.checkMistakes && wrong) {
        ++mistakes;
        res.mistake = true;
        if (r.mistakeLimit > 0 && mistakes >= mistakeAllowance(r)) {
            status = GameStatus::Lost;
            res.lost = true;
        }
    }
    res.completedUnits = completeUnits(r) & ~unitsBefore;
    res.completedDigits = Mask(completeDigits(r) & ~digitsBefore);
    if (values == solution) {
        status = GameStatus::Won;
        res.won = true;
    } else if (filledCount() == 81) {
        res.fullWithErrors = true;
    }
    return res;
}

MoveResult Game::toggleNote(int c, int d) {
    MoveResult res;
    if (!isPlaying() || c < 0 || c > 80 || d < 1 || d > 9 || values[c]) return res;
    std::vector<CellChange> ch{{uint8_t(c), 0, 0, notes[c], Mask(notes[c] ^ digitBit(d))}};
    apply(ch, true);
    commit(std::move(ch));
    ++moves;
    res.changed = true;
    return res;
}

MoveResult Game::erase(int c, const Rules& r) {
    MoveResult res;
    if (!isPlaying() || c < 0 || c > 80) return res;
    if (isLocked(c, r)) {
        res.blocked = true;
        return res;
    }
    if (!values[c] && !notes[c]) return res;
    std::vector<CellChange> ch;
    if (values[c]) ch.push_back({uint8_t(c), values[c], 0, notes[c], notes[c]});
    else ch.push_back({uint8_t(c), 0, 0, notes[c], 0});
    apply(ch, true);
    commit(std::move(ch));
    ++moves;
    res.changed = true;
    return res;
}

MoveResult Game::fillAllNotes() {
    MoveResult res;
    if (!isPlaying()) return res;
    std::vector<CellChange> ch;
    for (int c = 0; c < 81; ++c) {
        if (values[c]) continue;
        const Mask n = candidatesAt(c);
        if (n != notes[c]) ch.push_back({uint8_t(c), 0, 0, notes[c], n});
    }
    if (ch.empty()) return res;
    apply(ch, true);
    commit(std::move(ch));
    ++moves;
    autoNotesUsed = true;
    res.changed = true;
    return res;
}

MoveResult Game::clearAllNotes() {
    MoveResult res;
    if (!isPlaying()) return res;
    std::vector<CellChange> ch;
    for (int c = 0; c < 81; ++c)
        if (notes[c]) ch.push_back({uint8_t(c), values[c], values[c], notes[c], 0});
    if (ch.empty()) return res;
    apply(ch, true);
    commit(std::move(ch));
    ++moves;
    res.changed = true;
    return res;
}

MoveResult Game::undo() {
    MoveResult res;
    if (!isPlaying() || undo_.empty()) return res;
    std::vector<CellChange> ch = std::move(undo_.back());
    undo_.pop_back();
    apply(ch, false);
    redo_.push_back(std::move(ch));
    ++moves;
    res.changed = true;
    return res;
}

MoveResult Game::redo() {
    MoveResult res;
    if (!isPlaying() || redo_.empty()) return res;
    std::vector<CellChange> ch = std::move(redo_.back());
    redo_.pop_back();
    apply(ch, true);
    undo_.push_back(std::move(ch));
    ++moves;
    res.changed = true;
    return res;
}

void Game::grantSecondChance() {
    ++extraLives;
    status = GameStatus::Playing;
}

// ------------------------------------------------------------------ hints

Hint Game::computeHint() const {
    Hint h;
    if (!isPlaying()) return h;
    for (int c = 0; c < 81; ++c) {
        if (isWrong(c)) {
            h.kind = HintKind::Mistake;
            h.cell = c;
            h.text.title = "Check this cell";
            h.text.nudge = "There's a mistake on the board.";
            h.text.detail = "The " + std::string(1, char('0' + values[c])) + " in " + cellName(c) +
                            " doesn't belong there. Remove it before going further.";
            return h;
        }
    }
    for (int c = 0; c < 81; ++c) {
        if (!values[c] && notes[c] && !(notes[c] & digitBit(solution[c]))) {
            h.kind = HintKind::BadNotes;
            h.cell = c;
            h.text.title = "Check your notes";
            h.text.nudge = "One of your cells has notes that rule out its real digit.";
            h.text.detail = "The notes in " + cellName(c) +
                            " don't include the digit that belongs there. Applying this hint resets them to "
                            "every digit that is still possible.";
            return h;
        }
    }
    LogicGrid g = LogicGrid::fromGrid(values);
    for (int c = 0; c < 81; ++c)
        if (!values[c] && notes[c]) g.cand[c] &= notes[c];
    if (findStep(g, h.step)) {
        h.kind = HintKind::Logic;
        h.text = explain(h.step);
        h.cell = h.step.placeCell >= 0 ? h.step.placeCell : (h.step.cells.empty() ? -1 : h.step.cells.first());
        return h;
    }
    int best = -1, bestN = 10;
    for (int c = 0; c < 81; ++c) {
        if (values[c]) continue;
        const int n = bitCount(g.cand[c]);
        if (n < bestN) {
            bestN = n;
            best = c;
        }
    }
    if (best >= 0) {
        h.kind = HintKind::Reveal;
        h.cell = best;
        h.text.title = "Reveal a cell";
        h.text.nudge = "None of the logical techniques make progress here.";
        h.text.detail = "This position would need trial and error. Applying this hint reveals " + cellName(best) + ".";
    }
    return h;
}

MoveResult Game::applyHint(const Hint& h, const Rules& r) {
    MoveResult res;
    if (!isPlaying()) return res;
    switch (h.kind) {
    case HintKind::Mistake:
        if (isWrong(h.cell)) {
            std::vector<CellChange> ch{{uint8_t(h.cell), values[h.cell], 0, notes[h.cell], notes[h.cell]}};
            apply(ch, true);
            commit(std::move(ch));
            ++moves;
            res.changed = true;
        }
        return res;
    case HintKind::BadNotes: {
        const Mask n = candidatesAt(h.cell);
        std::vector<CellChange> ch{{uint8_t(h.cell), 0, 0, notes[h.cell], n}};
        apply(ch, true);
        commit(std::move(ch));
        ++moves;
        res.changed = true;
        return res;
    }
    case HintKind::Reveal:
        return setValue(h.cell, solution[h.cell], r);
    case HintKind::Logic: {
        const Step& s = h.step;
        if (s.placeCell >= 0) return setValue(s.placeCell, s.placeDigit, r);
        CellSet involved = s.cells;
        if (s.tech != Tech::ForcingChain) involved |= s.cells2;
        for (int c = 0; c < 81; ++c)
            if (s.elim[c]) involved.set(c);
        std::vector<CellChange> ch;
        involved.forEach([&](int c) {
            if (values[c]) return;
            const Mask base = notes[c] ? notes[c] : candidatesAt(c);
            const Mask nn = Mask(base & ~s.elim[c]);
            if (nn != notes[c]) ch.push_back({uint8_t(c), 0, 0, notes[c], nn});
        });
        if (ch.empty()) return res;
        apply(ch, true);
        commit(std::move(ch));
        ++moves;
        res.changed = true;
        return res;
    }
    case HintKind::None: break;
    }
    return res;
}

// ---------------------------------------------------------- serialization

std::string Game::serialize() const {
    std::string out;
    out.reserve(4096);
    out += "sudoku-save 1\n";
    out += "mode=" + std::string(mode == GameMode::Daily ? "daily" : "classic") + "\n";
    out += "difficulty=" + std::to_string(int(difficulty)) + "\n";
    out += "daily=" + std::to_string(dailyDate) + "\n";
    out += "givens=" + toString(givens) + "\n";
    out += "solution=" + toString(solution) + "\n";
    out += "values=" + toString(values) + "\n";
    out += "notes=";
    for (Mask m : notes) putHex(out, m, 3);
    out += "\n";
    out += "elapsed=" + std::to_string(elapsedMs) + "\n";
    out += "mistakes=" + std::to_string(mistakes) + "\n";
    out += "hints=" + std::to_string(hintsUsed) + "\n";
    out += "moves=" + std::to_string(moves) + "\n";
    out += "lives=" + std::to_string(extraLives) + "\n";
    out += "autonotes=" + std::to_string(autoNotesUsed ? 1 : 0) + "\n";
    out += "status=" + std::to_string(int(status)) + "\n";
    out += "started=" + std::to_string(startedUtc) + "\n";
    out += "undo=";
    encodeStack(out, undo_);
    out += "\nredo=";
    encodeStack(out, redo_);
    out += "\n";
    return out;
}

bool Game::deserialize(std::string_view text) {
    Game g;
    bool header = false, haveGivens = false, haveSolution = false, haveValues = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line == "sudoku-save 1") {
            header = true;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = line.substr(0, eq), val = line.substr(eq + 1);
        int iv = 0;
        if (key == "mode") g.mode = val == "daily" ? GameMode::Daily : GameMode::Classic;
        else if (key == "difficulty" && parseInt(val, iv) && iv >= 0 && iv < kDifficultyCount) g.difficulty = Difficulty(iv);
        else if (key == "daily") parseInt(val, g.dailyDate);
        else if (key == "givens") haveGivens = fromString(val, g.givens);
        else if (key == "solution") haveSolution = fromString(val, g.solution);
        else if (key == "values") haveValues = fromString(val, g.values);
        else if (key == "notes") {
            if (val.size() != 81 * 3) return false;
            for (int c = 0; c < 81; ++c) {
                unsigned m;
                if (!getHex(val, size_t(c) * 3, 3, m) || m > kAllDigits) return false;
                g.notes[c] = Mask(m);
            }
        } else if (key == "elapsed") parseInt(val, g.elapsedMs);
        else if (key == "mistakes") parseInt(val, g.mistakes);
        else if (key == "hints") parseInt(val, g.hintsUsed);
        else if (key == "moves") parseInt(val, g.moves);
        else if (key == "lives") parseInt(val, g.extraLives);
        else if (key == "autonotes") g.autoNotesUsed = val == "1";
        else if (key == "status" && parseInt(val, iv) && iv >= 0 && iv <= 2) g.status = GameStatus(iv);
        else if (key == "started") parseInt(val, g.startedUtc);
        else if (key == "undo") {
            if (!decodeStack(val, g.undo_)) g.undo_.clear();
        } else if (key == "redo") {
            if (!decodeStack(val, g.redo_)) g.redo_.clear();
        }
    }
    if (!header || !haveGivens || !haveSolution || !haveValues) return false;
    if (!isCompleteSolution(g.solution)) return false;
    for (int c = 0; c < 81; ++c)
        if (g.givens[c] && (g.givens[c] != g.solution[c] || g.values[c] != g.givens[c])) return false;
    g.active = true;
    *this = std::move(g);
    return true;
}

}  // namespace sudoku
