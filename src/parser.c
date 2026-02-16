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

Include *parse_include() {
    Include *inc = arena_calloc(arena, sizeof(Include));

    // caller ensures INCLUDE token but does not consume
    consume();

    for (int i = 0; true; i++) {
        Token path = consume();
        if (path.type != IDENT) error("Expected include path");
        char **new = arena_alloc(arena, sizeof(char*) * (i + 1));
        if (inc->path_len > 0) memcpy(new, inc->path, sizeof(char*) * inc->path_len);
        inc->path = new;

        inc->path[inc->path_len++] = path.value;
        if (peek().type != DOUBLECOLON) break;
        consume();
    }

    expect(SEMICOLON, "Missing include semicolon");

    return inc;
}

Module *parse_module() {
    expect(MODULE, "Missing module declaration");

    Module *m = arena_calloc(arena, sizeof(Module));
    
    // Parse and store module name
    Token modname = consume();
    if (modname.type != IDENT) error("Missing module name");
    m->name = arena_alloc(arena, strlen(modname.value) + 1);
    strcpy(m->name, modname.value);

    consume();  // ;

    // Parse all INCLUDEs
    for (int i = 0; peek().type == INCLUDE; i++) {
        // Increase the size of the includes buffer
        Include **new = arena_alloc(arena, sizeof(Include*) * (i + 1));
        if (m->include_count > 0) memcpy(new, m->includes, sizeof(Include*) * m->include_count);
        m->includes = new;

        // now append the node
        m->includes[m->include_count++] = parse_include();
    }

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