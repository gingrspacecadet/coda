#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "string.h"

typedef struct {
    String path;
    String contents;
} Source;

typedef struct {
    Source *source;
    size_t offset, length;
    size_t line, col;
} Span;

typedef enum {
    TOKEN_EOF,
    TOKEN_INVALID,

    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_FLOAT,
    TOKEN_STRING,
    TOKEN_CHAR,

    TOKEN_KW_MODULE,
    TOKEN_KW_INCLUDE,
    TOKEN_KW_TYPE,
    TOKEN_KW_STRUCT,
    TOKEN_KW_UNION,
    TOKEN_KW_ENUM,
    TOKEN_KW_FN,
    TOKEN_KW_RETURN,
    TOKEN_KW_IF,
    TOKEN_KW_ELSE,
    TOKEN_KW_MATCH,
    TOKEN_KW_FOR,
    TOKEN_KW_WHILE,
    TOKEN_KW_BREAK,
    TOKEN_KW_CONTINUE,
    TOKEN_KW_DEFER,
    TOKEN_KW_MUT,
    TOKEN_KW_TRUE,
    TOKEN_KW_FALSE,
    TOKEN_KW_NONE,

    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,

    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_COLON,
    TOKEN_DOUBLE_COLON,
    TOKEN_DOT,

    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,

    TOKEN_PLUS_PLUS,
    TOKEN_MINUS_MINUS,

    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS_EQUAL,
    TOKEN_STAR_EQUAL,
    TOKEN_SLASH_EQUAL,
    TOKEN_PERCENT_EQUAL,

    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG,
    TOKEN_BANG_EQUAL,

    TOKEN_LT,
    TOKEN_LT_EQUAL,
    TOKEN_GT,
    TOKEN_GT_EQUAL,

    TOKEN_LSHIFT,
    TOKEN_RSHIFT,
    TOKEN_LSHIFT_EQUAL,
    TOKEN_RSHIFT_EQUAL,

    TOKEN_AMPERSAND,
    TOKEN_AMPERSAND_EQUAL,
    TOKEN_LOGICAL_AND,

    TOKEN_PIPE,
    TOKEN_PIPE_EQUAL,
    TOKEN_LOGICAL_OR,

    TOKEN_CARET,
    TOKEN_CARET_EQUAL,

    TOKEN_TILDE,

    TOKEN_BANG_AMPERSAND,
    TOKEN_BANG_AMPERSAND_EQUAL,

    TOKEN_TILDE_PIPE,
    TOKEN_TILDE_PIPE_EQUAL,

    TOKEN_QUESTION,
    TOKEN_ARROW,

    TOKEN_HASH,
    TOKEN_DOLLAR,
    TOKEN_AT,
} TokenKind;

typedef struct {
    TokenKind kind;
    String text;
    Span span;
} Token;

typedef enum {
    LEXER_ERROR_INVALID_CHARACTER,
    LEXER_ERROR_UNTERMINATED_COMMENT,
    LEXER_ERROR_UNTERMINATED_STRING,
    LEXER_ERROR_UNTERMINATED_CHAR,
    LEXER_ERROR_INVALID_ESCAPE,
    LEXER_ERROR_INVALID_CHARACTER_LITERAL,
    LEXER_ERROR_INVALID_NUMBER,
} LexerErrorKind;

typedef void (*LexerErrorFn)(
    void *userdata,
    LexerErrorKind kind,
    Span span,
    String message,
);

typedef struct {
    Source *source;

    size_t pos;
    size_t line, col;

    LexerErrorFn error;
    void *error_userdata;
} Lexer;

static inline void lexer_init(Lexer *lexer, Source *source, LexerErrorFn error, void *error_userdata) {
    *lexer = (Lexer) {
        .source = source,
        .pos = 0,
        .line = 1,
        .column = 1,
        .error = error,
        .error_userdata = error_userdata,
    };
}

Token lexer_next(Lexer *lexer);

#endif