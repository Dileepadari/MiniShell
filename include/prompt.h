/*
 * prompt.h - renders the interactive prompt.
 */
#ifndef PROMPT_H
#define PROMPT_H

#include "minishell.h"

/*
 * Write the prompt into `out`. When `color` is set the string carries ANSI
 * escapes; `*width` always receives the number of visible columns, which the
 * line editor needs in order to place the cursor.
 */
void prompt_build(shell_t *sh, char *out, size_t n, int color, size_t *width);

#endif /* PROMPT_H */
