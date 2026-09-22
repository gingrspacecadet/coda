#ifndef CODA_TEST_H
#define CODA_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "array.h"
#include "String.h"

typedef enum {
    TEST_STOP_LEX,
    TEST_STOP_AST,
    TEST_STOP_HIR,
    TEST_STOP_LIR,
    TEST_STOP_MACHINE,
    TEST_STOP_ASSEMBLY,
    TEST_STOP_RUN,
} TestStop;

typedef enum {
    TEST_EXPECT_PASS,
    TEST_EXPECT_FAIL,
} TestExpectation;

typedef struct {
    String name;
    String source;

    TestExpectation expectation;
    TestStop stop;

    size_t error_count;
    bool has_exit_code;
    int exit_code;

    bool has_diagnostics;
    bool has_ast;
    bool has_hir;
    bool has_lir;
    bool has_machine;
    bool has_assembly;
    bool has_stdout;
    bool has_stderr;

    String diagnostics;
    String ast;
    String hir;
    String lir;
    String machine;
    String assembly;
    String stdout_text;
    String stderr_text;
} TestCase;

bool test_parse(Arena *arena, String contents, TestCase *test);

#endif