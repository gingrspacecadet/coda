#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer lexer;
    token_array tokens;
    size_t index;
    
    Arena *arena;
} Parser;

#endif