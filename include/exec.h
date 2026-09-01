/*
 * exec.h - runs parsed pipelines.
 */
#ifndef EXEC_H
#define EXEC_H

#include "minishell.h"
#include "parser.h"

/* Run every pipeline on the line in order. Returns the last exit status. */
int exec_pipeline_list(shell_t *sh, pipeline_t *list);

#endif /* EXEC_H */
