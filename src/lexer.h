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
    CHAR,

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
    SHRIGHT,
    SHLEFT,
    EQ,

    // comparisons
    GREATER,
    LESS,
    EQ_EQ,
    BANG_EQ,
    GREATER_EQ,
    LESS_EQ,

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