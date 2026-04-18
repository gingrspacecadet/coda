#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer lexer;
    token_array tokens;
    size_t index;
    
    Arena *arena;
} Parser;

Include *parse_include(Parser *ctx);
TypeRef *parse_type(Parser *ctx);
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
StructDecl *parse_struct_decl(Parser *ctx);
UnionDecl *parse_union_decl(Parser *ctx);
FnDecl *parse_fn_decl(Parser *ctx);
Decl *parse_decl(Parser *ctx);
Module *parse_module(Parser *ctx);

#endif