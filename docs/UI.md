# UI architecture and library choice

## Decision

**sokol_app + sokol_gfx + sokol_gp as the primary platform layer, our own UI on top of it, SDL3 kept as a secondary
backend.** No widget toolkit, no retained-mode framework, in either case.

Why the UI core itself stays hand-rolled, regardless of which backend sits under it ("maximum flexibility, control
over memory, nothing like React"):

- **A sudoku board is custom-drawn anyway.** The only generic widgets needed are buttons and a list. Hit regions, layout and
  drawing are a few hundred lines of C, so a toolkit would add more than it saves.
- **Memory control.** Everything the game itself owns is static (see "Memory" below).
- **No lock-in.** The UI core never calls into a platform library. Swapping backends means writing one ~150-200-line
  file against the fixed `ui.h` contract (draw list in, `ui_pointer`/`ui_key` in) — proven twice now, by `ui/shot.c`
  (software/PPM, no window system) and `ui/backend_sokol.c` (a real GPU window).

### Why sokol_app over SDL3 for the primary platform layer

The target is a **mobile app first (Android now, iOS deferred but definite), with desktop kept easy to build and
test.** Given that, sokol_app won on a mix of mobile-support parity and what could actually be verified:

- Both SDL3 and sokol_app have an official story for iOS + Android + desktop — that part is a wash.
- sokol_app is header-only and vendored straight into the repo (`third_party/sokol/`, zlib-licensed, no package
  manager, no system install step). That is the thing that was supposed to be true of SDL3 here and wasn't: SDL3
  wasn't packaged or buildable when `backend_sdl3.c` was first written (see "Backends" below for the full history,
  including that it *does* build via `pkg-config` in this environment now — status changed mid-project, which is
  itself an argument for not re-depending on a system package for the primary target).
- sokol_app + sokol_gp were the ones actually built, run, and driven through a real OS input path (`xdotool`, not a
  test hook) in this environment end to end: window opens, the level list renders, a tap selects a cell, enters a
  digit, and solves level 1. That's a materially stronger validation than a syntax check.
- Lower-level than SDL (no `SDL_RenderFillRect`-style convenience — `sokol_gp` gets close: `sgp_draw_filled_rect`,
  `sgp_draw_textured_rect`, `sgp_draw_filled_triangles` covered all four `dc_*` primitive kinds directly, so the
  renderer in `backend_sokol.c` ended up about the same size as `backend_sdl3.c`).
- No audio in sokol_app (not needed yet; `docs/DESIGN.md` §7 already pencils in `miniaudio` separately when sound
  is added, regardless of platform layer).

Alternatives considered, platform layer:

| Option | Mobile (2026) | Verdict |
|---|---|---|
| **SDL3** | Official iOS + Android. | Kept as the secondary backend (`backend_sdl3.c`, unchanged) — biggest ecosystem, same `ui.h` contract, free to carry. Builds via `pkg-config` in this environment as of this writing (3.4.18) but its runtime behavior here wasn't further chased down (opened no visible window in the time spent) — out of scope for this pass; revisit if sokol_app hits a real wall. |
| **raylib** | Android official (v6.0); **iOS community-fork only, explicitly not production-ready.** | Ruled out: iOS is deferred but certain, and raylib's iOS story is the shakiest of the three — would mean a second platform-layer migration later for no benefit over sokol_app now. |
| **GLFW** | None — desktop-only by design, not on its roadmap. | Ruled out as primary (no mobile path at all). `GLFM` (GLFW-alike, mobile-only) could pair with it, but that's two backends instead of one for no savings over sokol_app/SDL3. |
| **Native per-platform shells** (UIKit+Metal, NDK/GameActivity, Win32/Cocoa/X11) | Maximum control; matches Arche's own "thin C host, device-shaped" philosophy and the long-term plan for haptics/safe-areas (`docs/DESIGN.md` §7). | Most total work by far — N platform backends instead of 1. The right fit *eventually*, if a cross-platform lib hits a wall a specific native API doesn't expose. Not a first move. |

Alternatives considered, immediate-mode GUI/widget libraries (a different axis — these replace *layout and widgets*,
not the window/input layer, and the project already has both in `ui/ui.c`):

| Option | Verdict |
|---|---|
| **Dear ImGui** | Tool/editor-shaped: mouse+keyboard-first, chrome-heavy default skin, touch retrofitted (its own docs recommend SDL/GLFW rather than native mobile backends). Needs its own vertex-buffer renderer on top of a platform layer — more moving parts than the current 4-primitive draw list. Not a fit for a touch-first custom board. |
| **Nuklear** | Single-header, renderer/platform-agnostic, similar shape to this project's own `ui.c`. Would buy prebuilt button/menu widgets at the cost of a dependency, for a UI that already hand-rolls those in ~400 lines. Marginal. |
| **Clay** | Pure flexbox-like layout, renderer-agnostic, zero deps, static arena, C99, Wasm-friendly. Still the one worth revisiting if layouts get complicated (current layout — portrait-stack vs. landscape-side-by-side — isn't there yet). |
| **microui** | ~1100 lines ANSI C, zero malloc, fixed memory — philosophically close to this project's own constraints. Useful as prior art (confirms a full custom UI core can be this small) rather than something to adopt; it's still a panels-and-widgets model, not a board-game layout model. |
| **sokol_gp / sokol_gl** | Not widget libraries — immediate-mode 2D drawing primitives. This is what `backend_sokol.c` actually uses for the rendering half, see above. |
| **GTK / Qt / Flutter / web stacks** | Heavy runtimes with their own memory behaviour. Out. |

## Architecture

```
          engine (src/engine.h)           game rules, solver, play state. No UI concepts.
                  ^
                  |  eng_* calls only
          UI core (ui/ui.c)               layout, input -> actions, draw list. No platform calls, no heap, no files.
            ^                 |
   ui_pointer / ui_key        |  draw list: parallel arrays dc_* (rect, frame, circle, text)
            |                 v
   backend (ui/backend_sokol.c, ui/backend_sdl3.c, or ui/shot.c)
```

- **Input is data.** A backend pushes pointer and key events; a tap and a click are the same thing.
- **Output is data.** `ui_frame(w, h)` rebuilds a draw list of four primitives (filled rect, frame, filled circle, text).
  A backend is a loop over arrays.
- **Pure layout.** Everything is computed from the drawable size every frame: portrait stacks board over controls,
  landscape puts them side by side. No platform-specific code paths, no cached geometry to invalidate.
- **Built-in font.** A 5x7 bitmap font (`ui/font.c`) is shared by all backends, so there are no font files and text looks
  identical everywhere. It is crisp at integer scales and a deliberate placeholder: a vector font (e.g. stb_truetype) can
  replace it by changing only `DC_TEXT` rendering in the backends.
- **Engine state is the single source of truth.** The UI keeps only presentation state: selected cell, notes mode, message,
  hint highlight, which levels are finished.

## Backends

- **`ui/backend_sokol.c`** — the primary real app: window, mouse/touch/keyboard, via vendored `third_party/sokol/`
  headers (`sokol_app`, `sokol_gfx`, `sokol_gp`, `sokol_glue`, `sokol_log` — zlib, no package manager, see
  `third_party/sokol/README.md` for the pinned source commits). Build with `make build/sudoku-sokol`. **Status:
  built and run under X11 on Linux in the development sandbox** — window opens, the level list renders with correct
  fonts/colours/layout, and a real simulated tap through `xdotool` (the actual OS input path, not a test hook)
  selects a cell, enters a digit, and solves level 1 end to end.
  - One real bug found and fixed in that process: the original repaint-on-change gate (skip the draw entirely when
    `ui_wants_redraw()` is false, mirroring `backend_sdl3.c`'s `SDL_WaitEvent` idle behaviour) left the window
    blank after the first couple of frames on this sandbox's virtual X11 display — bisected against a minimal
    sokol_gp sample to confirm the repaint-skip was the exact cause. The backend now redraws every frame
    unconditionally, so the "idle costs no CPU" property is lost for now; worth re-testing the skip on a real
    compositor/device before shipping.
  - Android's hardware back button isn't mapped yet (no `SAPP_KEYCODE_*` the way SDL synthesizes `SDLK_AC_BACK`) —
    needs a look once an Android build is attempted.
  - sokol_gp's published header (`edubart/sokol_gp`, last tagged v0.7.0) predates an October 2026 resource-API
    rewrite in upstream `sokol_gfx.h`/`sokol_app.h`; vendoring `floooh/sokol`'s current `master` breaks the build.
    The fix was to vendor the exact `sokol_app.h`/`sokol_gfx.h`/`sokol_glue.h`/`sokol_log.h` that `sokol_gp`'s own
    repo bundles under its `thirdparty/` directory instead — keep pulling from there, not `floooh/sokol` directly,
    until sokol_gp itself updates.
- **`ui/backend_sdl3.c`** — kept as a secondary backend, unchanged. Build with `make build/sudoku-sdl` where SDL3 is
  installed. **Status:** builds via `pkg-config` in the current development sandbox (SDL3 3.4.18) — that was not true
  when this file was first written (not packaged, download site blocked; see git history), so the packaging
  situation has already shifted once. Runtime behaviour wasn't chased down in this pass (opened no visible window
  in the time spent); treat as "builds, runtime unverified" until someone drives it the way `backend_sokol.c` was
  driven above.
- **`ui/shot.c`** — software renderer + scripted driver. Needs no window system, so it runs anywhere. It renders the same
  draw list to PPM files and can drive the UI with simulated taps and keys. This is what the screenshots and `make test-ui`
  use.

## Testing

`make test-ui` plays all 14 levels in portrait (390x844) and landscape (844x390) using **only simulated taps on widgets**
(select a cell, tap a digit, tap bomb) and asserts the engine reports solved. That exercises layout hit-testing, the action
layer and the engine API together.

Screenshots: `./build/uishot 390 844 level 12 click cell 2 click hint shot out.ppm` (commands are listed at the top of
`ui/shot.c`).

## Memory

All UI memory is static and known at link time. From the current build (code + static data, excluding libc):

| Item | Size |
|---|---|
| Draw list (4096 primitives) + text pool | ~110 KB |
| Widget and event pools, UI state | ~10 KB |
| Engine pools (cells, rules, history, solver trace) | ~40 KB |
| **Total static data** | **~165 KB** (code ~46 KB) |

Pool capacities are `#define`s at the top of each header (`MAX_DC`, `MAX_TEXT`, `MAX_HIST`, ...). A 6x6 board uses a small
fraction of the draw list; the rest is headroom for larger grids.

## What the UI does today

Level list with finished markers; board with givens (bold), player digits (blue), pencil notes, bomb badges, hint cells with a
colour *and* a ring (colour is never the only cue), neighbourhood tint when a hint cell is selected, row/column clue labels,
mine counter, check marks (red frames), structured hint highlighting (unit or neighbourhood, green target cell), undo/redo,
a notes mode, a number pad, a solved screen with next-level. Keyboard: digits, arrows, backspace, `n` notes, `b` bomb,
`u`/`r` undo/redo, `c` check, `h` hint, Esc back.

## Not done yet

- Persistence of finished levels and in-progress boards (the engine has `eng_save`/`eng_load`; the UI needs a storage hook).
- A pre-generated seed list (the UI currently uses seed 1 for every level via `func_level_seed`).
- Haptics, sound, animations, a vector font, and safe-area insets on notched phones.
- A drag-to-scroll level list (it fits today's 14 levels on a phone-sized screen).
