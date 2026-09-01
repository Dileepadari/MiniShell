/*
 * peek.c - list directory contents.
 *
 * Flags follow ls: -a includes entries whose name starts with a dot, -l prints
 * the long form, and the two may be clustered (-la) or given separately.
 */
#include "minishell.h"
#include "builtins.h"
#include "util.h"

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <time.h>

#define COLOR_DIR   "\033[1;34m"
#define COLOR_EXEC  "\033[1;32m"
#define COLOR_LINK  "\033[1;36m"
#define COLOR_RESET "\033[0m"

typedef struct {
    int all;
    int long_format;
} peek_flags_t;

static int use_color;

static const char *color_for(const struct stat *st)
{
    if (!use_color) return NULL;
    if (S_ISLNK(st->st_mode)) return COLOR_LINK;
    if (S_ISDIR(st->st_mode)) return COLOR_DIR;
    if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return COLOR_EXEC;
    return NULL;
}

static void print_name(const char *name, const struct stat *st)
{
    const char *color = color_for(st);
    if (color) printf("%s%s%s", color, name, COLOR_RESET);
    else       printf("%s", name);
}

static void format_mode(char out[11], mode_t mode)
{
    out[0] = S_ISDIR(mode)  ? 'd' :
             S_ISLNK(mode)  ? 'l' :
             S_ISCHR(mode)  ? 'c' :
             S_ISBLK(mode)  ? 'b' :
             S_ISFIFO(mode) ? 'p' :
             S_ISSOCK(mode) ? 's' : '-';
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';

    if (mode & S_ISUID) out[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) out[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) out[9] = (mode & S_IXOTH) ? 't' : 'T';
}

/* A file older than six months shows its year instead of the time, as ls does. */
static void format_time(char *out, size_t n, time_t when)
{
    time_t now = time(NULL);
    const char *format = (now - when > 15552000 || when - now > 15552000)
                       ? "%b %e  %Y" : "%b %e %H:%M";

    struct tm tm;
    if (localtime_r(&when, &tm)) strftime(out, n, format, &tm);
    else                         snprintf(out, n, "?");
}

static void print_long(const char *dir, const char *name, const struct stat *st)
{
    char mode[11];
    format_mode(mode, st->st_mode);

    struct passwd *pw = getpwuid(st->st_uid);
    struct group  *gr = getgrgid(st->st_gid);
    char owner[64], group[64];
    if (pw) snprintf(owner, sizeof(owner), "%s", pw->pw_name);
    else    snprintf(owner, sizeof(owner), "%u", (unsigned)st->st_uid);
    if (gr) snprintf(group, sizeof(group), "%s", gr->gr_name);
    else    snprintf(group, sizeof(group), "%u", (unsigned)st->st_gid);

    char when[64];
    format_time(when, sizeof(when), st->st_mtime);

    printf("%s %3lu %-8s %-8s %8lld %s ", mode, (unsigned long)st->st_nlink,
           owner, group, (long long)st->st_size, when);
    print_name(name, st);

    if (S_ISLNK(st->st_mode)) {
        char target[PATH_MAX];
        char full[PATH_MAX];
        if (path_join(full, sizeof(full), dir, name) == 0) {
            ssize_t len = readlink(full, target, sizeof(target) - 1);
            if (len > 0) {
                target[len] = '\0';
                printf(" -> %s", target);
            }
        }
    }
    printf("\n");
}

static int terminal_width(void)
{
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

/* Short format: one entry per line off a terminal, packed columns on one. */
static void print_columns(const char *dir, svec_t *names, struct stat *stats)
{
    if (!isatty(STDOUT_FILENO)) {
        for (size_t i = 0; i < names->len; i++) {
            print_name(names->items[i], &stats[i]);
            printf("\n");
        }
        return;
    }

    size_t widest = 0;
    for (size_t i = 0; i < names->len; i++) {
        size_t len = strlen(names->items[i]);
        if (len > widest) widest = len;
    }

    size_t column_width = widest + 2;
    size_t columns = (size_t)terminal_width() / column_width;
    if (columns == 0) columns = 1;

    size_t rows = (names->len + columns - 1) / columns;
    for (size_t r = 0; r < rows; r++) {
        for (size_t c = 0; c < columns; c++) {
            size_t i = c * rows + r;   /* fill down, like ls */
            if (i >= names->len) continue;
            print_name(names->items[i], &stats[i]);
            if (c + 1 < columns && i + rows < names->len)
                printf("%*s", (int)(column_width - strlen(names->items[i])), "");
        }
        printf("\n");
    }
    (void)dir;
}

static int compare_names(const void *a, const void *b)
{
    return strcoll(*(char *const *)a, *(char *const *)b);
}

static int list_directory(const char *path, peek_flags_t flags)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "peek: %s: %s\n", path, strerror(errno));
        return 1;
    }

    svec_t names;
    svec_init(&names);
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (!flags.all && entry->d_name[0] == '.') continue;
        svec_push_copy(&names, entry->d_name);
    }
    closedir(d);

    qsort(names.items, names.len, sizeof(char *), compare_names);

    struct stat *stats = xcalloc(names.len ? names.len : 1, sizeof(struct stat));
    long long blocks = 0;
    for (size_t i = 0; i < names.len; i++) {
        char full[PATH_MAX];
        if (path_join(full, sizeof(full), path, names.items[i]) == 0 &&
            lstat(full, &stats[i]) == 0)
            blocks += stats[i].st_blocks;
    }

    if (flags.long_format) {
        printf("total %lld\n", blocks / 2);
        for (size_t i = 0; i < names.len; i++)
            print_long(path, names.items[i], &stats[i]);
    } else {
        print_columns(path, &names, stats);
    }

    free(stats);
    svec_free(&names);
    return 0;
}

int builtin_peek(shell_t *sh, int argc, char **argv)
{
    (void)sh;

    peek_flags_t flags = {0};
    svec_t operands;
    svec_init(&operands);
    use_color = isatty(STDOUT_FILENO);

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (const char *f = argv[i] + 1; *f; f++) {
                if      (*f == 'a') flags.all = 1;
                else if (*f == 'l') flags.long_format = 1;
                else {
                    fprintf(stderr, "peek: unknown flag `-%c`\n", *f);
                    svec_free(&operands);
                    return 1;
                }
            }
        } else {
            svec_push_copy(&operands, argv[i]);
        }
    }

    if (operands.len == 0) svec_push_copy(&operands, ".");

    int status = 0;
    for (size_t i = 0; i < operands.len; i++) {
        const char *path = operands.items[i];

        struct stat st;
        if (lstat(path, &st) != 0) {
            fprintf(stderr, "peek: %s: %s\n", path, strerror(errno));
            status = 1;
            continue;
        }

        /* A plain file operand is reported on its own, not listed into. */
        if (!S_ISDIR(st.st_mode)) {
            if (flags.long_format) print_long(".", path, &st);
            else { print_name(path, &st); printf("\n"); }
            continue;
        }

        if (operands.len > 1) printf("%s%s:\n", i ? "\n" : "", path);
        if (list_directory(path, flags) != 0) status = 1;
    }

    svec_free(&operands);
    return status;
}
