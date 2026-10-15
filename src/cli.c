#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

#ifndef CODA_VERSION
#define CODA_VERSION "development build"
#endif

static CliParseResult cli_error(char *buffer, size_t capacity, const char *format, ...) {
    if (buffer != NULL && capacity != 0) {
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, capacity, format, args);
        va_end(args);
    }

    return CLI_PARSE_ERROR;
}

static bool cli_append_include_path(CliOptions *options, const char *path) {
    const char **paths = realloc(options->include_paths, (options->include_path_count + 1) * sizeof(*paths));
    if (paths == NULL)
        return false;

    options->include_paths = paths;
    options->include_paths[options->include_path_count++] = path;
    return true;
}

static bool cli_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool cli_identifier_continue(char c) {
    return cli_identifier_start(c) || (c >= '0' && c <= '9');
}

static bool cli_valid_path(const char *path, size_t length) {
    if (path == NULL || length == 0)
        return false;

    size_t i = 0;
    while (i < length) {
        if (!cli_identifier_start(path[i]))
            return false;
        i++;
        while (i < length && cli_identifier_continue(path[i]))
            i++;
        if (i == length)
            return true;
        if (i + 1 >= length || path[i] != ':' || path[i + 1] != ':')
            return false;
        i += 2;
        if (i == length)
            return false;
    }

    return false;
}

static char *cli_copy_string(const char *text, size_t length) {
    char *copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

/* --include accepts MODULE or MODULE:ALIAS; :: remains a path separator. */
static bool cli_parse_include_spec(const char *spec, CliInclude *include) {
    if (spec == NULL || include == NULL)
        return false;

    const char *alias_separator = NULL;
    for (const char *p = spec; *p != '\0'; p++) {
        if (*p != ':')
            continue;
        if (p[1] == ':') {
            p++;
            continue;
        }
        alias_separator = p;
        break;
    }

    size_t module_length = alias_separator != NULL ? (size_t)(alias_separator - spec) : strlen(spec);
    const char *alias = alias_separator != NULL ? alias_separator + 1 : NULL;
    if (!cli_valid_path(spec, module_length) ||
        (alias != NULL && !cli_valid_path(alias, strlen(alias))))
        return false;

    char *module_copy = cli_copy_string(spec, module_length);
    char *alias_copy = alias != NULL ? cli_copy_string(alias, strlen(alias)) : NULL;
    if (module_copy == NULL || (alias != NULL && alias_copy == NULL)) {
        free(module_copy);
        free(alias_copy);
        return false;
    }

    *include = (CliInclude){.module_path = module_copy, .alias_path = alias_copy};
    return true;
}

static bool cli_append_include(CliOptions *options, const char *spec) {
    CliInclude include;
    if (!cli_parse_include_spec(spec, &include))
        return false;

    CliInclude *includes = realloc(options->includes, (options->include_count + 1) * sizeof(*includes));
    if (includes == NULL) {
        free(include.module_path);
        free(include.alias_path);
        return false;
    }

    options->includes = includes;
    options->includes[options->include_count++] = include;
    return true;
}

static bool cli_append_run_arg(CliOptions *options, const char *arg) {
    const char **args = realloc(options->run_args, (options->run_arg_count + 1) * sizeof(*args));
    if (args == NULL)
        return false;

    options->run_args = args;
    options->run_args[options->run_arg_count++] = arg;
    return true;
}

static CliParseResult cli_parse_args_impl(int argc, char **argv, CliOptions *options, char *error, size_t error_capacity) {
    if (error != NULL && error_capacity != 0)
        error[0] = '\0';

    if (options == NULL)
        return cli_error(error, error_capacity, "missing options destination");

    *options = (CliOptions){0};

    if (argc < 1 || argv == NULL)
        return cli_error(error, error_capacity, "invalid argument vector");

    bool options_ended = false;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg == NULL)
            return cli_error(error, error_capacity, "invalid null argument");

        if (!options_ended && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0))
            return CLI_PARSE_HELP;

        if (!options_ended && strcmp(arg, "--version") == 0)
            return CLI_PARSE_VERSION;

        if (!options_ended && strcmp(arg, "--") == 0) {
            options_ended = true;
            continue;
        }

        if (!options_ended && strcmp(arg, "--run") == 0) {
            options->run = true;
            continue;
        }

        if (!options_ended && strcmp(arg, "--include") == 0) {
            if (i + 1 >= argc || argv[i + 1] == NULL || argv[i + 1][0] == '\0' || argv[i + 1][0] == '-')
                return cli_error(error, error_capacity, "option '--include' requires MODULE[:ALIAS]");
            const char *spec = argv[++i];
            if (!cli_append_include(options, spec))
                return cli_error(error, error_capacity, "invalid module include '%s' (expected MODULE[:ALIAS])", spec);
            continue;
        }

        if (!options_ended && strncmp(arg, "--include=", 10) == 0) {
            if (arg[10] == '\0')
                return cli_error(error, error_capacity, "option '--include' requires MODULE[:ALIAS]");
            if (!cli_append_include(options, arg + 10))
                return cli_error(error, error_capacity, "invalid module include '%s' (expected MODULE[:ALIAS])", arg + 10);
            continue;
        }

        if (!options_ended && (strcmp(arg, "-I") == 0 || strcmp(arg, "--include-path") == 0 || strcmp(arg, "--module-path") == 0)) {
            if (i + 1 >= argc || argv[i + 1] == NULL || argv[i + 1][0] == '\0' || argv[i + 1][0] == '-')
                return cli_error(error, error_capacity, "option '%s' requires an include directory", arg);

            if (!cli_append_include_path(options, argv[++i]))
                return cli_error(error, error_capacity, "out of memory while recording include paths");
            continue;
        }

        if (!options_ended && strncmp(arg, "-I", 2) == 0 && arg[2] != '\0') {
            if (!cli_append_include_path(options, arg + 2))
                return cli_error(error, error_capacity, "out of memory while recording include paths");
            continue;
        }

        if (!options_ended && strncmp(arg, "--include-path=", 15) == 0) {
            if (arg[15] == '\0')
                return cli_error(error, error_capacity, "option '--include-path' requires an include directory");
            if (!cli_append_include_path(options, arg + 15))
                return cli_error(error, error_capacity, "out of memory while recording include paths");
            continue;
        }

        if (!options_ended && strncmp(arg, "--module-path=", 14) == 0) {
            if (arg[14] == '\0')
                return cli_error(error, error_capacity, "option '--module-path' requires an include directory");
            if (!cli_append_include_path(options, arg + 14))
                return cli_error(error, error_capacity, "out of memory while recording include paths");
            continue;
        }

        if (!options_ended && strcmp(arg, "--target") == 0) {
            if (options->target_name != NULL)
                return cli_error(error, error_capacity, "target specified more than once");

            if (i + 1 >= argc || argv[i + 1] == NULL || argv[i + 1][0] == '\0' || argv[i + 1][0] == '-')
                return cli_error(error, error_capacity, "option '--target' requires a target name");

            options->target_name = argv[++i];
            continue;
        }

        if (!options_ended && strcmp(arg, "--dump-ast") == 0) {
            options->dump_ast = true;
            continue;
        }

        if (!options_ended && strcmp(arg, "--dump-hir") == 0) {
            options->dump_hir = true;
            continue;
        }

        if (!options_ended && strcmp(arg, "--dump-lir") == 0) {
            options->dump_lir = true;
            continue;
        }

        if (!options_ended && (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0)) {
            if (options->output_path != NULL)
                return cli_error(error, error_capacity, "output path specified more than once");

            if (i + 1 >= argc || argv[i + 1] == NULL || argv[i + 1][0] == '\0')
                return cli_error(error, error_capacity, "option '%s' requires an output path", arg);

            options->output_path = argv[++i];
            continue;
        }

        if (!options_ended && arg[0] == '-' && arg[1] != '\0')
            return cli_error(error, error_capacity, "unknown option '%s'", arg);

        if (arg[0] == '\0')
            return cli_error(error, error_capacity, "input path cannot be empty");

        if (options->input_path != NULL && options_ended && options->run) {
            if (!cli_append_run_arg(options, arg))
                return cli_error(error, error_capacity, "out of memory while recording program arguments");
            continue;
        }

        if (options->input_path != NULL)
            return cli_error(error, error_capacity, "multiple input files specified ('%s' and '%s')", options->input_path, arg);

        options->input_path = arg;
    }

    if (options->input_path == NULL)
        return cli_error(error, error_capacity, "missing input file");

    if (options->run && options->output_path != NULL)
        return cli_error(error, error_capacity, "--run cannot be combined with -o/--output");

    return CLI_PARSE_OK;
}

CliParseResult cli_parse_args(int argc, char **argv, CliOptions *options, char *error, size_t error_capacity) {
    CliParseResult result = cli_parse_args_impl(argc, argv, options, error, error_capacity);
    if (result != CLI_PARSE_OK)
        cli_options_destroy(options);
    return result;
}

void cli_options_destroy(CliOptions *options) {
    if (options == NULL)
        return;

    free(options->include_paths);
    options->include_paths = NULL;
    options->include_path_count = 0;

    for (size_t i = 0; i < options->include_count; i++) {
        free(options->includes[i].module_path);
        free(options->includes[i].alias_path);
    }
    free(options->includes);
    options->includes = NULL;
    options->include_count = 0;

    free(options->run_args);
    options->run_args = NULL;
    options->run_arg_count = 0;
}

void cli_print_help(FILE *stream, const char *program_name) {
    if (program_name == NULL || program_name[0] == '\0')
        program_name = "codac";

    fprintf(stream, "Usage: %s [OPTIONS] INPUT\n", program_name);
    fprintf(stream, "Compile a Coda source file to assembly.\n\n");
    fprintf(stream, "Options:\n");
    fprintf(stream, "  -o PATH, --output PATH  Write output to PATH (default: a.s)\n");
    fprintf(stream, "  --target TARGET         Compile for TARGET (default: native host target)\n");
    fprintf(stream, "  -I DIR, -IDIR           Add a directory to the module search path (repeatable)\n");
    fprintf(stream, "      --include-path DIR  Long form of -I\n");
    fprintf(stream, "      --module-path DIR   Alias for --include-path\n");
    fprintf(stream, "      --include MODULE[:ALIAS]  Force a module include (repeatable; use :_ for wildcard imports)\n");
    fprintf(stream, "      --run               Compile to a temporary executable and run it\n");
    fprintf(stream, "  -h, --help              Display this help and exit\n");
    fprintf(stream, "  --dump-ast              Print the AST after successful compilation\n");
    fprintf(stream, "  --dump-hir              Print the HIR after successful compilation\n");
    fprintf(stream, "  --dump-lir              Print the LIR after successful compilation\n");
    fprintf(stream, "  --version               Display compiler version and exit\n");
    fprintf(stream, "  --                      End compiler options; with --run, pass following arguments to the program\n");
}

void cli_print_version(FILE *stream) {
    fprintf(stream, "codac %s\n", CODA_VERSION);
}

