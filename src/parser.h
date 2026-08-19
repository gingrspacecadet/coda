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
} Parser;

void parser_init(Parser *p, Lexer *lexer, Arena *arena);
Module *parser_parse_module(Parser *p);

#endif