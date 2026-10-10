# sudoku-app

"Sudoku, But…" — a mobile puzzle game that adds one rule at a time. The goal is a language-neutral prototype in C that
ports mechanically to [Arche](https://github.com/Truc4/arche).

```
make test
./build/sudoku gen 3 7     # level 3, seed 7: puzzle, solver trace, solution
./build/sudoku play 11 3   # play a level in the terminal through the engine API
make test-ui               # plays every level through the UI with simulated taps (software renderer)
make run                   # the real window (sokol_app, vendored in third_party/sokol; see docs/UI.md)
make build/sudoku-sdl      # secondary backend (needs SDL3 installed; see docs/UI.md)
make web                   # browser build (needs emcc: https://emscripten.org/docs/getting_started/downloads.html)
make serve-web             # make web, then serve it at http://localhost:8765 for a phone/browser check
```

- [docs/ENGINE.md](docs/ENGINE.md) — the core API (`src/engine.h`), UI-independent.
- [docs/UI.md](docs/UI.md) — UI architecture, library choice (sokol_app + our own UI core, SDL3 kept as a secondary backend), testing.
- [docs/DESIGN.md](docs/DESIGN.md) — architecture, solver/generator design, roadmap.
- [docs/MINESWEEPER.md](docs/MINESWEEPER.md) — the minesweeper variant.
- `src/` — the engine and CLI (`sb.h` explains the Arche-shaped conventions).
- `ui/` — platform-independent UI core, built-in font, sokol_app backend, SDL3 backend, software backend.
- `third_party/sokol/` — vendored sokol headers (single-header C, no package manager needed).
- `web/` — the Emscripten HTML shell (`make web`); auto-deployed to GitHub Pages by `.github/workflows/web.yml`
  on every push to `main`, so the Pages URL always serves the newest build.
- `android/` — NDK/CMake Android project (no Java/Kotlin code, see `android/README.md`); built by
  `.github/workflows/android.yml` on every push to `main`, which publishes a debug APK to a `latest` GitHub
  Release — one stable download link that always has the newest build.
