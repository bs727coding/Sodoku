#include "ui/board_view.h"

#include <algorithm>
#include <cmath>

#include "ui/anim.h"

namespace sudoku::ui {
namespace {

constexpr double kPopDur = 0.22, kShakeDur = 0.42, kWashDur = 0.6, kWinDur = 0.55, kWinStagger = 0.05;

bool sameColor(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

float washAmount(const BoardAnims& an, int c, double now) {
    float w = 0;
    const float t = progress(now, an.wash[c], kWashDur);
    if (t >= 0) w = bump(t);
    if (an.win >= 0) {
        const float dx = float(colOf(c) - colOf(an.winOrigin)), dy = float(rowOf(c) - rowOf(an.winOrigin));
        const double start = an.win + std::sqrt(dx * dx + dy * dy) * kWinStagger;
        const float tw = progress(now, start, kWinDur);
        if (tw >= 0) w = std::max(w, bump(tw));
    }
    return w;
}

}  // namespace

int BoardGeom::hit(float px, float py) const {
    const float rx = px - x, ry = py - y;
    if (rx < 0 || ry < 0 || rx >= float(size) || ry >= float(size)) return -1;
    int col = 8, row = 8;
    for (int i = 1; i < 9; ++i) {
        if (rx < float(pos[i])) {
            col = i - 1;
            break;
        }
    }
    for (int i = 1; i < 9; ++i) {
        if (ry < float(pos[i])) {
            row = i - 1;
            break;
        }
    }
    return cellAt(row, col);
}

D2D1_POINT_2F BoardGeom::candidateCenter(int c, int d) const {
    const Rect r = cellRect(c);
    const float pad = std::round(float(cell) * 0.06f), sub = (float(cell) - 2 * pad) / 3.0f;
    const int i = d - 1;
    return {r.x + pad + sub * (float(i % 3) + 0.5f), r.y + pad + sub * (float(i / 3) + 0.5f)};
}

BoardGeom layoutBoard(const Rect& area, float s) {
    BoardGeom g;
    g.thin = std::max(1, int(std::round(1.0f * s)));
    g.thick = std::max(2, int(std::round(2.0f * s)));
    const int avail = int(std::floor(std::min(area.w, area.h)));
    g.cell = std::max(10, (avail - 4 * g.thick - 6 * g.thin) / 9);
    int p = g.thick;
    for (int i = 0; i < 9; ++i) {
        g.pos[i] = p;
        p += g.cell + ((i % 3 == 2) ? g.thick : g.thin);
    }
    g.pos[9] = p;
    g.size = p;
    g.x = std::round(area.cx() - float(g.size) * 0.5f);
    g.y = std::round(area.cy() - float(g.size) * 0.5f);
    return g;
}

bool BoardAnims::active(double now) const {
    for (int c = 0; c < 81; ++c) {
        if (pop[c] >= 0 && now - pop[c] < kPopDur) return true;
        if (shake[c] >= 0 && now - shake[c] < kShakeDur) return true;
        if (wash[c] >= 0 && now - wash[c] < kWashDur) return true;
    }
    return win >= 0 && now - win < kWinDur + 12 * kWinStagger;
}

void BoardView::ensureLayouts(Renderer& r, int cell, float) {
    if (cell == layoutCell_) return;
    layoutCell_ = cell;
    const float bigSize = std::round(float(cell) * 0.60f);
    const float pad = std::round(float(cell) * 0.06f);
    const float sub = (float(cell) - 2 * pad) / 3.0f;
    const float noteSize = std::max(7.0f, std::round(sub * 0.80f));
    const TextStyle bigStyles[2] = {{Font::Display, bigSize, DWRITE_FONT_WEIGHT_SEMI_BOLD},
                                    {Font::Display, bigSize, DWRITE_FONT_WEIGHT_NORMAL}};
    const TextStyle noteStyles[2] = {{Font::Text, noteSize, DWRITE_FONT_WEIGHT_NORMAL},
                                     {Font::Text, noteSize, DWRITE_FONT_WEIGHT_SEMI_BOLD}};
    const DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    auto make = [&](ComPtr<IDWriteTextLayout>& out, const TextStyle& st, float box, int d) {
        out.Reset();
        IDWriteTextFormat* f = r.format(st);
        const wchar_t ch = wchar_t(L'0' + d);
        if (!f || FAILED(r.dwrite()->CreateTextLayout(&ch, 1, f, box, box, &out))) return;
        out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        out->SetTrimming(&none, nullptr);
        // Put the baseline so that the digit's cap height is centred vertically in the box.
        const float cap = r.capHeight(st.font) * st.size;
        out->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, box, std::round(box * 0.5f + cap * 0.5f));
    };
    for (int d = 1; d <= 9; ++d) {
        for (int k = 0; k < 2; ++k) {
            make(big_[k][d], bigStyles[k], float(cell), d);
            make(note_[k][d], noteStyles[k], sub, d);
        }
    }
}

void BoardView::drawDigit(Renderer& r, IDWriteTextLayout* layout, float x, float y, Color c) {
    if (layout) r.dc()->DrawTextLayout(D2D1::Point2F(x, y), layout, r.brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void BoardView::draw(Ui& ui, const BoardGeom& g, const BoardState& st) {
    Renderer& r = *ui.r;
    const Palette& p = ui.pal;
    const Game& game = *st.game;
    const Settings& set = *st.settings;
    const Rules rules = set.rules();
    const double now = ui.time;
    ensureLayouts(r, g.cell, ui.scale);

    const Rect bounds = g.bounds();
    const float radius = std::round(ui.dp(8));
    const bool show = !st.paused;
    const float pad = std::round(float(g.cell) * 0.06f);
    const float sub = (float(g.cell) - 2 * pad) / 3.0f;

    // ---- hint highlight sets
    const Hint* h = (st.hint && show) ? st.hint : nullptr;
    const bool logic = h && h->kind == HintKind::Logic;
    const bool detail = h && st.hintStage >= 1;
    CellSet hintCells, hintCells2;
    uint32_t hintUnits = 0;
    if (logic) {
        hintUnits = detail ? h->step.units
                           : (h->step.tech == Tech::NakedSingle ? (1u << h->step.unit) : h->step.units);
        if (detail) {
            hintCells = h->step.cells;
            hintCells2 = h->step.cells2;
        }
    } else if (h && detail && h->cell >= 0) {
        hintCells.set(h->cell);
    }

    const int sel = st.selected;
    int hiDigit = st.digitLock;
    if (!hiDigit && set.highlightSame && sel >= 0) hiDigit = game.values[sel];
    const CellSet conflicts = (show && set.highlightConflicts) ? game.conflicts() : CellSet{};

    r.fillRound(bounds, radius, p.board);

    // Corner cells follow the board's rounded corners: a rounded fill clipped to the cell
    // (an axis-aligned clip is free, unlike a layer).
    const float innerR = std::max(0.0f, radius - float(g.thick));
    auto fillCell = [&](int c, Color col) {
        const Rect cr = g.cellRect(c);
        const int row = rowOf(c), cl = colOf(c);
        if ((row == 0 || row == 8) && (cl == 0 || cl == 8) && innerR > 0) {
            const Rect big{cl == 0 ? cr.x : cr.x - innerR, row == 0 ? cr.y : cr.y - innerR, cr.w + innerR, cr.h + innerR};
            r.pushClip(cr);
            r.fillRound(big, innerR, col);
            r.popClip();
        } else {
            r.fill(cr, col);
        }
    };

    // ---- cell backgrounds
    for (int c = 0; c < 81; ++c) {
        Color bg = p.board;
        if (show) {
            if (sel >= 0 && set.highlightRegion && sees(sel, c)) bg = p.cellPeer;
            if (hintUnits) {
                for (int u : kT.unitsOf[c]) {
                    if (hintUnits >> u & 1) {
                        bg = p.unitHint;
                        break;
                    }
                }
            }
            if (hiDigit && game.values[c] == hiDigit) bg = p.cellSame;
            if (hintCells2.has(c)) bg = p.cellHint2;
            if (hintCells.has(c)) bg = p.cellHint;
            if (conflicts.has(c) || (st.revealWrong && game.isWrong(c))) bg = p.cellConflict;
            if (c == sel) bg = p.cellSelected;
        }
        if (st.anims) {
            const float w = washAmount(*st.anims, c, now);
            if (w > 0) bg = mix(bg, withAlpha(p.accent, 1.0f), 0.40f * w);
        }
        if (!sameColor(bg, p.board)) fillCell(c, bg);
    }

    // ---- grid lines (aliased, on exact pixels)
    r.dc()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 1; i < 9; ++i) {
            const bool box = i % 3 == 0;
            if (box != (pass == 1)) continue;
            const int w = box ? g.thick : g.thin;
            const float off = float(g.pos[i] - w);
            const Color col = box ? p.boardBox : p.boardLine;
            r.fill({g.x + off, g.y, float(w), float(g.size)}, col);
            r.fill({g.x, g.y + off, float(g.size), float(w)}, col);
        }
    }
    r.dc()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (show) {
        // ---- digits and notes
        const Mask hintDigits = (logic && detail) ? h->step.digits : Mask(0);
        const CellSet pattern = (logic && detail) ? (h->step.cells | h->step.cells2) : CellSet{};
        for (int c = 0; c < 81; ++c) {
            const Rect cr = g.cellRect(c);
            const int v = game.values[c];
            if (v) {
                const bool given = game.isGiven(c);
                Color col = given ? p.given : p.entry;
                // With instant checking only truly wrong digits turn red; otherwise conflicts do.
                const bool flagged = rules.checkMistakes ? game.isWrong(c) : conflicts.has(c);
                if (!given && (flagged || (st.revealWrong && game.isWrong(c)))) col = p.wrong;
                float scale = 1.0f, dx = 0.0f;
                if (st.anims) {
                    const float tp = progress(now, st.anims->pop[c], kPopDur);
                    if (tp >= 0) scale = 0.55f + 0.45f * easeOutBack(tp);
                    const float ts = progress(now, st.anims->shake[c], kShakeDur);
                    if (ts >= 0) dx = std::sin(ts * kPi * 5.0f) * (1.0f - ts) * float(g.cell) * 0.09f;
                }
                if (scale != 1.0f)
                    r.setTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(cr.cx(), cr.cy())));
                drawDigit(r, big_[given ? 0 : 1][v].Get(), cr.x + dx, cr.y, col);
                if (scale != 1.0f) r.resetTransform();
                continue;
            }
            Mask shown = game.notes[c];
            Mask emph = 0;
            if (pattern.has(c)) {
                const Mask cand = shown ? shown : game.candidatesAt(c);
                emph = cand & hintDigits;
                shown |= emph;
            }
            if (hiDigit) emph |= shown & digitBit(hiDigit);
            const Mask elim = (logic && detail) ? h->step.elim[c] : Mask(0);
            for (Mask m = shown | elim; m; m &= m - 1) {
                const int d = lowestDigit(m);
                const D2D1_POINT_2F pc = g.candidateCenter(c, d);
                const float bx = pc.x - sub * 0.5f, by = pc.y - sub * 0.5f;
                if (elim & digitBit(d)) {
                    r.fillEllipse(pc.x, pc.y, sub * 0.46f, sub * 0.46f, withAlpha(p.critical, 0.16f));
                    drawDigit(r, note_[1][d].Get(), bx, by, p.critical);
                    r.line(pc.x - sub * 0.34f, pc.y + sub * 0.34f, pc.x + sub * 0.34f, pc.y - sub * 0.34f, p.critical,
                           std::max(1.0f, ui.dp(1.25f)));
                } else if (emph & digitBit(d)) {
                    drawDigit(r, note_[1][d].Get(), bx, by, p.accent);
                } else {
                    drawDigit(r, note_[0][d].Get(), bx, by, p.note);
                }
            }
        }

        // ---- placement preview for the explained hint
        if (h && detail) {
            int pcell = -1, pdig = 0;
            if (logic && h->step.placeCell >= 0) {
                pcell = h->step.placeCell;
                pdig = h->step.placeDigit;
            } else if (h->kind == HintKind::Reveal && h->cell >= 0) {
                pcell = h->cell;
                pdig = game.solution[h->cell];
            }
            if (pcell >= 0 && !game.values[pcell]) {
                const Rect cr = g.cellRect(pcell);
                fillCell(pcell, p.cellHint2);
                drawDigit(r, big_[1][pdig].Get(), cr.x, cr.y, p.success);
            }
        }

        // ---- chain links
        if (logic && detail && h->step.chain.size() >= 2) {
            const Tech t = h->step.tech;
            const bool chainTech = t == Tech::XChain || t == Tech::XYChain || t == Tech::Skyscraper ||
                                   t == Tech::TwoStringKite || t == Tech::WWing;
            if (chainTech) {
                const auto& ch = h->step.chain;
                const float lw = std::max(1.0f, ui.dp(1.6f));
                for (size_t i = 0; i + 1 < ch.size(); ++i) {
                    const int d0 = ch[i].digit, d1 = t == Tech::XYChain ? ch[i].digit : ch[i + 1].digit;
                    const D2D1_POINT_2F a = g.candidateCenter(ch[i].cell, d0);
                    const D2D1_POINT_2F b = g.candidateCenter(ch[i + 1].cell, d1);
                    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1) continue;
                    const float k = sub * 0.42f / len;
                    bool strong = i % 2 == 0;
                    if (t == Tech::WWing) strong = i == 1;
                    if (t == Tech::XYChain) strong = false;
                    r.line(a.x + dx * k, a.y + dy * k, b.x - dx * k, b.y - dy * k, p.accent, lw, !strong);
                }
                for (size_t i = 0; i < ch.size(); ++i) {
                    const D2D1_POINT_2F a = g.candidateCenter(ch[i].cell, ch[i].digit);
                    r.strokeEllipse(a.x, a.y, sub * 0.44f, sub * 0.44f, p.accent, std::max(1.0f, ui.dp(1.2f)));
                }
            }
        }

        // ---- selection ring
        if (sel >= 0) {
            const float w = std::max(2.0f, std::round(ui.dp(2)));
            const bool corner = (rowOf(sel) == 0 || rowOf(sel) == 8) && (colOf(sel) == 0 || colOf(sel) == 8);
            r.strokeRound(g.cellRect(sel), corner ? std::max(ui.dp(2), innerR) : ui.dp(2), p.accent, w);
        }
    }

    r.strokeRound(bounds, radius, p.boardBox, float(g.thick));
}

}  // namespace sudoku::ui
