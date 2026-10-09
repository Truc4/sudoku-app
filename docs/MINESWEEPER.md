# Minesweeper variant — design

Status: **design only, nothing implemented.** Where your description left room, this doc picks a default and
says so. Open questions are collected at the end (§9); each one is marked in the text with **(Q#)**.

## 1. The idea in one paragraph

Some grid cells are **mines**: they hold no digit. Some **given digits are drawn in a colour**; a coloured
digit is a **hint**. A hint's number does double duty: it is an ordinary sudoku digit *and* it is the count of
mines around it. You deduce where the mines are *and* fill in the digits, and the two deductions feed each
other: a mine removes a cell from the digit puzzle, and a digit-puzzle elimination can prove a cell is a mine.

Why the dual role is the right design:
- No new symbol set. The player already reads digits; colour just says "this one also counts mines".
- A hint can never be 0 or 9 (digits are 1–9, a cell has at most 8 neighbours), so **every hint is informative**
  (at least one adjacent mine) and there is no flood-fill opening to simulate.
- It composes with the existing rule style: one more clue type whose target is read off the solution.

## 2. Rules as specified (defaults chosen where the brief was open)

1. The board is a normal sudoku-but board (§ DESIGN.md) plus a set of mines. A mine cell contains no digit.
   Digit rules (rows, columns, boxes all-different) apply to the *non-mine* cells only.
2. A **coloured given digit** `d` at cell `h` is a hint of some shape: exactly `d` of the cells in `shape(h)` are
   mines. Default shape = the 8 surrounding cells. `h` itself is never a mine. **(Q1)**
3. The colour encodes the **shape**, so later colours can mean other neighbourhoods (orthogonal-only, knight-move
   for the chess rule, a row or column) without any new UI concept. Colour is never the *only* signal (see §7).
4. Optionally a **mine counter** is shown ("5 mines"), exactly like classic minesweeper. **(Q2)**
5. The puzzle is solved when every non-mine cell has its digit and every mine cell is marked a bomb.
6. Uniqueness and "no guessing" hold as before: every generated puzzle is solvable by the technique ladder,
   verified by an independent brute-force solver.

## 3. Marks: bomb vs flag

You said a player can "mark tiles as a bomb or flag". Default reading, which keeps solving honest: **(Q3)**

- **Bomb** — "I know this is a mine." Counts toward the solution check and is what a hint/check reveals.
- **Flag** — a non-committal note ("probably a mine", or "look at this later"), like a pencil mark. It never affects
  checking and is cleared by one tap.

Marks are only allowed on cells that are not given. The cell's candidates (pencil digits) and these marks live in the
same bitmask: bit 0 = "could be a mine", bits 1–9 = digits.

## 4. Data model additions (still Arche-shaped)

No new storage idea; one new rule kind and a couple of columns.

```
Cell columns  (new)   cell_mine[]   solution: 1 if the cell is a mine
                      cell_hint[]   0 = plain, else the hint shape (the colour)
                      cell_mark[]   player mark: none / flag / bomb   (UI state, not solver state)
Rule rows     (new)   RULE_MINE_HINT   (kind, axis = AXIS_CELL, line = centre cell, param = shape)
                      RULE_MINE_TOTAL  (kind, param = number of mines on the board)
```

- **Candidate bit 0 means "can be a mine".** The digit bitmask already uses bits 1–9, so bit 0 is free. A cell is
  decided as a mine when its candidates are exactly `{0}`, and as a digit when bit 0 is gone. A cell with
  `{0, 3, 7}` is "mine, or 3, or 7".
- **A hint is a rule over a computed neighbourhood**, like a row is: `func_shape_cell(shape, centre, k, w, h)` returns
  the k-th neighbour or -1, mirroring `func_unit_cell`. Still **no membership join table**: a neighbourhood is a
  function of `(shape, centre)`. (Join tables stay reserved for arbitrary regions: killer cages, thermometers.)
- The hint's digit is the rule's target, read from `cell_sol[centre]`, so the existing "targets come from the
  solution" generator pattern applies. The only twist is that hint digits are part of the sudoku solution, so mines
  and hint cells must be fixed *before* the digit fill (§6).
- A mine is stored as value `-1` / candidate `{0}` in `cell_val`. `func_digit_ok` already ignores it (it compares
  digits), and line sums treat mines as 0. Unit lengths become "number of non-mine cells".

## 5. Solver techniques (new, in rank order after the existing ladder)

All are local to one hint; none needs a hypothetical.

| Technique | Fires when | Effect |
|---|---|---|
| `MINE_SATISFIED` | a hint already has `d` known mines around it | all other unknown neighbours become digits (bit 0 removed) |
| `MINE_FULL` | unknown neighbours == mines still needed | all unknown neighbours become mines (candidates = `{0}`) |
| `MINE_TOTAL` | the global counter is met or the rest must all be mines | same as above, board-wide |
| `HINT_RANGE` | a coloured cell whose digit is not yet known | restrict its digit to `[known mines .. known + unknown]` |
| `MINE_SUBSET` | neighbourhood of hint A is a subset of hint B's | the difference holds `d_B - d_A` mines (the classic 1-2 pattern) |

Cross-rule behaviour comes for free from the shared bitmask, with no special-casing:
- A mine decided by minesweeper reasoning drops out of every unit, so a unit's remaining cells have fewer candidates.
- A sudoku elimination that leaves a cell with only `{0}` is a mine; one that removes bit 0 is a digit.
- Full-unit techniques (`LAST_DIGIT`, `HIDDEN_SINGLE`) need a unit with exactly 9 digit cells, so they must also check
  that no cell in it can still be a mine.

Difficulty is the same metric as before (highest rank, step count, how many techniques are simultaneously available).

## 6. Generator changes

1. Choose mine cells (random; level decides the count; avoid putting every neighbour of a would-be hint out of range).
2. Choose hint cells among non-mine cells with at least one adjacent mine; each fixes a digit `d` = its adjacent mine
   count. Reject if two hints with the same digit share a row, column or box (they would break all-different).
3. Fill the remaining non-mine cells with a digit solution by backtracking, with mines and hint digits pre-set.
4. Derive `RULE_MINE_HINT` / `RULE_MINE_TOTAL` rows from this solution.
5. Remove givens as before. Hint digits stay as givens (they carry the colour); mines are never given, so every mine
   must be deduced. The ceiling-limited solver must still finish, and the level's teaching technique must be used.

The brute-force oracle in the selftest gains one branch per cell ("mine" besides digits 1–9) and checks hint counts
and the mine total, so uniqueness is still proven independently of the human solver.

## 7. UI notes

- **Colour is not enough on its own.** Pair each hint colour with a second cue: a ring or corner dot in a shape that
  matches the neighbourhood (a full ring for the 8 neighbours, a plus for orthogonal, etc.), and pick a
  colour-blind-safe palette. Selecting a hint **outlines its neighbourhood**, which doubles as the tutorial.
- Tap a cell, then pick: digit pad (as now), plus two extra pad buttons for **bomb** and **flag**.
- Bombs render as a bomb icon, flags as a flag; both can coexist with pencil digits in the same cell.
- A "check" or hint shows the next solver step in terms of the hint it came from ("this 3 already touches 3 mines,
  so its other neighbours are safe").
- Mine counter in the header when the level has one.

## 8. Curriculum for the variant

Introduced after sums/sandwiches so each concept arrives alone:

| Step | Grid | Idea |
|---|---|---|
| M0 | 3×3 | one hint, one mine: the hint digit says where the mine is by elimination. Teaches the dual role. |
| M1 | 6×6 | two or three mines, hints placed so only `MINE_SATISFIED`/`MINE_FULL` are needed |
| M2 | 6×6 | add the mine counter (`MINE_TOTAL`) |
| M3 | 6×6 | coloured blanks (`HINT_RANGE`): the hint's digit is not given |
| M4 | 6×6 | overlapping hints (`MINE_SUBSET`) |
| M5 | 6×6 | second hint colour/shape (orthogonal-only); leads into the chess neighbourhood |
| M6 | 6×6 | combine with a sum row and a sandwich row |

## 9. Open questions

1. **(Q1) Hint shape and the hint cell.** Is the default "8 surrounding cells" right, and is a hint cell never a mine?
2. **(Q2) Mine counter.** Always shown, only on some levels, or never?
3. **(Q3) Bomb vs flag.** Are these two different marks (certain vs tentative, as proposed), or one mark with two
   names? If different, which one does the final check count?
4. **(Q4) Can a coloured cell be blank?** Proposed for later (M3), where you only know the cell is a hint and must
   deduce its digit. Or are hints always given digits?
5. **(Q5) What does a mine cell contain?** Proposed: nothing (no digit), which shrinks the sudoku units. The
   alternative is a mine that still holds a digit and merely adds to neighbours' counts; that is a different, easier
   puzzle but keeps full units.
6. **(Q6) Is the 6×6 you want the minesweeper board size, or the general next grid after 3×3?** I made it both: the
   level ladder now goes 3×3 → 6×6, and M1–M6 above are 6×6.
