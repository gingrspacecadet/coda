#include "sema.h"

//! TODO: hash table / binary lookup
//! TODO: report duplicated symbols
void scope_insert(Scope *scope, Symbol *sym) {
    array_push(&scope->syms, sym);
}

Symbol *scope_lookup(Scope *scope, AstName name);
Symbol *sema_lookup(Sema *sema, AstName name);

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
                };
                break;

            case AST_DECL_FN:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_FN,
                    .name = d->fn.name,
                    .decl = d,
                };
                break;

            case AST_DECL_VAR:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_GLOBAL,
                    .name = d->var.name,
                    .decl = d,
                };
                break;

            case AST_DECL_CONSTRAINT:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_CONSTRAINT,
                    .name = d->constraint.name,
                    .decl = d,
                };
                break;

            case AST_DECL_INCLUDE:
            break;
            
            //! TODO: report internal error
            case AST_DECL_ERROR:
            default:
                break;
        }

        if (sym != NULL)
            scope_insert(&sema->global_scope, sym);
    }
}

HirType *sema_type(Sema *sema, AstType *ast) {
    HirType *hir = arena_alloc(sema->arena, sizeof(HirType));

    hir->mutable = ast->mutable;

    switch (ast->kind) {
        //! TODO: builtin types!
        case AST_TYPE_NAMED: {
            Symbol *symbol = sema_lookup(sema, ast->named.path);

            if (symbol == NULL) {
                //! TODO: unknown type diagnostic
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            if (symbol->kind != SYMBOL_TYPE) {
                //! TODO: expected type diagnostic
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            hir->kind = HIR_TYPE_NAMED;
            hir->named.symbol = symbol;
            break;
        }

        case AST_TYPE_POINTER:
            hir->kind = HIR_TYPE_POINTER;
            hir->pointer.pointee = sema_type(sema, ast->pointer.pointee);
            hir->pointer.optional = ast->pointer.optional;
            break;

        case AST_TYPE_ARRAY: {
            HirType *element = sema_type(sema, ast->array.element);

            if (!ast->array.sized) {
                hir->kind = HIR_TYPE_SLICE;
                hir->slice.element = element;
                break;
            }

            hir->kind = HIR_TYPE_ARRAY;
            hir->array.element = element;
            hir->array.length = 0;
            break;
        }

        case AST_TYPE_FN:
            hir->kind = HIR_TYPE_FUNCTION;
            hir->function.ret = sema_type(sema, ast->fn.ret);
            hir->function.params = array_create(sema->arena, sizeof(HirType *));

            for (size_t i = 0; i < ast->fn.params.len; i++) {
                AstType *param = ((AstType **)ast->fn.params.data)[i];

                HirType *type = sema_type(sema, param);
                array_push(&hir->function.params, &type);
            }
            break;

        case AST_TYPE_SUM:
            hir->kind = HIR_TYPE_SUM;
            hir->sum.members = array_create(sema->arena, sizeof(HirType *));

            for (size_t i = 0; i < ast->sum.members.len; i++) {
                AstType *member = ((AstType **)ast->sum.members.data)[i];

                HirType *type = sema_type(sema, member);
                array_push(&hir->sum.members, &type);
            }
            break;

        case AST_TYPE_STRUCT:
            hir->kind = HIR_TYPE_STRUCT;
            hir->structure.fields = array_create(sema->arena, sizeof(HirField));

            for (size_t i = 0; i < ast->structure.fields.len; i++) {
                AstField *field = ((AstField *)ast->structure.fields.data) + i;

                HirField hir_field = {
                    .type = sema_type(sema, field->type),
                };

                array_push(&hir->structure.fields, &hir_field);
            }
            break;

        case AST_TYPE_UNION:
            hir->kind = HIR_TYPE_UNION;
            hir->union_.fields = array_create(sema->arena, sizeof(HirField));

            for (size_t i = 0; i < ast->union_.fields.len; i++) {
                AstField *field = ((AstField *)ast->union_.fields.data) + i;

                HirField hir_field = {
                    .type = sema_type(sema, field->type),
                };

                array_push(&hir->union_.fields, &hir_field);
            }
            break;

        case AST_TYPE_ENUM:
            hir->kind = HIR_TYPE_ENUM;
            hir->enumeration.underlying =
                sema_type(sema, ast->enumeration.underlying);

            hir->enumeration.items =
                array_create(sema->arena, sizeof(HirEnumItem));

            for (size_t i = 0; i < ast->enumeration.items.len; i++) {
                AstEnumItem *item =
                    ((AstEnumItem *)ast->enumeration.items.data) + i;

                //! TODO: resolve enum item name/value
                HirEnumItem hir_item = {
                };

                array_push(&hir->enumeration.items, &hir_item);
            }
            break;

        // this can't be resolved until comptime eval
        case AST_TYPE_SPLICE:
            hir->kind = HIR_TYPE_ERROR;
            //! TODO: mark/defer comptime type resolution
            break;

        default:
            hir->kind = HIR_TYPE_ERROR;
            //! TODO: internal compiler error
            break;
    }

    return hir;
}

HirExpr *sema_expr(Sema *sema, AstExpr *ast);
HirStmt *sema_stmt(Sema *sema, AstStmt *ast);
void sema_fn_decl(Sema *sema, AstFnDecl *ast);
void sema_type_decl(Sema *sema, AstTypeDecl *ast);
void sema_var_decl(Sema *sema, AstVarDecl *ast);
void sema_constraint_decl(Sema *sema, AstConstraintDecl *ast);
void sema_include_decl(Sema *sema, AstIncludeDecl *ast);

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

        case AST_DECL_ERROR:
        default:
            break;
    }
}

HirModule *sema_analyse(Sema *sema, AstModule *module) {
    sema->module = module;
    HirModule *hmod = arena_alloc(sema->arena, sizeof(HirModule));

    collect_decls(sema, module->decls);

    for (size_t i = 0; i < module->decls.len; i++) {
        AstDecl *d = ((AstDecl **)module->decls.data)[i];
        sema_decl(sema, d);
    }

    if (diags_has_errors(sema->diags))
        return NULL;

    return hmod;
}