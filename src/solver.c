/*
 * Human-style solver. Cheap candidate elimination (sys_propagate, the "auto pencil marks") is free and
 * not logged. Every *technique* firing is one logged step. Techniques are tried in rank order and the
 * first that changes anything wins, so the trace is the easiest path a human could take. There is no
 * guessing and no backtracking here; a puzzle the solver finishes is therefore also unique, because every
 * technique only removes candidates that cannot be part of any solution.
 */
#include "sb.h"

static void sys_log(int tech, int rule, int cell, int digit, int changes) {
    if (n_steps >= MAX_STEPS) return;
    step_tech[n_steps] = (uint8_t)tech;
    step_rule[n_steps] = (int16_t)rule;
    step_cell[n_steps] = (int16_t)cell;
    step_digit[n_steps] = (int8_t)digit;
    step_changes[n_steps] = (int16_t)changes;
    n_steps++;
}

static void map_place(int c, int d) { cell_val[c] = (int8_t)d; cell_cand[c] = (uint16_t)BIT(d); }

/* ---- free propagation: placed digits leave the rest of every all-different unit ---- */
static int map_alldiff_eliminate(int r) {
    int axis = rule_axis[r], line = rule_line[r], n = func_unit_len(axis, grid_w, grid_h), changed = 0;
    unsigned placed = 0;
    for (int k = 0; k < n; k++) {
        int d = cell_val[func_unit_cell(axis, line, k, grid_w, grid_h)];
        if (!d) continue;
        if (placed & BIT(d)) contradiction = 1;
        placed |= BIT(d);
    }
    for (int k = 0; k < n; k++) {
        int c = func_unit_cell(axis, line, k, grid_w, grid_h);
        if (cell_val[c] || !(cell_cand[c] & placed)) continue;
        cell_cand[c] &= (uint16_t)~placed;
        changed = 1;
        if (!cell_cand[c]) contradiction = 1;
    }
    return changed;
}

static void sys_propagate(void) {
    int changed;
    do {
        changed = 0;
        for (int r = 0; r < n_rules; r++)
            if (rule_kind[r] == RULE_ALLDIFF) changed |= map_alldiff_eliminate(r);
    } while (changed && !contradiction);
}

/* ---- techniques ---- */

/* rank 0: a complete (9-cell) unit with exactly one empty cell */
static int tech_last_digit(int r) {
    int axis = rule_axis[r], line = rule_line[r], n = func_unit_len(axis, grid_w, grid_h);
    if (rule_kind[r] != RULE_ALLDIFF || n != DIGITS) return 0;
    unsigned placed = 0; int empty = -1, empties = 0;
    for (int k = 0; k < n; k++) {
        int c = func_unit_cell(axis, line, k, grid_w, grid_h);
        if (cell_val[c]) placed |= BIT(cell_val[c]); else { empty = c; empties++; }
    }
    if (empties != 1) return 0;
    unsigned missing = ALL_DIGITS & ~placed;
    if (tab_pc[missing] != 1) { contradiction = 1; return 0; }
    int d = 0; while (!(missing >> d & 1)) d++;
    map_place(empty, d);
    sys_log(TECH_LAST_DIGIT, r, empty, d, 0);
    return 1;
}

/* rank 1: a cell with one candidate left */
static int tech_naked_single(void) {
    for (int c = 0; c < grid_w * grid_h; c++) {
        if (cell_val[c] || tab_pc[cell_cand[c]] != 1) continue;
        int d = 0; while (!(cell_cand[c] >> d & 1)) d++;
        map_place(c, d);
        sys_log(TECH_NAKED_SINGLE, -1, c, d, 0);
        return 1;
    }
    return 0;
}

/* rank 2: in a 9-cell unit every digit must appear, so a digit with one possible home goes there */
static int tech_hidden_single(int r) {
    int axis = rule_axis[r], line = rule_line[r], n = func_unit_len(axis, grid_w, grid_h);
    if (rule_kind[r] != RULE_ALLDIFF || n != DIGITS) return 0;
    unsigned placed = 0;
    for (int k = 0; k < n; k++) {
        int c = func_unit_cell(axis, line, k, grid_w, grid_h);
        if (cell_val[c]) placed |= BIT(cell_val[c]);
    }
    for (int d = 1; d <= DIGITS; d++) {
        if (placed & BIT(d)) continue;
        int homes = 0, home = -1;
        for (int k = 0; k < n; k++) {
            int c = func_unit_cell(axis, line, k, grid_w, grid_h);
            if (!cell_val[c] && (cell_cand[c] & BIT(d))) { homes++; home = c; }
        }
        if (homes == 0) { contradiction = 1; return 0; }
        if (homes == 1) {
            map_place(home, d);
            sys_log(TECH_HIDDEN_SINGLE, r, home, d, 0);
            return 1;
        }
    }
    return 0;
}

/* rank 3: line sum. Remaining empties must be distinct digits summing to the remainder; only digits that
 * appear in some such set (and in the empties' candidates) survive. (Needs an all-different line.) */
static int tech_sum_combo(int r) {
    int axis = rule_axis[r], line = rule_line[r], n = func_unit_len(axis, grid_w, grid_h);
    if (rule_kind[r] != RULE_SUM) return 0;
    int filled_sum = 0, k_empty = 0; unsigned placed = 0, allowed = 0;
    for (int k = 0; k < n; k++) {
        int c = func_unit_cell(axis, line, k, grid_w, grid_h);
        if (cell_val[c]) { filled_sum += cell_val[c]; placed |= BIT(cell_val[c]); }
        else { k_empty++; allowed |= cell_cand[c]; }
    }
    if (!k_empty) return 0;
    int u = func_combo_union(k_empty, rule_param[r] - filled_sum, allowed & ~placed);
    if (u < 0) { contradiction = 1; return 0; }
    int changes = 0;
    for (int k = 0; k < n; k++) {
        int c = func_unit_cell(axis, line, k, grid_w, grid_h);
        if (cell_val[c] || !(cell_cand[c] & ~(unsigned)u)) continue;
        cell_cand[c] &= (uint16_t)u;
        changes++;
        if (!cell_cand[c]) contradiction = 1;
    }
    if (!changes) return 0;
    sys_log(TECH_SUM_COMBO, r, -1, 0, changes);
    return 1;
}

/* rank 4: sandwich. Consider every placement (i,j) of the 1 and the 9 on the line; keep the ones where the
 * cells between them can still sum to the clue. Each cell may then only hold digits some surviving
 * placement allows for it. Local to one line, no assumption chains. */
static int tech_sandwich(int r) {
    int axis = rule_axis[r], line = rule_line[r], n = func_unit_len(axis, grid_w, grid_h);
    if (rule_kind[r] != RULE_SANDWICH) return 0;
    const unsigned B1 = BIT(1), B9 = BIT(9), ends = B1 | B9;
    unsigned cd[MAX_W > MAX_H ? MAX_W : MAX_H], allowed[MAX_W > MAX_H ? MAX_W : MAX_H];
    int cells[MAX_W > MAX_H ? MAX_W : MAX_H], feasible = 0;
    for (int k = 0; k < n; k++) { cells[k] = func_unit_cell(axis, line, k, grid_w, grid_h); cd[k] = cell_cand[cells[k]]; allowed[k] = 0; }
    for (int i = 0; i < n; i++) {
        if (!(cd[i] & B1)) continue;
        for (int j = 0; j < n; j++) {
            if (j == i || !(cd[j] & B9)) continue;
            int lo = i < j ? i : j, hi = i < j ? j : i; unsigned U = 0;
            for (int x = lo + 1; x < hi; x++) U |= cd[x];
            int u = func_combo_union(hi - lo - 1, rule_param[r], U & ~ends);
            if (u < 0) continue;
            feasible = 1;
            for (int x = 0; x < n; x++) {
                if (x == i) allowed[x] |= B1;
                else if (x == j) allowed[x] |= B9;
                else if (x > lo && x < hi) allowed[x] |= cd[x] & (unsigned)u;
                else allowed[x] |= cd[x] & ~ends;
            }
        }
    }
    if (!feasible) { contradiction = 1; return 0; }
    int changes = 0;
    for (int k = 0; k < n; k++) {
        unsigned nc = cd[k] & allowed[k];
        if (nc == cd[k] || cell_val[cells[k]]) continue;
        cell_cand[cells[k]] = (uint16_t)nc;
        changes++;
        if (!nc) contradiction = 1;
    }
    if (!changes) return 0;
    sys_log(TECH_SANDWICH, r, -1, 0, changes);
    return 1;
}

static int func_all_filled(void) {
    for (int c = 0; c < grid_w * grid_h; c++) if (!cell_val[c]) return 0;
    return 1;
}

/* Solve with techniques of rank <= ceiling. Assumes sys_reset_state() has run. */
int sys_solve(int ceiling) {
    sys_propagate();
    while (!contradiction) {
        if (func_all_filled()) return 1;
        int fired = 0;
        for (int t = 0; t <= ceiling && !fired; t++) {
            if (t == TECH_NAKED_SINGLE) { fired = tech_naked_single(); continue; }
            for (int r = 0; r < n_rules && !fired; r++) {
                switch (t) {
                case TECH_LAST_DIGIT:    fired = tech_last_digit(r); break;
                case TECH_HIDDEN_SINGLE: fired = tech_hidden_single(r); break;
                case TECH_SUM_COMBO:     fired = tech_sum_combo(r); break;
                case TECH_SANDWICH:      fired = tech_sandwich(r); break;
                }
            }
        }
        if (!fired) return 0;
        sys_propagate();
    }
    return 0;
}
