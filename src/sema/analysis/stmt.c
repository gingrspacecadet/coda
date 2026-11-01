#include "common.h"

static bool stmt_is_assignment_expr(AstExpr *expr) {
    if (expr == NULL || expr->kind != AST_EXPR_BINARY)
        return false;

    switch (expr->binary.op) {
        case AST_BINARY_ASSIGN:
        case AST_BINARY_ADD_ASSIGN:
        case AST_BINARY_SUB_ASSIGN:
        case AST_BINARY_MUL_ASSIGN:
        case AST_BINARY_DIV_ASSIGN:
        case AST_BINARY_MOD_ASSIGN:
        case AST_BINARY_BIT_AND_ASSIGN:
        case AST_BINARY_BIT_OR_ASSIGN:
        case AST_BINARY_BIT_XOR_ASSIGN:
        case AST_BINARY_SHL_ASSIGN:
        case AST_BINARY_SHR_ASSIGN:
        case AST_BINARY_NAND_ASSIGN:
        case AST_BINARY_NOR_ASSIGN:
            return true;

        default:
            return false;
    }
}

static bool local_decl(Sema *sema, AstVarDecl *ast, HirStmt **init_stmt) {
    Scope *scope = (Scope *)array_at(&sema->scopes, sema->scopes.len - 1);

    Symbol *s = scope_lookup(scope, ast->name);
    if (s != NULL) {
        error_duplicate_symbol(sema->diags, ast->name.ident, ast->span, s->span);
        return false;
    }

    Symbol *existing = sema_lookup_non_method(sema, ast->name);

    if (existing != NULL) {
        error_shadowing(sema->diags, ast->name.ident, ast->span, existing->span);
        return false;
    }

    AstType *previous_vla_type = sema->vla_type_allowed;
    sema->vla_type_allowed = ast->type != NULL && ast->type->kind == AST_TYPE_ARRAY && ast->type->array.sized ? ast->type : NULL;
    HirType *type = sema_type(sema, ast->type);
    sema->vla_type_allowed = previous_vla_type;

    if (type == NULL || type->kind == HIR_TYPE_ERROR)
        return false;

    HirExpr *init = NULL;

    if (type->is_vla && ast->init != NULL) {
        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_TYPE_MISMATCH, ast->span, STRING("variable-length arrays cannot have an initializer"));
        diag_finish(&diagnostic);
        return false;
    }

    if (ast->init != NULL) {
        init = sema_expr(sema, ast->init, type);

        if (init == NULL || init->kind == HIR_EXPR_ERROR)
            return false;
    }

    if (type->is_vla) {
        init = arena_calloc(sema->arena, sizeof(*init));
        *init = (HirExpr){.span = ast->type->span, .kind = HIR_EXPR_VLA_ALLOC, .type = type, .vla_alloc = {.element = type->slice.element, .length = type->vla_length}};
    }

    Symbol *symbol = arena_calloc(sema->arena, sizeof(Symbol));
    *symbol = (Symbol) {
        .kind = SYMBOL_LOCAL,
        .decl = NULL,
        .type = type,
        .span = ast->span,
        .namespace_scope = NULL,
        .name = ast->name,
    };

    scope_insert(scope, symbol);

    HirLocal local = {
        .symbol = symbol,
        .type = type,
    };

    array_push(&sema->current_fn->locals, &local);

    *init_stmt = NULL;

    if (init != NULL) {
        HirExpr *target = arena_calloc(sema->arena, sizeof(HirExpr));
        *target = (HirExpr) {
            .span = ast->span,
            .kind = HIR_EXPR_VALUE,
            .type = type,
            .value = {
                .symbol = symbol,
            },
        };

        HirStmt *stmt = arena_calloc(sema->arena, sizeof(HirStmt));
        *stmt = (HirStmt) {
            .span = ast->span,
            .kind = HIR_STMT_ASSIGN,
            .assign = {
                .op = AST_BINARY_ASSIGN,
                .target = target,
                .value = init,
            },
        };

        *init_stmt = stmt;
    }

    return true;
}

static bool block(Sema *sema, AstStmt *ast, HirStmt *hir) {
    hir->kind = HIR_STMT_BLOCK;
    hir->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

    Scope *scope = sema_push_scope(sema);

    for (size_t i = 0; i < ast->block.stmts.len; i++) {
        AstStmt *stmt = ((AstStmt **)ast->block.stmts.data)[i];

        if (stmt->kind == AST_STMT_VAR) {
            HirStmt *init = NULL;

            if (!local_decl(sema, stmt->var, &init)) {
                sema_pop_scope(sema);
                return false;
            }

            if (init != NULL)
                array_push(&hir->block.stmts, &init);

            continue;
        }

        if (stmt->kind == AST_STMT_DEFER) {
            HirStmt *deferred = sema_stmt(sema, stmt->defer.deferred);

            if (deferred == NULL || deferred->kind == HIR_STMT_ERROR) {
                sema_pop_scope(sema);
                return false;
            }

            array_push(&scope->defers, &deferred);
            continue;
        }

        HirStmt *value = sema_stmt(sema, stmt);

        if (value == NULL)
            continue;

        if (value->kind == HIR_STMT_ERROR) {
            sema_pop_scope(sema);
            return false;
        }

        array_push(&hir->block.stmts, &value);

        if (hir_stmt_terminates(value))
            break;
    }

    if (hir->block.stmts.len == 0 || !hir_stmt_terminates(((HirStmt **)hir->block.stmts.data)[hir->block.stmts.len - 1])) {
        for (size_t i = scope->defers.len; i > 0; i--) {
            HirStmt *deferred = ((HirStmt **)scope->defers.data)[i - 1];

            array_push(&hir->block.stmts, &deferred);
        }
    }

    sema_pop_scope(sema);
    return true;
}

static HirStmt *hir_stmt(Sema *sema, HirStmtKind kind, Span span) {
    HirStmt *stmt = arena_calloc(sema->arena, sizeof(HirStmt));

    stmt->span = span;
    stmt->kind = kind;

    return stmt;
}

static HirExpr *hir_bool(Sema *sema, bool value) {
    HirExpr *expr = arena_calloc(sema->arena, sizeof(HirExpr));

    expr->span = (Span) { 0 };
    expr->kind = HIR_EXPR_LITERAL;
    expr->type = NULL;
    expr->literal = (HirLiteral) {
        .kind = HIR_LITERAL_BOOL,
        .boolean = value,
    };

    return expr;
}

static HirStmt *lower_defer_exit(Sema *sema, HirStmt *exit, size_t scope) {
    size_t count = 0;

    for (size_t i = sema->scopes.len; i > scope; i--) {
        Scope *current = (Scope *)array_at(&sema->scopes, i - 1);
        count += current->defers.len;
    }

    if (count == 0)
        return exit;

    HirStmt *block = hir_stmt(sema, HIR_STMT_BLOCK, exit->span);
    block->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

    for (size_t i = sema->scopes.len; i > scope; i--) {
        Scope *current = (Scope *)array_at(&sema->scopes, i - 1);

        for (size_t j = current->defers.len; j > 0; j--) {
            HirStmt *deferred =
                ((HirStmt **)current->defers.data)[j - 1];

            array_push(&block->block.stmts, &deferred);
        }
    }

    array_push(&block->block.stmts, &exit);
    return block;
}

typedef struct {
    HirExpr *condition;
    HirStmt *body;
    size_t member_index;
} SemaMatchCase;

static HirType *match_unwrap_type(HirType *type) {
    while (type != NULL && type->base != NULL)
        type = type->base;
    return type;
}

static size_t match_member_index(HirType *sum, HirType *type) {
    for (size_t i = 0; i < sum->sum.members.len; i++) {
        HirType *member = ((HirType **)sum->sum.members.data)[i];
        if (type_equal(member, type))
            return i;
    }
    return SIZE_MAX;
}

static size_t match_payload_offset(HirType *sum) {
    size_t align = 1;
    for (size_t i = 0; i < sum->sum.members.len; i++) {
        HirType *member = match_unwrap_type(((HirType **)sum->sum.members.data)[i]);
        if (member->align > align)
            align = member->align;
    }

    size_t tag_size = sizeof(uint64_t);
    size_t remainder = tag_size % align;
    return remainder == 0 ? tag_size : tag_size + align - remainder;
}

static HirExpr *match_field_expr(Sema *sema, Span span, HirExpr *object, HirType *type, size_t offset) {
    HirField *field = arena_calloc(sema->arena, sizeof(*field));
    *field = (HirField){.type = type, .offset = offset};

    HirExpr *expr = arena_calloc(sema->arena, sizeof(*expr));
    *expr = (HirExpr){
        .span = span,
        .kind = HIR_EXPR_FIELD,
        .type = type,
        .field = {.object = object, .field = field},
    };
    return expr;
}

static HirExpr *match_local_ref(Sema *sema, Span span, Symbol *symbol, HirType *type) {
    HirExpr *expr = arena_calloc(sema->arena, sizeof(*expr));
    *expr = (HirExpr){
        .span = span,
        .kind = HIR_EXPR_VALUE,
        .type = type,
        .value = {.symbol = symbol},
    };
    return expr;
}

static bool match_insert_binding(Sema *sema, AstName name, HirType *type, Span span, HirExpr *value, HirStmt **init) {
    if (name.kind != AST_NAME_IDENT)
        return false;

    Scope *scope = array_at(&sema->scopes, sema->scopes.len - 1);
    Symbol *previous = scope_lookup(scope, name);
    if (previous == NULL)
        previous = sema_lookup_non_method(sema, name);

    if (previous != NULL) {
        error_shadowing(sema->diags, name.ident, span, previous->span);
        return false;
    }

    Symbol *symbol = arena_calloc(sema->arena, sizeof(*symbol));
    *symbol = (Symbol){
        .kind = SYMBOL_LOCAL,
        .name = name,
        .type = type,
        .span = span,
    };
    scope_insert(scope, symbol);

    HirLocal local = {.symbol = symbol, .type = type};
    array_push(&sema->current_fn->locals, &local);

    HirExpr *target = match_local_ref(sema, span, symbol, type);
    *init = hir_stmt(sema, HIR_STMT_ASSIGN, span);
    (*init)->assign = (typeof((*init)->assign)){
        .op = AST_BINARY_ASSIGN,
        .target = target,
        .value = value,
    };
    return true;
}

static HirStmt *sema_match_stmt(Sema *sema, AstStmt *ast) {
    HirStmt *error = hir_stmt(sema, HIR_STMT_ERROR, ast->span);

    if (sema->current_fn == NULL) {
        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_TYPE_MISMATCH, ast->span, STRING("match requires a sum-type value inside a function"));
        diag_finish(&diagnostic);
        return error;
    }

    HirExpr *scrutinee = sema_expr(sema, ast->match.expr, NULL);
    if (scrutinee == NULL || scrutinee->kind == HIR_EXPR_ERROR || scrutinee->type == NULL)
        return error;

    HirType *sum = match_unwrap_type(scrutinee->type);
    if (sum == NULL || sum->kind != HIR_TYPE_SUM) {
        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_TYPE_MISMATCH, ast->match.expr->span, STRING("match currently requires a sum-type value"));
        diag_finish(&diagnostic);
        return error;
    }

    HirType *tag_type = builtin_type(sema, BUILTIN_UINT64);
    HirType *bool_type = builtin_type(sema, BUILTIN_BOOL);
    size_t member_count = sum->sum.members.len;
    bool *covered = arena_calloc(sema->arena, sizeof(bool) * member_count);

    Symbol *temporary = arena_calloc(sema->arena, sizeof(*temporary));
    *temporary = (Symbol){
        .kind = SYMBOL_LOCAL,
        .name = {.kind = AST_NAME_ERROR},
        .type = scrutinee->type,
        .span = ast->span,
    };
    HirLocal temporary_local = {.symbol = temporary, .type = scrutinee->type};
    array_push(&sema->current_fn->locals, &temporary_local);

    HirStmt *save = hir_stmt(sema, HIR_STMT_ASSIGN, ast->match.expr->span);
    save->assign = (typeof(save->assign)){
        .op = AST_BINARY_ASSIGN,
        .target = match_local_ref(sema, ast->match.expr->span, temporary, scrutinee->type),
        .value = scrutinee,
    };

    HirExpr *temporary_ref = match_local_ref(sema, ast->match.expr->span, temporary, scrutinee->type);
    Array(SemaMatchCase) cases = array_create(sema->arena, sizeof(SemaMatchCase));
    bool exhaustive = false;

    for (size_t i = 0; i < ast->match.cases.len; i++) {
        AstMatchCase *ast_case = array_at(&ast->match.cases, i);
        AstPattern *pattern = ast_case->pattern;
        size_t member_index = SIZE_MAX;
        bool unconditional = false;
        bool bind_payload = false;
        AstName binding_name = {.kind = AST_NAME_ERROR};
        HirType *binding_type = NULL;

        switch (pattern->kind) {
            case AST_PATTERN_WILDCARD:
                unconditional = true;
                exhaustive = true;
                break;

            case AST_PATTERN_VARIANT: {
                Symbol *variant = sema_lookup(sema, pattern->variant.name);
                if (variant == NULL || variant->kind != SYMBOL_TYPE || variant->type == NULL) {
                    String name = pattern->variant.name.kind == AST_NAME_IDENT ? pattern->variant.name.ident : STRING("<pattern>");
                    error_unknown_name(sema->diags, name, pattern->span);
                    return error;
                }

                member_index = match_member_index(sum, variant->type);
                if (member_index == SIZE_MAX) {
                    DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_TYPE_MISMATCH, pattern->span, STRING("pattern type is not a member of the matched sum type"));
                    diag_finish(&diagnostic);
                    return error;
                }

                binding_name = pattern->variant.binding;
                binding_type = ((HirType **)sum->sum.members.data)[member_index];
                bind_payload = true;
                break;
            }

            case AST_PATTERN_BINDING: {
                Symbol *symbol = sema_lookup(sema, pattern->binding);
                if (symbol != NULL && symbol->kind == SYMBOL_TYPE && symbol->type != NULL) {
                    member_index = match_member_index(sum, symbol->type);
                    if (member_index == SIZE_MAX) {
                        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_TYPE_MISMATCH, pattern->span, STRING("pattern type is not a member of the matched sum type"));
                        diag_finish(&diagnostic);
                        return error;
                    }
                } else {
                    unconditional = true;
                    exhaustive = true;
                    binding_name = pattern->binding;
                    binding_type = scrutinee->type;
                    bind_payload = true;
                }
                break;
            }

            case AST_PATTERN_EXPR: {
                if (pattern->expr->kind != AST_EXPR_PATH) {
                    DiagBuilder diagnostic = diag_begin(
                        sema->diags,
                        DIAG_ERROR,
                        E_UNEXPECTED_TOKEN,
                        pattern->span,
                        STRING("this expression pattern is not supported yet")
                    );
                    diag_finish(&diagnostic);
                    return error;
                }

                Symbol *symbol = sema_lookup_path(sema, pattern->expr->path);

                if (symbol == NULL || symbol->kind != SYMBOL_TYPE ||
                    symbol->type == NULL) {
                    error_unknown_type(sema->diags, pattern->expr->path, pattern->span);
                    return error;
                }

                member_index = match_member_index(sum, symbol->type);

                if (member_index == SIZE_MAX) {
                    DiagBuilder diagnostic = diag_begin(
                        sema->diags,
                        DIAG_ERROR,
                        E_TYPE_MISMATCH,
                        pattern->span,
                        STRING("pattern type is not a member of the matched sum type")
                    );
                    diag_finish(&diagnostic);
                    return error;
                }

                break;
            }

            default: {
                DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_UNEXPECTED_TOKEN, pattern->span, STRING("this match pattern form is not supported yet"));
                diag_note(&diagnostic, STRING("sum-type matches currently support type variants, payload bindings, and wildcard patterns"));
                diag_finish(&diagnostic);
                return error;
            }
        }

        if (member_index != SIZE_MAX) {
            if (covered[member_index]) {
                DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_UNEXPECTED_TOKEN, pattern->span, STRING("duplicate sum-type match case"));
                diag_finish(&diagnostic);
                return error;
            }
            covered[member_index] = true;
        }

        if (unconditional)
            for (size_t j = 0; j < member_count; j++)
                covered[j] = true;

        HirExpr *condition = NULL;
        if (!unconditional) {
            HirExpr *tag = match_field_expr(sema, pattern->span, temporary_ref, tag_type, 0);
            HirExpr *index = arena_calloc(sema->arena, sizeof(*index));
            *index = (HirExpr){
                .span = pattern->span,
                .kind = HIR_EXPR_LITERAL,
                .type = tag_type,
                .literal = {.kind = HIR_LITERAL_INTEGER, .integer = member_index},
            };

            condition = arena_calloc(sema->arena, sizeof(*condition));
            *condition = (HirExpr){
                .span = pattern->span,
                .kind = HIR_EXPR_BINARY,
                .type = bool_type,
                .binary = {.op = AST_BINARY_EQUAL, .left = tag, .right = index},
            };
        }

        HirStmt *binding_init = NULL;
        sema_push_scope(sema);

        if (bind_payload) {
            HirExpr *value = temporary_ref;
            if (member_index != SIZE_MAX)
                value = match_field_expr(sema, pattern->span, temporary_ref, binding_type, match_payload_offset(sum));

            if (!match_insert_binding(sema, binding_name, binding_type, pattern->span, value, &binding_init)) {
                sema_pop_scope(sema);
                return error;
            }
        }

        HirStmt *body = sema_stmt(sema, ast_case->body);
        sema_pop_scope(sema);

        if (body == NULL || body->kind == HIR_STMT_ERROR)
            return error;

        if (binding_init != NULL) {
            HirStmt *block = hir_stmt(sema, HIR_STMT_BLOCK, ast_case->span);
            block->block.stmts = array_create(sema->arena, sizeof(HirStmt *));
            array_push(&block->block.stmts, &binding_init);
            array_push(&block->block.stmts, &body);
            body = block;
        }

        SemaMatchCase lowered = {
            .condition = condition,
            .body = body,
            .member_index = member_index,
        };
        array_push(&cases, &lowered);
    }

    for (size_t i = 0; i < member_count; i++) {
        if (!covered[i]) {
            DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_TYPE_MISMATCH, ast->span, STRING("match does not cover every member of the sum type"));
            diag_finish(&diagnostic);
            return error;
        }
    }

    exhaustive = true;
    HirStmt *chain = NULL;
    for (size_t i = cases.len; i > 0; i--) {
        SemaMatchCase *current = array_at(&cases, i - 1);

        if (current->condition == NULL || (exhaustive && i == cases.len && current->member_index != SIZE_MAX)) {
            chain = current->body;
            continue;
        }

        HirStmt *branch = hir_stmt(sema, HIR_STMT_IF, ast->span);
        branch->_if.cond = current->condition;
        branch->_if.then = current->body;
        branch->_if._else = chain;
        chain = branch;
    }

    HirStmt *result = hir_stmt(sema, HIR_STMT_BLOCK, ast->span);
    result->block.stmts = array_create(sema->arena, sizeof(HirStmt *));
    array_push(&result->block.stmts, &save);
    if (chain != NULL)
        array_push(&result->block.stmts, &chain);
    return result;
}

HirStmt *sema_stmt(Sema *sema, AstStmt *ast) {
    if (ast->comptime) {
        AstStmt copy = *ast;
        copy.comptime = false;

        bool previous = sema->comptime;
        sema->comptime = true;

        HirStmt *hir = sema_stmt(sema, &copy);

        sema->comptime = previous;

        if (hir == NULL || hir->kind == HIR_STMT_ERROR)
            return hir;

        if (!comp_exec_stmt(sema, hir)) {
            error_comptime_not_evaluable(sema->diags, ast->span);
            hir->kind = HIR_STMT_ERROR;
            return hir;
        }

        return NULL;
    }

    HirStmt *hir = arena_calloc(sema->arena, sizeof(HirStmt));

    hir->span = ast->span;

    switch (ast->kind) {
        case AST_STMT_VAR: {
            HirStmt *init = NULL;

            if (!local_decl(sema, ast->var, &init)) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            return init;
        }

        case AST_STMT_EXPR: {
            if (stmt_is_assignment_expr(ast->expr))
                return sema_assignment_stmt(sema, ast->expr);

            hir->kind = HIR_STMT_EXPR;
            hir->expr = sema_expr(sema, ast->expr, NULL);

            if (hir->expr == NULL || hir->expr->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            break;
        }

        case AST_STMT_BLOCK:
            if (!block(sema, ast, hir)) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            return hir;

        case AST_STMT_RETURN:
            hir->kind = HIR_STMT_RETURN;
            hir->_return.value = NULL;

            if (sema->current_fn == NULL) {
                error_invalid_return(sema->diags, NULL, NULL, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            HirType *return_type = sema->current_fn->return_type;
            bool returns_none = sema_type_is_none(return_type);

            if (ast->_return.value == NULL) {
                if (!sema_type_allows_empty_return(return_type)) {
                    error_missing_return_value(sema->diags, return_type, ast->span);
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }

                hir->_return.value = sema_make_empty_sum_return(sema, return_type, ast->span);
                return hir;
            }

            HirExpr *value = sema_expr(sema, ast->_return.value, return_type);

            if (value == NULL || value->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            if (returns_none) {
                error_invalid_return(sema->diags, return_type, value->type, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            value = sema_coerce(sema, value, return_type);

            if (value == NULL || value->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->_return.value = value;
            return hir;

        case AST_STMT_IF: {
            hir->kind = HIR_STMT_IF;
            hir->_if.cond = sema_expr(sema, ast->_if.cond, NULL);

            if (hir->_if.cond == NULL || hir->_if.cond->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->_if.then = sema_stmt(sema, ast->_if.then);

            if (hir->_if.then == NULL || hir->_if.then->kind == HIR_STMT_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->_if._else = NULL;

            if (ast->_if._else != NULL) {
                hir->_if._else = sema_stmt(sema, ast->_if._else);

                if (hir->_if._else == NULL || hir->_if._else->kind == HIR_STMT_ERROR) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }
            }

            //! TODO: require bool condition

            return hir;
        }

        case AST_STMT_WHILE: {
            hir->kind = HIR_STMT_WHILE;
            hir->_while.cond = sema_expr(sema, ast->_while.cond, NULL);

            if (hir->_while.cond == NULL || hir->_while.cond->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            sema_push_scope(sema)->loop = true;

            hir->_while.body = sema_stmt(sema, ast->_while.body);

            sema_pop_scope(sema);

            if (hir->_while.body == NULL || hir->_while.body->kind == HIR_STMT_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            //! TODO: require bool condition

            return hir;
        }

        case AST_STMT_BREAK: {
            size_t level = 1;

            if (ast->_break.value != NULL) {
                HirExpr *expr = sema_expr(sema, ast->_break.value, NULL);

                if (expr == NULL || expr->kind == HIR_EXPR_ERROR) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }

                CompContext context = {
                    .sema = sema,
                    .frame = NULL
                };

                expr = comp_eval_expr(&context, expr);

                //! TODO: require comptime integer
                //! TODO: extract integer

                if (expr->kind != HIR_EXPR_LITERAL) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }
            }

            size_t loopscope = loop_scope(sema, level);

            if (loopscope == SIZE_MAX) {
                error_invalid_break(sema->diags, level, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->kind = HIR_STMT_BREAK;
            hir->_break.level = level;

            return lower_defer_exit(sema, hir, loopscope);
        }

        case AST_STMT_CONTINUE: {
            size_t level = 1;

            if (ast->_continue.value != NULL) {
                HirExpr *expr = sema_expr(sema, ast->_continue.value, NULL);

                if (expr == NULL || expr->kind == HIR_EXPR_ERROR) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }

                CompContext context = {
                    .sema = sema,
                    .frame = NULL,
                };

                expr = comp_eval_expr(&context, expr);

                //! TODO: require comptime integer
                //! TODO: extract integer

                if (expr->kind != HIR_EXPR_LITERAL) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }
            }

            size_t loopscope = loop_scope(sema, level);

            if (loopscope == SIZE_MAX) {
                error_invalid_continue(sema->diags, level, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->kind = HIR_STMT_CONTINUE;
            hir->_continue.level = level;

            return lower_defer_exit(sema, hir, loopscope);
        }

        case AST_STMT_FOR: {
            HirStmt *block = hir_stmt(sema, HIR_STMT_BLOCK, ast->span);
            block->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

            sema_push_scope(sema)->loop = true;

            if (ast->_for.init != NULL) {
                HirStmt *init = sema_stmt(sema, ast->_for.init);

                if (init != NULL) {
                    if (init->kind == HIR_STMT_ERROR) {
                        sema_pop_scope(sema);
                        return init;
                    }

                    array_push(&block->block.stmts, &init);
                }
            }

            HirStmt *loop = hir_stmt(sema, HIR_STMT_WHILE, ast->span);

            if (ast->_for.cond != NULL)
                loop->_while.cond = sema_expr(sema, ast->_for.cond, NULL);
            else
                loop->_while.cond = hir_bool(sema, true);

            if (loop->_while.cond == NULL ||
                loop->_while.cond->kind == HIR_EXPR_ERROR) {
                sema_pop_scope(sema);
                loop->kind = HIR_STMT_ERROR;
                return loop;
            }

            HirStmt *body = sema_stmt(sema, ast->_for.body);

            if (body == NULL || body->kind == HIR_STMT_ERROR) {
                sema_pop_scope(sema);
                loop->kind = HIR_STMT_ERROR;
                return loop;
            }

            HirStmt *loop_body = hir_stmt(sema, HIR_STMT_BLOCK, ast->_for.body->span);
            loop_body->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

            array_push(&loop_body->block.stmts, &body);

            if (ast->_for.post != NULL) {
                HirStmt *post;
                if (stmt_is_assignment_expr(ast->_for.post)) {
                    post = sema_assignment_stmt(sema, ast->_for.post);
                } else {
                    post = hir_stmt(sema, HIR_STMT_EXPR, ast->_for.post->span);
                    post->expr = sema_expr(sema, ast->_for.post, NULL);
                }

                if (post == NULL || post->kind == HIR_STMT_ERROR ||
                    (post->kind == HIR_STMT_EXPR &&
                     (post->expr == NULL || post->expr->kind == HIR_EXPR_ERROR))) {
                    sema_pop_scope(sema);
                    loop->kind = HIR_STMT_ERROR;
                    return loop;
                }

                array_push(&loop_body->block.stmts, &post);
            }

            loop->_while.body = loop_body;

            array_push(&block->block.stmts, &loop);

            sema_pop_scope(sema);

            return block;
        }

        case AST_STMT_MATCH:
            return sema_match_stmt(sema, ast);

        case AST_STMT_DEFER:
            //! TODO: lower defer
            hir->kind = HIR_STMT_ERROR;
            break;

        case AST_STMT_ERROR:
        default:
            hir->kind = HIR_STMT_ERROR;
            //! TODO: internal compiler error
            fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);
            break;
    }

    return hir;
}
