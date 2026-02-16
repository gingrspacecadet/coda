#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "lexer.h"
#include "parser.h"

static TokenBuffer *tokens = NULL;

static Token peek() {
    return tokens->data[tokens->pos];
}

static Token consume() {
    return tokens->data[tokens->pos++];
}

static int match(TokenType expected) {
    if (peek().type == expected) {
        consume();
        return 1;
    }
    return 0;
}

static void verror(const char *msg, va_list args) {
    fprintf(stderr, "Parse error at line %zu, column %zu: ", peek().line, peek().column);
    vfprintf(stderr, msg, args);
    putc('\n', stderr);
}

__attribute__((noreturn)) static void error(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    verror(msg, args);
    exit(1);
}

static void expect(TokenType expected, const char *msg, ...) {
    va_list args;
    if (!match(expected)) {
        va_start(args, msg);
        verror(msg, args);
        exit(1);    //TODO: register error, but still print others THEN crash
    }
}
