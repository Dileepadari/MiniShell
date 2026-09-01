#include "test.h"
#include "minishell.h"
#include "history.h"
#include "util.h"

void test_history(void)
{
    suite("history");

    shell_t sh;
    memset(&sh, 0, sizeof(sh));
    snprintf(sh.history_path, sizeof(sh.history_path),
             "/tmp/minishell-history-test-%d", (int)getpid());
    unlink(sh.history_path);

    history_clear_memory();
    CHECK_INT(history_count(), 0);
    CHECK(history_get(0) == NULL);

    CHECK_INT(history_add(&sh, "peek -la"), 1);
    CHECK_INT(history_count(), 1);
    CHECK_STR(history_get(0), "peek -la");

    /* Surrounding whitespace is trimmed before the line is stored. */
    CHECK_INT(history_add(&sh, "   warp ..  "), 1);
    CHECK_STR(history_get(1), "warp ..");

    /* Blank lines, immediate repeats and `pastevents` itself are not kept. */
    CHECK_INT(history_add(&sh, "   "), 0);
    CHECK_INT(history_add(&sh, "warp .."), 0);
    CHECK_INT(history_add(&sh, "pastevents"), 0);
    CHECK_INT(history_add(&sh, "pastevents purge"), 0);
    CHECK_INT(history_count(), 2);

    /* A command that merely starts with the same letters is kept. */
    CHECK_INT(history_add(&sh, "pasteventsx"), 1);
    CHECK_INT(history_count(), 3);

    /* The list is capped, dropping the oldest entry first. */
    for (int i = 0; i < HISTORY_MAX + 5; i++) {
        char line[64];
        snprintf(line, sizeof(line), "command %d", i);
        history_add(&sh, line);
    }
    CHECK_INT(history_count(), HISTORY_MAX);
    CHECK_STR(history_get(HISTORY_MAX - 1), "command 19");
    CHECK_STR(history_get(0), "command 5");

    /* What was saved comes back in the same order. */
    history_save(&sh);
    history_clear_memory();
    CHECK_INT(history_count(), 0);
    history_load(&sh);
    CHECK_INT(history_count(), HISTORY_MAX);
    CHECK_STR(history_get(0), "command 5");
    CHECK_STR(history_get(HISTORY_MAX - 1), "command 19");

    /* Purging empties the list and removes the file. */
    history_purge(&sh);
    CHECK_INT(history_count(), 0);
    CHECK(access(sh.history_path, F_OK) != 0);

    /* Loading a missing file is not an error. */
    history_load(&sh);
    CHECK_INT(history_count(), 0);

    history_clear_memory();
    unlink(sh.history_path);
}
