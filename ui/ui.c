#include <stdio.h>
#include <string.h>
#include "ui.h"

#define RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))
#define C_BG        RGBA(244, 241, 234, 255)
#define C_PANEL     RGBA(228, 224, 214, 255)
#define C_BTN       RGBA(255, 255, 255, 255)
#define C_BTN_DOWN  RGBA(200, 210, 230, 255)
#define C_ACCENT    RGBA(52, 120, 220, 255)
#define C_INK       RGBA(30, 30, 36, 255)
#define C_INK_SOFT  RGBA(110, 110, 120, 255)
#define C_PLAYER    RGBA(40, 90, 200, 255)
#define C_CELL      RGBA(255, 255, 255, 255)
#define C_SEL       RGBA(176, 204, 245, 255)
#define C_SAME      RGBA(222, 232, 248, 255)
#define C_HINT      RGBA(226, 112, 20, 255)
#define C_HINT_BG   RGBA(255, 238, 214, 255)
#define C_HINT_NB   RGBA(255, 150, 40, 60)
#define C_HL        RGBA(255, 214, 0, 80)
#define C_TARGET    RGBA(40, 170, 90, 255)
#define C_WRONG     RGBA(220, 40, 40, 255)
#define C_GRID      RGBA(190, 186, 176, 255)
#define C_BOX       RGBA(50, 50, 60, 255)
#define C_OK        RGBA(40, 150, 80, 255)

/* ---- pools ---- */
int      dc_count;
uint8_t  dc_kind[MAX_DC];
float    dc_x[MAX_DC], dc_y[MAX_DC], dc_w[MAX_DC], dc_h[MAX_DC];
uint32_t dc_rgba[MAX_DC];
int16_t  dc_t[MAX_DC];
int32_t  dc_text_off[MAX_DC];
char     dc_text[MAX_TEXT];
static int text_used;

#define MAX_WD 256
static int     wd_n;
static float   wd_x[MAX_WD], wd_y[MAX_WD], wd_w[MAX_WD], wd_h[MAX_WD];
static uint8_t wd_action[MAX_WD];
static int16_t wd_arg[MAX_WD];

#define MAX_EV 64
enum { EV_KEY = 100 };
static int     ev_n;
static uint8_t ev_kind[MAX_EV];
static float   ev_x[MAX_EV], ev_y[MAX_EV];
static int     ev_key[MAX_EV];

/* ---- state ---- */
enum { SCREEN_LEVELS, SCREEN_GAME };
static int  screen, level, sel = -1, notes_mode, dirty = 1;
static int  pressed_action, pressed_arg;
static char msg[96];
static int  hl_cells[MAX_CELLS], hl_n, hl_target = -1;
static uint8_t level_done[NLEVELS];

static uint64_t func_level_seed(int l) { (void)l; return 1; }   /* replaced by the pre-generated seed list */

/* ---- draw list builders ---- */
static int dc_new(int kind, float x, float y, float w, float h, uint32_t rgba) {
    if (dc_count >= MAX_DC) return -1;
    int i = dc_count++;
    dc_kind[i] = (uint8_t)kind; dc_x[i] = x; dc_y[i] = y; dc_w[i] = w; dc_h[i] = h; dc_rgba[i] = rgba; dc_t[i] = 0;
    return i;
}
static void rect(float x, float y, float w, float h, uint32_t c) { dc_new(DC_RECT, x, y, w, h, c); }
static void frame(float x, float y, float w, float h, int t, uint32_t c) { int i = dc_new(DC_FRAME, x, y, w, h, c); if (i >= 0) dc_t[i] = (int16_t)t; }
static void circle(float cx, float cy, float r, uint32_t c) { dc_new(DC_CIRCLE, cx, cy, r, r, c); }
static float text_w(const char *s, int scale) { int n = (int)strlen(s); return n ? (float)(n * (FONT_W + 1) * scale - scale) : 0; }
static void text(float x, float y, int scale, uint32_t c, const char *s) {
    size_t n = strlen(s);
    if (dc_count >= MAX_DC || text_used + (int)n + 1 > MAX_TEXT) return;
    int i = dc_new(DC_TEXT, x, y, text_w(s, scale), (float)(FONT_H * scale), c);
    dc_t[i] = (int16_t)scale; dc_text_off[i] = text_used;
    memcpy(dc_text + text_used, s, n + 1); text_used += (int)n + 1;
}
static void text_center(float cx, float cy, int scale, uint32_t c, const char *s) {
    text(cx - text_w(s, scale) / 2, cy - (float)(FONT_H * scale) / 2, scale, c, s);
}
static int scale_for(float px) { int s = (int)(px / FONT_H); return s < 1 ? 1 : s; }
static int fit_scale(const char *s, float max_w, int scale) { while (scale > 1 && text_w(s, scale) > max_w) scale--; return scale; }

/* greedy word wrap; returns lines drawn */
static int text_wrapped(float x, float y, float w, int scale, uint32_t c, const char *s, int max_lines) {
    char line[96]; int len = 0, lines = 0, per = (int)(w / ((FONT_W + 1) * scale));
    if (per < 4) return 0;
    const char *p = s;
    while (*p && lines < max_lines) {
        int wl = 0; while (p[wl] && p[wl] != ' ') wl++;
        if (len && len + 1 + wl > per) {
            line[len] = 0; text(x, y + (float)(lines * (FONT_H + 3) * scale), scale, c, line); lines++; len = 0;
            if (lines >= max_lines) return lines;
        }
        if (len) line[len++] = ' ';
        for (int i = 0; i < wl && len < (int)sizeof line - 2; i++) line[len++] = p[i];
        p += wl; while (*p == ' ') p++;
    }
    if (len && lines < max_lines) { line[len] = 0; text(x, y + (float)(lines * (FONT_H + 3) * scale), scale, c, line); lines++; }
    return lines;
}

/* ---- widgets ---- */
static int widget(float x, float y, float w, float h, int action, int arg) {
    if (wd_n >= MAX_WD) return -1;
    wd_x[wd_n] = x; wd_y[wd_n] = y; wd_w[wd_n] = w; wd_h[wd_n] = h; wd_action[wd_n] = (uint8_t)action; wd_arg[wd_n] = (int16_t)arg;
    return wd_n++;
}
static int func_hit(float x, float y) {
    for (int i = wd_n - 1; i >= 0; i--)
        if (x >= wd_x[i] && x < wd_x[i] + wd_w[i] && y >= wd_y[i] && y < wd_y[i] + wd_h[i]) return i;
    return -1;
}
int ui_find_widget(int action, int arg, float *cx, float *cy) {
    for (int i = 0; i < wd_n; i++)
        if (wd_action[i] == action && wd_arg[i] == arg) { *cx = wd_x[i] + wd_w[i] / 2; *cy = wd_y[i] + wd_h[i] / 2; return 1; }
    return 0;
}

/* style: 0 normal, 1 toggled on, 2 disabled */
static int button_scale_override;   /* 0 = fit each label; else force one scale (keeps a row of buttons uniform) */
static void button(float x, float y, float w, float h, const char *label, int action, int arg, int style) {
    int down = pressed_action == action && pressed_arg == arg && action != ACT_NONE;
    uint32_t bg = style == 1 ? C_ACCENT : down ? C_BTN_DOWN : C_BTN;
    rect(x, y, w, h, bg);
    frame(x, y, w, h, 1, C_GRID);
    int sc = button_scale_override ? button_scale_override : fit_scale(label, w * 0.88f, scale_for(h * 0.36f));
    text_center(x + w / 2, y + h / 2, sc, style == 1 ? RGBA(255, 255, 255, 255) : style == 2 ? C_INK_SOFT : C_INK, label);
    if (style != 2) widget(x, y, w, h, action, arg);
}

/* ---- game actions ---- */
static void sys_clear_hint(void) { hl_n = 0; hl_target = -1; }

int ui_start_game(int l) {
    if (l < 0 || l >= NLEVELS) return ENG_ERR_RANGE;
    int e = eng_new_game(l, func_level_seed(l));
    if (e) { snprintf(msg, sizeof msg, "COULD NOT BUILD LEVEL %d", l + 1); return e; }
    level = l; screen = SCREEN_GAME; sel = -1; notes_mode = 0; msg[0] = 0; sys_clear_hint(); dirty = 1;
    return ENG_OK;
}

static void set_hint_msg(int e) {
    sys_clear_hint();
    if (e == ENG_ERR_INCONSISTENT) { snprintf(msg, sizeof msg, "SOMETHING ENTERED IS WRONG - TRY CHECK"); return; }
    if (e) { snprintf(msg, sizeof msg, "NOTHING LEFT TO DO"); return; }
    const char *t = eng_tech_name(hint_tech);
    if (hint_cell >= 0) {
        snprintf(msg, sizeof msg, "HINT: %s - R%dC%d IS %d", t, hint_cell / eng_width() + 1, hint_cell % eng_width() + 1, hint_digit);
        hl_target = hint_cell;
    } else snprintf(msg, sizeof msg, "HINT: %s", t);
    if (hint_rule >= 0) {
        int axis = rule_axis[hint_rule], line = rule_line[hint_rule], w = eng_width(), h = eng_height();
        if (axis == AXIS_CELL && rule_kind[hint_rule] == RULE_MINE_HINT) {
            hl_cells[hl_n++] = line;
            for (int k = 0; k < 8; k++) { int c = func_king_cell(line, k, w, h); if (c >= 0) hl_cells[hl_n++] = c; }
        } else if (axis != AXIS_CELL) {
            int n = func_unit_len(axis, w, h);
            for (int k = 0; k < n; k++) hl_cells[hl_n++] = func_unit_cell(axis, line, k, w, h);
        }
    }
}

static void sys_after_move(void) {
    sys_clear_hint();
    if (eng_is_solved()) { level_done[level] = 1; snprintf(msg, sizeof msg, "SOLVED!"); }
}

static void sys_report(int e) {
    if (e == ENG_ERR_GIVEN) snprintf(msg, sizeof msg, "GIVEN DIGITS CANNOT BE CHANGED");
    else if (e == ENG_ERR_HINT_SAFE) snprintf(msg, sizeof msg, "HINT CELLS ARE SAFE - NO BOMB HERE");
    else if (e == ENG_ERR_NOTHING) snprintf(msg, sizeof msg, "NOTHING TO UNDO OR REDO");
}

static void sys_act(int a, int arg) {
    int e = 0;
    switch (a) {
    case ACT_LEVEL: ui_start_game(arg); return;
    case ACT_NEXT:  ui_start_game(level + 1 < NLEVELS ? level + 1 : level); return;
    case ACT_BACK:  screen = SCREEN_LEVELS; msg[0] = 0; return;
    case ACT_CELL:  sel = arg; msg[0] = 0; sys_clear_hint(); return;
    case ACT_NOTES: notes_mode = !notes_mode; return;
    case ACT_UNDO:  e = eng_undo(); break;
    case ACT_REDO:  e = eng_redo(); break;
    case ACT_CHECK: {
        int n = eng_check();
        if (n) snprintf(msg, sizeof msg, "%d WRONG - MARKED IN RED", n); else snprintf(msg, sizeof msg, "NO MISTAKES SO FAR");
        return;
    }
    case ACT_HINT:  set_hint_msg(eng_hint()); return;
    default: break;
    }
    if (a == ACT_DIGIT || a == ACT_CLEAR || a == ACT_BOMB) {
        if (sel < 0) { snprintf(msg, sizeof msg, "SELECT A CELL FIRST"); return; }
        msg[0] = 0;
        if (a == ACT_CLEAR) e = eng_clear_cell(sel);
        else if (a == ACT_BOMB) e = eng_toggle_bomb(sel);
        else if (notes_mode && !eng_cell_is_given(sel)) e = eng_toggle_note(sel, arg);
        else e = eng_set_digit(sel, play_val[sel] == arg ? 0 : arg);   /* tapping the same digit clears it */
    }
    if (e) sys_report(e); else sys_after_move();
}

static void sys_handle_key(int k) {
    if (screen == SCREEN_LEVELS) return;
    int w = eng_width(), h = eng_height();
    if (k >= '1' && k <= '9') sys_act(ACT_DIGIT, k - '0');
    else if (k == '0' || k == UI_KEY_BACKSPACE) sys_act(ACT_CLEAR, 0);
    else if (k == 'n' || k == 'N') sys_act(ACT_NOTES, 0);
    else if ((k == 'b' || k == 'B') && eng_has_mines()) sys_act(ACT_BOMB, 0);
    else if (k == 'u' || k == 'U') sys_act(ACT_UNDO, 0);
    else if (k == 'r' || k == 'R') sys_act(ACT_REDO, 0);
    else if (k == 'c' || k == 'C') sys_act(ACT_CHECK, 0);
    else if (k == 'h' || k == 'H') sys_act(ACT_HINT, 0);
    else if (k == UI_KEY_ESCAPE) sys_act(ACT_BACK, 0);
    else if (k >= UI_KEY_LEFT && k <= UI_KEY_DOWN) {
        if (sel < 0) { sel = 0; return; }
        int r = sel / w, c = sel % w;
        if (k == UI_KEY_LEFT && c > 0) c--;
        if (k == UI_KEY_RIGHT && c < w - 1) c++;
        if (k == UI_KEY_UP && r > 0) r--;
        if (k == UI_KEY_DOWN && r < h - 1) r++;
        sel = r * w + c; sys_clear_hint();
    }
}

static void sys_process_events(void) {
    for (int i = 0; i < ev_n; i++) {
        if (ev_kind[i] == EV_KEY) { sys_handle_key(ev_key[i]); continue; }
        int hit = func_hit(ev_x[i], ev_y[i]);
        if (ev_kind[i] == UI_PTR_DOWN) {
            pressed_action = hit >= 0 ? wd_action[hit] : ACT_NONE; pressed_arg = hit >= 0 ? wd_arg[hit] : 0;
        } else if (ev_kind[i] == UI_PTR_UP) {
            if (hit >= 0 && pressed_action != ACT_NONE && wd_action[hit] == pressed_action && wd_arg[hit] == pressed_arg)
                sys_act(wd_action[hit], wd_arg[hit]);
            pressed_action = ACT_NONE;
        }
    }
    ev_n = 0;
}

void ui_pointer(int kind, float x, float y) {
    if (ev_n >= MAX_EV) return;
    ev_kind[ev_n] = (uint8_t)kind; ev_x[ev_n] = x; ev_y[ev_n] = y; ev_n++; dirty = 1;
}
void ui_key(int key) {
    if (ev_n >= MAX_EV) return;
    ev_kind[ev_n] = EV_KEY; ev_key[ev_n] = key; ev_n++; dirty = 1;
}
void ui_init(void) { screen = SCREEN_LEVELS; sel = -1; msg[0] = 0; dirty = 1; }
int  ui_wants_redraw(void) { return dirty; }

/* ---- screens ---- */
static void draw_levels(int W, int H) {
    rect(0, 0, (float)W, (float)H, C_BG);
    float m = (float)(W < H ? W : H) * 0.04f;
    int ts = fit_scale("SUDOKU, BUT...", (float)W - 2 * m, scale_for((float)H * 0.05f));
    text_center((float)W / 2, m + (float)(FONT_H * ts), ts, C_INK, "SUDOKU, BUT...");
    float top = m * 2 + (float)(FONT_H * ts) * 2, rowh = ((float)H - top - m) / (float)NLEVELS, gap = rowh * 0.08f;
    if (rowh > (float)H * 0.09f) rowh = (float)H * 0.09f;
    int ns = 99;                                               /* one text size for every row */
    for (int l = 0; l < NLEVELS; l++) {
        float w = (float)W - 2 * m, h = rowh - gap;
        int f = fit_scale(eng_level_name(l), w - h * 2.8f, scale_for(h * 0.36f));
        if (f < ns) ns = f;
    }
    for (int l = 0; l < NLEVELS; l++) {
        float y = top + (float)l * rowh, x = m, w = (float)W - 2 * m, h = rowh - gap;
        int down = pressed_action == ACT_LEVEL && pressed_arg == l;
        rect(x, y, w, h, down ? C_BTN_DOWN : C_BTN);
        frame(x, y, w, h, 1, C_GRID);
        char num[8], name[96];
        snprintf(num, sizeof num, "%d", l + 1);
        int sc = scale_for(h * 0.36f);
        text_center(x + h * 0.55f, y + h / 2, sc, C_ACCENT, num);
        snprintf(name, sizeof name, "%s", eng_level_name(l));
        text(x + h * 1.1f, y + h / 2 - (float)(FONT_H * ns) / 2, ns, C_INK, name);
        if (level_done[l]) text_center(x + w - h * 0.7f, y + h / 2, sc, C_OK, "OK");
        widget(x, y, w, h, ACT_LEVEL, l);
    }
}

static void draw_bomb(float cx, float cy, float r) {
    float s = r * 0.35f;
    rect(cx - r * 1.3f, cy - s / 4, r * 2.6f, s / 2, C_INK);
    rect(cx - s / 4, cy - r * 1.3f, s / 2, r * 2.6f, C_INK);
    circle(cx, cy, r, C_INK);
    circle(cx - r * 0.35f, cy - r * 0.35f, r * 0.22f, RGBA(255, 255, 255, 255));
}

static int func_in_hl(int cell) {
    for (int i = 0; i < hl_n; i++) if (hl_cells[i] == cell) return 1;
    return 0;
}

static void draw_game(int W, int H) {
    int bw = eng_width(), bh = eng_height(), portrait = H >= W, mines = eng_has_mines();
    rect(0, 0, (float)W, (float)H, C_BG);

    /* header */
    float m = (float)(portrait ? W : H) * 0.03f, hh = (float)H * 0.065f;
    if (hh < 30) hh = 30;
    rect(0, 0, (float)W, hh, C_PANEL);
    float back_w = hh * 1.6f;
    button(m, hh * 0.12f, back_w, hh * 0.76f, "BACK", ACT_BACK, 0, 0);
    char title[48], ctr[32] = ""; snprintf(title, sizeof title, "LEVEL %d", level + 1);
    if (mines) snprintf(ctr, sizeof ctr, "BOMBS %d/%d", eng_bombs_marked(), eng_mines_total());
    float room = (float)W - 2 * m - back_w - m * 2;                 /* space for title + counter */
    int hs = scale_for(hh * 0.4f);
    while (hs > 1 && text_w("LEVEL 00", hs) + text_w("BOMBS 00/00", hs) + m > room) hs--;
    text(m + back_w + m, hh / 2 - (float)(FONT_H * hs) / 2, hs, C_INK, title);
    if (mines) {
        uint32_t col = eng_bombs_marked() > eng_mines_total() ? C_WRONG : C_INK;
        text(((float)W - m) - text_w(ctr, hs), hh / 2 - (float)(FONT_H * hs) / 2, hs, col, ctr);
    }

    /* areas: board on top (portrait) or left (landscape), controls below / right */
    float ax = m, ay = hh + m, aw, ah, px, py, pw, ph;
    if (portrait) { aw = (float)W - 2 * m; ah = ((float)H - hh) * 0.54f - m; px = m; py = ay + ah + m; pw = aw; ph = (float)H - py - m; }
    else { aw = ((float)W - 3 * m) * 0.58f; ah = (float)H - hh - 2 * m; px = ax + aw + m; py = ay; pw = (float)W - px - m; ph = ah; }

    int row_clue = 0, col_clue = 0;
    for (int r = 0; r < eng_rule_count(); r++)
        if (rule_kind[r] == RULE_SUM || rule_kind[r] == RULE_SANDWICH) { if (rule_axis[r] == AXIS_ROW) row_clue = 1; else col_clue = 1; }
    float gr = row_clue ? 2.0f : 0, gb = col_clue ? 1.25f : 0;
    float cs = aw / ((float)bw + gr), cs2 = ah / ((float)bh + gb);
    if (cs2 < cs) cs = cs2;
    cs = (float)(int)cs;
    float bx = ax + (aw - ((float)bw + gr) * cs) / 2, by = ay;
    if (portrait) { py = ay + ((float)bh + gb) * cs + m; ph = (float)H - py - m; }

    /* cells */
    int seld = sel >= 0 ? eng_cell_digit(sel) : 0;
    int sel_is_hint = sel >= 0 && eng_cell_hint(sel);
    int dsc = scale_for(cs * 0.58f), nsc = scale_for(cs * 0.28f);
    for (int c = 0; c < bw * bh; c++) {
        float x = bx + (float)(c % bw) * cs, y = by + (float)(c / bw) * cs;
        int d = eng_cell_digit(c), hint = eng_cell_hint(c);
        uint32_t bg = c == sel ? C_SEL : (seld && d == seld) ? C_SAME : hint ? C_HINT_BG : C_CELL;
        rect(x, y, cs, cs, bg);
        if (func_in_hl(c)) rect(x, y, cs, cs, C_HL);
        if (sel_is_hint && func_king_adjacent(sel, c, bw)) rect(x, y, cs, cs, C_HINT_NB);
        if (d) {
            char s[2] = { (char)('0' + d), 0 };
            uint32_t col = hint ? C_HINT : eng_cell_is_given(c) ? C_INK : C_PLAYER;
            text_center(x + cs / 2, y + cs / 2, dsc, col, s);
            if (eng_cell_is_given(c)) text_center(x + cs / 2 + 1, y + cs / 2, dsc, col, s);   /* poor man's bold */
        } else if (play_notes[c]) {
            for (int n = 1; n <= 9; n++) {
                if (!(play_notes[c] & BIT(n))) continue;
                char s[2] = { (char)('0' + n), 0 };
                text_center(x + cs * (0.2f + 0.3f * (float)((n - 1) % 3)), y + cs * (0.2f + 0.3f * (float)((n - 1) / 3)), nsc, C_INK_SOFT, s);
            }
        }
        if (hint) frame(x + cs * 0.07f, y + cs * 0.07f, cs * 0.86f, cs * 0.86f, cs / 16 < 2 ? 2 : (int)(cs / 16), C_HINT);
        if (play_bomb[c]) draw_bomb(x + cs * 0.78f, y + cs * 0.24f, cs * 0.13f);
        if (play_wrong[c]) frame(x + 1, y + 1, cs - 2, cs - 2, 3, C_WRONG);
        if (c == hl_target) frame(x + 1, y + 1, cs - 2, cs - 2, 3, C_TARGET);
        widget(x, y, cs, cs, ACT_CELL, c);
    }
    /* grid lines: thin per cell, thick per 3x3 box */
    for (int i = 0; i <= bw; i++) rect(bx + (float)i * cs - (i % 3 ? 0.5f : 1.5f), by, i % 3 ? 1.0f : 3.0f, (float)bh * cs, i % 3 ? C_GRID : C_BOX);
    for (int i = 0; i <= bh; i++) rect(bx, by + (float)i * cs - (i % 3 ? 0.5f : 1.5f), (float)bw * cs, i % 3 ? 1.0f : 3.0f, i % 3 ? C_GRID : C_BOX);

    /* line clues: rows to the right, columns underneath */
    int cl = scale_for(cs * 0.26f);
    for (int r = 0; r < eng_rule_count(); r++) {
        if (rule_kind[r] != RULE_SUM && rule_kind[r] != RULE_SANDWICH) continue;
        const char *label = rule_kind[r] == RULE_SUM ? "SUM" : "SAND";
        char v[16]; snprintf(v, sizeof v, "%d", rule_param[r]);
        int idx = 0;                                           /* how many earlier clues share this line */
        for (int q = 0; q < r; q++)
            if ((rule_kind[q] == RULE_SUM || rule_kind[q] == RULE_SANDWICH) && rule_axis[q] == rule_axis[r] && rule_line[q] == rule_line[r]) idx++;
        if (rule_axis[r] == AXIS_ROW) {
            char t[32]; snprintf(t, sizeof t, "%s %s", label, v);
            float x = bx + (float)bw * cs + cs * 0.15f, lh = (float)((FONT_H + 3) * cl);
            text(x, by + (float)rule_line[r] * cs + cs * 0.5f - lh + (float)idx * lh, cl, C_INK, t);
        } else {
            float cx = bx + (float)rule_line[r] * cs + cs / 2, y = by + (float)bh * cs + cs * 0.1f + (float)idx * (float)(2 * (FONT_H + 3) * cl);
            text_center(cx, y + (float)(FONT_H * cl) / 2, cl, C_INK, label);
            text_center(cx, y + (float)((FONT_H + 3) * cl) + (float)(FONT_H * cl) / 2, cl, C_INK, v);
        }
    }

    /* controls */
    float gap = pw * 0.02f, msgH = ph * 0.2f, toolH = ph * 0.15f, padH = ph - msgH - 2 * toolH - 3 * gap;
    if (msg[0]) text_wrapped(px, py, pw, scale_for(msgH / 3.4f), !strcmp(msg, "SOLVED!") ? C_OK : C_INK, msg, 3);
    float ty = py + msgH + gap;
    int ti = 0;
    float tw = (pw - 2 * gap) / 3;
    {   /* one text size for the whole toolbar: the smallest that fits any label */
        static const char *labels[] = { "NOTES", "BOMB", "UNDO", "REDO", "CHECK", "HINT" };
        int best = scale_for(toolH * 0.4f);
        for (int i = 0; i < 6; i++) { int f = fit_scale(labels[i], tw * 0.9f, best); if (f < best) best = f; }
        button_scale_override = best;
    }
    #define TOOL(label, act, style) do { button(px + (float)(ti % 3) * (tw + gap), ty + (float)(ti / 3) * (toolH + gap), tw, toolH, label, act, 0, style); ti++; } while (0)
    TOOL("NOTES", ACT_NOTES, notes_mode);
    if (mines) TOOL("BOMB", ACT_BOMB, 0);
    TOOL("UNDO", ACT_UNDO, 0);
    TOOL("REDO", ACT_REDO, 0);
    TOOL("CHECK", ACT_CHECK, 0);
    TOOL("HINT", ACT_HINT, 0);
    #undef TOOL
    button_scale_override = 0;
    float pady = ty + 2 * (toolH + gap);
    if (eng_is_solved()) {
        float bwid = (pw - gap) / 2;
        button_scale_override = fit_scale("NEXT LEVEL", bwid * 0.9f, scale_for(padH * 0.7f * 0.3f));
        button(px, pady, bwid, padH * 0.7f, level + 1 < NLEVELS ? "NEXT LEVEL" : "AGAIN", ACT_NEXT, 0, 0);
        button(px + bwid + gap, pady, bwid, padH * 0.7f, "LEVELS", ACT_BACK, 0, 0);
        button_scale_override = 0;
    } else {
        float cw = (pw - 4 * gap) / 5, chh = (padH - gap) / 2;
        for (int i = 0; i < 10; i++) {
            char s[8]; float x = px + (float)(i % 5) * (cw + gap), y = pady + (float)(i / 5) * (chh + gap);
            if (i < 9) { snprintf(s, sizeof s, "%d", i + 1); button(x, y, cw, chh, s, ACT_DIGIT, i + 1, 0); }
            else button(x, y, cw, chh, "CLEAR", ACT_CLEAR, 0, 0);
        }
    }
}

void ui_frame(int W, int H) {
    sys_process_events();
    dc_count = 0; text_used = 0; wd_n = 0;
    if (screen == SCREEN_LEVELS) draw_levels(W, H); else draw_game(W, H);
    dirty = 0;
}
