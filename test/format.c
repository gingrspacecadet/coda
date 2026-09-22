#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

typedef enum {
    TEST_SECTION_NONE,
    TEST_SECTION_SOURCE,
    TEST_SECTION_DIAGNOSTICS,
    TEST_SECTION_AST,
    TEST_SECTION_HIR,
    TEST_SECTION_LIR,
    TEST_SECTION_MACHINE,
    TEST_SECTION_ASSEMBLY,
    TEST_SECTION_STDOUT,
    TEST_SECTION_STDERR,
    TEST_SECTION_EXIT,
} TestSection;

static String test_trim(String string) {
    while (string.length > 0 && isspace((unsigned char)string.data[0]))
        string.data++, string.length--;

    while (string.length > 0 && isspace((unsigned char)string.data[string.length - 1]))
        string.length--;

    return string;
}

static bool test_string_equal(String a, const char *b) {
    size_t length = strlen(b);

    return a.length == length && memcmp(a.data, b, length) == 0;
}

static bool test_parse_expect(String value, TestExpectation *expectation) {
    if (test_string_equal(value, "pass")) {
        *expectation = TEST_EXPECT_PASS;
        return true;
    }

    if (test_string_equal(value, "fail")) {
        *expectation = TEST_EXPECT_FAIL;
        return true;
    }

    return false;
}

static bool test_parse_stop(String value, TestStop *stop) {
    if (test_string_equal(value, "lex")) {
        *stop = TEST_STOP_LEX;
        return true;
    }

    if (test_string_equal(value, "ast")) {
        *stop = TEST_STOP_AST;
        return true;
    }

    if (test_string_equal(value, "hir")) {
        *stop = TEST_STOP_HIR;
        return true;
    }

    if (test_string_equal(value, "lir")) {
        *stop = TEST_STOP_LIR;
        return true;
    }

    if (test_string_equal(value, "machine")) {
        *stop = TEST_STOP_MACHINE;
        return true;
    }

    if (test_string_equal(value, "assembly")) {
        *stop = TEST_STOP_ASSEMBLY;
        return true;
    }

    if (test_string_equal(value, "run")) {
        *stop = TEST_STOP_RUN;
        return true;
    }

    return false;
}

static TestSection test_parse_section(String name) {
    if (test_string_equal(name, "source"))
        return TEST_SECTION_SOURCE;

    if (test_string_equal(name, "diagnostics"))
        return TEST_SECTION_DIAGNOSTICS;

    if (test_string_equal(name, "ast"))
        return TEST_SECTION_AST;

    if (test_string_equal(name, "hir"))
        return TEST_SECTION_HIR;

    if (test_string_equal(name, "lir"))
        return TEST_SECTION_LIR;

    if (test_string_equal(name, "machine"))
        return TEST_SECTION_MACHINE;

    if (test_string_equal(name, "assembly"))
        return TEST_SECTION_ASSEMBLY;

    if (test_string_equal(name, "stdout"))
        return TEST_SECTION_STDOUT;

    if (test_string_equal(name, "stderr"))
        return TEST_SECTION_STDERR;

    if (test_string_equal(name, "exit"))
        return TEST_SECTION_EXIT;

    return TEST_SECTION_NONE;
}

static bool test_parse_number(String value, long *result) {
    char *end;
    char *buffer = malloc(value.length + 1);

    if (buffer == NULL)
        return false;

    memcpy(buffer, value.data, value.length);
    buffer[value.length] = '\0';

    *result = strtol(buffer, &end, 10);

    bool valid = end != buffer && *end == '\0';

    free(buffer);
    return valid;
}

static bool test_set_header(Arena *arena, TestCase *test, String line) {
    size_t colon = SIZE_MAX;

    for (size_t i = 0; i < line.length; i++) {
        if (line.data[i] == ':') {
            colon = i;
            break;
        }
    }

    if (colon == SIZE_MAX)
        return false;

    String key = test_trim((String){ .data = line.data, .length = colon });
    String value = test_trim((String){ .data = line.data + colon + 1, .length = line.length - colon - 1 });

    if (test_string_equal(key, "name")) {
        test->name = value;
        return true;
    }

    if (test_string_equal(key, "expect"))
        return test_parse_expect(value, &test->expectation);

    if (test_string_equal(key, "stop"))
        return test_parse_stop(value, &test->stop);

    if (test_string_equal(key, "errors")) {
        long number;

        if (!test_parse_number(value, &number) || number < 0)
            return false;

        test->error_count = (size_t)number;
        return true;
    }

    return false;
}

static bool test_section_header(String line, TestSection *section) {
    if (line.length < 2 || line.data[0] != '[' || line.data[line.length - 1] != ']')
        return false;

    String name = {
        .data = line.data + 1,
        .length = line.length - 2,
    };

    *section = test_parse_section(name);
    return *section != TEST_SECTION_NONE;
}

static void test_assign_section(TestCase *test, TestSection section, String value) {
    switch (section) {
        case TEST_SECTION_SOURCE:
            test->source = value;
            break;

        case TEST_SECTION_DIAGNOSTICS:
            test->diagnostics = value;
            test->has_diagnostics = true;
            break;

        case TEST_SECTION_AST:
            test->ast = value;
            test->has_ast = true;
            break;

        case TEST_SECTION_HIR:
            test->hir = value;
            test->has_hir = true;
            break;

        case TEST_SECTION_LIR:
            test->lir = value;
            test->has_lir = true;
            break;

        case TEST_SECTION_MACHINE:
            test->machine = value;
            test->has_machine = true;
            break;

        case TEST_SECTION_ASSEMBLY:
            test->assembly = value;
            test->has_assembly = true;
            break;

        case TEST_SECTION_STDOUT:
            test->stdout_text = value;
            test->has_stdout = true;
            break;

        case TEST_SECTION_STDERR:
            test->stderr_text = value;
            test->has_stderr = true;
            break;

        case TEST_SECTION_EXIT: {
            String trimmed = test_trim(value);
            long exit_code;

            if (test_parse_number(trimmed, &exit_code)) {
                test->has_exit_code = true;
                test->exit_code = (int)exit_code;
            }

            break;
        }

        case TEST_SECTION_NONE:
            break;
    }
}

bool test_parse(Arena *arena, String contents, TestCase *test) {
    (void)arena;

    *test = (TestCase){
        .expectation = TEST_EXPECT_PASS,
        .stop = TEST_STOP_HIR,
    };

    TestSection section = TEST_SECTION_NONE;
    size_t section_start = 0;
    size_t position = 0;
    bool saw_section = false;

    while (position <= contents.length) {
        size_t line_start = position;

        while (position < contents.length && contents.data[position] != '\n')
            position++;

        size_t line_length = position - line_start;

        if (line_length > 0 && contents.data[line_start + line_length - 1] == '\r')
            line_length--;

        String line = {
            .data = contents.data + line_start,
            .length = line_length,
        };

        TestSection next_section;

        if (test_section_header(line, &next_section)) {
            if (section != TEST_SECTION_NONE) {
                String value = {
                    .data = contents.data + section_start,
                    .length = line_start - section_start,
                };

                test_assign_section(test, section, value);
            }

            section = next_section;
            section_start = position + (position < contents.length);
            saw_section = true;
        } else if (!saw_section && line.length != 0) {
            if (!test_set_header(arena, test, line))
                return false;
        }

        if (position == contents.length)
            break;

        position++;
    }

    if (section != TEST_SECTION_NONE) {
        String value = {
            .data = contents.data + section_start,
            .length = contents.length - section_start,
        };

        test_assign_section(test, section, value);
    }

    if (test->name.length == 0)
        return false;

    if (test->source.length == 0)
        return false;

    return true;
}