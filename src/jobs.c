#include "minishell.h"
#include "jobs.h"
#include "util.h"

#include <termios.h>

void jobs_init(shell_t *sh)
{
    sh->jobs = xcalloc(MAX_JOBS, sizeof(job_t));
    sh->next_job_id = 1;
}

static void job_release(job_t *j)
{
    free(j->pids);
    free(j->cmd);
    memset(j, 0, sizeof(*j));
}

void jobs_shutdown(shell_t *sh)
{
    if (!sh->jobs) return;
    for (int i = 0; i < MAX_JOBS; i++)
        if (sh->jobs[i].used) job_release(&sh->jobs[i]);
    free(sh->jobs);
    sh->jobs = NULL;
}

job_t *jobs_add(shell_t *sh, pid_t pgid, const pid_t *pids, int npids,
                const char *cmd, job_state_t state, int background)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *j = &sh->jobs[i];
        if (j->used) continue;

        j->used       = 1;
        j->id         = background ? sh->next_job_id++ : 0;
        j->pgid       = pgid;
        j->npids      = npids;
        j->nlive      = npids;
        j->pids       = xmalloc((size_t)npids * sizeof(pid_t));
        memcpy(j->pids, pids, (size_t)npids * sizeof(pid_t));
        j->cmd        = xstrdup(cmd);
        j->state      = state;
        j->background = background;
        return j;
    }
    return NULL; /* table full: the pipeline still runs, it is just untracked */
}

job_t *jobs_find_by_id(shell_t *sh, int id)
{
    if (id <= 0) return NULL;
    for (int i = 0; i < MAX_JOBS; i++)
        if (sh->jobs[i].used && sh->jobs[i].id == id) return &sh->jobs[i];
    return NULL;
}

job_t *jobs_at(shell_t *sh, int slot)
{
    if (slot < 0 || slot >= MAX_JOBS || !sh->jobs[slot].used) return NULL;
    return &sh->jobs[slot];
}

int jobs_slot_of(shell_t *sh, job_t *j)
{
    return (int)(j - sh->jobs);
}

job_t *jobs_find_by_pid(shell_t *sh, pid_t pid)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *j = &sh->jobs[i];
        if (!j->used) continue;
        if (j->pgid == pid) return j;
        for (int k = 0; k < j->npids; k++)
            if (j->pids[k] == pid) return j;
    }
    return NULL;
}

/* Describe how a process finished, for the completion notice. */
static void describe_status(char *out, size_t n, int status)
{
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) snprintf(out, n, "done");
        else           snprintf(out, n, "exited with %d", code);
    } else if (WIFSIGNALED(status)) {
        snprintf(out, n, "terminated by %s", strsignal(WTERMSIG(status)));
    } else {
        snprintf(out, n, "ended abnormally");
    }
}

/*
 * Record one status change. Returns the job the pid belongs to, or NULL when
 * the pid is not one of ours (a process the shell adopted, for instance).
 */
static job_t *absorb_status(shell_t *sh, pid_t pid, int status, int report)
{
    job_t *j = jobs_find_by_pid(sh, pid);
    if (!j) return NULL;

    if (WIFSTOPPED(status)) {
        j->state = JOB_STOPPED;
        if (!j->id) j->id = sh->next_job_id++;
        if (report) fprintf(stderr, "[%d] stopped   %s\n", j->id, j->cmd);
        return j;
    }
    if (WIFCONTINUED(status)) {
        j->state = JOB_RUNNING;
        return j;
    }

    if (--j->nlive <= 0) {
        if (report && j->id) {   /* an unnumbered job was never backgrounded */
            char how[64];
            describe_status(how, sizeof(how), status);
            fprintf(stderr, "[%d] %-16s %s\n", j->id, how, j->cmd);
        }
        job_release(j);
        return NULL;
    }
    return j;
}

void jobs_poll(shell_t *sh, int report)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
        absorb_status(sh, pid, status, report);
}

void jobs_give_terminal(shell_t *sh, pid_t pgid)
{
    if (!sh->interactive || sh->term_fd < 0) return;

    /* The shell ignores SIGTTOU, so this call cannot stop us. */
    tcsetpgrp(sh->term_fd, pgid ? pgid : sh->pgid);
}

int jobs_wait_foreground(shell_t *sh, job_t *job)
{
    /* Copy what we need: absorb_status() frees the entry when the last process
     * of the pipeline exits, so `job` must not be read after that point. */
    pid_t pgid = job->pgid;
    pid_t last = job->pids[job->npids - 1];
    int   slot = jobs_slot_of(sh, job);
    int   last_status = 0;

    while (1) {
        int status;
        pid_t pid = waitpid(-pgid, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR) continue;
            break; /* ECHILD: everything has been collected */
        }

        /* The status of the last process in the pipeline is the job's status. */
        if (pid == last) {
            if (WIFEXITED(status))        last_status = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
        }

        if (WIFSTOPPED(status)) {
            job_t *j = jobs_at(sh, slot);
            if (j) {
                j->state      = JOB_STOPPED;
                j->background = 1;
                if (!j->id) j->id = sh->next_job_id++;
                fprintf(stderr, "\n[%d] stopped   %s\n", j->id, j->cmd);
            }
            return 128 + WSTOPSIG(status);
        }

        absorb_status(sh, pid, status, 0);
        if (!jobs_at(sh, slot)) break;   /* the whole pipeline is done */
    }
    return last_status;
}

/* --------------------------------------------------------- job builtins --- */

static int compare_jobs(const void *a, const void *b)
{
    const job_t *ja = *(job_t *const *)a;
    const job_t *jb = *(job_t *const *)b;
    int cmp = strcmp(ja->cmd, jb->cmd);
    return cmp ? cmp : (ja->id - jb->id);
}

int builtin_activities(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;

    job_t *listed[MAX_JOBS];
    int n = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (sh->jobs[i].used && sh->jobs[i].id) listed[n++] = &sh->jobs[i];

    if (n == 0) {
        printf("No activities spawned by this shell.\n");
        return 0;
    }

    /* The specification asks for the list in lexicographic command order. */
    qsort(listed, (size_t)n, sizeof(job_t *), compare_jobs);
    for (int i = 0; i < n; i++)
        printf("[%d] : %s - %s\n", listed[i]->id, listed[i]->cmd,
               listed[i]->state == JOB_STOPPED ? "Stopped" : "Running");
    return 0;
}

/*
 * Resolve the argument of fg/bg. Accepts a job number, or a pid belonging to a
 * known job, so that the number printed when a job is launched also works.
 */
static job_t *resolve_job(shell_t *sh, const char *arg, const char *who)
{
    if (!arg) {
        /* No argument: the most recently created job. */
        job_t *best = NULL;
        for (int i = 0; i < MAX_JOBS; i++)
            if (sh->jobs[i].used && sh->jobs[i].id &&
                (!best || sh->jobs[i].id > best->id))
                best = &sh->jobs[i];
        if (!best) fprintf(stderr, "%s: no current job\n", who);
        return best;
    }

    int n;
    if (!parse_int(arg, &n)) {
        fprintf(stderr, "%s: `%s` is not a job number\n", who, arg);
        return NULL;
    }

    job_t *j = jobs_find_by_id(sh, n);
    if (!j) j = jobs_find_by_pid(sh, (pid_t)n);
    if (!j) fprintf(stderr, "%s: no job numbered %d\n", who, n);
    return j;
}

int builtin_fg(shell_t *sh, int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "fg: usage: fg [job number]\n");
        return 1;
    }

    job_t *j = resolve_job(sh, argc == 2 ? argv[1] : NULL, "fg");
    if (!j) return 1;

    printf("%s\n", j->cmd);
    fflush(stdout);

    j->state      = JOB_RUNNING;
    j->background = 0;

    jobs_give_terminal(sh, j->pgid);
    if (kill(-j->pgid, SIGCONT) < 0 && errno != ESRCH)
        perror("fg: SIGCONT");

    int status = jobs_wait_foreground(sh, j);
    jobs_give_terminal(sh, 0);
    return status;
}

int builtin_bg(shell_t *sh, int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "bg: usage: bg [job number]\n");
        return 1;
    }

    job_t *j = resolve_job(sh, argc == 2 ? argv[1] : NULL, "bg");
    if (!j) return 1;

    if (j->state == JOB_RUNNING) {
        fprintf(stderr, "bg: job [%d] is already running\n", j->id);
        return 1;
    }

    j->state      = JOB_RUNNING;
    j->background = 1;
    if (kill(-j->pgid, SIGCONT) < 0) {
        perror("bg");
        return 1;
    }

    printf("[%d] %s &\n", j->id, j->cmd);
    return 0;
}
