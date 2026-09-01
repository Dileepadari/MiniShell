/*
 * signals.h - signal disposition for the shell and for its children.
 *
 * An interactive shell ignores the job-control signals outright. The terminal
 * driver sends Ctrl-C and Ctrl-Z to the foreground process group, which is
 * always a child's group and never the shell's, so nothing is lost by that.
 */
#ifndef SIGNALS_H
#define SIGNALS_H

#include "minishell.h"

void signals_install_shell(shell_t *sh);

/* Restore every disposition to the default. Called between fork and exec. */
void signals_reset_child(void);

#endif /* SIGNALS_H */
