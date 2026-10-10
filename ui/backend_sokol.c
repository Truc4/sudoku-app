/*
 * sokol_app/sokol_gfx/sokol_gp backend: a window, input and a renderer around the platform-independent UI core.
 * Same role as backend_sdl3.c (that file is still built and kept as a secondary backend) but on a platform layer
 * that is vendored in-tree (third_party/sokol, single-header C, no package manager) and has an official story
 * for desktop + Android + iOS from one API. This file is the only place that knows about sokol; it holds no game
 * logic and no layout.
 *
 *   1. turns sokol_app events into ui_pointer / ui_key calls (mouse and touch both feed ui_pointer; printable keys
 *      come from the CHAR event so case/shift match what SDL's keycode-is-the-unshifted-char convention gave us,
 *      specials come from KEY_DOWN),
 *   2. calls ui_frame with the framebuffer size in pixels (sokol_app's mouse/touch coords and sapp_width/height
 *      are already in framebuffer pixels when high_dpi is requested, so no window-to-pixel scaling is needed here,
 *      unlike backend_sdl3.c),
 *   3. walks the draw list with sokol_gp immediate-mode 2D calls.
 *
 * Status: built and run under X11 on Linux in this sandbox -- window opens, level list renders, a real simulated
 * tap (via xdotool, through the actual OS input path, not a test hook) selects a cell, enters a digit and solves
 * level 1 end to end. First backend for this project that's actually been exercised past a syntax check.
 *
 * Known gap: sokol_app has no "block until an event arrives" primitive like SDL_WaitEvent, so unlike
 * backend_sdl3.c this can't fully sleep when idle. Worse, in this sandbox (virtual X11 display, software GL),
 * skipping the sgp_begin/.../sg_commit sequence on frames where ui_wants_redraw() is false left the window
 * content blank after the first couple of frames -- confirmed by bisecting against a minimal sokol_gp sample,
 * where removing the exact same repaint-skip was the only change that fixed it. So every frame redraws
 * unconditionally here, same as a typical game loop; the "idle costs no CPU" property backend_sdl3.c has via
 * SDL_WaitEvent is lost for now. Revisit with an explicit frame-rate cap (or re-test the skip on a real
 * compositor/device, where it may simply work) if battery use matters before a real device test.
 *
 * Known gap: Android's hardware back button is not mapped yet (there's no SAPP_KEYCODE_* for it the way SDL
 * synthesizes SDLK_AC_BACK); needs a look once an Android build is attempted.
 */
#define SOKOL_IMPL
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define SOKOL_GLES3     /* WebGL2 on Emscripten, the only backend sokol_app's Android path supports */
#else
#define SOKOL_GLCORE    /* desktop: Linux/macOS/Windows */
#endif
#include "../third_party/sokol/sokol_app.h"
#include "../third_party/sokol/sokol_gfx.h"
#include "../third_party/sokol/sokol_gp.h"
#include "../third_party/sokol/sokol_glue.h"
#include "../third_party/sokol/sokol_log.h"
#include <string.h>
#include <math.h>
#include "ui.h"

static sg_image atlas;                              /* 64 glyphs of 8x8 cells, white on transparent, RGBA8 */

static void sys_build_atlas(void) {
    static uint32_t pix[8 * 64 * 8];                /* 512 x 8, RGBA */
    memset(pix, 0, sizeof pix);
    for (int ch = 32; ch < 96; ch++) {
        const uint8_t *g = func_glyph(ch);
        for (int r = 0; r < FONT_H; r++)
            for (int b = 0; b < FONT_W; b++)
                if (g[r] & (1 << (FONT_W - 1 - b))) pix[r * 512 + (ch - 32) * 8 + b] = 0xFFFFFFFFu;
    }
    atlas = sg_make_image(&(sg_image_desc){
        .width = 512, .height = 8,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.subimage[0][0] = SG_RANGE(pix),
        .label = "glyph-atlas",
    });
}

static void rgba_to_f(uint32_t c, float *r, float *g, float *b, float *a) {
    *r = (float)((c >> 24) & 0xFF) / 255.0f;
    *g = (float)((c >> 16) & 0xFF) / 255.0f;
    *b = (float)((c >> 8) & 0xFF) / 255.0f;
    *a = (float)(c & 0xFF) / 255.0f;
}

static void render(void) {
    sgp_set_blend_mode(SGP_BLENDMODE_BLEND);
    for (int i = 0; i < dc_count; i++) {
        float r, g, b, a;
        rgba_to_f(dc_rgba[i], &r, &g, &b, &a);
        float x = dc_x[i], y = dc_y[i], w = dc_w[i], h = dc_h[i];
        switch (dc_kind[i]) {
        case DC_RECT:
            sgp_set_color(r, g, b, a);
            sgp_draw_filled_rect(x, y, w, h);
            break;
        case DC_FRAME: {
            float t = dc_t[i];
            sgp_set_color(r, g, b, a);
            sgp_draw_filled_rect(x, y, w, t); sgp_draw_filled_rect(x, y + h - t, w, t);
            sgp_draw_filled_rect(x, y + t, t, h - 2 * t); sgp_draw_filled_rect(x + w - t, y + t, t, h - 2 * t);
            break; }
        case DC_CIRCLE: {
            float radius = w;                      /* a fan of triangles from the centre */
            enum { SEG = 24 };
            sgp_triangle tris[SEG];
            for (int s = 0; s < SEG; s++) {
                float th0 = (float)s / (float)SEG * 6.28318530718f;
                float th1 = (float)(s + 1) / (float)SEG * 6.28318530718f;
                tris[s].a.x = x; tris[s].a.y = y;
                tris[s].b.x = x + radius * cosf(th0); tris[s].b.y = y + radius * sinf(th0);
                tris[s].c.x = x + radius * cosf(th1); tris[s].c.y = y + radius * sinf(th1);
            }
            sgp_set_color(r, g, b, a);
            sgp_draw_filled_triangles(tris, SEG);
            break; }
        case DC_TEXT: {
            const char *s = dc_text + dc_text_off[i];
            int sc = dc_t[i];
            sgp_set_color(r, g, b, a);
            sgp_set_image(0, atlas);
            for (int n = 0; s[n]; n++) {
                int ch = (unsigned char)s[n];
                if (ch >= 'a' && ch <= 'z') ch -= 32;
                if (ch < 32 || ch > 95) ch = '?';
                sgp_rect src = { (float)((ch - 32) * 8), 0, (float)FONT_W, (float)FONT_H };
                sgp_rect dst = { x + (float)(n * (FONT_W + 1) * sc), y, (float)(FONT_W * sc), (float)(FONT_H * sc) };
                sgp_draw_textured_rect(0, dst, src);
            }
            sgp_reset_image(0);
            break; }
        }
    }
}

static int func_key(sapp_keycode k) {
    switch (k) {
    case SAPP_KEYCODE_LEFT: return UI_KEY_LEFT;
    case SAPP_KEYCODE_RIGHT: return UI_KEY_RIGHT;
    case SAPP_KEYCODE_UP: return UI_KEY_UP;
    case SAPP_KEYCODE_DOWN: return UI_KEY_DOWN;
    case SAPP_KEYCODE_BACKSPACE: case SAPP_KEYCODE_DELETE: return UI_KEY_BACKSPACE;
    case SAPP_KEYCODE_ESCAPE: return UI_KEY_ESCAPE;
    case SAPP_KEYCODE_ENTER: return UI_KEY_ENTER;
    default: return 0;
    }
}

static void on_init(void) {
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = slog_func });
    sgp_setup(&(sgp_desc){0});
    sys_build_atlas();
    ui_init();
}

static void on_frame(void) {
    int w = sapp_width(), h = sapp_height();
    ui_frame(w, h);
    sgp_begin(w, h);
    sgp_viewport(0, 0, w, h);
    sgp_set_color(0, 0, 0, 1);
    sgp_clear();
    render();
    sg_pass pass = { .swapchain = sglue_swapchain() };
    sg_begin_pass(&pass);
    sgp_flush();
    sgp_end();
    sg_end_pass();
    sg_commit();
}

static void on_event(const sapp_event *e) {
    switch (e->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN: if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) ui_pointer(UI_PTR_DOWN, e->mouse_x, e->mouse_y); break;
    case SAPP_EVENTTYPE_MOUSE_UP:   if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) ui_pointer(UI_PTR_UP, e->mouse_x, e->mouse_y); break;
    case SAPP_EVENTTYPE_MOUSE_MOVE: ui_pointer(UI_PTR_MOVE, e->mouse_x, e->mouse_y); break;
    case SAPP_EVENTTYPE_TOUCHES_BEGAN: if (e->num_touches > 0) ui_pointer(UI_PTR_DOWN, e->touches[0].pos_x, e->touches[0].pos_y); break;
    case SAPP_EVENTTYPE_TOUCHES_MOVED: if (e->num_touches > 0) ui_pointer(UI_PTR_MOVE, e->touches[0].pos_x, e->touches[0].pos_y); break;
    case SAPP_EVENTTYPE_TOUCHES_ENDED: case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
        if (e->num_touches > 0) ui_pointer(UI_PTR_UP, e->touches[0].pos_x, e->touches[0].pos_y); break;
    case SAPP_EVENTTYPE_KEY_DOWN: { int k = func_key(e->key_code); if (k) ui_key(k); break; }
    case SAPP_EVENTTYPE_CHAR: if (e->char_code >= 32 && e->char_code < 127) ui_key((int)e->char_code); break;
    default: break;
    }
}

static void on_cleanup(void) {
    sgp_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = on_init,
        .frame_cb = on_frame,
        .event_cb = on_event,
        .cleanup_cb = on_cleanup,
        .width = 390,
        .height = 844,
        .window_title = "Sudoku, But...",
        .high_dpi = true,
        .logger.func = slog_func,
    };
}
