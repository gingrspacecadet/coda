#include "common.h"

void sema_fn_decl(Sema *sema, AstFnDecl *ast) {
    if (ast->generics.len != 0)
        return;

    Symbol *symbol = sema_lookup(sema, ast->name);

    if (symbol == NULL) {
        //! TODO: internal compiler error
        fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);
        return;
    }

    symbol->span = ast->span;

    if (symbol->kind != SYMBOL_FN) {
        //! TODO: internal compiler error
        fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);
        return;
    }

    if (symbol->type == NULL) {
        HirType *type = arena_alloc(sema->arena, sizeof(HirType));

        *type = (HirType) {
            .kind = HIR_TYPE_FUNCTION,
            .mutable = false,
            .function = {
                .ret = sema_type(sema, ast->ret),
                .params = array_create(sema->arena, sizeof(HirType *)),
            },
        };

        for (size_t i = 0; i < ast->params.len; i++) {
            AstParam *param = (AstParam *)array_at(&ast->params, i);
            HirType *param_type = sema_type(sema, param->type);

            array_push(&type->function.params, &param_type);
        }

        symbol->type = type;
    }

    if (ast->body == NULL)
        return;

    HirFunction *fn = arena_alloc(sema->arena, sizeof(HirFunction));

    *fn = (HirFunction) {
        .symbol = symbol,
        .return_type = symbol->type->function.ret,
        .params = array_create(sema->arena, sizeof(HirParam)),
        .locals = array_create(sema->arena, sizeof(HirLocal)),
        .is_comptime = ast->comptime,
    };

    sema_push_scope(sema);

    HirType *type = symbol->type;

    for (size_t i = 0; i < ast->params.len; i++) {
        AstParam *param = (AstParam *)array_at(&ast->params, i);
        HirType *param_type = *(HirType **)array_at(&type->function.params, i);
        Symbol *parameter = insert_parameter(sema, param, param_type);

        HirParam hir_param = {
            .symbol = parameter,
            .type = param_type,
        };

        array_push(&fn->params, &hir_param);
    }

    HirFunction *previous_fn = sema->current_fn;
    sema->current_fn = fn;

    //! TODO: analyse attributes properly
    if (ast->attrs.len > 0 && string_eq(((AstAttribute *)array_at(&ast->attrs, 0))->name, STRING("export")))
        fn->is_export = true;

    fn->body = sema_stmt(sema, ast->body);

    if (fn->body == NULL || fn->body->kind == HIR_STMT_ERROR) {
        sema->current_fn = previous_fn;
        sema_pop_scope(sema);
        return;
    }

    HirType *return_type = fn->return_type;
    bool returns_none =
        return_type != NULL &&
        return_type->kind == HIR_TYPE_BUILTIN &&
        return_type->builtin == BUILTIN_NONE;

    if (!returns_none && !hir_stmt_terminates(fn->body))
        error_missing_return_value(sema->diags, return_type, ast->body->span);

    sema->current_fn = previous_fn;
    sema_pop_scope(sema);

    array_push(&sema->hir_module->functions, fn);
}

void sema_type_decl(Sema *sema, AstTypeDecl *ast) {
    if (ast->generics.len != 0)
        return;

    Symbol *symbol = sema_lookup(sema, ast->name);

    if (symbol == NULL) {
        fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);
        return;
    }

    if (symbol->kind != SYMBOL_TYPE) {
        fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);
        return;
    }

    symbol->span = ast->span;

    HirType *base = sema_type(sema, ast->type);

    if (base == NULL || base->kind == HIR_TYPE_ERROR)
        return;

    HirType *type = arena_alloc(sema->arena, sizeof(*type));
    *type = *base;
    type->mutable = false;
    type->nominal = symbol;
    type->base = base;

    symbol->type = type;
}

static bool expr_is_static(HirExpr *expr) {
    if (expr == NULL)
        return true;

    switch (expr->kind) {
        case HIR_EXPR_LITERAL:
            return true;

        case HIR_EXPR_INIT:
            for (size_t i = 0; i < expr->init.fields.len; i++) {
                HirInitField *field = &((HirInitField *)expr->init.fields.data)[i];

                if (!expr_is_static(field->value))
                    return false;
            }

            return true;

        case HIR_EXPR_CAST:
            return expr_is_static(expr->cast.operand);

        default:
            return false;
    }
}

bool sema_var_decl(Sema *sema, AstVarDecl *ast) {
    Symbol *symbol = sema_lookup(sema, ast->name);

    if (symbol == NULL || symbol->kind != SYMBOL_GLOBAL)
        return false;

    HirType *type = sema_type(sema, ast->type);

    if (type == NULL || type->kind == HIR_TYPE_ERROR)
        return false;

    HirExpr *init = NULL;

    if (ast->init != NULL) {
        init = sema_expr(sema, ast->init, type);

        if (init == NULL || init->kind == HIR_EXPR_ERROR)
            return false;

        if (!expr_is_static(init)) {
            error_global_initialiser_not_static(sema->diags, init->span);
            return false;
        }
    }

    symbol->type = type;

    HirGlobal global = {
        .symbol = symbol,
        .type = type,
        .init = init,
        .is_export = false,
    };

    array_push(&sema->hir_module->globals, &global);
    return true;
}

void sema_constraint_decl(Sema *sema, AstConstraintDecl *ast) {}
void sema_include_decl(Sema *sema, AstIncludeDecl *ast) {}

void sema_decl(Sema *sema, AstDecl *ast) {
    switch (ast->kind) {
        case AST_DECL_FN:
            sema_fn_decl(sema, &ast->fn);
            break;

        case AST_DECL_TYPE:
            sema_type_decl(sema, &ast->type);
            break;

        case AST_DECL_VAR:
            sema_var_decl(sema, &ast->var);
            break;

        case AST_DECL_CONSTRAINT:
            sema_constraint_decl(sema, &ast->constraint);
            break;

        case AST_DECL_INCLUDE:
            sema_include_decl(sema, &ast->include);
            break;

        //! TODO: internal compiler error
        fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);

        case AST_DECL_ERROR:
        default:
            break;
    }
}

void collect_decls(Sema *sema, Array(AstDecl *) decls) {
    for (size_t i = 0; i < decls.len; i++) {
        AstDecl *d = ((AstDecl **)decls.data)[i];
        Symbol *sym = NULL;

        switch (d->kind) {
            case AST_DECL_TYPE:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_TYPE,
                    .name = d->type.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_FN:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_FN,
                    .name = d->fn.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_VAR:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_GLOBAL,
                    .name = d->var.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_CONSTRAINT:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_CONSTRAINT,
                    .name = d->constraint.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_INCLUDE:
            break;
            
            //! TODO: report internal error
            case AST_DECL_ERROR:
            default:
                break;
        }

        if (sym != NULL) {
            Symbol *s = scope_lookup(&sema->global_scope, sym->name);
            if (s != NULL) {
                error_duplicate_symbol(sema->diags, sym->name.ident, d->span, s->span);
                continue;
            }

            scope_insert(&sema->global_scope, sym);
        }
    }
}