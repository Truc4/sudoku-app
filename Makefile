CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

ENGINE := src/core.c src/solver.c src/gen.c src/engine.c
UI     := ui/ui.c ui/font.c

build/sudoku: src/*.c src/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/*.c

# software-rendered UI driver (no window system needed): screenshots and scripted UI tests
build/uishot: ui/shot.c $(UI) $(ENGINE) src/*.h ui/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ ui/shot.c $(UI) $(ENGINE)

# desktop/mobile window using SDL3 (needs libsdl3-dev / SDL3 installed; not buildable in every environment)
build/sudoku-sdl: ui/backend_sdl3.c $(UI) $(ENGINE) src/*.h ui/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) $$(pkg-config --cflags sdl3) -o $@ ui/backend_sdl3.c $(UI) $(ENGINE) $$(pkg-config --libs sdl3)

# desktop/mobile window using sokol_app+sokol_gfx+sokol_gp (vendored header-only in third_party/sokol, no
# package manager needed). Linux link line and -pthread requirement per third_party/sokol/sokol_app.h.
build/sudoku-sokol: ui/backend_sokol.c $(UI) $(ENGINE) src/*.h ui/*.h third_party/sokol/*.h
	@mkdir -p build
	$(CC) -std=c99 -O2 -pthread -o $@ ui/backend_sokol.c $(UI) $(ENGINE) -lGL -lX11 -lXi -lXcursor -ldl -lpthread -lm

run: build/sudoku-sokol
	./build/sudoku-sokol

# web build via Emscripten (needs emcc on PATH: https://emscripten.org/docs/getting_started/downloads.html).
# Same backend_sokol.c, same engine -- SOKOL_GLES3 is selected automatically under __EMSCRIPTEN__.
# Output is named index.html so the build/web directory can be served/deployed (e.g. GitHub Pages) as-is.
build/web/index.html: ui/backend_sokol.c $(UI) $(ENGINE) src/*.h ui/*.h third_party/sokol/*.h web/shell.html
	@mkdir -p build/web
	emcc -std=c99 -O2 -o $@ ui/backend_sokol.c $(UI) $(ENGINE) \
	  -sUSE_WEBGL2=1 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 --shell-file web/shell.html

web: build/web/index.html

# serve the web build locally at http://localhost:8765/ for a quick phone/browser check
serve-web: build/web/index.html
	cd build/web && python3 -m http.server 8765

test: build/sudoku
	./build/sudoku selftest 25

# plays every level through the UI using only simulated taps, in portrait and landscape
test-ui: build/uishot
	@for l in $$(seq 1 14); do \
	  ./build/uishot 390 844 level $$l play expect solved || { echo "FAIL portrait level $$l"; exit 1; }; \
	  ./build/uishot 844 390 level $$l play expect solved || { echo "FAIL landscape level $$l"; exit 1; }; \
	done; echo "ui ok: 14 levels x 2 orientations"

clean:
	rm -rf build

.PHONY: run web serve-web test test-ui clean
