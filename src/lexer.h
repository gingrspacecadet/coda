#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdbool.h>
#include "array.h"
#include "string.h"
#include "optional.h"
#include "arena.h"

typedef enum {
    TOKENTYPE_MODULE,
    TOKENTYPE_INCLUDE,
    TOKENTYPE_FN,
    TOKENTYPE_RETURN,
    TOKENTYPE_STRUCT,
    TOKENTYPE_UNION,
    TOKENTYPE_ENUM,
    TOKENTYPE_TYPE,
    TOKENTYPE_IDENT,
    TOKENTYPE_AT,
    TOKENTYPE_MUT,
    TOKENTYPE_IF,
    TOKENTYPE_ELSE,
    TOKENTYPE_FOR,
    TOKENTYPE_WHILE,
    TOKENTYPE_BREAK,
    TOKENTYPE_CONTINUE,
    TOKENTYPE_STR_LIT,
    TOKENTYPE_CHAR_LIT,
    TOKENTYPE_INT_LIT,    // TODO: more number literals maybe
    TOKENTYPE_TRUE,
    TOKENTYPE_FALSE,
    TOKENTYPE_NULL,
    TOKENTYPE_LPAREN,
    TOKENTYPE_RPAREN,
    TOKENTYPE_LBRACE,
    TOKENTYPE_RBRACE,
    TOKENTYPE_LBRACK,
    TOKENTYPE_RBRACK,
    TOKENTYPE_COLON,
    TOKENTYPE_SEMICOLON,
    TOKENTYPE_DOUBLECOLON,
    TOKENTYPE_COMMA,
    TOKENTYPE_DOT,
    TOKENTYPE_AMP,
    TOKENTYPE_QUESTION,
    TOKENTYPE_NOT,
    TOKENTYPE_CARET,
    TOKENTYPE_PERCENT,
    TOKENTYPE_PIPE,
    TOKENTYPE_PLUS,
    TOKENTYPE_MINUS,
    TOKENTYPE_STAR,
    TOKENTYPE_SLASH,
    TOKENTYPE_SHR,
    TOKENTYPE_SHL,
    TOKENTYPE_GT,
    TOKENTYPE_LT,
    TOKENTYPE_EQEQ,
    TOKENTYPE_NEQ,
    TOKENTYPE_GE,
    TOKENTYPE_LE,
    TOKENTYPE_AMPAMP,
    TOKENTYPE_PIPEPIPE,
    TOKENTYPE_EQ,
    TOKENTYPE_PLUSEQ,
    TOKENTYPE_MINUSEQ,
    TOKENTYPE_STAREQ,
    TOKENTYPE_SLASHEQ,
    TOKENTYPE_SHREQ,
    TOKENTYPE_SHLEQ,
    TOKENTYPE_EOF,
} TokenType;

typedef struct {
    size_t start, length;
} Span;

INSTANTIATE(String, string, OPTIONAL_TEMPLATE)

typedef struct {
    TokenType type;
    string_optional value;
    Span span;
    size_t line, col;
} Token;

INSTANTIATE(Token, token, ARRAY_TEMPLATE)

typedef struct {
    String path;
    String contents;
    size_t index;
} Source;

typedef struct {
    Source source;
    size_t line, col;

    Arena *arena;
} Lexer;

token_array lex(Lexer *lexer);

#endif