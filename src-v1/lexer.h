#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <errno.h>
#include <unistd.h>
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
    TOKENTYPE_DEFER,
    TOKENTYPE_STRUCT,
    TOKENTYPE_MATCH,
    TOKENTYPE_UNION,
    TOKENTYPE_ENUM,
    TOKENTYPE_TYPE,
    TOKENTYPE_IDENT,
    TOKENTYPE_DOLLAR,
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
    TOKENTYPE_UINT_LIT,
    TOKENTYPE_TRUE,
    TOKENTYPE_FALSE,
    TOKENTYPE_NULL,
    TOKENTYPE_LPAREN,
    TOKENTYPE_RPAREN,
    TOKENTYPE_LBRACE,
    TOKENTYPE_RBRACE,
    TOKENTYPE_LBRACK,
    TOKENTYPE_RBRACK,
    TOKENTYPE_RARROW,
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
    TOKENTYPE_BAD,
    TOKENTYPE_EOF,
} TokenType;

typedef struct {
    size_t start, length;
} Span;

INSTANTIATE(String, string, OPTIONAL_TEMPLATE)

typedef struct {
    String path;
    String contents;
    size_t index;
} Source;

typedef struct {
    TokenType type;
    string_optional value;
    Span span;
    Source *source;
    size_t line, col;
} Token;

// INSTANTIATE(String, string, OPTIONAL_TEMPLATE)
INSTANTIATE(Token, token, ARRAY_TEMPLATE)

typedef struct {
    Source source;
    size_t line, col;

    Arena *arena;
} Lexer;

Token lex_next_token(Lexer *ctx);

static void lexer_free(Lexer *l) {
    arena_destroy(l->arena);
}

static char *read_file(Arena *arena, char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) goto err;

    if (fseek(f, 0, SEEK_END) != 0)
        goto err;

    long size = ftell(f);
    if (size < 0)
        goto err;

    if (fseek(f, 0, SEEK_SET) != 0)
        goto err;

    char *data = arena_alloc(arena, (size_t)size + 1);
    if (!data)
        goto err;

    size_t nread = fread(data, 1, (size_t)size, f);
    if (nread != (size_t)size)
        goto err;

    if (fclose(f) != 0)
        goto err;

    data[size] = '\0';
    return data;

err:
    if (f) fclose(f);
    fprintf(stderr, "Failed to read file %s: %s\n", path, strerror(errno));
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)))
        fprintf(stderr, "CWD %s\n", cwd);
    exit(1);
}

static Lexer lexer_init_from_file(char *path) {
    Arena *a = arena_create();
    return (Lexer){
        .arena = a,
        .source = {
            .path = string_make(path),
            .contents = string_make(read_file(a, path)),
            .index = 0,
        },
        .line = 1,
        .col = 1,
    };
}

static Lexer lexer_init_from_string(char *path) {
    return (Lexer){
        .arena = arena_create(),
        .source = {
            .path = {0},
            .contents = string_make(path),
            .index = 0,
        },
        .line = 1,
        .col = 1,
    };
}

#endif