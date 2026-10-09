# sudoku-app

"Sudoku, But…" — a mobile puzzle game that adds one rule at a time. The goal is a language-neutral prototype in C that
ports mechanically to [Arche](https://github.com/Truc4/arche).

```
make test
./build/sudoku gen 3 7     # level 3, seed 7: puzzle, solver trace, solution
```

- [docs/DESIGN.md](docs/DESIGN.md) — architecture, solver/generator design, roadmap.
- `src/` — the prototype (`sb.h` explains the Arche-shaped conventions).
