#include <dirent.h>
#include <sys/stat.h>
#include "sema.h"
#include "error.h"

static String generate_mangled_name(Analyser *ctx, Module *mod, String sym_name, Token err_tok);
Symbol *declare_symbol(Analyser *ctx, String name, uint32_t flags);
Symbol *lookup_symbol(Analyser *ctx, String name);
void register_globals(Analyser *ctx, Module *mod);
void resolve_types(Analyser *ctx, Module *mod);
void check_bodies(Analyser *ctx, Module *mod);
TypeRef *check_expr(Analyser *ctx, Expr *expr);
bool types_compatible(TypeRef *src, TypeRef *dst);

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
    printf("Creating new scope %p\n", s);
    s->symbols = syms_array_init(a);
    s->parent = NULL;
    return s;
}

Analyser analyser_init(Module *m, Arena *a) {
    m->scope = scope_init(a);
    Analyser an;
    an.arena = a;
    an.current_function = NULL;
    an.global_scope = m->scope;
    an.current_scope = an.global_scope;
    an.module = m;
    an.module_map = modentry_array_init(a);

    inject_builtin_types(&an);

    return an;
}

void analyse(Analyser *ctx) {
    ctx->module->scope = ctx->global_scope;

    register_globals(ctx, ctx->module);
    for (size_t i = 0; i < ctx->module_map.len; i++) {
        if (ctx->module_map.data[i].is_parsed) {
            register_globals(ctx, ctx->module_map.data[i].ast);
        }
    }

    resolve_types(ctx, ctx->module);
    for (size_t i = 0; i < ctx->module_map.len; i++) {
        if (ctx->module_map.data[i].is_parsed) {
            resolve_types(ctx, ctx->module_map.data[i].ast);
        }
    }

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
                error(sym->decl->token, format("Redeclaration of symbol '%.*s'", string_fmt(sym->name)));
            } else {
                error(sym->type->token, format("Redeclaration of symbol '%.*s'", string_fmt(sym->name)));
            }
        }
        sym->defined_in = ctx->current_scope;
    }

    Symbol *sym = arena_calloc(ctx->arena, sizeof(Symbol));
    sym->name = name;
    sym->flags = flags;
    sym->defined_in = ctx->current_scope;

    syms_array_push(&ctx->current_scope->symbols, sym);
    return sym;
}

void print_indent(int indent) { for (int i = 0; i < indent; i++) putchar(' '); }

void print_scope(Scope *scope, int indent) {
    print_indent(indent); printf("Scope %p: %zu symbols:\n", scope, scope->symbols.len);
    for (size_t i = 0; i < scope->symbols.len; i++) {
        print_indent(indent + 2); printf("- %.*s\n", string_fmt(scope->symbols.data[i]->name));
    }
    if (scope->parent) print_scope(scope->parent, indent + 2);
}

Symbol *lookup_symbol(Analyser *ctx, String name) {
    Scope *scope = ctx->current_scope;

    while (scope != NULL) {
        for (size_t i = 0; i < scope->symbols.len; i++) {
            Symbol *sym = scope->symbols.data[i];
            fprintf(stderr, "symbol %.*s scope %p\n", string_fmt(sym->name), scope);
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
            case DECL_VAR: {
                break;
            }
            case DECL_ENUM: {
                break;
            }
        }
        if (found) continue;
        
        error(a->token, format("Unknown attribute '%.*s'", string_fmt(a->name)));
    }
}

void register_globals(Analyser *ctx, Module *mod) {
    ctx->current_scope = mod->scope;

    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *d = mod->decls.data[i];

        // skip globals that have already been registered
        // namely, monomorphised decls
        if (d->symbol) continue;

        validate_decl_attrs(ctx, d);

        switch (d->type) {
            case DECL_FN: {
                uint32_t flags = SYMFLAG_FN;
                if (d->is_export) flags |= SYMFLAG_EXPORT;
                if (d->fn->is_extern) flags |= SYMFLAG_EXTERN;

                Symbol *sym = declare_symbol(ctx, d->fn->name, flags);
                if (d->is_export) sym->mangled = generate_mangled_name(ctx, mod, d->fn->name, d->token);
                else sym->mangled = sym->name;
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
                uint32_t flags = SYMFLAG_VAR;
                if (d->is_export) flags |= SYMFLAG_EXPORT;

                Symbol *sym = declare_symbol(ctx, d->var->name, flags);

                if (d->is_export) {
                    sym->mangled = generate_mangled_name(ctx, mod, d->var->name, d->token);
                } else {
                    sym->mangled = sym->name;
                }

                sym->decl = d;
                d->symbol = sym;
                break;
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
                if (sym) {
                    error(type->token, format("Unknown type '%.*s'", string_fmt(sym->name)));
                } else {
                    error(type->token, format("Unknown type '%.*s'", string_fmt(type_to_string(type))));
                }
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
        case TYPEREF_FN: {
            resolve_typeref(ctx, type->fn.ret_type);
            for (size_t i = 0; i < type->fn.params.len; i++) {
                resolve_typeref(ctx, type->fn.params.data[i].type);
            }
            break;
        }
    }
}

size_t get_type_size(TypeRef *type) {
    if (!type) return 0;

    switch (type->type) {
        case TYPEREF_FN:
        case TYPEREF_POINTER: {
            return 8;   // TODO: architecture-dependant
        }
        case TYPEREF_ARRAY: {
            if (type->array.length > 0) {
                return 8 + get_type_size(type->array.elem) * type->array.length;
            } else {
                return 16;
            }
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
            
            if (!sym->decl) return 0;
            if (sym->decl->type == DECL_STRUCT) {
                return sym->decl->_struct->size;
            }
            if (sym->decl->type == DECL_UNION) {
                return sym->decl->_union->size;
            }

            return 0;
        }
        case TYPEREF_SUM: {
            size_t highest = 0;
            for (size_t i = 0; i < type->sum.cases.len; i++) {
                if (get_type_size(type->sum.cases.data[i]) > highest) {
                    highest = get_type_size(type->sum.cases.data[i]);
                }
            }
            return highest;
        }

        return 0;
    }

    return 0;
}

static size_t get_type_align(TypeRef *type) {
    if (!type) return 0;

    switch (type->type) {
        case TYPEREF_FN:
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
        case TYPEREF_SUM: {
            size_t highest = 1;
            for (size_t i = 0; i < type->sum.cases.len; i++) {
                if (get_type_align(type->sum.cases.data[i]) > highest) {
                    highest = get_type_align(type->sum.cases.data[i]);
                }
            }
            return highest;
        }
        
        return 1;
    }

    return 0;
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

        // align the offset
        if (offset % member_align != 0) {
            offset += member_align - (offset % member_align);
        }

        str->field_offsets.data[i] = offset;
        offset += member_size;
    }

    // final struct size must be a multiple of its alignment
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

    // final union size must be a multiple of its alignment
    if (max_size % max_align != 0) {
        max_size += max_align - (max_size % max_align);
    }

    unn->size = max_size;
    unn->align = max_align;
}

static TypeRef *check_expr_with_target(Analyser *ctx, Expr *expr, TypeRef *target_type) {
    if (expr->type == EXPR_INIT) {
        if (target_type->type != TYPEREF_NAMED || target_type->type_symbol->decl->type != DECL_STRUCT) {
            error(expr->token, "Initialiser list can only be inferred to a struct type");
        }
        
        StructDecl *str = target_type->type_symbol->decl->_struct;
        expr->resolved_type = target_type;

        for (size_t i = 0; i < expr->init_list.fields.len; i++) {
            InitField *f = &expr->init_list.fields.data[i];
            
            if (f->field_name.has_value) {
                VarDecl *member;

                for (size_t i = 0; i < str->members.len; i++) {
                    if (string_eq(str->members.data[i]->name, f->field_name.value)) {
                        member = str->members.data[i];
                        break;
                    }
                }

                if (!member) {
                    error(f->token, format("Struct '%.*s' has no member named '%.*s'", 
                        string_fmt(str->name), string_fmt(f->field_name.value)));
                }
                
                TypeRef *field_expr_type = check_expr(ctx, f->value);
                if (!types_compatible(field_expr_type, member->type)) {
                    error(f->token, format("Cannot assign to member of type '%.*s' object of type '%.*s'", string_fmt(type_to_string(member->type)), string_fmt(type_to_string(field_expr_type))));
                }
            } else {
                if (i >= str->members.len) {
                    error(f->token, "Too many elements in structural initialiser list");
                }
                VarDecl *member = str->members.data[i];
                TypeRef *field_expr_type = check_expr(ctx, f->value);
                if (!types_compatible(member->type, field_expr_type)) {
                    error(f->token, "Type mismatch for positional initialiser field");
                }
            }
        }
        return target_type;
    }

    return check_expr(ctx, expr);
}

void resolve_types(Analyser *ctx, Module *mod) {
    ctx->current_scope = mod->scope;

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

                resolve_typeref(ctx, d->symbol->type->fn.ret_type);
                for (size_t p = 0; p < d->symbol->type->fn.params.len; p++) {
                    resolve_typeref(ctx, d->symbol->type->fn.params.data[p].type);
                    
                    if (d->symbol->type->fn.params.data[p].default_value) {
                        Scope *old = ctx->current_scope;
                        ctx->current_scope = d->symbol->defined_in;
                        fprintf(stderr, "entering new scope! %.*s\n", string_fmt(d->symbol->mangled));
                        check_expr(ctx, d->symbol->type->fn.params.data[p].default_value);
                        ctx->current_scope = old;
                    }
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
            case DECL_ENUM: {
                resolve_typeref(ctx, d->_enum->type);
                break;
            }
            case DECL_VAR: {
                VarDecl *v = d->var;

                resolve_typeref(ctx, v->type);

                d->symbol->type = v->type;

                if (v->init) {
                    TypeRef *init_type;
                    if (v->init->type == EXPR_INIT) {
                        init_type = check_expr_with_target(ctx, v->init, v->type);
                    } else {
                        init_type = check_expr(ctx, v->init);
                    }

                    if (!types_compatible(v->type, init_type)) {
                        error(v->token, format("Cannot assign variable of type '%.*s' object of type '%.*s'", string_fmt(type_to_string(v->type)), string_fmt(type_to_string(init_type))));
                    }
                }
            }
        }
    }
}

void check_stmt(Analyser *ctx, Stmt *stmt) {
    if (!stmt) return;

    stmt->scope = ctx->current_scope;

    switch (stmt->type) {
        case STMT_UNSAFE:
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
                        error(var->token, format("Cannot assign value of type '%.*s' to variable of type '%.*s'", string_fmt(init_type->type_symbol->name), string_fmt(var->type->type_symbol->name)));
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
                    error(stmt->_return.value->token, format("Cannot return '%.*s' in function expecting '%.*s'", string_fmt(type_to_string(ret_type)), string_fmt(type_to_string(ctx->current_function->ret_type))));
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
        case STMT_DEFER: {
            check_stmt(ctx, stmt->defer.deferred);
            break;
        }
        case STMT_MATCH: {
            for (size_t i = 0; i < stmt->match.cases.len; i++) {
                check_stmt(ctx, stmt->match.cases.data[i].body);
            }
            break;
        }
    }
}

void check_fn_body(Analyser *ctx, FnDecl *fn) {
    if (!fn) return;

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
                error(d->fn->token, format("Extern function '%.*s' cannot have a body definition", string_fmt(d->fn->name)));
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

static Symbol *find_struct_method(Analyser *ctx, String struct_name, String method) {
    Module *mod = ctx->module;
    if (!mod) return NULL;

    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *decl = mod->decls.data[i];
        if (decl->type == DECL_FN) {
            FnDecl *fn = decl->fn;
            if (fn->struct_name.has_value && string_eq(fn->struct_name.value, struct_name)) {
                if (string_eq(fn->name, method)) return fn->symbol;
            }
        }
    }

    return NULL;
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
                case LITERAL_FLOAT: type_name = string_make("float"); break;
            }
            Symbol *type_sym = lookup_symbol(ctx, type_name);
            result_type = type_sym ? type_sym->type : NULL;
            goto check_expr_finished;
        }
        case EXPR_IDENT: {
            Symbol *sym = lookup_symbol(ctx, expr->ident.name);
            print_scope(ctx->current_scope, 0);
            if (!sym) {
                error(expr->token, format("Unknown variable '%.*s'", string_fmt(expr->ident.name)));
            }
            expr->symbol = sym;
            result_type = sym->type;
            goto check_expr_finished;
        }
        case EXPR_BINARY: {
            TypeRef *left_t = check_expr(ctx, expr->binary.left);
            TypeRef *right_t = check_expr(ctx, expr->binary.right);

            if (!types_compatible(right_t, left_t)) {
                error(expr->token, format("Cannot operate between incompatible types '%.*s' and '%.*s'", string_fmt(type_to_string(left_t)), string_fmt(type_to_string(right_t))));
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
            Expr *callee = expr->call.callee;

            if (callee->type == EXPR_SPECIALISE) {
                callee = callee->specialise.expr;
            }

            check_expr(ctx, callee);

            Symbol *callee_sym = callee->symbol;
            TypeRef *callee_type = callee->resolved_type;
            
            if (!callee_type) {
                error(callee->token, format("Unknown function '%.*s'", string_fmt(callee->token.value.value)));
            }

            if (callee_type->type != TYPEREF_FN) {
                if (callee_sym && callee_sym->decl) {
                    error(callee_sym->decl->token, "Cannot call non-function");
                } else {
                    error(callee->token, "Cannot call non-function");
                }
            }

            bool is_method_call = false;
            Expr *method_base = NULL;

            if (callee->type == EXPR_MEMBER) {
                method_base = callee->member.base;
                is_method_call = true;
            }

            size_t expected_params = callee_type->fn.params.len;
            size_t provided_args = expr->call.args.len;
            size_t virtual_args_len = is_method_call ? (provided_args + 1) : provided_args;

            if (virtual_args_len < expected_params) {
                size_t missing_count = expected_params - virtual_args_len;

                for (size_t i = virtual_args_len; i < expected_params; i++) {
                    if (!callee_type->fn.params.data[i].default_value) {
                        Token err_token = (callee_sym && callee_sym->decl) ? callee_sym->decl->token : callee->token;
                        error(err_token, format("Missing argument for parameter %ld which has no default value", i + 1));
                    }
                }

                for (size_t i = virtual_args_len; i < expected_params; i++) {
                    exprs_array_push(&expr->call.args, callee_type->fn.params.data[i].default_value);
                }

                provided_args = expr->call.args.len;
                virtual_args_len = is_method_call ? (provided_args + 1) : provided_args;
            }

            if (virtual_args_len > expected_params) {
                Token err_token = (callee_sym && callee_sym->decl) ? callee_sym->decl->token : callee->token;
                error(err_token, format("Function expects %ld arguments, got %ld", expected_params, provided_args));
            }

            for (size_t i = 0; i < expected_params; i++) {
                TypeRef *param_type = callee_type->fn.params.data[i].type;

                bool pushed_generic_scope = false;
                if (callee_sym && callee_sym->decl && callee_sym->decl->type == DECL_FN) {
                    FnDecl *fn = callee_sym->decl->fn;
                    if (fn->generic_params.len > 0) {
                        enter_scope(ctx, NULL);
                        pushed_generic_scope = true;
                        
                        for (size_t k = 0; k < fn->generic_params.len; k++) {
                            String param_name = fn->generic_params.data[k].name;
                            Symbol *sym = declare_symbol(ctx, param_name, SYMFLAG_TYPE);
                            
                            TypeRef *generic_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                            generic_type->type = TYPEREF_NAMED;
                            generic_type->type_symbol = sym;
                            sym->type = generic_type;
                        }
                    }
                }

                resolve_typeref(ctx, param_type);

                if (pushed_generic_scope) {
                    leave_scope(ctx);
                }

                TypeRef *arg_type = NULL;

                if (is_method_call && i == 0) {
                    arg_type = check_expr(ctx, method_base);
                    if (param_type->type == TYPEREF_POINTER && arg_type->type != TYPEREF_POINTER) {
                        if (types_compatible(arg_type, param_type->pointer.pointee)) {
                            Expr *implicit_addr = arena_calloc(ctx->arena, sizeof(Expr));
                            implicit_addr->type = EXPR_UNARY;
                            implicit_addr->unary.op = UOP_ADDR;
                            implicit_addr->unary.operand = method_base;
                            implicit_addr->token = method_base->token;

                            TypeRef *ptr_type = arena_calloc(ctx->arena, sizeof(TypeRef));
                            ptr_type->type = TYPEREF_POINTER;
                            ptr_type->pointer.pointee = arg_type;
                            ptr_type->is_mutable = param_type->is_mutable;
                            implicit_addr->resolved_type = ptr_type;

                            if (expr->call.callee->type == EXPR_MEMBER) {
                                expr->call.callee->member.base = implicit_addr;
                            } else if (expr->call.callee->type == EXPR_SPECIALISE && expr->call.callee->specialise.expr->type == EXPR_MEMBER) {
                                expr->call.callee->specialise.expr->member.base = implicit_addr;
                            }

                            arg_type = ptr_type;
                        }
                    }
                } else {
                    size_t arg_idx = is_method_call ? (i - 1) : i;
                    Expr *arg = expr->call.args.data[arg_idx];
                    
                    bool is_default_arg = (i >= virtual_args_len);
                    Scope *old_scope = ctx->current_scope;

                    if (is_default_arg && callee_sym && callee_sym->defined_in) {
                        ctx->current_scope = callee_sym->defined_in;
                    }

                    arg_type = check_expr(ctx, arg);

                    ctx->current_scope = old_scope;
                }

                if (!types_compatible(arg_type, param_type)) {
                    error(expr->token, format("Cannot pass argument of type '%.*s' to parameter expecting type '%.*s'", 
                        string_fmt(type_to_string(arg_type)), string_fmt(type_to_string(param_type))));
                }
            }

            expr->call.callee->resolved_type = callee_type;
            expr->call.callee->symbol = callee_sym;

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
                    // construct a pointer to the array's underlying element type
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
                    error(expr->member.base->token, format("Unknown array member '%.*s'", string_fmt(expr->member.member)));
                }
            } else {
                TypeRef *actual_struct_type = base_type;
                
                if (actual_struct_type->type == TYPEREF_POINTER && expr->member.deref) {
                    actual_struct_type = actual_struct_type->pointer.pointee;
                }

                Symbol *type_sym = actual_struct_type->type_symbol;
                if (!type_sym) {
                    error(expr->member.base->token, format("Unknown base type '%.*s'", string_fmt(type_to_string(base_type))));
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

                        error(expr->token, format("Generic parameter '%.*s' has no method named '%.*s'", string_fmt(type_sym->name), string_fmt(expr->member.member)));
                    }

                    // TODO: ensure constraints allow this, otherwise error
                    error(expr->token, format("Type placeholder '%.*s' does not have member elements", string_fmt(type_sym->name)));
                }

                if (!(type_sym->flags & SYMFLAG_TYPE)) {
                    error(expr->member.base->token, format("Unknown base type '%.*s'", string_fmt(type_to_string(base_type))));
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

                // prioritise members over methods (TODO: error on this?)
                if (found_member) {
                    result_type = found_member->type;
                    goto check_expr_finished;
                }

                Symbol *method_sym = find_struct_method(ctx, type_sym->name, expr->member.member);
                if (method_sym) {
                    expr->symbol = method_sym;
                    result_type = method_sym->type;
                    goto check_expr_finished;
                }

                error(expr->token, format("Unknown member or method '%.*s' on type '%.*s'", string_fmt(expr->member.member), string_fmt(type_sym->name)));
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
            break;
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
            resolve_typeref(ctx, expr->cast.to);
            
            if (expr->cast.expr->type == EXPR_INIT) {
                expr->cast.expr->resolved_type = expr->cast.to;
            }

            check_expr(ctx, expr->cast.expr);

            result_type = expr->cast.to;
            goto check_expr_finished;
        }
        case EXPR_INTRINSIC: {
            // Intrinsic *p = &expr->intrinsic;
            // if (p->is_arg_type) {
            //     resolve_typeref(ctx, p->type);
            //     if (!p->type->type_symbol) {
            //         error(expr->token, format("Unknown type '%.*s'", string_fmt(type_to_string(p->type))));
            //     }
            //     if (string_eq(p->name, string_make("sizeof"))) {
            //         size_t sz = get_type_size(p->type);
            //         if (sz == 0) error(expr->token, format("Cannot take sizeof of incomplete type '%.*s'", string_fmt(type_to_string(p->type))));
            //         result_type = lookup_symbol(ctx, string_make("int"))->type;
            //         expr->type = EXPR_LIT;
            //         expr->literal.type = LITERAL_INT;
            //         expr->literal._int = (int64_t)sz;
            //     }
            //     else {
            //         // TODO: typeid stuff
            //         error(expr->token, "TODO");
            //     }
            // } else {
            //     TypeRef *t = check_expr(ctx, p->expr);
            //     if (string_eq(p->name, string_make("sizeof"))) {
            //         size_t sz = get_type_size(t);
            //         if (sz == 0) error(expr->token, format("Cannot take sizeof of incomplete type '%.*s'", string_fmt(type_to_string(t))));
            //         result_type = lookup_symbol(ctx, string_make("int"))->type;
            //         expr->type = EXPR_LIT;
            //         expr->literal.type = LITERAL_INT;
            //         expr->literal._int = (int64_t)sz;
            //     } else {
            //         // TODO: typeid stuff
            //         error(expr->token, "TODO");
            //     }
            // }

            if (expr->intrinsic.is_arg_type) {
                resolve_typeref(ctx, expr->intrinsic.type);
            } else {
                check_expr(ctx, expr->intrinsic.expr);
            }
            // TODO: different intrinsics might return different types...
            result_type = lookup_symbol(ctx, string_make("uint"))->type;
            goto check_expr_finished;
            break;
        }
        case EXPR_PATH: {
            size_t components_len = expr->path.components.len;
            if (components_len < 2) {
                error(expr->token, "Invalid path expression");
            }

            String base_name = expr->path.components.data[0];
            Symbol *current_sym = lookup_symbol(ctx, base_name);
            if (!current_sym) {
                error(expr->token, format("Unknown identifier '%.*s'", string_fmt(base_name)));
            }

            for (size_t i = 1; i < components_len - 1; i++) {
                String next_name = expr->path.components.data[i];

                if (!current_sym->decl || current_sym->decl->type != DECL_NAMESPACE) {
                    error(expr->token, format("'%.*s' is not a namespace container", string_fmt(current_sym->name)));
                }

                Module *mod = current_sym->decl->namespace;
                Scope *mod_scope = mod->scope;
                Symbol *found_next = NULL;

                for (size_t j = 0; j < mod_scope->symbols.len; j++) {
                    Symbol *candidate = mod_scope->symbols.data[j];
                    if (string_eq(candidate->name, next_name)) {
                        if (!(candidate->flags & SYMFLAG_EXPORT)) {
                            error(expr->token, format("Namespace '%.*s' is private to module '%.*s'", 
                                string_fmt(candidate->name), string_fmt(module_name_to_string(ctx->arena, &mod->name))));
                        }
                        found_next = candidate;
                        break;
                    }
                }

                if (!found_next) {
                    error(expr->token, format("No nested namespace '%.*s' found in module '%.*s'", 
                        string_fmt(next_name), string_fmt(module_name_to_string(ctx->arena, &mod->name))));
                }

                current_sym = found_next;
            }

            String target_name = expr->path.components.data[components_len - 1];

            if (!current_sym->decl) {
                error(expr->token, "Invalid path resolution state");
            }

            switch (current_sym->decl->type) {
                case DECL_NAMESPACE: {
                    Module *mod = current_sym->decl->namespace;
                    Scope *mod_scope = mod->scope;
                    Symbol *target_sym = NULL;

                    for (size_t i = 0; i < mod_scope->symbols.len; i++) {
                        Symbol *candidate = mod_scope->symbols.data[i];
                        if (string_eq(candidate->name, target_name)) {
                            if (!(candidate->flags & SYMFLAG_EXPORT)) {
                                error(expr->token, format("Symbol '%.*s' is private to module '%.*s'", 
                                    string_fmt(candidate->name), string_fmt(module_name_to_string(ctx->arena, &mod->name))));
                            }
                            target_sym = candidate;
                            break;
                        }
                    }

                    if (!target_sym) {
                        error(expr->token, format("No symbol '%.*s' found in namespace '%.*s'", 
                            string_fmt(target_name), string_fmt(module_name_to_string(ctx->arena, &mod->name))));
                    }

                    result_type = target_sym->type;
                    expr->symbol = target_sym;
                    break;
                }

                case DECL_ENUM: {
                    EnumDecl *en = current_sym->decl->_enum;
                    bool found = false;

                    for (size_t i = 0; i < en->variants.len; i++) {
                        if (string_eq(en->variants.data[i].name, target_name)) {
                            result_type = current_sym->type;
                            expr->symbol = current_sym;
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        error(expr->token, format("Enum '%.*s' has no variant '%.*s'", 
                            string_fmt(current_sym->name), string_fmt(target_name)));
                    }
                    break;
                }

                case DECL_UNION: {
                    UnionDecl *unn = current_sym->decl->_union;
                    bool found = false;

                    for (size_t i = 0; i < unn->members.len; i++) {
                        if (string_eq(unn->members.data[i]->name, target_name)) {
                            result_type = current_sym->type;
                            expr->symbol = current_sym;
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        error(expr->token, format("Union '%.*s' has no constructor variant '%.*s'", 
                            string_fmt(current_sym->name), string_fmt(target_name)));
                    }
                    break;
                }

                default: {
                    error(expr->token, format("Path resolution is not supported out of container type: '%.*s'", 
                        string_fmt(current_sym->name)));
                }
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
                error(expr->token, format("Cannot use '?' on non-sum type '%.*s'", string_fmt(type_to_string(inner_t))));
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
            TypeRef *target = expr->resolved_type;

            for (size_t i = 0; i < expr->init_list.fields.len; i++) {
                check_expr(ctx, expr->init_list.fields.data[i].value);
            }

            result_type = target;
            goto check_expr_finished;
        }
        case EXPR_LAMBDA: {
            char mangled_str[64];
            int len = snprintf(mangled_str, sizeof(mangled_str), ".L__lambda_%d", ctx->lambda_count++);
            char *safe_str = arena_alloc(ctx->arena, len + 1);
            memcpy(safe_str, mangled_str, len + 1);
            String mangled = string_make(safe_str);

            resolve_typeref(ctx, expr->lambda.ret_type);

            for (size_t i = 0; i < expr->lambda.params.len; i++) {
                Param param = expr->lambda.params.data[i];
                resolve_typeref(ctx, param.type);
            }

            Scope *old_scope = ctx->current_scope;
            Scope *lambda_scope = scope_init(ctx->arena);
            lambda_scope->parent = old_scope; 
            ctx->current_scope = lambda_scope;

            for (size_t i = 0; i < expr->lambda.params.len; i++) {
                Param param = expr->lambda.params.data[i];
                Symbol *param_sym = declare_symbol(ctx, param.name, SYMFLAG_VAR);
                param_sym->type = param.type;
                param.symbol = param_sym;
            }

            check_stmt(ctx, expr->lambda.body);

            ctx->current_scope = old_scope;

            FnDecl *fndecl = arena_calloc(ctx->arena, sizeof(FnDecl));
            fndecl->name = mangled;
            fndecl->ret_type = expr->lambda.ret_type;
            fndecl->params = expr->lambda.params;
            fndecl->body = expr->lambda.body;
            fndecl->token = expr->token;

            Decl *decl = arena_calloc(ctx->arena, sizeof(Decl));
            decl->type = DECL_FN;
            decl->token = expr->token;
            decl->fn = fndecl;

            Symbol *sym = arena_calloc(ctx->arena, sizeof(Symbol));
            sym->name = mangled;
            sym->mangled = mangled;
            sym->decl = decl;
            sym->flags = SYMFLAG_FN;
            
            TypeRef *type = arena_calloc(ctx->arena, sizeof(TypeRef));
            type->type = TYPEREF_FN;
            type->fn.ret_type = expr->lambda.ret_type;
            type->fn.params = expr->lambda.params;
            sym->type = type;
            decl->symbol = sym;
            fndecl->symbol = sym;

            syms_array_push(&ctx->global_scope->symbols, sym);
            decls_array_push(&ctx->module->decls, decl);

            expr->lambda.symbol = sym;
            result_type = type;
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

    if (src->type == TYPEREF_NAMED && string_eq(src->type_symbol->name, string_make("$null")) && dst->type == TYPEREF_POINTER) {
        return dst->is_optional;
    }
    
    if (!dst->is_optional && src->is_optional) return false;
    
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

// silly monomorphisation thingies
static Symbol *monomorphise_struct(Module *mod, StructDecl *template, typerefs_array concrete_args);
static String mangle_generic_name(Arena *arena, String base_name, typerefs_array concrete_args);

static FnDecl *find_generic_fn_template(Module *mod, String name) {
    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *decl = mod->decls.data[i];
        if (decl->type == DECL_FN && string_eq(decl->fn->name, name)) {
            if (decl->fn->generic_params.len > 0) {
                return decl->fn;
            }
        }
    }
    return NULL;
}

static StructDecl *find_generic_struct_template(Module *mod, String name) {
    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *decl = mod->decls.data[i];
        if (decl->type == DECL_STRUCT && string_eq(decl->_struct->name, name)) {
            if (decl->_struct->generic_params.len > 0) {
                return decl->_struct;
            }
        }
    }
    return NULL;
}

static TypeRef *ast_substitute_type(Module *mod, TypeRef *src, genparam_array params, typerefs_array args) {
    if (!src) return NULL;

    if (src->type == TYPEREF_NAMED) {
        for (size_t i = 0; i < params.len; i++) {
            if (string_eq(src->named.name, params.data[i].name)) {
                return args.data[i];
            }
        }

        if (src->named.generic_args.len > 0) {
            typerefs_array substituted_args = typerefs_array_init(mod->arena);
            for (size_t i = 0; i < src->named.generic_args.len; i++) {
                typerefs_array_push(&substituted_args, 
                    ast_substitute_type(mod, src->named.generic_args.data[i], params, args));
            }

            StructDecl *template_str = find_generic_struct_template(mod, src->named.name);
            if (template_str) {
                Symbol *concrete_sym = monomorphise_struct(mod, template_str, substituted_args);

                TypeRef *rewritten = arena_calloc(mod->arena, sizeof(TypeRef));
                rewritten->type = TYPEREF_NAMED;
                rewritten->named.name = concrete_sym->name;
                rewritten->named.generic_args = typerefs_array_init(mod->arena);
                rewritten->type_symbol = concrete_sym;
                rewritten->token = src->token;
                return rewritten;
            }
        }

        return src; 
    }

    TypeRef *dst = arena_calloc(mod->arena, sizeof(TypeRef));
    *dst = *src;

    switch (dst->type) {
        case TYPEREF_POINTER:
            dst->pointer.pointee = ast_substitute_type(mod, src->pointer.pointee, params, args);
            break;
        case TYPEREF_ARRAY:
            dst->array.elem = ast_substitute_type(mod, src->array.elem, params, args);
            break;
        case TYPEREF_SUM:
            dst->sum.cases = typerefs_array_init(mod->arena);
            for (size_t i = 0; i < src->sum.cases.len; i++) {
                typerefs_array_push(&dst->sum.cases, 
                    ast_substitute_type(mod, src->sum.cases.data[i], params, args));
            }
            break;
        case TYPEREF_FN:
            dst->fn.ret_type = ast_substitute_type(mod, src->fn.ret_type, params, args);
            dst->fn.params = param_array_init(mod->arena);
            for (size_t i = 0; i < src->fn.params.len; i++) {
                Param p = src->fn.params.data[i];
                p.type = ast_substitute_type(mod, p.type, params, args);
                param_array_push(&dst->fn.params, p);
            }
            break;
        default: break;
    }

    return dst;
}

static Symbol *monomorphise_struct(Module *mod, StructDecl *template, typerefs_array concrete_args) {
    String mangled_name = mangle_generic_name(mod->arena, template->name, concrete_args);
    
    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *d = mod->decls.data[i];
        if (d->type == DECL_STRUCT && string_eq(d->_struct->name, mangled_name)) {
            return d->symbol; 
        }
    }

    StructDecl *concrete_struct = arena_calloc(mod->arena, sizeof(StructDecl));
    concrete_struct->name = mangled_name;
    concrete_struct->generic_params = (genparam_array){0};
    concrete_struct->members = vardecls_array_init(mod->arena);
    concrete_struct->field_offsets = size_array_init(mod->arena);

    for (size_t i = 0; i < template->members.len; i++) {
        VarDecl *src_member = template->members.data[i];
        VarDecl *dst_member = arena_calloc(mod->arena, sizeof(VarDecl));
        *dst_member = *src_member;
        
        dst_member->type = ast_substitute_type(mod, src_member->type, template->generic_params, concrete_args);
        vardecls_array_push(&concrete_struct->members, dst_member);
    }

    Decl *new_decl = arena_calloc(mod->arena, sizeof(Decl));
    new_decl->type = DECL_STRUCT;
    new_decl->_struct = concrete_struct;
    new_decl->token = template->token;
    
    Symbol *sym = arena_calloc(mod->arena, sizeof(Symbol));
    sym->name = mangled_name;
    sym->decl = new_decl;
    sym->flags = SYMFLAG_TYPE;
    
    TypeRef *struct_type = arena_calloc(mod->arena, sizeof(TypeRef));
    struct_type->type = TYPEREF_NAMED;
    struct_type->named.name = mangled_name;
    struct_type->type_symbol = sym;
    sym->type = struct_type;

    new_decl->symbol = sym; 

    if (mod->scope) {
        syms_array_push(&mod->scope->symbols, sym);
    }
    decls_array_push(&mod->decls, new_decl);

    return sym;
}

static Expr *clone_ast_expr(Module *mod, Expr *src, genparam_array params, typerefs_array args) {
    if (!src) return NULL;

    Expr *dst = arena_calloc(mod->arena, sizeof(Expr));
    *dst = *src;
    
    dst->resolved_type = ast_substitute_type(mod, src->resolved_type, params, args);

    switch (src->type) {
        case EXPR_INTRINSIC:
            if (src->intrinsic.is_arg_type) {
                dst->intrinsic.type = ast_substitute_type(mod, src->intrinsic.type, params, args);
            } else {
                dst->intrinsic.expr = clone_ast_expr(mod, src->intrinsic.expr, params, args);
            }
            break;
            
        case EXPR_SPECIALISE:
            dst->specialise.expr = clone_ast_expr(mod, src->specialise.expr, params, args);
            dst->specialise.args = typerefs_array_init(mod->arena);
            for (size_t i = 0; i < src->specialise.args.len; i++) {
                typerefs_array_push(&dst->specialise.args, 
                    ast_substitute_type(mod, src->specialise.args.data[i], params, args));
            }
            break;

        case EXPR_CALL:
            dst->call.callee = clone_ast_expr(mod, src->call.callee, params, args);
            dst->call.args = exprs_array_init(mod->arena);
            for (size_t i = 0; i < src->call.args.len; i++) {
                exprs_array_push(&dst->call.args, 
                    clone_ast_expr(mod, src->call.args.data[i], params, args));
            }
            break;

        case EXPR_UNARY:
            dst->unary.operand = clone_ast_expr(mod, src->unary.operand, params, args);
            break;

        case EXPR_BINARY:
            dst->binary.left = clone_ast_expr(mod, src->binary.left, params, args);
            dst->binary.right = clone_ast_expr(mod, src->binary.right, params, args);
            break;

        case EXPR_MEMBER:
            dst->member.base = clone_ast_expr(mod, src->member.base, params, args);
            break;

        case EXPR_INDEX:
            dst->index.base = clone_ast_expr(mod, src->index.base, params, args);
            dst->index.index = clone_ast_expr(mod, src->index.index, params, args);
            break;

        case EXPR_CAST:
            dst->cast.expr = clone_ast_expr(mod, src->cast.expr, params, args);
            dst->cast.to = ast_substitute_type(mod, src->cast.to, params, args);
            break;

        default: break;
    }

    return dst;
}

static Stmt *clone_ast_stmt(Module *mod, Stmt *src, genparam_array params, typerefs_array args) {
    if (!src) return NULL;

    Stmt *dst = arena_calloc(mod->arena, sizeof(Stmt));
    *dst = *src;

    switch (src->type) {
        case STMT_EXPR:
            dst->expr = clone_ast_expr(mod, src->expr, params, args);
            break;

        case STMT_DEFER:
            dst->defer.deferred = clone_ast_stmt(mod, src->defer.deferred, params, args);
            break;

        case STMT_RETURN:
            dst->_return.value = clone_ast_expr(mod, src->_return.value, params, args);
            break;

        case STMT_BLOCK:
            dst->block.stmts = stmts_array_init(mod->arena);
            for (size_t i = 0; i < src->block.stmts.len; i++) {
                stmts_array_push(&dst->block.stmts, 
                    clone_ast_stmt(mod, src->block.stmts.data[i], params, args));
            }
            break;

        case STMT_IF:
            dst->_if.cond = clone_ast_expr(mod, src->_if.cond, params, args);
            dst->_if.then = clone_ast_stmt(mod, src->_if.then, params, args);
            dst->_if._else = clone_ast_stmt(mod, src->_if._else, params, args);
            break;

        case STMT_WHILE:
            dst->_while.body = clone_ast_stmt(mod, src->_while.body, params, args);
            dst->_while.cond = clone_ast_expr(mod, src->_while.cond, params, args);
            break;

        case STMT_FOR:
            dst->_for.body = clone_ast_stmt(mod, src->_for.body, params, args);
            dst->_for.cond = clone_ast_expr(mod, src->_for.cond, params, args);
            dst->_for.init = clone_ast_stmt(mod, src->_for.init, params, args);
            dst->_for.post = clone_ast_expr(mod, src->_for.post, params, args);
            break;

        case STMT_VAR:
            dst->var = arena_calloc(mod->arena, sizeof(VarDecl));
            *dst->var = *src->var;
            dst->var->init = clone_ast_expr(mod, src->var->init, params, args);
            dst->var->type = ast_substitute_type(mod, src->var->type, params, args);
            break;

        case STMT_MATCH:
            dst->match.expr = clone_ast_expr(mod, src->match.expr, params, args);
            dst->match.cases = case_array_init(mod->arena);
            for (size_t i = 0; i < src->match.cases.len; i++) {
                Case sc = src->match.cases.data[i];
                Case dc = sc;
                
                dc.body = clone_ast_stmt(mod, sc.body, params, args);
                if (sc.var) {
                    dc.var = arena_calloc(mod->arena, sizeof(VarDecl));
                    *dc.var = *sc.var;
                    dc.var->init = clone_ast_expr(mod, sc.var->init, params, args);
                    dc.var->type = ast_substitute_type(mod, sc.var->type, params, args);
                }
                case_array_push(&dst->match.cases, dc);
            }
            break;

        default: break;
    }

    return dst;
}

static bool concrete_fn_exists(Module *mod, String mangled_name) {
    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *decl = mod->decls.data[i];
        if (decl->type == DECL_FN && string_eq(decl->fn->name, mangled_name)) {
            return true;
        }
    }
    return false;
}

static void scan_expr(Module *mod, Expr *expr);
static void scan_stmt(Module *mod, Stmt *stmt);

static String mangle_generic_name(Arena *arena, String base_name, typerefs_array concrete_args) {
    char_array cs = char_array_init(arena);
    char_array_push(&cs, '$');
    append_string_to_char_array(&cs, base_name);
    
    for (size_t i = 0; i < concrete_args.len; i++) {
        char_array_push(&cs, '_');
        String type_str = type_to_string(concrete_args.data[i]);
        append_string_to_char_array(&cs, type_str);
    }
    
    return (String){ .data = cs.data, .length = cs.len };
}

static void scan_expr(Module *mod, Expr *expr) {
    if (!expr) return;

    switch (expr->type) {
        case EXPR_SPECIALISE: {
            if (expr->specialise.expr->type != EXPR_IDENT) break; 

            String base_name = expr->specialise.expr->ident.name;
            typerefs_array concrete_args = expr->specialise.args;

            String mangled_name = mangle_generic_name(mod->arena, base_name, concrete_args);

            if (!concrete_fn_exists(mod, mangled_name)) {
                FnDecl *template = find_generic_fn_template(mod, base_name);
                if (template) {
                    if (template->generic_params.len != concrete_args.len) {
                        printf("Monomorphisation Error: Generic param count mismatch!\n");
                        break;
                    }

                    FnDecl *concrete_fn = arena_calloc(mod->arena, sizeof(FnDecl));
                    *concrete_fn = *template;
                    
                    concrete_fn->name = mangled_name;
                    concrete_fn->generic_params = (genparam_array){0};
                    
                    concrete_fn->ret_type = ast_substitute_type(mod, template->ret_type, template->generic_params, concrete_args);
                    concrete_fn->body = clone_ast_stmt(mod, template->body, template->generic_params, concrete_args);
                    
                    concrete_fn->params = param_array_init(mod->arena);
                    for (size_t i = 0; i < template->params.len; i++) {
                        Param p = template->params.data[i];
                        p.type = ast_substitute_type(mod, p.type, template->generic_params, concrete_args);
                        param_array_push(&concrete_fn->params, p);
                    }

                    Decl *new_decl = arena_calloc(mod->arena, sizeof(Decl));
                    new_decl->type = DECL_FN;
                    new_decl->fn = concrete_fn;
                    new_decl->token = template->token;
                    
                    Symbol *sym = arena_calloc(mod->arena, sizeof(Symbol));
                    sym->name = mangled_name;
                    sym->decl = new_decl;
                    sym->flags = SYMFLAG_FN;
                    sym->defined_in = mod->scope;
                    
                    TypeRef *fn_type = arena_calloc(mod->arena, sizeof(TypeRef));
                    fn_type->type = TYPEREF_FN;
                    fn_type->fn.ret_type = concrete_fn->ret_type;
                    fn_type->fn.params = concrete_fn->params;
                    sym->type = fn_type;

                    new_decl->symbol = sym;
                    concrete_fn->symbol = sym;
                    
                    if (mod->scope) {
                        syms_array_push(&mod->scope->symbols, sym);
                    }

                    decls_array_push(&mod->decls, new_decl);
                }
            }

            expr->type = EXPR_IDENT;
            expr->ident.name = mangled_name;
            break;
        }

        case EXPR_CALL:
            scan_expr(mod, expr->call.callee);
            for (size_t i = 0; i < expr->call.args.len; i++) {
                scan_expr(mod, expr->call.args.data[i]);
            }
            break;

        case EXPR_UNARY:
            scan_expr(mod, expr->unary.operand);
            break;

        case EXPR_BINARY:
            scan_expr(mod, expr->binary.left);
            scan_expr(mod, expr->binary.right);
            break;

        case EXPR_INTRINSIC:
            if (!expr->intrinsic.is_arg_type) {
                scan_expr(mod, expr->intrinsic.expr);
            }
            break;

        case EXPR_INDEX:
            scan_expr(mod, expr->index.base);
            scan_expr(mod, expr->index.index);
            break;

        case EXPR_MEMBER:
            scan_expr(mod, expr->member.base);
            break;

        case EXPR_CAST:
            scan_expr(mod, expr->cast.expr);
            break;

        default: break;
    }
}

static void scan_stmt(Module *mod, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_EXPR: {
            scan_expr(mod, stmt->expr);
            break;
        }
            
        case STMT_BLOCK: {
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                scan_stmt(mod, stmt->block.stmts.data[i]);
            }
            break;
        }
            
        case STMT_RETURN: {
            scan_expr(mod, stmt->_return.value);
            break;
        }
            
        case STMT_DEFER: {
            scan_stmt(mod, stmt->defer.deferred);
            break;
        }
            
        case STMT_IF: {
            scan_expr(mod, stmt->_if.cond);
            scan_stmt(mod, stmt->_if.then);
            scan_stmt(mod, stmt->_if._else);
            break;
        }

        case STMT_VAR: {
            scan_expr(mod, stmt->var->init);

            if (stmt->var->type && stmt->var->type->type == TYPEREF_NAMED) {
                if (stmt->var->type->named.generic_args.len > 0) {
                    
                    stmt->var->type = ast_substitute_type(mod, stmt->var->type, (genparam_array){0}, (typerefs_array){0});
                }
            }
            break;
        }

        // ... add STMT_FOR, STMT_WHILE, STMT_MATCH matching structures
        default: break;
    }
}

void ast_pass_monomorphise(Module *mod) {
    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *decl = mod->decls.data[i];
        
        // only look inside function bodies that are NOT themselves generic templates
        if (decl->type == DECL_FN && decl->fn->generic_params.len == 0) {
            scan_stmt(mod, decl->fn->body);
        }
    }
}

// include res!

static bool peek_module_decl(Arena *arena, char *filepath, string_array *out) {
    Lexer l = lexer_init_from_file(filepath);
    if (lex_next_token(&l).type != TOKENTYPE_MODULE) {
        lexer_free(&l);
        return false;
    }
    *out = string_array_init(arena);

    while (true) {
        Token t = lex_next_token(&l);
        if (t.type != TOKENTYPE_IDENT) {
            string_array_free(out);
            lexer_free(&l);
            return false;
        }
        string_array_push(out, string_copy(arena, t.value.value));
        Token s = lex_next_token(&l);
        if (s.type == TOKENTYPE_DOUBLECOLON) continue;
        
        if (s.type == TOKENTYPE_SEMICOLON) break;

        string_array_free(out);
        lexer_free(&l);
        return false;
    }

    lexer_free(&l);
    return true;
}

void scan_dir(Analyser *ctx, char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(ctx, full_path);
            continue;
        }

        const char *ext = strrchr(entry->d_name, '.');
        if (ext && strcmp(ext, ".coda") == 0) {
            string_array modname = string_array_init(ctx->arena);
            if (!peek_module_decl(ctx->arena, full_path, &modname)) continue;

            modentry_array_push(&ctx->module_map, (ModuleEntry){
                .path = string_copy(ctx->arena, string_make(full_path)),
                .name = modname,
                .mtime = (uint64_t)st.st_mtime,
                .hash = 0,
                .is_parsed = false,
                .ast = NULL,
            });
        }
    }

    closedir(dir);
}

static bool string_array_eq(string_array *a, string_array *b) {
    if (a->len != b->len) return false;
    for (size_t i = 0; i < a->len; i++) {
        if (!string_eq(a->data[i], b->data[i])) return false;
    }
    return true;
}

void resolve_includes(Analyser *ctx, Module *mod) {
    for (size_t i = 0; i < mod->includes.len; i++) {
        Include *inc = mod->includes.data[i];
        ModuleEntry *entry = NULL;

        for (size_t j = 0; j < ctx->module_map.len; j++) {
            if (string_array_eq(&ctx->module_map.data[j].name, &inc->path)) {
                entry = &ctx->module_map.data[j];
                break;
            }
        }

        if (!entry) {
            error(inc->token, "Module not found in include paths");
        }

        if (!entry->is_parsed) {
            entry->ast = parse_file(string_unmake(ctx->arena, entry->path), NULL);

            entry->ast->scope = scope_init(ctx->arena);
            entry->ast->scope->parent = mod->scope;

            entry->is_parsed = true;
            resolve_includes(ctx, entry->ast);
        }

        entry->ast->scope->parent = ctx->current_scope;
        inc->resolved = entry->ast;
    }
}

void populate_module_namespaces(Analyser *ctx, Module *mod) {
    if (!mod || !mod->scope) return;

    for (size_t i = 0; i < mod->includes.len; i++) {
        Include *inc = mod->includes.data[i];
        if (!inc->resolved) continue;

        string_array *target_path = &inc->path;
        if (inc->alias.len > 0) {
            target_path = &inc->alias;
        }

        if (target_path->len == 0) continue;

        Scope *current_scope = mod->scope;

        for (size_t j = 0; j < target_path->len - 1; j++) {
            String part = target_path->data[j];
            Symbol *existing_sym = NULL;

            for (size_t k = 0; k < current_scope->symbols.len; k++) {
                if (string_eq(current_scope->symbols.data[k]->name, part)) {
                    existing_sym = current_scope->symbols.data[k];
                    break;
                }
            }

            if (existing_sym) {
                if (existing_sym->decl && existing_sym->decl->type == DECL_NAMESPACE) {
                    current_scope = existing_sym->decl->namespace->scope;
                } else {
                    fprintf(stderr, "\e[1;37merror:\e[0m Namespace ''%.*s'' is already defined.\n", string_fmt(part));
                    exit(1);
                }
            } else {
                Module *synth_mod = arena_calloc(ctx->arena, sizeof(Module));
                
                synth_mod->name = string_array_init(mod->arena);
                string_array_push(&synth_mod->name, part);
                
                synth_mod->scope = scope_init(ctx->arena);
                synth_mod->scope->parent = current_scope;

                Decl *synth_decl = arena_calloc(ctx->arena, sizeof(Decl));
                synth_decl->type = DECL_NAMESPACE;
                synth_decl->namespace = synth_mod;

                Symbol *synth_sym = arena_calloc(ctx->arena, sizeof(Symbol));
                synth_sym->name = part;
                synth_sym->decl = synth_decl;
                synth_sym->flags = SYMFLAG_NAMESPACE;
                synth_sym->defined_in = current_scope;
                synth_decl->symbol = synth_sym;

                syms_array_push(&current_scope->symbols, synth_sym);

                current_scope = synth_mod->scope;
            }
        }

        String leaf_name = target_path->data[target_path->len - 1];

        Decl *leaf_decl = arena_calloc(ctx->arena, sizeof(Decl));
        leaf_decl->type = DECL_NAMESPACE;
        leaf_decl->namespace = inc->resolved;
        leaf_decl->token = inc->token;

        Symbol *leaf_sym = arena_calloc(ctx->arena, sizeof(Symbol));
        leaf_sym->name = leaf_name;
        leaf_sym->decl = leaf_decl;
        leaf_sym->flags = SYMFLAG_NAMESPACE | SYMFLAG_EXPORT;
        leaf_sym->defined_in = current_scope;
        leaf_decl->symbol = leaf_sym;

        syms_array_push(&current_scope->symbols, leaf_sym);
    }
}

static String generate_mangled_name(Analyser *ctx, Module *mod, String sym_name, Token err_tok) {
    if (sym_name.length >= 2 && sym_name.data[0] == '_' && sym_name.data[1] == 'C') {
        error(err_tok, "Identifiers starting with '_C' are reserved for the compiler");
    }

    if (string_eq(sym_name, string_make("main"))) return sym_name;

    char buffer[512];
    int written = 0;

    written += snprintf(buffer + written, sizeof(buffer) - written, "_C");

    if (mod && mod->name.len > 0) {
        for (size_t i = 0; i < mod->name.len; i++) {
            String part = mod->name.data[i];
            written += snprintf(buffer + written, sizeof(buffer) - written, "%zu%.*s", 
                                part.length, string_fmt(part));
        }
    }

    written += snprintf(buffer + written, sizeof(buffer) - written, "%zu%.*s", sym_name.length, string_fmt(sym_name));

    String mangled;
    mangled.length = written;
    mangled.data = arena_calloc(ctx->arena, mangled.length);
    memcpy(mangled.data, buffer, mangled.length);

    return mangled;
}
