/*
 * test.h - a very small assertion harness.
 *
 * Every suite is a function listed in test_main.c. A failing check reports its
 * file and line and lets the rest of the suite carry on, so one run shows every
 * problem rather than only the first.
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

extern int tests_run;
extern int tests_failed;

void suite(const char *name);

#define CHECK(cond)                                                        \
    do {                                                                   \
        tests_run++;                                                       \
        if (!(cond)) {                                                     \
            tests_failed++;                                                \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                  \
    } while (0)

#define CHECK_STR(actual, expected)                                        \
    do {                                                                   \
        tests_run++;                                                       \
        const char *a_ = (actual), *e_ = (expected);                       \
        if (!a_ || !e_ || strcmp(a_, e_) != 0) {                           \
            tests_failed++;                                                \
            printf("  FAIL %s:%d  got \"%s\", wanted \"%s\"\n",            \
                   __FILE__, __LINE__, a_ ? a_ : "(null)", e_ ? e_ : "(null)"); \
        }                                                                  \
    } while (0)

#define CHECK_INT(actual, expected)                                        \
    do {                                                                   \
        tests_run++;                                                       \
        long a_ = (long)(actual), e_ = (long)(expected);                   \
        if (a_ != e_) {                                                    \
            tests_failed++;                                                \
            printf("  FAIL %s:%d  got %ld, wanted %ld\n",                  \
                   __FILE__, __LINE__, a_, e_);                            \
        }                                                                  \
    } while (0)

void test_util(void);
void test_lexer(void);
void test_parser(void);
void test_history(void);

#endif /* TEST_H */
