/*
 * warp.c - change directory.
 *
 * `~` has already become the shell's home by the time the parser is done, so
 * the only special token left here is `-`.
 */
#include "minishell.h"
#include "builtins.h"
#include "util.h"

static int warp_to(shell_t *sh, const char *target)
{
    const char *dest = target;
    if (!strcmp(target, "-")) {
        dest = sh->prev_dir;
        if (!*dest) {
            fprintf(stderr, "warp: no previous directory\n");
            return 1;
        }
    }

    char before[PATH_MAX];
    if (!getcwd(before, sizeof(before))) before[0] = '\0';

    if (chdir(dest) != 0) {
        fprintf(stderr, "warp: %s: %s\n", dest, strerror(errno));
        return 1;
    }

    if (before[0]) snprintf(sh->prev_dir, sizeof(sh->prev_dir), "%s", before);

    char after[PATH_MAX];
    if (getcwd(after, sizeof(after))) printf("%s\n", after);
    return 0;
}

int builtin_warp(shell_t *sh, int argc, char **argv)
{
    if (argc == 1) return warp_to(sh, sh->home);

    int status = 0;
    for (int i = 1; i < argc; i++) {
        status = warp_to(sh, argv[i]);
        if (status != 0) break;   /* stop at the first hop that fails */
    }
    return status;
}
