/* edit_internal.h — what the engine's own translation units share.
 *
 * Deliberately not in syn-edit.h. These are the moving parts of the editor
 * (how a register is indexed, how a range of text is removed) and nothing
 * outside vim.c and ex.c has any business calling them: a front-end that
 * deleted a range directly would be a front-end that bypassed the undo
 * journal, which is the one rule buffer.c exists to enforce.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_EDIT_INTERNAL_H
#define SYN_EDIT_INTERNAL_H

#include "syn-edit.h"

buf_t *ed_cur(ed_t *e);
void   ed_clamp(ed_t *e);
size_t line_first_nonblank(const buf_t *b, size_t y);

/* Registers. `c` is the character the user typed: 'a', '3', '"', '+'. An
 * uppercase letter APPENDS, which is why setting one is not just an index. */
int          reg_index(int c);
void         reg_set(ed_t *e, int c, const char *text, bool linewise);
const reg_t *reg_get(ed_t *e, int c);

/* Ranges. x1 is EXCLUSIVE for charwise; for linewise the x values are ignored
 * and y1 is inclusive. Both forms take the range already normalised. */
char *range_text(ed_t *e, size_t y0, size_t x0, size_t y1, size_t x1,
                 bool linewise);
void  range_delete(ed_t *e, size_t y0, size_t x0, size_t y1, size_t x1,
                   bool linewise);
void  insert_text_at(ed_t *e, size_t y, size_t x, const char *text,
                     size_t *endy, size_t *endx);
void  set_line(ed_t *e, size_t y, const char *text);

/* Search and substitute. Both compile the pattern with POSIX regcomp: BRE by
 * default, which is the closest thing in libc to vim's default magic level
 * (\( \) \| \+ \? all behave), and ERE when the pattern opens with \v. */
bool ed_search(ed_t *e, const char *pat, int dir, char **err);
long ed_substitute(ed_t *e, size_t y0, size_t y1, const char *pat,
                   const char *rep, bool global, bool icase, char **err);
bool ed_regex_ok(const char *pat, char **err);

#endif /* SYN_EDIT_INTERNAL_H */
