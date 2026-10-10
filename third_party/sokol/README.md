# Vendored sokol headers

Single-header C libraries, vendored verbatim (no local modifications) so the build needs no
package manager or network access. Fetched 2026-10-09 from:

- `sokol_app.h`, `sokol_gfx.h`, `sokol_glue.h`, `sokol_log.h` — https://github.com/floooh/sokol
  @ `401f21f8b7039258c35fef75c11d9a8a0e616771` (master)
- `sokol_gp.h` — https://github.com/edubart/sokol_gp
  @ `d2af9f4ef58850b6599a9aea6edaa7bd79ce67b4` (master), v0.7.0

Both are zlib-licensed. See `docs/UI.md` for why sokol_app was chosen as the platform layer.
