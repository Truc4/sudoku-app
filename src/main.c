#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sb.h"

static const char *tech_name[TECH_COUNT] = { "last digit", "naked single", "hidden single", "sum combo", "sandwich" };
static const char *axis_name[3] = { "row", "col", "box" };

static int func_digit_char(int d) { return d ? '0' + d : '.'; }

static void sys_print_board(const int8_t *v) {
    for (int r = 0; r < grid_h; r++) {
        if (r && r % 3 == 0) {
            for (int i = 0; i < grid_w * 2 + (grid_w / 3 - 1) * 2; i++) putchar('-');
            putchar('\n');
        }
        for (int c = 0; c < grid_w; c++) {
            if (c && c % 3 == 0) printf("| ");
            printf("%c ", func_digit_char(v[r * grid_w + c]));
        }
        for (int i = 0; i < n_rules; i++)
            if (rule_axis[i] == AXIS_ROW && rule_line[i] == r && rule_kind[i] != RULE_ALLDIFF)
                printf(" %s %d", rule_kind[i] == RULE_SUM ? "sum" : "sandwich", rule_param[i]);
        putchar('\n');
    }
    for (int i = 0; i < n_rules; i++)
        if (rule_axis[i] == AXIS_COL && rule_kind[i] != RULE_ALLDIFF)
            printf("col %d: %s %d\n", rule_line[i] + 1, rule_kind[i] == RULE_SUM ? "sum" : "sandwich", rule_param[i]);
}

static void sys_print_trace(void) {
    for (int i = 0; i < n_steps; i++) {
        printf("  %2d. %-13s", i + 1, tech_name[step_tech[i]]);
        if (step_rule[i] >= 0) printf(" %s %d", axis_name[rule_axis[step_rule[i]]], rule_line[step_rule[i]] + 1);
        if (step_cell[i] >= 0) printf("  -> r%dc%d = %d", step_cell[i] / grid_w + 1, step_cell[i] % grid_w + 1, step_digit[i]);
        else printf("  (%d candidates removed)", step_changes[i]);
        putchar('\n');
    }
}

static int sys_cmd_gen(int level, uint64_t seed) {
    int attempts = sys_generate(level, seed);
    if (!attempts) { fprintf(stderr, "level %d: generation failed\n", level); return 1; }
    int empties = 0;
    for (int c = 0; c < grid_w * grid_h; c++) empties += !cell_given[c];
    printf("level %d: %s  (seed %llu, %d attempt%s, %d empty)\n\n", level, lvl_name[level],
           (unsigned long long)seed, attempts, attempts == 1 ? "" : "s", empties);
    sys_print_board(cell_val);
    sys_reset_state();
    int solved = sys_solve(func_level_ceiling(level));
    printf("\nsolver (%s, %d steps):\n", solved ? "solved" : "STUCK", n_steps);
    sys_print_trace();
    printf("\nsolution:\n");
    sys_print_board(cell_sol);
    return 0;
}

/* Independent check of the whole pipeline: solution legal, puzzle has exactly one solution (brute force,
 * not the human solver), and the human solver reproduces it using only the level's techniques. */
static int sys_cmd_selftest(int seeds) {
    int bad = 0;
    for (int level = 0; level < NLEVELS; level++) {
        int tech_hits[TECH_COUNT] = {0}, empties = 0, steps = 0;
        for (int s = 1; s <= seeds; s++) {
            if (!sys_generate(level, (uint64_t)s)) { printf("FAIL level %d seed %d: no puzzle\n", level, s); bad++; continue; }
            int n = grid_w * grid_h;
            for (int c = 0; c < n; c++) empties += !cell_given[c];
            for (int r = 0; r < n_rules; r++) {
                if (rule_kind[r] == RULE_ALLDIFF) continue;
                int got = rule_kind[r] == RULE_SUM ? func_line_sum(cell_sol, grid_w, grid_h, rule_axis[r], rule_line[r])
                                                   : func_sandwich_value(cell_sol, grid_w, grid_h, rule_axis[r], rule_line[r]);
                if (got != rule_param[r]) { printf("FAIL level %d seed %d: solution breaks rule %d\n", level, s, r); bad++; }
            }
            sys_reset_state();
            int sols = sys_backtrack(0, 1, 2);
            if (sols != 1) { printf("FAIL level %d seed %d: %d solutions\n", level, s, sols); bad++; }
            sys_reset_state();
            int solved = sys_solve(func_level_ceiling(level));
            if (!solved || memcmp(cell_val, cell_sol, (size_t)n)) { printf("FAIL level %d seed %d: human solver did not reproduce solution\n", level, s); bad++; }
            for (int i = 0; i < n_steps; i++) tech_hits[step_tech[i]]++;
            steps += n_steps;
        }
        printf("level %d %-34s avg empty %5.1f, avg steps %5.1f  |", level, lvl_name[level], (double)empties / seeds, (double)steps / seeds);
        for (int t = 0; t < TECH_COUNT; t++) if (tech_hits[t]) printf(" %s:%d", tech_name[t], tech_hits[t]);
        putchar('\n');
    }
    printf(bad ? "\n%d failure(s)\n" : "\nall ok\n", bad);
    return bad != 0;
}

int main(int argc, char **argv) {
    sys_init_tables();
    if (argc >= 2 && !strcmp(argv[1], "levels")) {
        for (int i = 0; i < NLEVELS; i++) printf("%d  %s\n", i, lvl_name[i]);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "gen")) {
        int level = atoi(argv[2]);
        if (level < 0 || level >= NLEVELS) { fprintf(stderr, "level 0..%d\n", NLEVELS - 1); return 2; }
        return sys_cmd_gen(level, argc >= 4 ? strtoull(argv[3], 0, 10) : 1);
    }
    if (argc >= 2 && !strcmp(argv[1], "selftest")) return sys_cmd_selftest(argc >= 3 ? atoi(argv[2]) : 20);
    fprintf(stderr, "usage: sudoku levels | gen <level> [seed] | selftest [seeds]\n");
    return 2;
}
