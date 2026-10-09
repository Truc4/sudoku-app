# sudoku-app

"Sudoku, But…" — a mobile puzzle game that adds one rule at a time. The goal is a language-neutral prototype in C that
ports mechanically to [Arche](https://github.com/Truc4/arche).

```
make test
./build/sudoku gen 3 7     # level 3, seed 7: puzzle, solver trace, solution
./build/sudoku play 11 3   # play a level in the terminal through the engine API
make test-ui               # plays every level through the UI with simulated taps (software renderer)
make build/sudoku-sdl      # the real window (needs SDL3; see docs/UI.md)
```

- [docs/ENGINE.md](docs/ENGINE.md) — the core API (`src/engine.h`), UI-independent.
- [docs/UI.md](docs/UI.md) — UI architecture, library choice (SDL3 + our own UI core), testing.
- [docs/DESIGN.md](docs/DESIGN.md) — architecture, solver/generator design, roadmap.
- [docs/MINESWEEPER.md](docs/MINESWEEPER.md) — the minesweeper variant.
- `src/` — the engine and CLI (`sb.h` explains the Arche-shaped conventions).
- `ui/` — platform-independent UI core, built-in font, SDL3 backend, software backend.
