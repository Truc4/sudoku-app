# Engine API

`src/engine.h` is the core API: the whole game minus presentation. A front end (terminal, desktop, mobile, test
harness) is just another consumer. The engine has **no UI concepts**: no pixels, colours, fonts, input events or layout,
and the only strings it returns are technique and level names.

## Shape of the API (Arche style)

The interface is **data plus systems**:

- **Read** the public columns freely: `cell_given`, `cell_sol`, `cell_hint`, `rule_kind/axis/line/param`, `play_val`,
  `play_notes`, `play_bomb`, `play_wrong`, `hint_*` (declared in `engine.h` and `sb.h`).
- **Write** only through `eng_*` systems, so history, checks and invariants stay consistent.
- Static storage, no handles to free, no callbacks. One game is live at a time.
- Cell ids are row-major (`row * eng_width() + col`). Digits are 1..9; 0 means none.

## Surface

| Area | Calls |
|---|---|
| Lifecycle | `eng_level_count`, `eng_level_name`, `eng_new_game(level, seed)`, `eng_level`, `eng_seed` |
| Clues (reads) | `eng_width/height`, `eng_cell_is_given`, `eng_cell_digit`, `eng_cell_hint` (a shape id), `eng_has_mines`, `eng_mines_total`, `eng_bombs_marked`, rule columns |
| Moves | `eng_set_digit`, `eng_toggle_note`, `eng_toggle_bomb` (bomb and flag are one mark), `eng_clear_cell`, `eng_undo`, `eng_redo` |
| Checking | `eng_check` (manual; fills `play_wrong`), `eng_is_solved`, `eng_hint` (fills `hint_tech/rule/cell/digit`) |
| Persistence | `eng_save`, `eng_load` |

Every call returns `ENG_OK` or a negative `ENG_ERR_*`.

Behaviour worth knowing:
- **Checking is manual.** Nothing is validated until `eng_check` is called; any later move clears the marks it set.
- **Solved** means every digit is right and every mine is marked with no more marks than the counter (so the marks are
  exactly the mines).
- **Hints are structured, not text.** `eng_hint` runs the solver from the player's current digits and reports which
  technique fires, from which rule, and what it concludes; the front end words it. A hint never changes play state.
  Wrong entries make it return `ENG_ERR_INCONSISTENT`, which a front end can turn into "run a check first".
- **Hint shapes, not colours.** `eng_cell_hint` returns `SHAPE_KING` (8 neighbours) today; mapping shapes to colours or
  icons is the front end's job, and new shapes add enum values without changing the API.
- **Determinism.** A puzzle is a pure function of `(level, seed)`, so a save is ~13 bytes plus 4 bytes per cell.
- **Generation can miss.** `eng_new_game` may return `ENG_ERR_GENERATE` for a seed where a teaching technique cannot be
  produced (rare; only seen on the subset-mine level). Ship a list of known-good seeds per level, or retry with the next
  seed.

## Internals (not API)

`sb.h` also exposes the solver and generator (`sys_solve`, `sys_step`, `sys_generate`, the oracle) because the test
harness uses them. A front end should not call them; `eng_hint` and `eng_new_game` are the supported routes.

## Terminal consumer

`./build/sudoku play <level> [seed]` is a line-based REPL built on the API alone (`set r c d`, `bomb r c`, `undo`,
`check`, `hint`, `save`…), which keeps the API honest and lets you play any level today.

## Tests

`make test` generates 25 puzzles per level and checks, per puzzle: the solution satisfies every rule; exactly one digit
solution and one mine assignment (independent brute-force oracles); the human-style solver reproduces both layers
using only the level's techniques; and the API behaves (hint, wrong entry + check + undo/redo, givens read-only, hint
cells unmarkable, solving through the API, extra bombs rejected, save/load round trip).

## Known tuning items

- Mine density on the 6×6 mine levels comes out high (about 16 of 36 cells in one sample) because the backwards
  construction places `digit` mines around each hint and hint digits are uniform in 1..8. Cap hint digits or the
  total per level if it should feel sparser.
- Generation of the subset level takes several thousand attempts; fine offline, slow for on-device generation, so
  pre-generate seeds for it.
