#include "test.h"

int tests_run;
int tests_failed;

void suite(const char *name)
{
    printf("%s\n", name);
}

int main(void)
{
    test_util();
    test_lexer();
    test_parser();
    test_history();

    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
