#ifndef DXBALL_TEST_CHECK_H
#define DXBALL_TEST_CHECK_H
#include <stdio.h>

/* Report every failed expectation with its location; exit status counts them. */
static int check_failures;
#define CHECK(condition) do { if (!(condition)) { ++check_failures; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); } } while (0)
#define CHECK_EQ(actual, expected) do { long long check_a = (long long)(actual), \
    check_e = (long long)(expected); if (check_a != check_e) { ++check_failures; \
    fprintf(stderr, "%s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, \
        #actual, check_a, check_e); } } while (0)
#define CHECK_DONE() (check_failures == 0 ? 0 : (fprintf(stderr, "%d failure(s)\n", \
    check_failures), 1))

#endif
