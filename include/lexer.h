/*
 * lexer.h - turns a raw input line into a token stream.
 *
 * The lexer owns all quoting rules. It removes quotes, resolves backslash
 * escapes and substitutes variables, because only the scanner knows which
 * quoting context a character sits in ($HOME expands inside double quotes but
 * not inside single quotes). It records on each word whether any character was
 * quoted, so that a quoted word is not later tilde-expanded or globbed.
 */
#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    TOK_EOF = 0,
    TOK_WORD,
    TOK_PIPE,           /* |  */
    TOK_SEMI,           /* ;  */
    TOK_AMP,            /* &  */
    TOK_REDIR_IN,       /* <  */
    TOK_REDIR_OUT,      /* >  */
    TOK_REDIR_APPEND    /* >> */
} tok_type_t;

typedef struct {
    tok_type_t type;
    char      *text;    /* owned, words only */
    int        quoted;  /* word contained a quote or escape */
} token_t;

typedef struct {
    token_t *items;
    size_t   len;
    size_t   cap;
} tokens_t;

/*
 * Resolve `$name` during scanning. Return NULL for an unset name, which the
 * lexer substitutes as the empty string. `name` is "?" for the exit status.
 */
typedef const char *(*var_lookup_fn)(const char *name, void *ctx);

/*
 * Tokenise `line`. On success returns 0 and fills `out` (always terminated by a
 * TOK_EOF entry). On a lexical error - an unterminated quote or a trailing
 * backslash - returns -1, leaves `out` holding only TOK_EOF, and points `*err`
 * at a static message.
 *
 * lex_line() looks variables up in the environment; lex_line_ex() lets the shell
 * add `$?` and friends, and lets the tests supply a fixture.
 *
 * lex_segment() stops after the first top-level `;` or `&` and points `*rest`
 * at what follows (NULL at the end of the line). The shell scans one segment at
 * a time so that expansion happens after the previous segment has run, which is
 * what makes `false ; echo $?` report 1 rather than the status from before.
 */
int  lex_line(const char *line, tokens_t *out, const char **err);
int  lex_line_ex(const char *line, tokens_t *out, const char **err,
                 var_lookup_fn lookup, void *ctx);
int  lex_segment(const char *line, tokens_t *out, const char **err,
                 var_lookup_fn lookup, void *ctx, const char **rest);
void tokens_free(tokens_t *t);

/* Human-readable operator name, for parser error messages. */
const char *tok_name(tok_type_t type);

#endif /* LEXER_H */
