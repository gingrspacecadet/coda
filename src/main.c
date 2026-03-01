#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "print.h"
#include "arena.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;

    // Setup the source
    FILE *src_file = fopen(argv[1], "r");
    fseek(src_file, 0, SEEK_END);
    size src_size = ftell(src_file);
    rewind(src_file);
    char *buf = malloc(src_size + 1);
    fread(buf, 1, src_size, src_file);
    buf[src_size] = '\0';

    // lex it
    TokenBuffer tokens = lexer(buf);
    // pretty_print_tokens(&tokens);
    Arena *a = arena_create();

    // parse it
    Module *module = parser(&tokens, a);
    pretty_print_module(module);

    arena_destroy(a);
    return 0;
}
