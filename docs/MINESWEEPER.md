# Minesweeper variant — design

Status: **design only, nothing implemented.** Decisions already made are stated as rules. Anything still open is in §9.

## 1. The idea in one paragraph

(Terminology: a **layer** is one of the two puzzles on the board. The *digit layer* is the sudoku part: what number is
in each cell. The *mine layer* is the minesweeper part: which cells are bombs. Each cell has an answer in both.)

The board is a normal, complete sudoku-but puzzle: every cell holds a digit. On top of it sits a **second layer**:
some cells are **mines** (bombs). A mine still holds its digit; being a mine is just an extra property. Some **given
digits are drawn in a colour**, and a coloured digit is a **hint**: its number is the count of mines among its 8
surrounding cells. The player solves both layers: fills in digits and **marks** the bombs. The hint digit is the bridge
between the layers: one given number that is both a sudoku digit and a mine count.

Why that is a good design:
- No new symbol set. The player already reads digits; colour only says "this one also counts mines".
- A hint can never be 0 or 9 (digits are 1–9, a cell has at most 8 neighbours), so every hint is informative
  (at least one adjacent mine) and there is no flood-fill opening to simulate.
- Hints are free sudoku givens, so the mine layer also *helps* the digit puzzle.
- It reuses the house pattern: one more clue type whose target is read off the solution.

## 2. Rules (decided)

1. Digit rules are unchanged and apply to **every** cell (rows, columns, boxes all-different, plus sum/sandwich lines
   when a level has them). Mines do not change the digit puzzle.
2. A **coloured given digit** `d` at cell `h` is a hint: exactly `d` of the 8 cells surrounding `h` are mines. The hint
   cell itself is never a mine. Hints are always given digits; a blank cell cannot be a hint, because the number *is*
   the clue.
3. **Any other cell can be a mine**, including cells with given digits.
4. The colour encodes the hint's **shape**. Only "8 surrounding cells" exists now, but later colours can mean other
   neighbourhoods (orthogonal-only, knight-move for the chess rule, a row or column) with no new UI concept. Colour is
   never the only signal (§7).
5. Every level that has minesweeper mechanics shows a **mine counter**.
6. There is **one mark**: the player marks a cell as a bomb, and bomb and flag are the same thing (use whichever name
   reads better in the UI).
7. The final check only requires that **every bomb is marked**. Combined with the always-visible counter this is exact:
   marking more cells than the counter allows is not a valid solution, so "mark everything" cannot win.
8. **Checking is manual.** The player can press check at any time; nothing is checked automatically. Check compares the
   current digits and bomb marks with the solution and reports what is wrong (a wrong digit, or a mark on a non-mine),
   without revealing the answers. Missing entries are not errors.
9. Uniqueness and "no guessing" hold as before, for both layers, verified by an independent brute-force solver.

## 3. Marks

A mark sits **next to** a cell's digit and pencil marks, not instead of them. One tap toggles it. The counter shows
`mines − marks`, so a negative number is the player's cue that something is wrong. Hints are safe by rule 2, so they
cannot be marked.

## 4. Data model additions (still Arche-shaped)

Mine status is **independent of the digit**, so it gets its own columns rather than sharing the digit bitmask.

```
Cell columns (new)   cell_mine[]        solution: 1 if the cell is a mine
                     cell_mine_cand[]   2-bit set: bit0 "may be safe", bit1 "may be a mine"; 3 = unknown
                     cell_hint[]        0 = plain digit, else the hint shape (the colour)
                     cell_mark[]        player mark: bomb or not   (UI state, not solver state)
Rule rows    (new)   RULE_MINE_HINT   (kind, axis = AXIS_CELL, line = centre cell, param = shape)
                     RULE_MINE_TOTAL  (kind, param = number of mines on the board)
```

- A **hint is a rule over a computed neighbourhood**, like a row is: `func_shape_cell(shape, centre, k, w, h)` returns the
  k-th neighbour or -1, mirroring `func_unit_cell`. Still **no membership join table**; joins stay reserved for
  arbitrary regions (killer cages, thermometers).
- The hint's target is `cell_sol[centre]`, so the existing "targets come from the solution" pattern holds.
- `cell_val` and `cell_cand` are untouched. Unit lengths stay at 9 for boxes, so `LAST_DIGIT` and `HIDDEN_SINGLE`
  keep working unchanged.

## 5. Solver techniques (new, ranked after the existing ladder)

All are local to one hint; none needs a hypothetical.

| Technique | Fires when | Effect |
|---|---|---|
| `MINE_SATISFIED` | a hint already has `d` known mines around it | its other unknown neighbours are safe |
| `MINE_FULL` | unknown neighbours == mines still needed (a corner hint 3, an edge hint 5, an 8 anywhere inside) | all unknown neighbours are mines |
| `MINE_TOTAL` | the counter is met, or the rest must all be mines | same, board-wide |
| `MINE_SUBSET` | neighbourhood of hint A is a subset of hint B's | the difference holds `d_B − d_A` mines (the classic 1-2 pattern) |

Since hints are always given, the mine layer needs nothing from the digit layer and vice versa: **the two layers share
only the hint cells.** That is a clean separation (each layer's solver is independent), but it means the interesting
cross-layer play comes from the *shape of the puzzle* rather than from deductions. That is intentional (§9).

Difficulty is the same metric as before (highest rank, step count, number of simultaneously available techniques).

## 6. Generator: work backwards

The mine count is an **output** of generation, not a setting.

1. Build the full digit solution exactly as today (including any sum/sandwich rules).
2. Pick a random mine set (any density the level's range allows).
3. **Derive the hints backwards:** every non-mine cell whose digit `d` (1–8) equals its number of mined neighbours is a
   *candidate* hint. If a level wants more candidates, nudge the mine set locally (flip a mine on or off next to a cell
   whose count is off by one) and re-derive; keep the best set found.
4. Colour a subset of the candidate hints and write `RULE_MINE_HINT` rows, plus `RULE_MINE_TOTAL` (the counter) from the
   chosen mine set.
5. Remove givens as today, and also thin out the coloured hints, while the ceiling-limited solver still finishes both
   layers. Hint digits count as sudoku givens, so removal treats them as one pool.
6. The level's teaching technique must appear in the trace.

The brute-force oracle in the selftest counts digit solutions and mine assignments separately (the layers are
independent given the hints) and requires exactly one of each.

## 7. UI notes

- **Colour is not enough on its own.** Pair each hint colour with a second cue: a ring or corner dot whose shape
  matches the neighbourhood, with a colour-blind-safe palette. Selecting a hint **outlines its neighbourhood**, which
  doubles as the tutorial.
- Tap a cell, then pick a digit (as now) or the **bomb** button. Digit and bomb coexist in one cell, so the cell needs
  room for the big digit, pencil marks and a small bomb badge.
- A hint/check explains the next step in terms of the hint it came from ("this 3 already touches 3 bombs, so its other
  neighbours are safe").
- The mine counter is always in the header on minesweeper levels (rule 5).

## 8. Curriculum for the variant

Introduced after sums/sandwiches so each concept arrives alone.

| Step | Grid | Idea |
|---|---|---|
| M0 | 3×3 | one coloured corner **3** (3 neighbours): all three are bombs (`MINE_FULL`). Teaches the dual role and the counter. |
| M1 | 6×6 | a few hints; only `MINE_SATISFIED` / `MINE_FULL` |
| M2 | 6×6 | the counter does real work (`MINE_TOTAL`) |
| M3 | 6×6 | overlapping hints (`MINE_SUBSET`) |
| M4 | 6×6 | second hint shape (orthogonal-only); leads into the chess neighbourhood |
| M5 | 6×6 | combine with a sum row and a sandwich row |

## 9. Decisions and open questions

No open questions.

**Layers stay separate.** The digit layer and the mine layer touch only at hint cells; there is no rule constraining the
digits on bombs. They already inform each other compositionally in the player's head (hint digits are free sudoku
givens, and sudoku progress makes hints readable), so nothing is forced. Revisit only if playtesting says the mine
layer feels bolted on.

Resolved: checking is manual and available at any time (rule 8); hint = 8 surrounding cells and the hint cell is safe; the counter is on every minesweeper level; the final
check requires every bomb marked; bomb and flag are one mark; hints are always given digits; any cell can be a mine; mine
cells hold digits; mine density is generated backwards; 6×6 is both the next grid after 3×3 and the minesweeper board.
