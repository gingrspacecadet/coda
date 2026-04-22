#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "arena.h"
#include "error.h"

// this is completely unsafe lmao
char *read_file(char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("Failed to open file");
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(fsize + 1);
    if (!data) {
        perror("Failed to allocate memory for file");
        exit(1);
    }
    fread(data, fsize, 1, f);
    fclose(f);
    data[fsize] = 0;
    return data;
}

Source setup_source(char *path) {
    Source source;
    source.path = string_make(path);
    source.index = 0;
    source.contents = string_make(read_file(path));

    return source;
}

int main(void) {
    Lexer lexer = {
        .arena = arena_create(),
        .source = setup_source("test/main.coda")
    };
    error_set_source(lexer.source);

    token_array tokens = lex(&lexer);

    Parser parser = {
        .arena = lexer.arena,
        .index = 0,
        .tokens = tokens,
    };

    Module *module = parse_module(&parser);

    Analyser analyser = analyser_init(module, lexer.arena);
    analyse(&analyser);
}