#include "common.h"
#include "../../mangle.h"

static HirType *generic_type_error(Sema *sema, AstType *ast, DiagCode code, String message, String note) {
    DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, code, ast->span, message);
    if (note.length != 0)
        diag_note(&diagnostic, note);
    diag_finish(&diagnostic);

    HirType *type = arena_calloc(sema->arena, sizeof(*type));
    *type = (HirType){
        .kind = HIR_TYPE_ERROR,
        .mutable = ast->mutable,
        .align = 1,
    };
    return type;
}

static bool generic_type_arguments_equal(Array left, Array right) {
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

static GenericTypeInstance *generic_type_find_instance(Sema *sema, Symbol *generic, Array arguments) {
    for (size_t i = 0; i < sema->generic_type_instances.len; i++) {
        GenericTypeInstance *instance = array_at(&sema->generic_type_instances, i);
        if (symbol_canonical(instance->generic) == symbol_canonical(generic) && generic_type_arguments_equal(instance->arguments, arguments))
            return instance;
    }

    return NULL;
}

static HirType *generic_type_with_mutability(Sema *sema, HirType *type, bool mutable) {
    if (type->mutable == mutable)
        return type;

    HirType *copy = arena_calloc(sema->arena, sizeof(*copy));
    *copy = *type;
    copy->mutable = mutable;
    return copy;
}

static Symbol *generic_type_create_symbol(Sema *sema, Symbol *generic) {
    AstDecl *decl = arena_calloc(sema->arena, sizeof(*decl));
    *decl = *generic->decl;
    decl->type.generics.len = 0;

    Symbol *instance = arena_calloc(sema->arena, sizeof(*instance));
    *instance = (Symbol){
        .kind = SYMBOL_TYPE,
        .decl = decl,
        .name = generic->name,
        .span = generic->span,
        .owner_module = generic->owner_module,
        .owner_scope = generic->owner_scope,
    };

    HirType *type = arena_calloc(sema->arena, sizeof(*type));
    *type = (HirType){
        .kind = HIR_TYPE_ERROR,
        .mutable = false,
        .align = 1,
        .nominal = instance,
    };
    instance->type = type;
    return instance;
}

HirType *sema_generic_type(Sema *sema, AstType *ast, Symbol *generic) {
    AstTypeDecl *template = &generic->decl->type;

    String generic_name = generic->name.kind == AST_NAME_IDENT ? generic->name.ident : STRING("<generic>");

    if (ast->named.args.len == 0) {
        String message = format(sema->arena, "generic type '%.*s' requires %zu type argument(s), got 0", (int)generic_name.length, generic_name.data, template->generics.len);
        String note = format(sema->arena, "provide %zu type argument(s) for this declaration", template->generics.len);
        return generic_type_error(sema, ast, E_GENERIC_TYPE_ARGUMENTS_REQUIRED, message, note);
    }

    if (ast->named.args.len != template->generics.len) {
        String message = format(sema->arena, "generic type '%.*s' expects %zu type argument(s), got %zu", (int)generic_name.length, generic_name.data, template->generics.len, ast->named.args.len);
        String note = format(sema->arena, "provide exactly %zu type argument(s)", template->generics.len);
        return generic_type_error(sema, ast, E_GENERIC_ARGUMENT_COUNT, message, note);
    }

    Array arguments = array_create(sema->arena, sizeof(HirType *));
    for (size_t i = 0; i < ast->named.args.len; i++) {
        AstType *argument_ast = ((AstType **)ast->named.args.data)[i];
        HirType *argument = sema_type(sema, argument_ast);
        if (argument == NULL || argument->kind == HIR_TYPE_ERROR) {
            HirType *error = arena_calloc(sema->arena, sizeof(*error));
            *error = (HirType){.kind = HIR_TYPE_ERROR, .align = 1, .mutable = ast->mutable};
            return error;
        }
        array_push(&arguments, &argument);
    }

    if (!sema_check_symbol_generic_constraints(sema, generic, template->generics, arguments, ast->span)) {
        HirType *error = arena_calloc(sema->arena, sizeof(*error));
        *error = (HirType){.kind = HIR_TYPE_ERROR, .align = 1, .mutable = ast->mutable};
        return error;
    }

    GenericTypeInstance *cached = generic_type_find_instance(sema, generic, arguments);
    if (cached != NULL) {
        if (!cached->complete) {
            if (sema->type_indirection_depth <= cached->pointer_depth)
                return generic_type_error(sema, ast, E_RECURSIVE_GENERIC_TYPE, STRING("generic type has an infinitely sized by-value recursive field"), STRING("use a pointer or slice for recursive fields"));
            return cached->instance->type;
        }

        if (cached->instance->type == NULL)
            return generic_type_error(sema, ast, E_RECURSIVE_GENERIC_TYPE, STRING("generic type instantiation failed"), STRING("check the type declaration and its fields"));
        return generic_type_with_mutability(sema, cached->instance->type, ast->mutable);
    }

    Symbol *instance_symbol = generic_type_create_symbol(sema, generic);
    size_t cache_index = sema->generic_type_instances.len;

    Array cached_arguments = array_create(sema->arena, sizeof(HirType *));
    for (size_t i = 0; i < arguments.len; i++) {
        HirType *type = ((HirType **)arguments.data)[i];
        array_push(&cached_arguments, &type);
    }

    GenericTypeInstance instance = {
        .generic = generic,
        .instance = instance_symbol,
        .arguments = cached_arguments,
        .pointer_depth = sema->type_indirection_depth,
        .complete = false,
    };
    array_push(&sema->generic_type_instances, &instance);

    AstModule *previous_module = sema->module;
    Scope previous_global_scope = sema->global_scope;
    Scope *previous_module_scope = sema->module_scope;
    if (generic->owner_module != NULL)
        sema->module = generic->owner_module;
    if (generic->owner_scope != NULL && generic->owner_scope != &sema->global_scope) {
        sema->global_scope = *generic->owner_scope;
        sema->module_scope = generic->owner_scope;
    }

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

    HirType *base = sema_type(sema, template->type);
    sema->scopes = previous_scopes;
    sema->module = previous_module;
    sema->global_scope = previous_global_scope;
    sema->module_scope = previous_module_scope;

    GenericTypeInstance *stored = array_at(&sema->generic_type_instances, cache_index);
    stored->complete = true;

    HirType *type = instance_symbol->type;
    if (base == NULL || base->kind == HIR_TYPE_ERROR) {
        type->kind = HIR_TYPE_ERROR;
        type->align = 1;
        return generic_type_with_mutability(sema, type, ast->mutable);
    }

    *type = *base;
    type->mutable = false;
    type->nominal = instance_symbol;
    type->base = base;
    instance_symbol->link_name = coda_mangle_symbol(sema, generic, arguments, true);
    return generic_type_with_mutability(sema, type, ast->mutable);
}

Symbol *sema_generic_receiver_type(Sema *sema, AstType *receiver) {
    while (receiver != NULL && receiver->kind == AST_TYPE_POINTER)
        receiver = receiver->pointer.pointee;

    if (receiver == NULL || receiver->kind != AST_TYPE_NAMED)
        return NULL;

    Symbol *symbol = sema_lookup_path(sema, receiver->named.path);
    if (symbol == NULL || symbol->kind != SYMBOL_TYPE || symbol->decl == NULL || symbol->decl->kind != AST_DECL_TYPE)
        return NULL;

    if (symbol->decl->type.generics.len == 0 || receiver->named.args.len != symbol->decl->type.generics.len)
        return NULL;

    return symbol;
}

bool sema_fn_has_unbound_receiver_generics(Sema *sema, AstFnDecl *fn) {
    if (fn == NULL || fn->receiver == NULL)
        return false;

    Symbol *generic = sema_generic_receiver_type(sema, fn->receiver);
    if (generic == NULL)
        return false;

    for (size_t i = 0; i < generic->decl->type.generics.len; i++) {
        AstGenericParam *param = array_at(&generic->decl->type.generics, i);
        Symbol *binding = sema_lookup(sema, param->name);

        if (binding == NULL || binding->kind != SYMBOL_TYPE || binding->type == NULL)
            return true;
    }

    return false;
}

static bool generic_type_symbol_matches(Symbol *left, Symbol *right) {
    if (left == NULL || right == NULL)
        return false;

    if (left == right || symbol_canonical(left) == symbol_canonical(right))
        return true;

    if (left->kind != SYMBOL_TYPE || right->kind != SYMBOL_TYPE)
        return false;

    if (left->decl != NULL && left->decl == right->decl)
        return true;

    if (!ast_name_equal(&left->name, &right->name))
        return false;

    if (left->owner_module != NULL || right->owner_module != NULL)
        return left->owner_module != NULL && left->owner_module == right->owner_module;

    return left->owner_scope != NULL && left->owner_scope == right->owner_scope;
}

GenericTypeInstance *sema_generic_type_instance_for_type(Sema *sema, HirType *type, Symbol *generic) {
    while (type != NULL && type->kind == HIR_TYPE_POINTER)
        type = type->pointer.pointee;

    if (type == NULL || type->nominal == NULL)
        return NULL;

    for (size_t i = 0; i < sema->generic_type_instances.len; i++) {
        GenericTypeInstance *instance = array_at(&sema->generic_type_instances, i);

        if (symbol_canonical(instance->instance) != symbol_canonical(type->nominal))
            continue;
        if (generic != NULL && !generic_type_symbol_matches(instance->generic, generic))
            continue;

        return instance;
    }

    return NULL;
}

bool sema_method_receiver_arguments(Sema *sema, Symbol *method, HirType *receiver, Array *arguments, bool *is_generic) {
    *arguments = array_create(sema->arena, sizeof(HirType *));
    *is_generic = false;

    if (method == NULL || method->decl == NULL || method->decl->kind != AST_DECL_FN)
        return false;

    AstModule *previous_module = sema->module;
    Scope previous_global_scope = sema->global_scope;
    Scope *previous_module_scope = sema->module_scope;
    Array previous_scopes = sema->scopes;

    if (method->owner_module != NULL)
        sema->module = method->owner_module;
    if (method->owner_scope != NULL) {
        sema->global_scope = *method->owner_scope;
        sema->module_scope = method->owner_scope;
    }
    sema->scopes = array_create(sema->arena, sizeof(Scope));

    Symbol *generic = sema_generic_receiver_type(sema, method->decl->fn.receiver);
    bool success = true;
    if (generic != NULL) {
        *is_generic = true;
        GenericTypeInstance *instance = sema_generic_type_instance_for_type(sema, receiver, NULL);
        if (instance == NULL) {
            success = false;
        } else {
            *arguments = instance->arguments;
        }
    }

    sema->module = previous_module;
    sema->global_scope = previous_global_scope;
    sema->module_scope = previous_module_scope;
    sema->scopes = previous_scopes;
    return success;
}