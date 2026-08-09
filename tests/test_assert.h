#pragma once
#include <stdio.h>

static int s_test_checks = 0;
static int s_test_failed = 0;

#define TEST_CHECK(cond)                                                       \
  do {                                                                         \
    s_test_checks++;                                                          \
    if(!(cond)) {                                                              \
      s_test_failed++;                                                        \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                          \
  } while(0)

#define TEST_SECTION(name) fprintf(stderr, "-- %s\n", name)

#define TEST_REPORT()                                                          \
  do {                                                                         \
    fprintf(stderr, "%d/%d checks passed\n", s_test_checks - s_test_failed, s_test_checks); \
  } while(0)
