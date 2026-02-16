#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "lexer.h"
#include "parser.h"
#include "arena.h"

static TokenBuffer *tokens = NULL;
static bool failed = false;
static Arena *arena = NULL;

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
    failed = true;
    while (peek().type != SEMICOLON) consume();  // skip to sync point
}

static void error(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    verror(msg, args);
}

static void expect(TokenType expected, const char *msg, ...) {
    va_list args;
    if (!match(expected)) {
        va_start(args, msg);
        verror(msg, args);
        exit(1);    //TODO: register error, but still print others THEN crash
    }
}

Module *parse_module() {
    expect(MODULE, "Missing module declaration");
    Token modname = consume();
    if (modname.type != IDENT) error("Missing module name");
    Module *m = arena_alloc(arena, sizeof(Module));
    m->name = arena_alloc(arena, strlen(modname.value));
    strcpy(m->name, modname.value);

    return m;
}

Module *parser(TokenBuffer *t, Arena *a) {
    tokens = t;
    arena = a;
    Module *m = parse_module();
    if (failed) {
        fprintf(stderr, "Compilation failed.\n");
        exit(1);
    }
    return m;
}