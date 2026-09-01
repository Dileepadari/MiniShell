#include "minishell.h"
#include "history.h"
#include "util.h"

static char *entries[HISTORY_MAX];
static int   count;

int history_count(void)
{
    return count;
}

const char *history_get(int index)
{
    if (index < 0 || index >= count) return NULL;
    return entries[index];
}

void history_clear_memory(void)
{
    for (int i = 0; i < count; i++) {
        free(entries[i]);
        entries[i] = NULL;
    }
    count = 0;
}

/* Drop the oldest entry to make room for one more. */
static void evict_oldest(void)
{
    free(entries[0]);
    memmove(&entries[0], &entries[1], (HISTORY_MAX - 1) * sizeof(char *));
    entries[HISTORY_MAX - 1] = NULL;
    count--;
}

static int is_pastevents_command(const char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, "pastevents", 10) != 0) return 0;
    return line[10] == '\0' || line[10] == ' ' || line[10] == '\t';
}

int history_add(shell_t *sh, const char *line)
{
    char *copy = xstrdup(line);
    str_trim(copy);

    if (copy[0] == '\0' || is_pastevents_command(copy) ||
        (count > 0 && strcmp(entries[count - 1], copy) == 0)) {
        free(copy);
        return 0;
    }

    if (count == HISTORY_MAX) evict_oldest();
    entries[count++] = copy;
    if (sh) history_save(sh);
    return 1;
}

void history_load(shell_t *sh)
{
    history_clear_memory();

    FILE *f = fopen(sh->history_path, "r");
    if (!f) return;

    char line[MAX_INPUT_LEN];
    while (fgets(line, sizeof(line), f)) {
        str_trim(line);
        if (line[0] == '\0') continue;
        if (count == HISTORY_MAX) evict_oldest();
        entries[count++] = xstrdup(line);
    }
    fclose(f);
}

void history_save(shell_t *sh)
{
    /* Write through a temporary file so an interrupted save cannot truncate
     * the history that is already on disk. */
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", sh->history_path) >= (int)sizeof(tmp))
        return;

    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < count; i++) fprintf(f, "%s\n", entries[i]);
    fclose(f);

    if (rename(tmp, sh->history_path) != 0) unlink(tmp);
}

void history_purge(shell_t *sh)
{
    history_clear_memory();
    unlink(sh->history_path);
}
