/*
 * history.h - the `pastevents` command list.
 *
 * The list holds at most HISTORY_MAX entries in memory and is mirrored to a
 * file in the shell's home directory, so it survives across sessions. The most
 * recent entry is the last one.
 */
#ifndef HISTORY_H
#define HISTORY_H

#include "minishell.h"

void history_load(shell_t *sh);
void history_save(shell_t *sh);

/*
 * Record a line. Blank lines, an exact repeat of the previous line, and any
 * line whose first word is `pastevents` are dropped, matching the shell's
 * specification. Returns 1 if the line was recorded.
 */
int  history_add(shell_t *sh, const char *line);

int         history_count(void);
const char *history_get(int index);   /* 0 is the oldest entry, NULL if absent */
void        history_purge(shell_t *sh);
void        history_clear_memory(void); /* tests and shutdown */

#endif /* HISTORY_H */
