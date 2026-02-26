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
    BREAK,
    CONTINUE,
    
    // literals
    STRING_LIT,
    CHAR_LIT,
    NUMBER,
    TRUE,
    FALSE,

    // Types
    CHAR, STRING,
    INT, INT8, INT16, INT32, INT64,
    UINT, UINT8, UINT16, UINT32, UINT64,
    BOOL,
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
    BANG,
    CARET,
    PIPE,
    
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
    GREATER,
    LESS,
    EQ_EQ,
    BANG_EQ,
    GREATER_EQ,
    LESS_EQ,
    AND_AND,
    OR_OR,

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