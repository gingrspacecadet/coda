#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer *lexer;
    Token current;
    Token next;
    
    Arena *arena;
} Parser;

Include *parse_include(Parser *ctx);
TypeRef *parse_type_single(Parser *ctx);
Expr *parse_expr_prefix(Parser *ctx);
Expr *expr_handle_postfix(Parser *ctx, Expr *left);
Expr *parse_expr(Parser *ctx, int min_bp);
Stmt *parse_return_stmt(Parser *ctx);
Stmt *parse_for_stmt(Parser *ctx);
Stmt *parse_if_stmt(Parser *ctx);
Stmt *parse_while_stmt(Parser *ctx);
Stmt *parse_var_stmt(Parser *ctx);
Stmt *parse_expr_stmt(Parser *ctx);
Stmt *parse_stmt(Parser *ctx);
Stmt *parse_block_stmt(Parser *ctx);
TypeRef *parse_type(Parser *ctx);
FnDecl *parse_fn_decl(Parser *ctx);
Decl *parse_decl(Parser *ctx);
Module *parse_module(Parser *ctx);

extern void error_set_source(Source);

static Module *parse_file(char *path, Parser *out) {
    Lexer le = lexer_init_from_file(path);
    Lexer *l = arena_calloc(le.arena, sizeof(Lexer));
    memcpy(l, &le, sizeof(Lexer));
    Parser p = {
        .arena = l->arena,
        .lexer = l,
        .current = lex_next_token(l),
        .next = lex_next_token(l)
    };
    if (out) *out = p;
    return parse_module(&p);
}

#endif