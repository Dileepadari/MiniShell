#include "minishell.h"
#include "signals.h"

static const int job_control_signals[] = {
    SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU
};

void signals_install_shell(shell_t *sh)
{
    if (!sh->interactive) return;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);

    for (size_t i = 0; i < sizeof(job_control_signals) / sizeof(int); i++)
        sigaction(job_control_signals[i], &sa, NULL);

    /* Writing into a pipe whose reader has gone must fail with EPIPE rather
     * than kill the shell, so that `peek | head -1` is not fatal. */
    sigaction(SIGPIPE, &sa, NULL);
}

void signals_reset_child(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);

    for (size_t i = 0; i < sizeof(job_control_signals) / sizeof(int); i++)
        sigaction(job_control_signals[i], &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
}
