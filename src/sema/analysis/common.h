#ifndef SEMA_ANALYSIS_COMMON_H
#define SEMA_ANALYSIS_COMMON_H

#include "../common.h"

typedef struct {
    Symbol *symbol;
    HirExpr *value;
} CompBinding;

typedef struct {
    Array(CompBinding) bindings;
} CompFrame;

typedef struct {
    Sema *sema;
    CompFrame *frame;
} CompContext;

HirExpr *comp_eval_expr(CompContext *sema, HirExpr *expr);
bool comp_exec_stmt(Sema *sema, HirStmt *stmt);
bool comp_expr_is_evaluable(HirExpr *expr);

HirExpr *sema_coerce(Sema *sema, HirExpr *expr, HirType *type);
HirExpr *sema_expr(Sema *sema, AstExpr *ast, HirType *expected);

HirType *type_with_mutability(Sema *sema, HirType *type, bool mutable);
HirType *builtin_type(Sema *sema, BuiltinType builtin);
bool type_equal(HirType *a, HirType *b);

void sema_decl(Sema *sema, AstDecl *ast);
void sema_constraint_decl(Sema *sema, AstConstraintDecl *ast);
void sema_include_decl(Sema *sema, AstIncludeDecl *ast);
bool sema_var_decl(Sema *sema, AstVarDecl *ast);
void sema_type_decl(Sema *sema, AstTypeDecl *ast);
void sema_fn_decl(Sema *sema, AstFnDecl *ast);

static inline bool is_integer(HirType *type) {
    if (!type || type->kind != HIR_TYPE_BUILTIN || (
        type->builtin != BUILTIN_INT8 &&
        type->builtin != BUILTIN_INT16 &&
        type->builtin != BUILTIN_INT32 &&
        type->builtin != BUILTIN_INT64 &&
        type->builtin != BUILTIN_UINT8 &&
        type->builtin != BUILTIN_UINT16 &&
        type->builtin != BUILTIN_UINT32 &&
        type->builtin != BUILTIN_UINT64)
    )
        return false;
    return true;
}

static inline bool is_pointer(HirType *type) {
    return type && type->kind == HIR_TYPE_POINTER && !type->pointer.optional;
}

static inline bool is_signed(HirType *type) {
    if (!type || type->kind != HIR_TYPE_BUILTIN)
        return false;
    
    switch (type->builtin) {
        case BUILTIN_INT8:
        case BUILTIN_INT16:
        case BUILTIN_INT32:
        case BUILTIN_INT64:
            return true;

        default:
            return false;
    }
}

static inline Symbol *insert_parameter(Sema *sema, AstParam *param, HirType *type) {
    Symbol *symbol = arena_alloc(sema->arena, sizeof(Symbol));

    *symbol = (Symbol) {
        .kind = SYMBOL_PARAMETER,
        .name = param->name,
        .decl = NULL,
        .type = type,
    };

    scope_insert((Scope *)array_at(&sema->scopes, sema->scopes.len - 1), symbol);

    return symbol;
}

static bool hir_stmt_terminates(HirStmt *stmt) {
    if (stmt == NULL)
        return false;

    switch (stmt->kind) {
        case HIR_STMT_RETURN:
        case HIR_STMT_BREAK:
        case HIR_STMT_CONTINUE:
            return true;

        case HIR_STMT_BLOCK:
            if (stmt->block.stmts.len == 0)
                return false;

            return hir_stmt_terminates(((HirStmt **)stmt->block.stmts.data)[stmt->block.stmts.len - 1]);

        case HIR_STMT_IF:
            return stmt->_if._else != NULL && hir_stmt_terminates(stmt->_if.then) && hir_stmt_terminates(stmt->_if._else);

        default:
            return false;
    }
}

static inline size_t loop_scope(Sema *sema, size_t level) {
    for (size_t i = sema->scopes.len; i > 0; i--) {
        Scope *scope = (Scope *)array_at(&sema->scopes, i - 1);

        if (!scope->loop)
            continue;

        if (--level == 0)
            return i - 1;
    }

    return SIZE_MAX;
}

static inline HirType *pointer_type(Sema *sema, HirType *pointee, bool optional) {
    HirType *type = arena_alloc(sema->arena, sizeof(*type));

    type->kind = HIR_TYPE_POINTER;
    type->size = sizeof(void *);
    type->align = __alignof(void *);
    type->pointer.pointee = pointee;
    type->pointer.optional = optional;

    return type;
}

void scope_insert(Scope *scope, Symbol *sym);
Symbol *scope_lookup(Scope *scope, AstName name);
Symbol *sema_lookup(Sema *sema, AstName name);
Symbol *sema_lookup_path(Sema *sema, Path path);

#endif