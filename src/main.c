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

char consume() {
    if (!(src + idx)) return '\0';
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

Token *lexer(char *data) {
    Token *tokens = malloc(1024 * sizeof(Token));   // TODO: dynamic resizing
    int num_tokens = 0;
    Buffer buf = buffer_create(64);
    src = data;

    while (peek()) {
        if (isalpha(peek())) {
            buffer_push(&buf, consume());
            while (isalnum(peek())) {
                buffer_push(&buf, consume());
            }

            if (strcmp(buf.data, "module") == 0) {
                tokens[num_tokens++] = (Token){.type = MODULE};
                buffer_clear(&buf);
            }
            else if (strcmp(buf.data, "fn") == 0) {
                tokens[num_tokens++] = (Token){.type = FN};
                buffer_clear(&buf);
            }
            else if (strcmp(buf.data, "int") == 0) {
                tokens[num_tokens++] = (Token){.type = INT};
                buffer_clear(&buf);
            }
            else if (strcmp(buf.data, "return") == 0) {
                tokens[num_tokens++] = (Token){.type = RETURN};
                buffer_clear(&buf);
            }
            else {
                tokens[num_tokens++] = (Token){.type = IDENT, .value = strdup(buf.data), .len = buf.len};
                buffer_clear(&buf);
            }
        }
        else if (isdigit(peek())) {
            buffer_push(&buf, consume());
            while (isdigit(peek())) {
                buffer_push(&buf, consume());
            }
            tokens[num_tokens++] = (Token){.type = NUMBER, .value = strdup(buf.data), .len = buf.len};
            buffer_clear(&buf);
        }
        else if (peek() == '(') {
            tokens[num_tokens++] = (Token){.type = LPAREN};
            consume();
        }
        else if (peek() == ')') {
            tokens[num_tokens++] = (Token){.type = RPAREN};
            consume();
        }
        else if (peek() == '{') {
            tokens[num_tokens++] = (Token){.type = LBRACE};
            consume();
        }
        else if (peek() == '}') {
            tokens[num_tokens++] = (Token){.type = RBRACE};
            consume();
        }
        else if (peek() == ';') {
            tokens[num_tokens++] = (Token){.type = SEMI};
            consume();
        }
        else {
            consume();
        }
    }

    tokens[num_tokens++] = (Token){.type = _EOF};

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
    Token *tokens = lexer(buf);

    for (int i = 0; tokens[i].type != _EOF; i++) {
        Token t = tokens[i];
        switch (t.type) {
            case MODULE: puts("MODULE"); break;
            case IDENT: printf("IDENT %s\n", t.value); break;
            case SEMI: puts("SEMI"); break;
            case FN: puts("FN"); break;
            case INT: puts("INT"); break;
            case LPAREN: puts("LPAREN"); break;
            case RPAREN: puts("RPAREN"); break;
            case LBRACE: puts("LBRACE"); break;
            case RBRACE: puts("RBRACE"); break;
            case RETURN: puts("RETURN"); break;
            case NUMBER: printf("NUMBER %s\n", t.value); break;
        }
    }

    return 0;
}