/*
 * shell.h - lifecycle of the shell itself.
 */
#ifndef SHELL_H
#define SHELL_H

#include "minishell.h"

void shell_init(shell_t *sh);
void shell_shutdown(shell_t *sh);

/* Read, parse and run one line. Returns the exit status of its last pipeline. */
int  shell_run_line(shell_t *sh, const char *line);

/* The read-eval-print loop. Returns the status the process should exit with. */
int  shell_run_interactive(shell_t *sh);

/*
 * Run every line of an already-open stream. `record` adds each line to the
 * history, which is right for piped input but not for a script file.
 */
int  shell_run_stream(shell_t *sh, FILE *in, int record);

#endif /* SHELL_H */
