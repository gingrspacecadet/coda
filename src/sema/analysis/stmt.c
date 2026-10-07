#include "common.h"

static bool local_decl(Sema *sema, AstVarDecl *ast, HirStmt **init_stmt) {
    Scope *scope = (Scope *)array_at(&sema->scopes, sema->scopes.len - 1);

    Symbol *s = scope_lookup(scope, ast->name);
    if (s != NULL) {
        error_duplicate_symbol(sema->diags, ast->name.ident, ast->span, s->span);
        return false;
    }

    Symbol *existing = sema_lookup(sema, ast->name);

    if (existing != NULL) {
        error_shadowing(sema->diags, ast->name.ident, ast->span, existing->span);
        return false;
    }

    HirType *type = sema_type(sema, ast->type);

    if (type == NULL || type->kind == HIR_TYPE_ERROR)
        return false;

    HirExpr *init = NULL;

    if (ast->init != NULL) {
        init = sema_expr(sema, ast->init, type);

        if (init == NULL || init->kind == HIR_EXPR_ERROR)
            return false;
    }

    Symbol *symbol = arena_alloc(sema->arena, sizeof(Symbol));
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
        HirExpr *target = arena_alloc(sema->arena, sizeof(HirExpr));
        *target = (HirExpr) {
            .span = ast->span,
            .kind = HIR_EXPR_VALUE,
            .type = type,
            .value = {
                .symbol = symbol,
            },
        };

        HirStmt *stmt = arena_alloc(sema->arena, sizeof(HirStmt));
        *stmt = (HirStmt) {
            .span = ast->span,
            .kind = HIR_STMT_ASSIGN,
            .assign = {
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
    HirStmt *stmt = arena_alloc(sema->arena, sizeof(HirStmt));

    stmt->span = span;
    stmt->kind = kind;

    return stmt;
}

static HirExpr *hir_bool(Sema *sema, bool value) {
    HirExpr *expr = arena_alloc(sema->arena, sizeof(HirExpr));

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

    HirStmt *hir = arena_alloc(sema->arena, sizeof(HirStmt));

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
            if (ast->expr->kind == AST_EXPR_BINARY &&
                ast->expr->binary.op == AST_BINARY_ASSIGN) {
                HirStmt *stmt = arena_alloc(sema->arena, sizeof(HirStmt));
                HirExpr *target = sema_expr(sema, ast->expr->binary.left, NULL);
                HirExpr *value = sema_expr(sema, ast->expr->binary.right, target->type);

                if (target == NULL || target->kind == HIR_EXPR_ERROR ||
                    value == NULL || value->kind == HIR_EXPR_ERROR) {
                    stmt->kind = HIR_STMT_ERROR;
                    return stmt;
                }

                if (!target->type->mutable) {
                    error_not_mutable(sema->diags, target->span);
                    stmt->kind = HIR_STMT_ERROR;
                    return stmt;
                }

                stmt->span = ast->span;
                stmt->kind = HIR_STMT_ASSIGN;
                stmt->assign.target = target;
                stmt->assign.value = value;
                return stmt;
            }

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
            bool returns_none =
                return_type != NULL &&
                return_type->kind == HIR_TYPE_BUILTIN &&
                return_type->builtin == BUILTIN_NONE;

            if (ast->_return.value == NULL) {
                if (!returns_none) {
                    error_missing_return_value(sema->diags, return_type, ast->span);
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }

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
                HirStmt *post = hir_stmt(sema, HIR_STMT_EXPR, ast->_for.post->span);
                post->expr = sema_expr(sema, ast->_for.post, NULL);

                if (post->expr == NULL || post->expr->kind == HIR_EXPR_ERROR) {
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
            //! TODO: lower match
            hir->kind = HIR_STMT_ERROR;
            break;

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
