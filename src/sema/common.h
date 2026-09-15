#ifndef SEMA_COMMON_H
#define SEMA_COMMON_H

#include "../sema.h"

static inline bool ast_name_equal(AstName *a, AstName *b) {
    if (a->kind != AST_NAME_IDENT || b->kind != AST_NAME_IDENT)
        return false;

    return string_eq(a->ident, b->ident);
}

void collect_decls(Sema *sema, Array(AstDecl *) decls);
void sema_decl(Sema *sema, AstDecl *ast);

static inline Scope sema_make_scope(Sema *sema) {
    return (Scope){
        .syms = array_create(sema->arena, sizeof(Symbol)),
        .defers = array_create(sema->arena, sizeof(HirStmt *)),
        .loop = false,
    };
}

static inline Scope *sema_push_scope(Sema *sema) {
    Scope scope = sema_make_scope(sema);
    array_push(&sema->scopes, &scope);
    return (Scope *)array_at(&sema->scopes, sema->scopes.len - 1);
}

static inline void sema_pop_scope(Sema *sema) {
    sema->scopes.len--;
}


static inline ModuleIndex module_index_make(Arena *arena) {
    ModuleIndex index = {
        .entries = array_create(arena, sizeof(ModuleEntry)),
    };

    return index;
}

static inline String unary_op_name(AstUnaryOp op) {
    switch (op) {
        case AST_UNARY_POS:     return STRING("+");
        case AST_UNARY_NEG:     return STRING("-");
        case AST_UNARY_BIT_NOT: return STRING("~");
        case AST_UNARY_NOT:     return STRING("!");
        case AST_UNARY_DEREF:   return STRING("*");
        case AST_UNARY_ADDRESS: return STRING("&");
    }

    return STRING("?");
}

static String binary_op_name(AstBinaryOp op) {
    switch (op) {
        case AST_BINARY_ADD: return STRING("+");
        case AST_BINARY_SUB: return STRING("-");
        case AST_BINARY_MUL: return STRING("*");
        case AST_BINARY_DIV: return STRING("/");
        case AST_BINARY_MOD: return STRING("%");

        case AST_BINARY_EQUAL: return STRING("==");
        case AST_BINARY_NOT_EQUAL: return STRING("!=");
        case AST_BINARY_LT: return STRING("<");
        case AST_BINARY_LTE: return STRING("<=");
        case AST_BINARY_GT: return STRING(">");
        case AST_BINARY_GTE: return STRING(">=");

        case AST_BINARY_LOGICAL_AND: return STRING("&&");
        case AST_BINARY_LOGICAL_OR: return STRING("||");

        case AST_BINARY_BIT_AND: return STRING("&");
        case AST_BINARY_BIT_OR: return STRING("|");
        case AST_BINARY_BIT_XOR: return STRING("^");

        case AST_BINARY_SHL: return STRING("<<");
        case AST_BINARY_SHR: return STRING(">>");

        case AST_BINARY_ASSIGN: return STRING("=");

        case AST_BINARY_ADD_ASSIGN: return STRING("+=");
        case AST_BINARY_SUB_ASSIGN: return STRING("-=");
        case AST_BINARY_MUL_ASSIGN: return STRING("*=");
        case AST_BINARY_DIV_ASSIGN: return STRING("/=");
        case AST_BINARY_MOD_ASSIGN: return STRING("%=");

        case AST_BINARY_BIT_AND_ASSIGN: return STRING("&=");
        case AST_BINARY_BIT_OR_ASSIGN: return STRING("|=");
        case AST_BINARY_BIT_XOR_ASSIGN: return STRING("^=");

        case AST_BINARY_SHL_ASSIGN: return STRING("<<=");
        case AST_BINARY_SHR_ASSIGN: return STRING(">>=");

        case AST_BINARY_NAND: return STRING("~&");
        case AST_BINARY_NAND_ASSIGN: return STRING("~&=");

        case AST_BINARY_NOR: return STRING("~|");
        case AST_BINARY_NOR_ASSIGN: return STRING("~|=");
    }

    return STRING("?");
}

void scope_insert(Scope *scope, Symbol *sym);
Symbol *scope_lookup(Scope *scope, AstName name);
Symbol *sema_lookup(Sema *sema, AstName name);
Symbol *sema_lookup_path(Sema *sema, Path path);
HirExpr *comp_eval_expr(Sema *sema, HirExpr *expr);
void collect_decls(Sema *sema, Array(AstDecl *) decls);
HirExpr *sema_expr(Sema *sema, AstExpr *ast, HirType *expected);
HirType *sema_type(Sema *sema, AstType *ast);
HirType *sema_symbol_type(Sema *sema, Symbol *symbol);
HirExpr *sema_coerce(Sema *sema, HirExpr *expr, HirType *type);
HirStmt *sema_stmt(Sema *sema, AstStmt *ast);
void sema_fn_decl(Sema *sema, AstFnDecl *ast);
void sema_type_decl(Sema *sema, AstTypeDecl *ast);
void sema_var_decl(Sema *sema, AstVarDecl *ast);
void sema_constraint_decl(Sema *sema, AstConstraintDecl *ast);
void sema_include_decl(Sema *sema, AstIncludeDecl *ast);
void sema_decl(Sema *sema, AstDecl *ast);
void sema_insert_builtin_types(Sema *sema);
bool sema_resolve_includes(Sema *sema, AstModule *module);
void module_index_scan(ModuleIndex *index, Arena *arena, Array(String) paths);

void error_unknown_name(Diags *diags, String name, Span span);
void error_unknown_path(Diags *diags, Path path, Span span);
void error_duplicate_symbol(Diags *diags, String name, Span span, Span previous);
void error_shadowing(Diags *diags, String name, Span span, Span previous);
void error_unknown_type(Diags *diags, Path path, Span span);
void error_type_mismatch(Diags *diags, HirType *expected, HirType *found, Span span);
void error_expected_function(Diags *diags, HirType *found, Span span);
void error_expected_type_symbol(Diags *diags, String name, Span span);
void error_wrong_argument_count(Diags *diags, size_t expected, size_t actual, Span span);
void error_unknown_field(Diags *diags, String name, Span span);
void error_invalid_unary_operation(Diags *diags, String operation, HirType *operand, Span span);
void error_invalid_binary_operation(Diags *diags, String operation, HirType *left, HirType *right, Span span);
void error_missing_return_value(Diags *diags, HirType *expected, Span span);
void error_invalid_return(Diags *diags, HirType *expected, HirType *found, Span span);
void error_invalid_break(Diags *diags, size_t level, Span span);
void error_invalid_continue(Diags *diags, size_t level, Span span);
void error_namespace_value(Diags *diags, Span span);
void error_module_not_found(Diags *diags, Path path, Span span);

#endif