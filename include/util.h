/*
 * util.h - allocation wrappers, growable buffers and path helpers.
 *
 * Everything here is deliberately dependency-free so the unit tests can link it
 * without dragging in the rest of the shell.
 */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Allocation that never returns NULL: on exhaustion it reports and exits. */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* A growable, always NUL-terminated character buffer. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} sbuf_t;

void  sbuf_init(sbuf_t *b);
void  sbuf_putc(sbuf_t *b, char c);
void  sbuf_puts(sbuf_t *b, const char *s);
void  sbuf_putn(sbuf_t *b, const char *s, size_t n);
void  sbuf_clear(sbuf_t *b);
void  sbuf_free(sbuf_t *b);
char *sbuf_release(sbuf_t *b); /* hand ownership of the string to the caller */

/* A growable, always NULL-terminated vector of owned strings. */
typedef struct {
    char **items;
    size_t len;
    size_t cap;
} svec_t;

void   svec_init(svec_t *v);
void   svec_push(svec_t *v, char *owned);       /* takes ownership */
void   svec_push_copy(svec_t *v, const char *s);
void   svec_free(svec_t *v);
char **svec_release(svec_t *v);                 /* NULL-terminated argv */

/* Strip leading and trailing whitespace in place. Returns the same pointer. */
char *str_trim(char *s);
/* Non-destructive prefix test. */
int   str_has_prefix(const char *s, const char *prefix);
/* Parse a whole string as an int. Returns 0 and leaves *out alone on failure. */
int   parse_int(const char *s, int *out);

/*
 * Join `dir` and `name` into `out`, collapsing a trailing slash on `dir`.
 * Returns 0 on success, -1 if the result would not fit.
 */
int path_join(char *out, size_t out_size, const char *dir, const char *name);

/*
 * Rewrite `path` as "~..." when it lies inside `home`. Used by the prompt and by
 * `proclore`, which both display paths relative to the shell's home.
 */
void path_abbreviate(char *out, size_t out_size, const char *path, const char *home);

#endif /* UTIL_H */
