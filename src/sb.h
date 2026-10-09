/*
 * sudoku-but prototype. Written in the *shape* of Arche so it ports 1:1; not idiomatic C.
 *
 *   - Storage is static columns (one array per field) in fixed pools. No malloc, no structs-per-element,
 *     no pointers to rows, no callbacks, no vtables. A row is an integer id.
 *   - Four kinds of function, mirroring Arche's split:
 *       func_*  pure: values (or read-only column slices) in, value out, touches no globals
 *               (except the constant lookup tables, written once by sys_init_tables)
 *       map_*   per-row transform over one pool, writes only the columns it names
 *       sys_*   whole-pool work; the only things allowed to touch several pools / do I/O-ish setup
 *       tech_*  a solver technique: one deduction step, returns whether it changed anything
 *   - A "rule" is a row in the rule pool: (kind, axis, line, param). Every rule so far applies to a single
 *     unit: one row, one column, or one 3x3 box. Cells of a unit are *computed* (func_unit_cell), so no
 *     membership join table yet. Thermo/killer/chess/minesweeper will need one (see docs/DESIGN.md).
 */
#ifndef SB_H
#define SB_H
#include <stdint.h>

#define MAX_W      9
#define MAX_H      9
#define MAX_CELLS  (MAX_W * MAX_H)
#define MAX_RULES  96
#define MAX_STEPS  512
#define DIGITS     9
#define BIT(d)     (1u << (d))
#define ALL_DIGITS 0x3FEu               /* bits 1..9 */

enum { AXIS_ROW, AXIS_COL, AXIS_BOX };
enum { RULE_ALLDIFF, RULE_SUM, RULE_SANDWICH, RULE_KINDS };
/* Rank order == priority order. A level's ceiling is the highest technique a player may need. */
enum { TECH_LAST_DIGIT, TECH_NAKED_SINGLE, TECH_HIDDEN_SINGLE, TECH_SUM_COMBO, TECH_SANDWICH, TECH_COUNT };

/* ---- pools (columns) ---- */
extern int      grid_w, grid_h;
extern int8_t   cell_val[MAX_CELLS];    /* 0 = empty */
extern uint16_t cell_cand[MAX_CELLS];   /* bitmask, bit d set = digit d still possible */
extern uint8_t  cell_given[MAX_CELLS];
extern int8_t   cell_sol[MAX_CELLS];

extern int      n_rules;
extern uint8_t  rule_kind[MAX_RULES];
extern uint8_t  rule_axis[MAX_RULES];
extern int8_t   rule_line[MAX_RULES];
extern int16_t  rule_param[MAX_RULES];  /* SUM: target; SANDWICH: target */

extern int      n_steps;                /* solver trace */
extern uint8_t  step_tech[MAX_STEPS];
extern int16_t  step_rule[MAX_STEPS];   /* -1 if not rule-bound */
extern int16_t  step_cell[MAX_STEPS];   /* placed cell or -1 */
extern int8_t   step_digit[MAX_STEPS];
extern int16_t  step_changes[MAX_STEPS];/* candidate eliminations made */
extern int      contradiction;

extern uint8_t  tab_pc[1024], tab_sum[1024];

/* ---- core.c ---- */
void sys_init_tables(void);
int  sys_set_grid(int w, int h);                      /* 0 ok, -1 bad size */
int  sys_add_rule(int kind, int axis, int line, int param);
void sys_build_alldiff(void);
void sys_reset_state(void);                           /* val/cand from givens, clears trace */
void sys_seed(uint64_t s);
int  sys_rand_below(int n);
int  sys_backtrack(int random_order, int check_lines, int limit);
int  sys_lines_hold_at(int cell);

int  func_unit_len(int axis, int w, int h);
int  func_unit_count(int axis, int w, int h);
int  func_unit_cell(int axis, int line, int k, int w, int h);
int  func_box_of(int r, int c, int w);
int  func_digit_ok(const int8_t *v, int w, int h, int c, int d);
int  func_line_sum(const int8_t *v, int w, int h, int axis, int line);
int  func_sandwich_value(const int8_t *v, int w, int h, int axis, int line); /* -1 if no 1 or no 9 */
int  func_line_complete(const int8_t *v, int w, int h, int axis, int line);
int  func_combo_union(int k, int R, unsigned allowed); /* -1 none; else OR of all k-subsets summing to R */

/* ---- solver.c ---- */
int  sys_solve(int ceiling);                          /* 1 if every cell filled */

/* ---- gen.c ---- */
#define NLEVELS 5
extern const char *lvl_name[NLEVELS];
int  sys_generate(int level, uint64_t seed);          /* attempts used, or 0 on failure */
int  func_level_ceiling(int level);
int  func_level_must_use(int level);
#endif
