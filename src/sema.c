#include "sema.h"
#include "error.h"

Symbol *declare_symbol(Analyser *ctx, String name, uint32_t flags);
Symbol *lookup_symbol(Analyser *ctx, String name);
void register_globals(Analyser *ctx, Module *mod);
void resolve_types(Analyser *ctx, Module *mod);
void check_bodies(Analyser *ctx, Module *mod);
TypeRef *check_expr(Analyser *ctx, Expr *expr);
bool types_compatible(TypeRef *src, TypeRef *dst);

INSTANTIATE(char, char, ARRAY_TEMPLATE)

static void append_string_to_char_array(char_array *cs, String s) {
    for (size_t i = 0; i < s.length; ++i) {
        char_array_push(cs, s.data[i]);
    }
}

static String type_to_string(TypeRef *t) {
    if (!t) return string_make("(null)"); // keep existing behavior for null

    if (t->type == TYPEREF_NAMED) {
        if (t->type_symbol) {
            return t->type_symbol->name;
        }
        return t->named.name;
    }

    char_array cs = char_array_init();

    if (t->type == TYPEREF_POINTER) {
        String pointee = type_to_string(t->pointer.pointee);
        append_string_to_char_array(&cs, pointee);
        if (t->is_mutable) append_string_to_char_array(&cs, string_make(" mut"));
        char_array_push(&cs, '*');
        if (t->is_optional) char_array_push(&cs, '?');
        return (String){ .data = cs.data, .length = cs.len };
    }
    else if (t->type == TYPEREF_ARRAY) {
        String base = type_to_string(t->array.elem);
        append_string_to_char_array(&cs, base);
        char_array_push(&cs, '[');

        char buf[32];
        int n = snprintf(buf, sizeof buf, "%zu", t->array.length);
        if (n > 0) {
            for (int i = 0; i < n; ++i) char_array_push(&cs, buf[i]);
        }

        char_array_push(&cs, ']');
        return (String){ .data = cs.data, .length = cs.len };
    }
    else if (t->type == TYPEREF_FN) {
        char_array_push(&cs, 'f');
        char_array_push(&cs, 'n');
        char_array_push(&cs, ' ');

        String ret = type_to_string(t->fn.ret_type);
        append_string_to_char_array(&cs, ret);

        char_array_push(&cs, '(');
        // TODO: append parameters here, using the same pattern
        char_array_push(&cs, ')');

        return (String){ .data = cs.data, .length = cs.len };
    }
    else if (t->type == TYPEREF_SUM) {
        for (size_t i = 0; i < t->sum.cases.len; ++i) {
            TypeRef *case_t = t->sum.cases.data[i];
            String cs_str = type_to_string(case_t);
            append_string_to_char_array(&cs, cs_str);
            if (i + 1 < t->sum.cases.len) char_array_push(&cs, '|');
        }
        return (String){ .data = cs.data, .length = cs.len };
    }

    return string_make("Unknown");
}

static bool is_unsigned_integer(TypeRef *t) {
    if (!t || t->type != TYPEREF_NAMED) return false;
    Symbol *s = t->type_symbol;
    if (!s) return false;
    return string_eq(s->name, string_make("uint")) ||
           string_eq(s->name, string_make("uint8")) ||
           string_eq(s->name, string_make("uint16")) ||
           string_eq(s->name, string_make("uint32")) ||
           string_eq(s->name, string_make("uint64"));
}

static bool is_signed_integer(TypeRef *t) {
    if (!t || t->type != TYPEREF_NAMED) return false;
    Symbol *s = t->type_symbol;
    if (!s) return false;
    return string_eq(s->name, string_make("int")) ||
           string_eq(s->name, string_make("int8")) ||
           string_eq(s->name, string_make("int16")) ||
           string_eq(s->name, string_make("int32")) ||
           string_eq(s->name, string_make("int64"));
}

static size_t integer_size_bits(TypeRef *t) {
    if (!t) return 0;

    return get_type_size(t) * 8;
}

static void inject_builtin_types(Analyser *ctx) {
    char* builtins[] = {
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "char",
        "bool",
        "none",
        "$null",    // compiler internal type for `null` literal
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        char *type_name = builtins[i];

        Symbol *sym = declare_symbol(ctx, string_make(type_name), SYMFLAG_TYPE);

        TypeRef *type_ref = arena_calloc(ctx->arena, sizeof(TypeRef));
        type_ref->type = TYPEREF_NAMED;
        type_ref->is_mutable = false;
        type_ref->type_symbol = sym;
        sym->type = type_ref;
    }

    TypeRef *type_ref = arena_calloc(ctx->arena, sizeof(TypeRef));
    type_ref->type = TYPEREF_ARRAY;
    type_ref->array.elem = arena_calloc(ctx->arena, sizeof(TypeRef));
    type_ref->array.elem->type = TYPEREF_NAMED;
    type_ref->array.elem->named.name = string_make("char");
    type_ref->array.elem->type_symbol = lookup_symbol(ctx, string_make("char"));
    
    Symbol *sym = declare_symbol(ctx, string_make("string"), SYMFLAG_TYPE);

    type_ref->type_symbol = sym;
    sym->type = type_ref;
}

Scope *scope_init(Arena *a) {
    Scope *s = arena_calloc(a, sizeof(Scope));
    s->symbols = syms_array_init();
    s->parent = NULL;
    return s;
}

Analyser analyser_init(Module *m, Arena *a) {
    Analyser an;
    an.arena = a;
    an.current_function = NULL;
    an.global_scope = scope_init(a);
    an.current_scope = an.global_scope;
    an.module = m;

    inject_builtin_types(&an);

    return an;
}

void analyse(Analyser *ctx) {
    ctx->module->scope = ctx->global_scope;

    register_globals(ctx, ctx->module);
    resolve_types(ctx, ctx->module);
    check_bodies(ctx, ctx->module);
}

void enter_scope(Analyser *ctx, Scope *existing) {
    if (existing) {
        existing->parent = ctx->current_scope;
        ctx->current_scope = existing;
    } else {
        Scope *new_scope = scope_init(ctx->arena);
        new_scope->parent = ctx->current_scope;
        ctx->current_scope = new_scope;
    }
}

void leave_scope(Analyser *ctx) {
    if (ctx->current_scope && ctx->current_scope->parent) {
        ctx->current_scope = ctx->current_scope->parent;
    }
}

Symbol *declare_symbol(Analyser *ctx, String name, uint32_t flags) {
    for (size_t i = 0; i < ctx->current_scope->symbols.len; i++) {
        Symbol *sym = ctx->current_scope->symbols.data[i];
        if (string_eq(sym->name, name)) {
            if (sym->decl) {
                error(sym->decl->token, format("Redeclaration of symbol %.*s", string_fmt(sym->name)));
            } else {
                error((Token){}, format("Redeclaration of symbol %.*s", string_fmt(sym->name)));
            }
        }
    }

    Symbol *sym = arena_calloc(ctx->arena, sizeof(Symbol));
    sym->name = name;
    sym->flags = flags;
    sym->defined_in = ctx->current_scope;

    syms_array_push(&ctx->current_scope->symbols, sym);
    return sym;
}

Symbol *lookup_symbol(Analyser *ctx, String name) {
    Scope *scope = ctx->current_scope;

    while (scope != NULL) {
        for (size_t i = 0; i < scope->symbols.len; i++) {
            Symbol *sym = scope->symbols.data[i];
            if (string_eq(sym->name, name)) {
                return sym;
            }
        }
        scope = scope->parent;
    }

    return NULL;
}

void validate_decl_attrs(Analyser *ctx, Decl *d) {
    // first, general attributes
    for (size_t i = 0; i < d->attributes.len; i++) {
        Attribute *a = &d->attributes.data[i];
        if (a->consumed) continue;
        
        if (string_eq(a->name, string_make("export"))) {
            d->is_export = true;
            a->consumed = true;
        }
    }
    
    // now specific
    for (size_t i = 0; i < d->attributes.len; i++) {
        Attribute *a = &d->attributes.data[i];
        if (a->consumed) continue;
        
        bool found = false;
        switch (d->type) {
            case DECL_FN: {
                if (string_eq(a->name, string_make("extern"))) {
                    found = true;
                    d->fn->is_extern = true;
                    a->consumed = true;
                }
                break;
            }
            case DECL_STRUCT: {
                if (string_eq(a->name, string_make("packed"))) {
                    found = true;
                    d->_struct->align = 1;
                    a->consumed = true;
                }
                else if (string_eq(a->name, string_make("align"))) {
                    found = true;
                    if (a->args.len != 1 || a->args.data[0].type != LITERAL_INT) {
                        error(a->args.data[0].token, "Attribute only expects a single integer argument");
                    }
                    d->_struct->align = a->args.data[0]._int;
                    a->consumed = true;
                }
                break;
            }
            case DECL_TYPE: {
                break;
            }
            case DECL_UNION: {
                break;
            }
        }
        if (found) continue;
        
        error(a->token, format("Unknown attribute %.*s", string_fmt(a->name)));
    }
}

void register_globals(Analyser *ctx, Module *mod) {
    ctx->current_scope = ctx->global_scope;

    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *d = mod->decls.data[i];
        validate_decl_attrs(ctx, d);

        switch (d->type) {
            case DECL_FN: {
                uint32_t flags = SYMFLAG_FN;
                if (d->is_export) flags |= SYMFLAG_EXPORT;
                if (d->fn->is_extern) flags |= SYMFLAG_EXTERN;

                Symbol *sym = declare_symbol(ctx, d->fn->name, flags);
                TypeRef *fn_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                fn_type->type = TYPEREF_FN;
                fn_type->fn.ret_type = d->fn->ret_type;
                fn_type->fn.params = d->fn->params;
                sym->type = fn_type;
                sym->decl = d;
                d->symbol = sym;
                d->fn->symbol = sym;
                break;
            }
            case DECL_STRUCT: {
                Symbol *sym = declare_symbol(ctx, d->_struct->name, SYMFLAG_TYPE);
                sym->decl = d;
                d->symbol = sym;
                d->_struct->symbol = sym;
                break;
            }
            case DECL_UNION: {
                Symbol *sym = declare_symbol(ctx, d->_union->name, SYMFLAG_TYPE);
                sym->decl = d;
                d->symbol = sym;
                d->_union->symbol = sym;
                break;
            }
            case DECL_TYPE: {
                Symbol *sym = declare_symbol(ctx, d->_type->name, SYMFLAG_TYPE);
                sym->decl = d;
                sym->type = d->_type->alias;
                d->symbol = sym;
                d->_type->symbol = sym;
                break;
            }
            case DECL_ENUM: {
                Symbol *sym = declare_symbol(ctx, d->_enum->name, SYMFLAG_TYPE);

                TypeRef *enum_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                enum_type->type = TYPEREF_NAMED;
                enum_type->type_symbol = sym;
                sym->type = enum_type;

                sym->decl = d;
                d->symbol = sym;
                break;
            }
            case DECL_VAR: {
                error(d->token, "Global variables are not allowed");
            }
        }
    }
}

void resolve_typeref(Analyser *ctx, TypeRef *type) {
    if (!type) return;

    switch (type->type) {
        case TYPEREF_NAMED: {
            Symbol *sym = lookup_symbol(ctx, type->named.name);
            if (!sym || !(sym->flags & SYMFLAG_TYPE)) {
                error(type->token, "Unknown type");
            }

            type->type_symbol = sym;
            
            // type aliases are annoying
            // gotta do weird recursive stuffs
            // anyway it works now
            if (sym->type) {
                if (sym->type->type == TYPEREF_NAMED && 
                    sym->type->type_symbol == sym) {
                    return;
                }
                
                resolve_typeref(ctx, sym->type);
                
                type->type = sym->type->type;
                if (type->type == TYPEREF_ARRAY) type->array =sym->type->array;
                if (type->type == TYPEREF_POINTER) type->pointer = sym->type->pointer;
                if (type->type == TYPEREF_FN) type->fn = sym->type->fn;
                if (type->type == TYPEREF_NAMED) type->named = sym->type->named;
                type->type_symbol = sym->type->type_symbol;
            }
            break;
        }
        case TYPEREF_POINTER: {
            resolve_typeref(ctx, type->pointer.pointee);
            break;
        }
        case TYPEREF_ARRAY: {
            resolve_typeref(ctx, type->array.elem);
            break;
        }
        case TYPEREF_SUM: {
            for (size_t i = 0; i < type->sum.cases.len; i++) {
                resolve_typeref(ctx, type->sum.cases.data[i]);
            }
            break;
        }
    }
}

size_t get_type_size(TypeRef *type) {
    if (!type) return 0;

    switch (type->type) {
        case TYPEREF_POINTER: {
            return 8;   // TODO: architecture-dependant
        }
        case TYPEREF_ARRAY: {
            return get_type_size(type->array.elem) * type->array.length;
        }
        case TYPEREF_NAMED: {
            Symbol *sym = type->type_symbol;
            if (!sym) return 0;

            if (string_eq(sym->name, string_make("int8")) || string_eq(sym->name, string_make("uint8")) 
             || string_eq(sym->name, string_make("bool")) || string_eq(sym->name, string_make("char")) ) {
                return 1;
            }
            if (string_eq(sym->name, string_make("int16")) || string_eq(sym->name, string_make("uint16")) ) {
                return 2;
            }
            if (string_eq(sym->name, string_make("int32")) || string_eq(sym->name, string_make("uint32")) ) {
                return 4;
            }
            if (string_eq(sym->name, string_make("int64")) || string_eq(sym->name, string_make("uint64")) 
             || string_eq(sym->name, string_make("int")) || string_eq(sym->name, string_make("uint")) ) {
                return 8;
            }
            if (string_eq(sym->name, string_make("string")) ) {
                return 16;  // ptr + len
            }

            if (sym->decl->type == DECL_STRUCT) {
                return sym->decl->_struct->size;
            }
            if (sym->decl->type == DECL_UNION) {
                return sym->decl->_union->size;
            }

            return 0;
        }

        return 0;
    }
}

static size_t get_type_align(TypeRef *type) {
    if (!type) return 0;

    switch (type->type) {
        case TYPEREF_POINTER: {
            return 8;   // TODO: architecture-dependant
        }
        case TYPEREF_ARRAY: {
            return get_type_align(type->array.elem);
        }
        case TYPEREF_NAMED: {
            Symbol *sym = type->type_symbol;
            if (!sym) return 1;

            if (string_eq(sym->name, string_make("int8")) || string_eq(sym->name, string_make("uint8")) 
             || string_eq(sym->name, string_make("bool")) || string_eq(sym->name, string_make("char")) ) {
                return 1;
            }
            if (string_eq(sym->name, string_make("int16")) || string_eq(sym->name, string_make("uint16")) ) {
                return 2;
            }
            if (string_eq(sym->name, string_make("int32")) || string_eq(sym->name, string_make("uint32")) ) {
                return 4;
            }
            if (string_eq(sym->name, string_make("int64")) || string_eq(sym->name, string_make("uint64")) 
             || string_eq(sym->name, string_make("int")) || string_eq(sym->name, string_make("uint")) ) {
                return 8;
            }
            if (string_eq(sym->name, string_make("string")) ) {
                return 8;   // ptr + len
            }
            if (sym->decl->type == DECL_STRUCT) {
                return sym->decl->_struct->align;
            }
            if (sym->decl->type == DECL_UNION) {
                return sym->decl->_union->align;
            }

            return 1;
        }
        
        return 1;
    }
}

static void calculate_struct_layout(StructDecl *str) {
    size_t offset = 0;
    size_t max_align = 1;

    for (size_t i = 0; i < str->members.len; i++) {
        VarDecl *member = str->members.data[i];
        size_t member_size = get_type_size(member->type);
        size_t member_align = get_type_align(member->type);

        if (member_align > max_align) {
            max_align = member_align;
        }

        // Align the offset
        if (offset % member_align != 0) {
            offset += member_align - (offset % member_align);
        }

        str->field_offsets.data[i] = offset;
        offset += member_size;
    }

    // Final struct size must be a multiple of its alignment
    if (offset % max_align != 0) {
        offset += max_align - (offset % max_align);
    }

    str->size = offset;
    str->align = max_align;
}

static void calculate_union_layout(UnionDecl *unn) {
    size_t max_size = 0;
    size_t max_align = 1;

    for (size_t i = 0; i < unn->members.len; i++) {
        VarDecl *member = unn->members.data[i];
        size_t member_size = get_type_size(member->type);
        size_t member_align = get_type_align(member->type);

        if (member_size > max_size) {
            max_size = member_size;
        }
        if (member_align > max_align) {
            max_align = member_align;
        }
    }

    // Final union size must be a multiple of its alignment
    if (max_size % max_align != 0) {
        max_size += max_align - (max_size % max_align);
    }

    unn->size = max_size;
    unn->align = max_align;
}

void resolve_types(Analyser *ctx, Module *mod) {
    ctx->current_scope = ctx->global_scope;

    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *d = mod->decls.data[i];

        switch (d->type) {
            case DECL_FN: {
                enter_scope(ctx, NULL);

                if (d->fn->generic_params.len > 0) {
                    for (size_t k = 0; k < d->fn->generic_params.len; k++) {
                        String param_name = d->fn->generic_params.data[k].name;

                        Symbol *sym = declare_symbol(ctx, param_name, SYMFLAG_TYPE);

                        TypeRef *generic_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                        generic_type->type = TYPEREF_NAMED;
                        generic_type->type_symbol = sym;
                        sym->type = generic_type;
                    }
                }

                resolve_typeref(ctx, d->fn->ret_type);

                for (size_t j = 0; j < d->fn->params.len; j++) {
                    Param p = d->fn->params.data[j];
                    resolve_typeref(ctx, p.type);
                }

                leave_scope(ctx);
                break;
            }
            case DECL_STRUCT: {
                enter_scope(ctx, NULL);

                if (d->_struct->generic_params.len > 0) {
                    for (size_t k = 0; k < d->_struct->generic_params.len; k++) {
                        String param_name = d->_struct->generic_params.data[k].name;

                        Symbol *sym = declare_symbol(ctx, param_name, SYMFLAG_TYPE);

                        TypeRef *generic_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                        generic_type->type = TYPEREF_NAMED;
                        generic_type->type_symbol = sym;
                        sym->type = generic_type;
                    }
                }

                for (size_t j = 0; j < d->_struct->members.len; j++) {
                    VarDecl *m = d->_struct->members.data[j];
                    resolve_typeref(ctx, m->type);
                }

                if (d->_struct->generic_params.len == 0) {
                    calculate_struct_layout(d->_struct);
                }

                leave_scope(ctx);
                break;
            }
            case DECL_UNION: {
                for (size_t j = 0; j < d->_union->members.len; j++) {
                    VarDecl *m = d->_union->members.data[j];
                    resolve_typeref(ctx, m->type);
                }

                calculate_union_layout(d->_union);
                break;
            }
            case DECL_TYPE: {
                resolve_typeref(ctx, d->_type->alias);
                d->symbol->type = d->_type->alias;
                break;
            }
            case DECL_VAR: {
                error(d->token, "Global variables are not allowed %d");
            }
        }
    }
}

void check_stmt(Analyser *ctx, Stmt *stmt) {
    if (!stmt) return;

    stmt->scope = ctx->current_scope;

    switch (stmt->type) {
        case STMT_BLOCK: {
            enter_scope(ctx, NULL);
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                Stmt *s = stmt->block.stmts.data[i];
                check_stmt(ctx, s);
            }
            leave_scope(ctx);
            break;
        }
        case STMT_VAR: {
            VarDecl *var = stmt->var;
            resolve_typeref(ctx, var->type);

            if (var->init) {
                if (var->init->type == EXPR_INIT) {
                    var->init->resolved_type = var->type;
                }

                TypeRef *init_type = check_expr(ctx, var->init);

                if (!types_compatible(init_type, var->type)) {
                    if (init_type->type_symbol && var->type->type_symbol) {
                        error(var->token, format("Cannot assign value of type %.*s to variable of type %.*s", string_fmt(init_type->type_symbol->name), string_fmt(var->type->type_symbol->name)));
                    } else {
                        error(var->token, "Cannot assign variables of differing types");
                    }
                }
            }

            uint32_t flags = SYMFLAG_VAR;
            if (var->is_mutable) flags |= SYMFLAG_MUT;
            if (var->type->type == TYPEREF_FN) flags |= SYMFLAG_FN;

            Symbol *sym = declare_symbol(ctx, var->name, flags);
            sym->type = var->type;
            var->symbol = sym;
            break;
        }
        case STMT_RETURN: {
            if (stmt->_return.value) {
                TypeRef *ret_type = check_expr(ctx, stmt->_return.value);
                if (!types_compatible(ret_type, ctx->current_function->ret_type)) {
                    error(stmt->_return.value->token, format("Cannot return %.*s in function expecting %.*s", string_fmt(type_to_string(ret_type)), string_fmt(type_to_string(ctx->current_function->ret_type))));
                }
            } else {
                if (ctx->current_function->ret_type
                 && !string_eq(ctx->current_function->ret_type->type_symbol->name, string_make("none"))) {
                    error(stmt->token, "Function expects a return value");
                 }
            }
            break;
        }
        case STMT_EXPR: {
            check_expr(ctx, stmt->expr);
            break;
        }
        case STMT_IF: {
            if (stmt->_if.cond) {
                TypeRef *cond_type = check_expr(ctx, stmt->_if.cond);
                if (!cond_type || !string_eq(cond_type->type_symbol->name, string_make("bool"))) {
                    error(stmt->token, "Condition must be of boolean type");
                }
            }

            check_stmt(ctx, stmt->_if.then);
            if (stmt->_if._else) {
                check_stmt(ctx, stmt->_if._else);
            }
            break;
        }
        case STMT_WHILE: {
            if (stmt->_while.cond) {
                TypeRef *cond_type = check_expr(ctx, stmt->_while.cond);
                if (!cond_type || !string_eq(cond_type->type_symbol->name, string_make("bool"))) {
                    error(stmt->token, "Condition must be of boolean type");
                }
            }

            check_stmt(ctx, stmt->_while.body);
            break;
        }
        case STMT_FOR: {
            enter_scope(ctx, NULL);

            if (stmt->_for.init) {
                check_stmt(ctx, stmt->_for.init);
            }

            if (stmt->_for.cond) {
                TypeRef *cond_type = check_expr(ctx, stmt->_for.cond);
                if (!cond_type || !string_eq(cond_type->type_symbol->name, string_make("bool"))) {
                    error(stmt->token, "Condition must be of boolean type");
                }
            }

            if (stmt->_for.post) {
                check_expr(ctx, stmt->_for.post);
            }

            if (stmt->_for.body) {
                check_stmt(ctx, stmt->_for.body);
            }

            leave_scope(ctx);

            break;
        }
    }
}

void check_fn_body(Analyser *ctx, FnDecl *fn) {
    ctx->current_function = fn;

    enter_scope(ctx, NULL);
    fn->local_scope = ctx->current_scope;

    if (fn->generic_params.len > 0) {
        for (size_t k = 0; k < fn->generic_params.len; k++) {
            String param_name = fn->generic_params.data[k].name;

            Symbol *sym = declare_symbol(ctx, param_name, SYMFLAG_TYPE);
            TypeRef *generic_type = arena_calloc(ctx->arena, sizeof(TypeRef));
            generic_type->type = TYPEREF_NAMED;
            generic_type->type_symbol = sym;
            sym->type = generic_type;
        }
    }

    for (size_t i = 0; i < fn->params.len; i++) {
        Param *p = &fn->params.data[i];
        Symbol *sym = declare_symbol(ctx, p->name, SYMFLAG_VAR);
        sym->type = p->type;
        p->symbol = sym;
    }

    if (fn->body) {
        check_stmt(ctx, fn->body);
    }

    leave_scope(ctx);
    ctx->current_function = NULL;
}

void check_bodies(Analyser *ctx, Module *mod) {
    ctx->current_scope = ctx->global_scope;

    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *d = mod->decls.data[i];
        if (d->type == DECL_FN) {
            if (d->fn->body && d->fn->is_extern) {
                error(d->fn->token, format("Extern function %.*s cannot have a body definition", string_fmt(d->fn->name)));
            }
            check_fn_body(ctx, d->fn);
        }
    }
}

static bool is_integer_type(TypeRef *type) {
    if (!type || type->type != TYPEREF_NAMED) return false;
    Symbol *sym = type->type_symbol;
    if (!sym) return false;

    return string_eq(sym->name, string_make("int"))   ||
           string_eq(sym->name, string_make("int8"))  ||
           string_eq(sym->name, string_make("int16")) ||
           string_eq(sym->name, string_make("int32")) ||
           string_eq(sym->name, string_make("int64")) ||
           string_eq(sym->name, string_make("uint"))  ||
           string_eq(sym->name, string_make("uint8")) ||
           string_eq(sym->name, string_make("uint16"))||
           string_eq(sym->name, string_make("uint32"))||
           string_eq(sym->name, string_make("uint64"));
}

static String type_name(TypeRef *type) {
    if (!type) {
        return string_make("<unknown>");
    }

    switch (type->type) {
        case TYPEREF_NAMED: {
            return type->named.name;
        }
        
        case TYPEREF_POINTER: {
            return string_make(format("*%.*s", string_fmt(type_name(type->pointer.pointee)))); // NOTE: this is godawful, but it only runs on errors so it's fiiiiiine
        }

        case TYPEREF_ARRAY: {
            return string_make(format("%.*s[%d]", string_fmt(type_name(type->array.elem)), type->array.length));
        }
    }
    return string_make("<unknown>");
}

TypeRef *check_expr(Analyser *ctx, Expr *expr) {
    if (!expr) return NULL;

    TypeRef *result_type = NULL;
    switch (expr->type) {
        case EXPR_LIT: {
            String type_name;
            switch (expr->literal.type) {
                case LITERAL_INT: type_name = string_make("int"); break;
                case LITERAL_UINT: type_name = string_make("uint"); break;
                case LITERAL_BOOL: type_name = string_make("bool"); break;
                case LITERAL_STRING: type_name = string_make("string"); break;
                case LITERAL_CHAR: type_name = string_make("char"); break;
                case LITERAL_NULL: type_name = string_make("$null"); break;
            }
            Symbol *type_sym = lookup_symbol(ctx, type_name);
            result_type = type_sym ? type_sym->type : NULL;
            goto check_expr_finished;
        }
        case EXPR_IDENT: {
            Symbol *sym = lookup_symbol(ctx, expr->ident.name);
            if (!sym) {
                error(expr->token, "Unknown variable");
            }
            expr->symbol = sym;
            result_type = sym->type;
            goto check_expr_finished;
        }
        case EXPR_BINARY: {
            TypeRef *left_t = check_expr(ctx, expr->binary.left);
            TypeRef *right_t = check_expr(ctx, expr->binary.right);

            if (!types_compatible(left_t, right_t)) {
                error(expr->token, format("Cannot operate between incompatible types %.*s and %.*s", string_fmt(type_to_string(left_t)), string_fmt(type_to_string(right_t))));
            }

            if (expr->binary.op == BINOP_EQ || 
                expr->binary.op == BINOP_LT || 
                expr->binary.op == BINOP_LE || 
                expr->binary.op == BINOP_GT || 
                expr->binary.op == BINOP_GE || 
                expr->binary.op == BINOP_NE) {
                result_type = lookup_symbol(ctx, string_make("bool"))->type;
                goto check_expr_finished;
            }

            if (expr->binary.op == BINOP_ASSIGN) {
                if (!types_compatible(left_t, right_t)) {
                    error(expr->token, "Can only assign equal types");
                }

                if (!left_t->is_mutable) {
                    error(expr->token, "Can only modify mutable types");
                }

                result_type = left_t;
                goto check_expr_finished;
            }

            result_type = left_t;
            goto check_expr_finished;
        }
        case EXPR_CALL: {
            check_expr(ctx, expr->call.callee);

            Symbol *callee_sym = expr->call.callee->symbol;
            TypeRef *callee_type = expr->call.callee->resolved_type;
            if (!callee_type) {
                if (callee_sym && callee_sym->decl) {
                    error(callee_sym->decl->token, "Unknown function");
                }
                error(expr->call.callee->token, "Unknown function");
            }

            if (callee_type->type != TYPEREF_FN) {
                if (callee_sym && callee_sym->decl) {
                    error(callee_sym->decl->token, "Cannot call non-function");
                } else {
                    error(expr->call.callee->token, "Cannot call non-function");
                }
            }

            bool is_method_call = false;
            Expr *method_base = NULL;

            if (expr->call.callee->type == EXPR_MEMBER) {
                method_base = expr->call.callee->member.base;
                is_method_call = true;
            }

            size_t expected_params = callee_type->fn.params.len;
            size_t provided_args = expr->call.args.len;
            size_t virtual_args_len = is_method_call ? (provided_args + 1) : provided_args;

            if (virtual_args_len != expected_params) {
                if (callee_sym && callee_sym->decl) {
                    error(callee_sym->decl->token, format("Function expects %ld arguments", expected_params));
                } else {
                    error(expr->call.callee->token, format("Function expects %ld arguments, got %ld", expected_params, provided_args));
                }
            }

            for (size_t i = 0; i < expected_params; i++) {
                TypeRef *param_type = callee_type->fn.params.data[i].type;
                resolve_typeref(ctx, param_type);
                TypeRef *arg_type = NULL;

                if (is_method_call && i == 0) {
                    arg_type = check_expr(ctx, method_base);
                } else {
                    size_t arg_idx = is_method_call ? (i - 1) : i;
                    Expr *arg = expr->call.args.data[arg_idx];
                    arg_type = check_expr(ctx, arg);
                }

                if (!types_compatible(arg_type, param_type)) {
                    error(expr->token, format("Cannot pass argument of type %.*s to parameter expecting type %.*s", 
                        string_fmt(type_to_string(arg_type)), string_fmt(type_to_string(param_type))));
                }
            }

            result_type = callee_type->fn.ret_type;
            break;
        }
        case EXPR_MEMBER: {
            TypeRef *base_type = check_expr(ctx, expr->member.base);
            if (!base_type) {
                result_type = NULL;
                goto check_expr_finished;
            }

            if (base_type->type == TYPEREF_ARRAY) {
                if (string_eq(expr->member.member, string_make("ptr"))) {
                    // Construct a pointer to the array's underlying element type
                    TypeRef *ptr_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                    ptr_type->type = TYPEREF_POINTER;
                    ptr_type->pointer.pointee = base_type->array.elem;
                    
                    result_type = ptr_type;
                    goto check_expr_finished;
                } else if (string_eq(expr->member.member, string_make("len"))) {
                    // len is a uint64
                    Symbol *uint64_sym = lookup_symbol(ctx, string_make("uint64"));
                    result_type = uint64_sym->type;
                    goto check_expr_finished;
                } else {
                    error(expr->member.base->token, format("Unknown array member %.*s", string_fmt(expr->member.member)));
                }
            } else {
                TypeRef *actual_struct_type = base_type;
                
                if (actual_struct_type->type == TYPEREF_POINTER && expr->member.deref) {
                    actual_struct_type = actual_struct_type->pointer.pointee;
                }

                if (actual_struct_type->type == TYPEREF_GENERIC) {
                    actual_struct_type = actual_struct_type->generic.base_type;
                }

                Symbol *type_sym = actual_struct_type->type_symbol;
                if (!type_sym) {
                    error(expr->member.base->token, format("Unknown base type %.*s", string_fmt(type_to_string(base_type))));
                }

                if (!type_sym->decl) {
                    FnDecl *current_fn = ctx->current_function;

                    if (current_fn && current_fn->generic_params.len > 0) {
                        for (size_t i = 0; i < current_fn->generic_params.len; i++) {
                            if (string_eq(current_fn->generic_params.data[i].name, type_sym->name)) {
                                for (size_t j = 0; j < current_fn->generic_params.data[i].constraints.len; j++) {
                                    FnDecl *constraint = current_fn->generic_params.data[i].constraints.data[j];
    
                                    if (constraint && string_eq(constraint->name, expr->member.member)) {
                                        resolve_typeref(ctx, constraint->ret_type);
                                        for (size_t k = 0; k < constraint->params.len; k++) {
                                            resolve_typeref(ctx, constraint->params.data[k].type);
                                        }

                                        TypeRef *fn_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                                        fn_type->type = TYPEREF_FN;
                                        fn_type->fn.ret_type = constraint->ret_type;
                                        fn_type->fn.params = constraint->params;
                                        
                                        result_type = fn_type;
                                        expr->symbol = NULL;
                                        goto check_expr_finished;
                                    }
                                }
                            }
                        }

                        error(expr->token, format("Generic parameter %.*s has no method named %.*s", string_fmt(type_sym->name), string_fmt(expr->member.member)));
                    }

                    // TODO: ensure constraints allow this, otherwise error
                    error(expr->token, format("Type placeholder %.*s does not have member elements", string_fmt(type_sym->name)));
                }

                if (!(type_sym->flags & SYMFLAG_TYPE)) {
                    error(expr->member.base->token, format("Unknown base type %.*s", string_fmt(type_to_string(base_type))));
                }
    
                StructDecl *str = NULL;
                UnionDecl *unn = NULL;
    
                if (type_sym->decl->type == DECL_STRUCT) str = type_sym->decl->_struct;
                else if (type_sym->decl->type == DECL_UNION) unn = type_sym->decl->_union;
    
                if (!str && !unn) {
                    error(expr->token, "Cannot get member of type without members");
                }
    
                VarDecl *found_member = NULL;
                size_t member_index = 0;

                for (size_t i = 0; i < (str ? str->members.len : unn->members.len); i++) {
                    if (string_eq(str ? str->members.data[i]->name : unn->members.data[i]->name, expr->member.member)) {
                        found_member = str ? str->members.data[i] : unn->members.data[i];
                        member_index = i;
                        break;
                    }
                }

                if (!found_member) {
                    error(expr->token, "Unknown member");
                }
    
                if (base_type->type == TYPEREF_GENERIC) {
                    TypeRef *member_type = found_member->type;

                    if (member_type->type == TYPEREF_NAMED && member_type->type_symbol->flags & SYMFLAG_GENERIC) {
                        int param_idx = -1;
                        for (size_t k = 0; k < (str ? str->generic_params.len : unn->generic_params.len); k++) {
                            if (string_eq(str ? str->generic_params.data[k].name : unn->generic_params.data[k].name, member_type->named.name)) {
                                param_idx = k;
                                break;
                            }
                        }

                        if (param_idx != -1) {
                            result_type = base_type->generic.arg_types.data[param_idx];
                        } else {
                            result_type = member_type;
                        }
                    } else {
                        result_type = member_type;
                    }
                } else {
                    result_type = found_member->type;
                }
                goto check_expr_finished;
            }
        }
        case EXPR_UNARY: {
            TypeRef *operand_type = check_expr(ctx, expr->unary.operand);
            if (!operand_type) result_type = NULL;

            switch (expr->unary.op) {
                case UOP_NEG: {
                    String name = operand_type->type_symbol->name;
                    if (!is_integer_type(operand_type)) {
                        error(operand_type->token, "Can only negate integers");
                    }
                    result_type = operand_type;
                    goto check_expr_finished;
                }
                case UOP_NOT: {
                    if (!string_eq(operand_type->type_symbol->name, string_make("bool"))) {
                        error(operand_type->token, "Can only '!' booleans");
                    }
                    result_type = operand_type;
                    goto check_expr_finished;
                }
                case UOP_ADDR: {
                    TypeRef *p = arena_calloc(ctx->arena, sizeof(TypeRef));
                    p->type = TYPEREF_POINTER;
                    p->pointer.pointee = operand_type;
                    p->is_mutable = false;
                    result_type = p;
                    goto check_expr_finished;
                }
                case UOP_DEREF: {
                    if (operand_type->type == TYPEREF_POINTER) {
                        result_type = operand_type->pointer.pointee;
                        goto check_expr_finished;
                    }

                    error(operand_type->token, "Cannot dereference non-pointer");
                }
            }
        }
        case EXPR_INDEX: {
            TypeRef *base_type = check_expr(ctx, expr->index.base);
            TypeRef *index_type = check_expr(ctx, expr->index.index);

            if (!index_type || !is_integer_type(index_type)) {
                if (index_type) {
                    error(index_type->token, "Array index must be an integer");
                } else {
                    error(expr->token, "Array index must be an integer");
                }
            }

            switch (base_type->type) {
                //TODO: only allow in unsafe block
                case TYPEREF_POINTER: {
                    result_type = base_type->pointer.pointee;
                    goto check_expr_finished;
                }
                case TYPEREF_ARRAY: {
                    result_type = base_type->array.elem;
                    goto check_expr_finished;
                }
                default: {
                    error(base_type->token, "Cannot index into non-array type");
                }
            }
        }
        case EXPR_CAST: {
            check_expr(ctx, expr->cast.expr);

            resolve_typeref(ctx, expr->cast.to);

            result_type = expr->cast.to;
            goto check_expr_finished;
        }
        case EXPR_INTRINSIC: {
            Intrinsic *p = &expr->intrinsic;
            if (p->is_arg_type) {
                resolve_typeref(ctx, p->type);
                if (!p->type->type_symbol) {
                    error(expr->token, format("Unknown type %.*s", string_fmt(type_to_string(p->type))));
                }
                if (string_eq(p->name, string_make("sizeof"))) {
                    size_t sz = get_type_size(p->type);
                    if (sz == 0) error(expr->token, format("Cannot take sizeof of incomplete type %.*s", string_fmt(type_to_string(p->type))));
                    result_type = lookup_symbol(ctx, string_make("int"))->type;
                    expr->type = EXPR_LIT;
                    expr->literal.type = LITERAL_INT;
                    expr->literal._int = (int64_t)sz;
                }
                else {
                    // TODO: typeid stuff
                    error(expr->token, "TODO");
                }
            } else {
                TypeRef *t = check_expr(ctx, p->expr);
                if (string_eq(p->name, string_make("sizeof"))) {
                    size_t sz = get_type_size(t);
                    if (sz == 0) error(expr->token, format("Cannot take sizeof of incomplete type %.*s", string_fmt(type_to_string(t))));
                    result_type = lookup_symbol(ctx, string_make("int"))->type;
                    expr->type = EXPR_LIT;
                    expr->literal.type = LITERAL_INT;
                    expr->literal._int = (int64_t)sz;
                } else {
                    // TODO: typeid stuff
                    error(expr->token, "TODO");
                }
            }
            break;
        }
        case EXPR_PATH: {
            if (expr->path.components.len < 2) {
                error(expr->token, "Invalid path expression"); // shouldn't be possible but just in case
            }

            String base_name = expr->path.components.data[0];
            Symbol *parent_sym = lookup_symbol(ctx, base_name);
            if (!parent_sym) {
                error(expr->token, format("Unknown namespace %.*s", string_fmt(base_name)));
            }

            String target_name = expr->path.components.data[1];

            if (parent_sym->decl && parent_sym->decl->type == DECL_ENUM) {
                EnumDecl *en = parent_sym->decl->_enum;
                bool found = false;

                for (size_t i = 0; i < en->variants.len; i++) {
                    if (string_eq(en->variants.data[i].name, target_name)) {
                        result_type = parent_sym->type;
                        expr->symbol = parent_sym;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    error(expr->token, format("Enum %.*s has no variant %.*s", string_fmt(parent_sym->name), string_fmt(target_name)));
                }
            }
            else if (parent_sym->decl && parent_sym->decl->type == DECL_UNION) {
                UnionDecl *unn = parent_sym->decl->_union;
                bool found = false;

                for (size_t i = 0; i < unn->members.len; i++) {
                    if (string_eq(unn->members.data[i]->name, target_name)) {
                        result_type = parent_sym->type;
                        expr->symbol = parent_sym;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    error(expr->token, format("Union %.*s has no constructor variant %.*s", string_fmt(parent_sym->name), string_fmt(target_name)));
                }
            } 
            else {
                error(expr->token, "Path resolution is only supported on Enums and Unions currently");
            }

            goto check_expr_finished;
        }
        case EXPR_BUBBLE: {
            TypeRef *inner_t = check_expr(ctx, expr->bubble.expr);
            if (!inner_t) {
                result_type = NULL;
                goto check_expr_finished;
            }

            if (inner_t->type != TYPEREF_SUM) {
                error(expr->token, format("Cannot use '?' on non-sum type %.*s", string_fmt(type_to_string(inner_t))));
            }

            if (inner_t->sum.cases.len == 0) {
                error(expr->token, "Empty sum type cannot be bubbled");
            }

            TypeRef *success_type = inner_t->sum.cases.data[0];
            
            // TODO: validate that the remaining variants in inner_t->sum.cases 
            // are compatible with ctx->current_function->ret_type's error variants

            result_type = success_type;
            goto check_expr_finished;
        }
        case EXPR_INIT: {
            if (expr->resolved_type) {
                result_type = expr->resolved_type;
                goto check_expr_finished;
            }

            for (size_t i = 0; i < expr->init_list.fields.len; i++) {
                check_expr(ctx, expr->init_list.fields.data[i].value);
            }

            result_type = NULL;
            goto check_expr_finished;
        }
        default: {
            result_type = NULL;
            goto check_expr_finished;
        }
    }

check_expr_finished:

    expr->resolved_type = result_type;
    return result_type;
}

bool types_equal(TypeRef *a, TypeRef *b) {
    if (a == b) return true;

    if (!a || !b) return false;

    if (a->is_optional != b->is_optional) return false;

    if (a->type != b->type) return false;

    switch (a->type) {
        case TYPEREF_NAMED: {
            return a->type_symbol == b->type_symbol;
        }
        case TYPEREF_POINTER: {
            return types_equal(a->pointer.pointee, b->pointer.pointee);
        }
        case TYPEREF_ARRAY: {
            return (a->array.length == b->array.length) && types_equal(a->array.elem, b->array.elem);
        }
        default: {
            return false;
        }
    }
}

bool types_compatible(TypeRef *src, TypeRef *dst) {
    if (!src || !dst) return false;

    if (types_equal(src, dst)) return true;

    if (dst->type == TYPEREF_SUM) {
        for (size_t i = 0; i < dst->sum.cases.len; i++) {
            if (types_compatible(src, dst->sum.cases.data[i])) return true;
        }
        return false;
    }

    if (src->type == TYPEREF_NAMED && string_eq(src->type_symbol->name, string_make("$null"))) {
        // `null` may only be assigned to optional types.
        return dst->is_optional;
    }

    if (src->is_optional != dst->is_optional) return false;

    if (src->type != dst->type) return false;

    if (src->type == TYPEREF_NAMED) {
        if ((is_signed_integer(src) || is_unsigned_integer(src)) &&
            (is_signed_integer(dst) || is_unsigned_integer(dst))) {

            size_t src_bits = integer_size_bits(src);
            size_t dst_bits = integer_size_bits(dst);

            if (dst_bits >= src_bits) {
                if (is_signed_integer(src) && is_unsigned_integer(dst)) {
                    return false;
                }
                return true;
            }
            return false;
        }

        if (src->type_symbol == dst->type_symbol) {
            return true;
        }

        if (src->type_symbol && dst->type_symbol) {
            bool src_is_generic = (src->type_symbol->flags & SYMFLAG_GENERIC) || 
                                  (src->type_symbol->flags & SYMFLAG_TYPE && !src->type_symbol->decl);
            bool dst_is_generic = (dst->type_symbol->flags & SYMFLAG_GENERIC) || 
                                  (dst->type_symbol->flags & SYMFLAG_TYPE && !dst->type_symbol->decl);

            if (src_is_generic && dst_is_generic) {
                return string_eq(src->type_symbol->name, dst->type_symbol->name);
            }
        }

        return false;
    }

    if (src->type == TYPEREF_POINTER) {
        return types_compatible(src->pointer.pointee, dst->pointer.pointee);
    }

    if (src->type == TYPEREF_ARRAY) {
        if (dst->array.length != 0 && src->array.length != dst->array.length) {
            return false;
        }
        
        return types_compatible(src->array.elem, dst->array.elem);
    }

    if (src->type == TYPEREF_FN) {
        if (!types_compatible(src->fn.ret_type, dst->fn.ret_type)) return false;

        if (src->fn.params.len != dst->fn.params.len) return false;
        for (size_t i = 0; i < src->fn.params.len; i++) {
            if (!types_compatible(src->fn.params.data[i].type, dst->fn.params.data[i].type)) return false;
        }

        return true;
    }

    if (src->type == TYPEREF_SUM) {
        for (size_t i = 0; i < src->sum.cases.len; i++) {
            if (types_compatible(src->sum.cases.data[i], dst)) return true;
        }
        return false;
    }

    return false;
}
