#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "coda.h"
#include "diag.h"
#include "print.h"
#include "source.h"
#include "test.h"

typedef struct {
    size_t passed;
    size_t failed;
} TestStats;

typedef struct {
    bool unsupported_stop;
    bool compile_status;
    bool error_count;
    bool stage_output;
    bool diagnostics;
    bool runtime;

    bool expected_failure;
    bool actual_failure;
    size_t actual_errors;

    size_t diff_line;
    String expected_line;
    String actual_line;

    char *diagnostics_data;
    size_t diagnostics_length;
} TestFailure;

static bool test_read_file(const char *path, char **data, size_t *length) {
    FILE *file = fopen(path, "rb");

    if (!file)
        return false;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long size = ftell(file);

    if (size < 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char *buffer = malloc((size_t)size + 1);

    if (!buffer) {
        fclose(file);
        return false;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return false;
    }

    fclose(file);

    buffer[size] = '\0';

    *data = buffer;
    *length = (size_t)size;

    return true;
}

static bool test_has_extension(const char *path, const char *extension) {
    size_t path_length = strlen(path);
    size_t extension_length = strlen(extension);

    return path_length >= extension_length &&
           strcmp(path + path_length - extension_length, extension) == 0;
}

static bool test_is_directory(const char *path) {
    struct stat statbuf;

    if (stat(path, &statbuf) != 0)
        return false;

    return S_ISDIR(statbuf.st_mode);
}

static char *test_join_path(const char *directory, const char *name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    bool separator = directory_length != 0 && directory[directory_length - 1] != '/';

    size_t length = directory_length + name_length + separator + 1;
    char *path = malloc(length);

    if (!path)
        return NULL;

    memcpy(path, directory, directory_length);

    size_t offset = directory_length;

    if (separator)
        path[offset++] = '/';

    memcpy(path + offset, name, name_length);
    path[offset + name_length] = '\0';

    return path;
}

static int test_path_compare(const void *a, const void *b) {
    const char *const *left = a;
    const char *const *right = b;

    return strcmp(*left, *right);
}

static CodaStage test_coda_stage(TestStop stop) {
    switch (stop) {
        case TEST_STOP_AST:
            return CODA_STAGE_AST;

        case TEST_STOP_HIR:
            return CODA_STAGE_HIR;

        case TEST_STOP_LIR:
            return CODA_STAGE_LIR;

        default:
            return CODA_STAGE_LIR;
    }
}

static const char *test_stop_name(TestStop stop) {
    switch (stop) {
        case TEST_STOP_LEX:
            return "lex";

        case TEST_STOP_AST:
            return "ast";

        case TEST_STOP_HIR:
            return "hir";

        case TEST_STOP_LIR:
            return "lir";

        case TEST_STOP_MACHINE:
            return "machine";

        case TEST_STOP_ASSEMBLY:
            return "assembly";

        case TEST_STOP_RUN:
            return "run";
    }

    return "unknown";
}

static bool test_stop_supported(TestStop stop) {
    return stop == TEST_STOP_AST ||
           stop == TEST_STOP_HIR ||
           stop == TEST_STOP_LIR;
}

static bool test_render_ast(const AstModule *module, char **data, size_t *length) {
    FILE *file = open_memstream(data, length);

    if (!file)
        return false;

    print_ast_module(file, module);

    if (fclose(file) != 0) {
        free(*data);
        *data = NULL;
        *length = 0;
        return false;
    }

    return true;
}

static bool test_render_hir(const HirModule *module, char **data, size_t *length) {
    FILE *file = open_memstream(data, length);

    if (!file)
        return false;

    print_hir_module(file, module);

    if (fclose(file) != 0) {
        free(*data);
        *data = NULL;
        *length = 0;
        return false;
    }

    return true;
}

static bool test_render_lir(const LirModule *module, char **data, size_t *length) {
    FILE *file = open_memstream(data, length);

    if (!file)
        return false;

    lir_print(file, module);

    if (fclose(file) != 0) {
        free(*data);
        *data = NULL;
        *length = 0;
        return false;
    }

    return true;
}

static String test_trim_right(String string) {
    while (string.length > 0 && isspace((unsigned char)string.data[string.length - 1]))
        string.length--;

    return string;
}

static String test_line_at(const char *data, size_t length, size_t line) {
    size_t current_line = 1;
    size_t start = 0;

    while (start < length && current_line < line) {
        if (data[start] == '\n')
            current_line++;

        start++;
    }

    if (current_line != line)
        return (String){0};

    size_t end = start;

    while (end < length && data[end] != '\n')
        end++;

    return (String){
        .data = (char *)(data + start),
        .length = end - start,
    };
}

static size_t test_first_difference(String expected, const char *actual, size_t actual_length) {
    expected = test_trim_right(expected);

    while (actual_length > 0 && isspace((unsigned char)actual[actual_length - 1]))
        actual_length--;

    size_t length = expected.length < actual_length ? expected.length : actual_length;

    for (size_t i = 0; i < length; i++) {
        if (expected.data[i] != actual[i])
            return i;
    }

    return length;
}

static size_t test_line_number(const char *data, size_t length, size_t offset) {
    size_t line = 1;

    if (offset > length)
        offset = length;

    for (size_t i = 0; i < offset; i++) {
        if (data[i] == '\n')
            line++;
    }

    return line;
}

static bool test_compare_text(String expected, const char *actual, size_t actual_length, TestFailure *failure) {
    String trimmed_expected = test_trim_right(expected);
    size_t trimmed_actual_length = actual_length;

    while (trimmed_actual_length > 0 &&
           isspace((unsigned char)actual[trimmed_actual_length - 1]))
        trimmed_actual_length--;

    if (trimmed_expected.length == trimmed_actual_length &&
        memcmp(trimmed_expected.data, actual, trimmed_actual_length) == 0)
        return true;

    size_t difference = test_first_difference(
        trimmed_expected,
        actual,
        trimmed_actual_length
    );

    size_t expected_line = test_line_number(
        trimmed_expected.data,
        trimmed_expected.length,
        difference
    );

    size_t actual_line = test_line_number(
        actual,
        trimmed_actual_length,
        difference
    );

    failure->diff_line = expected_line > actual_line ? expected_line : actual_line;
    failure->expected_line = test_line_at(
        trimmed_expected.data,
        trimmed_expected.length,
        expected_line
    );

    String actual_line_string = test_line_at(
        actual,
        trimmed_actual_length,
        actual_line
    );

    if (actual_line_string.length != 0) {
        failure->actual_line.data = malloc(actual_line_string.length);

        if (!failure->actual_line.data)
            return false;

        memcpy(
            failure->actual_line.data,
            actual_line_string.data,
            actual_line_string.length
        );

        failure->actual_line.length = actual_line_string.length;
    }

    return false;
}

static bool test_get_expected_output(const TestCase *test, String *expected) {
    switch (test->stop) {
        case TEST_STOP_AST:
            if (!test->has_ast)
                return false;

            *expected = test->ast;
            return true;

        case TEST_STOP_HIR:
            if (!test->has_hir)
                return false;

            *expected = test->hir;
            return true;

        case TEST_STOP_LIR:
            if (!test->has_lir)
                return false;

            *expected = test->lir;
            return true;

        default:
            return false;
    }
}

static bool test_compare_stage(const TestCase *test, const CodaCompiler *compiler, TestFailure *failure) {
    String expected;

    if (!test_get_expected_output(test, &expected))
        return true;

    char *actual = NULL;
    size_t actual_length = 0;
    bool rendered = false;

    switch (test->stop) {
        case TEST_STOP_AST:
            if (!compiler->compilation.ast) {
                failure->stage_output = true;
                return false;
            }

            rendered = test_render_ast(
                compiler->compilation.ast,
                &actual,
                &actual_length
            );
            break;

        case TEST_STOP_HIR:
            if (!compiler->compilation.hir) {
                failure->stage_output = true;
                return false;
            }

            rendered = test_render_hir(
                compiler->compilation.hir,
                &actual,
                &actual_length
            );
            break;

        case TEST_STOP_LIR:
            if (!compiler->compilation.lir) {
                failure->stage_output = true;
                return false;
            }

            rendered = test_render_lir(compiler->compilation.lir, &actual, &actual_length);
            break;

        default:
            return true;
    }

    if (!rendered) {
        failure->stage_output = true;
        return false;
    }

    bool equal = test_compare_text(expected, actual, actual_length, failure);

    if (!equal)
        failure->stage_output = true;

    free(actual);

    return equal;
}

static bool test_capture_stderr_start(FILE **file, int *saved_fd) {
    *file = tmpfile();

    if (!*file)
        return false;

    fflush(stderr);

    *saved_fd = dup(STDERR_FILENO);

    if (*saved_fd < 0) {
        fclose(*file);
        *file = NULL;
        return false;
    }

    if (dup2(fileno(*file), STDERR_FILENO) < 0) {
        close(*saved_fd);
        fclose(*file);
        *file = NULL;
        return false;
    }

    return true;
}

static bool test_capture_stderr_stop(FILE **file, int saved_fd, char **data, size_t *length) {
    fflush(stderr);

    if (dup2(saved_fd, STDERR_FILENO) < 0) {
        close(saved_fd);
        fclose(*file);
        *file = NULL;
        return false;
    }

    close(saved_fd);

    FILE *capture = *file;

    if (fseek(capture, 0, SEEK_END) != 0) {
        fclose(capture);
        *file = NULL;
        return false;
    }

    long size = ftell(capture);

    if (size < 0) {
        fclose(capture);
        *file = NULL;
        return false;
    }

    if (fseek(capture, 0, SEEK_SET) != 0) {
        fclose(capture);
        *file = NULL;
        return false;
    }

    if (size == 0) {
        fclose(capture);
        *file = NULL;
        *data = NULL;
        *length = 0;
        return true;
    }

    char *buffer = malloc((size_t)size);

    if (!buffer) {
        fclose(capture);
        *file = NULL;
        return false;
    }

    if (fread(buffer, 1, (size_t)size, capture) != (size_t)size) {
        free(buffer);
        fclose(capture);
        *file = NULL;
        return false;
    }

    fclose(capture);
    *file = NULL;

    *data = buffer;
    *length = (size_t)size;

    return true;
}

static bool test_compile(const char *path, const TestCase *test, CodaCompiler *compiler, size_t *actual_errors, char **diagnostics, size_t *diagnostics_length) {
    Arena *arena = compiler->arena;

    String test_path = {
        .data = (char *)path,
        .length = strlen(path),
    };

    Source source = {
        .path = test_path,
        .contents = test->source,
    };

    source_build_lines(&source, arena);

    FILE *capture = NULL;
    int saved_stderr = -1;

    if (!test_capture_stderr_start(&capture, &saved_stderr))
        return false;

    bool compiled = coda_compile(
        compiler,
        &source,
        test_coda_stage(test->stop)
    );

    *actual_errors = compiler->diags->diags.len;

    if (!test_capture_stderr_stop(
            &capture,
            saved_stderr,
            diagnostics,
            diagnostics_length)) {
        return false;
    }

    return compiled;
}

static void test_print_line(const char *label, String line) {
    fprintf(stderr, "    %s: ", label);

    if (line.length == 0) {
        fprintf(stderr, "<empty>\n");
        return;
    }

    fwrite(line.data, 1, line.length, stderr);
    fputc('\n', stderr);
}

static void test_print_failures(const TestCase *test, const TestFailure *failure) {
    if (failure->unsupported_stop) {
        fprintf(stderr, "    stop stage '%s' is not implemented yet\n", test_stop_name(test->stop));
    }

    if (failure->compile_status) {
        fprintf(stderr, "    expected compilation to %s, but it %s\n", failure->expected_failure ? "fail" : "succeed", failure->actual_failure ? "failed" : "succeeded");
    }

    if (failure->error_count) {
        fprintf(stderr, "    expected %zu errors, got %zu\n", test->error_count, failure->actual_errors);
    }

    if (failure->stage_output) {
        if (failure->diff_line == 0) {
            fprintf(stderr, "    stage output unavailable\n");
        } else {
            fprintf(stderr, "    stage output mismatch at line %zu\n", failure->diff_line);

            test_print_line("expected", failure->expected_line);
            test_print_line("actual", failure->actual_line);
        }
    }

    if (failure->diagnostics)
        fprintf(stderr, "    diagnostics comparison is not implemented yet\n");

    if (failure->runtime)
        fprintf(stderr, "    runtime/backend expectations are not implemented yet\n");

    if (failure->diagnostics_data && failure->diagnostics_length != 0) {
        fprintf(stderr, "\n");
        fwrite(failure->diagnostics_data, 1, failure->diagnostics_length, stderr);

        if (failure->diagnostics_data[failure->diagnostics_length - 1] != '\n')
            fputc('\n', stderr);
    }
}

static bool test_check(const char *path, const TestCase *test, TestFailure *failure) {
    memset(failure, 0, sizeof(*failure));

    if (!test_stop_supported(test->stop)) {
        failure->unsupported_stop = true;
        return false;
    }

    Arena *arena = arena_create();

    if (!arena) {
        fprintf(stderr, "ERROR %s: failed to create arena\n", path);
        failure->runtime = true;
        return false;
    }

    Diags diags;
    diags_init(&diags, arena);

    CodaCompiler compiler;
    coda_compiler_init(&compiler, arena, &diags);

    bool compiled = test_compile(path, test, &compiler, &failure->actual_errors, &failure->diagnostics_data, &failure->diagnostics_length);

    failure->expected_failure = test->expectation == TEST_EXPECT_FAIL;
    failure->actual_failure = !compiled;

    bool pass = true;

    if (failure->expected_failure != failure->actual_failure) {
        failure->compile_status = true;
        pass = false;
    }

    if (failure->actual_errors != test->error_count) {
        failure->error_count = true;
        pass = false;
    }

    if (!failure->actual_failure &&
        !test_compare_stage(test, &compiler, failure)) {
        pass = false;
    }

    if (test->has_diagnostics) {
        failure->diagnostics = true;
        pass = false;
    }

    if (test->has_machine || test->has_assembly || test->has_stdout || test->has_stderr || test->has_exit_code) {
        failure->runtime = true;
        pass = false;
    }

    arena_destroy(arena);

    if (pass) {
        free(failure->diagnostics_data);
        failure->diagnostics_data = NULL;
        failure->diagnostics_length = 0;
        free(failure->actual_line.data);
        failure->actual_line.data = NULL;
        failure->actual_line.length = 0;
    }

    return pass;
}

static bool test_run_file(const char *path, TestStats *stats) {
    char *contents = NULL;
    size_t length = 0;

    if (!test_read_file(path, &contents, &length)) {
        fprintf(stderr, "ERROR %s: failed to read test file\n", path);
        stats->failed++;
        return false;
    }

    Arena *arena = arena_create();

    if (!arena) {
        fprintf(stderr, "ERROR %s: failed to create arena\n", path);
        free(contents);
        stats->failed++;
        return false;
    }

    TestCase test;

    if (!test_parse(arena, (String){ .data = contents, .length = length }, &test)) {
        fprintf(stderr, "ERROR %s: invalid test file\n", path);
        arena_destroy(arena);
        free(contents);
        stats->failed++;
        return false;
    }

    TestFailure failure;
    bool pass = test_check(path, &test, &failure);

    printf("%s\x1b[0m  %.*s\n", pass ? "\x1b[32mPASS" : "\x1b[31mFAIL", string_fmt(test.name));

    if (!pass) {
        test_print_failures(&test, &failure);
        stats->failed++;
    } else {
        stats->passed++;
    }

    free(failure.diagnostics_data);
    free(failure.actual_line.data);
    arena_destroy(arena);
    free(contents);

    return pass;
}

static size_t test_run_directory(const char *path, TestStats *stats) {
    DIR *directory = opendir(path);

    if (!directory) {
        fprintf(stderr, "ERROR %s: %s\n", path, strerror(errno));
        stats->failed++;
        return 1;
    }

    char **files = NULL;
    size_t file_count = 0;
    size_t file_capacity = 0;

    struct dirent *entry;

    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char *child = test_join_path(path, entry->d_name);

        if (!child) {
            closedir(directory);
            return 1;
        }

        if (test_is_directory(child)) {
            free(child);
            continue;
        }

        if (!test_has_extension(child, ".test")) {
            free(child);
            continue;
        }

        if (file_count == file_capacity) {
            size_t new_capacity = file_capacity ? file_capacity * 2 : 8;
            char **new_files = realloc(files, new_capacity * sizeof(*files));

            if (!new_files) {
                free(child);

                for (size_t i = 0; i < file_count; i++)
                    free(files[i]);

                free(files);
                closedir(directory);

                return 1;
            }

            files = new_files;
            file_capacity = new_capacity;
        }

        files[file_count++] = child;
    }

    closedir(directory);

    qsort(files, file_count, sizeof(*files), test_path_compare);

    for (size_t i = 0; i < file_count; i++) {
        test_run_file(files[i], stats);
        free(files[i]);
    }

    free(files);

    return file_count;
}

static size_t test_run_path(const char *path, TestStats *stats) {
    if (test_is_directory(path))
        return test_run_directory(path, stats);

    if (test_has_extension(path, ".test")) {
        test_run_file(path, stats);
        return 1;
    }

    fprintf(stderr, "ERROR %s: not a .test file or directory\n", path);
    stats->failed++;

    return 1;
}

int main(int argc, char **argv) {
    TestStats stats = {0};

    if (argc == 1) {
        test_run_path("tests", &stats);
    } else {
        for (int i = 1; i < argc; i++)
            test_run_path(argv[i], &stats);
    }

    printf("\n\x1b[32m%zu\x1b[0m passed, \x1b[31m%zu\x1b[0m failed\n", stats.passed, stats.failed);

    return stats.failed != 0;
}