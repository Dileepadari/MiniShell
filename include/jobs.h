/*
 * jobs.h - the job table and the job-control builtins.
 *
 * One job is one pipeline. Every job runs in its own process group, so that the
 * terminal can deliver Ctrl-C and Ctrl-Z to the foreground job alone and never
 * to the shell.
 */
#ifndef JOBS_H
#define JOBS_H

#include "minishell.h"

typedef enum {
    JOB_RUNNING = 0,
    JOB_STOPPED
} job_state_t;

struct job {
    int          used;        /* this slot holds a job */
    int          id;          /* job number; 0 until a job goes to the background */
    pid_t        pgid;
    pid_t       *pids;
    int          npids;
    int          nlive;
    char        *cmd;
    job_state_t  state;
    int          background;
};

void   jobs_init(shell_t *sh);
void   jobs_shutdown(shell_t *sh);

/*
 * Register a running pipeline. Takes a copy of `pids` and `cmd`. A background
 * job is numbered straight away; a foreground one is numbered only if it later
 * stops, so that job numbers count background jobs and nothing else.
 */
job_t *jobs_add(shell_t *sh, pid_t pgid, const pid_t *pids, int npids,
                const char *cmd, job_state_t state, int background);

job_t *jobs_find_by_id(shell_t *sh, int id);
job_t *jobs_at(shell_t *sh, int slot);      /* NULL when the slot is free */
int    jobs_slot_of(shell_t *sh, job_t *j);
job_t *jobs_find_by_pid(shell_t *sh, pid_t pid);

/*
 * Collect status changes without blocking. When `report` is set, finished and
 * stopped background jobs are announced on stderr. Call it before each prompt.
 */
void jobs_poll(shell_t *sh, int report);

/*
 * Wait for a foreground job. Returns the exit status of its last process, or
 * 128+signal if that process was killed. A job that stops is kept in the table.
 */
int  jobs_wait_foreground(shell_t *sh, job_t *job);

/* Hand the terminal to `pgid` (or back to the shell when pgid is 0). */
void jobs_give_terminal(shell_t *sh, pid_t pgid);

int builtin_activities(shell_t *sh, int argc, char **argv);
int builtin_fg(shell_t *sh, int argc, char **argv);
int builtin_bg(shell_t *sh, int argc, char **argv);

#endif /* JOBS_H */
