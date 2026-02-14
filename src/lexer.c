#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "lexer.h"

static char *src = NULL;
static int idx = 0;

static char peek() {
    return src[idx];
}

static size line = 1, col = 1;

static char consume() {
    if (!(src + idx)) return '\0';
    if (src[idx] == '\n') {
        line++;
        col = 0;
    } else {
        col++;
    }
    return src[idx++];
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
        .len = elements,
        .idx = 0,
    };
}

void token_push(TokenBuffer *buffer, Token token) {
    if (buffer->idx >= buffer->len) return;
    token.line = line;
    token.column = col;
    buffer->data[buffer->idx++] = token;
}

TokenBuffer lexer(char *data) {
    TokenBuffer tokens = tokenbuffer_create(1024);
    Buffer buf = buffer_create(64);
    src = data;

    while (peek()) {
        if (isalpha(peek())) {
            buffer_push(&buf, consume());
            while (isalnum(peek()) || peek() == ':') {
                buffer_push(&buf, consume());
            }

            if (strcmp(buf.data, "module") == 0) {
                token_push(&tokens, (Token){.type = MODULE});
                buffer_clear(&buf);
            }
            else if (strcmp(buf.data, "include") == 0) {
                token_push(&tokens, (Token){.type = INCLUDE});
                buffer_clear(&buf);
            }
            else if (strcmp(buf.data, "fn") == 0) {
                token_push(&tokens, (Token){.type = FN});
                buffer_clear(&buf);
            }
            else if (strcmp(buf.data, "int") == 0) {
                token_push(&tokens, (Token){.type = INT});
                buffer_clear(&buf);
            }
            else if (strcmp(buf.data, "return") == 0) {
                token_push(&tokens, (Token){.type = RETURN});
                buffer_clear(&buf);
            }
            else {
                if (peek() == '\n') { 
                    line++; col = 0;
                } else {
                    col++;
                }
                token_push(&tokens, (Token){.type = IDENT, .value = strdup(buf.data), .len = buf.idx});
                buffer_clear(&buf);
            }
        }
        else if (isdigit(peek())) {
            buffer_push(&buf, consume());
            while (isdigit(peek())) {
                buffer_push(&buf, consume());
            }
            token_push(&tokens, (Token){.type = NUMBER, .value = strdup(buf.data), .len = buf.idx});
            buffer_clear(&buf);
        }
        else if (peek() == '/' && src[idx+1] == '/') {
            while (peek() != '\n') consume();
        }
        else if (peek() == '/' && src[idx+1] == '*') {
            while (peek() != '*' && src[idx+1] != '/') consume();
        }
        else if (peek() == '(') {
            token_push(&tokens, (Token){.type = LPAREN});
            consume();
        }
        else if (peek() == ')') {
            token_push(&tokens, (Token){.type = RPAREN});
            consume();
        }
        else if (peek() == '{') {
            token_push(&tokens, (Token){.type = LBRACE});
            consume();
        }
        else if (peek() == '}') {
            token_push(&tokens, (Token){.type = RBRACE});
            consume();
        }
        else if (peek() == ';') {
            token_push(&tokens, (Token){.type = SEMI});
            consume();
        }
        else if (peek() == '+') {
            token_push(&tokens, (Token){.type = PLUS});
            consume();
        }
        else if (peek() == '-') {
            token_push(&tokens, (Token){.type = MINUS});
            consume();
        }
        else {
            consume();
        }
    }

    token_push(&tokens, (Token){.type = _EOF});

    return tokens;
}