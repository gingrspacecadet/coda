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
    ATTR,

    // Types
    STRING,
    CHAR,
    INT8, INT16, INT32, INT64,
    UINT8, UINT16, UINT32, UINT64,
    _NULL,

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
    AMP,
    QUESTION,
    
    // operations
    PLUS,
    MINUS,
    STAR,
    DIV,
    RSHIFT,
    LSHIFT,

    // comparisons
    GREATER,
    LESS,
    EQ_EQ,
    BANG_EQ,
    GREATER_EQ,
    LESS_EQ,

    //Assignment
    EQ,
    PLUS_EQ,
    MINUS_EQ,
    STAR_EQ,
    DIV_EQ,
    RSHIFT_EQ,
    LSHIFT_EQ,

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