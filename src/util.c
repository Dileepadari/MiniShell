#include "minishell.h"
#include "util.h"

#include <ctype.h>

static void oom(void)
{
    fputs("minishell: out of memory\n", stderr);
    _exit(1);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) oom();
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---------------------------------------------------------------- sbuf --- */

void sbuf_init(sbuf_t *b)
{
    b->cap = 32;
    b->len = 0;
    b->data = xmalloc(b->cap);
    b->data[0] = '\0';
}

static void sbuf_reserve(sbuf_t *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    while (b->len + extra + 1 > b->cap) b->cap *= 2;
    b->data = xrealloc(b->data, b->cap);
}

void sbuf_putc(sbuf_t *b, char c)
{
    sbuf_reserve(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void sbuf_putn(sbuf_t *b, const char *s, size_t n)
{
    sbuf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void sbuf_puts(sbuf_t *b, const char *s)
{
    sbuf_putn(b, s, strlen(s));
}

void sbuf_clear(sbuf_t *b)
{
    b->len = 0;
    b->data[0] = '\0';
}

void sbuf_free(sbuf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

char *sbuf_release(sbuf_t *b)
{
    char *s = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    return s;
}

/* ---------------------------------------------------------------- svec --- */

void svec_init(svec_t *v)
{
    v->cap = 8;
    v->len = 0;
    v->items = xmalloc(v->cap * sizeof(char *));
    v->items[0] = NULL;
}

void svec_push(svec_t *v, char *owned)
{
    if (v->len + 2 > v->cap) {
        v->cap *= 2;
        v->items = xrealloc(v->items, v->cap * sizeof(char *));
    }
    v->items[v->len++] = owned;
    v->items[v->len] = NULL;
}

void svec_push_copy(svec_t *v, const char *s)
{
    svec_push(v, xstrdup(s));
}

void svec_free(svec_t *v)
{
    for (size_t i = 0; i < v->len; i++) free(v->items[i]);
    free(v->items);
    v->items = NULL;
    v->len = v->cap = 0;
}

char **svec_release(svec_t *v)
{
    char **items = v->items;
    v->items = NULL;
    v->len = v->cap = 0;
    return items;
}

/* -------------------------------------------------------------- strings --- */

char *str_trim(char *s)
{
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    s[len] = '\0';
    return s;
}

int str_has_prefix(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int parse_int(const char *s, int *out)
{
    if (!s || !*s) return 0;

    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    if (v < INT_MIN || v > INT_MAX) return 0;

    *out = (int)v;
    return 1;
}

/* ---------------------------------------------------------------- paths --- */

int path_join(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    while (dlen > 1 && dir[dlen - 1] == '/') dlen--;

    /* "/" already ends in a separator, and an empty directory adds none. */
    const char *sep = (dlen == 0 || (dlen == 1 && dir[0] == '/')) ? "" : "/";

    int n = snprintf(out, out_size, "%.*s%s%s", (int)dlen, dir, sep, name);
    if (n < 0 || (size_t)n >= out_size) return -1;
    return 0;
}

void path_abbreviate(char *out, size_t out_size, const char *path, const char *home)
{
    size_t hlen = strlen(home);

    /* Inside home, or home itself: show it as "~[/rest]". */
    if (hlen > 0 && strncmp(path, home, hlen) == 0 &&
        (path[hlen] == '\0' || path[hlen] == '/')) {
        snprintf(out, out_size, "~%s", path + hlen);
        return;
    }
    snprintf(out, out_size, "%s", path);
}
