#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include "cli.h"
#include "print.h"
#include "coda.h"

static bool load_source(const char *path, Source *source, Arena *arena) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        perror(path);
        return false;
    }

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

    char *contents = arena_calloc(arena, (size_t)size + 1);

    if (contents == NULL) {
        fclose(file);
        return false;
    }

    if (fread(contents, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        return false;
    }

    fclose(file);

    contents[size] = '\0';

    *source = (Source) {
        .path = (String) {
            .data = (char *)path,
            .length = strlen(path),
        },
        .contents = (String) {
            .data = contents,
            .length = (size_t)size,
        },
    };

    return true;
}

static bool wait_for_child(pid_t child, int *status) {
    pid_t result;
    do {
        result = waitpid(child, status, 0);
    } while (result < 0 && errno == EINTR);
    return result == child;
}

static int coda_run_program(const char *assembly_path, const char *executable_path, const CliOptions *options) {
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }
    if (child == 0) {
        execlp("cc", "cc", "-no-pie", assembly_path, "-o", executable_path, (char *)NULL);
        perror("cc");
        _exit(127);
    }

    int status = 0;
    if (!wait_for_child(child, &status)) {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "codac: failed to link temporary executable for --run\n");
        return 1;
    }

    char **run_argv = calloc(options->run_arg_count + 2, sizeof(*run_argv));
    if (run_argv == NULL) {
        fprintf(stderr, "codac: out of memory while preparing program arguments\n");
        return 1;
    }
    run_argv[0] = (char *)executable_path;
    for (size_t i = 0; i < options->run_arg_count; i++)
        run_argv[i + 1] = (char *)options->run_args[i];

    child = fork();
    if (child < 0) {
        perror("fork");
        free(run_argv);
        return 1;
    }
    if (child == 0) {
        execv(executable_path, run_argv);
        perror(executable_path);
        _exit(127);
    }

    if (!wait_for_child(child, &status)) {
        perror("waitpid");
        free(run_argv);
        return 1;
    }
    free(run_argv);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}

static bool cli_path_push_part(Arena *arena, Path *path, const char *start, size_t length) {
    if (length == 0)
        return false;

    String ident = {
        .data = arena_calloc(arena, length + 1),
        .length = length,
    };
    if (ident.data == NULL)
        return false;
    memcpy(ident.data, start, length);

    AstName name = {.kind = AST_NAME_IDENT, .ident = ident};
    array_push(&path->parts, &name);
    return true;
}

static bool cli_build_path(Arena *arena, const char *text, Path *path) {
    if (text == NULL || text[0] == '\0')
        return false;

    path->parts = array_create(arena, sizeof(AstName));
    const char *start = text;
    const char *p = text;
    while (true) {
        if (*p == ':' && p[1] == ':') {
            if (!cli_path_push_part(arena, path, start, (size_t)(p - start)))
                return false;
            p += 2;
            start = p;
            continue;
        }
        if (*p == '\0')
            return cli_path_push_part(arena, path, start, (size_t)(p - start));
        p++;
    }
}

static void cleanup_run_files(const char *directory, const char *assembly_path, const char *executable_path) {
    if (assembly_path != NULL && assembly_path[0] != '\0')
        unlink(assembly_path);
    if (executable_path != NULL && executable_path[0] != '\0')
        unlink(executable_path);
    if (directory != NULL && directory[0] != '\0')
        rmdir(directory);
}

int main(int argc, char **argv) {
    CliOptions options;
    char parse_error[256];
    CliParseResult parse_result = cli_parse_args(argc, argv, &options, parse_error, sizeof(parse_error));

    if (parse_result == CLI_PARSE_HELP) {
        cli_print_help(stdout, argv[0]);
        return 0;
    }
    if (parse_result == CLI_PARSE_VERSION) {
        cli_print_version(stdout);
        return 0;
    }
    if (parse_result == CLI_PARSE_ERROR) {
        const char *program_name = argv[0] != NULL ? argv[0] : "codac";
        fprintf(stderr, "%s: %s\n", program_name, parse_error);
        fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
        return 2;
    }

    Arena *arena = arena_create();
    Diags diags = {.arena = arena, .diags = array_create(arena, sizeof(Diag))};
    Source source;
    if (!load_source(options.input_path, &source, arena)) {
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 1;
    }

    const TargetInfo *target = options.target_name != NULL
        ? (strcmp(options.target_name, "native") == 0 ? target_native() : target_builtin(options.target_name))
        : target_native();
    if (target == NULL) {
        if (options.target_name != NULL && strcmp(options.target_name, "native") != 0)
            fprintf(stderr, "unknown target '%s'\n", options.target_name);
        else
            fprintf(stderr, "native target is unsupported\n");
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 1;
    }

    if (options.run && target != target_native()) {
        fprintf(stderr, "codac: --run requires the native target (selected '%s')\n", target->name);
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 2;
    }

    CodaCompiler compiler;
    coda_compiler_init(&compiler, arena, &diags, target);
    if (compiler.backend == NULL) {
        fprintf(stderr, "no code generator is available for target '%s'\n", target->name);
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 1;
    }

    for (size_t i = 0; i < options.include_path_count; i++) {
        String path = {
            .data = (char *)options.include_paths[i],
            .length = strlen(options.include_paths[i]),
        };
        array_push(&compiler.include_paths, &path);
    }

    for (size_t i = 0; i < options.include_count; i++) {
        CliInclude *requested = &options.includes[i];
        AstIncludeDecl include = {0};
        if (!cli_build_path(arena, requested->module_path, &include.path) ||
            (requested->alias_path != NULL && !cli_build_path(arena, requested->alias_path, &include.alias))) {
            fprintf(stderr, "codac: invalid forced include specification\n");
            cli_options_destroy(&options);
            arena_destroy(arena);
            return 2;
        }
        array_push(&compiler.forced_includes, &include);
    }

    char run_directory[] = "/tmp/coda-run-XXXXXX";
    char run_assembly_path[PATH_MAX] = {0};
    char run_executable_path[PATH_MAX] = {0};
    bool run_directory_created = false;
    const char *output_path = options.output_path != NULL ? options.output_path : "a.s";
    if (options.run) {
        if (mkdtemp(run_directory) == NULL) {
            perror("mkdtemp");
            cli_options_destroy(&options);
            arena_destroy(arena);
            return 1;
        }
        run_directory_created = true;
        int asm_length = snprintf(run_assembly_path, sizeof(run_assembly_path), "%s/program.s", run_directory);
        int exe_length = snprintf(run_executable_path, sizeof(run_executable_path), "%s/program", run_directory);
        if (asm_length < 0 || (size_t)asm_length >= sizeof(run_assembly_path) ||
            exe_length < 0 || (size_t)exe_length >= sizeof(run_executable_path)) {
            fprintf(stderr, "codac: failed to construct temporary paths\n");
            cleanup_run_files(run_directory, NULL, NULL);
            cli_options_destroy(&options);
            arena_destroy(arena);
            return 1;
        }
        output_path = run_assembly_path;
    }

    FILE *output = fopen(output_path, "w");
    if (output == NULL) {
        perror(output_path);
        if (run_directory_created)
            cleanup_run_files(run_directory, NULL, NULL);
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 1;
    }

    compiler.output = output;
    if (options.run && fputs(".globl main\n", output) < 0) {
        fclose(output);
        unlink(output_path);
        if (run_directory_created)
            cleanup_run_files(run_directory, run_assembly_path, run_executable_path);
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 1;
    }

    if (!coda_compile(&compiler, &source, CODA_STAGE_CODEGEN)) {
        fclose(output);
        unlink(output_path);
        print_diags(&diags);
        if (run_directory_created)
            cleanup_run_files(run_directory, run_assembly_path, run_executable_path);
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 1;
    }
    if (fclose(output) != 0) {
        perror(output_path);
        if (run_directory_created)
            cleanup_run_files(run_directory, run_assembly_path, run_executable_path);
        cli_options_destroy(&options);
        arena_destroy(arena);
        return 1;
    }

    if (options.dump_ast)
        print_ast_module(stdout, compiler.compilation.ast);
    if (options.dump_hir)
        print_hir_module(stdout, compiler.compilation.hir);
    if (options.dump_lir)
        lir_print(stdout, compiler.compilation.lir);

    cli_options_destroy(&options);
    arena_destroy(arena);

    return 0;
}