/*
 * proclore.c - report on a process, read straight out of /proc.
 */
#include "minishell.h"
#include "builtins.h"
#include "util.h"

/*
 * Parse the state and process group out of /proc/<pid>/stat. The command name
 * sits in parentheses and may itself contain spaces and parentheses, so the
 * scan starts after the last `)`.
 */
static int read_stat(pid_t pid, char *state, pid_t *pgrp)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[4096];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    char *close_paren = strrchr(line, ')');
    if (!close_paren) return -1;

    int ppid = 0, group = 0;
    if (sscanf(close_paren + 1, " %c %d %d", state, &ppid, &group) != 3) return -1;

    *pgrp = (pid_t)group;
    return 0;
}

static long read_virtual_memory(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/statm", (int)pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    long pages = 0;
    int read_ok = fscanf(f, "%ld", &pages) == 1;
    fclose(f);
    if (!read_ok) return -1;

    return pages * (sysconf(_SC_PAGESIZE) / 1024);   /* kB, as ps reports it */
}

int builtin_proclore(shell_t *sh, int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "proclore: usage: proclore [pid]\n");
        return 1;
    }

    pid_t pid = getpid();
    if (argc == 2) {
        int requested;
        if (!parse_int(argv[1], &requested) || requested <= 0) {
            fprintf(stderr, "proclore: `%s` is not a pid\n", argv[1]);
            return 1;
        }
        pid = (pid_t)requested;
    }

    char state = '?';
    pid_t pgrp = 0;
    if (read_stat(pid, &state, &pgrp) != 0) {
        fprintf(stderr, "proclore: %d: no such process\n", (int)pid);
        return 1;
    }

    /* A process in the terminal's foreground group is marked with `+`. */
    pid_t foreground = sh->term_fd >= 0 ? tcgetpgrp(sh->term_fd) : -1;
    const char *plus = (foreground > 0 && pgrp == foreground) ? "+" : "";

    char exe_path[PATH_MAX] = "unavailable";
    char link[64];
    snprintf(link, sizeof(link), "/proc/%d/exe", (int)pid);
    char target[PATH_MAX];
    ssize_t len = readlink(link, target, sizeof(target) - 1);
    if (len > 0) {
        target[len] = '\0';
        path_abbreviate(exe_path, sizeof(exe_path), target, sh->home);
    }

    long memory = read_virtual_memory(pid);

    printf("pid : %d\n", (int)pid);
    printf("process status : %c%s\n", state, plus);
    printf("process group : %d\n", (int)pgrp);
    if (memory >= 0) printf("virtual memory : %ld kB\n", memory);
    else             printf("virtual memory : unavailable\n");
    printf("executable path : %s\n", exe_path);
    return 0;
}
