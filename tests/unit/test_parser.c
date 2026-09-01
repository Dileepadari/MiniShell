#include "test.h"
#include "minishell.h"
#include "parser.h"
#include "util.h"

static pipeline_t *parse(const char *line, const char **err)
{
    pipeline_t *p = NULL;
    parse_line(line, "/shell/home", &p, err);
    return p;
}

void test_parser(void)
{
    suite("parser");

    const char *err = NULL;
    pipeline_t *p;

    /* A blank line parses to nothing at all. */
    p = parse("   ", &err);
    CHECK(p == NULL);
    CHECK(err == NULL);
    pipeline_free(p);

    p = parse("echo hello world", &err);
    CHECK(p != NULL);
    CHECK_INT(p->ncmds, 1);
    CHECK_INT(p->cmds[0].argc, 3);
    CHECK_STR(p->cmds[0].argv[0], "echo");
    CHECK_STR(p->cmds[0].argv[2], "world");
    CHECK(p->cmds[0].argv[3] == NULL);
    CHECK_INT(p->background, 0);
    CHECK(p->next == NULL);
    pipeline_free(p);

    /* Semicolons make separate pipelines; `&` marks the one before it. */
    p = parse("one ; two & three", &err);
    CHECK_INT(p->cmds[0].argc, 1);
    CHECK_INT(p->background, 0);
    CHECK_INT(p->next->background, 1);
    CHECK_STR(p->next->cmds[0].argv[0], "two");
    CHECK_INT(p->next->next->background, 0);
    CHECK(p->next->next->next == NULL);
    pipeline_free(p);

    /* A trailing separator does not create an empty pipeline. */
    p = parse("only ;", &err);
    CHECK_STR(p->cmds[0].argv[0], "only");
    CHECK(p->next == NULL);
    pipeline_free(p);

    p = parse("job &", &err);
    CHECK_INT(p->background, 1);
    CHECK(p->next == NULL);
    pipeline_free(p);

    /* Pipelines collect their commands in order. */
    p = parse("a | b arg | c", &err);
    CHECK_INT(p->ncmds, 3);
    CHECK_STR(p->cmds[1].argv[0], "b");
    CHECK_STR(p->cmds[1].argv[1], "arg");
    CHECK_STR(p->cmds[2].argv[0], "c");
    CHECK_STR(p->text, "a | b arg | c");
    pipeline_free(p);

    /* Redirections attach to their command and leave argv alone. */
    p = parse("sort < in.txt > out.txt", &err);
    CHECK_INT(p->cmds[0].argc, 1);
    CHECK_STR(p->cmds[0].infile, "in.txt");
    CHECK_STR(p->cmds[0].outfile, "out.txt");
    CHECK_INT(p->cmds[0].append, 0);
    pipeline_free(p);

    p = parse("echo hi >> log", &err);
    CHECK_STR(p->cmds[0].outfile, "log");
    CHECK_INT(p->cmds[0].append, 1);
    pipeline_free(p);

    /* Redirections may appear before or between arguments. */
    p = parse("> out cat file", &err);
    CHECK_STR(p->cmds[0].argv[0], "cat");
    CHECK_STR(p->cmds[0].argv[1], "file");
    CHECK_STR(p->cmds[0].outfile, "out");
    pipeline_free(p);

    /* The last redirection of a kind wins. */
    p = parse("cat > first > second", &err);
    CHECK_STR(p->cmds[0].outfile, "second");
    pipeline_free(p);

    /* Each command of a pipeline keeps its own redirections. */
    p = parse("a < in | b > out", &err);
    CHECK_STR(p->cmds[0].infile, "in");
    CHECK(p->cmds[0].outfile == NULL);
    CHECK_STR(p->cmds[1].outfile, "out");
    pipeline_free(p);

    /* Tilde expands to the shell home, but not when it is quoted. */
    p = parse("warp ~ ~/sub '~' \"~\"", &err);
    CHECK_STR(p->cmds[0].argv[1], "/shell/home");
    CHECK_STR(p->cmds[0].argv[2], "/shell/home/sub");
    CHECK_STR(p->cmds[0].argv[3], "~");
    CHECK_STR(p->cmds[0].argv[4], "~");
    pipeline_free(p);

    /* A pattern that matches nothing is passed through unchanged. */
    p = parse("peek /nonexistent-dir-xyz/*.none", &err);
    CHECK_INT(p->cmds[0].argc, 2);
    CHECK_STR(p->cmds[0].argv[1], "/nonexistent-dir-xyz/*.none");
    pipeline_free(p);

    /* Syntax errors are reported rather than half-parsed. */
    p = parse("| grep x", &err);
    CHECK(p == NULL);
    CHECK_STR(err, "missing command around `|`");

    p = parse("echo hi |", &err);
    CHECK(p == NULL);
    CHECK_STR(err, "missing command after `|`");

    p = parse("cat >", &err);
    CHECK(p == NULL);
    CHECK_STR(err, "expected a file name after a redirection operator");

    p = parse("cat < > out", &err);
    CHECK(p == NULL);
    CHECK(err != NULL);

    /* A lexical error surfaces through parse_line as well. */
    p = parse("echo 'oops", &err);
    CHECK(p == NULL);
    CHECK_STR(err, "unterminated single quote");
}
