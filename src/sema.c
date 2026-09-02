#include "sema.h"

//! TODO: hash table / binary lookup
//! TODO: report duplicated symbols
void scope_insert(Scope *scope, Symbol sym) {
    array_push(&scope->syms, &sym);
}

Symbol *scope_lookup(Scope *scope, String name) {
    return NULL;
}

Symbol *sema_lookup(Sema *sema, String name) {
    for (size_t i = sema->scopes.len; i > 0; --i) {
        Scope *scope = (Scope *)array_at(&sema->scopes, i - 1);
        Symbol *symbol = scope_lookup(scope, name);

        if (symbol != NULL)
            return symbol;
    }

    return scope_lookup(&sema->global_scope, name);
}

void collect_decls(Sema *sema, Array(AstDecl *) decls) {
    for (size_t i = 0; i < decls.len; i++) {
        AstDecl *d = ((AstDecl**)decls.data)[i];
        
        Symbol sym = { .decl = d };
        switch (d->kind) {
            case AST_DECL_TYPE: {
                sym.kind = SYMBOL_TYPE;
                sym.name = d->type.name;
                break;
            }

            case AST_DECL_FN: {
                sym.kind = SYMBOL_FN;
                sym.name = d->fn.name;
                break;
            }

            case AST_DECL_VAR: {
                sym.kind = SYMBOL_GLOBAL;
                sym.name = d->var.name;
                break;
            }

            case AST_DECL_CONSTRAINT: {
                sym.kind = SYMBOL_CONSTRAINT;
                sym.name = d->constraint.name;
                break;
            }

            // includes are managed later, ignore for now
            case AST_DECL_INCLUDE: {
                break;
            }

            //! TODO: report internal error
            default: {
                break;
            }
        }
        scope_insert(&sema->global_scope, sym);
    }
}

HirType *sema_type(Sema *sema, AstType *ast);
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