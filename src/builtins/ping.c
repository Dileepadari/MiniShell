/*
 * ping.c - send a signal to a process.
 */
#include "minishell.h"
#include "builtins.h"
#include "util.h"

int builtin_ping(shell_t *sh, int argc, char **argv)
{
    (void)sh;

    if (argc != 3) {
        fprintf(stderr, "ping: usage: ping <pid> <signal>\n");
        return 1;
    }

    int pid, signal_number;
    if (!parse_int(argv[1], &pid)) {
        fprintf(stderr, "ping: `%s` is not a pid\n", argv[1]);
        return 1;
    }
    if (!parse_int(argv[2], &signal_number)) {
        fprintf(stderr, "ping: `%s` is not a signal number\n", argv[2]);
        return 1;
    }

    /* The specification wraps the signal number into the valid range. */
    signal_number %= 32;
    if (signal_number < 0) signal_number += 32;

    if (kill((pid_t)pid, signal_number) != 0) {
        fprintf(stderr, "ping: %d: %s\n", pid, strerror(errno));
        return 1;
    }

    printf("Sent signal %d to process with pid %d\n", signal_number, pid);
    return 0;
}
