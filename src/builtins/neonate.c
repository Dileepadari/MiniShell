/*
 * neonate.c - print the most recently created pid at a fixed interval.
 *
 * The last field of /proc/loadavg is the pid the kernel handed out most
 * recently, which is exactly what this reports. The loop ends when the user
 * presses `x`; select() supplies both the delay and the keypress check, so no
 * time is spent sleeping through a keystroke.
 */
#include "minishell.h"
#include "builtins.h"
#include "util.h"

#include <sys/select.h>
#include <termios.h>

static int read_recent_pid(unsigned *pid)
{
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    char line[256];
    char *got = fgets(line, sizeof(line), f);
    fclose(f);
    if (!got) return -1;

    /* "0.00 0.01 0.05 1/523 12345" - the pid is the last field. */
    char *last = strrchr(line, ' ');
    if (!last) return -1;

    return sscanf(last + 1, "%u", pid) == 1 ? 0 : -1;
}

int builtin_neonate(shell_t *sh, int argc, char **argv)
{
    (void)sh;

    int seconds = 0;
    if (argc != 3 || strcmp(argv[1], "-n") != 0 ||
        !parse_int(argv[2], &seconds) || seconds <= 0) {
        fprintf(stderr, "neonate: usage: neonate -n <seconds>\n");
        return 1;
    }

    struct termios saved;
    int raw = 0;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
        struct termios raw_mode = saved;
        raw_mode.c_lflag &= ~(ICANON | ECHO);
        raw_mode.c_cc[VMIN]  = 0;
        raw_mode.c_cc[VTIME] = 0;
        raw = tcsetattr(STDIN_FILENO, TCSADRAIN, &raw_mode) == 0;
    }

    int status = 0;
    while (1) {
        unsigned pid;
        if (read_recent_pid(&pid) != 0) {
            fprintf(stderr, "neonate: cannot read /proc/loadavg: %s\n", strerror(errno));
            status = 1;
            break;
        }
        printf("%u\n", pid);
        fflush(stdout);

        /* Without a terminal there is no way to press `x`, so report once and
         * return rather than looping forever in a script. */
        if (!raw) break;

        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(STDIN_FILENO, &readable);
        struct timeval timeout = { .tv_sec = seconds, .tv_usec = 0 };

        int ready = select(STDIN_FILENO + 1, &readable, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1 && (c == 'x' || c == 'X')) break;
        }
    }

    if (raw) tcsetattr(STDIN_FILENO, TCSADRAIN, &saved);
    return status;
}
