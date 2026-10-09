/*
 * UI core. Platform-independent: it never touches a window, GPU, file or the heap.
 *
 *   input  : a backend pushes pointer and key events as data (ui_pointer, ui_key)
 *   frame  : ui_frame(w, h) consumes the events, drives the engine API, lays out the screen for a w x h pixel
 *            surface and writes a draw list
 *   output : the draw list is a set of parallel arrays (dc_*); a backend just walks them
 *
 * It talks to the game only through engine.h. Coordinates are pixels, origin top-left, y down. Colours are 0xRRGGBBAA.
 * Memory is fixed: every pool below is a static array, so peak use is known at link time.
 */
#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "../src/engine.h"
#include "font.h"

/* ---- draw list ---- */
enum { DC_RECT, DC_FRAME, DC_CIRCLE, DC_TEXT };
#define MAX_DC    4096
#define MAX_TEXT  16384
extern int      dc_count;
extern uint8_t  dc_kind[MAX_DC];
extern float    dc_x[MAX_DC], dc_y[MAX_DC], dc_w[MAX_DC], dc_h[MAX_DC]; /* CIRCLE: centre x,y and radius in dc_w */
extern uint32_t dc_rgba[MAX_DC];
extern int16_t  dc_t[MAX_DC];       /* FRAME: border thickness; TEXT: pixel scale of the 5x7 font */
extern int32_t  dc_text_off[MAX_DC];/* TEXT: offset of its zero-terminated string in dc_text */
extern char     dc_text[MAX_TEXT];

/* ---- input ---- */
enum { UI_PTR_DOWN, UI_PTR_UP, UI_PTR_MOVE };
enum { UI_KEY_LEFT = 1000, UI_KEY_RIGHT, UI_KEY_UP, UI_KEY_DOWN, UI_KEY_BACKSPACE, UI_KEY_ESCAPE, UI_KEY_ENTER };
void ui_pointer(int kind, float x, float y);   /* a touch and a mouse are the same thing here */
void ui_key(int key);                          /* printable keys as their ASCII code, specials as UI_KEY_* */

/* ---- lifecycle ---- */
void ui_init(void);                            /* start on the level list */
void ui_frame(int width, int height);          /* handle events, lay out, rebuild the draw list */
int  ui_start_game(int level);                 /* jump straight into a level (also used by tests) */
int  ui_wants_redraw(void);                    /* 1 if state changed since the last ui_frame (lets a backend sleep) */

/* ---- widgets (hit regions rebuilt every frame; exposed for tests and accessibility) ---- */
enum { ACT_NONE, ACT_CELL, ACT_DIGIT, ACT_CLEAR, ACT_NOTES, ACT_BOMB, ACT_UNDO, ACT_REDO, ACT_CHECK, ACT_HINT,
       ACT_BACK, ACT_LEVEL, ACT_NEXT };
int  ui_find_widget(int action, int arg, float *cx, float *cy);   /* 1 if present this frame */
#endif
