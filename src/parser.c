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

Expr *parse_number() {
    Token t = consume();
    if (t.type != NUMBER) {
        fprintf(stderr, "expected number at line %zu (got %d)\n", t.line, t.type);
        exit(1);
    }
    Expr *e = malloc(sizeof(Expr));
    e->type = EXPR_NUMBER;
    e->number = atoi(t.value);
    return e;
}

Expr *parse_expr() {
    Expr *left = parse_number();

    while (peek().type == PLUS || peek().type == MINUS) {
        char op = consume().type == PLUS ? '+' : '-';
        Expr *right = parse_number();

        Expr *bin = malloc(sizeof(Expr));
        bin->type = EXPR_BINOP;
        bin->binop.left = left;
        bin->binop.op = op;
        bin->binop.right = right;

        left = bin;
    }

    return left;
}

Module parse_module() {
    expect(MODULE, "expected 'module'");
    Token name_token = consume();
    if (name_token.type != IDENT) {
        fprintf(stderr, "expected identifier for module name (%d)\n", name_token.type);
        exit(1);
    }
    expect(SEMI, "expected ';' after module name");

    Module m;
    m.name = name_token.value;
    return m;
}

Function parse_main() {
    expect(FN, "expected 'fn'");
    expect(INT, "expected 'int'");
    Token name_token = consume();
    if (name_token.type != IDENT || strcmp(name_token.value, "main") != 0) {
        fprintf(stderr, "expected function named 'main'\n");
        exit(1);
    }

    expect(LPAREN, "expected '('");
    expect(RPAREN, "expected ')'");
    expect(LBRACE, "expected '{'");

    expect(RETURN, "expected 'return'");
    
    Expr *ret_expr = parse_expr();

    expect(SEMI, "expected ';'");
    expect(RBRACE, "expected '}'");

    Function fn;
    fn.ret = ret_expr;
    return fn;
}

Program parse_program(TokenBuffer *t) {
    tokens = t;
    Program prog;
    prog.module = parse_module();
    prog.main_fn = parse_main();
    return prog;
}