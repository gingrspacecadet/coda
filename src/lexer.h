#pragma once

#include <stddef.h>

typedef size_t size;

typedef enum {
    // Keywords
    MODULE,
    INCLUDE,
    FN,
    RETURN,
    IDENT,

    // Types
    INT,
    STRING,

    // punctuation
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    NUMBER,
    COLON,
    SEMICOLON,
    DOUBLECOLON,
    COMMA,
    DOT,
    
    // operations
    PLUS,
    MINUS,
    STAR,
    DIV,

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