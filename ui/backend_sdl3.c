/*
 * SDL3 backend: a window, input and a renderer around the platform-independent UI core. This file is the only
 * place that knows about SDL. It holds no game logic and no layout; it
 *   1. turns SDL events into ui_pointer / ui_key calls,
 *   2. calls ui_frame with the drawable size in pixels,
 *   3. walks the draw list with SDL_Renderer calls.
 *
 * Touch needs no separate path: SDL3 synthesises mouse events from touch by default, and the UI treats a tap and a
 * click identically. Rendering only happens when something changed (or the window needs repainting), so an idle game
 * costs no CPU or battery.
 *
 * Status: written against the SDL3 API but NOT yet compiled or run (SDL3 was not available where this was written).
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <string.h>
#include "ui.h"

static SDL_Texture *atlas;                         /* 64 glyphs of 8x8 cells, white on transparent */

static int sys_build_atlas(SDL_Renderer *ren) {
    static uint32_t pix[8 * 64 * 8];               /* 512 x 8, RGBA */
    memset(pix, 0, sizeof pix);
    for (int ch = 32; ch < 96; ch++) {
        const uint8_t *g = func_glyph(ch);
        for (int r = 0; r < FONT_H; r++)
            for (int b = 0; b < FONT_W; b++)
                if (g[r] & (1 << (FONT_W - 1 - b))) pix[r * 512 + (ch - 32) * 8 + b] = 0xFFFFFFFFu;
    }
    atlas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 512, 8);
    if (!atlas) return 0;
    SDL_UpdateTexture(atlas, NULL, pix, 512 * 4);
    SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    return 1;
}

static void set_color(SDL_Renderer *ren, uint32_t c) {
    SDL_SetRenderDrawColor(ren, (Uint8)(c >> 24), (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c);
}
static void fill(SDL_Renderer *ren, float x, float y, float w, float h) {
    SDL_FRect r = { x, y, w, h };
    SDL_RenderFillRect(ren, &r);
}

static void render(SDL_Renderer *ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < dc_count; i++) {
        uint32_t c = dc_rgba[i];
        float x = dc_x[i], y = dc_y[i], w = dc_w[i], h = dc_h[i];
        switch (dc_kind[i]) {
        case DC_RECT:
            set_color(ren, c); fill(ren, x, y, w, h);
            break;
        case DC_FRAME: {
            float t = dc_t[i];
            set_color(ren, c);
            fill(ren, x, y, w, t); fill(ren, x, y + h - t, w, t);
            fill(ren, x, y + t, t, h - 2 * t); fill(ren, x + w - t, y + t, t, h - 2 * t);
            break; }
        case DC_CIRCLE: {
            float r = w;                           /* scanline fill: one rect per row */
            set_color(ren, c);
            for (int j = (int)(y - r); j <= (int)(y + r); j++) {
                float dy = (float)j + 0.5f - y, half2 = r * r - dy * dy;
                if (half2 <= 0) continue;
                float half = SDL_sqrtf(half2);
                fill(ren, x - half, (float)j, 2 * half, 1.0f);
            }
            break; }
        case DC_TEXT: {
            const char *s = dc_text + dc_text_off[i];
            int sc = dc_t[i];
            SDL_SetTextureColorMod(atlas, (Uint8)(c >> 24), (Uint8)(c >> 16), (Uint8)(c >> 8));
            SDL_SetTextureAlphaMod(atlas, (Uint8)c);
            for (int n = 0; s[n]; n++) {
                int ch = (unsigned char)s[n];
                if (ch >= 'a' && ch <= 'z') ch -= 32;
                if (ch < 32 || ch > 95) ch = '?';
                SDL_FRect src = { (float)((ch - 32) * 8), 0, (float)FONT_W, (float)FONT_H };
                SDL_FRect dst = { x + (float)(n * (FONT_W + 1) * sc), y, (float)(FONT_W * sc), (float)(FONT_H * sc) };
                SDL_RenderTexture(ren, atlas, &src, &dst);
            }
            break; }
        }
    }
}

static int func_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_LEFT: return UI_KEY_LEFT;
    case SDLK_RIGHT: return UI_KEY_RIGHT;
    case SDLK_UP: return UI_KEY_UP;
    case SDLK_DOWN: return UI_KEY_DOWN;
    case SDLK_BACKSPACE: case SDLK_DELETE: return UI_KEY_BACKSPACE;
    case SDLK_ESCAPE: case SDLK_AC_BACK: return UI_KEY_ESCAPE;   /* Android back button */
    case SDLK_RETURN: return UI_KEY_ENTER;
    default: return (k >= 32 && k < 127) ? (int)k : 0;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    SDL_Window *win; SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("Sudoku, But...", 390, 844, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
        SDL_Log("window: %s", SDL_GetError()); return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    if (!sys_build_atlas(ren)) { SDL_Log("atlas: %s", SDL_GetError()); return 1; }
    ui_init();

    int running = 1, repaint = 1;
    while (running) {
        SDL_Event e;
        if (!repaint && !ui_wants_redraw()) { if (!SDL_WaitEvent(&e)) break; } else if (!SDL_PollEvent(&e)) e.type = 0;
        do {
            int ww, wh, pw, ph;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_GetCurrentRenderOutputSize(ren, &pw, &ph);
            float sx = ww > 0 ? (float)pw / (float)ww : 1.0f, sy = wh > 0 ? (float)ph / (float)wh : 1.0f;   /* window units -> pixels */
            switch (e.type) {
            case SDL_EVENT_QUIT: running = 0; break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: if (e.button.button == SDL_BUTTON_LEFT) ui_pointer(UI_PTR_DOWN, e.button.x * sx, e.button.y * sy); break;
            case SDL_EVENT_MOUSE_BUTTON_UP:   if (e.button.button == SDL_BUTTON_LEFT) ui_pointer(UI_PTR_UP, e.button.x * sx, e.button.y * sy); break;
            case SDL_EVENT_MOUSE_MOTION:      ui_pointer(UI_PTR_MOVE, e.motion.x * sx, e.motion.y * sy); break;
            case SDL_EVENT_KEY_DOWN: { int k = func_key(e.key.key); if (k) ui_key(k); break; }
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_EXPOSED: repaint = 1; break;
            default: break;
            }
        } while (running && SDL_PollEvent(&e));
        if (!running) break;
        if (repaint || ui_wants_redraw()) {
            int pw, ph;
            SDL_GetCurrentRenderOutputSize(ren, &pw, &ph);
            ui_frame(pw, ph);
            render(ren);
            SDL_RenderPresent(ren);
            repaint = 0;
        }
    }
    SDL_DestroyTexture(atlas);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
