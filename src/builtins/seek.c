/*
 * seek.c - search a directory tree for a name.
 *
 * A name matches when it is exactly the target, or when the target is the name
 * with its extension removed, so `seek notes` finds both `notes` and `notes.md`.
 * Symbolic links are never followed, which keeps the walk free of cycles.
 */
#include "minishell.h"
#include "builtins.h"
#include "util.h"

#include <dirent.h>

#define COLOR_DIR   "\033[1;34m"
#define COLOR_FILE  "\033[1;32m"
#define COLOR_RESET "\033[0m"

typedef struct {
    const char *target;
    int  dirs_only;
    int  files_only;
    int  act;
    int  matched_dirs;
    int  matched_files;
    char last_match[PATH_MAX];
    int  last_is_dir;
    int  color;
} seek_ctx_t;

static int name_matches(const char *name, const char *target)
{
    if (!strcmp(name, target)) return 1;

    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;

    size_t stem = (size_t)(dot - name);
    return strlen(target) == stem && strncmp(name, target, stem) == 0;
}

static void report(seek_ctx_t *ctx, const char *path, int is_dir)
{
    if (is_dir) ctx->matched_dirs++;
    else        ctx->matched_files++;

    snprintf(ctx->last_match, sizeof(ctx->last_match), "%s", path);
    ctx->last_is_dir = is_dir;

    if (ctx->color)
        printf("%s%s%s\n", is_dir ? COLOR_DIR : COLOR_FILE, path, COLOR_RESET);
    else
        printf("%s\n", path);
}

static int compare_names(const void *a, const void *b)
{
    return strcoll(*(char *const *)a, *(char *const *)b);
}

/*
 * Depth-first walk in name order. readdir() returns entries in whatever order
 * the filesystem keeps them, so they are collected and sorted first: a search
 * that reports its hits in a different order on each run is hard to read and
 * impossible to test.
 */
static void walk(seek_ctx_t *ctx, const char *dir, const char *display)
{
    DIR *d = opendir(dir);
    if (!d) return;   /* an unreadable subtree is skipped, not an error */

    svec_t names;
    svec_init(&names);
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (entry->d_name[0] == '.') continue;   /* hidden entries are skipped */
        svec_push_copy(&names, entry->d_name);
    }
    closedir(d);

    qsort(names.items, names.len, sizeof(char *), compare_names);

    for (size_t i = 0; i < names.len; i++) {
        const char *name = names.items[i];

        char full[PATH_MAX], shown[PATH_MAX];
        if (path_join(full, sizeof(full), dir, name) != 0) continue;
        if (path_join(shown, sizeof(shown), display, name) != 0) continue;

        struct stat st;
        if (lstat(full, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);
        if (name_matches(name, ctx->target)) {
            if ((is_dir && !ctx->files_only) || (!is_dir && !ctx->dirs_only))
                report(ctx, shown, is_dir);
        }

        if (is_dir) walk(ctx, full, shown);
    }

    svec_free(&names);
}

/* `-e` acts on a single match: print a file, or move into a directory. */
static int act_on_match(shell_t *sh, seek_ctx_t *ctx)
{
    if (ctx->matched_dirs + ctx->matched_files != 1) {
        fprintf(stderr, "seek: -e needs exactly one match, found %d\n",
                ctx->matched_dirs + ctx->matched_files);
        return 1;
    }

    if (ctx->last_is_dir) {
        if (chdir(ctx->last_match) != 0) {
            fprintf(stderr, "seek: %s: %s\n", ctx->last_match, strerror(errno));
            return 1;
        }
        char now[PATH_MAX];
        if (getcwd(now, sizeof(now))) {
            snprintf(sh->prev_dir, sizeof(sh->prev_dir), "%s", now);
            printf("%s\n", now);
        }
        return 0;
    }

    FILE *f = fopen(ctx->last_match, "r");
    if (!f) {
        fprintf(stderr, "seek: %s: %s\n", ctx->last_match, strerror(errno));
        return 1;
    }

    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) fwrite(chunk, 1, n, stdout);
    fclose(f);
    return 0;
}

int builtin_seek(shell_t *sh, int argc, char **argv)
{
    seek_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.color = isatty(STDOUT_FILENO);

    svec_t operands;
    svec_init(&operands);

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (const char *f = argv[i] + 1; *f; f++) {
                if      (*f == 'd') ctx.dirs_only = 1;
                else if (*f == 'f') ctx.files_only = 1;
                else if (*f == 'e') ctx.act = 1;
                else {
                    fprintf(stderr, "seek: unknown flag `-%c`\n", *f);
                    svec_free(&operands);
                    return 1;
                }
            }
        } else {
            svec_push_copy(&operands, argv[i]);
        }
    }

    if (ctx.dirs_only && ctx.files_only) {
        fprintf(stderr, "seek: -d and -f cannot be used together\n");
        svec_free(&operands);
        return 1;
    }
    if (operands.len < 1 || operands.len > 2) {
        fprintf(stderr, "seek: usage: seek [-d|-f] [-e] <name> [directory]\n");
        svec_free(&operands);
        return 1;
    }

    ctx.target = operands.items[0];
    const char *root = operands.len == 2 ? operands.items[1] : ".";

    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "seek: %s: not a directory\n", root);
        svec_free(&operands);
        return 1;
    }

    walk(&ctx, root, root);

    int status = 0;
    if (ctx.matched_dirs + ctx.matched_files == 0) {
        printf("No match found\n");
        status = 1;
    } else if (ctx.act) {
        status = act_on_match(sh, &ctx);
    }

    svec_free(&operands);
    return status;
}
