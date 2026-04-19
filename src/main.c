#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "arena.h"

// this is completely unsafe lmao
char *read_file(char *path) {
    FILE *f = fopen(path, "r");
    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(fsize + 1);
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

    token_array tokens = lex(&lexer);

    Parser parser = {
        .arena = lexer.arena,
        .index = 0,
        .lexer = lexer,
        .tokens = tokens,
    };

    Module *module = parse_module(&parser);
    
    printf("Module name %.*s\n", module->name.length, module->name.data);
}