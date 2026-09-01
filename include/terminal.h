/*
 * terminal.h - the interactive line reader.
 *
 * On a terminal this puts the tty into raw mode and implements editing,
 * history recall and tab completion itself. When input is a pipe or a file it
 * falls back to reading whole lines, so the shell can be scripted and tested.
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include "minishell.h"

/* Remember the terminal settings so they can always be put back. */
void terminal_init(shell_t *sh);

/* Undo raw mode. Registered with atexit(), and safe to call repeatedly. */
void terminal_restore(void);

/*
 * Read one line. Returns a newly allocated string without its newline, an empty
 * string if the user pressed Ctrl-C, or NULL at end of input (Ctrl-D).
 */
char *line_read(shell_t *sh, const char *prompt, size_t prompt_width);

#endif /* TERMINAL_H */
