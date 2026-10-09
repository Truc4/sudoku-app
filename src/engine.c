#include <string.h>
#include "engine.h"

int      play_val[MAX_CELLS];
uint16_t play_notes[MAX_CELLS];
uint8_t  play_bomb[MAX_CELLS];
uint8_t  play_wrong[MAX_CELLS];

int hint_tech = -1, hint_rule = -1, hint_cell = -1, hint_digit, hint_changes;

static int      cur_level = -1;
static uint64_t cur_seed;
static int      tables_ready;

/* ---- history: a flat log of single-field edits; edits made by one call share a group ---- */
#define MAX_HIST 2048
enum { FIELD_DIGIT, FIELD_NOTES, FIELD_BOMB };
static int16_t  hist_cell[MAX_HIST];
static uint8_t  hist_field[MAX_HIST];
static int16_t  hist_old[MAX_HIST], hist_new[MAX_HIST];
static int32_t  hist_group[MAX_HIST];
static int      hist_len, hist_pos;       /* entries recorded / entries currently applied */
static int32_t  next_group;

int eng_level_count(void) { return NLEVELS; }
const char *eng_level_name(int level) { return level >= 0 && level < NLEVELS ? lvl_name[level] : "?"; }
int eng_level(void) { return cur_level; }
uint64_t eng_seed(void) { return cur_seed; }
int eng_width(void) { return grid_w; }
int eng_height(void) { return grid_h; }
int eng_rule_count(void) { return n_rules; }
const char *eng_tech_name(int tech) { return func_tech_name(tech); }

static int func_cell_ok(int cell) { return cell >= 0 && cell < grid_w * grid_h; }

int eng_cell_is_given(int cell) { return func_cell_ok(cell) && cell_given[cell]; }
int eng_cell_digit(int cell) { return !func_cell_ok(cell) ? 0 : cell_given[cell] ? cell_sol[cell] : play_val[cell]; }
int eng_cell_hint(int cell) { return func_cell_ok(cell) ? cell_hint[cell] : 0; }
int eng_has_mines(void) { return sys_has_mines(); }

int eng_mines_total(void) {
    for (int r = 0; r < n_rules; r++) if (rule_kind[r] == RULE_MINE_TOTAL) return rule_param[r];
    return 0;
}
int eng_bombs_marked(void) {
    int m = 0;
    for (int c = 0; c < grid_w * grid_h; c++) m += play_bomb[c];
    return m;
}

static void sys_clear_play(void) {
    memset(play_val, 0, sizeof play_val);
    memset(play_notes, 0, sizeof play_notes);
    memset(play_bomb, 0, sizeof play_bomb);
    memset(play_wrong, 0, sizeof play_wrong);
    hist_len = hist_pos = 0;
    hint_tech = hint_rule = hint_cell = -1; hint_digit = hint_changes = 0;
}

int eng_new_game(int level, uint64_t seed) {
    if (level < 0 || level >= NLEVELS) return ENG_ERR_RANGE;
    if (!tables_ready) { sys_init_tables(); tables_ready = 1; }
    if (!sys_generate(level, seed)) return ENG_ERR_GENERATE;
    cur_level = level; cur_seed = seed;
    sys_clear_play();
    return ENG_OK;
}

/* ---- editing primitives ---- */
static int func_get(int cell, int field) {
    return field == FIELD_DIGIT ? play_val[cell] : field == FIELD_NOTES ? play_notes[cell] : play_bomb[cell];
}
static void map_put(int cell, int field, int v) {
    if (field == FIELD_DIGIT) play_val[cell] = v;
    else if (field == FIELD_NOTES) play_notes[cell] = (uint16_t)v;
    else play_bomb[cell] = (uint8_t)v;
}

static void sys_drop_oldest_group(void) {
    int g = hist_group[0], k = 0;
    while (k < hist_len && hist_group[k] == g) k++;
    memmove(hist_cell, hist_cell + k, (size_t)(hist_len - k) * sizeof *hist_cell);
    memmove(hist_field, hist_field + k, (size_t)(hist_len - k) * sizeof *hist_field);
    memmove(hist_old, hist_old + k, (size_t)(hist_len - k) * sizeof *hist_old);
    memmove(hist_new, hist_new + k, (size_t)(hist_len - k) * sizeof *hist_new);
    memmove(hist_group, hist_group + k, (size_t)(hist_len - k) * sizeof *hist_group);
    hist_len -= k; hist_pos -= k;
}

/* record + apply one field edit as part of group g */
static void sys_edit(int cell, int field, int v, int32_t g) {
    int old = func_get(cell, field);
    if (old == v) return;
    hist_len = hist_pos;                       /* a new edit discards the redo tail */
    if (hist_len >= MAX_HIST) sys_drop_oldest_group();
    hist_cell[hist_len] = (int16_t)cell; hist_field[hist_len] = (uint8_t)field;
    hist_old[hist_len] = (int16_t)old; hist_new[hist_len] = (int16_t)v; hist_group[hist_len] = g;
    hist_len++; hist_pos = hist_len;
    map_put(cell, field, v);
}

static int func_editable(int cell) {
    if (!func_cell_ok(cell)) return ENG_ERR_RANGE;
    if (cell_given[cell]) return ENG_ERR_GIVEN;
    return ENG_OK;
}

int eng_set_digit(int cell, int digit) {
    int e = func_editable(cell);
    if (e) return e;
    if (digit < 0 || digit > DIGITS) return ENG_ERR_RANGE;
    memset(play_wrong, 0, sizeof play_wrong);
    sys_edit(cell, FIELD_DIGIT, digit, ++next_group);
    return ENG_OK;
}

int eng_toggle_note(int cell, int digit) {
    int e = func_editable(cell);
    if (e) return e;
    if (digit < 1 || digit > DIGITS) return ENG_ERR_RANGE;
    memset(play_wrong, 0, sizeof play_wrong);
    sys_edit(cell, FIELD_NOTES, play_notes[cell] ^ (int)BIT(digit), ++next_group);
    return ENG_OK;
}

int eng_toggle_bomb(int cell) {
    if (!func_cell_ok(cell)) return ENG_ERR_RANGE;
    if (!sys_has_mines()) return ENG_ERR_NO_MINES;
    if (cell_hint[cell]) return ENG_ERR_HINT_SAFE;
    memset(play_wrong, 0, sizeof play_wrong);
    sys_edit(cell, FIELD_BOMB, !play_bomb[cell], ++next_group);   /* givens can be mines, so they may be marked */
    return ENG_OK;
}

int eng_clear_cell(int cell) {
    if (!func_cell_ok(cell)) return ENG_ERR_RANGE;
    int32_t g = ++next_group;
    memset(play_wrong, 0, sizeof play_wrong);
    if (!cell_given[cell]) { sys_edit(cell, FIELD_DIGIT, 0, g); sys_edit(cell, FIELD_NOTES, 0, g); }
    sys_edit(cell, FIELD_BOMB, 0, g);
    return ENG_OK;
}

int eng_undo(void) {
    if (hist_pos == 0) return ENG_ERR_NOTHING;
    int32_t g = hist_group[hist_pos - 1];
    while (hist_pos > 0 && hist_group[hist_pos - 1] == g) { hist_pos--; map_put(hist_cell[hist_pos], hist_field[hist_pos], hist_old[hist_pos]); }
    memset(play_wrong, 0, sizeof play_wrong);
    return ENG_OK;
}

int eng_redo(void) {
    if (hist_pos >= hist_len) return ENG_ERR_NOTHING;
    int32_t g = hist_group[hist_pos];
    while (hist_pos < hist_len && hist_group[hist_pos] == g) { map_put(hist_cell[hist_pos], hist_field[hist_pos], hist_new[hist_pos]); hist_pos++; }
    memset(play_wrong, 0, sizeof play_wrong);
    return ENG_OK;
}

/* ---- checking ---- */
int eng_check(void) {
    int wrong = 0;
    memset(play_wrong, 0, sizeof play_wrong);
    for (int c = 0; c < grid_w * grid_h; c++) {
        if (!cell_given[c] && play_val[c] && play_val[c] != cell_sol[c]) { play_wrong[c] |= WRONG_DIGIT; wrong++; }
        if (play_bomb[c] && !cell_mine[c]) { play_wrong[c] |= WRONG_BOMB; wrong++; }
    }
    return wrong;
}

int eng_is_solved(void) {
    int n = grid_w * grid_h;
    if (cur_level < 0) return 0;
    for (int c = 0; c < n; c++) if (eng_cell_digit(c) != cell_sol[c]) return 0;
    if (sys_has_mines()) {
        for (int c = 0; c < n; c++) if (cell_mine[c] && !play_bomb[c]) return 0;
        if (eng_bombs_marked() > eng_mines_total()) return 0;
    }
    return 1;
}

/* ---- hint: run the solver from the player's current digits and report its next technique ---- */
int eng_hint(void) {
    int n = grid_w * grid_h;
    hint_tech = hint_rule = hint_cell = -1; hint_digit = hint_changes = 0;
    if (eng_is_solved()) return ENG_ERR_SOLVED;
    for (int c = 0; c < n; c++) {                 /* seed the solver scratch state with givens + entries */
        int d = eng_cell_digit(c);
        cell_val[c] = (int8_t)d;
        cell_cand[c] = d ? (uint16_t)BIT(d) : (uint16_t)ALL_DIGITS;
        cell_mcand[c] = MC_UNKNOWN;
    }
    for (int r = 0; r < n_rules; r++) if (rule_kind[r] == RULE_MINE_HINT) cell_mcand[rule_line[r]] = MC_SAFE;
    n_steps = 0; contradiction = 0;
    int r = sys_step(TECH_COUNT - 1);
    if (r > 0) {
        hint_tech = step_tech[0]; hint_rule = step_rule[0]; hint_cell = step_cell[0];
        hint_digit = step_digit[0]; hint_changes = step_changes[0];
        return ENG_OK;
    }
    return contradiction ? ENG_ERR_INCONSISTENT : ENG_ERR_SOLVED;
}

/* ---- persistence: "SBU1", level, seed, then three bytes-per-cell blocks ---- */
int eng_save(uint8_t *buf, int cap) {
    int n = grid_w * grid_h, need = 4 + 1 + 8 + n * 4;
    if (cur_level < 0) return ENG_ERR_RANGE;
    if (cap < need) return ENG_ERR_RANGE;
    memcpy(buf, "SBU1", 4); buf[4] = (uint8_t)cur_level;
    for (int i = 0; i < 8; i++) buf[5 + i] = (uint8_t)(cur_seed >> (8 * i));
    uint8_t *p = buf + 13;
    for (int c = 0; c < n; c++) { *p++ = (uint8_t)play_val[c]; *p++ = (uint8_t)(play_notes[c] & 0xFF); *p++ = (uint8_t)(play_notes[c] >> 8); *p++ = play_bomb[c]; }
    return need;
}

int eng_load(const uint8_t *buf, int len) {
    if (len < 13 || memcmp(buf, "SBU1", 4)) return ENG_ERR_BAD_DATA;
    int level = buf[4]; uint64_t seed = 0;
    for (int i = 0; i < 8; i++) seed |= (uint64_t)buf[5 + i] << (8 * i);
    if (level >= NLEVELS) return ENG_ERR_BAD_DATA;
    int e = eng_new_game(level, seed);
    if (e) return e;
    int n = grid_w * grid_h;
    if (len < 13 + n * 4) return ENG_ERR_BAD_DATA;
    const uint8_t *p = buf + 13;
    for (int c = 0; c < n; c++) {
        int d = *p++; int notes = p[0] | (p[1] << 8); int bomb = p[2]; p += 3;
        if (d > DIGITS || (notes & ~(int)ALL_DIGITS) || bomb > 1) { sys_clear_play(); return ENG_ERR_BAD_DATA; }
        if (!cell_given[c]) { play_val[c] = d; play_notes[c] = (uint16_t)notes; }
        if (bomb && sys_has_mines() && !cell_hint[c]) play_bomb[c] = 1;
    }
    return ENG_OK;
}
