# UI architecture and library choice

## Decision

**SDL3 as the platform layer, and our own UI on top of it.** No widget toolkit, no retained-mode framework.

Why this fits "maximum flexibility, control over memory, nothing like React":

- **A sudoku board is custom-drawn anyway.** The only generic widgets needed are buttons and a list. Hit regions, layout and
  drawing are a few hundred lines of C, so a toolkit would add more than it saves.
- **SDL3 does the parts that are painful to own:** windows, input (mouse, keyboard, touch), HiDPI, app lifecycle and audio, on
  Linux, macOS, Windows, Android and iOS from one C API. It is plain C, so it also matches the language the engine is written
  in and, later, Arche's C bridge.
- **Memory control.** Everything the game itself owns is static (see "Memory" below). SDL allocates internally for windows
  and textures, and exposes `SDL_SetMemoryFunctions` if you ever want to route those through an arena.
- **No lock-in.** The UI core never calls SDL. Swapping the platform layer means rewriting one ~150-line file.

Alternatives considered:

| Option | Verdict |
|---|---|
| **sokol_app + sokol_gfx** | Tiny, header-only, very controllable, mobile support. A strong second choice; it is a graphics/window layer with no audio, and 2D text/primitives are entirely DIY. Pick it if you want the smallest dependency footprint. |
| **raylib** | Easiest to start, great for desktop. Mobile builds are clunkier, and it owns more of the loop and its memory than we want. |
| **Dear ImGui / Nuklear / Clay** | Immediate-mode widget/layout libraries. Good for tools; for a custom-drawn game board they mostly add a competing layout model. Clay (layout only, static arena, renderer-agnostic) is the one worth revisiting if layouts get complicated. |
| **GTK / Qt / Flutter / web stacks** | Heavy runtimes with their own memory behaviour. Out. |

## Architecture

```
          engine (src/engine.h)           game rules, solver, play state. No UI concepts.
                  ^
                  |  eng_* calls only
          UI core (ui/ui.c)               layout, input -> actions, draw list. No SDL, no heap, no files.
            ^                 |
   ui_pointer / ui_key        |  draw list: parallel arrays dc_* (rect, frame, circle, text)
            |                 v
   backend (ui/backend_sdl3.c  or  ui/shot.c)
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

- **`ui/backend_sdl3.c`** — the real app: window, mouse/touch/keyboard, repaint-on-change (an idle game uses no CPU).
  Build with `make build/sudoku-sdl` where SDL3 is installed. **Status: written, not yet compiled or run.** SDL3 is not
  packaged for the Ubuntu 24.04 image used for development here and the SDL download site is blocked by the sandbox network
  policy. Only a syntax check against a hand-written stub was possible, which catches C mistakes but not API mismatches.
  Expect small fixes on the first real build.
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
