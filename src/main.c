/*
 * Terminal front end + test harness. Everything under "play" goes through the engine API only; "gen" and
 * "selftest" also reach into solver internals because they test the engine itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"

static const char *axis_name[4] = { "row", "col", "box", "cell" };

/* ---- rendering (text only; a real UI would do its own) ---- */
static void sys_print_clues_for_row(int r) {
    for (int i = 0; i < eng_rule_count(); i++)
        if ((rule_kind[i] == RULE_SUM || rule_kind[i] == RULE_SANDWICH) && rule_axis[i] == AXIS_ROW && rule_line[i] == r)
            printf(" %s %d", rule_kind[i] == RULE_SUM ? "sum" : "sandwich", rule_param[i]);
}

/* cell = digit char + marker: '*' hint, '#' bomb mark (or 'M' real mine when showing the solution) */
static void sys_print_board(int solution) {
    int w = eng_width(), h = eng_height();
    for (int r = 0; r < h; r++) {
        if (r && r % 3 == 0) { for (int i = 0; i < w * 3 + (w / 3 - 1) * 2; i++) putchar('-'); putchar('\n'); }
        for (int c = 0; c < w; c++) {
            int id = r * w + c, d = solution ? cell_sol[id] : eng_cell_digit(id);
            if (c && c % 3 == 0) printf("| ");
            char mark = ' ';
            if (solution) mark = cell_mine[id] ? 'M' : ' ';
            else if (eng_cell_hint(id)) mark = '*';
            else if (play_bomb[id]) mark = '#';
            if (!solution && play_wrong[id]) mark = '!';
            printf("%c%c ", d ? '0' + d : '.', mark);
        }
        sys_print_clues_for_row(r);
        putchar('\n');
    }
    for (int i = 0; i < eng_rule_count(); i++)
        if ((rule_kind[i] == RULE_SUM || rule_kind[i] == RULE_SANDWICH) && rule_axis[i] == AXIS_COL)
            printf("col %d: %s %d\n", rule_line[i] + 1, rule_kind[i] == RULE_SUM ? "sum" : "sandwich", rule_param[i]);
    if (eng_has_mines()) printf("bombs: %d / %d\n", solution ? eng_mines_total() : eng_bombs_marked(), eng_mines_total());
}

static void sys_print_trace(void) {
    for (int i = 0; i < n_steps; i++) {
        printf("  %2d. %-14s", i + 1, eng_tech_name(step_tech[i]));
        if (step_rule[i] >= 0) printf(" %s %d", axis_name[rule_axis[step_rule[i]]], rule_line[step_rule[i]] + 1);
        if (step_cell[i] >= 0) printf("  -> r%dc%d = %d", step_cell[i] / grid_w + 1, step_cell[i] % grid_w + 1, step_digit[i]);
        else printf("  (%d candidates removed)", step_changes[i]);
        putchar('\n');
    }
}

static int sys_cmd_gen(int level, uint64_t seed) {
    int e = eng_new_game(level, seed);
    if (e) { fprintf(stderr, "level %d: generation failed (%d)\n", level, e); return 1; }
    int empties = 0;
    for (int c = 0; c < grid_w * grid_h; c++) empties += !cell_given[c];
    printf("level %d: %s  (seed %llu, %d empty)\n\n", level, eng_level_name(level), (unsigned long long)seed, empties);
    sys_print_board(0);
    sys_reset_state();
    int solved = sys_solve(func_level_ceiling(level));
    printf("\nsolver (%s, %d steps):\n", solved ? "solved" : "STUCK", n_steps);
    sys_print_trace();
    printf("\nsolution (M = mine):\n");
    sys_print_board(1);
    return 0;
}

/* ---- play: a line-based REPL over the public API ---- */
static void sys_cmd_play(int level, uint64_t seed) {
    char line[128];
    if (eng_new_game(level, seed)) { fprintf(stderr, "cannot start level %d\n", level); return; }
    printf("level %d: %s. commands: set r c d | clear r c | note r c d | bomb r c | undo | redo | check | hint | show | save | quit\n", level, eng_level_name(level));
    sys_print_board(0);
    while (fgets(line, sizeof line, stdin)) {
        char cmd[16] = ""; int a = 0, b = 0, d = 0, e = 0, n = sscanf(line, "%15s %d %d %d", cmd, &a, &b, &d);
        int cell = (a - 1) * eng_width() + (b - 1);
        if (!strcmp(cmd, "quit")) break;
        else if (!strcmp(cmd, "set") && n == 4) e = eng_set_digit(cell, d);
        else if (!strcmp(cmd, "clear") && n >= 3) e = eng_clear_cell(cell);
        else if (!strcmp(cmd, "note") && n == 4) e = eng_toggle_note(cell, d);
        else if (!strcmp(cmd, "bomb") && n >= 3) e = eng_toggle_bomb(cell);
        else if (!strcmp(cmd, "undo")) e = eng_undo();
        else if (!strcmp(cmd, "redo")) e = eng_redo();
        else if (!strcmp(cmd, "check")) { printf("%d wrong\n", eng_check()); }
        else if (!strcmp(cmd, "hint")) {
            e = eng_hint();
            if (!e) {
                printf("hint: %s", eng_tech_name(hint_tech));
                if (hint_rule >= 0) printf(" (%s %d)", axis_name[rule_axis[hint_rule]], rule_line[hint_rule] + 1);
                if (hint_cell >= 0) printf(" -> r%dc%d = %d", hint_cell / grid_w + 1, hint_cell % grid_w + 1, hint_digit);
                putchar('\n');
            }
        }
        else if (!strcmp(cmd, "save")) {
            uint8_t buf[512]; int len = eng_save(buf, sizeof buf);
            for (int i = 0; i < len; i++) printf("%02x", buf[i]);
            putchar('\n');
        }
        else if (strcmp(cmd, "show")) { printf("?\n"); continue; }
        if (e) printf("error %d\n", e);
        sys_print_board(0);
        if (eng_is_solved()) { printf("solved!\n"); break; }
    }
}

/* ---- selftest ---- */
#define EXPECT(cond, ...) do { if (!(cond)) { printf("FAIL level %d seed %d: ", level, s); printf(__VA_ARGS__); putchar('\n'); bad++; } } while (0)

static int sys_cmd_selftest(int seeds) {
    int bad = 0;
    for (int level = 0; level < eng_level_count(); level++) {
        int tech_hits[TECH_COUNT] = {0}, empties = 0, steps = 0, ran = 0, misses = 0;
        for (int s = 1; s <= seeds; s++) {
            if (eng_new_game(level, (uint64_t)s)) { misses++; continue; }   /* some seeds legitimately yield no puzzle */
            ran++;
            int n = grid_w * grid_h;
            for (int c = 0; c < n; c++) empties += !cell_given[c];

            /* the generated solution satisfies every rule */
            int mines = 0;
            for (int c = 0; c < n; c++) mines += cell_mine[c];
            for (int r = 0; r < n_rules; r++) {
                if (rule_kind[r] == RULE_SUM || rule_kind[r] == RULE_SANDWICH) {
                    int got = rule_kind[r] == RULE_SUM ? func_line_sum(cell_sol, grid_w, grid_h, rule_axis[r], rule_line[r])
                                                       : func_sandwich_value(cell_sol, grid_w, grid_h, rule_axis[r], rule_line[r]);
                    EXPECT(got == rule_param[r], "solution breaks rule %d", r);
                } else if (rule_kind[r] == RULE_MINE_HINT) {
                    int m = 0, h = rule_line[r];
                    for (int k = 0; k < 8; k++) { int c = func_king_cell(h, k, grid_w, grid_h); if (c >= 0) m += cell_mine[c]; }
                    EXPECT(m == cell_sol[h] && !cell_mine[h] && cell_given[h], "hint at %d: %d mines vs digit %d", h, m, cell_sol[h]);
                } else if (rule_kind[r] == RULE_MINE_TOTAL) EXPECT(rule_param[r] == mines, "mine total %d vs %d", rule_param[r], mines);
            }

            /* independent oracles: exactly one digit solution, exactly one mine assignment */
            sys_reset_state();
            int sols = sys_backtrack(0, 1, 2);
            EXPECT(sols == 1, "%d digit solutions", sols);
            if (eng_has_mines()) { int ms = sys_count_mine_solutions(2); EXPECT(ms == 1, "%d mine solutions", ms); }

            /* the human-style solver reproduces both layers using only the level's techniques */
            sys_reset_state();
            int solved = sys_solve(func_level_ceiling(level));
            EXPECT(solved, "human solver stuck");
            EXPECT(!memcmp(cell_val, cell_sol, (size_t)n), "digits differ from solution");
            if (eng_has_mines())
                for (int c = 0; c < n; c++) EXPECT(cell_mcand[c] == (cell_mine[c] ? MC_MINE : MC_SAFE), "cell %d mine state wrong", c);
            for (int i = 0; i < n_steps; i++) tech_hits[step_tech[i]]++;
            steps += n_steps;

            /* engine API behaviour: hint, wrong entry + check + undo/redo, solving through the API, save/load */
            eng_new_game(level, (uint64_t)s);
            EXPECT(eng_hint() == ENG_OK && hint_tech >= 0, "hint failed");
            int free_cell = -1;
            for (int c = 0; c < n && free_cell < 0; c++) if (!cell_given[c]) free_cell = c;
            if (free_cell >= 0) {
                EXPECT(eng_set_digit(free_cell, cell_sol[free_cell] % 9 + 1) == ENG_OK, "set digit");
                EXPECT(eng_check() == 1 && (play_wrong[free_cell] & WRONG_DIGIT), "check should see 1 wrong digit");
                EXPECT(eng_undo() == ENG_OK && play_val[free_cell] == 0 && !play_wrong[free_cell], "undo");
                EXPECT(eng_redo() == ENG_OK && play_val[free_cell] == cell_sol[free_cell] % 9 + 1, "redo");
                eng_undo();
            }
            int given_cell = -1;
            for (int c = 0; c < n && given_cell < 0; c++) if (cell_given[c]) given_cell = c;
            EXPECT(eng_set_digit(given_cell, 1) == ENG_ERR_GIVEN, "givens must be read-only");
            for (int c = 0; c < n; c++) {
                if (!cell_given[c]) eng_set_digit(c, cell_sol[c]);
                if (cell_mine[c]) EXPECT(eng_toggle_bomb(c) == ENG_OK, "bomb mark");
            }
            if (eng_has_mines()) {
                int hint_c = -1;
                for (int c = 0; c < n; c++) if (cell_hint[c]) hint_c = c;
                if (hint_c >= 0) EXPECT(eng_toggle_bomb(hint_c) == ENG_ERR_HINT_SAFE, "hint cells are safe");
            } else EXPECT(eng_toggle_bomb(0) == ENG_ERR_NO_MINES, "no mine layer");
            EXPECT(eng_check() == 0, "perfect board should check clean");
            EXPECT(eng_is_solved(), "perfect board should be solved");
            uint8_t buf[512]; int len = eng_save(buf, sizeof buf);
            int snap[MAX_CELLS]; memcpy(snap, play_val, sizeof snap);
            eng_new_game(level == 0 ? 1 : 0, 99);
            EXPECT(eng_load(buf, len) == ENG_OK && !memcmp(snap, play_val, sizeof snap) && eng_is_solved(), "save/load round trip");
            if (eng_has_mines()) {
                int extra = -1;
                for (int c = 0; c < n && extra < 0; c++) if (!cell_mine[c] && !cell_hint[c]) extra = c;
                if (extra >= 0) { eng_toggle_bomb(extra); EXPECT(!eng_is_solved(), "an extra bomb exceeds the counter"); }
            }
        }
        printf("level %2d %-46s empty %5.1f steps %5.1f |", level, eng_level_name(level), (double)empties / (ran ? ran : 1), (double)steps / (ran ? ran : 1));
        for (int t = 0; t < TECH_COUNT; t++) if (tech_hits[t]) printf(" %s:%d", eng_tech_name(t), tech_hits[t]);
        if (misses) printf(" (%d seed%s gave no puzzle)", misses, misses == 1 ? "" : "s");
        if (misses * 10 > seeds) { printf("\nFAIL level %d: too many seeds without a puzzle (%d/%d)", level, misses, seeds); bad++; }
        putchar('\n');
    }
    printf(bad ? "\n%d failure(s)\n" : "\nall ok\n", bad);
    return bad != 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "levels")) {
        for (int i = 0; i < eng_level_count(); i++) printf("%2d  %s\n", i, eng_level_name(i));
        return 0;
    }
    if (argc >= 3 && (!strcmp(argv[1], "gen") || !strcmp(argv[1], "play"))) {
        int level = atoi(argv[2]);
        if (level < 0 || level >= eng_level_count()) { fprintf(stderr, "level 0..%d\n", eng_level_count() - 1); return 2; }
        uint64_t seed = argc >= 4 ? strtoull(argv[3], 0, 10) : 1;
        if (!strcmp(argv[1], "play")) { sys_cmd_play(level, seed); return 0; }
        return sys_cmd_gen(level, seed);
    }
    if (argc >= 2 && !strcmp(argv[1], "selftest")) return sys_cmd_selftest(argc >= 3 ? atoi(argv[2]) : 20);
    fprintf(stderr, "usage: sudoku levels | gen <level> [seed] | play <level> [seed] | selftest [seeds]\n");
    return 2;
}
