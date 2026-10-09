/*
 * Level table + generator. The curriculum is data: one row per level, plus a join table (level, rule)
 * for the line rules a level adds. Adding "sandwich on a different row" is one more join row.
 * Generator: random solution -> derive line-rule targets from it -> remove givens while the
 * technique-limited solver still finishes -> require the level's teaching technique to be used.
 */
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
};
static const int lvl_w[NLEVELS]         = { 3, 3, 3, 3, 3, 3, 6, 6, 6, 6 };
static const int lvl_h[NLEVELS]         = { 3, 3, 3, 3, 3, 3, 6, 6, 6, 6 };
static const int lvl_max_empty[NLEVELS] = { 1, 99, 99, 99, 99, 99, 10, 99, 99, 99 };
static const int lvl_ceiling[NLEVELS]   = { TECH_LAST_DIGIT, TECH_SUM_COMBO, TECH_SUM_COMBO, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH, TECH_HIDDEN_SINGLE, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH };
static const int lvl_must_use[NLEVELS]  = { -1, TECH_SUM_COMBO, TECH_SUM_COMBO, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH, -1, TECH_SUM_COMBO,
                                            TECH_SANDWICH, TECH_SANDWICH };

/* join table: line rules added by each level (level, kind, axis, line) */
#define S RULE_SUM
#define W RULE_SANDWICH
#define R AXIS_ROW
#define C AXIS_COL
#define N_LR 18
static const int lr_level[N_LR] = { 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 5, 7, 8, 8, 9, 9, 9 };
static const int lr_kind[N_LR]  = { S, S, S, S, S, S, S, S, W, S, S, W, S, S, W, S, W, S };
static const int lr_axis[N_LR]  = { R, R, R, R, R, C, C, R, R, R, C, R, R, R, R, R, R, C };
static const int lr_line[N_LR]  = { 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 2, 4 };
#undef S
#undef W
#undef R
#undef C

int func_level_ceiling(int level) { return lvl_ceiling[level]; }
int func_level_must_use(int level) { return lvl_must_use[level]; }

static int func_trace_uses(int tech) {
    for (int i = 0; i < n_steps; i++) if (step_tech[i] == tech) return 1;
    return 0;
}

/* Returns attempts used (>= 1) or 0 if no puzzle met the spec. Leaves the pools holding the puzzle
 * (givens, solution, rules) with state reset to the givens. */
int sys_generate(int level, uint64_t seed) {
    int order[MAX_CELLS];
    sys_seed(seed);
    sys_set_grid(lvl_w[level], lvl_h[level]);
    int n = grid_w * grid_h;

    for (int attempt = 1; attempt <= 400; attempt++) {
        /* 1. a full random solution (digit rules only) */
        for (int c = 0; c < n; c++) cell_val[c] = 0;
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

        /* 3. remove givens in random order while the ceiling-limited solver still finishes */
        for (int c = 0; c < n; c++) order[c] = c;
        for (int i = n - 1; i > 0; i--) { int j = sys_rand_below(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
        int empties = 0;
        for (int i = 0; i < n && empties < lvl_max_empty[level]; i++) {
            cell_given[order[i]] = 0;
            sys_reset_state();
            if (sys_solve(lvl_ceiling[level])) empties++; else cell_given[order[i]] = 1;
        }

        /* 4. the level must actually teach its technique */
        sys_reset_state();
        sys_solve(lvl_ceiling[level]);
        if (lvl_must_use[level] >= 0 && !func_trace_uses(lvl_must_use[level])) continue;
        sys_reset_state();
        return attempt;
    }
    return 0;
}
