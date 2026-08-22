#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include "lexer.h"
#include "ast.h"
#include "arena.h"

typedef struct {
    Lexer *lexer;
    Arena *arena;

    Token current;
    Token previous;

    Diags *diags;
} Parser;

static inline void parser_init(Parser *p, Lexer *lexer, Arena *arena) {
    *p = (Parser) {
        .lexer = lexer,
        .arena = arena,
        .previous = (Token) {0},
        .current = lexer_next(lexer),
        .diags = lexer->diags,
    };
}

Module *parser_parse_module(Parser *p);

#endif