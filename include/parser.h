/*
 * parser.h - builds the command tree from a token stream.
 *
 * Grammar:
 *   line     := pipeline ( (';' | '&') pipeline )* [ ';' | '&' ]
 *   pipeline := command ( '|' command )*
 *   command  := ( WORD | redirection )+
 *
 * Tilde expansion and globbing happen here, once quoting is already resolved,
 * because a single unquoted word can expand into several arguments.
 */
#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

typedef struct command {
    char **argv;      /* NULL-terminated */
    int    argc;
    char  *infile;    /* `< file`, or NULL */
    char  *outfile;   /* `> file` or `>> file`, or NULL */
    int    append;    /* outfile opened with O_APPEND */
} command_t;

typedef struct pipeline {
    command_t        *cmds;
    int               ncmds;
    int               background;
    char             *text;   /* reconstructed text, shown by `activities` */
    struct pipeline  *next;
} pipeline_t;

/*
 * Parse an already-tokenised line. `home` is the shell's home, used for `~`.
 * Returns 0 and sets `*out` (possibly to NULL for a blank line) on success;
 * returns -1 and sets `*err` to a static message on a syntax error.
 */
int  parse_tokens(tokens_t *toks, const char *home, pipeline_t **out, const char **err);

/* Lex and parse in one step. Variables come from the environment. */
int  parse_line(const char *line, const char *home, pipeline_t **out, const char **err);

void pipeline_free(pipeline_t *p);

#endif /* PARSER_H */
