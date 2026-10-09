# Minesweeper variant — design

Status: **design only, nothing implemented.** Where the brief left room, this doc picks a default and says so.
Open questions are collected at the end (§9) and marked in the text with **(Q#)**.

## 1. The idea in one paragraph

The board is a normal, complete sudoku-but puzzle: every cell holds a digit. On top of it sits a **second layer**:
some cells are **mines** (bombs). A mine still holds its digit; being a mine is just an extra property. Some **given
digits are drawn in a colour**, and a coloured digit is a **hint**: its number is the count of mines around it. The
player solves both layers, filling digits and **marking** cells as bomb or flag. The only bridge between the layers
is the hint digit, which is read two ways at once: as a sudoku digit and as a mine count.

Why that bridge is a good design:
- No new symbol set. The player already reads digits; colour only says "this one also counts mines".
- A hint can never be 0 or 9 (digits are 1–9, a cell has at most 8 neighbours), so every hint is informative
  (at least one adjacent mine) and there is no flood-fill opening to simulate.
- The layers can be taught separately and then welded: sudoku logic yields a hint's digit, the digit yields a mine
  count, the count yields mines.
- It reuses the house pattern: one more clue type whose target is read off the solution.

## 2. Rules (defaults chosen where the brief was open)

1. Digit rules are unchanged and apply to **every** cell (rows, columns, boxes all-different, plus sum/sandwich lines
   when a level has them). Mines do not change the digit puzzle at all.
2. A **coloured given digit** `d` at cell `h` is a hint of some shape: exactly `d` of the cells in `shape(h)` are
   mines. Default shape = the 8 surrounding cells. By default `h` itself is not a mine. **(Q1)**
3. The colour encodes the **shape**, so later colours can mean other neighbourhoods (orthogonal-only, knight-move for
   the chess rule, a row or column) without any new UI concept. Colour is never the only signal (§7).
4. Optionally a **mine counter** is shown, like classic minesweeper. **(Q2)**
5. Solved when every cell has the right digit and the bomb marks are exactly the mine set.
6. Uniqueness and "no guessing" hold as before, for both layers, verified by an independent brute-force solver.

## 3. Marks: bomb vs flag

Both are **player marks on a cell that also holds a digit**; they sit alongside the digit and its pencil marks, not
instead of them. Default reading, keeping solving honest: **(Q3)**

- **Bomb** — "I know this is a mine." Counted by the final check; revealed by a hint/check.
- **Flag** — a non-committal note ("probably a mine / look at this later"), like a pencil mark. Never affects checking
  and is cleared by one tap.

A given digit can be marked too (a given is not automatically safe), unless it is a hint cell, which is safe by
default (Q1).

## 4. Data model additions (still Arche-shaped)

Mine status is **independent of the digit**, so it gets its own columns rather than sharing the digit bitmask.

```
Cell columns (new)   cell_mine[]        solution: 1 if the cell is a mine
                     cell_mine_cand[]   2-bit set: bit0 "may be safe", bit1 "may be a mine"; 3 = unknown
                     cell_hint[]        0 = plain digit, else the hint shape (the colour)
                     cell_mark[]        player mark: none / flag / bomb   (UI state, not solver state)
Rule rows    (new)   RULE_MINE_HINT   (kind, axis = AXIS_CELL, line = centre cell, param = shape)
                     RULE_MINE_TOTAL  (kind, param = number of mines on the board)
```

- A **hint is a rule over a computed neighbourhood**, like a row is: `func_shape_cell(shape, centre, k, w, h)` returns the
  k-th neighbour or -1, mirroring `func_unit_cell`. Still **no membership join table**; joins stay reserved for
  arbitrary regions (killer cages, thermometers).
- The hint's target is `cell_sol[centre]`, so the existing "targets come from the solution" pattern holds.
- Because the layers are independent, `cell_val` and `cell_cand` are untouched. Unit lengths stay at 9 for boxes, so
  `LAST_DIGIT` and `HIDDEN_SINGLE` keep working unchanged.

## 5. Solver techniques (new, ranked after the existing ladder)

All are local to one hint; none needs a hypothetical.

| Technique | Fires when | Effect |
|---|---|---|
| `MINE_SATISFIED` | a hint already has `d` known mines around it | its other unknown neighbours are safe |
| `MINE_FULL` | unknown neighbours == mines still needed (a corner hint 3, an edge hint 5, an 8 anywhere inside) | all unknown neighbours are mines |
| `MINE_TOTAL` | the global counter is met, or the rest must all be mines | same, board-wide |
| `HINT_RANGE` | a coloured cell whose digit is not yet known | restrict its digit to `[known mines .. known + unknown]` |
| `HINT_DIGIT_MINES` | a hint's digit has been deduced by sudoku logic | now its mine count is known: run the techniques above |
| `MINE_SUBSET` | neighbourhood of hint A is a subset of hint B's | the difference holds `d_B - d_A` mines (the classic 1-2 pattern) |

The coupling is deliberately narrow. The digit layer informs the mine layer only through a hint's digit
(`HINT_DIGIT_MINES`), and the mine layer informs the digit layer only through a hint's range (`HINT_RANGE`).
That keeps each layer's logic independent and makes the "coloured blank" levels (§8) the interesting ones.

Difficulty is the same metric as before (highest rank, step count, number of simultaneously available techniques).

## 6. Generator changes

1. Build the full digit solution exactly as today (including any sum/sandwich rules).
2. Choose **hint cells** among cells whose solution digit is `d` in 1..8 and which have at least `d` neighbours.
3. Choose the **mine set** by a small randomised backtracking over cells so that every hint sees exactly its digit
   worth of mines (and the optional total matches). Reject and retry if no assignment exists.
4. Derive `RULE_MINE_HINT` / `RULE_MINE_TOTAL` rows from the result.
5. Remove givens as today. A hint's digit stays given if the level shows it; for "coloured blank" levels the hint
   cell is un-given but stays coloured. Mines are never given, so each must be deduced.
6. The level's teaching technique must appear in the trace, and the ceiling-limited solver must still finish.

The brute-force oracle in the selftest counts digit solutions and mine assignments separately (the layers are
independent given the hints) and requires exactly one of each.

## 7. UI notes

- **Colour is not enough on its own.** Pair each hint colour with a second cue: a ring or corner dot whose shape
  matches the neighbourhood (a full ring for the 8 neighbours, a plus for orthogonal, …), with a colour-blind-safe
  palette. Selecting a hint **outlines its neighbourhood**, which doubles as the tutorial.
- Tap a cell, then pick: the digit pad (as now) plus two extra buttons, **bomb** and **flag**. Digit and mark coexist
  in one cell, so the cell needs room for the big digit, pencil marks, and a small bomb/flag badge.
- A hint/check explains the next step in terms of the hint it came from ("this 3 already touches 3 bombs, so its other
  neighbours are safe").
- Mine counter in the header when the level has one.

## 8. Curriculum for the variant

Introduced after sums/sandwiches so each concept arrives alone.

| Step | Grid | Idea |
|---|---|---|
| M0 | 3×3 | one coloured corner **3** (3 neighbours): all three are bombs (`MINE_FULL`). Teaches the dual role without any deduction about digits. |
| M1 | 6×6 | a few hints with given digits; only `MINE_SATISFIED` / `MINE_FULL` |
| M2 | 6×6 | mine counter added (`MINE_TOTAL`) |
| M3 | 6×6 | **coloured blank**: the hint's digit is not given, so sudoku logic must produce it before the mines can be found (`HINT_DIGIT_MINES`); also `HINT_RANGE` |
| M4 | 6×6 | overlapping hints (`MINE_SUBSET`) |
| M5 | 6×6 | second hint shape (orthogonal-only); leads into the chess neighbourhood |
| M6 | 6×6 | combine with a sum row and a sandwich row |

## 9. Open questions

1. **(Q1) Hint shape and the hint cell.** Is "8 surrounding cells" right, and is a hint cell itself always safe?
2. **(Q2) Mine counter.** Always shown, only on some levels, or never?
3. **(Q3) Bomb vs flag.** Two different marks (certain vs tentative, as proposed), or one mark with two names? If
   different, does the final check count only bombs?
4. **(Q4) Coloured blanks.** Proposed for M3: a cell you can tell is a hint but whose digit you must deduce. Or are
   hints always given digits?
5. **(Q5) Which cells can be mines?** Any cell, given or not (proposed), or only non-given cells?
6. **(Q6) Mine density.** A rough target, e.g. roughly 15–25% of a 6×6 board, or whatever makes the hints work?

Resolved: mine cells **do** hold digits (the mine layer is independent of the digit layer), and 6×6 is both the general
next grid after 3×3 and the minesweeper board size.
