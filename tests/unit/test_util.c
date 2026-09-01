#include "test.h"
#include "minishell.h"
#include "util.h"

void test_util(void)
{
    suite("util");

    /* sbuf grows past its initial capacity and stays NUL terminated. */
    sbuf_t b;
    sbuf_init(&b);
    for (int i = 0; i < 100; i++) sbuf_putc(&b, 'x');
    sbuf_puts(&b, "end");
    CHECK_INT(b.len, 103);
    CHECK_INT(strlen(b.data), 103);
    CHECK_STR(b.data + 100, "end");
    sbuf_clear(&b);
    CHECK_INT(b.len, 0);
    CHECK_STR(b.data, "");
    sbuf_free(&b);

    /* svec keeps a NULL terminator so it can be used directly as argv. */
    svec_t v;
    svec_init(&v);
    for (int i = 0; i < 20; i++) svec_push_copy(&v, "arg");
    CHECK_INT(v.len, 20);
    CHECK(v.items[20] == NULL);
    svec_free(&v);

    char trim1[] = "   spaced out \t\n";
    CHECK_STR(str_trim(trim1), "spaced out");
    char trim2[] = "\t\n  ";
    CHECK_STR(str_trim(trim2), "");
    char trim3[] = "none";
    CHECK_STR(str_trim(trim3), "none");

    int n = -1;
    CHECK(parse_int("42", &n) == 1);
    CHECK_INT(n, 42);
    CHECK(parse_int("-7", &n) == 1);
    CHECK_INT(n, -7);
    CHECK(parse_int("12abc", &n) == 0);
    CHECK(parse_int("", &n) == 0);
    CHECK(parse_int("99999999999999999999", &n) == 0);
    CHECK_INT(n, -7);   /* a failed parse leaves the destination alone */

    char joined[64];
    CHECK(path_join(joined, sizeof(joined), "/a/b", "c") == 0);
    CHECK_STR(joined, "/a/b/c");
    CHECK(path_join(joined, sizeof(joined), "/a/b/", "c") == 0);
    CHECK_STR(joined, "/a/b/c");
    CHECK(path_join(joined, sizeof(joined), "/", "c") == 0);
    CHECK_STR(joined, "/c");
    CHECK(path_join(joined, sizeof(joined), "", "c") == 0);
    CHECK_STR(joined, "c");
    char tiny[4];
    CHECK(path_join(tiny, sizeof(tiny), "/long/dir", "name") == -1);

    char shown[64];
    path_abbreviate(shown, sizeof(shown), "/home/u/proj/src", "/home/u/proj");
    CHECK_STR(shown, "~/src");
    path_abbreviate(shown, sizeof(shown), "/home/u/proj", "/home/u/proj");
    CHECK_STR(shown, "~");
    /* A path that merely shares a prefix is not inside home. */
    path_abbreviate(shown, sizeof(shown), "/home/u/projector", "/home/u/proj");
    CHECK_STR(shown, "/home/u/projector");
    path_abbreviate(shown, sizeof(shown), "/etc", "/home/u/proj");
    CHECK_STR(shown, "/etc");
}
