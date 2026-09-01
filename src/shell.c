#include "minishell.h"
#include "shell.h"
#include "exec.h"
#include "history.h"
#include "jobs.h"
#include "parser.h"
#include "prompt.h"
#include "signals.h"
#include "terminal.h"
#include "util.h"

#include <termios.h>

shell_t g_shell;

/* `$?` and `$$` come from the shell; everything else from the environment. */
static const char *shell_var_lookup(const char *name, void *ctx)
{
    static char scratch[32];
    shell_t *sh = ctx;

    if (!strcmp(name, "?")) {
        snprintf(scratch, sizeof(scratch), "%d", sh->last_status);
        return scratch;
    }
    if (!strcmp(name, "$")) {
        snprintf(scratch, sizeof(scratch), "%d", (int)getpid());
        return scratch;
    }
    return getenv(name);
}

/*
 * The shell's "~" is where it was started, not the account's home directory.
 * MINISHELL_HOME overrides that, which is what the test suite uses.
 */
static void resolve_home(shell_t *sh)
{
    const char *override = getenv("MINISHELL_HOME");
    if (override && *override) {
        char resolved[PATH_MAX];
        if (realpath(override, resolved)) {
            snprintf(sh->home, sizeof(sh->home), "%s", resolved);
            return;
        }
    }
    if (!getcwd(sh->home, sizeof(sh->home))) snprintf(sh->home, sizeof(sh->home), "/");
}

/*
 * Take control of the terminal. A shell started in the background must wait
 * until it is put in the foreground, otherwise every later attempt to touch the
 * terminal would stop it.
 *
 * When the tty is not our controlling terminal - a pty opened by a test
 * harness, say - tcgetpgrp() fails and there is no foreground group to wait
 * for. Job control is switched off in that case and the shell carries on with
 * line editing intact, rather than looping on a handshake that cannot finish.
 */
static void claim_terminal(shell_t *sh)
{
    sh->term_fd = STDIN_FILENO;

    for (int attempt = 0; attempt < 100; attempt++) {
        pid_t owner = tcgetpgrp(sh->term_fd);
        if (owner < 0) {
            sh->term_fd = -1;       /* no job control on this terminal */
            break;
        }
        if (owner == getpgrp()) break;
        kill(-getpgrp(), SIGTTIN);  /* stop until someone foregrounds us */
    }

    signals_install_shell(sh);

    if (sh->term_fd < 0) {
        sh->pgid = getpgrp();
        return;
    }

    /* Become our own process group leader, so that a child's group is never
     * the shell's. If that is refused, fall back to the group we are in and
     * carry on rather than refusing to start. */
    sh->pgid = getpid();
    if (setpgid(sh->pgid, sh->pgid) < 0) sh->pgid = getpgrp();
    tcsetpgrp(sh->term_fd, sh->pgid);
}

void shell_init(shell_t *sh)
{
    memset(sh, 0, sizeof(*sh));
    sh->term_fd     = -1;
    sh->interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);

    resolve_home(sh);
    strcpy(sh->prev_dir, sh->home);
    path_join(sh->history_path, sizeof(sh->history_path), sh->home, HISTORY_FILE);

    jobs_init(sh);
    history_load(sh);

    if (sh->interactive) {
        claim_terminal(sh);
        terminal_init(sh);
    } else {
        signals_install_shell(sh);
    }
}

void shell_shutdown(shell_t *sh)
{
    history_save(sh);
    history_clear_memory();
    jobs_shutdown(sh);
    terminal_restore();
}

int shell_run_line(shell_t *sh, const char *line)
{
    const char *cursor = line;
    int status = sh->last_status;

    /* Each `;`- or `&`-separated segment is scanned only once the one before it
     * has finished, so that expansions see the state that segment left behind. */
    while (cursor && *cursor && !sh->should_exit) {
        tokens_t toks;
        const char *err  = NULL;
        const char *rest = NULL;

        if (lex_segment(cursor, &toks, &err, shell_var_lookup, sh, &rest) != 0) {
            fprintf(stderr, "minishell: %s\n", err);
            tokens_free(&toks);
            return (sh->last_status = 2);
        }

        pipeline_t *list = NULL;
        int rc = parse_tokens(&toks, sh->home, &list, &err);
        tokens_free(&toks);

        if (rc != 0) {
            fprintf(stderr, "minishell: syntax error: %s\n", err);
            return (sh->last_status = 2);
        }

        status = exec_pipeline_list(sh, list);
        pipeline_free(list);
        cursor = rest;
    }
    return status;
}

int shell_run_stream(shell_t *sh, FILE *in, int record)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    while (!sh->should_exit && (n = getline(&line, &cap, in)) > 0) {
        if (line[n - 1] == '\n') line[n - 1] = '\0';
        jobs_poll(sh, 0);
        if (record) history_add(sh, line);
        shell_run_line(sh, line);
    }
    free(line);

    /* Give any background job a moment's grace, then report what finished. */
    jobs_poll(sh, 0);
    return sh->last_status;
}

int shell_run_interactive(shell_t *sh)
{
    while (!sh->should_exit) {
        jobs_poll(sh, 1);

        char text[PATH_MAX + 512];   /* path, user, host, timing and colours */
        size_t width = 0;
        prompt_build(sh, text, sizeof(text), sh->interactive, &width);
        sh->slow_cmd[0] = '\0';   /* the timing note is shown once */
        sh->slow_secs   = 0;

        char *line = line_read(sh, text, width);
        if (!line) {              /* Ctrl-D or end of input */
            if (sh->interactive) printf("exit\n");
            break;
        }

        if (*str_trim(line)) {
            history_add(sh, line);
            shell_run_line(sh, line);
        }
        free(line);
    }
    return sh->last_status;
}
