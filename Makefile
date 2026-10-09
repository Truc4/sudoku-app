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

.PHONY: test test-ui clean
