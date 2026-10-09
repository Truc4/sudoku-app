#include <string.h>
#include "sb.h"

int      grid_w, grid_h;
int8_t   cell_val[MAX_CELLS];
uint16_t cell_cand[MAX_CELLS];
uint8_t  cell_given[MAX_CELLS];
int8_t   cell_sol[MAX_CELLS];

int      n_rules;
uint8_t  rule_kind[MAX_RULES];
uint8_t  rule_axis[MAX_RULES];
int8_t   rule_line[MAX_RULES];
int16_t  rule_param[MAX_RULES];

int      n_steps;
uint8_t  step_tech[MAX_STEPS];
int16_t  step_rule[MAX_STEPS];
int16_t  step_cell[MAX_STEPS];
int8_t   step_digit[MAX_STEPS];
int16_t  step_changes[MAX_STEPS];
int      contradiction;

uint8_t  tab_pc[1024], tab_sum[1024];

/* constant lookup tables: popcount and digit-sum of every candidate mask */
void sys_init_tables(void) {
    for (int m = 0; m < 1024; m++) {
        int pc = 0, sum = 0;
        for (int d = 1; d <= DIGITS; d++)
            if (m >> d & 1) { pc++; sum += d; }
        tab_pc[m] = (uint8_t)pc;
        tab_sum[m] = (uint8_t)sum;
    }
}

/* ---- geometry (pure) ---- */
int func_unit_len(int axis, int w, int h) { return axis == AXIS_ROW ? w : axis == AXIS_COL ? h : 9; }
int func_unit_count(int axis, int w, int h) { return axis == AXIS_ROW ? h : axis == AXIS_COL ? w : (w / 3) * (h / 3); }
int func_box_of(int r, int c, int w) { return (r / 3) * (w / 3) + c / 3; }

int func_unit_cell(int axis, int line, int k, int w, int h) {
    (void)h;
    if (axis == AXIS_ROW) return line * w + k;
    if (axis == AXIS_COL) return k * w + line;
    int bx = line % (w / 3), by = line / (w / 3);
    return (by * 3 + k / 3) * w + bx * 3 + k % 3;
}

/* can digit d go in cell c without repeating in its row, column or box? */
int func_digit_ok(const int8_t *v, int w, int h, int c, int d) {
    int r = c / w, col = c % w, b = func_box_of(r, col, w);
    for (int i = 0; i < w * h; i++) {
        if (i == c || v[i] != d) continue;
        int ir = i / w, ic = i % w;
        if (ir == r || ic == col || func_box_of(ir, ic, w) == b) return 0;
    }
    return 1;
}

int func_line_complete(const int8_t *v, int w, int h, int axis, int line) {
    int n = func_unit_len(axis, w, h);
    for (int k = 0; k < n; k++)
        if (!v[func_unit_cell(axis, line, k, w, h)]) return 0;
    return 1;
}

int func_line_sum(const int8_t *v, int w, int h, int axis, int line) {
    int n = func_unit_len(axis, w, h), s = 0;
    for (int k = 0; k < n; k++) s += v[func_unit_cell(axis, line, k, w, h)];
    return s;
}

/* sum of the digits strictly between the 1 and the 9 on a line; -1 if the line lacks either */
int func_sandwich_value(const int8_t *v, int w, int h, int axis, int line) {
    int n = func_unit_len(axis, w, h), p1 = -1, p9 = -1;
    for (int k = 0; k < n; k++) {
        int d = v[func_unit_cell(axis, line, k, w, h)];
        if (d == 1) p1 = k;
        if (d == 9) p9 = k;
    }
    if (p1 < 0 || p9 < 0) return -1;
    int lo = p1 < p9 ? p1 : p9, hi = p1 < p9 ? p9 : p1, s = 0;
    for (int k = lo + 1; k < hi; k++) s += v[func_unit_cell(axis, line, k, w, h)];
    return s;
}

/* The Kakuro combo table, computed on demand: which digits can appear in some set of k distinct digits,
 * drawn from `allowed`, summing to R?  Returns -1 if no such set exists (k == 0 needs R == 0). */
int func_combo_union(int k, int R, unsigned allowed) {
    if (k == 0) return R == 0 ? 0 : -1;
    int u = 0, found = 0;
    for (unsigned m = allowed; m; m = (m - 1) & allowed)
        if (tab_pc[m] == k && tab_sum[m] == R) { u |= (int)m; found = 1; }
    return found ? u : -1;
}

/* ---- rule pool ---- */
int sys_add_rule(int kind, int axis, int line, int param) {
    if (n_rules >= MAX_RULES) return -1;
    rule_kind[n_rules] = (uint8_t)kind;
    rule_axis[n_rules] = (uint8_t)axis;
    rule_line[n_rules] = (int8_t)line;
    rule_param[n_rules] = (int16_t)param;
    return n_rules++;
}

void sys_build_alldiff(void) {
    for (int axis = AXIS_ROW; axis <= AXIS_BOX; axis++) {
        int n = func_unit_count(axis, grid_w, grid_h);
        for (int line = 0; line < n; line++) sys_add_rule(RULE_ALLDIFF, axis, line, 0);
    }
}

int sys_set_grid(int w, int h) {
    if (w < 3 || h < 3 || w > MAX_W || h > MAX_H || w % 3 || h % 3) return -1;
    grid_w = w; grid_h = h;
    return 0;
}

void sys_reset_state(void) {
    for (int c = 0; c < grid_w * grid_h; c++) {
        cell_val[c] = cell_given[c] ? cell_sol[c] : 0;
        cell_cand[c] = cell_given[c] ? (uint16_t)BIT(cell_sol[c]) : (uint16_t)ALL_DIGITS;
    }
    n_steps = 0;
    contradiction = 0;
}

/* ---- rng (xorshift64*) ---- */
static uint64_t rng_state = 88172645463325252ull;
void sys_seed(uint64_t s) { rng_state = s * 0x9E3779B97F4A7C15ull + 1442695040888963407ull; if (!rng_state) rng_state = 1; }
int sys_rand_below(int n) {
    rng_state ^= rng_state >> 12; rng_state ^= rng_state << 25; rng_state ^= rng_state >> 27;
    return (int)(((rng_state * 2685821657736338717ull) >> 33) % (uint64_t)n);
}

/* ---- line rules on completed lines ---- */
int sys_lines_hold_at(int cell) {
    int r = cell / grid_w, col = cell % grid_w;
    for (int i = 0; i < n_rules; i++) {
        if (rule_kind[i] == RULE_ALLDIFF) continue;
        int line = rule_line[i], axis = rule_axis[i];
        if ((axis == AXIS_ROW ? r : col) != line) continue;
        if (!func_line_complete(cell_val, grid_w, grid_h, axis, line)) continue;
        int got = rule_kind[i] == RULE_SUM ? func_line_sum(cell_val, grid_w, grid_h, axis, line)
                                           : func_sandwich_value(cell_val, grid_w, grid_h, axis, line);
        if (got != rule_param[i]) return 0;
    }
    return 1;
}

/* ---- backtracking (explicit stack, no recursion; generation and test-oracle only) ----
 * Fills the empty cells of cell_val. random_order: shuffled digit order (solution generation).
 * check_lines: enforce SUM/SANDWICH when a line completes. Stops after `limit` solutions; on an early
 * stop cell_val holds the last solution found, otherwise it is restored to its entry state. */
static int8_t bt_cell[MAX_CELLS];
static int8_t bt_ord[MAX_CELLS][DIGITS];
static int8_t bt_pos[MAX_CELLS];

static void sys_bt_enter(int d, int random_order) {
    bt_pos[d] = 0;
    for (int i = 0; i < DIGITS; i++) bt_ord[d][i] = (int8_t)(i + 1);
    if (random_order)
        for (int i = DIGITS - 1; i > 0; i--) {
            int j = sys_rand_below(i + 1);
            int8_t t = bt_ord[d][i]; bt_ord[d][i] = bt_ord[d][j]; bt_ord[d][j] = t;
        }
}

int sys_backtrack(int random_order, int check_lines, int limit) {
    int n = grid_w * grid_h, nf = 0, found = 0, d = 0;
    for (int i = 0; i < n; i++) if (!cell_val[i]) bt_cell[nf++] = (int8_t)i;
    if (nf == 0) return 1;
    sys_bt_enter(0, random_order);
    while (d >= 0) {
        int c = bt_cell[d], p;
        cell_val[c] = 0;
        for (p = bt_pos[d]; p < DIGITS; p++) {
            int dg = bt_ord[d][p];
            if (!func_digit_ok(cell_val, grid_w, grid_h, c, dg)) continue;
            cell_val[c] = (int8_t)dg;
            if (check_lines && !sys_lines_hold_at(c)) { cell_val[c] = 0; continue; }
            break;
        }
        if (p >= DIGITS) { d--; continue; }
        bt_pos[d] = (int8_t)(p + 1);
        if (d + 1 == nf) {
            if (++found >= limit) return found;
            continue;
        }
        d++;
        sys_bt_enter(d, random_order);
    }
    return found;
}
