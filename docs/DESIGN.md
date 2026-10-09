# Sudoku, But… — architecture & design notes

Status: **first-pass notes, nothing built.** Written without a keyboard to try things, so every
"Arche can do X" claim below is based on reading the READMEs/docs in `arche`, `arche-portfolio`
and `arche-playground`, not on compiling anything. Items marked **[verify]** need a check at the computer.

## 1. Concept

A mobile puzzle game that starts trivially and **adds one rule at a time**:

1. 3×3 box, one digit missing. (Teaches: tap cell, tap digit.)
2. 6×3 (two boxes side by side). Rows now matter.
3. Kakuro-style cage sums appear.
4. …more rules (killer cages, thermometers, even/odd, diagonals, arrows, etc.)

The core promise: **every puzzle is solvable by deduction alone, with shallow reasoning.** No
guessing, no "if I put 4 here, then 7 there, then…" lookahead. A puzzle that needs deep
what-ifs is not generated. This is what makes it a *learning* game rather than a hard-puzzle game.

## 2. The central idea: rules are data, techniques are the unit of difficulty

Everything else falls out of two choices.

**Rules are rows, not code paths.** A board is a set of cells plus a set of constraints over subsets
of those cells. "Row unique", "box unique", "cage sums to 17 with no repeats", "thermo increases"
are all the same shape: `{ kind, parameter, member cells }`. Adding a new rule to the game means a
new `kind` plus its propagation logic. The board, renderer, input and generator don't change.

**Difficulty is measured in deduction techniques, not clue count.** Each human-style deduction
("naked single", "hidden single", "this cage must be {1,2,4} so…") is a named **technique** with a
rank. A puzzle's difficulty is *which techniques a human-style solver needs and how many steps*.
"Solvable with abstractions, no far lookahead" becomes a precise, checkable property:

> The puzzle is solvable by repeatedly applying techniques from the allowed set (rank ≤ N),
> with **no backtracking and no trial-and-error**.

Because every technique is **sound** (never removes a real candidate), a puzzle the solver finishes
this way is automatically **unique**. No separate uniqueness check is needed in the player-facing path.

## 3. Data model (Arche-shaped)

Arche is columnar/ECS with static pools, which fits a bounded puzzle board well.

```
Cell    { cell_id, row, col, value, given, cand }     // cand = u16 bitmask of candidates (bit d = digit d possible)
Rule    { rule_id, kind, param }                       // param: sum target, direction, etc.
Member  { rule_id, cell_id, ord }                      // join table: which cells a rule covers (ord for ordered rules like thermos)
Step    { step_id, technique, rule_id, cell_id, ... }  // solver trace; also drives hints
Puzzle  { puzzle_id, width, height, seed, level, ... } // metadata for the curriculum
```

- **Membership as a join table** (`Member`) instead of per-rule fixed arrays. Rules cover wildly different
  numbers of cells; a DB-style join is the natural fit and avoids variable-length fields. **[verify]** how
  well maps can gather/reduce across a join in current Arche (cage-sum propagation needs "for each rule, look at
  its member cells' candidates"). This is the biggest language-fit risk (see §9).
- **Candidates as bitmasks** make most propagation branchless bit ops (AND/OR/popcount), which suits pure
  `map`s.
- **Static pools:** pick hard upper bounds (e.g. 9×9 = 81 cells, N rules, M memberships) and size pools in the driver.
  A smaller board just uses fewer rows. No dynamic allocation needed.

## 4. Layering: devices and a driver

Arche's model is "devices" (libraries: shapes + behavior, no storage) and a "driver" (your program: picks pool sizes
and schedules). Proposed split:

| Device | Responsibility | Notes |
|---|---|---|
| `grid` | Cell archetype, board geometry, coordinate ↔ cell_id | Shapes only (non-rectangular boards possible later). |
| `rules` | Rule/Member archetypes + per-kind **validators** (is the filled board legal?) | One module per rule kind; registry by `kind`. |
| `solver` | Candidate propagation + technique ladder, emits `Step`s | The heart. See §5. |
| `gen` | Puzzle generator (solution → rules → clue removal) | See §6. Offline first, in-app later. |
| `curriculum` | Level ladder: which rules/techniques/size at each level | Data (a table), not logic. |
| `board_ui` | Draw grid, selection, candidates ("pencil marks"), cage outlines | Layout is pure functions of viewport size. |
| `input` | Touch → cell select / digit entry / pencil toggle / undo | Builds on the `button`/pointer work from the portfolio. |
| `store` | Save progress, settings, completed levels | Thin C bridge (§7). |
| `fx` | Haptics, sound, small animations | Optional; C bridge. |

Driver (`sudoku_but.arche`): owns all pools, wires input → game state → solver (hints/validation) → render, in a
`forever` frame loop. The portfolio already has this shape and compiles to a wasm reactor
(`arche_run` once, `arche_frame` per tick), so the same source runs native and in a browser.

Frame order matters in this style (the portfolio docs call it "load-bearing"): input → apply move →
propagate/validate → hint/trace → render.

## 5. The solver (technique ladder)

Two solvers, deliberately separate:

**A. Human-style solver (player-facing + difficulty meter).** Loop:
1. Propagate cheap eliminations to a fixed point.
2. Find the **cheapest applicable technique**; apply one step; record `Step{technique, where, what}`.
3. Repeat. Fail ⇒ "needs more than the allowed techniques".

Technique examples, roughly by rank:

- T0 *Last remaining digit* (the level-1 puzzle is only this).
- T1 *Naked single* / *hidden single* (row, col, box).
- T2 *Cage arithmetic*: sum + size ⇒ forced/forbidden digits (Kakuro combo tables: "3 cells summing to 7 must be {1,2,4}").
- T3 *Pairs/triples* (naked/hidden), *pointing/claiming* interactions between rules.
- T4 *Inequality chains* (thermo), short fixed-length chains.
- Higher (fish, coloring, …): **only if the curriculum ever needs them.**

Each technique is a `map`/`system` over candidate columns + `Member` joins, so adding one is additive.
The same step trace powers the **hint system** for free ("this cage can only be {1,2,4}, so 9 can't go here")
and a **"why was I wrong"** explanation.

**B. Brute-force backtracking solver (generation-time only).** Builds random valid full solutions and
optionally cross-checks solution counts. Never shown to the player. Needs an explicit depth-bounded stack to
respect "no heap" **[verify]**.

### Defining "no far lookahead" concretely

Constrain the human solver, and reject puzzles that violate any of:

1. **Zero guessing:** no step is "assume X and see".
2. **Technique ceiling:** only techniques with rank ≤ the level's ceiling.
3. **Bounded chain length:** any multi-cell reasoning (pairs, thermo chains) spans at most *k* cells.
4. **Bounded working memory (optional):** at each step, the facts a player must hold to see the deduction ≤ *m*.
   Rough proxy: size of the rule's member set × candidates involved.
5. **Always a visible next move:** at every state ≥1 step applies (never "stuck, now search").

Difficulty score for the curriculum = (highest technique rank, count of non-trivial steps, average number of
simultaneously-available steps — fewer available means harder to spot).

## 6. Generator

Pipeline for a spec `{ size, rule set, technique ceiling, must_use: technique/rule?, seed }`:

1. **Build a full valid solution** satisfying only the *unique-digit* rules (backtracking solver B).
2. **Derive the data-bearing rules from the solution.** Cage sums, thermo placement, even/odd marks are computed from
   the solution, so they hold by construction. The puzzle gets *its constraints from its answer*.
3. **Choose givens by removal:** remove givens one at a time (random or heuristic order); keep a removal only if the
   human solver (A, restricted to the ceiling) **still finishes**. Result: a minimal-ish puzzle with a guaranteed
   shallow solution path.
4. **Teaching check:** if the level exists to introduce rule R, require the trace to use R's technique at least once,
   early, ideally as the **only productive move** at that moment ("spotlight" the new rule). Reject/retry otherwise.
5. **Score and bucket** by §5's difficulty score; store with seed for reproducibility.

Where to run it: **offline first** — a generator tool emits puzzle files (the repo already has CSV I/O), shipped as
data. The same Arche source can later run on-device for an endless mode. Offline means no generation stalls on a
phone and puzzle QA is reviewable.

## 7. Platform & C bridges

Arche targets LLVM, and `--arch=wasm32` already works as a reactor with a canvas framebuffer backend, touch input,
and a `coarse_pointer()` check. Two shippable paths:

1. **Web / PWA (do this first).** Existing wasm gfx + DOM `text`/`ui` devices. No new native work, installable on a
   phone home screen, fastest iteration loop. Cost: no real haptics / app-store presence.
2. **Native iOS/Android shell (later).** Arche compiled to an object/library for the target arch, linked into a thin C host:
   - **SDL3** (window, touch, audio, lifecycle) or **sokol_app** as the platform layer. Fits well since the language
     is already "framebuffer + input columns".
   - **stb_truetype / fontstash** for scalable text (see gap below).
   - **miniaudio** for sound.
   - Small platform shims (ObjC/JNI) for haptics, safe-area insets, share sheet.
   - Storage: plain file or SQLite behind a `store` device that exposes systems, not raw procs (Arche keeps
     `#foreign` procs private to devices).

Bridge convention (matches Arche's rules): each C lib sits **behind a device**; its externs are private; the game
touches only the device's systems and columns.

**Gaps to expect [verify]:**
- The UI font today is a shared 8×8 bitmap. A phone game wants crisp scalable numerals at multiple DPIs. Either add a
  vector-font path (stb_truetype in a `text` backend) or draw digits 1–9 as custom glyphs (consistent look, arguably on-brand).
- Cross-compiling Arche to aarch64 iOS/Android — confirm mobile triples work with `--target`.
- Safe-area / orientation handling and high-DPI backing stores.

## 8. UX notes (mobile-first)

- **Tap cell → tap digit** pad at the bottom (thumb reach). Big hit targets; board scales to `min(vw, vh)` (the portfolio
  already sizes by vmin for this reason).
- Pencil marks toggle; undo/redo; long-press to clear.
- Rules appear as small **legend chips** that introduce themselves the first time they show up (a tiny interactive
  example, not a text wall). Introducing a rule = one spotlight level + a mini-tutorial.
- Gentle instant feedback by default (highlight conflicts); optional "check" mode for purists.
- Hint button reveals the next solver `Step` in escalating detail: *region* → *technique name* → *the deduction*.
- Short sessions: early levels take seconds, growing with rule count. Stars for no-hint clears.

## 9. Risks & open questions

1. **Cross-row aggregation in Arche** (cage sums; "does this digit appear anywhere in this rule's members") is the least
   proven part of the columnar model. Fallbacks: dense per-rule member arrays, or `system`s that loop over a rule's slice.
   Prototype this *first*.
2. **Static pool sizing for variable boards:** hard maxima are fine; confirm pools can use a prefix of their extent
   (see `arche/docs/design/pool-extents-and-column-buffers.md`).
3. **Backtracking without heap:** explicit depth-bounded stack.
4. **Generation quality:** removal-based minimization can yield samey puzzles; may need varied removal orders or
   technique-targeted construction.
5. **Rule interaction explosion:** each pair of rules may enable new compound techniques (cage × thermo). Order the
   curriculum so the technique set stays small; defer exotic combos.
6. **Adaptivity:** v1 fixed curriculum. v2 could track per-technique success/time and pick the next puzzle's
   must-use technique.
7. **Branding:** "Sudoku, But…" works as a literal UI device — each chapter completes the sentence ("…but it's sums",
   "…but it's thermometers").

## 10. Suggested milestones

1. **M0 — Spike (answers risk #1):** 3×3 and 6×3 boards; `Cell`/`Rule`/`Member`; unique rule + cage-sum rule; brute-force
   solver; ASCII print. No UI.
2. **M1 — Human solver + trace:** technique ladder T0–T2, `Step` log, difficulty metrics; doctests via `arche test`.
3. **M2 — Offline generator:** emits level files to a spec; assert "human solver always finishes with no backtracking".
4. **M3 — Playable web build:** board render, touch input, digit pad, pencil marks, hints; wasm/PWA, tested on a phone.
5. **M4 — Curriculum v1:** ~30 levels over the first 4–5 rules; rule-intro spotlight levels; progress saving.
6. **M5 — Native shell:** SDL3 (or sokol) host + store/haptics; try iOS/Android; decide distribution.
7. **M6+ — More rules, endless mode, adaptive selection.**

## 11. Open questions

- Which repo should this live in — a new `sudoku-but` repo, or a subfolder of an existing one?
- Is web/PWA-first OK, or is native app-store distribution a day-one requirement?
- Preferred first five rules after cage sums?
- How strict is "no lookahead": single-step deductions only, or short chains (pairs, thermo runs) allowed?
- Fixed hand-tuned curriculum for v1, or adaptive from the start?

## 12. C prototype (first implementation)

The first prototype is plain C99 in `src/`, written in the *shape* of Arche so it ports mechanically:
static column arrays in fixed pools, no malloc, no per-element structs, no pointers to rows, no callbacks.
Functions follow Arche's split by naming convention: `func_*` pure, `map_*` per-row, `sys_*` whole-pool,
`tech_*` one solver deduction. Details are in the header comment of `src/sb.h`.

```
make            # build/sudoku
make test       # selftest: brute-force uniqueness + human-solver replay for every level
./build/sudoku levels
./build/sudoku gen <level> [seed]    # puzzle, solver trace, solution
```

### Key decision: rules attach to a single line first

Many rules only concern one row or column, so v0 rules are rows of `(kind, axis, line, param)` where `axis` is
row, column or box. A unit's cells are *computed* from `(axis, line, k)`; there is no membership join table yet.
Curriculum is data too: a level table plus a `(level, rule)` join table, so "add a sandwich to a different row" is
one extra join row:

| Level | Grid | New idea | Avg empty cells |
|---|---|---|---|
| 0 | 3×3 | one missing digit (last digit) | 1 |
| 1 | 3×3 | **sum** clue on a row (Kakuro-style combo technique) | 2 |
| 2 | 3×3 | sums on two rows | 3 |
| 3 | 3×3 | sums on rows *and* columns | ~7 |
| 4 | 3×3 | sum on one row, **sandwich** on a *different* row | ~3 |
| 5 | 3×3 | sums plus a sandwich | ~6 |
| 6 | 6×6 | no clues; rows, columns and four boxes (capped at 10 empty) | 10 |
| 7 | 6×6 | sum on a row | ~17 |
| 8 | 6×6 | sum row + sandwich on another row | ~19 |
| 9 | 6×6 | sums/sandwiches on rows and a column | ~20 |
| 10 | 3×3 | **minesweeper**: a hint, bombs and the counter | 1 |
| 11 | 6×6 | mine hints and the counter | 10 |
| 12 | 6×6 | overlapping hints (subset technique) | 10 |
| 13 | 6×6 | mines with a sum row and a sandwich row | ~18 |

The grid stays 3×3 for the first six levels, then jumps straight to 6×6 (no 6×3 stage): line clues are what let more than one digit be missing, so the
clue count and the empty-cell count rise together. (Level 3 is a big jump; a "two rows + one column" level
could sit between 2 and 3.)

Findings so far:
- A lone 3×3 box with no clues is unique only with exactly one empty cell. Sum/sandwich clues on its rows and
  columns are what allow 2, 3, … empty cells, so they come *before* the grid grows.
- Sum and sandwich both lean on one function, `func_combo_union(k, R, allowed)` (the Kakuro combo table computed
  on demand). The sandwich technique enumerates (position of 1, position of 9) pairs, keeps those whose in-between
  cells can still reach the clue, and restricts each cell to what a surviving placement allows. It stays local to the
  line, so it does not count as lookahead.
- Sum clues assume the line is all-different. Hidden single / last digit only apply to 9-cell units (a 6-wide row need
  not contain every digit). Sandwich clues assert that the line contains both a 1 and a 9 (the generator rejects
  solutions where it does not).
- Free candidate elimination (auto pencil marks) is not a logged step; only technique firings are.
- Every generated puzzle is verified by an independent brute-force solver (exactly one solution) and replayed by the
  human solver using only the level's techniques.

### Roadmap past line rules

1. **Grow the grid** beyond 6×6 and start **offset (black) cells**, Kakuro-style: the grid gets a cell *kind*
   column (digit / blocked / clue) and units are runs of digit cells, so `func_unit_cell` stops being arithmetic.
2. **Membership join table** `Member(rule, cell, ord)`: needed as soon as a rule covers an arbitrary cell set.
   Thermo (ordered chain), killer cages (sum over a region), chess (knight/king-move constraints between cells),
   and minesweeper (neighbour-count clues) are all rules over cell sets or neighbourhoods. Then `tech_*` take the
   rule id and walk its members; line rules become the special case where the members form a row or column.
3. Each new rule adds: a `RULE_*` kind, a validator, a generator target derived from the solution, a technique (or
   two), and a curriculum row. Nothing else should change.

The minesweeper variant is specified in [MINESWEEPER.md](MINESWEEPER.md) and implemented as levels 10–13. The core API
is described in [ENGINE.md](ENGINE.md).
