#ifndef TOKEN_H
#define TOKEN_H

#include "source.h"

#define TOKEN_LIST       \
                         \
    /* Keywords! */      \
    X(TK_KW_MODULE)      \
    X(TK_KW_INCLUDE)     \
    X(TK_KW_FN)          \
    X(TK_KW_CONSTRAINT)  \
    X(TK_KW_RETURN)      \
    X(TK_KW_STRUCT)      \
    X(TK_KW_UNION)       \
    X(TK_KW_DEFER)       \
    X(TK_KW_MATCH)       \
    X(TK_KW_ENUM)        \
    X(TK_KW_TYPE)        \
    X(TK_KW_MUT)         \
    X(TK_KW_IF)          \
    X(TK_KW_ELSE)        \
    X(TK_KW_FOR)         \
    X(TK_KW_WHILE)       \
    X(TK_KW_BREAK)       \
    X(TK_KW_CONTINUE)    \
    X(TK_KW_TRUE)        \
    X(TK_KW_FALSE)       \
    X(TK_KW_NULL)        \
                         \
    /* Operators */      \
    X(TK_PLUS)           \
    X(TK_MINUS)          \
    X(TK_SLASH)          \
    X(TK_STAR)           \
    X(TK_AMP)            \
    X(TK_AMP_AMP)        \
    X(TK_PIPE)           \
    X(TK_PIPE_PIPE)      \
    X(TK_CARET)          \
    X(TK_BANG)           \
    X(TK_TILDE)          \
    X(TK_PERCENT)        \
    X(TK_LT)             \
    X(TK_GT)             \
    X(TK_EQ)             \
    X(TK_SHL)            \
    X(TK_SHR)            \
    X(TK_BANG_AMP)       \
    X(TK_TILDE_AMP)      \
    X(TK_BANG_PIPE)      \
    X(TK_TILDE_PIPE)     \
    X(TK_BANG_CARET)     \
    X(TK_TILDE_CARET)    \
    X(TK_PLUS_EQ)        \
    X(TK_MINUS_EQ)       \
    X(TK_SLASH_EQ)       \
    X(TK_STAR_EQ)        \
    X(TK_AMP_EQ)         \
    X(TK_AMP_AMP_EQ)     \
    X(TK_PIPE_EQ)        \
    X(TK_PIPE_PIPE_EQ)   \
    X(TK_CARET_EQ)       \
    X(TK_BANG_EQ)        \
    X(TK_TILDE_EQ)       \
    X(TK_PERCENT_EQ)     \
    X(TK_LT_EQ)          \
    X(TK_GT_EQ)          \
    X(TK_EQ_EQ)          \
    X(TK_SHL_EQ)         \
    X(TK_SHR_EQ)         \
    X(TK_BANG_AMP_EQ)    \
    X(TK_TILDE_AMP_EQ)   \
    X(TK_BANG_PIPE_EQ)   \
    X(TK_TILDE_PIPE_EQ)  \
    X(TK_BANG_CARET_EQ)  \
    X(TK_TILDE_CARET_EQ) \
                         \
    /* Punctuation */    \
    X(TK_LPAREN)         \
    X(TK_RPAREN)         \
    X(TK_LBRACE)         \
    X(TK_RBRACE)         \
    X(TK_LBRACK)         \
    X(TK_RBRACK)         \
    X(TK_COLON)          \
    X(TK_COLON_COLON)    \
    X(TK_SEMICOLON)      \
    X(TK_DOT)            \
    X(TK_COMMA)          \
    X(TK_POUND)          \
    X(TK_DOLLAR)         \
    X(TK_AT)             \
    X(TK_QUERY)          \
                         \
    X(TK_IDENT)          \
    X(TK_NUMBER)         \
    X(TK_STRING)         \
    X(TK_CHAR)           \
                         \
    X(TK_EOF)            \
    X(TK_ERROR)          \

typedef enum {
#define X(tok) tok,
    TOKEN_LIST
#undef X
    TK_COUNT,
} TokenType;

static const char *TokenTypeNames[] = {
#define X(tok) #tok,
    TOKEN_LIST
#undef X
};

typedef struct {
    TokenType type;
    Span span;
} Token;

#endif