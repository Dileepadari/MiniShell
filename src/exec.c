#include "minishell.h"
#include "exec.h"
#include "builtins.h"
#include "jobs.h"
#include "signals.h"
#include "terminal.h"
#include "util.h"

#include <fcntl.h>
#include <time.h>

/* ---------------------------------------------------------- redirection --- */

static int open_redirection_targets(command_t *c, int *in_fd, int *out_fd)
{
    *in_fd = *out_fd = -1;

    if (c->infile) {
        *in_fd = open(c->infile, O_RDONLY);
        if (*in_fd < 0) {
            fprintf(stderr, "minishell: %s: %s\n", c->infile, strerror(errno));
            return -1;
        }
    }
    if (c->outfile) {
        int flags = O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC);
        *out_fd = open(c->outfile, flags, 0644);
        if (*out_fd < 0) {
            fprintf(stderr, "minishell: %s: %s\n", c->outfile, strerror(errno));
            if (*in_fd >= 0) close(*in_fd);
            *in_fd = -1;
            return -1;
        }
    }
    return 0;
}

/* In a child: point stdin/stdout at the redirection targets and forget the fds. */
static int redirect_in_child(command_t *c)
{
    int in_fd, out_fd;
    if (open_redirection_targets(c, &in_fd, &out_fd) != 0) return -1;

    if (in_fd >= 0)  { dup2(in_fd, STDIN_FILENO);   close(in_fd); }
    if (out_fd >= 0) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    return 0;
}

typedef struct {
    int saved_in;
    int saved_out;
} redir_save_t;

/* In the shell itself: same thing, but remember the old fds so they come back. */
static int redirect_in_parent(command_t *c, redir_save_t *save)
{
    save->saved_in = save->saved_out = -1;
    if (!c->infile && !c->outfile) return 0;

    int in_fd, out_fd;
    if (open_redirection_targets(c, &in_fd, &out_fd) != 0) return -1;

    if (in_fd >= 0) {
        save->saved_in = dup(STDIN_FILENO);
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }
    if (out_fd >= 0) {
        fflush(stdout);
        save->saved_out = dup(STDOUT_FILENO);
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }
    return 0;
}

static void restore_in_parent(redir_save_t *save)
{
    if (save->saved_out >= 0) {
        fflush(stdout);
        dup2(save->saved_out, STDOUT_FILENO);
        close(save->saved_out);
    }
    if (save->saved_in >= 0) {
        dup2(save->saved_in, STDIN_FILENO);
        close(save->saved_in);
    }
    save->saved_in = save->saved_out = -1;
}

/* --------------------------------------------------------------- child --- */

/* Never returns. Runs one command of a pipeline in the freshly forked child. */
static void run_child(shell_t *sh, command_t *c, pid_t pgid, int background,
                      int in_fd, int out_fd, int close_fd)
{
    pid_t self = getpid();
    setpgid(self, pgid ? pgid : self);
    signals_reset_child();
    terminal_restore();

    if (in_fd >= 0)  { dup2(in_fd, STDIN_FILENO);   close(in_fd); }
    if (out_fd >= 0) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (close_fd >= 0) close(close_fd);

    /* A background job with no explicit input must not read the terminal. */
    if (background && !c->infile && in_fd < 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
    }

    if (redirect_in_child(c) != 0) _exit(1);

    const builtin_t *b = builtin_lookup(c->argv[0]);
    if (b) {
        int status = b->fn(sh, c->argc, c->argv);
        fflush(NULL);
        _exit(status);
    }

    execvp(c->argv[0], c->argv);

    fflush(NULL);
    if (errno == ENOENT)
        fprintf(stderr, "minishell: %s: command not found\n", c->argv[0]);
    else
        fprintf(stderr, "minishell: %s: %s\n", c->argv[0], strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

/* ------------------------------------------------------------ pipeline --- */

static int run_builtin_in_shell(shell_t *sh, command_t *c)
{
    redir_save_t save;
    if (redirect_in_parent(c, &save) != 0) return 1;

    const builtin_t *b = builtin_lookup(c->argv[0]);
    int status = b->fn(sh, c->argc, c->argv);

    fflush(stdout);
    restore_in_parent(&save);
    return status;
}

static int run_forked_pipeline(shell_t *sh, pipeline_t *p)
{
    /* Anything still buffered must reach the terminal before a child writes,
     * otherwise the shell's own output lands after the child's. */
    fflush(NULL);

    pid_t *pids = xmalloc((size_t)p->ncmds * sizeof(pid_t));
    pid_t  pgid = 0;
    int    prev_read = -1;
    int    launched = 0;

    for (int i = 0; i < p->ncmds; i++) {
        int fds[2] = {-1, -1};
        if (i < p->ncmds - 1 && pipe(fds) != 0) {
            perror("minishell: pipe");
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("minishell: fork");
            if (fds[0] >= 0) { close(fds[0]); close(fds[1]); }
            break;
        }

        if (pid == 0) {
            run_child(sh, &p->cmds[i], pgid, p->background,
                      prev_read, fds[1], fds[0]);
        }

        /* Set the group in the parent too: whichever call runs first wins, and
         * neither side may assume it was scheduled before the other. */
        setpgid(pid, pgid ? pgid : pid);
        if (!pgid) pgid = pid;
        pids[launched++] = pid;

        if (prev_read >= 0) close(prev_read);
        if (fds[1] >= 0) close(fds[1]);
        prev_read = fds[0];
    }

    if (prev_read >= 0) close(prev_read);

    if (launched == 0) {
        free(pids);
        return 1;
    }

    job_t *job = jobs_add(sh, pgid, pids, launched, p->text, JOB_RUNNING,
                          p->background);
    free(pids);

    if (p->background) {
        if (job) printf("[%d] %d\n", job->id, (int)pgid);
        else     printf("[-] %d\n", (int)pgid);
        fflush(stdout);
        return 0;
    }

    if (!job) {                 /* job table full: still wait, just untracked */
        int status = 0;
        for (int i = 0; i < launched; i++) waitpid(-pgid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    jobs_give_terminal(sh, pgid);
    int status = jobs_wait_foreground(sh, job);
    jobs_give_terminal(sh, 0);
    return status;
}

static int exec_pipeline(shell_t *sh, pipeline_t *p)
{
    if (p->ncmds == 0 || p->cmds[0].argc == 0) return 0;

    /* A lone foreground builtin runs here, in the shell, so that `warp`,
     * `exit` and `pastevents purge` can change the shell's own state. */
    if (p->ncmds == 1 && !p->background && builtin_lookup(p->cmds[0].argv[0]))
        return run_builtin_in_shell(sh, &p->cmds[0]);

    return run_forked_pipeline(sh, p);
}

int exec_pipeline_list(shell_t *sh, pipeline_t *list)
{
    int status = 0;

    for (pipeline_t *p = list; p && !sh->should_exit; p = p->next) {
        time_t started = time(NULL);

        status = exec_pipeline(sh, p);
        sh->last_status = status;

        /* Report anything slow enough to have been worth waiting for. */
        int elapsed = (int)difftime(time(NULL), started);
        if (!p->background && elapsed > SLOW_COMMAND_SECONDS) {
            snprintf(sh->slow_cmd, sizeof(sh->slow_cmd), "%s", p->cmds[0].argv[0]);
            sh->slow_secs = elapsed;
        }
    }
    return status;
}
