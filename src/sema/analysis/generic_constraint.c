#include "common.h"

static bool generic_constraint_error(Sema *sema, Span span, DiagCode code, String message, String note) {
    DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, code, span, message);
    if (note.length != 0)
        diag_note(&diagnostic, note);
    diag_finish(&diagnostic);
    return false;
}

static HirField *generic_constraint_find_field(HirType *type, AstName name) {
    if (type == NULL)
        return NULL;

    Array fields = {0};
    switch (type->kind) {
        case HIR_TYPE_STRUCT: fields = type->structure.fields; break;
        case HIR_TYPE_UNION: fields = type->union_.fields; break;
        case HIR_TYPE_ARRAY: fields = type->array.fields; break;
        case HIR_TYPE_SLICE: fields = type->slice.fields; break;
        default: return NULL;
    }

    for (size_t i = 0; i < fields.len; i++) {
        HirField *field = array_at(&fields, i);
        if (field->symbol != NULL && ast_name_equal(&field->symbol->name, &name))
            return field;
    }

    return NULL;
}

static HirType *generic_constraint_resolve_in_symbol_scope(Sema *sema, Symbol *symbol, AstType *ast) {
    AstModule *previous_module = sema->module;
    Scope previous_global_scope = sema->global_scope;
    Scope *previous_module_scope = sema->module_scope;
    Array previous_scopes = sema->scopes;

    if (symbol->owner_module != NULL)
        sema->module = symbol->owner_module;
    if (symbol->owner_scope != NULL && symbol->owner_scope != &sema->global_scope) {
        sema->global_scope = *symbol->owner_scope;
        sema->module_scope = symbol->owner_scope;
    }
    sema->scopes = array_create(sema->arena, sizeof(Scope));
    HirType *type = sema_type(sema, ast);

    sema->module = previous_module;
    sema->global_scope = previous_global_scope;
    sema->module_scope = previous_module_scope;
    sema->scopes = previous_scopes;
    return type;
}

static bool generic_constraint_method_matches(Sema *sema, AstFnDecl *required, HirType *concrete) {
    Symbol *symbol = sema_find_method(sema, concrete, required->name);
    if (symbol == NULL || symbol->decl == NULL || symbol->decl->kind != AST_DECL_FN)
        return false;

    AstFnDecl *candidate = &symbol->decl->fn;
    if (candidate->params.len != required->params.len)
        return false;

    if (required->receiver != NULL) {
        HirType *required_receiver = sema_type(sema, required->receiver);
        if (required_receiver == NULL || required_receiver->kind == HIR_TYPE_ERROR || !type_equal(required_receiver, concrete))
            return false;
    }

    HirType *required_return = sema_type(sema, required->ret);
    HirType *candidate_return = generic_constraint_resolve_in_symbol_scope(sema, symbol, candidate->ret);
    if (required_return == NULL || candidate_return == NULL || required_return->kind == HIR_TYPE_ERROR ||
        candidate_return->kind == HIR_TYPE_ERROR || !type_equal(required_return, candidate_return))
        return false;

    for (size_t i = 0; i < required->params.len; i++) {
        AstParam *required_param = array_at(&required->params, i);
        AstParam *candidate_param = array_at(&candidate->params, i);
        HirType *required_type = sema_type(sema, required_param->type);
        HirType *candidate_type = generic_constraint_resolve_in_symbol_scope(sema, symbol, candidate_param->type);

        if (required_type == NULL || candidate_type == NULL || required_type->kind == HIR_TYPE_ERROR ||
            candidate_type->kind == HIR_TYPE_ERROR || !type_equal(required_type, candidate_type))
            return false;
    }

    return true;
}

static bool generic_constraint_check_items(Sema *sema, AstConstraintDecl *constraint, HirType *concrete, Span use_span) {
    for (size_t i = 0; i < constraint->items.len; i++) {
        AstConstraintItem *item = array_at(&constraint->items, i);

        switch (item->kind) {
            case AST_CONSTRAINT_FIELD: {
                HirField *actual = generic_constraint_find_field(concrete, item->field.name);
                if (actual == NULL) {
                    String message = format(sema->arena, "type does not satisfy constraint: required field '%.*s' is missing", (int)item->field.name.ident.length, item->field.name.ident.data);
                    return generic_constraint_error(sema, use_span, E_GENERIC_CONSTRAINT_UNSATISFIED, message, STRING("add a field with the required name and type"));
                }

                HirType *required = sema_type(sema, item->field.type);
                if (required == NULL || required->kind == HIR_TYPE_ERROR || !type_equal(actual->type, required)) {
                    String message = format(sema->arena, "type does not satisfy constraint: field '%.*s' has an incompatible type", (int)item->field.name.ident.length, item->field.name.ident.data);
                    return generic_constraint_error(sema, use_span, E_GENERIC_CONSTRAINT_UNSATISFIED, message, STRING("change the field type or the constraint declaration"));
                }
                break;
            }

            case AST_CONSTRAINT_METHOD:
                if (!generic_constraint_method_matches(sema, item->method, concrete)) {
                    String message = format(sema->arena, "type does not satisfy constraint: required method '%.*s' is missing or incompatible", (int)item->method->name.ident.length, item->method->name.ident.data);
                    return generic_constraint_error(sema, use_span, E_GENERIC_CONSTRAINT_UNSATISFIED, message, STRING("provide a method with a compatible receiver, return type, and parameter types"));
                }
                break;

            case AST_CONSTRAINT_EXPR:
                return generic_constraint_error(sema, item->span, E_GENERIC_CONSTRAINT_UNSUPPORTED, STRING("expression constraints are not implemented"), STRING("use field or method requirements for now"));

            case AST_CONSTRAINT_ERROR:
            default:
                return generic_constraint_error(sema, item->span, E_GENERIC_CONSTRAINT_INVALID, STRING("invalid generic constraint item"), STRING("check the constraint declaration"));
        }
    }

    return true;
}

bool sema_check_generic_constraints(Sema *sema, Array generics, Array arguments, Span span) {
    if (generics.len != arguments.len)
        return generic_constraint_error(sema, span, E_GENERIC_CONSTRAINT_INVALID, STRING("internal error: generic parameter and argument counts differ"), STRING("this is a compiler consistency error"));

    for (size_t i = 0; i < generics.len; i++) {
        AstGenericParam *param = array_at(&generics, i);
        HirType *concrete = ((HirType **)arguments.data)[i];
        if (concrete == NULL || concrete->kind == HIR_TYPE_ERROR)
            return false;
        if (param->constraints.len == 0)
            continue;

        sema_push_scope(sema);
        Symbol binding = {
            .kind = SYMBOL_TYPE,
            .name = param->name,
            .type = concrete,
            .span = param->span,
        };
        scope_insert((Scope *)array_at(&sema->scopes, sema->scopes.len - 1), &binding);

        AstName self_name = {.kind = AST_NAME_IDENT, .ident = STRING("Self")};
        if (!ast_name_equal(&param->name, &self_name)) {
            Symbol self_binding = {
                .kind = SYMBOL_TYPE,
                .name = self_name,
                .type = concrete,
                .span = param->span,
            };
            scope_insert((Scope *)array_at(&sema->scopes, sema->scopes.len - 1), &self_binding);
        }

        bool valid = true;
        for (size_t j = 0; j < param->constraints.len && valid; j++) {
            AstConstraintRef *ref = array_at(&param->constraints, j);
            AstConstraintDecl *constraint = NULL;

            if (ref->kind == AST_CONSTRAINTREF_INLINE) {
                constraint = ref->_inline;
            } else if (ref->kind == AST_CONSTRAINTREF_NAMED) {
                Symbol *symbol = sema_lookup_path(sema, ref->path);
                if (symbol == NULL || symbol->kind != SYMBOL_CONSTRAINT || symbol->decl == NULL || symbol->decl->kind != AST_DECL_CONSTRAINT) {
                    valid = generic_constraint_error(sema, ref->span, E_GENERIC_CONSTRAINT_INVALID, STRING("unknown or invalid generic constraint"), STRING("reference a constraint declaration or use an inline constraint block"));
                } else {
                    constraint = &symbol->decl->constraint;
                }
            } else {
                valid = generic_constraint_error(sema, ref->span, E_GENERIC_CONSTRAINT_INVALID, STRING("invalid generic constraint reference"), STRING("reference a named constraint or use an inline constraint block"));
            }

            if (valid && constraint == NULL)
                valid = generic_constraint_error(sema, ref->span, E_GENERIC_CONSTRAINT_INVALID, STRING("empty generic constraint"), STRING("provide at least one required field or method"));
            if (valid)
                valid = generic_constraint_check_items(sema, constraint, concrete, span);
        }

        sema_pop_scope(sema);
        if (!valid)
            return false;
    }

    return true;
}


bool sema_check_symbol_generic_constraints(Sema *sema, Symbol *generic, Array generics, Array arguments, Span span) {
    AstModule *previous_module = sema->module;
    Scope previous_global_scope = sema->global_scope;
    Scope *previous_module_scope = sema->module_scope;
    Array previous_scopes = sema->scopes;

    if (generic->owner_module != NULL)
        sema->module = generic->owner_module;
    if (generic->owner_scope != NULL && generic->owner_scope != &sema->global_scope) {
        sema->global_scope = *generic->owner_scope;
        sema->module_scope = generic->owner_scope;
    }
    sema->scopes = array_create(sema->arena, sizeof(Scope));
    bool valid = sema_check_generic_constraints(sema, generics, arguments, span);

    sema->module = previous_module;
    sema->global_scope = previous_global_scope;
    sema->module_scope = previous_module_scope;
    sema->scopes = previous_scopes;
    return valid;
}
