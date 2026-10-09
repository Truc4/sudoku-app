/*
 * Level table + generator. The curriculum is data: one row per level, plus a join table (level, rule)
 * for the line rules a level adds. Adding "sandwich on a different row" is one more join row.
 * Generator: random solution -> derive line-rule targets from it -> (mine levels) pick mines and derive hints
 * backwards -> remove givens while the technique-limited solver still finishes -> require the level's
 * teaching technique to be used.
 */
#include <string.h>
#include "sb.h"

const char *lvl_name[NLEVELS] = {
    "one missing digit",
    "3x3: sum on a row",
    "3x3: sums on two rows",
    "3x3: sums on rows and columns",
    "3x3: sum row + sandwich on another row",
    "3x3: sums and a sandwich",
    "6x6: no clues",
    "6x6: sum on a row",
    "6x6: sum row + sandwich on another row",
    "6x6: sums and sandwiches on rows and a column",
    "3x3 mines: a hint and the counter",
    "6x6 mines: hints and the counter",
    "6x6 mines: overlapping hints",
    "6x6 mines: with a sum row and a sandwich row",
};
static const int lvl_w[NLEVELS]         = { 3, 3, 3, 3, 3, 3, 6, 6, 6, 6, 3, 6, 6, 6 };
static const int lvl_h[NLEVELS]         = { 3, 3, 3, 3, 3, 3, 6, 6, 6, 6, 3, 6, 6, 6 };
static const int lvl_max_empty[NLEVELS] = { 1, 99, 99, 99, 99, 99, 10, 99, 99, 99, 1, 10, 10, 99 };
static const int lvl_ceiling[NLEVELS]   = { TECH_LAST_DIGIT, TECH_SUM_COMBO, TECH_SUM_COMBO, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH, TECH_HIDDEN_SINGLE, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH,
                                            TECH_MINE_TOTAL, TECH_MINE_TOTAL, TECH_MINE_SUBSET, TECH_MINE_SUBSET };
static const int lvl_must_use[NLEVELS]  = { -1, TECH_SUM_COMBO, TECH_SUM_COMBO, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH, -1, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH,
                                            TECH_MINE_FULL, TECH_MINE_SATISFIED, TECH_MINE_SUBSET, TECH_MINE_SATISFIED };
/* mine layer: how many hints to build the mine set around (0 = level has no mine layer) */
static const int lvl_mine_hints[NLEVELS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4, 6, 4 };

/* join table: line rules added by each level (level, kind, axis, line) */
#define S RULE_SUM
#define W RULE_SANDWICH
#define R AXIS_ROW
#define C AXIS_COL
#define N_LR 20
static const int lr_level[N_LR] = { 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 5, 7, 8, 8, 9, 9, 9, 13, 13 };
static const int lr_kind[N_LR]  = { S, S, S, S, S, S, S, S, W, S, S, W, S, S, W, S, W, S, S, W };
static const int lr_axis[N_LR]  = { R, R, R, R, R, C, C, R, R, R, C, R, R, R, R, R, R, C, R, R };
static const int lr_line[N_LR]  = { 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 2, 4, 0, 1 };
#undef S
#undef W
#undef R
#undef C

int func_level_ceiling(int level) { return lvl_ceiling[level]; }
int func_level_must_use(int level) { return lvl_must_use[level]; }
int func_level_has_mines(int level) { return lvl_mine_hints[level] > 0; }

static int func_trace_uses(int tech) {
    for (int i = 0; i < n_steps; i++) if (step_tech[i] == tech) return 1;
    return 0;
}

static int func_hint_rule_of(int cell) {
    for (int r = 0; r < n_rules; r++) if (rule_kind[r] == RULE_MINE_HINT && rule_line[r] == cell) return r;
    return -1;
}

static int func_mined_neighbours(int cell) {
    int m = 0;
    for (int k = 0; k < 8; k++) {
        int c = func_king_cell(cell, k, grid_w, grid_h);
        if (c >= 0 && cell_mine[c]) m++;
    }
    return m;
}

/* Work backwards from hints to mines. Visit cells in random order; a safe-able cell whose digit d (1..8) fits its
 * neighbourhood becomes a hint, and we place exactly d mines among its still-undecided neighbours (the rest of its
 * neighbours become safe). Stop after `lvl_mine_hints` hints. Everything left undecided is safe, so the mine count
 * is whatever the construction produced. Returns the number of mines (0 on failure). */
static int sys_build_mines(int level) {
    int n = grid_w * grid_h, order[MAX_CELLS], made = 0, mines = 0;
    uint8_t decided[MAX_CELLS];                         /* 0 free, 1 safe, 2 mine */
    memset(decided, 0, sizeof decided);
    for (int c = 0; c < n; c++) cell_mine[c] = 0;
    for (int c = 0; c < n; c++) order[c] = c;
    for (int i = n - 1; i > 0; i--) { int j = sys_rand_below(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < n && made < lvl_mine_hints[level]; i++) {
        int h = order[i], d = cell_sol[h], nb[8], cnt = 0, m = 0, f = 0;
        if (d > 8 || decided[h] == 2) continue;
        for (int k = 0; k < 8; k++) {
            int c = func_king_cell(h, k, grid_w, grid_h);
            if (c < 0) continue;
            nb[cnt++] = c;
            if (decided[c] == 2) m++; else if (!decided[c]) f++;
        }
        int need = d - m;
        if (need < 0 || need > f) continue;
        decided[h] = 1;
        for (int j = 0; j < cnt; j++) {                  /* choose `need` of the free neighbours as mines */
            int c = nb[j];
            if (decided[c]) continue;
            int left_free = 0;
            for (int q = j; q < cnt; q++) if (!decided[nb[q]]) left_free++;
            if (sys_rand_below(left_free) < need) { decided[c] = 2; need--; } else decided[c] = 1;
        }
        made++;
    }
    for (int c = 0; c < n; c++) { cell_mine[c] = decided[c] == 2; mines += cell_mine[c]; }
    return made == lvl_mine_hints[level] ? mines : 0;
}

/* Returns attempts used (>= 1) or 0 if no puzzle met the spec. Leaves the pools holding the puzzle
 * (givens, solution, rules) with state reset to the givens. */
int sys_generate(int level, uint64_t seed) {
    int order[MAX_CELLS];
    sys_seed(seed);
    sys_set_grid(lvl_w[level], lvl_h[level]);
    int n = grid_w * grid_h;

    for (int attempt = 1; attempt <= 6000; attempt++) {
        /* 1. a full random solution (digit rules only) */
        for (int c = 0; c < n; c++) { cell_val[c] = 0; cell_mine[c] = 0; cell_hint[c] = 0; }
        n_rules = 0;
        sys_build_alldiff();
        if (sys_backtrack(1, 0, 1) != 1) return 0;
        for (int c = 0; c < n; c++) { cell_sol[c] = cell_val[c]; cell_given[c] = 1; }

        /* 2. line rules, with targets read off the solution */
        int ok = 1;
        for (int i = 0; i < N_LR; i++) {
            if (lr_level[i] != level) continue;
            int target = lr_kind[i] == RULE_SUM ? func_line_sum(cell_sol, grid_w, grid_h, lr_axis[i], lr_line[i])
                                                : func_sandwich_value(cell_sol, grid_w, grid_h, lr_axis[i], lr_line[i]);
            if (target < 0) { ok = 0; break; }          /* sandwich needs both a 1 and a 9 on the line */
            sys_add_rule(lr_kind[i], lr_axis[i], lr_line[i], target);
        }
        if (!ok) continue;

        /* 2b. mine layer, derived backwards from a random mine set; then keep only the hints that are needed */
        if (lvl_mine_hints[level]) {
            if (!sys_build_mines(level)) continue;
            int total = 0, hr[MAX_CELLS], nh = 0;
            for (int c = 0; c < n; c++) {
                total += cell_mine[c];
                if (!cell_mine[c] && cell_sol[c] <= 8 && func_mined_neighbours(c) == cell_sol[c])
                    hr[nh++] = sys_add_rule(RULE_MINE_HINT, AXIS_CELL, c, SHAPE_KING);
            }
            sys_add_rule(RULE_MINE_TOTAL, AXIS_CELL, 0, total);
            for (int i = nh - 1; i > 0; i--) { int j = sys_rand_below(i + 1); int t = hr[i]; hr[i] = hr[j]; hr[j] = t; }
            int must = lvl_must_use[level], keep_must = must >= TECH_MINE_SATISFIED;
            sys_reset_state();
            if (!sys_solve(lvl_ceiling[level])) continue;   /* mine layer not solvable at this ceiling */
            if (keep_must && !func_trace_uses(must)) continue;
            for (int i = 0; i < nh; i++) {                  /* thin hints while the teaching technique survives */
                rule_kind[hr[i]] = RULE_OFF;
                sys_reset_state();
                if (!sys_solve(lvl_ceiling[level]) || (keep_must && !func_trace_uses(must))) rule_kind[hr[i]] = RULE_MINE_HINT;
            }
            sys_reset_state();
            if (!sys_solve(lvl_ceiling[level])) continue;
        }

        /* 3. remove givens in random order while the ceiling-limited solver still finishes.
         *    A hint's digit is its clue, so removing it switches the hint off with it. */
        for (int c = 0; c < n; c++) order[c] = c;
        for (int i = n - 1; i > 0; i--) { int j = sys_rand_below(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
        int empties = 0;
        for (int i = 0; i < n && empties < lvl_max_empty[level]; i++) {
            int c = order[i], hr = func_hint_rule_of(c);
            cell_given[c] = 0;
            if (hr >= 0) rule_kind[hr] = RULE_OFF;
            sys_reset_state();
            if (sys_solve(lvl_ceiling[level])) empties++;
            else { cell_given[c] = 1; if (hr >= 0) rule_kind[hr] = RULE_MINE_HINT; }
        }

        /* 4. the level must actually teach its technique */
        sys_reset_state();
        sys_solve(lvl_ceiling[level]);
        if (lvl_must_use[level] >= 0 && !func_trace_uses(lvl_must_use[level])) continue;
        for (int r = 0; r < n_rules; r++) if (rule_kind[r] == RULE_MINE_HINT) cell_hint[rule_line[r]] = (uint8_t)rule_param[r];
        sys_reset_state();
        return attempt;
    }
    return 0;
}
