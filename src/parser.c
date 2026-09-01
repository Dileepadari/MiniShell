#include "minishell.h"
#include "parser.h"
#include "util.h"

#include <glob.h>
#include <pwd.h>

/* ------------------------------------------------------------ expansion --- */

/*
 * Replace a leading `~` with the shell's home, or `~user` with that user's home.
 * Returns a newly allocated string; the input is left alone.
 */
static char *expand_tilde(const char *word, const char *home)
{
    if (word[0] != '~') return xstrdup(word);

    /* "~" or "~/..." -> the shell's home. */
    if (word[1] == '\0' || word[1] == '/') {
        sbuf_t b;
        sbuf_init(&b);
        sbuf_puts(&b, home);
        sbuf_puts(&b, word + 1);
        return sbuf_release(&b);
    }

    /* "~user" or "~user/..." -> that account's home, if it exists. */
    const char *slash = strchr(word + 1, '/');
    size_t name_len = slash ? (size_t)(slash - word - 1) : strlen(word + 1);
    char name[128];
    if (name_len >= sizeof(name)) return xstrdup(word);
    memcpy(name, word + 1, name_len);
    name[name_len] = '\0';

    struct passwd *pw = getpwnam(name);
    if (!pw) return xstrdup(word);

    sbuf_t b;
    sbuf_init(&b);
    sbuf_puts(&b, pw->pw_dir);
    if (slash) sbuf_puts(&b, slash);
    return sbuf_release(&b);
}

static int has_glob_chars(const char *s)
{
    return strpbrk(s, "*?[") != NULL;
}

/*
 * Expand one word into zero or more arguments appended to `out`.
 * A quoted word is taken literally. An unquoted pattern that matches nothing is
 * also kept literally, which is what a user typing `peek *.md` in an empty
 * directory expects to see reported back at them.
 */
static void expand_word(const token_t *tok, const char *home, svec_t *out)
{
    char *word = tok->quoted ? xstrdup(tok->text) : expand_tilde(tok->text, home);

    if (tok->quoted || !has_glob_chars(word)) {
        svec_push(out, word);
        return;
    }

    glob_t g;
    memset(&g, 0, sizeof(g));
    if (glob(word, GLOB_TILDE, NULL, &g) == 0 && g.gl_pathc > 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) svec_push_copy(out, g.gl_pathv[i]);
        free(word);
    } else {
        svec_push(out, word);
    }
    globfree(&g);
}

/* --------------------------------------------------------------- build --- */

typedef struct {
    command_t *cmds;
    int        ncmds;
    int        cap;
} cmdlist_t;

static command_t *cmdlist_open(cmdlist_t *l)
{
    if (l->ncmds + 1 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->cmds = xrealloc(l->cmds, l->cap * sizeof(command_t));
    }
    command_t *c = &l->cmds[l->ncmds++];
    memset(c, 0, sizeof(*c));
    return c;
}

static void command_free(command_t *c)
{
    if (c->argv) {
        for (int i = 0; c->argv[i]; i++) free(c->argv[i]);
        free(c->argv);
    }
    free(c->infile);
    free(c->outfile);
}

void pipeline_free(pipeline_t *p)
{
    while (p) {
        pipeline_t *next = p->next;
        for (int i = 0; i < p->ncmds; i++) command_free(&p->cmds[i]);
        free(p->cmds);
        free(p->text);
        free(p);
        p = next;
    }
}

/* Reconstruct a printable form of the pipeline, for the job table. */
static char *pipeline_text(const command_t *cmds, int ncmds)
{
    sbuf_t b;
    sbuf_init(&b);
    for (int i = 0; i < ncmds; i++) {
        if (i > 0) sbuf_puts(&b, " | ");
        for (int j = 0; j < cmds[i].argc; j++) {
            if (j > 0) sbuf_putc(&b, ' ');
            sbuf_puts(&b, cmds[i].argv[j]);
        }
    }
    return sbuf_release(&b);
}

int parse_tokens(tokens_t *toks, const char *home, pipeline_t **out, const char **err)
{
    *out = NULL;
    *err = NULL;

    pipeline_t *head = NULL, *tail = NULL;
    cmdlist_t list = {0};
    svec_t argv;
    svec_init(&argv);
    command_t *cur = cmdlist_open(&list);

    /* Close the current command: move the collected words into its argv. */
    #define FINISH_COMMAND()                                   \
        do {                                                   \
            cur->argc = (int)argv.len;                         \
            cur->argv = svec_release(&argv);                   \
            svec_init(&argv);                                  \
        } while (0)

    #define FAIL(msg)                                          \
        do {                                                   \
            *err = (msg);                                      \
            FINISH_COMMAND();                                  \
            for (int k = 0; k < list.ncmds; k++) command_free(&list.cmds[k]); \
            free(list.cmds);                                   \
            svec_free(&argv);                                  \
            pipeline_free(head);                               \
            *out = NULL;                                       \
            return -1;                                         \
        } while (0)

    for (size_t i = 0; i < toks->len; i++) {
        token_t *t = &toks->items[i];

        switch (t->type) {
        case TOK_WORD:
            expand_word(t, home, &argv);
            break;

        case TOK_REDIR_IN:
        case TOK_REDIR_OUT:
        case TOK_REDIR_APPEND: {
            token_t *file = (i + 1 < toks->len) ? &toks->items[i + 1] : NULL;
            if (!file || file->type != TOK_WORD)
                FAIL("expected a file name after a redirection operator");

            svec_t expanded;
            svec_init(&expanded);
            expand_word(file, home, &expanded);
            if (expanded.len != 1) {
                svec_free(&expanded);
                FAIL("redirection target must name exactly one file");
            }
            char *name = expanded.items[0];
            free(expanded.items);

            if (t->type == TOK_REDIR_IN) {
                free(cur->infile);
                cur->infile = name;
            } else {
                free(cur->outfile);
                cur->outfile = name;
                cur->append = (t->type == TOK_REDIR_APPEND);
            }
            i++; /* consume the file name */
            break;
        }

        case TOK_PIPE:
            if (argv.len == 0) FAIL("missing command around `|`");
            FINISH_COMMAND();
            cur = cmdlist_open(&list);
            break;

        case TOK_SEMI:
        case TOK_AMP:
        case TOK_EOF: {
            int background = (t->type == TOK_AMP);

            if (argv.len == 0 && list.ncmds == 1 && !list.cmds[0].infile &&
                !list.cmds[0].outfile) {
                /* Nothing accumulated: an empty segment such as ";;" or a
                 * blank line. Reset and carry on. */
                if (t->type == TOK_EOF) goto done;
                list.ncmds = 0;
                cur = cmdlist_open(&list);
                break;
            }
            if (argv.len == 0)
                FAIL(list.ncmds > 1 ? "missing command after `|`" : "missing command");

            FINISH_COMMAND();

            pipeline_t *p = xcalloc(1, sizeof(pipeline_t));
            p->cmds       = list.cmds;
            p->ncmds      = list.ncmds;
            p->background = background;
            p->text       = pipeline_text(list.cmds, list.ncmds);
            if (tail) tail->next = p; else head = p;
            tail = p;

            memset(&list, 0, sizeof(list));
            if (t->type == TOK_EOF) goto done;
            cur = cmdlist_open(&list);
            break;
        }
        }
    }

done:
    for (int k = 0; k < list.ncmds; k++) command_free(&list.cmds[k]);
    free(list.cmds);
    svec_free(&argv);
    *out = head;
    return 0;

    #undef FINISH_COMMAND
    #undef FAIL
}

int parse_line(const char *line, const char *home, pipeline_t **out, const char **err)
{
    tokens_t toks;
    if (lex_line(line, &toks, err) != 0) {
        tokens_free(&toks);
        *out = NULL;
        return -1;
    }
    int rc = parse_tokens(&toks, home, out, err);
    tokens_free(&toks);
    return rc;
}
