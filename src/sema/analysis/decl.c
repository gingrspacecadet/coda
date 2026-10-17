#include "common.h"
#include "../../mangle.h"
#include "../../mangle.h"

typedef struct {
    Symbol *constraint;
    AstName name;
} LiftedConstraintType;

static bool sema_fn_uses_name(AstFnDecl *fn, AstName name) {
    for (size_t i = 0; i < fn->generics.len; i++) {
        AstGenericParam *param = array_at(&fn->generics, i);
        if (ast_name_equal(&param->name, &name))
            return true;
    }

    for (size_t i = 0; i < fn->params.len; i++) {
        AstParam *param = array_at(&fn->params, i);
        if (ast_name_equal(&param->name, &name))
            return true;
    }

    return false;
}

static LiftedConstraintType *sema_find_lifted_constraint_type(Array lifted, Symbol *constraint) {
    Symbol *canonical = symbol_canonical(constraint);
    for (size_t i = 0; i < lifted.len; i++) {
        LiftedConstraintType *entry = array_at(&lifted, i);
        if (symbol_canonical(entry->constraint) == canonical)
            return entry;
    }

    return NULL;
}

static AstName sema_make_constraint_generic_name(Sema *sema, AstFnDecl *fn, size_t *next_name) {
    for (;;) {
        AstName name = {
            .kind = AST_NAME_IDENT,
            .ident = format(sema->arena, "_C%03zu", (*next_name)++),
        };
        if (!sema_fn_uses_name(fn, name))
            return name;
    }
}

static AstType *sema_lift_constraint_signature_type(Sema *sema, AstFnDecl *fn, Array *lifted, AstType *type, size_t *next_name) {
    if (type == NULL)
        return NULL;

    switch (type->kind) {
        case AST_TYPE_NAMED: {
            Symbol *symbol = sema_lookup_path(sema, type->named.path);
            if (symbol != NULL && symbol->kind == SYMBOL_CONSTRAINT && type->named.args.len == 0) {
                LiftedConstraintType *entry = sema_find_lifted_constraint_type(*lifted, symbol);
                if (entry == NULL) {
                    AstName name = sema_make_constraint_generic_name(sema, fn, next_name);
                    AstGenericParam param = {
                        .span = type->span,
                        .name = name,
                        .constraints = array_create(sema->arena, sizeof(AstConstraintRef)),
                    };
                    AstConstraintRef constraint = {
                        .span = type->span,
                        .kind = AST_CONSTRAINTREF_NAMED,
                        .path = type->named.path,
                        .args = array_create(sema->arena, sizeof(AstType *)),
                    };
                    array_push(&param.constraints, &constraint);
                    array_push(&fn->generics, &param);

                    LiftedConstraintType new_entry = {
                        .constraint = symbol_canonical(symbol),
                        .name = name,
                    };
                    array_push(lifted, &new_entry);
                    entry = array_at(lifted, lifted->len - 1);
                }

                type->named.path.parts = array_create(sema->arena, sizeof(AstName));
                array_push(&type->named.path.parts, &entry->name);
                type->named.args = array_create(sema->arena, sizeof(AstType *));
                return type;
            }

            if (symbol != NULL && symbol->kind == SYMBOL_TYPE && symbol->decl == NULL &&
                symbol->type != NULL && symbol->type->kind == HIR_TYPE_ANY)
                return type;

            for (size_t i = 0; i < type->named.args.len; i++)
                ((AstType **)type->named.args.data)[i] = sema_lift_constraint_signature_type(sema, fn, lifted, ((AstType **)type->named.args.data)[i], next_name);
            return type;
        }

        case AST_TYPE_POINTER:
            type->pointer.pointee = sema_lift_constraint_signature_type(sema, fn, lifted, type->pointer.pointee, next_name);
            break;

        case AST_TYPE_ARRAY:
            type->array.element = sema_lift_constraint_signature_type(sema, fn, lifted, type->array.element, next_name);
            break;

        case AST_TYPE_FN:
            type->fn.ret = sema_lift_constraint_signature_type(sema, fn, lifted, type->fn.ret, next_name);
            for (size_t i = 0; i < type->fn.params.len; i++)
                ((AstType **)type->fn.params.data)[i] = sema_lift_constraint_signature_type(sema, fn, lifted, ((AstType **)type->fn.params.data)[i], next_name);
            break;

        case AST_TYPE_SUM:
            for (size_t i = 0; i < type->sum.members.len; i++)
                ((AstType **)type->sum.members.data)[i] = sema_lift_constraint_signature_type(sema, fn, lifted, ((AstType **)type->sum.members.data)[i], next_name);
            break;

        case AST_TYPE_STRUCT:
            type->structure.base = sema_lift_constraint_signature_type(sema, fn, lifted, type->structure.base, next_name);
            for (size_t i = 0; i < type->structure.fields.len; i++) {
                AstField *field = array_at(&type->structure.fields, i);
                field->type = sema_lift_constraint_signature_type(sema, fn, lifted, field->type, next_name);
            }
            break;

        case AST_TYPE_UNION:
            for (size_t i = 0; i < type->union_.fields.len; i++) {
                AstField *field = array_at(&type->union_.fields, i);
                field->type = sema_lift_constraint_signature_type(sema, fn, lifted, field->type, next_name);
            }
            break;

        case AST_TYPE_ENUM:
            type->_enum.underlying = sema_lift_constraint_signature_type(sema, fn, lifted, type->_enum.underlying, next_name);
            break;

        case AST_TYPE_ERROR:
        case AST_TYPE_SPLICE:
            break;
    }

    return type;
}

static void sema_lift_constraint_signature_types(Sema *sema, AstFnDecl *fn) {
    Array lifted = array_create(sema->arena, sizeof(LiftedConstraintType));
    size_t next_name = 1;

    for (size_t i = 0; i < fn->params.len; i++) {
        AstParam *param = array_at(&fn->params, i);
        param->type = sema_lift_constraint_signature_type(sema, fn, &lifted, param->type, &next_name);
    }

    fn->ret = sema_lift_constraint_signature_type(sema, fn, &lifted, fn->ret, &next_name);
}

void sema_fn_decl(Sema *sema, AstFnDecl *ast) {
    if (ast->generics.len != 0) {
        sema_validate_param_defaults(sema, ast);
        return;
    }

    if (sema_fn_has_unbound_receiver_generics(sema, ast))
        return;

    Symbol *symbol = sema_lookup_fn_decl(sema, ast);

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
        HirType *type = arena_calloc(sema->arena, sizeof(HirType));

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

    if (symbol->link_name.length == 0) {
        Array arguments = array_create(sema->arena, sizeof(HirType *));
        symbol->link_name = coda_mangle_symbol(sema, symbol, arguments, false);
    }

    if (symbol->link_name.length == 0) {
        Array arguments = array_create(sema->arena, sizeof(HirType *));
        symbol->link_name = coda_mangle_symbol(sema, symbol, arguments, false);
    }

    if (!sema_prepare_function_defaults(sema, symbol, true))
        return;

    if (ast->body == NULL)
        return;

    HirFunction *fn = arena_calloc(sema->arena, sizeof(HirFunction));

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
    for (size_t i = 0; i < ast->attrs.len; i++) {
        AstAttribute attr = ((AstAttribute *)ast->attrs.data)[i];

        if (string_eq(attr.name, STRING("export")))
            fn->is_export = true;
        else if (string_eq(attr.name, STRING("noreturn")))
            fn->is_noreturn = true;
    }

    fn->body = sema_stmt(sema, ast->body);

    if (fn->body == NULL || fn->body->kind == HIR_STMT_ERROR) {
        fprintf(stderr, "internal compiler error: dropping body of function %.*s\n", string_fmt(fn->symbol->name.ident));
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

    if (fn->body != NULL && fn->body->kind == HIR_STMT_BLOCK && !hir_stmt_terminates(fn->body)) {
        if (fn->is_noreturn) {
            HirStmt *unreachable = arena_calloc(sema->arena, sizeof(*unreachable));
            unreachable->kind = HIR_STMT_UNREACHABLE;
            array_push(&fn->body->block.stmts, &unreachable);
        } else if (returns_none) {
            HirStmt *ret = arena_calloc(sema->arena, sizeof(*ret));
            ret->kind = HIR_STMT_RETURN;
            array_push(&fn->body->block.stmts, &ret);
        }
    }

    sema->current_fn = previous_fn;
    sema_pop_scope(sema);

    array_push(&sema->hir_module->functions, fn);
}

void sema_type_decl(Sema *sema, AstTypeDecl *ast, bool packed, bool alias) {
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

    if (alias) {
        symbol->type = base;
        return;
    }

    if (packed && ast->type->kind == AST_TYPE_STRUCT && base->kind == HIR_TYPE_STRUCT) {
        size_t offset = 0;
        for (size_t i = 0; i < base->structure.fields.len; i++) {
            HirField *field = &((HirField *)base->structure.fields.data)[i];
            field->offset = offset;
            offset += field->type->size;
        }
        base->size = offset;
        base->align = 1;
    }

    HirType *type = arena_calloc(sema->arena, sizeof(*type));
    *type = *base;
    type->mutable = false;
    type->nominal = symbol;
    type->base = base;

    symbol->type = type;

    if (type->kind == HIR_TYPE_ENUM) {
        Array(HirEnumItem) inherited_items = type->_enum.items;
        type->_enum.items = array_create(sema->arena, sizeof(HirEnumItem));

        for (size_t i = 0; i < inherited_items.len; i++) {
            HirEnumItem *inherited = array_at(&inherited_items, i);
            Symbol *enum_symbol = arena_calloc(sema->arena, sizeof(*enum_symbol));
            *enum_symbol = *inherited->symbol;
            enum_symbol->type = type;
            enum_symbol->canonical = enum_symbol;
            enum_symbol->owner_module = sema->module;
            enum_symbol->owner_scope = sema->module_scope;

            HirEnumItem item = {
                .symbol = enum_symbol,
                .value = inherited->value,
            };
            array_push(&type->_enum.items, &item);
        }
    }
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
            return false;

        default:
            return false;
    }
}

static bool expr_contains_call(HirExpr *expr) {
    if (expr == NULL)
        return false;

    switch (expr->kind) {
        case HIR_EXPR_CALL:
        case HIR_EXPR_ANY_CALL:
            return true;

        case HIR_EXPR_UNARY:
            return expr_contains_call(expr->unary.operand);

        case HIR_EXPR_BINARY:
            return expr_contains_call(expr->binary.left) || expr_contains_call(expr->binary.right);

        case HIR_EXPR_INDEX:
            return expr_contains_call(expr->index.object) || expr_contains_call(expr->index.index);

        case HIR_EXPR_FIELD:
            return expr_contains_call(expr->field.object);

        case HIR_EXPR_CAST:
            return expr_contains_call(expr->cast.operand);

        case HIR_EXPR_INIT:
            for (size_t i = 0; i < expr->init.fields.len; i++) {
                HirInitField *field = &((HirInitField *)expr->init.fields.data)[i];
                if (expr_contains_call(field->value))
                    return true;
            }
            return false;

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

        if (!expr_contains_call(init)) {
            CompContext context = {.sema = sema, .frame = NULL};
            init = comp_eval_expr(&context, init);
            if (init == NULL || init->kind == HIR_EXPR_ERROR)
                return false;
        }

        if (!expr_is_static(init)) {
            error_global_initialiser_not_static(sema->diags, init->span);
            return false;
        }
    }

    symbol->type = type;

    if (symbol->link_name.length == 0) {
        Array arguments = array_create(sema->arena, sizeof(HirType *));
        symbol->link_name = coda_mangle_symbol(sema, symbol, arguments, false);
    }

    if (symbol->link_name.length == 0) {
        Array arguments = array_create(sema->arena, sizeof(HirType *));
        symbol->link_name = coda_mangle_symbol(sema, symbol, arguments, false);
    }

    HirGlobal global = {
        .symbol = symbol,
        .type = type,
        .init = init,
        .is_export = symbol->is_exported,
    };

    array_push(&sema->hir_module->globals, &global);
    return true;
}

void sema_constraint_decl(Sema *sema, AstConstraintDecl *ast) {}
void sema_include_decl(Sema *sema, AstIncludeDecl *ast) {}

void sema_decl(Sema *sema, AstDecl *ast) {
    switch (ast->kind) {
        case AST_DECL_FN:
            sema_lift_constraint_signature_types(sema, &ast->fn);
            sema_fn_decl(sema, &ast->fn);
            break;

        case AST_DECL_TYPE: {
            bool packed = false, alias = false;
            for (size_t i = 0; i < ast->attrs.len; i++) {
                AstAttribute *attribute = array_at(&ast->attrs, i);
                if (string_eq(attribute->name, STRING("packed"))) {
                    packed = true;
                    break;
                } else if (string_eq(attribute->name, STRING("alias"))) {
                    alias = true;
                    break;
                }
            }
            sema_type_decl(sema, &ast->type, packed, alias);
            break;
        }

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
                sym = arena_calloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_TYPE,
                    .name = d->type.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_FN:
                sym = arena_calloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_FN,
                    .name = d->fn.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_VAR:
                sym = arena_calloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_GLOBAL,
                    .name = d->var.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_CONSTRAINT:
                sym = arena_calloc(sema->arena, sizeof(Symbol));
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
            sym->owner_module = sema->module;
            sym->owner_scope = sema->module_scope;
            sym->canonical = sym;
            for (size_t j = 0; j < d->attrs.len; j++) {
                AstAttribute *attribute = array_at(&d->attrs, j);
                if (string_eq(attribute->name, STRING("export")))
                    sym->is_exported = true;
            }

            bool is_method = d->kind == AST_DECL_FN && d->fn.receiver != NULL;

            if (!is_method) {
                Symbol *s = sema_lookup_non_method(sema, sym->name);
                if (s != NULL) {
                    error_duplicate_symbol(sema->diags, sym->name.ident, d->span, s->span);
                    continue;
                }
            }

            scope_insert(&sema->global_scope, sym);
        }
    }
}

void sema_decls(Sema *sema, Array(AstDecl *) decls) {
    for (size_t pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < decls.len; i++) {
            AstDecl *decl = *(AstDecl **)array_at(&decls, i);
            if ((decl->kind == AST_DECL_TYPE) != (pass == 0))
                continue;
            sema_decl(sema, decl);
        }
    }
}
