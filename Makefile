CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

build/sudoku: src/*.c src/sb.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/*.c

test: build/sudoku
	./build/sudoku selftest 25

clean:
	rm -rf build

.PHONY: test clean
