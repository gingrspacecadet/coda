#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
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

    bool runtime_output_mismatch;
    String runtime_stream;
    bool runtime_exit_mismatch;
    int expected_runtime_exit;
    int actual_runtime_exit;
    char *runtime_message;
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

        case TEST_STOP_RUN:
            return CODA_STAGE_CODEGEN;

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
           stop == TEST_STOP_LIR ||
           stop == TEST_STOP_RUN;
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

    char *contents = arena_calloc(arena, test->source.length + 1);
    if (contents == NULL)
        return false;

    memcpy(contents, test->source.data, test->source.length);
    contents[test->source.length] = '\0';

    Source *source = arena_calloc(arena, sizeof(*source));
    if (source == NULL)
        return false;

    *source = (Source) {
        .path = {
            .data = (char *)path,
            .length = strlen(path),
        },
        .contents = {
            .data = contents,
            .length = test->source.length,
        },
    };

    FILE *capture = NULL;
    int saved_stderr = -1;

    if (!test_capture_stderr_start(&capture, &saved_stderr))
        return false;

    bool compiled = coda_compile(compiler, source, test_coda_stage(test->stop));

    *actual_errors = compiler->diags->diags.len;

    if (!test_capture_stderr_stop(&capture, saved_stderr, diagnostics, diagnostics_length))
        return false;

    return compiled;
}

static bool test_read_stream(FILE *file, char **data, size_t *length) {
    if (fflush(file) != 0 || fseek(file, 0, SEEK_END) != 0)
        return false;

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0)
        return false;

    char *buffer = malloc((size_t)size + 1);
    if (buffer == NULL)
        return false;

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        return false;
    }

    buffer[size] = '\0';
    *data = buffer;
    *length = (size_t)size;
    return true;
}

static void test_runtime_message(TestFailure *failure, const char *message) {
    if (failure->runtime_message != NULL)
        return;

    failure->runtime_message = strdup(message);
}

static bool test_wait_child(pid_t child, int *status) {
    pid_t result;
    do {
        result = waitpid(child, status, 0);
    } while (result < 0 && errno == EINTR);

    return result == child;
}

static bool test_runtime_execute(const char *assembly, size_t assembly_length, const TestCase *test, TestFailure *failure) {
    char directory[] = "/tmp/coda-test-XXXXXX";
    char assembly_path[PATH_MAX] = {0};
    char executable_path[PATH_MAX] = {0};
    bool passed = false;
    FILE *link_stderr = NULL;
    FILE *run_stdout = NULL;
    FILE *run_stderr = NULL;
    char *captured = NULL;
    size_t captured_length = 0;
    char *stdout_data = NULL;
    size_t stdout_length = 0;
    char *stderr_data = NULL;
    size_t stderr_length = 0;

    if (mkdtemp(directory) == NULL) {
        test_runtime_message(failure, "could not create temporary directory for runtime test");
        goto done;
    }

    if (snprintf(assembly_path, sizeof(assembly_path), "%s/test.s", directory) >= (int)sizeof(assembly_path) ||
        snprintf(executable_path, sizeof(executable_path), "%s/test-bin", directory) >= (int)sizeof(executable_path)) {
        test_runtime_message(failure, "temporary runtime-test path is too long");
        goto done;
    }

    FILE *assembly_file = fopen(assembly_path, "wb");
    if (assembly_file == NULL) {
        test_runtime_message(failure, "could not create temporary assembly file");
        goto done;
    }

    bool wrote_assembly = fputs(".globl main\n", assembly_file) >= 0 &&
                          fwrite(assembly, 1, assembly_length, assembly_file) == assembly_length;
    if (fclose(assembly_file) != 0)
        wrote_assembly = false;
    if (!wrote_assembly) {
        test_runtime_message(failure, "could not write generated assembly");
        goto done;
    }

    link_stderr = tmpfile();
    if (link_stderr == NULL) {
        test_runtime_message(failure, "could not capture linker diagnostics");
        goto done;
    }

    pid_t child = fork();
    if (child < 0) {
        test_runtime_message(failure, "could not start linker process");
        goto done;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0)
            dup2(null_fd, STDOUT_FILENO);
        dup2(fileno(link_stderr), STDERR_FILENO);
        execlp("gcc", "gcc", "-no-pie", assembly_path, "-o", executable_path, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    if (!test_wait_child(child, &status)) {
        test_runtime_message(failure, "could not wait for linker process");
        goto done;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (test_read_stream(link_stderr, &captured, &captured_length) && captured_length != 0) {
            failure->runtime_message = captured;
            captured = NULL;
        } else {
            test_runtime_message(failure, "linking generated assembly failed");
        }
        goto done;
    }

    run_stdout = tmpfile();
    run_stderr = tmpfile();
    if (run_stdout == NULL || run_stderr == NULL) {
        test_runtime_message(failure, "could not capture runtime output");
        goto done;
    }

    child = fork();
    if (child < 0) {
        test_runtime_message(failure, "could not start runtime test process");
        goto done;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0)
            dup2(null_fd, STDIN_FILENO);
        dup2(fileno(run_stdout), STDOUT_FILENO);
        dup2(fileno(run_stderr), STDERR_FILENO);
        alarm(5);
        execl(executable_path, executable_path, (char *)NULL);
        _exit(127);
    }

    if (!test_wait_child(child, &status)) {
        test_runtime_message(failure, "could not wait for runtime test process");
        goto done;
    }
    if (WIFEXITED(status)) {
        failure->actual_runtime_exit = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        char message[128];
        snprintf(message, sizeof(message), "runtime test terminated by signal %d (possible timeout)", WTERMSIG(status));
        test_runtime_message(failure, message);
        goto compare_output;
    } else {
        test_runtime_message(failure, "runtime test ended without an exit status");
        goto compare_output;
    }

compare_output:
    if (!test_read_stream(run_stdout, &stdout_data, &stdout_length) ||
        !test_read_stream(run_stderr, &stderr_data, &stderr_length)) {
        test_runtime_message(failure, "could not read captured runtime output");
        goto done;
    }

    if (test->has_stdout && !test_compare_text(test->stdout_text, stdout_data, stdout_length, failure)) {
        failure->runtime = true;
        failure->runtime_output_mismatch = true;
        failure->runtime_stream = (String){.data = "stdout", .length = 6};
    }

    if (test->has_stderr && !failure->runtime_output_mismatch &&
        !test_compare_text(test->stderr_text, stderr_data, stderr_length, failure)) {
        failure->runtime = true;
        failure->runtime_output_mismatch = true;
        failure->runtime_stream = (String){.data = "stderr", .length = 6};
    }

    failure->expected_runtime_exit = test->has_exit_code ? test->exit_code : 0;
    if (!failure->runtime_message && WIFEXITED(status) && failure->actual_runtime_exit != failure->expected_runtime_exit) {
        failure->runtime = true;
        failure->runtime_exit_mismatch = true;
    }

    passed = !failure->runtime;

done:
    if (link_stderr != NULL)
        fclose(link_stderr);
    if (run_stdout != NULL)
        fclose(run_stdout);
    if (run_stderr != NULL)
        fclose(run_stderr);
    free(captured);
    free(stdout_data);
    free(stderr_data);
    unlink(assembly_path);
    unlink(executable_path);
    rmdir(directory);
    if (!passed)
        failure->runtime = true;
    return passed;
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

    if (failure->runtime_output_mismatch) {
        fprintf(stderr, "    runtime %.*s mismatch at line %zu\n", string_fmt(failure->runtime_stream), failure->diff_line);
        test_print_line("expected", failure->expected_line);
        test_print_line("actual", failure->actual_line);
    }

    if (failure->runtime_exit_mismatch)
        fprintf(stderr, "    runtime exit mismatch: expected %d, got %d\n", failure->expected_runtime_exit, failure->actual_runtime_exit);

    if (failure->runtime_message != NULL)
        fprintf(stderr, "    runtime error: %s\n", failure->runtime_message);
    else if (failure->runtime && !failure->runtime_output_mismatch && !failure->runtime_exit_mismatch)
        fprintf(stderr, "    runtime test failed\n");

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
    const TargetInfo *target = target_native();
    if (target == NULL) {
        fprintf(stderr, "native target is unsupported\n");
        arena_destroy(arena);
        failure->runtime = true;
        return false;
    }
    coda_compiler_init(&compiler, arena, &diags, target);

    FILE *assembly_stream = NULL;
    char *assembly_data = NULL;
    size_t assembly_length = 0;
    if (test->stop == TEST_STOP_RUN) {
        assembly_stream = open_memstream(&assembly_data, &assembly_length);
        if (assembly_stream == NULL) {
            test_runtime_message(failure, "could not create assembly stream");
            arena_destroy(arena);
            failure->runtime = true;
            return false;
        }
        compiler.output = assembly_stream;
    }

    bool compiled = test_compile(path, test, &compiler, &failure->actual_errors, &failure->diagnostics_data, &failure->diagnostics_length);
    if (!compiled && test->stop == TEST_STOP_RUN && test->expectation != TEST_EXPECT_FAIL)
        print_diags(&diags);
    if (assembly_stream != NULL && fclose(assembly_stream) != 0) {
        compiled = false;
        test_runtime_message(failure, "could not finish generated assembly stream");
    }

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

    if (!failure->actual_failure && test->stop != TEST_STOP_RUN &&
        !test_compare_stage(test, &compiler, failure))
        pass = false;

    if (test->stop == TEST_STOP_RUN && compiled && !failure->expected_failure &&
        failure->actual_errors == 0 && !failure->runtime_message) {
        if (!test_runtime_execute(assembly_data, assembly_length, test, failure))
            pass = false;
    }

    if (test->has_diagnostics) {
        failure->diagnostics = true;
        pass = false;
    }

    if (test->has_machine || test->has_assembly ||
        (test->stop != TEST_STOP_RUN && (test->has_stdout || test->has_stderr || test->has_exit_code))) {
        failure->runtime = true;
        pass = false;
    }

    if (failure->runtime)
        pass = false;

    free(assembly_data);
    arena_destroy(arena);

    if (pass) {
        free(failure->diagnostics_data);
        failure->diagnostics_data = NULL;
        failure->diagnostics_length = 0;
        free(failure->actual_line.data);
        failure->actual_line.data = NULL;
        failure->actual_line.length = 0;
        free(failure->runtime_message);
        failure->runtime_message = NULL;
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
    free(failure.runtime_message);
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