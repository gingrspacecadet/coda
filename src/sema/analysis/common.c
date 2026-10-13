#include "common.h"

//! TODO: hash table / binary lookup
//! TODO: report duplicated symbols
void scope_insert(Scope *scope, Symbol *sym) {
    array_push(&scope->syms, sym);
}

Symbol *scope_lookup(Scope *scope, AstName name) {
    for (size_t i = scope->syms.len; i > 0; --i) {
        Symbol *sym = (Symbol *)array_at(&scope->syms, i - 1);

        if (ast_name_equal(&sym->name, &name))
            return sym;
    }

    return NULL;
}

Symbol *sema_lookup(Sema *sema, AstName name) {
    for (size_t i = sema->scopes.len; i > 0; --i) {
        Scope *scope = (Scope *)array_at(&sema->scopes, i - 1);
        Symbol *symbol = scope_lookup(scope, name);

        if (symbol != NULL)
            return symbol;
    }

    Symbol *symbol = scope_lookup(&sema->global_scope, name);
    if (symbol != NULL)
        return symbol;

    return scope_lookup(&sema->builtin_scope, name);
}

Symbol *sema_lookup_path(Sema *sema, Path path) {
    if (path.parts.len == 0)
        return NULL;

    AstName *part = (AstName *)array_at(&path.parts, 0);
    Symbol *symbol = sema_lookup(sema, *part);

    if (symbol == NULL)
        return NULL;

    for (size_t i = 1; i < path.parts.len; i++) {
        if (symbol->kind != SYMBOL_NAMESPACE)
            return NULL;

        part = (AstName *)array_at(&path.parts, i);
        symbol = scope_lookup(symbol->namespace_scope, *part);

        if (symbol == NULL)
            return NULL;
    }

    return symbol;
}

bool sema_validate_param_defaults(Sema *sema, AstFnDecl *decl) {
    bool saw_default = false;

    for (size_t i = 0; i < decl->params.len; i++) {
        AstParam *param = array_at(&decl->params, i);

        if (decl->receiver != NULL && i == 0 && param->default_value != NULL) {
            DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_UNEXPECTED_TOKEN, param->span, STRING("a method receiver cannot have a default argument"));
            diag_finish(&diagnostic);
            return false;
        }

        if (param->default_value != NULL) {
            saw_default = true;
        } else if (saw_default) {
            DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_UNEXPECTED_TOKEN, param->span, STRING("parameters after a defaulted parameter must also have defaults"));
            diag_note(&diagnostic, STRING("move required parameters before the first defaulted parameter"));
            diag_finish(&diagnostic);
            return false;
        }
    }

    return true;
}


static size_t sema_function_defaults_index(Sema *sema, AstFnDecl *decl) {
    for (size_t i = 0; i < sema->function_defaults.len; i++) {
        FunctionDefaults *entry = array_at(&sema->function_defaults, i);
        if (entry->decl == decl)
            return i;
    }
    return SIZE_MAX;
}

static FunctionDefaults *sema_function_defaults_get(Sema *sema, AstFnDecl *decl) {
    size_t index = sema_function_defaults_index(sema, decl);
    return index == SIZE_MAX ? NULL : array_at(&sema->function_defaults, index);
}

static FunctionDefaults *sema_function_defaults_add(Sema *sema, AstFnDecl *decl) {
    FunctionDefaults entry = {
        .decl = decl,
        .values = array_create(sema->arena, sizeof(HirExpr *)),
        .state = FUNCTION_DEFAULTS_RESOLVING,
    };
    array_push(&sema->function_defaults, &entry);
    return array_at(&sema->function_defaults, sema->function_defaults.len - 1);
}

bool sema_prepare_function_defaults(Sema *sema, Symbol *symbol, bool declaration_context) {
    if (symbol == NULL || symbol->decl == NULL || symbol->decl->kind != AST_DECL_FN)
        return false;

    AstFnDecl *decl = &symbol->decl->fn;
    FunctionDefaults *cached = sema_function_defaults_get(sema, decl);
    if (cached != NULL) {
        if (cached->state == FUNCTION_DEFAULTS_RESOLVED)
            return true;
        if (cached->state == FUNCTION_DEFAULTS_FAILED)
            return false;
        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_COMP_CANT_EVAL, decl->span, STRING("cyclic dependency while resolving default arguments"));
        diag_note(&diagnostic, STRING("break the cycle between the involved default expressions"));
        diag_finish(&diagnostic);
        cached = sema_function_defaults_get(sema, decl);
        if (cached != NULL)
            cached->state = FUNCTION_DEFAULTS_FAILED;
        return false;
    }

    if (!sema_validate_param_defaults(sema, decl))
        return false;

    if (decl->generics.len != 0)
        return true;

    HirType *function_type = sema_symbol_type(sema, symbol);
    if (function_type == NULL || function_type->kind != HIR_TYPE_FUNCTION || function_type->function.params.len != decl->params.len)
        return false;

    sema_function_defaults_add(sema, decl);

    AstModule *previous_module = sema->module;
    Scope previous_global_scope = sema->global_scope;
    Scope *previous_module_scope = sema->module_scope;
    Array previous_scopes = sema->scopes;
    HirFunction *previous_fn = sema->current_fn;
    bool previous_comptime = sema->comptime;

    if (!declaration_context) {
        if (symbol->owner_module != NULL)
            sema->module = symbol->owner_module;
        if (symbol->owner_scope != NULL) {
            sema->global_scope = *symbol->owner_scope;
            sema->module_scope = symbol->owner_scope;
        }
        sema->scopes = array_create(sema->arena, sizeof(Scope));
    }

    sema->current_fn = NULL;
    sema->comptime = false;

    Array values = array_create(sema->arena, sizeof(HirExpr *));
    bool success = true;
    for (size_t i = 0; i < decl->params.len; i++) {
        AstParam *param = array_at(&decl->params, i);
        HirExpr *value = NULL;
        if (param->default_value != NULL) {
            HirType *param_type = ((HirType **)function_type->function.params.data)[i];
            value = sema_expr(sema, param->default_value, param_type);
            if (value == NULL || value->kind == HIR_EXPR_ERROR)
                success = false;
        }
        array_push(&values, &value);
        if (!success)
            break;
    }

    if (!declaration_context) {
        sema->module = previous_module;
        sema->global_scope = previous_global_scope;
        sema->module_scope = previous_module_scope;
        sema->scopes = previous_scopes;
    }
    sema->current_fn = previous_fn;
    sema->comptime = previous_comptime;

    cached = sema_function_defaults_get(sema, decl);
    if (cached == NULL)
        return false;
    cached->values = values;
    cached->state = success ? FUNCTION_DEFAULTS_RESOLVED : FUNCTION_DEFAULTS_FAILED;
    return success;
}

static bool sema_call_argument_error(Sema *sema, DiagCode code, Span span, String message, String note) {
    DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, code, span, message);
    if (note.length != 0)
        diag_note(&diagnostic, note);
    diag_finish(&diagnostic);
    return false;
}

bool sema_map_call_arguments(Sema *sema, AstExpr *call, AstFnDecl *decl, size_t first_param, size_t *arg_params, bool *provided) {
    size_t param_count = decl->params.len;
    if (first_param > param_count)
        return false;

    for (size_t i = 0; i < param_count; i++)
        provided[i] = false;

    size_t next_positional = first_param;
    size_t last_named = 0;
    bool have_named = false;

    for (size_t i = 0; i < call->call.args.len; i++) {
        AstCallArg *arg = array_at(&call->call.args, i);
        size_t parameter = SIZE_MAX;

        if (arg->name.kind == AST_NAME_ERROR) {
            if (have_named)
                return sema_call_argument_error(sema, E_UNEXPECTED_TOKEN, arg->span, STRING("positional arguments must precede named arguments"), STRING("move this argument before the first named argument"));
            if (next_positional >= param_count)
                return sema_call_argument_error(sema, E_WRONG_ARGUMENT_COUNT, arg->span, STRING("too many call arguments"), STRING("remove the extra argument"));
            parameter = next_positional++;
        } else {
            bool had_named = have_named;
            have_named = true;
            for (size_t j = first_param; j < param_count; j++) {
                AstParam *candidate = array_at(&decl->params, j);
                if (ast_name_equal(&candidate->name, &arg->name)) {
                    parameter = j;
                    break;
                }
            }

            if (parameter == SIZE_MAX)
                return sema_call_argument_error(sema, E_UNKNOWN_NAME, arg->span, STRING("unknown named argument"), STRING("the name must match a parameter of the called function"));
            if (provided[parameter])
                return sema_call_argument_error(sema, E_DUPLICATE_SYMBOL, arg->span, STRING("argument supplied more than once"), STRING("remove either the positional or named occurrence"));
            if (had_named && parameter <= last_named)
                return sema_call_argument_error(sema, E_UNEXPECTED_TOKEN, arg->span, STRING("named arguments must follow parameter declaration order"), STRING("reorder the named arguments to match the function declaration"));
            last_named = parameter;
        }

        if (provided[parameter])
            return sema_call_argument_error(sema, E_DUPLICATE_SYMBOL, arg->span, STRING("argument supplied more than once"), STRING("a parameter cannot be supplied both positionally and by name"));

        provided[parameter] = true;
        arg_params[i] = parameter;
    }

    for (size_t i = first_param; i < param_count; i++) {
        AstParam *param = array_at(&decl->params, i);
        if (!provided[i] && param->default_value == NULL) {
            DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_WRONG_ARGUMENT_COUNT, call->span, STRING("missing required argument"));
            diag_note(&diagnostic, STRING("supply the argument positionally or by name"));
            diag_finish(&diagnostic);
            return false;
        }
    }

    return true;
}

bool sema_complete_call_arguments(Sema *sema, Symbol *symbol, size_t first_param, bool *provided, HirExpr **values) {
    AstFnDecl *decl = &symbol->decl->fn;
    bool needs_defaults = false;

    for (size_t i = first_param; i < decl->params.len; i++) {
        if (!provided[i]) {
            needs_defaults = true;
            break;
        }
    }

    if (!needs_defaults)
        return true;

    if (!sema_prepare_function_defaults(sema, symbol, false))
        return false;

    FunctionDefaults *cached = sema_function_defaults_get(sema, &symbol->decl->fn);
    if (cached == NULL || cached->state != FUNCTION_DEFAULTS_RESOLVED || cached->values.len != decl->params.len)
        return false;

    for (size_t i = first_param; i < decl->params.len; i++) {
        if (provided[i])
            continue;

        HirExpr **value = array_at(&cached->values, i);
        if (value == NULL || *value == NULL)
            return false;
        values[i] = *value;
    }

    return true;
}

