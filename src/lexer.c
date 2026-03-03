#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "lexer.h"

static char peek(LexerContext *ctx) {
    return ctx->src[ctx->idx];
}

static size line = 1, col = 1;

static char consume(LexerContext *ctx) {
    if (!(ctx->src + ctx->idx)) return '\0';
    if (ctx->src[ctx->idx] == '\n') {
        line++;
        col = 0;
    } else {
        col++;
    }
    return ctx->src[ctx->idx++];
}

typedef struct {
    char *data;
    size len;
    int idx;
} Buffer;

void buffer_push(Buffer *buffer, char c) {
    if (buffer->idx >= buffer->len) return;
    buffer->data[buffer->idx++] = c;
}

Buffer buffer_create(size elements) {
    return (Buffer){.data = calloc(elements, sizeof(char)), .len = elements, .idx = 0};
}

void buffer_clear(Buffer *buffer) {
    memset(buffer->data, 0, buffer->len);
    buffer->idx = 0;
}

TokenBuffer tokenbuffer_create(size elements) {
    return (TokenBuffer){
        .data = calloc(elements, sizeof(Token)),
        .cap = elements,
        .len = 0,
        .pos = 0,
    };
}

void token_push(LexerContext *ctx, Token token) {
    if (ctx->tokens.len >= ctx->tokens.cap) return;
    token.line = line;
    token.column = col - token.len;
    ctx->tokens.data[ctx->tokens.len++] = token;
}

typedef struct {
    char *name;
    TokenType type;
} Keyword;

Keyword keywords[] = {
    {"module", MODULE},
    {"include", INCLUDE},
    {"fn", FN},
    {"if", IF},
    {"else", ELSE},
    {"for", FOR},
    {"while", WHILE},
    {"break", BREAK},
    {"continue", CONTINUE},
    {"return", RETURN},
    {"char", CHAR},
    {"String", STRING},
    {"true", TRUE},
    {"false", FALSE},
    {"int", INT},
    {"int8", INT8},
    {"int16", INT16},
    {"int32", INT32},
    {"int64", INT64},
    {"uint", UINT},
    {"uint8", UINT8},
    {"uint16", UINT16},
    {"uint32", UINT32},
    {"uint64", UINT64},
    {"bool", BOOL},
    {"null", _NULL},
    {"mut", MUT},
};

void lex_ident(LexerContext *ctx, Buffer *buf) {
    for (int i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        Keyword k = keywords[i];
        if (strcmp(buf->data, k.name) == 0) {
            token_push(ctx, (Token){.type = k.type, .len = buf->idx});
            buffer_clear(buf);
            return;
        }
    }

    consume(ctx); ctx->idx--;
    token_push(ctx, (Token){.type = IDENT, .value = strdup(buf->data), .len = buf->idx});
    buffer_clear(buf);
}

static char handle_esc(LexerContext *ctx) {
    if (peek(ctx) == '\\') {
        consume(ctx);  // backslash
        char c = peek(ctx);
        consume(ctx);
        switch (peek(ctx)) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case 'b': return '\b';
            case '"': return '\"';
            case '\'': return '\'';
            case '\\': return '\\';
            default: return c;
        }
    } else {
        return consume(ctx);
    }
}

LexerContext *lexer(char *data) {
    TokenBuffer tokens = tokenbuffer_create(1024);
    Buffer buf = buffer_create(64);
    LexerContext *ctx = malloc(sizeof(LexerContext));
    *ctx = (LexerContext){
        .src = data,
        .idx = 0,
        .tokens = tokens,
    };

    while (peek(ctx)) {
        if (isalpha(peek(ctx))) {
            buffer_push(&buf, consume(ctx));
            while (isalnum(peek(ctx)) || peek(ctx) == '_') {
                buffer_push(&buf, consume(ctx));
            }

            lex_ident(ctx, &buf);
        }
        else if (isdigit(peek(ctx))) {
            buffer_push(&buf, consume(ctx));
            while (isdigit(peek(ctx))) {
                buffer_push(&buf, consume(ctx));
            }
            token_push(ctx, (Token){.type = NUMBER, .value = strdup(buf.data), .len = buf.idx});
            buffer_clear(&buf);
        }
        else {
            switch (peek(ctx)) {
                case '@':
                    consume(ctx);  // @
                    buffer_push(&buf, consume(ctx));
                    while (isalnum(peek(ctx)) || peek(ctx) == '_') {
                        buffer_push(&buf, consume(ctx));
                    }
        
                    token_push(ctx, (Token){.type = ATTR, .value = strdup(buf.data), .len = buf.idx});
                    buffer_clear(&buf);
                    break;
                case '\'':
                    consume(ctx);          // '
                    char c = handle_esc(ctx);
                    if (peek(ctx) != '\'') {
                        fprintf(stderr, "Lexer error: unterminated char literal at %zu, %zu.\n", line, col);
                        exit(1);
                    }
                    consume(ctx);          // '
                    char tmp[2] = {c, '\0'};
                    token_push(ctx, (Token){.type = CHAR_LIT, .value = strdup(tmp), .len = 1});
                    break;
                case '"':
                    consume(ctx);  // "
                    Buffer str = buffer_create(128);    // TODO: longer string literals
                    while (peek(ctx) != '"') {
                        buffer_push(&str, handle_esc(ctx));
                    }
                    consume(ctx);  // "
                    token_push(ctx, (Token){.type = STRING_LIT, .value = strdup(str.data), .len = str.len});
                    buffer_clear(&str);
                    break;
                case '/':
                    consume(ctx);
                    if (peek(ctx) == '/') {
                        consume(ctx);   // <- this thingy
                        while (peek(ctx) && peek(ctx) != '\n') consume(ctx);
                    } else if (peek(ctx) == '*') {
                        consume(ctx); /* <- this thingy */
                        while (peek(ctx)) {
                            if (peek(ctx) == '*'){
                                consume(ctx);
                                if (peek(ctx) == '/') {
                                    consume(ctx);
                                    break;
                                }
                            } else consume(ctx);
                        }
                    } else {
                        if (peek(ctx) == '=') {
                            consume(ctx);
                            token_push(ctx, (Token){.type = DIV_EQ});
                        }
                        else token_push(ctx, (Token){.type = SLASH});
                    }
                    break;
                case ':':
                    consume(ctx);
                    if (peek(ctx) == ':') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = DOUBLECOLON});
                    }
                    else token_push(ctx, (Token){.type = COLON});
                    break;
                case '(':
                    token_push(ctx, (Token){.type = LPAREN}); consume(ctx); break;
                case ')':
                    token_push(ctx, (Token){.type = RPAREN}); consume(ctx); break;
                case '{':
                    token_push(ctx, (Token){.type = LBRACE}); consume(ctx); break;
                case '}':
                    token_push(ctx, (Token){.type = RBRACE}); consume(ctx); break;
                case '[':
                    token_push(ctx, (Token){.type = LBRACK}); consume(ctx); break;
                case ']':
                    token_push(ctx, (Token){.type = RBRACK}); consume(ctx); break;
                case ';':
                    token_push(ctx, (Token){.type = SEMICOLON}); consume(ctx); break;
                case '+':
                    consume(ctx);
                    if (peek(ctx) == '=') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = PLUS_EQ});
                    }
                    else token_push(ctx, (Token){.type = PLUS});
                    break;
                case '!':
                    consume(ctx);
                    if (peek(ctx) == '=') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = BANG_EQ});
                    }
                    else token_push(ctx, (Token){.type = BANG});
                    break;
                case '-':
                    consume(ctx);
                    if (peek(ctx) == '=') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = MINUS_EQ});
                    }
                    else token_push(ctx, (Token){.type = MINUS});
                    break;
                case '*':
                    consume(ctx);
                    if (peek(ctx) == '=') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = STAR_EQ});
                    }
                    else token_push(ctx, (Token){.type = STAR});
                    break;
                case '<':
                    consume(ctx);
                    if (peek(ctx) == '<') {
                        consume(ctx);
                        if (peek(ctx) == '=') {
                            consume(ctx);
                            token_push(ctx, (Token){.type = LSHIFT_EQ});
                        }
                        else token_push(ctx, (Token){.type = LSHIFT});
                    }
                    else if (peek(ctx) == '=') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = LESS_EQ});
                    }
                    else token_push(ctx, (Token){.type = LESS});
                    break;
                case '>':
                    consume(ctx);
                    if (peek(ctx) == '>') {
                        consume(ctx);
                        if (peek(ctx) == '=') {
                            consume(ctx);
                            token_push(ctx, (Token){.type = RSHIFT_EQ});
                        }
                        else token_push(ctx, (Token){.type = RSHIFT});
                    }
                    else if (peek(ctx) == '=') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = GREATER_EQ});
                    }
                    else token_push(ctx, (Token){.type = GREATER});
                    break;
                case '=':
                    consume(ctx);
                    if (peek(ctx) == '=') {
                        consume(ctx);
                        token_push(ctx, (Token){.type = EQ_EQ});
                    }
                    else token_push(ctx, (Token){.type = EQ});
                    break;
                case ',':
                    consume(ctx); token_push(ctx, (Token){.type = COMMA}); break;
                case '.':
                    consume(ctx); token_push(ctx, (Token){.type = DOT}); break;
                case '?':
                    consume(ctx); token_push(ctx, (Token){.type = QUESTION}); break;
                case ' ':
                case '\t':
                case '\n': consume(ctx); break;
                default:
                    fprintf(stderr, "Lexer error at %zu, %zu: unknown character %c\n", line, col + 1, peek(ctx));
                    exit(1);
            }
        }
    }

    token_push(ctx, (Token){.type = _EOF});
    return ctx;
}