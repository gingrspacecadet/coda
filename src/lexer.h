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
    MUT,
    IF,
    ELSE,
    FOR,
    WHILE,
    
    // literals
    STRING_LIT,
    CHAR_LIT,
    NUMBER,

    // Types
    CHAR, STRING,
    INT8, INT16, INT32, INT64,
    UINT8, UINT16, UINT32, UINT64,
    _NULL,

    // punctuation
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACK,
    RBRACK,
    COLON,
    SEMICOLON,
    DOUBLECOLON,
    COMMA,
    DOT,
    AMP,
    QUESTION,
    PERCENT,
    
    // operations
    PLUS,
    MINUS,
    STAR,
    SLASH,
    RSHIFT,
    LSHIFT,
    OR,
    AND,

    // comparisons
    GT,
    LT,
    EQEQ,
    NEQ,
    GE,
    LE,

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

typedef struct {
    const char const *source;
    size source_index;
    TokenBuffer tokens;
} LexerContext;

TokenBuffer lexer(char *src);