/*
 * pastevents.c - show, clear or re-run the command history.
 */
#include "minishell.h"
#include "builtins.h"
#include "history.h"
#include "shell.h"
#include "util.h"

int builtin_pastevents(shell_t *sh, int argc, char **argv)
{
    if (argc == 1) {
        int n = history_count();
        for (int i = 0; i < n; i++)
            printf("%2d  %s\n", i + 1, history_get(i));
        return 0;
    }

    if (argc == 2 && !strcmp(argv[1], "purge")) {
        history_purge(sh);
        return 0;
    }

    if (argc == 3 && !strcmp(argv[1], "execute")) {
        int index;
        if (!parse_int(argv[2], &index)) {
            fprintf(stderr, "pastevents: `%s` is not a number\n", argv[2]);
            return 1;
        }

        const char *line = history_get(index - 1);
        if (!line) {
            fprintf(stderr, "pastevents: no event numbered %d\n", index);
            return 1;
        }

        /* Copy first: running the line can rewrite the history it lives in. */
        char *command = xstrdup(line);
        printf("%s\n", command);
        fflush(stdout);
        int status = shell_run_line(sh, command);
        free(command);
        return status;
    }

    fprintf(stderr, "pastevents: usage: pastevents [purge | execute <n>]\n");
    return 1;
}
