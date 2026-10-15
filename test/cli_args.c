#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "test.h"

static bool cli_test_parse(const char *name, int argc, char **argv, CliParseResult expected_result, const char *expected_input, const char *expected_output, const char *expected_error) {
    CliOptions options;
    char error[256] = {0};
    CliParseResult result = cli_parse_args(argc, argv, &options, error, sizeof(error));

    if (result != expected_result) {
        fprintf(stderr, "CLI parser test '%s': expected result %d, got %d\n", name, expected_result, result);
        cli_options_destroy(&options);
        return false;
    }

    if (expected_result == CLI_PARSE_OK &&
        ((expected_input == NULL) != (options.input_path == NULL) ||
         (expected_output == NULL) != (options.output_path == NULL) ||
         (expected_input != NULL && strcmp(expected_input, options.input_path) != 0) ||
         (expected_output != NULL && strcmp(expected_output, options.output_path) != 0))) {
        fprintf(stderr, "CLI parser test '%s': parsed paths do not match expectations\n", name);
        cli_options_destroy(&options);
        return false;
    }

    if (expected_error != NULL && strstr(error, expected_error) == NULL) {
        fprintf(stderr, "CLI parser test '%s': expected error containing '%s', got '%s'\n", name, expected_error, error);
        cli_options_destroy(&options);
        return false;
    }

    cli_options_destroy(&options);
    return true;
}

static bool cli_test_target_flag(void) {
    char *argv[] = {"codac", "--target", "x86_64-linux", "main.coda"};
    CliOptions options;
    char error[256] = {0};
    CliParseResult result = cli_parse_args(4, argv, &options, error, sizeof(error));

    if (result != CLI_PARSE_OK || options.target_name == NULL || strcmp(options.target_name, "x86_64-linux") != 0 ||
        options.input_path == NULL || strcmp(options.input_path, "main.coda") != 0) {
        fprintf(stderr, "CLI parser test 'target flag': parsing failed or target was not preserved (%s)\n", error);
        return false;
    }

    cli_options_destroy(&options);
    return true;
}

static bool cli_test_dump_flags(void) {
    char *argv[] = {"codac", "--dump-ast", "main.coda", "--dump-hir", "--dump-lir"};
    CliOptions options;
    char error[256] = {0};
    CliParseResult result = cli_parse_args(5, argv, &options, error, sizeof(error));

    if (result != CLI_PARSE_OK || options.input_path == NULL || strcmp(options.input_path, "main.coda") != 0 ||
        !options.dump_ast || !options.dump_hir || !options.dump_lir) {
        fprintf(stderr, "CLI parser test 'dump flags': parsing failed or flags were not preserved (%s)\n", error);
        return false;
    }

    cli_options_destroy(&options);
    return true;
}

static bool cli_test_include_paths(void) {
    char *argv[] = {"codac", "-I", "modules", "-Ivendor", "--include-path=third_party", "--module-path", "generated", "main.coda"};
    CliOptions options;
    char error[256] = {0};
    CliParseResult result = cli_parse_args(8, argv, &options, error, sizeof(error));
    const char *expected[] = {"modules", "vendor", "third_party", "generated"};

    if (result != CLI_PARSE_OK || options.include_path_count != sizeof(expected) / sizeof(*expected)) {
        fprintf(stderr, "CLI parser test 'include paths': wrong result or path count (%s)\n", error);
        cli_options_destroy(&options);
        return false;
    }

    for (size_t i = 0; i < options.include_path_count; i++) {
        if (strcmp(options.include_paths[i], expected[i]) != 0) {
            fprintf(stderr, "CLI parser test 'include paths': path %zu does not match\n", i);
            cli_options_destroy(&options);
            return false;
        }
    }

    cli_options_destroy(&options);
    return true;
}

static bool cli_test_forced_includes(void) {
    char *argv[] = {"codac", "--include", "std::prelude:_", "--include=app::config:config", "--include", "std::fmt", "main.coda"};
    CliOptions options;
    char error[256] = {0};
    CliParseResult result = cli_parse_args(7, argv, &options, error, sizeof(error));
    bool valid = result == CLI_PARSE_OK && options.include_count == 3 &&
        strcmp(options.includes[0].module_path, "std::prelude") == 0 &&
        strcmp(options.includes[0].alias_path, "_") == 0 &&
        strcmp(options.includes[1].module_path, "app::config") == 0 &&
        strcmp(options.includes[1].alias_path, "config") == 0 &&
        strcmp(options.includes[2].module_path, "std::fmt") == 0 &&
        options.includes[2].alias_path == NULL;
    if (!valid)
        fprintf(stderr, "CLI parser test 'forced includes': parsing failed (%s)\n", error);
    cli_options_destroy(&options);
    return valid;
}

static bool cli_test_run_flag(void) {
    char *argv[] = {"codac", "--run", "main.coda", "--", "one", "two"};
    CliOptions options;
    char error[256] = {0};
    CliParseResult result = cli_parse_args(6, argv, &options, error, sizeof(error));
    bool valid = result == CLI_PARSE_OK && options.run && options.input_path != NULL &&
        strcmp(options.input_path, "main.coda") == 0 && options.run_arg_count == 2 &&
        strcmp(options.run_args[0], "one") == 0 && strcmp(options.run_args[1], "two") == 0;
    if (!valid)
        fprintf(stderr, "CLI parser test 'run flag': parsing failed (%s)\n", error);
    cli_options_destroy(&options);
    return valid;
}

bool test_cli_arguments(void) {
    char *basic[] = {"codac", "main.coda"};
    char *output_before[] = {"codac", "-o", "main.s", "main.coda"};
    char *output_after[] = {"codac", "main.coda", "--output", "main.s"};
    char *dash_input[] = {"codac", "--", "-main.coda"};
    char *help[] = {"codac", "--help"};
    char *short_help[] = {"codac", "-h"};
    char *version[] = {"codac", "--version"};
    char *missing_input[] = {"codac"};
    char *missing_output[] = {"codac", "main.coda", "-o"};
    char *missing_include[] = {"codac", "-I"};
    char *empty_include[] = {"codac", "--include-path=", "main.coda"};
    char *missing_forced_include[] = {"codac", "--include"};
    char *invalid_forced_include[] = {"codac", "--include", "std::::prelude", "main.coda"};
    char *run_with_output[] = {"codac", "--run", "-o", "out.s", "main.coda"};
    char *missing_target[] = {"codac", "--target"};
    char *option_as_target[] = {"codac", "--target", "--dump-hir", "main.coda"};
    char *duplicate_target[] = {"codac", "--target", "x86_64-linux", "--target", "aarch64-linux", "main.coda"};
    char *unknown_option[] = {"codac", "--not-an-option", "main.coda"};
    char *multiple_inputs[] = {"codac", "one.coda", "two.coda"};
    char *duplicate_output[] = {"codac", "-o", "one.s", "--output", "two.s", "main.coda"};

    return cli_test_parse("positional input", 2, basic, CLI_PARSE_OK, "main.coda", NULL, NULL) &&
           cli_test_parse("output before input", 4, output_before, CLI_PARSE_OK, "main.coda", "main.s", NULL) &&
           cli_test_parse("output after input", 4, output_after, CLI_PARSE_OK, "main.coda", "main.s", NULL) &&
           cli_test_parse("end of options", 3, dash_input, CLI_PARSE_OK, "-main.coda", NULL, NULL) &&
           cli_test_parse("help", 2, help, CLI_PARSE_HELP, NULL, NULL, NULL) &&
           cli_test_parse("short help", 2, short_help, CLI_PARSE_HELP, NULL, NULL, NULL) &&
           cli_test_parse("version", 2, version, CLI_PARSE_VERSION, NULL, NULL, NULL) &&
           cli_test_parse("missing input", 1, missing_input, CLI_PARSE_ERROR, NULL, NULL, "missing input file") &&
           cli_test_parse("missing output value", 3, missing_output, CLI_PARSE_ERROR, NULL, NULL, "requires an output path") &&
           cli_test_parse("missing include value", 2, missing_include, CLI_PARSE_ERROR, NULL, NULL, "requires an include directory") &&
           cli_test_parse("empty include value", 3, empty_include, CLI_PARSE_ERROR, NULL, NULL, "requires an include directory") &&
           cli_test_parse("missing forced include", 2, missing_forced_include, CLI_PARSE_ERROR, NULL, NULL, "requires MODULE[:ALIAS]") &&
           cli_test_parse("invalid forced include", 4, invalid_forced_include, CLI_PARSE_ERROR, NULL, NULL, "invalid module include") &&
           cli_test_parse("run output conflict", 5, run_with_output, CLI_PARSE_ERROR, NULL, NULL, "cannot be combined") &&
           cli_test_parse("missing target value", 2, missing_target, CLI_PARSE_ERROR, NULL, NULL, "requires a target name") &&
           cli_test_parse("option is not target value", 4, option_as_target, CLI_PARSE_ERROR, NULL, NULL, "requires a target name") &&
           cli_test_parse("duplicate target", 6, duplicate_target, CLI_PARSE_ERROR, NULL, NULL, "more than once") &&
           cli_test_parse("unknown option", 3, unknown_option, CLI_PARSE_ERROR, NULL, NULL, "unknown option") &&
           cli_test_parse("multiple inputs", 3, multiple_inputs, CLI_PARSE_ERROR, NULL, NULL, "multiple input files") &&
           cli_test_parse("duplicate output", 6, duplicate_output, CLI_PARSE_ERROR, NULL, NULL, "more than once") &&
           cli_test_dump_flags() &&
           cli_test_target_flag() &&
           cli_test_include_paths() &&
           cli_test_forced_includes() &&
           cli_test_run_flag();
}

