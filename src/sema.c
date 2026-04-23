#include "sema.h"
#include "error.h"

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
            error(sym->token, format("Redeclaration of symbol %.*s", sym->name.length, sym->name.data));
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

void register_globals(Analyser *ctx, Module *mod) {
    ctx->current_scope = ctx->global_scope;

    for (size_t i = 0; i < mod->decls.len; i++) {
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
                resolve_typeref(ctx, d->fn->ret_type);

                for (size_t j = 0; j < d->fn->params.len; j++) {
                    Param p = d->fn->params.data[j];
                    resolve_typeref(ctx, p.type);
                }
                break;
            }
            case DECL_STRUCT: {
                for (size_t j = 0; j < d->_struct->members.len; j++) {
                    VarDecl *m = d->_struct->members.data[j];
                    resolve_typeref(ctx, m->type);
                }

                calculate_struct_layout(d->_struct);
                break;
            }
            case DECL_UNION: {
                for (size_t j = 0; j < d->_union->members.len; j++) {
                    VarDecl *m = d->_union->members.data[j];
                    resolve_typeref(ctx, m->type);
                }

                calculate_union_layout(d->_union);
            }
            case DECL_VAR: {
                error(d->token, "Global variables are not allowed");
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
                TypeRef *init_type = check_expr(ctx, var->init);

                if (!types_equal(var->type, init_type)) {
                    error(var->token, "Cannot assign differing types");
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
                    error(stmt->token, "Return types do not match");
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
    if (!fn->body) return;

    ctx->current_function = fn;

    enter_scope(ctx, NULL);
    fn->local_scope = ctx->current_scope;

    for (size_t i = 0; i < fn->params.len; i++) {
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

    for (size_t i = 0; i < mod->decls.len; i++) {
        Decl *d = mod->decls.data[i];
        if (d->type == DECL_FN) {
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
                error(expr->token, "Unknown variable");
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
                error(expr->token, "Can only operate on equal types");
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
            if (!callee_sym) {
                error(expr->token, "Unknown function");
            }

            if (!(callee_sym->flags & SYMFLAG_FN)) {
                error(callee_sym->token, "Cannot call non-function");
            }

            FnDecl *fn = callee_sym->decl->fn;
            if (!fn) {
                error(callee_sym->token, "Unknown function");
            }

            if (expr->call.args.len != fn->params.len) {
                error(callee_sym->token, "Function expects X arguments"); // TODO: this needs formatting :sob:
            }

            for (size_t i = 0; i < expr->call.args.len; i++) {
                TypeRef *arg_type = check_expr(ctx, expr->call.args.data[i]);
                TypeRef *param_type = fn->params.data[i].type;

                if (!types_equal(arg_type, param_type)) {
                    error(arg_type->token, "Parameter expected different type");
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
                error(expr->token, "Unknown type");
            }

            StructDecl *str = NULL;
            UnionDecl *unn = NULL;

            if (type_sym->decl->type == DECL_STRUCT) str = type_sym->decl->_struct;
            else if (type_sym->decl->type == DECL_UNION) unn = type_sym->decl->_union;

            if (!str && !unn) {
                error(expr->token, "Cannot get member of type without members");
            }

            vardecls_array members = str ? str->members : unn->members;
            for (size_t i = 0; i < members.len; i++) {
                if (string_eq(members.data[i]->name, expr->member.member)) {
                    result_type = members.data[i]->type;
                    goto check_expr_finished;
                }
            }

            error(expr->token, "Unknown member");
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