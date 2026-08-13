#ifndef HIR_H
#define HIR_H

#include "ast.h"
#include "sema.h"

typedef struct HirExpr HirExpr;
typedef struct HirStmt HirStmt;
typedef struct HirInitField HirInitField;

struct HirInitField {
    string_optional field_name;
    HirExpr *value;
    Token token;
};

INSTANTIATE(HirExpr*, hirexprs, ARRAY_TEMPLATE)
INSTANTIATE(HirStmt*, hirstmts, ARRAY_TEMPLATE)
INSTANTIATE(HirInitField, hirinitfields, ARRAY_TEMPLATE)

struct HirExpr {
    enum {
        HIR_EXPR_LIT,
        HIR_EXPR_VAR,
        HIR_EXPR_UNARY,
        HIR_EXPR_BINARY,
        HIR_EXPR_CALL,
        HIR_EXPR_FIELD_OFFSET,
        HIR_EXPR_ARRAY_INDEX,
        HIR_EXPR_CAST,
        HIR_EXPR_INIT,
    } type;

    TypeRef *resolved_type;

    union {
        Literal literal;

        struct {
            Symbol *symbol;
        } var;

        struct {
            UnaryOp op;
            HirExpr *operand;
        } unary;

        struct {
            BinaryOp op;
            HirExpr *left;
            HirExpr *right;
        } binary;

        struct {
            HirExpr *callee;
            hirexprs_array args;
        } call;

        struct {
            HirExpr *base;
            size_t byte_offset;
        } field_offset;

        struct {
            HirExpr *base;
            HirExpr *index;
            size_t elem_size;
        } array_index;

        struct {
            TypeRef *to_type;
            HirExpr *expr;
        } cast;

        struct {
            hirinitfields_array fields;
        } init_list;
    };
};

struct HirStmt {
    enum {
        HIR_STMT_EXPR,
        HIR_STMT_BLOCK,
        HIR_STMT_RETURN,
        HIR_STMT_IF,
        HIR_STMT_WHILE,
        HIR_STMT_ASSIGN,
        HIR_STMT_DEFER,
    } type;

    union {
        HirExpr *expr;

        struct {
            hirstmts_array stmts;
        } block;

        struct {
            HirExpr *value;
        } _return;

        struct {
            HirExpr *cond;
            HirStmt *then_block;
            HirStmt *else_block;
        } _if;

        struct {
            HirExpr *cond;
            HirStmt *body;
        } _while;

        struct {
            HirExpr *target;
            HirExpr *value;
        } assign;

        struct {
            HirStmt *stmt;
        } defer;
    };
};

typedef struct {
    Symbol *symbol;
    TypeRef *ret_type;
    syms_array params;
    syms_array locals;
    bool is_extern;
    bool is_export;
    HirStmt *body;
} HirFnDecl;

typedef struct {
    Symbol *symbol;
    TypeRef *type;
    bool is_export;
    HirExpr *init;
} HirVarDecl;

INSTANTIATE(HirFnDecl *, hirfndecls, ARRAY_TEMPLATE)
INSTANTIATE(HirVarDecl *, hirvardecls, ARRAY_TEMPLATE)

typedef struct {
    hirfndecls_array functions;
    hirvardecls_array globals;
} HirModule;

HirModule *hir_lower_module(Analyser *ctx, Module *ast_mod);
void hir_pass_resolve_defers(Analyser *ctx, HirModule *mod);
void hir_pass_monomorphise(Analyser *ctx, HirModule *mod);
void hir_pretty_print(HirModule *mod);

#endif