/*
 * sudoku-but engine: the core API.
 *
 * This is the whole game minus presentation. It knows nothing about pixels, colours, fonts, input devices or
 * layout, so any front end (terminal, desktop, mobile, test harness) is just another consumer. In the Arche
 * style the interface is data plus systems:
 *   - READ the public columns freely (declared here and in sb.h): cell geometry, givens, rules, hints, play state.
 *   - WRITE only through eng_* systems, so history, checks and invariants stay consistent.
 * Everything is static storage; there are no handles to free and no callbacks. One game is live at a time.
 *
 * Cell ids are row-major: id = row * eng_width() + col. Digits are 1..9; 0 means empty/none.
 * Hints are reported as a shape id (SHAPE_*); mapping a shape to a colour is the front end's business.
 */
#ifndef ENGINE_H
#define ENGINE_H
#include <stdint.h>
#include "sb.h"

enum {
    ENG_OK = 0,
    ENG_ERR_RANGE = -1,         /* bad cell or digit */
    ENG_ERR_GIVEN = -2,         /* cell is a given; it cannot be changed */
    ENG_ERR_NO_MINES = -3,      /* bomb mark on a level without a mine layer */
    ENG_ERR_HINT_SAFE = -4,     /* hint cells are safe by rule; cannot be marked */
    ENG_ERR_NOTHING = -5,       /* nothing to undo/redo */
    ENG_ERR_INCONSISTENT = -6,  /* hint requested but the current entries contradict the rules */
    ENG_ERR_SOLVED = -7,        /* nothing left to do */
    ENG_ERR_BAD_DATA = -8,      /* eng_load rejected the buffer */
    ENG_ERR_GENERATE = -9       /* no puzzle could be made for this level/seed */
};

/* ---- play state (public columns; write only via eng_*) ---- */
extern int      play_val[MAX_CELLS];      /* digit the player entered (0 none); never set on givens */
extern uint16_t play_notes[MAX_CELLS];    /* pencil marks, bit d = digit d */
extern uint8_t  play_bomb[MAX_CELLS];     /* 1 = player marked this cell as a bomb */
extern uint8_t  play_wrong[MAX_CELLS];    /* result of the last eng_check: WRONG_* bits, cleared by any move */
enum { WRONG_DIGIT = 1, WRONG_BOMB = 2 };

/* ---- last hint (public columns) ---- */
extern int hint_tech;      /* TECH_*, or -1 if none */
extern int hint_rule;      /* rule id the step came from, or -1 */
extern int hint_cell;      /* cell the step placed a digit in, or -1 */
extern int hint_digit;
extern int hint_changes;   /* candidates removed (elimination steps) */

/* ---- game lifecycle ---- */
int  eng_level_count(void);
const char *eng_level_name(int level);
int  eng_new_game(int level, uint64_t seed);          /* ENG_OK or ENG_ERR_*; deterministic in (level, seed) */
int  eng_level(void);
uint64_t eng_seed(void);

/* ---- geometry and clues (pure reads) ---- */
int  eng_width(void);
int  eng_height(void);
int  eng_cell_is_given(int cell);
int  eng_cell_digit(int cell);                        /* what the board shows: given, else player's, else 0 */
int  eng_cell_hint(int cell);                         /* SHAPE_NONE or SHAPE_KING: an active mine hint is centred here */
int  eng_has_mines(void);
int  eng_mines_total(void);                           /* the counter's target (0 on levels without mines) */
int  eng_bombs_marked(void);
int  eng_rule_count(void);                            /* rules are public columns: rule_kind/axis/line/param */

/* ---- moves (all undoable) ---- */
int  eng_set_digit(int cell, int digit);              /* digit 1..9, or 0 to clear the digit */
int  eng_toggle_note(int cell, int digit);
int  eng_toggle_bomb(int cell);                       /* bomb and flag are one mark */
int  eng_clear_cell(int cell);                        /* digit, notes and bomb together */
int  eng_undo(void);
int  eng_redo(void);

/* ---- checking and help (manual; nothing runs unless called) ---- */
int  eng_check(void);                                 /* fills play_wrong; returns how many entries are wrong */
int  eng_is_solved(void);                             /* all digits right and every bomb marked within the counter */
int  eng_hint(void);                                  /* fills hint_*; ENG_OK or ENG_ERR_*; changes no play state */
const char *eng_tech_name(int tech);

/* ---- persistence: puzzle is regenerated from (level, seed), so a save is small ---- */
int  eng_save(uint8_t *buf, int cap);                 /* bytes written, or ENG_ERR_* */
int  eng_load(const uint8_t *buf, int len);           /* ENG_OK or ENG_ERR_* */
#endif
