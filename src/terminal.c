#include "minishell.h"
#include "terminal.h"
#include "builtins.h"
#include "history.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>

static struct termios saved_termios;
static int            termios_valid;
static int            raw_active;

void terminal_init(shell_t *sh)
{
    if (!sh->interactive) return;
    if (tcgetattr(STDIN_FILENO, &saved_termios) == 0) termios_valid = 1;
    atexit(terminal_restore);
}

void terminal_restore(void)
{
    if (termios_valid && raw_active) {
        /* TCSADRAIN, not TCSAFLUSH: anything already typed belongs to whatever
         * runs next and must not be thrown away. */
        tcsetattr(STDIN_FILENO, TCSADRAIN, &saved_termios);
        raw_active = 0;
    }
}

static int raw_enable(void)
{
    if (!termios_valid) return -1;

    struct termios raw = saved_termios;
    /* ISIG off: Ctrl-C and Ctrl-Z arrive as ordinary bytes while we are editing,
     * so the editor can act on them without a signal handler. OPOST stays on so
     * that a written "\n" still produces a carriage return. */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    /* TCSADRAIN keeps input typed while the previous command was running; with
     * TCSAFLUSH the terminal would discard it and the line would be lost. */
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) != 0) return -1;
    raw_active = 1;
    return 0;
}

/* ------------------------------------------------------------ plumbing --- */

static void out_str(const char *s)
{
    size_t n = strlen(s);
    ssize_t written = 0;
    while ((size_t)written < n) {
        ssize_t w = write(STDOUT_FILENO, s + written, n - written);
        if (w <= 0) return;
        written += w;
    }
}

static int terminal_columns(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

typedef struct {
    sbuf_t      buf;
    size_t      pos;          /* cursor offset into buf.data */
    const char *prompt;
    size_t      prompt_width;
    int         history_index; /* == history_count() means "the draft" */
    char       *draft;
} editor_t;

/*
 * Repaint the input line. Long lines scroll horizontally instead of wrapping,
 * which keeps the repaint to a single row and avoids guessing where the
 * terminal decided to break.
 */
static void editor_refresh(editor_t *e)
{
    int cols = terminal_columns();
    size_t start = 0;
    size_t visible = e->buf.len;

    while (e->prompt_width + (e->pos - start) >= (size_t)cols) start++;
    while (e->prompt_width + (visible - start) > (size_t)cols - 1 && visible > start) visible--;

    sbuf_t line;
    sbuf_init(&line);
    sbuf_puts(&line, "\r");
    sbuf_puts(&line, e->prompt);
    sbuf_putn(&line, e->buf.data + start, visible - start);
    sbuf_puts(&line, "\033[0K");            /* erase whatever the old line left */

    char move[32];
    snprintf(move, sizeof(move), "\r\033[%zuC", e->prompt_width + (e->pos - start));
    sbuf_puts(&line, move);

    out_str(line.data);
    sbuf_free(&line);
}

static void editor_insert(editor_t *e, char c)
{
    sbuf_putc(&e->buf, c);                  /* grow by one, then shift right */
    memmove(e->buf.data + e->pos + 1, e->buf.data + e->pos, e->buf.len - e->pos - 1);
    e->buf.data[e->pos++] = c;
}

static void editor_insert_str(editor_t *e, const char *s)
{
    for (const char *p = s; *p; p++) editor_insert(e, *p);
}

static void editor_delete_at(editor_t *e, size_t index)
{
    if (index >= e->buf.len) return;
    memmove(e->buf.data + index, e->buf.data + index + 1, e->buf.len - index - 1);
    e->buf.len--;
    e->buf.data[e->buf.len] = '\0';
}

static void editor_set(editor_t *e, const char *text)
{
    sbuf_clear(&e->buf);
    sbuf_puts(&e->buf, text);
    e->pos = e->buf.len;
}

/* ------------------------------------------------------------- history --- */

static void editor_history(editor_t *e, int delta)
{
    int total = history_count();
    if (total == 0) return;

    if (e->history_index == total) {
        free(e->draft);
        e->draft = xstrdup(e->buf.data);
    }

    int next = e->history_index + delta;
    if (next < 0) next = 0;
    if (next > total) next = total;
    if (next == e->history_index) return;

    e->history_index = next;
    editor_set(e, next == total ? (e->draft ? e->draft : "") : history_get(next));
}

/* ---------------------------------------------------------- completion --- */

static int is_executable(const char *dir, const char *name)
{
    char full[PATH_MAX];
    if (path_join(full, sizeof(full), dir, name) != 0) return 0;

    struct stat st;
    return stat(full, &st) == 0 && S_ISREG(st.st_mode) && access(full, X_OK) == 0;
}

static void collect_commands(const char *prefix, svec_t *out)
{
    size_t nbuiltins;
    const builtin_t *table = builtin_table(&nbuiltins);
    for (size_t i = 0; i < nbuiltins; i++)
        if (str_has_prefix(table[i].name, prefix)) svec_push_copy(out, table[i].name);

    const char *path = getenv("PATH");
    if (!path) return;

    char *copy = xstrdup(path);
    for (char *dir = strtok(copy, ":"); dir; dir = strtok(NULL, ":")) {
        DIR *d = opendir(*dir ? dir : ".");
        if (!d) continue;
        struct dirent *entry;
        while ((entry = readdir(d))) {
            if (entry->d_name[0] == '.') continue;
            if (!str_has_prefix(entry->d_name, prefix)) continue;
            if (is_executable(*dir ? dir : ".", entry->d_name))
                svec_push_copy(out, entry->d_name);
        }
        closedir(d);
    }
    free(copy);
}

/*
 * Complete a filename. `word` is the partial path as typed; every candidate is
 * returned as a full replacement for it, with a `/` appended for directories.
 */
static void collect_paths(shell_t *sh, const char *word, svec_t *out)
{
    char expanded[PATH_MAX];
    if (word[0] == '~' && (word[1] == '/' || word[1] == '\0')) {
        if (strlen(sh->home) + strlen(word) >= sizeof(expanded)) return;
        strcpy(expanded, sh->home);
        strcat(expanded, word + 1);
    } else {
        if (strlen(word) >= sizeof(expanded)) return;
        strcpy(expanded, word);
    }

    const char *slash = strrchr(expanded, '/');
    char dir[PATH_MAX];
    const char *prefix;

    if (slash) {
        size_t dlen = (size_t)(slash - expanded) + 1; /* keep the slash */
        if (dlen >= sizeof(dir)) return;
        memcpy(dir, expanded, dlen);
        dir[dlen] = '\0';
        prefix = slash + 1;
    } else {
        strcpy(dir, "./");
        prefix = expanded;
    }

    DIR *d = opendir(dir);
    if (!d) return;

    /* Candidates are rebuilt from the text the user typed, not from `expanded`,
     * so an entered `~/` stays a `~/` on the line. */
    char typed_dir[PATH_MAX];
    const char *typed_slash = strrchr(word, '/');
    if (typed_slash) {
        size_t dlen = (size_t)(typed_slash - word) + 1;
        memcpy(typed_dir, word, dlen);
        typed_dir[dlen] = '\0';
    } else {
        typed_dir[0] = '\0';
    }

    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (prefix[0] != '.' && entry->d_name[0] == '.') continue;
        if (!str_has_prefix(entry->d_name, prefix)) continue;

        char full[PATH_MAX];
        struct stat st;
        int is_dir = path_join(full, sizeof(full), dir, entry->d_name) == 0 &&
                     stat(full, &st) == 0 && S_ISDIR(st.st_mode);

        sbuf_t candidate;
        sbuf_init(&candidate);
        sbuf_puts(&candidate, typed_dir);
        sbuf_puts(&candidate, entry->d_name);
        if (is_dir) sbuf_putc(&candidate, '/');
        svec_push(out, sbuf_release(&candidate));
    }
    closedir(d);
}

static int compare_strings(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Longest prefix shared by every candidate. */
static size_t common_prefix_len(svec_t *v)
{
    if (v->len == 0) return 0;

    size_t n = strlen(v->items[0]);
    for (size_t i = 1; i < v->len; i++) {
        size_t k = 0;
        while (k < n && v->items[i][k] && v->items[i][k] == v->items[0][k]) k++;
        n = k;
    }
    return n;
}

static void editor_complete(shell_t *sh, editor_t *e)
{
    /* The word under the cursor runs back to the last unquoted separator. */
    size_t start = e->pos;
    while (start > 0 && !strchr(" \t|;&<>", e->buf.data[start - 1])) start--;

    char word[PATH_MAX];
    size_t wlen = e->pos - start;
    if (wlen >= sizeof(word)) return;
    memcpy(word, e->buf.data + start, wlen);
    word[wlen] = '\0';

    /* First word of a command position completes command names. */
    size_t before = start;
    while (before > 0 && (e->buf.data[before - 1] == ' ' || e->buf.data[before - 1] == '\t'))
        before--;
    int command_position = (before == 0) || strchr("|;&", e->buf.data[before - 1]) != NULL;

    svec_t matches;
    svec_init(&matches);
    if (command_position && !strchr(word, '/'))
        collect_commands(word, &matches);
    else
        collect_paths(sh, word, &matches);

    if (matches.len == 0) {
        svec_free(&matches);
        return;
    }

    qsort(matches.items, matches.len, sizeof(char *), compare_strings);

    /* Drop duplicates, which PATH directories produce readily. */
    size_t unique = 1;
    for (size_t i = 1; i < matches.len; i++) {
        if (strcmp(matches.items[i], matches.items[unique - 1]) == 0) {
            free(matches.items[i]);
            continue;
        }
        matches.items[unique++] = matches.items[i];
    }
    matches.len = unique;
    matches.items[unique] = NULL;

    size_t shared = common_prefix_len(&matches);
    if (shared > wlen && shared < PATH_MAX) {
        /* Extend the word by as much as every candidate agrees on. */
        for (size_t i = 0; i < wlen; i++) editor_delete_at(e, start);
        e->pos = start;
        char stem[PATH_MAX];
        memcpy(stem, matches.items[0], shared);
        stem[shared] = '\0';
        editor_insert_str(e, stem);
    }

    if (matches.len == 1) {
        char last = matches.items[0][strlen(matches.items[0]) - 1];
        if (last != '/') editor_insert(e, ' ');
    } else if (shared <= wlen) {
        out_str("\r\n");
        for (size_t i = 0; i < matches.len; i++) {
            out_str(matches.items[i]);
            out_str("  ");
        }
        out_str("\r\n");
    }

    svec_free(&matches);
    editor_refresh(e);
}

/* ---------------------------------------------------------- key reading --- */

static int read_byte(char *c)
{
    while (1) {
        ssize_t n = read(STDIN_FILENO, c, 1);
        if (n == 1) return 1;
        if (n == 0) return 0;
        if (errno != EINTR) return -1;
    }
}

/* Decode the tail of an escape sequence into one of these pseudo-keys. */
enum {
    KEY_UP = 1000, KEY_DOWN, KEY_RIGHT, KEY_LEFT,
    KEY_HOME, KEY_END, KEY_DELETE, KEY_UNKNOWN
};

static int read_escape(void)
{
    char a, b;
    if (read_byte(&a) != 1) return KEY_UNKNOWN;

    if (a == '[') {
        if (read_byte(&b) != 1) return KEY_UNKNOWN;
        if (b >= '0' && b <= '9') {
            char tilde;
            if (read_byte(&tilde) != 1 || tilde != '~') return KEY_UNKNOWN;
            switch (b) {
            case '1': case '7': return KEY_HOME;
            case '3':           return KEY_DELETE;
            case '4': case '8': return KEY_END;
            default:            return KEY_UNKNOWN;
            }
        }
        switch (b) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        default:  return KEY_UNKNOWN;
        }
    }
    if (a == 'O') {
        if (read_byte(&b) != 1) return KEY_UNKNOWN;
        if (b == 'H') return KEY_HOME;
        if (b == 'F') return KEY_END;
    }
    return KEY_UNKNOWN;
}

/* --------------------------------------------------------------- entry --- */

/* Plain, unedited input: a pipe, a file, or a terminal we could not switch. */
static char *read_plain_line(shell_t *sh, const char *prompt)
{
    if (sh->interactive) {
        fputs(prompt, stdout);
        fflush(stdout);
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
        free(line);
        return NULL;
    }
    if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';
    return line;
}

char *line_read(shell_t *sh, const char *prompt, size_t prompt_width)
{
    if (!sh->interactive || raw_enable() != 0)
        return read_plain_line(sh, prompt);

    editor_t e;
    memset(&e, 0, sizeof(e));
    sbuf_init(&e.buf);
    e.prompt        = prompt;
    e.prompt_width  = prompt_width;
    e.history_index = history_count();

    editor_refresh(&e);

    char *result = NULL;
    while (1) {
        char c;
        int r = read_byte(&c);
        if (r <= 0) {                       /* end of input or a fatal error */
            if (e.buf.len > 0) { result = sbuf_release(&e.buf); break; }
            result = NULL;
            break;
        }

        if (c == '\r' || c == '\n') {
            out_str("\r\n");
            result = sbuf_release(&e.buf);
            break;
        }

        if (c == 3) {                       /* Ctrl-C: abandon the line */
            out_str("^C\r\n");
            sbuf_clear(&e.buf);
            result = sbuf_release(&e.buf);
            break;
        }

        if (c == 4) {                       /* Ctrl-D */
            if (e.buf.len == 0) { result = NULL; break; }
            editor_delete_at(&e, e.pos);
            editor_refresh(&e);
            continue;
        }

        if (c == 9) { editor_complete(sh, &e); continue; }

        if (c == 127 || c == 8) {           /* backspace */
            if (e.pos > 0) { editor_delete_at(&e, --e.pos); editor_refresh(&e); }
            continue;
        }

        if (c == 27) {
            switch (read_escape()) {
            case KEY_LEFT:   if (e.pos > 0) e.pos--; break;
            case KEY_RIGHT:  if (e.pos < e.buf.len) e.pos++; break;
            case KEY_HOME:   e.pos = 0; break;
            case KEY_END:    e.pos = e.buf.len; break;
            case KEY_UP:     editor_history(&e, -1); break;
            case KEY_DOWN:   editor_history(&e, +1); break;
            case KEY_DELETE: editor_delete_at(&e, e.pos); break;
            default: break;
            }
            editor_refresh(&e);
            continue;
        }

        switch (c) {
        case 1:  e.pos = 0; break;                                  /* Ctrl-A */
        case 5:  e.pos = e.buf.len; break;                          /* Ctrl-E */
        case 2:  if (e.pos > 0) e.pos--; break;                     /* Ctrl-B */
        case 6:  if (e.pos < e.buf.len) e.pos++; break;             /* Ctrl-F */
        case 21:                                                    /* Ctrl-U */
            while (e.pos > 0) editor_delete_at(&e, --e.pos);
            break;
        case 11:                                                    /* Ctrl-K */
            while (e.buf.len > e.pos) editor_delete_at(&e, e.pos);
            break;
        case 23:                                                    /* Ctrl-W */
            while (e.pos > 0 && isspace((unsigned char)e.buf.data[e.pos - 1]))
                editor_delete_at(&e, --e.pos);
            while (e.pos > 0 && !isspace((unsigned char)e.buf.data[e.pos - 1]))
                editor_delete_at(&e, --e.pos);
            break;
        case 12:                                                    /* Ctrl-L */
            out_str("\033[H\033[2J");
            break;
        default:
            if (!iscntrl((unsigned char)c)) editor_insert(&e, c);
            break;
        }
        editor_refresh(&e);
    }

    terminal_restore();
    free(e.draft);
    sbuf_free(&e.buf);
    return result;
}
