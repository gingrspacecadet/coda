#pragma once

#include <stddef.h>

typedef size_t size;

typedef enum {
    MODULE,
    INCLUDE,
    IDENT,
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
    DOUBLECOLON,
    SEMICOLON,
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
    size cap;
    size len;
    size pos;
} TokenBuffer;

TokenBuffer lexer(char *src);