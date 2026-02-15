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
        .cap = elements,
        .len = 0,
        .pos = 0,
    };
}

void token_push(TokenBuffer *buffer, Token token) {
    if (buffer->len >= buffer->cap) return;
    token.line = line;
    token.column = col - token.len;
    buffer->data[buffer->len++] = token;
}

typedef struct {
    char *name;
    TokenType type;
} Keyword;

Keyword keywords[] = {
    {"module", MODULE},
    {"include", INCLUDE},
    {"fn", FN},
    {"int", INT},
    {"return", RETURN}
};

void lex_ident(Buffer *buf, TokenBuffer *tokens) {
    for (int i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        Keyword k = keywords[i];
        if (strcmp(buf->data, k.name) == 0) {
            token_push(tokens, (Token){.type = k.type, .len = buf->idx});
            buffer_clear(buf);
            return;
        }
    }

    if (peek() == '\n') {
        line++; col = 0;
    } else {
        col++;
    }
    token_push(tokens, (Token){.type = IDENT, .value = strdup(buf->data), .len = buf->idx});
    buffer_clear(buf);
}

static char handle_esc(TokenBuffer *tokens) {
    if (peek() == '\\') {
        consume();  // backslash
        char c = peek();
        consume();
        switch (peek()) {
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
        return consume();
    }
}

TokenBuffer lexer(char *data) {
    TokenBuffer tokens = tokenbuffer_create(1024);
    Buffer buf = buffer_create(64);
    src = data;

    while (peek()) {
        if (isalpha(peek())) {
            buffer_push(&buf, consume());
            while (isalnum(peek()) || peek() == '_') {
                buffer_push(&buf, consume());
            }

            lex_ident(&buf, &tokens);
        }
        else if (isdigit(peek())) {
            buffer_push(&buf, consume());
            while (isdigit(peek())) {
                buffer_push(&buf, consume());
            }
            token_push(&tokens, (Token){.type = NUMBER, .value = strdup(buf.data), .len = buf.idx});
            buffer_clear(&buf);
        }
        else {
            switch (peek()) {
                case '\'':
                    consume();          // '
                    char c = handle_esc(&tokens);
                    if (peek() != '\'') {
                        fprintf(stderr, "Lexer error: unterminated char literal at %zu, %zu.", line, col)    ;
                        exit(1);
                    }
                    consume();          // '
                    char tmp[2] = {c, '\0'};
                    token_push(&tokens, (Token){.type = CHAR, .value = strdup(tmp), .len = 1});
                    break;
                case '"':
                    consume();  // "
                    Buffer str = buffer_create(128);    // TODO: longer string literals
                    while (peek() != '"') {
                        buffer_push(&str, handle_esc(&tokens));
                    }
                    consume();  // "
                    token_push(&tokens, (Token){.type = STRING, .value = strdup(str.data), .len = str.len});
                    buffer_clear(&str);
                    break;
                case '/':
                    if (src[idx + 1] == '/') {
                        consume(); consume();   // <- this thingy
                        while (peek() && peek() != '\n') consume();
                    } else if (src[idx + 1] == '*') {
                        consume(); consume(); /* <- this thingy */
                        while (peek()) {
                            if (peek() == '*' && src[idx+1] == '/') {
                                consume(); consume(); /* this thingy -> */
                                break;
                            }
                            consume();
                        }
                    } else {
                        token_push(&tokens, (Token){.type = DIV});
                        consume();
                    }
                    break;
                case ':':
                    if (src[idx+1] == ':') {
                        token_push(&tokens, (Token){.type = DOUBLECOLON});
                        consume(); consume();
                    } else {
                        token_push(&tokens, (Token){.type = COLON});
                        consume();
                    }
                    break;
                case '(':
                    token_push(&tokens, (Token){.type = LPAREN}); consume(); break;
                case ')':
                    token_push(&tokens, (Token){.type = RPAREN}); consume(); break;
                case '{':
                    token_push(&tokens, (Token){.type = LBRACE}); consume(); break;
                case '}':
                    token_push(&tokens, (Token){.type = RBRACE}); consume(); break;
                case ';':
                    token_push(&tokens, (Token){.type = SEMICOLON}); consume(); break;
                case '+':
                    token_push(&tokens, (Token){.type = PLUS}); consume(); break;
                case '-':
                    token_push(&tokens, (Token){.type = MINUS}); consume(); break;
                case '*':
                    token_push(&tokens, (Token){.type = STAR}); consume(); break;
                case '<':
                    consume();
                    if (peek() == '<') {
                        token_push(&tokens, (Token){.type = SHLEFT});
                        consume();
                    }
                    else token_push(&tokens, (Token){.type = LESS});
                    break;
                case '>':
                    consume();
                    if (peek() == '>') {
                        consume();
                        token_push(&tokens, (Token){.type = SHRIGHT});
                    }
                    else token_push(&tokens, (Token){.type = GREATER});
                    break;
                case '=':
                    token_push(&tokens, (Token){.type = EQ}); consume(); break;
                case ',':
                    token_push(&tokens, (Token){.type = COMMA}); consume(); break;
                case '.':
                    token_push(&tokens, (Token){.type = DOT}); consume(); break;
                case ' ':
                case '\t':
                case '\n': consume(); break;
                default:
                    fprintf(stderr, "Lexer error at %zu, %zu: unknown character %c\n", line, col + 1, peek());
                    exit(1);
            }
        }
    }

    token_push(&tokens, (Token){.type = _EOF});
    return tokens;
}