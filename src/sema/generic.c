#include <stdio.h>
#include "common.h"

static HirExpr *generic_error_expr(Sema *sema, AstExpr *ast) {
    HirExpr *expr = arena_alloc(sema->arena, sizeof(*expr));
    *expr = (HirExpr){
        .span = ast->span,
        .kind = HIR_EXPR_ERROR,
    };
    return expr;
}

static HirExpr *generic_error(Sema *sema, AstExpr *ast, DiagCode code, String message, String note) {
    DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, code, ast->span, message);
    if (note.length != 0)
        diag_note(&diagnostic, note);
    diag_finish(&diagnostic);
    return generic_error_expr(sema, ast);
}

static size_t generic_type_index(AstFnDecl *fn, AstType *type) {
    if (type == NULL || type->kind != AST_TYPE_NAMED || type->named.path.parts.len != 1 || type->named.args.len != 0)
        return SIZE_MAX;

    AstName *name = array_at(&type->named.path.parts, 0);

    for (size_t i = 0; i < fn->generics.len; i++) {
        AstGenericParam *param = array_at(&fn->generics, i);
        if (ast_name_equal(name, &param->name))
            return i;
    }

    return SIZE_MAX;
}

static bool generic_type_contains(AstFnDecl *fn, AstType *type) {
    if (type == NULL)
        return false;

    if (generic_type_index(fn, type) != SIZE_MAX)
        return true;

    switch (type->kind) {
        case AST_TYPE_NAMED:
            for (size_t i = 0; i < type->named.args.len; i++) {
                if (generic_type_contains(fn, ((AstType **)type->named.args.data)[i]))
                    return true;
            }
            return false;

        case AST_TYPE_POINTER:
            return generic_type_contains(fn, type->pointer.pointee);

        case AST_TYPE_ARRAY:
            return generic_type_contains(fn, type->array.element);

        case AST_TYPE_FN:
            if (generic_type_contains(fn, type->fn.ret))
                return true;
            for (size_t i = 0; i < type->fn.params.len; i++) {
                if (generic_type_contains(fn, ((AstType **)type->fn.params.data)[i]))
                    return true;
            }
            return false;

        case AST_TYPE_SUM:
            for (size_t i = 0; i < type->sum.members.len; i++) {
                if (generic_type_contains(fn, ((AstType **)type->sum.members.data)[i]))
                    return true;
            }
            return false;

        case AST_TYPE_STRUCT:
            for (size_t i = 0; i < type->structure.fields.len; i++) {
                AstField *field = array_at(&type->structure.fields, i);
                if (generic_type_contains(fn, field->type))
                    return true;
            }
            return false;

        case AST_TYPE_UNION:
            for (size_t i = 0; i < type->union_.fields.len; i++) {
                AstField *field = array_at(&type->union_.fields, i);
                if (generic_type_contains(fn, field->type))
                    return true;
            }
            return false;

        case AST_TYPE_ENUM:
            return generic_type_contains(fn, type->_enum.underlying);

        case AST_TYPE_ERROR:
        case AST_TYPE_SPLICE:
            return false;
    }

    return false;
}

static HirField *generic_find_field(Array(HirField) fields, AstName name) {
    for (size_t i = 0; i < fields.len; i++) {
        HirField *field = array_at(&fields, i);
        if (field->symbol != NULL && ast_name_equal(&field->symbol->name, &name))
            return field;
    }

    return NULL;
}

static bool generic_infer_type(Sema *sema, AstFnDecl *fn, AstType *pattern, HirType *actual, Array inferred, Span span, bool *reported_error) {
    if (pattern == NULL || actual == NULL)
        return false;

    size_t index = generic_type_index(fn, pattern);
    if (index != SIZE_MAX) {
        HirType **bound = array_at(&inferred, index);
        if (*bound == NULL) {
            *bound = actual;
            return true;
        }

        if (type_equal(*bound, actual))
            return true;

        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_GENERIC_TYPE_MISMATCH, span, STRING("conflicting types inferred for a generic parameter"));
        diag_note(&diagnostic, STRING("make the argument types agree or supply explicit type arguments"));
        diag_finish(&diagnostic);
        *reported_error = true;
        return false;
    }

    if (!generic_type_contains(fn, pattern))
        return true;

    switch (pattern->kind) {
        case AST_TYPE_POINTER:
            return actual->kind == HIR_TYPE_POINTER && generic_infer_type(sema, fn, pattern->pointer.pointee, actual->pointer.pointee, inferred, span, reported_error);

        case AST_TYPE_ARRAY:
            if (pattern->array.sized) {
                if (actual->kind != HIR_TYPE_ARRAY)
                    return false;
                return generic_infer_type(sema, fn, pattern->array.element, actual->array.element, inferred, span, reported_error);
            }
            if (actual->kind != HIR_TYPE_SLICE)
                return false;
            return generic_infer_type(sema, fn, pattern->array.element, actual->slice.element, inferred, span, reported_error);

        case AST_TYPE_FN:
            if (actual->kind != HIR_TYPE_FUNCTION || pattern->fn.params.len != actual->function.params.len)
                return false;
            if (!generic_infer_type(sema, fn, pattern->fn.ret, actual->function.ret, inferred, span, reported_error))
                return false;
            for (size_t i = 0; i < pattern->fn.params.len; i++) {
                if (!generic_infer_type(sema, fn, ((AstType **)pattern->fn.params.data)[i], ((HirType **)actual->function.params.data)[i], inferred, span, reported_error))
                    return false;
            }
            return true;

        case AST_TYPE_SUM:
            if (actual->kind != HIR_TYPE_SUM || pattern->sum.members.len != actual->sum.members.len)
                return false;
            for (size_t i = 0; i < pattern->sum.members.len; i++) {
                if (!generic_infer_type(sema, fn, ((AstType **)pattern->sum.members.data)[i], ((HirType **)actual->sum.members.data)[i], inferred, span, reported_error))
                    return false;
            }
            return true;

        case AST_TYPE_STRUCT:
            if (actual->kind != HIR_TYPE_STRUCT || pattern->structure.fields.len != actual->structure.fields.len)
                return false;
            for (size_t i = 0; i < pattern->structure.fields.len; i++) {
                AstField *field = array_at(&pattern->structure.fields, i);
                HirField *actual_field = generic_find_field(actual->structure.fields, field->name);
                if (actual_field == NULL || !generic_infer_type(sema, fn, field->type, actual_field->type, inferred, span, reported_error))
                    return false;
            }
            return true;

        case AST_TYPE_UNION:
            if (actual->kind != HIR_TYPE_UNION || pattern->union_.fields.len != actual->union_.fields.len)
                return false;
            for (size_t i = 0; i < pattern->union_.fields.len; i++) {
                AstField *field = array_at(&pattern->union_.fields, i);
                HirField *actual_field = generic_find_field(actual->union_.fields, field->name);
                if (actual_field == NULL || !generic_infer_type(sema, fn, field->type, actual_field->type, inferred, span, reported_error))
                    return false;
            }
            return true;

        case AST_TYPE_ENUM:
            return actual->kind == HIR_TYPE_ENUM && generic_infer_type(sema, fn, pattern->_enum.underlying, actual->_enum.underlying, inferred, span, reported_error);

        case AST_TYPE_NAMED:
        case AST_TYPE_ERROR:
        case AST_TYPE_SPLICE:
            return false;
    }

    return false;
}

static bool generic_arguments_equal(Array left, Array right) {
    if (left.len != right.len)
        return false;

    for (size_t i = 0; i < left.len; i++) {
        HirType *a = ((HirType **)left.data)[i];
        HirType *b = ((HirType **)right.data)[i];
        if (!type_equal(a, b))
            return false;
    }

    return true;
}

static Symbol *generic_find_instance(Sema *sema, Symbol *generic, Array arguments) {
    for (size_t i = 0; i < sema->generic_instances.len; i++) {
        GenericInstance *instance = array_at(&sema->generic_instances, i);
        if (instance->generic == generic && generic_arguments_equal(instance->arguments, arguments))
            return instance->instance;
    }

    return NULL;
}

static AstName generic_instance_name(Sema *sema, Scope *scope) {
    for (;;) {
        char *data = arena_alloc(sema->arena, 64);
        int length = snprintf(data, 64, ".L_coda_generic_%zu", sema->next_generic_instance++);
        AstName name = {
            .kind = AST_NAME_IDENT,
            .ident = {.data = data, .length = length < 0 ? 0 : (size_t)length},
        };

        if (length >= 0 && scope_lookup(scope, name) == NULL)
            return name;
    }
}

static Symbol *generic_create_instance(Sema *sema, Symbol *generic, Array arguments) {
    AstFnDecl *template = &generic->decl->fn;
    Array previous_scopes = sema->scopes;
    sema->scopes = array_create(sema->arena, sizeof(Scope));
    Scope *scope = sema_push_scope(sema);

    for (size_t i = 0; i < template->generics.len; i++) {
        AstGenericParam *param = array_at(&template->generics, i);
        Symbol binding = {
            .kind = SYMBOL_TYPE,
            .decl = NULL,
            .name = param->name,
            .type = ((HirType **)arguments.data)[i],
            .span = param->span,
        };
        scope_insert(scope, &binding);
    }

    AstDecl *instance_decl = arena_alloc(sema->arena, sizeof(*instance_decl));
    *instance_decl = *generic->decl;
    instance_decl->fn.generics.len = 0;
    instance_decl->fn.name = generic_instance_name(sema, scope);

    Array attributes = array_create(sema->arena, sizeof(AstAttribute));
    for (size_t i = 0; i < instance_decl->fn.attrs.len; i++) {
        AstAttribute *attribute = array_at(&instance_decl->fn.attrs, i);
        if (!string_eq(attribute->name, STRING("export")))
            array_push(&attributes, attribute);
    }
    instance_decl->fn.attrs = attributes;

    Symbol instance_symbol = {
        .kind = SYMBOL_FN,
        .decl = instance_decl,
        .name = instance_decl->fn.name,
        .span = instance_decl->fn.span,
    };
    scope_insert(scope, &instance_symbol);

    Symbol *instance = scope_lookup(scope, instance_decl->fn.name);
    instance->type = sema_symbol_type(sema, instance);

    if (instance->type == NULL || instance->type->kind != HIR_TYPE_FUNCTION) {
        sema->scopes = previous_scopes;
        return NULL;
    }

    Array cached_arguments = array_create(sema->arena, sizeof(HirType *));
    for (size_t i = 0; i < arguments.len; i++) {
        HirType *type = ((HirType **)arguments.data)[i];
        array_push(&cached_arguments, &type);
    }

    GenericInstance cached = {
        .generic = generic,
        .instance = instance,
        .arguments = cached_arguments,
    };
    array_push(&sema->generic_instances, &cached);

    /* Register the concrete signature before the body so recursive calls reuse it. */
    sema_fn_decl(sema, &instance_decl->fn);
    sema->scopes = previous_scopes;
    return instance;
}

HirExpr *sema_generic_call(Sema *sema, AstExpr *ast, HirType *expected, Symbol *generic) {
    AstFnDecl *template = &generic->decl->fn;

    if (template->params.len != ast->call.args.len) {
        error_wrong_argument_count(sema->diags, template->params.len, ast->call.args.len, ast->span);
        return generic_error_expr(sema, ast);
    }

    Array arguments = array_create(sema->arena, sizeof(HirType *));
    for (size_t i = 0; i < template->generics.len; i++) {
        HirType *type = NULL;
        array_push(&arguments, &type);
    }

    Array inferred_values = array_create(sema->arena, sizeof(HirExpr *));
    for (size_t i = 0; i < ast->call.args.len; i++) {
        HirExpr *value = NULL;
        array_push(&inferred_values, &value);
    }

    if (ast->call.generic_args.len != 0) {
        if (ast->call.generic_args.len != template->generics.len)
            return generic_error(sema, ast, E_GENERIC_ARGUMENT_COUNT, STRING("generic argument count does not match the declaration"), STRING("provide one type argument for each generic parameter"));

        for (size_t i = 0; i < ast->call.generic_args.len; i++) {
            AstType *ast_type = ((AstType **)ast->call.generic_args.data)[i];
            HirType *type = sema_type(sema, ast_type);
            if (type == NULL || type->kind == HIR_TYPE_ERROR)
                return generic_error_expr(sema, ast);
            *(HirType **)array_at(&arguments, i) = type;
        }
    } else {
        for (size_t i = 0; i < template->params.len; i++) {
            AstParam *param = array_at(&template->params, i);
            if (!generic_type_contains(template, param->type))
                continue;

            AstExpr *ast_arg = ((AstExpr **)ast->call.args.data)[i];
            HirExpr *value = sema_expr(sema, ast_arg, NULL);
            if (value == NULL || value->kind == HIR_EXPR_ERROR)
                return generic_error_expr(sema, ast);
            *(HirExpr **)array_at(&inferred_values, i) = value;

            bool reported_error = false;
            if (!generic_infer_type(sema, template, param->type, value->type, arguments, ast_arg->span, &reported_error)) {
                if (reported_error)
                    return generic_error_expr(sema, ast);
                return generic_error(sema, ast, E_CANNOT_INFER_GENERIC, STRING("could not infer generic type arguments from the arguments"), STRING("supply explicit type arguments at the call site"));
            }
        }

        for (size_t i = 0; i < arguments.len; i++) {
            if (((HirType **)arguments.data)[i] == NULL)
                return generic_error(sema, ast, E_CANNOT_INFER_GENERIC, STRING("could not infer all generic type arguments"), STRING("supply explicit type arguments at the call site"));
        }
    }

    if (!sema_check_generic_constraints(sema, template->generics, arguments, ast->span))
        return generic_error_expr(sema, ast);

    Symbol *instance = generic_find_instance(sema, generic, arguments);
    if (instance == NULL)
        instance = generic_create_instance(sema, generic, arguments);
    if (instance == NULL || instance->type == NULL || instance->type->kind != HIR_TYPE_FUNCTION)
        return generic_error_expr(sema, ast);

    if (instance->decl->fn.comptime && !sema->comptime && !(sema->current_fn != NULL && sema->current_fn->is_comptime))
        return generic_error(sema, ast, E_CANT_CALL_COMPTIME, STRING("cannot call a comptime function at runtime"), STRING("call this function from a comptime context"));

    HirExpr *call = arena_alloc(sema->arena, sizeof(*call));
    *call = (HirExpr){
        .span = ast->span,
        .kind = HIR_EXPR_CALL,
        .type = instance->type->function.ret,
        .call = {
            .function = instance,
            .args = array_create(sema->arena, sizeof(HirExpr *)),
        },
    };

    for (size_t i = 0; i < ast->call.args.len; i++) {
        HirType *param_type = ((HirType **)instance->type->function.params.data)[i];
        HirExpr *value = ((HirExpr **)inferred_values.data)[i];
        if (value != NULL) {
            if (value->type == NULL || !type_equal(value->type, param_type)) {
                error_type_mismatch(sema->diags, param_type, value->type, value->span);
                return generic_error_expr(sema, ast);
            }
        } else {
            value = sema_expr(sema, ((AstExpr **)ast->call.args.data)[i], param_type);
            if (value == NULL || value->kind == HIR_EXPR_ERROR)
                return generic_error_expr(sema, ast);
        }

        array_push(&call->call.args, &value);
    }

    if (expected != NULL) {
        HirExpr *coerced = sema_coerce(sema, call, expected);
        return coerced != NULL ? coerced : generic_error_expr(sema, ast);
    }

    return call;
}
