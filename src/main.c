#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

typedef size_t size;

typedef enum {
    MODULE,
    IDENT,
    SEMI,
    FN,
    INT,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    RETURN,
    NUMBER,
    _EOF,
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    size len;
    size line;
    size column;
} Token;

static char *src = NULL;
static int idx = 0;

char peek() {
    return src[idx];
}

static size line = 1, col = 1;

char consume() {
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

typedef struct {
    Token *data;
    size len;
    int idx;
} TokenBuffer;

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
            while (isalnum(peek())) {
                buffer_push(&buf, consume());
            }

            if (strcmp(buf.data, "module") == 0) {
                token_push(&tokens, (Token){.type = MODULE});
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
        else {
            consume();
        }
    }

    token_push(&tokens, (Token){.type = _EOF});

    return tokens;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;

    FILE *src_file = fopen(argv[1], "r");
    fseek(src_file, 0, SEEK_END);
    size src_size = ftell(src_file);
    rewind(src_file);

    char *buf = malloc(src_size + 1);
    fread(buf, 1, src_size, src_file);
    buf[src_size] = '\0';
    TokenBuffer tokens = lexer(buf);

    for (int i = 0; i < tokens.idx; i++) {
        Token t = tokens.data[i];
        switch (t.type) {
            case MODULE: puts("MODULE"); break;
            case IDENT: printf("IDENT %s\n", t.value); free(t.value); break;
            case SEMI: puts("SEMI"); break;
            case FN: puts("FN"); break;
            case INT: puts("INT"); break;
            case LPAREN: puts("LPAREN"); break;
            case RPAREN: puts("RPAREN"); break;
            case LBRACE: puts("LBRACE"); break;
            case RBRACE: puts("RBRACE"); break;
            case RETURN: puts("RETURN"); break;
            case NUMBER: printf("NUMBER %s\n", t.value); free(t.value); break;
            case _EOF: puts("EOF"); break;
        }
    }

    return 0;
}