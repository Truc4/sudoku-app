/*
 * Software backend + scripted driver: renders the UI draw list into an RGB image and writes PPM files.
 * Needs no window system, so it runs in CI and in headless sandboxes, and doubles as a UI test tool.
 *
 *   uishot W H cmd...      commands, run in order:
 *     level N              start level N (1-based)
 *     frame                run a frame
 *     click ACT [ARG]      tap the centre of a widget (digit 5, cell 7, notes, bomb, undo, redo, check, hint,
 *                          back, level 3, next, clear)
 *     key C                send a key (a character, or left/right/up/down/backspace/escape)
 *     shot FILE.ppm        render the current frame to a file
 *     play                 solve the level using only taps on widgets (cells, digits, bomb)
 *     expect solved|open   exit non-zero if the engine state differs
 *     solve                fill the board with the solution through the API (to see the solved screen)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

static uint8_t *fb; static int FW, FH;

static void put(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= FW || y >= FH) return;
    uint8_t *p = fb + ((size_t)y * (size_t)FW + (size_t)x) * 3;
    unsigned a = c & 255, r = c >> 24, g = (c >> 16) & 255, b = (c >> 8) & 255;
    if (a == 255) { p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; return; }
    p[0] = (uint8_t)((p[0] * (255 - a) + r * a) / 255); p[1] = (uint8_t)((p[1] * (255 - a) + g * a) / 255); p[2] = (uint8_t)((p[2] * (255 - a) + b * a) / 255);
}
static void fill(float x, float y, float w, float h, uint32_t c) {
    int x0 = (int)(x + 0.5f), y0 = (int)(y + 0.5f), x1 = (int)(x + w + 0.5f), y1 = (int)(y + h + 0.5f);
    for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) put(i, j, c);
}

static void render(void) {
    for (int i = 0; i < dc_count; i++) {
        uint32_t c = dc_rgba[i];
        switch (dc_kind[i]) {
        case DC_RECT: fill(dc_x[i], dc_y[i], dc_w[i], dc_h[i], c); break;
        case DC_FRAME: {
            float x = dc_x[i], y = dc_y[i], w = dc_w[i], h = dc_h[i], t = dc_t[i];
            fill(x, y, w, t, c); fill(x, y + h - t, w, t, c); fill(x, y + t, t, h - 2 * t, c); fill(x + w - t, y + t, t, h - 2 * t, c);
            break; }
        case DC_CIRCLE: {
            float r = dc_w[i];
            for (int j = (int)(dc_y[i] - r - 1); j <= (int)(dc_y[i] + r + 1); j++)
                for (int k = (int)(dc_x[i] - r - 1); k <= (int)(dc_x[i] + r + 1); k++) {
                    float dx = (float)k + 0.5f - dc_x[i], dy = (float)j + 0.5f - dc_y[i];
                    if (dx * dx + dy * dy <= r * r) put(k, j, c);
                }
            break; }
        case DC_TEXT: {
            const char *s = dc_text + dc_text_off[i]; int sc = dc_t[i];
            for (int n = 0; s[n]; n++) {
                const uint8_t *g = func_glyph(s[n]);
                for (int r = 0; r < FONT_H; r++) for (int b = 0; b < FONT_W; b++)
                    if (g[r] & (1 << (FONT_W - 1 - b)))
                        fill(dc_x[i] + (float)(n * (FONT_W + 1) * sc + b * sc), dc_y[i] + (float)(r * sc), (float)sc, (float)sc, c);
            }
            break; }
        }
    }
}

static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", FW, FH); fwrite(fb, 3, (size_t)FW * (size_t)FH, f); fclose(f);
}

static int key_of(const char *s) {
    if (!strcmp(s, "left")) return UI_KEY_LEFT;
    if (!strcmp(s, "right")) return UI_KEY_RIGHT;
    if (!strcmp(s, "up")) return UI_KEY_UP;
    if (!strcmp(s, "down")) return UI_KEY_DOWN;
    if (!strcmp(s, "backspace")) return UI_KEY_BACKSPACE;
    if (!strcmp(s, "escape")) return UI_KEY_ESCAPE;
    return s[0];
}
static int act_of(const char *s) {
    static const char *names[] = { "", "cell", "digit", "clear", "notes", "bomb", "undo", "redo", "check", "hint", "back", "level", "next" };
    for (int i = 1; i < 13; i++) if (!strcmp(names[i], s)) return i;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: uishot W H cmd...\n"); return 2; }
    FW = atoi(argv[1]); FH = atoi(argv[2]);
    fb = malloc((size_t)FW * (size_t)FH * 3);                    /* test tool: heap is fine here */
    ui_init(); ui_frame(FW, FH);
    for (int a = 3; a < argc; a++) {
        const char *c = argv[a];
        if (!strcmp(c, "level") && a + 1 < argc) { ui_start_game(atoi(argv[++a]) - 1); ui_frame(FW, FH); }
        else if (!strcmp(c, "frame")) ui_frame(FW, FH);
        else if (!strcmp(c, "key") && a + 1 < argc) { ui_key(key_of(argv[++a])); ui_frame(FW, FH); }
        else if (!strcmp(c, "click") && a + 1 < argc) {
            int act = act_of(argv[++a]), arg = 0; float x, y;
            if (a + 1 < argc && (argv[a + 1][0] >= '0' && argv[a + 1][0] <= '9')) arg = atoi(argv[++a]);
            if (act == ACT_LEVEL || act == ACT_CELL) arg -= 1;       /* 1-based on the command line */
            ui_frame(FW, FH);
            if (!ui_find_widget(act, arg, &x, &y)) { fprintf(stderr, "no widget %s %d\n", argv[a], arg); return 1; }
            ui_pointer(UI_PTR_DOWN, x, y); ui_frame(FW, FH); ui_pointer(UI_PTR_UP, x, y); ui_frame(FW, FH);
        }
        else if (!strcmp(c, "solve")) {
            for (int i = 0; i < eng_width() * eng_height(); i++) { if (!cell_given[i]) eng_set_digit(i, cell_sol[i]); if (cell_mine[i]) eng_toggle_bomb(i); }
            ui_frame(FW, FH); ui_key('h'); ui_frame(FW, FH);
        }
        else if (!strcmp(c, "play")) {          /* solve the level using only taps on widgets */
            for (int i = 0; i < eng_width() * eng_height(); i++) {
                float x, y;
                if (!cell_given[i]) {
                    ui_frame(FW, FH); if (!ui_find_widget(ACT_CELL, i, &x, &y)) return 1;
                    ui_pointer(UI_PTR_DOWN, x, y); ui_frame(FW, FH); ui_pointer(UI_PTR_UP, x, y); ui_frame(FW, FH);
                    ui_frame(FW, FH); if (!ui_find_widget(ACT_DIGIT, cell_sol[i], &x, &y)) return 1;
                    ui_pointer(UI_PTR_DOWN, x, y); ui_frame(FW, FH); ui_pointer(UI_PTR_UP, x, y); ui_frame(FW, FH);
                }
                if (cell_mine[i] && eng_has_mines()) {
                    ui_frame(FW, FH); if (!ui_find_widget(ACT_CELL, i, &x, &y)) return 1;
                    ui_pointer(UI_PTR_DOWN, x, y); ui_frame(FW, FH); ui_pointer(UI_PTR_UP, x, y); ui_frame(FW, FH);
                    ui_frame(FW, FH); if (!ui_find_widget(ACT_BOMB, 0, &x, &y)) return 1;
                    ui_pointer(UI_PTR_DOWN, x, y); ui_frame(FW, FH); ui_pointer(UI_PTR_UP, x, y); ui_frame(FW, FH);
                }
            }
        }
        else if (!strcmp(c, "expect") && a + 1 < argc) {
            int want = !strcmp(argv[++a], "solved");
            if (eng_is_solved() != want) { fprintf(stderr, "expected %s\n", argv[a]); return 1; }
        }
        else if (!strcmp(c, "shot") && a + 1 < argc) { fill(0, 0, (float)FW, (float)FH, 0x000000FF); render(); write_ppm(argv[++a]); }
        else { fprintf(stderr, "bad command %s\n", c); return 2; }
    }
    return 0;
}
