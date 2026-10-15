#ifndef CODA_CLI_H
#define CODA_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
    CLI_PARSE_OK,
    CLI_PARSE_HELP,
    CLI_PARSE_VERSION,
    CLI_PARSE_ERROR,
} CliParseResult;

typedef struct {
    char *module_path;
    char *alias_path;
} CliInclude;

typedef struct {
    const char *input_path;
    const char *output_path;
    const char *target_name;
    const char **include_paths;
    size_t include_path_count;
    CliInclude *includes;
    size_t include_count;
    const char **run_args;
    size_t run_arg_count;
    bool run;
    bool dump_ast;
    bool dump_hir;
    bool dump_lir;
} CliOptions;

CliParseResult cli_parse_args(int argc, char **argv, CliOptions *options, char *error, size_t error_capacity);
void cli_options_destroy(CliOptions *options);
void cli_print_help(FILE *stream, const char *program_name);
void cli_print_version(FILE *stream);

#endif

