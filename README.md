# sudoku-app

"Sudoku, But…" — a mobile puzzle game that adds one rule at a time. The goal is a language-neutral prototype in C that
ports mechanically to [Arche](https://github.com/Truc4/arche).

```
make test
./build/sudoku gen 3 7     # level 3, seed 7: puzzle, solver trace, solution
./build/sudoku play 11 3   # play a level in the terminal through the engine API
```

- [docs/ENGINE.md](docs/ENGINE.md) — the core API (`src/engine.h`), UI-independent.
- [docs/DESIGN.md](docs/DESIGN.md) — architecture, solver/generator design, roadmap.
- [docs/MINESWEEPER.md](docs/MINESWEEPER.md) — the minesweeper variant.
- `src/` — the prototype (`sb.h` explains the Arche-shaped conventions).
