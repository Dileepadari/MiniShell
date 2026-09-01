/*
 * table.c - the builtin dispatch table, plus `exit` and `help`.
 */
#include "minishell.h"
#include "builtins.h"
#include "jobs.h"
#include "util.h"

static const builtin_t builtins[] = {
    { "warp",       builtin_warp,
      "warp [dir]...",
      "change directory; `-` returns to the previous one, `~` is the shell home" },
    { "peek",       builtin_peek,
      "peek [-a] [-l] [path]...",
      "list a directory; -a includes hidden entries, -l shows details" },
    { "seek",       builtin_seek,
      "seek [-d|-f] [-e] <name> [dir]",
      "search a directory tree; -d directories only, -f files only, -e act on a lone match" },
    { "proclore",   builtin_proclore,
      "proclore [pid]",
      "report the state, group, memory and executable of a process" },
    { "pastevents", builtin_pastevents,
      "pastevents [purge | execute <n>]",
      "show, clear or re-run the command history" },
    { "activities", builtin_activities,
      "activities",
      "list the jobs this shell has spawned, in command order" },
    { "fg",         builtin_fg,
      "fg [job]",
      "resume a job in the foreground and hand it the terminal" },
    { "bg",         builtin_bg,
      "bg [job]",
      "resume a stopped job in the background" },
    { "ping",       builtin_ping,
      "ping <pid> <signal>",
      "send a signal to a process" },
    { "neonate",    builtin_neonate,
      "neonate -n <seconds>",
      "print the most recently created pid every <seconds>; press x to stop" },
    { "iMan",       builtin_iman,
      "iMan <command>",
      "fetch a manual page from man.he.net" },
    { "exit",       builtin_exit,
      "exit [status]",
      "leave the shell" },
    { "help",       builtin_help,
      "help [command]",
      "list the builtins, or explain one of them" },
};

const builtin_t *builtin_table(size_t *count)
{
    *count = sizeof(builtins) / sizeof(builtins[0]);
    return builtins;
}

const builtin_t *builtin_lookup(const char *name)
{
    size_t n;
    const builtin_t *table = builtin_table(&n);
    for (size_t i = 0; i < n; i++)
        if (!strcmp(table[i].name, name)) return &table[i];
    return NULL;
}

int builtin_exit(shell_t *sh, int argc, char **argv)
{
    int status = 0;

    if (argc > 2) {
        fprintf(stderr, "exit: usage: exit [status]\n");
        return 1;
    }
    if (argc == 2 && !parse_int(argv[1], &status)) {
        fprintf(stderr, "exit: `%s` is not a number\n", argv[1]);
        return 1;
    }

    sh->should_exit = 1;
    return status & 0xff;
}

int builtin_help(shell_t *sh, int argc, char **argv)
{
    (void)sh;

    size_t n;
    const builtin_t *table = builtin_table(&n);

    if (argc == 2) {
        const builtin_t *b = builtin_lookup(argv[1]);
        if (!b) {
            fprintf(stderr, "help: no builtin named `%s`\n", argv[1]);
            return 1;
        }
        printf("%s\n    %s\n", b->usage, b->summary);
        return 0;
    }

    printf("minishell %s builtins:\n\n", MINISHELL_VERSION);
    for (size_t i = 0; i < n; i++)
        printf("  %-14s %s\n", table[i].name, table[i].summary);
    printf("\nAnything else is looked up on PATH. `help <name>` shows one entry.\n");
    return 0;
}
