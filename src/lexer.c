#include "minishell.h"
#include "lexer.h"
#include "util.h"

#include <ctype.h>

static void tokens_init(tokens_t *t)
{
    t->cap = 16;
    t->len = 0;
    t->items = xmalloc(t->cap * sizeof(token_t));
}

static void tokens_push(tokens_t *t, tok_type_t type, char *text, int quoted)
{
    if (t->len + 1 > t->cap) {
        t->cap *= 2;
        t->items = xrealloc(t->items, t->cap * sizeof(token_t));
    }
    t->items[t->len].type   = type;
    t->items[t->len].text   = text;
    t->items[t->len].quoted = quoted;
    t->len++;
}

void tokens_free(tokens_t *t)
{
    for (size_t i = 0; i < t->len; i++) free(t->items[i].text);
    free(t->items);
    t->items = NULL;
    t->len = t->cap = 0;
}

const char *tok_name(tok_type_t type)
{
    switch (type) {
    case TOK_PIPE:         return "|";
    case TOK_SEMI:         return ";";
    case TOK_AMP:          return "&";
    case TOK_REDIR_IN:     return "<";
    case TOK_REDIR_OUT:    return ">";
    case TOK_REDIR_APPEND: return ">>";
    case TOK_WORD:         return "word";
    default:               return "end of line";
    }
}

static const char *env_lookup(const char *name, void *ctx)
{
    (void)ctx;
    return getenv(name);
}

static int is_operator_char(char c)
{
    return c == '|' || c == ';' || c == '&' || c == '<' || c == '>';
}

static int is_name_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/*
 * Handle a `$` at line[*i]. Appends the substitution to `word` and advances
 * `*i` past the reference. A `$` that starts nothing recognisable stays literal.
 */
static void expand_variable(const char *line, size_t *i, sbuf_t *word,
                            var_lookup_fn lookup, void *ctx)
{
    size_t p = *i + 1; /* skip '$' */
    char name[128];

    if (line[p] == '{') {
        size_t start = ++p;
        while (line[p] && line[p] != '}') p++;
        if (line[p] != '}' || p - start == 0 || p - start >= sizeof(name)) {
            sbuf_putc(word, '$'); /* not a usable reference: keep it literal */
            (*i)++;
            return;
        }
        memcpy(name, line + start, p - start);
        name[p - start] = '\0';
        p++;
    } else if (line[p] == '?' || line[p] == '$') {
        name[0] = line[p];
        name[1] = '\0';
        p++;
    } else if (isalpha((unsigned char)line[p]) || line[p] == '_') {
        size_t start = p;
        while (line[p] && is_name_char(line[p])) p++;
        if (p - start >= sizeof(name)) {
            sbuf_putc(word, '$');
            (*i)++;
            return;
        }
        memcpy(name, line + start, p - start);
        name[p - start] = '\0';
    } else {
        sbuf_putc(word, '$');
        (*i)++;
        return;
    }

    const char *value = lookup(name, ctx);
    if (value) sbuf_puts(word, value);
    *i = p;
}

static int lex_impl(const char *line, tokens_t *out, const char **err,
                    var_lookup_fn lookup, void *ctx, const char **rest)
{
    tokens_init(out);
    if (rest) *rest = NULL;
    *err = NULL;
    if (!lookup) lookup = env_lookup;

    size_t i = 0;
    while (line[i]) {
        if (isspace((unsigned char)line[i])) {
            i++;
            continue;
        }

        if (is_operator_char(line[i])) {
            switch (line[i]) {
            case '|': tokens_push(out, TOK_PIPE, NULL, 0); i++; break;
            case ';':
            case '&':
                tokens_push(out, line[i] == ';' ? TOK_SEMI : TOK_AMP, NULL, 0);
                i++;
                if (rest) {                 /* one segment per call */
                    if (line[i]) *rest = line + i;
                    tokens_push(out, TOK_EOF, NULL, 0);
                    return 0;
                }
                break;
            case '<': tokens_push(out, TOK_REDIR_IN, NULL, 0); i++; break;
            default:
                if (line[i + 1] == '>') {
                    tokens_push(out, TOK_REDIR_APPEND, NULL, 0);
                    i += 2;
                } else {
                    tokens_push(out, TOK_REDIR_OUT, NULL, 0);
                    i++;
                }
                break;
            }
            continue;
        }

        /* A word runs until unquoted whitespace or an unquoted operator. */
        sbuf_t word;
        sbuf_init(&word);
        int quoted = 0;
        const char *fail = NULL;

        while (line[i] && !isspace((unsigned char)line[i]) && !is_operator_char(line[i])) {
            char c = line[i];

            if (c == '\'') {
                quoted = 1;
                i++;
                while (line[i] && line[i] != '\'') sbuf_putc(&word, line[i++]);
                if (line[i] != '\'') { fail = "unterminated single quote"; break; }
                i++;
            } else if (c == '"') {
                quoted = 1;
                i++;
                while (line[i] && line[i] != '"') {
                    if (line[i] == '\\' && line[i + 1] && strchr("\"\\$`", line[i + 1])) {
                        sbuf_putc(&word, line[i + 1]);
                        i += 2;
                    } else if (line[i] == '$') {
                        expand_variable(line, &i, &word, lookup, ctx);
                    } else {
                        sbuf_putc(&word, line[i++]);
                    }
                }
                if (line[i] != '"') { fail = "unterminated double quote"; break; }
                i++;
            } else if (c == '\\') {
                if (!line[i + 1]) { fail = "trailing backslash"; break; }
                quoted = 1;
                sbuf_putc(&word, line[i + 1]);
                i += 2;
            } else if (c == '$') {
                expand_variable(line, &i, &word, lookup, ctx);
            } else {
                sbuf_putc(&word, c);
                i++;
            }
        }

        if (fail) {
            sbuf_free(&word);
            tokens_free(out);
            tokens_init(out);
            tokens_push(out, TOK_EOF, NULL, 0);
            *err = fail;
            return -1;
        }

        tokens_push(out, TOK_WORD, sbuf_release(&word), quoted);
    }

    tokens_push(out, TOK_EOF, NULL, 0);
    return 0;
}

int lex_line_ex(const char *line, tokens_t *out, const char **err,
                var_lookup_fn lookup, void *ctx)
{
    return lex_impl(line, out, err, lookup, ctx, NULL);
}

int lex_segment(const char *line, tokens_t *out, const char **err,
                var_lookup_fn lookup, void *ctx, const char **rest)
{
    return lex_impl(line, out, err, lookup, ctx, rest);
}

int lex_line(const char *line, tokens_t *out, const char **err)
{
    return lex_impl(line, out, err, env_lookup, NULL, NULL);
}
