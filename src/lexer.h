#pragma once

#include <stddef.h>

typedef size_t size;

typedef enum {
    MODULE = 0,
    IDENT,
    SEMI,
    FN,
    INT,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    RETURN,
    NUMBER,
    PLUS,
    MINUS,
    _EOF,
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    size len;
    size line;
    size column;
} Token;

typedef struct {
    Token *data;
    size len;
    int idx;
} TokenBuffer;

TokenBuffer lexer(char *src);