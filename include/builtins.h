/*
 * builtins.h - the table of commands the shell implements itself.
 *
 * A builtin runs inside the shell process when it is the only command of a
 * foreground pipeline, so that `warp` and `exit` can change the shell's own
 * state. Inside a pipeline it runs in the forked child instead, exactly as a
 * POSIX shell does, and its side effects on shell state are therefore lost.
 */
#ifndef BUILTINS_H
#define BUILTINS_H

#include "minishell.h"
#include <stddef.h>

typedef int (*builtin_fn)(shell_t *sh, int argc, char **argv);

typedef struct {
    const char *name;
    builtin_fn  fn;
    const char *usage;
    const char *summary;
} builtin_t;

const builtin_t *builtin_lookup(const char *name);
const builtin_t *builtin_table(size_t *count);

int builtin_warp(shell_t *sh, int argc, char **argv);
int builtin_peek(shell_t *sh, int argc, char **argv);
int builtin_seek(shell_t *sh, int argc, char **argv);
int builtin_proclore(shell_t *sh, int argc, char **argv);
int builtin_pastevents(shell_t *sh, int argc, char **argv);
int builtin_iman(shell_t *sh, int argc, char **argv);
int builtin_neonate(shell_t *sh, int argc, char **argv);
int builtin_ping(shell_t *sh, int argc, char **argv);
int builtin_exit(shell_t *sh, int argc, char **argv);
int builtin_help(shell_t *sh, int argc, char **argv);

#endif /* BUILTINS_H */
