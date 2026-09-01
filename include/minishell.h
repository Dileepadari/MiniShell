/*
 * minishell.h - shared configuration, limits and the shell state object.
 *
 * Every translation unit includes this first. It pulls in the POSIX headers the
 * shell needs and defines the single `shell_t` that carries all mutable state,
 * so that no module has to reach for a global of its own.
 */
#ifndef MINISHELL_H
#define MINISHELL_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MINISHELL_VERSION "2.0.0"

/* Longest line the reader will accept, including the terminating newline. */
#define MAX_INPUT_LEN 8192

/* Number of commands `pastevents` remembers, per the shell's specification. */
#define HISTORY_MAX 15

/* Jobs the shell tracks at once. Beyond this, new jobs still run, but they are
 * not listed by `activities` and cannot be named by `fg`/`bg`. */
#define MAX_JOBS 128

/* A foreground command that runs longer than this many seconds gets its name
 * and duration appended to the next prompt. */
#define SLOW_COMMAND_SECONDS 2

/* Name of the history file inside the shell's home directory. */
#define HISTORY_FILE ".minishell_history"

typedef struct job job_t;

typedef struct shell {
    char  home[PATH_MAX];       /* the shell's "~": cwd at startup, or $MINISHELL_HOME */
    char  prev_dir[PATH_MAX];   /* target of `warp -` */
    char  history_path[PATH_MAX];

    int   last_status;          /* $? - exit status of the last pipeline */
    int   interactive;          /* stdin and stderr are both a terminal */
    int   should_exit;          /* set by the `exit` builtin */
    pid_t pgid;                 /* the shell's own process group */
    int   term_fd;              /* controlling terminal, or -1 when not interactive */

    job_t *jobs;                /* job table, MAX_JOBS entries */
    int    next_job_id;

    /* Timing report carried into the next prompt; empty when there is none. */
    char  slow_cmd[64];
    int   slow_secs;
} shell_t;

/* Defined in shell.c. The signal handlers need it and cannot be passed one. */
extern shell_t g_shell;

#endif /* MINISHELL_H */
