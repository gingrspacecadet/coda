#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"

static Token peek(TokenBuffer *tokens) {
    return tokens->data[tokens->idx];
}

static Token consume(TokenBuffer *tokens) {
    return tokens->data[tokens->idx++];
}

int match(TokenBuffer *tokens, TokenType expected) {
    if (peek(tokens).type == expected) {
        consume(tokens);
        return 1;
    }
    return 0;
}

void expect(TokenBuffer *tokens, TokenType expected, const char *msg) {
    if (!match(tokens, expected)) {
        fprintf(stderr, "parse error at line %zu, column %zu: %s\n",
        peek(tokens).line, peek(tokens).column, msg);
        exit(1);
    }
}

Module parse_module(TokenBuffer *tokens) {
    expect(tokens, MODULE, "expected 'module'");
    Token name_token = consume(tokens);
    if (name_token.type != IDENT) {
        fprintf(stderr, "expected identifier for module name (%d)\n", name_token.type);
        exit(1);
    }
    expect(tokens, SEMI, "expected ';' after module name");

    Module m;
    m.name = name_token.value;
    return m;
}

Function parse_main(TokenBuffer *tokens) {
    expect(tokens, FN, "expected 'fn'");
    expect(tokens, INT, "expected 'int'");
    Token name_token = consume(tokens);
    if (name_token.type != IDENT || strcmp(name_token.value, "main") != 0) {
        fprintf(stderr, "expected function named 'main'\n");
        exit(1);
    }

    expect(tokens, LPAREN, "expected '('");
    expect(tokens, RPAREN, "expected ')'");
    expect(tokens, LBRACE, "expected '{'");

    expect(tokens, RETURN, "expected 'return'");
    Token num_token = consume(tokens);
    if (num_token.type != NUMBER) {
        fprintf(stderr, "expected number after 'return'\n");
        exit(1);
    }
    expect(tokens, SEMI, "expected ';'");
    expect(tokens, RBRACE, "expected '}'");

    Function fn;
    fn.ret_val = atoi(num_token.value);
    return fn;
}

Program parse_program(TokenBuffer *tokens) {
    Program prog;
    prog.module = parse_module(tokens);
    prog.main_fn = parse_main(tokens);
    return prog;
}