#include "sema.h"

Symbol *declare_symbol(Analyser *ctx, String name, uint32_t flags);
void register_globals(Analyser *ctx, Module *mod);
void resolve_types(Analyser *ctx, Module *mod);
void check_bodies(Analyser *ctx, Module *mod);
TypeRef *check_expr(Analyser *ctx, Expr *expr);
bool types_equal(TypeRef *a, TypeRef *b);

static void inject_builtin_types(Analyser *ctx) {
    char* builtins[] = {
        "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "char", "string",
        "bool",
        "none"
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
}

Scope *scope_init(Arena *a) {
    Scope *s = arena_alloc(a, sizeof(Scope));
    s->symbols = syms_array_init();
    s->parent = NULL;
    return s;
}

Analyser analyser_init(Module *m, Arena *a) {
    Analyser an = {
        .arena = a,
        .current_function = NULL,
        .global_scope = scope_init(a),
        .current_scope = an.global_scope,
        .module = m,
    };

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
        Scope *new_scope = arena_calloc(ctx->arena, sizeof(Scope));
        new_scope->parent = ctx->current_scope;
        ctx->current_scope = new_scope;
    }
}

void leave_scope(Analyser *ctx) {
    if (ctx->current_scope == ctx->current_scope->parent) {
        ctx->current_scope = ctx->current_scope->parent;
    }
}

Symbol *declare_symbol(Analyser *ctx, String name, uint32_t flags) {
    for (size_t i = 0; i < ctx->current_scope->symbols.idx; i++) {
        Symbol *sym = ctx->current_scope->symbols.data[i];
        if (string_cmp(sym->name, name) == 0) {
            exit(1);    // error
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
        for (size_t i = 0; i < scope->symbols.idx; i++) {
            Symbol *sym = scope->symbols.data[i];
            if (string_cmp(sym->name, name) == 0) {
                return sym;
            }
        }
        scope = scope->parent;
    }

    return NULL;
}

void register_globals(Analyser *ctx, Module *mod) {
    ctx->current_scope = ctx->global_scope;

    for (size_t i = 0; i < mod->decls.idx; i++) {
        Decl *d = mod->decls.data[i];

        switch (d->type) {
            case DECL_FN: {
                uint32_t flags = SYMFLAG_FN;
                if (d->fn->is_export) flags |= SYMFLAG_EXPORT;

                Symbol *sym = declare_symbol(ctx, d->fn->name, flags);
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
            case DECL_VAR: {
                // error
                exit(1);
            }
        }
    }
}

void resolve_typeref(Analyser *ctx, TypeRef *type) {
    if (!type) return;

    switch (type->type) {
        case TYPEREF_NAMED: {
            Symbol *sym = lookup_symbol(ctx, type->named.name);
            if (!sym || !(sym->flags * SYMFLAG_TYPE)) {
                // error
                exit(1);
            }
            type->type_symbol = sym;
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
    }
}

static size_t get_type_size(TypeRef *type) {
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

            if (string_cmp(sym->name, string_make("int8")) == 0 || string_cmp(sym->name, string_make("uint8")) == 0
             || string_cmp(sym->name, string_make("bool")) == 0 || string_cmp(sym->name, string_make("char")) == 0) {
                return 1;
            }
            if (string_cmp(sym->name, string_make("int16")) == 0 || string_cmp(sym->name, string_make("uint16")) == 0) {
                return 2;
            }
            if (string_cmp(sym->name, string_make("int32")) == 0 || string_cmp(sym->name, string_make("uint32")) == 0) {
                return 4;
            }
            if (string_cmp(sym->name, string_make("int64")) == 0 || string_cmp(sym->name, string_make("uint64")) == 0
             || string_cmp(sym->name, string_make("int")) == 0 || string_cmp(sym->name, string_make("uint")) == 0) {
                return 8;
            }
            if (string_cmp(sym->name, string_make("string")) == 0) {
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

            if (string_cmp(sym->name, string_make("int8")) == 0 || string_cmp(sym->name, string_make("uint8")) == 0
             || string_cmp(sym->name, string_make("bool")) == 0 || string_cmp(sym->name, string_make("char")) == 0) {
                return 1;
            }
            if (string_cmp(sym->name, string_make("int16")) == 0 || string_cmp(sym->name, string_make("uint16")) == 0) {
                return 2;
            }
            if (string_cmp(sym->name, string_make("int32")) == 0 || string_cmp(sym->name, string_make("uint32")) == 0) {
                return 4;
            }
            if (string_cmp(sym->name, string_make("int64")) == 0 || string_cmp(sym->name, string_make("uint64")) == 0
             || string_cmp(sym->name, string_make("int")) == 0 || string_cmp(sym->name, string_make("uint")) == 0) {
                return 8;
            }
            if (string_cmp(sym->name, string_make("string")) == 0) {
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

    for (size_t i = 0; i < str->members.idx; i++) {
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

    for (size_t i = 0; i < unn->members.idx; i++) {
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

    for (size_t i = 0; i < mod->decls.idx; i++) {
        Decl *d = mod->decls.data[i];

        switch (d->type) {
            case DECL_FN: {
                resolve_typeref(ctx, d->fn->ret_type);

                for (size_t j = 0; j < d->fn->params.idx; j++) {
                    Param p = d->fn->params.data[j];
                    resolve_typeref(ctx, p.type);
                }
                break;
            }
            case DECL_STRUCT: {
                for (size_t j = 0; j < d->_struct->members.idx; j++) {
                    VarDecl *m = d->_struct->members.data[j];
                    resolve_typeref(ctx, m->type);
                }

                calculate_struct_layout(d->_struct);
                break;
            }
            case DECL_UNION: {
                for (size_t j = 0; j < d->_union->members.idx; j++) {
                    VarDecl *m = d->_union->members.data[j];
                    resolve_typeref(ctx, m->type);
                }

                calculate_union_layout(d->_union);
            }
            case DECL_VAR: {
                // error
                exit(1);
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
            for (size_t i = 0; i < stmt->block.stmts.idx; i++) {
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
                TypeRef *init_type = check_expr(ctx, var->init);

                if (!types_equal(var->type, init_type)) {
                    // error
                    exit(1);
                }
            }

            uint32_t flags = SYMFLAG_VAR;
            if (var->is_mutable) flags |= SYMFLAG_MUT;

            Symbol *sym = declare_symbol(ctx, var->name, flags);
            sym->type = var->type;
            var->symbol = sym;
            break;
        }
        case STMT_RETURN: {
            if (stmt->_return.value) {
                TypeRef *ret_type = check_expr(ctx, stmt->_return.value);
                if (!types_equal(ret_type, ctx->current_function->ret_type)) {
                    // errpr
                    exit(1);
                }
            } else {
                if (ctx->current_function->ret_type
                 && string_cmp(ctx->current_function->ret_type->type_symbol->name, string_make("none")) != 0) {
                    // error
                    exit(1);
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
                if (!cond_type || string_cmp(cond_type->type_symbol->name, string_make("bool")) != 0) {
                    // error
                    exit(1);
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
                if (!cond_type || string_cmp(cond_type->type_symbol->name, string_make("bool")) != 0) {
                    // error
                    exit(1);
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
                if (!cond_type || string_cmp(cond_type->type_symbol->name, string_make("bool")) != 0) {
                    // error
                    exit(1);
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
    if (!fn->body) return;

    ctx->current_function = fn;

    enter_scope(ctx, NULL);
    fn->local_scope = ctx->current_scope;

    for (size_t i = 0; i < fn->params.idx; i++) {
        Param p = fn->params.data[i];
        Symbol *sym = declare_symbol(ctx, p.name, SYMFLAG_VAR);
        sym->type = p.type;
        p.symbol = sym;
    }

    check_stmt(ctx, fn->body);

    leave_scope(ctx);
    ctx->current_function = NULL;
}

void check_bodies(Analyser *ctx, Module *mod) {
    ctx->current_scope = ctx->global_scope;

    for (size_t i = 0; i < mod->decls.idx; i++) {
        Decl *d = mod->decls.data[i];
        if (d->type == DECL_FN) {
            check_fn_body(ctx, d->fn);
        }
    }
}

TypeRef *check_expr(Analyser *ctx, Expr *expr) {
    if (!expr) return NULL;

    TypeRef *result_type = NULL;
    switch (expr->type) {
        case EXPR_LIT: {
            String type_name;
            switch (expr->literal.type) {
                case LITERAL_INT: type_name = string_make("int"); break;
                case LITERAL_BOOL: type_name = string_make("bool"); break;
                case LITERAL_STRING: type_name = string_make("string"); break;
                case LITERAL_CHAR: type_name = string_make("char"); break;
            }
            Symbol *type_sym = lookup_symbol(ctx, type_name);
            result_type = type_sym ? type_sym->type : NULL;
            goto check_expr_finished;
        }
        case EXPR_IDENT: {
            Symbol *sym = lookup_symbol(ctx, expr->ident.name);
            if (!sym) {
                // err
                exit(1);
            }
            expr->symbol = sym;
            result_type = sym->type;
            goto check_expr_finished;
        }
        case EXPR_BINARY: {
            TypeRef *left_t = check_expr(ctx, expr->binary.left);
            TypeRef *right_t = check_expr(ctx, expr->binary.right);

            // TODO: allow implicit widening
            if (left_t && right_t && left_t->type_symbol != right_t->type_symbol) {
                // err
                exit(1);
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
                if (!types_equal(left_t, right_t)) {
                    // err
                    exit(1);
                }

                if (!left_t->is_mutable) {
                    // err
                    exit(1);
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
            if (!callee_sym) {
                // err
                exit(1);
            }

            if (!(callee_sym->flags & SYMFLAG_FN)) {
                // err
                exit(1);
            }

            FnDecl *fn = callee_sym->decl->fn;
            if (!fn) {
                // err
                exit(1);
            
            }

            if (expr->call.args.idx != fn->params.idx) {
                // err
                exit(1);
            }

            for (size_t i = 0; i < expr->call.args.idx; i++) {
                TypeRef *arg_type = check_expr(ctx, expr->call.args.data[i]);
                TypeRef *param_type = fn->params.data[i].type;

                if (!types_equal(arg_type, param_type)) {
                    // err
                    exit(1);
                }
            }

            result_type = fn->ret_type;
            break;
        }
        case EXPR_MEMBER: {
            TypeRef *base_type = check_expr(ctx, expr->member.base);
            if (!base_type) {
                result_type = NULL;
                goto check_expr_finished;
            }

            Symbol *type_sym = base_type->type_symbol;
            if (!type_sym || !(type_sym->flags & SYMFLAG_TYPE)) {
                // error
                exit(1);
            }

            StructDecl *str = NULL;
            UnionDecl *unn = NULL;

            if (type_sym->decl->type == DECL_STRUCT) str = type_sym->decl->_struct;
            else if (type_sym->decl->type == DECL_UNION) unn = type_sym->decl->_union;

            if (!str && !unn) {
                // error
                exit(1);
            }

            vardecls_array members = str ? str->members : unn->members;
            for (size_t i = 0; i < members.idx; i++) {
                if (string_cmp(members.data[i]->name, expr->member.member)) {
                    result_type = members.data[i]->type;
                    goto check_expr_finished;
                }
            }

            // error
            exit(1);
        }
        case EXPR_UNARY: {
            TypeRef *operand_type = check_expr(ctx, expr->unary.operand);
            if (!operand_type) result_type = NULL;

            switch (expr->unary.op) {
                case UOP_NEG: {
                    String name = operand_type->type_symbol->name;
                    if (!string_find(name, string_make("int"))) {
                        //er r
                        exit(1);
                    }
                    result_type = operand_type;
                    goto check_expr_finished;
                }
                case UOP_NOT: {
                    if (string_cmp(operand_type->type_symbol->name, string_make("bool")) != 0) {
                        // error
                        exit(1);
                    }
                    result_type = operand_type;
                    goto check_expr_finished;
                }
                case UOP_ADDR: {
                    TypeRef *p = arena_alloc(ctx->arena, sizeof(TypeRef));
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

                    // error
                    exit(1);
                }
            }
        }
        case EXPR_INDEX: {
            TypeRef *base_type = check_expr(ctx, expr->index.base);
            TypeRef *index_type = check_expr(ctx, expr->index.index);

            if (!index_type || !string_find(index_type->type_symbol->name, string_make("int"))) {
                // error
                exit(1);
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
                    //err
                    exit(1);
                }
            }
        }
        case EXPR_CAST: {
            check_expr(ctx, expr->cast.expr);

            resolve_typeref(ctx, expr->cast.to);

            result_type = expr->cast.to;
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