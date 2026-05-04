#include "hir.h"
#include "sema.h"

HirExpr *lower_expr(Analyser *ctx, Expr *ast_expr) {
    HirExpr *hir = arena_calloc(ctx->arena, sizeof(HirExpr));
    hir->resolved_type = ast_expr->resolved_type;

    switch (ast_expr->type) {
        case EXPR_LIT: {
            hir->type = HIR_EXPR_LIT;
            hir->literal = ast_expr->literal;
            break;
        }
        case EXPR_PATH:
        case EXPR_IDENT: {
            hir->type = HIR_EXPR_VAR;
            hir->var.symbol = ast_expr->symbol;
            break;
        }
        case EXPR_UNARY: {
            hir->type = HIR_EXPR_UNARY;
            hir->unary.op = ast_expr->unary.op;
            hir->unary.operand = lower_expr(ctx, ast_expr->unary.operand);
            break;
        }
        case EXPR_BINARY: {
            hir->type = HIR_EXPR_BINARY;
            hir->binary.op = ast_expr->binary.op;
            hir->binary.left = lower_expr(ctx, ast_expr->binary.left);
            hir->binary.right = lower_expr(ctx, ast_expr->binary.right);
            break;
        }
        case EXPR_CALL: {
            hir->type = HIR_EXPR_CALL;
            hir->call.callee = ast_expr->call.callee->symbol;
            hir->call.args = hirexprs_array_init();
            for (size_t i = 0; i < ast_expr->call.args.len; i++) {
                hirexprs_array_push(&hir->call.args, lower_expr(ctx, ast_expr->call.args.data[i]));
            }
            break;
        }
        case EXPR_INDEX: {
            hir->type = HIR_EXPR_ARRAY_INDEX;
            hir->array_index.base = lower_expr(ctx, ast_expr->index.base);
            hir->array_index.index = lower_expr(ctx, ast_expr->index.index);
            
            TypeRef *base_type = ast_expr->index.base->resolved_type;
            if (base_type->type == TYPEREF_POINTER) {
                hir->array_index.elem_size = get_type_size(base_type->pointer.pointee);
            } else if (base_type->type == TYPEREF_ARRAY) {
                hir->array_index.elem_size = get_type_size(base_type->array.elem);
            }
            break;
        }
        case EXPR_MEMBER: {
            hir->type = HIR_EXPR_FIELD_OFFSET;
            hir->field_offset.base = lower_expr(ctx, ast_expr->member.base);

            Symbol *type_sym = ast_expr->member.base->resolved_type->type_symbol;
            StructDecl *str = type_sym->decl->_struct;

            size_t offset = 0;
            if (type_sym->decl->type == DECL_STRUCT) {
                for (size_t i = 0; i < str->members.len; i++) {
                    if (string_eq(str->members.data[i]->name, ast_expr->member.member)) {
                        offset = str->field_offsets.data[i];
                        break;
                    }
                }
            }
            hir->field_offset.byte_offset = offset;
            break;
        }
        case EXPR_CAST: {
            hir->type = HIR_EXPR_CAST;
            hir->cast.expr = lower_expr(ctx, ast_expr->cast.expr);
            hir->cast.to_type = ast_expr->cast.to;
            break;
        }
    }

    return hir;
}

HirStmt *lower_stmt(Analyser *ctx, Stmt *ast_stmt) {
    HirStmt *hir = arena_calloc(ctx->arena, sizeof(HirStmt));
    
    switch (ast_stmt->type) {
        case STMT_VAR: {
            hir->type = HIR_STMT_ASSIGN;
            
            HirExpr *target = arena_calloc(ctx->arena, sizeof(HirExpr));
            target->type = HIR_EXPR_VAR;
            target->var.symbol = ast_stmt->var->symbol;
            target->resolved_type = ast_stmt->var->type;

            hir->assign.target = target;
            hir->assign.value = lower_expr(ctx, ast_stmt->var->init);
            break;
        }
        case STMT_EXPR: {
            hir->type = HIR_STMT_EXPR;
            hir->expr = lower_expr(ctx, ast_stmt->expr);
            break;
        }
        case STMT_BLOCK: {
            hir->type = HIR_STMT_BLOCK;
            hir->block.stmts = hirstmts_array_init();
            for (size_t i = 0; i < ast_stmt->block.stmts.len; i++) {
                hirstmts_array_push(&hir->block.stmts, lower_stmt(ctx, ast_stmt->block.stmts.data[i]));
            }
            break;
        }
        case STMT_RETURN: {
            hir->type = HIR_STMT_RETURN;
            hir->_return.value = lower_expr(ctx, ast_stmt->_return.value);
            break;
        }
        case STMT_IF: {
            hir->type = HIR_STMT_IF;
            hir->_if.cond = lower_expr(ctx, ast_stmt->_if.cond);
            hir->_if.then_block = lower_stmt(ctx, ast_stmt->_if.then);
            hir->_if.else_block = lower_stmt(ctx, ast_stmt->_if._else);
            break;
        }
        case STMT_FOR: {
            hir->type = HIR_STMT_BLOCK;
            hir->block.stmts = hirstmts_array_init();

            if (ast_stmt->_for.init) {
                hirstmts_array_push(&hir->block.stmts, lower_stmt(ctx, ast_stmt->_for.init));
            }

            HirStmt *_while = arena_calloc(ctx->arena, sizeof(HirStmt));
            _while->type = HIR_STMT_WHILE;
            _while->_while.cond = lower_expr(ctx, ast_stmt->_for.cond);

            HirStmt *while_body = arena_calloc(ctx->arena, sizeof(HirStmt));
            while_body->type = HIR_STMT_BLOCK;
            while_body->block.stmts = hirstmts_array_init();

            hirstmts_array_push(&while_body->block.stmts, lower_stmt(ctx, ast_stmt->_for.body));

            if (ast_stmt->_for.post) {
                HirStmt *post_stmt = arena_calloc(ctx->arena, sizeof(HirStmt));
                post_stmt->type = HIR_STMT_EXPR;
                post_stmt->expr = lower_expr(ctx, ast_stmt->_for.post);
                hirstmts_array_push(&while_body->block.stmts, post_stmt);
            }

            _while->_while.body = while_body;

            hirstmts_array_push(&hir->block.stmts, _while);
            break;
        }
        case STMT_WHILE: {
            hir->type = HIR_STMT_WHILE;
            hir->_while.cond = lower_expr(ctx, ast_stmt->_while.cond);
            hir->_while.body = lower_stmt(ctx, ast_stmt->_while.body);
            break;
        }
        case STMT_UNSAFE: {
            // TODO: none of `unsafe` is implemented upstream, it's WIP so ignore for now
            break;
        }
    }

    return hir;
}