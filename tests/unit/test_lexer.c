#include "test.h"
#include "minishell.h"
#include "lexer.h"

static const char *fixture_lookup(const char *name, void *ctx)
{
    (void)ctx;
    if (!strcmp(name, "GREETING")) return "hello";
    if (!strcmp(name, "EMPTY"))    return "";
    if (!strcmp(name, "?"))        return "7";
    return NULL;
}

static tokens_t lex(const char *line, const char **err)
{
    tokens_t t;
    lex_line_ex(line, &t, err, fixture_lookup, NULL);
    return t;
}

void test_lexer(void)
{
    suite("lexer");

    const char *err = NULL;
    tokens_t t;

    /* Words split on whitespace; the stream always ends with TOK_EOF. */
    t = lex("echo  hello   world", &err);
    CHECK_INT(t.len, 4);
    CHECK_INT(t.items[0].type, TOK_WORD);
    CHECK_STR(t.items[0].text, "echo");
    CHECK_STR(t.items[2].text, "world");
    CHECK_INT(t.items[3].type, TOK_EOF);
    tokens_free(&t);

    /* Operators break words even without surrounding spaces. */
    t = lex("a>b>>c<d|e;f&g", &err);
    CHECK_INT(t.items[1].type, TOK_REDIR_OUT);
    CHECK_INT(t.items[3].type, TOK_REDIR_APPEND);
    CHECK_INT(t.items[5].type, TOK_REDIR_IN);
    CHECK_INT(t.items[7].type, TOK_PIPE);
    CHECK_INT(t.items[9].type, TOK_SEMI);
    CHECK_INT(t.items[11].type, TOK_AMP);
    CHECK_STR(t.items[12].text, "g");
    tokens_free(&t);

    /* Quotes are removed and hold spaces and operators together. */
    t = lex("echo 'one two' \"three | four\"", &err);
    CHECK_INT(t.len, 4);
    CHECK_STR(t.items[1].text, "one two");
    CHECK_INT(t.items[1].quoted, 1);
    CHECK_STR(t.items[2].text, "three | four");
    tokens_free(&t);

    /* Adjacent quoted and bare pieces make a single word. */
    t = lex("pre'mid'post", &err);
    CHECK_INT(t.len, 2);
    CHECK_STR(t.items[0].text, "premidpost");
    tokens_free(&t);

    /* Backslash escapes the next character, whatever it is. */
    t = lex("a\\ b \\| c", &err);
    CHECK_STR(t.items[0].text, "a b");
    CHECK_STR(t.items[1].text, "|");
    CHECK_INT(t.items[1].quoted, 1);
    tokens_free(&t);

    /* Inside double quotes only a few escapes are special. */
    t = lex("\"say \\\"hi\\\" \\n\"", &err);
    CHECK_STR(t.items[0].text, "say \"hi\" \\n");
    tokens_free(&t);

    /* Variables expand bare and inside double quotes, not inside single. */
    t = lex("$GREETING \"$GREETING there\" '$GREETING' ${GREETING}s", &err);
    CHECK_STR(t.items[0].text, "hello");
    CHECK_STR(t.items[1].text, "hello there");
    CHECK_STR(t.items[2].text, "$GREETING");
    CHECK_STR(t.items[3].text, "hellos");
    tokens_free(&t);

    /* An unset name becomes empty; a lone or unusable $ stays literal. */
    t = lex("$NOPE- $EMPTY- $ $%", &err);
    CHECK_STR(t.items[0].text, "-");
    CHECK_STR(t.items[1].text, "-");
    CHECK_STR(t.items[2].text, "$");
    CHECK_STR(t.items[3].text, "$%");
    tokens_free(&t);

    t = lex("code=$?", &err);
    CHECK_STR(t.items[0].text, "code=7");
    tokens_free(&t);

    /* Lexical errors report and yield no words. */
    t = lex("echo 'unterminated", &err);
    CHECK_STR(err, "unterminated single quote");
    CHECK_INT(t.items[0].type, TOK_EOF);
    tokens_free(&t);

    t = lex("echo \"unterminated", &err);
    CHECK_STR(err, "unterminated double quote");
    tokens_free(&t);

    t = lex("echo trailing\\", &err);
    CHECK_STR(err, "trailing backslash");
    tokens_free(&t);

    /* An empty line is just TOK_EOF. */
    t = lex("   \t ", &err);
    CHECK_INT(t.len, 1);
    CHECK_INT(t.items[0].type, TOK_EOF);
    tokens_free(&t);

    CHECK_STR(tok_name(TOK_REDIR_APPEND), ">>");
}
