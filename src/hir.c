#include <stdio.h>
#include "hir.h"

HirExpr *lower_expr(Analyser *ctx, Expr *ast_expr) {
    if (!ast_expr) return NULL;
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
            size_t offset = 0;
            if (type_sym && type_sym->decl && type_sym->decl->type == DECL_STRUCT) {
                StructDecl *str = type_sym->decl->_struct;
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
        case STMT_UNSAFE:
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
            hir->_if.then_block = ast_stmt->_if.then ? lower_stmt(ctx, ast_stmt->_if.then) : NULL;
            hir->_if.else_block = ast_stmt->_if._else ? lower_stmt(ctx, ast_stmt->_if._else) : NULL;
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

            if (ast_stmt->_for.body) {
                hirstmts_array_push(&while_body->block.stmts, lower_stmt(ctx, ast_stmt->_for.body));
            }

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
            hir->_while.body = ast_stmt->_while.body ? lower_stmt(ctx, ast_stmt->_while.body) : NULL;
            break;
        }
    }

    return hir;
}

static void collect_locals(syms_array *locals, syms_array *params, HirStmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case HIR_STMT_ASSIGN: {
            if (stmt->assign.target->type == HIR_EXPR_VAR) {
                Symbol *sym = stmt->assign.target->var.symbol;

                bool is_param = false;
                for (size_t i = 0; i < params->len; i++) {
                    if (params->data[i] == sym) { is_param = true; break; }
                }

                bool already_added = false;
                for (size_t i = 0; i < locals->len; i++) {
                    if (locals->data[i] == sym) {
                        already_added = true;
                        break;
                    }
                }

                if (!is_param && !already_added) {
                    syms_array_push(locals, sym);
                }
            }
            break;
        }
        case HIR_STMT_BLOCK: {
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                collect_locals(locals, params, stmt->block.stmts.data[i]);
            }
            break;
        }
        case HIR_STMT_IF: {
            collect_locals(locals, params, stmt->_if.then_block);
            collect_locals(locals, params, stmt->_if.else_block);
            break;
        }
        case HIR_STMT_WHILE: {
            collect_locals(locals, params, stmt->_while.body);
            break;
        }
    }
}

HirModule *hir_lower_module(Analyser *ctx, Module *ast_mod) {
    HirModule *hir = arena_calloc(ctx->arena, sizeof(HirModule));
    hir->functions = hirfndecls_array_init();
    for (size_t i = 0; i < ast_mod->decls.len; i++) {
        Decl *d = ast_mod->decls.data[i];
        if (d->type != DECL_FN) continue;

        HirFnDecl *fndecl = arena_calloc(ctx->arena, sizeof(HirFnDecl));
        fndecl->symbol = d->symbol;
        fndecl->is_extern = d->fn->is_extern;

        fndecl->params = syms_array_init();
        for (size_t j = 0; j < d->fn->params.len; j++) {
            syms_array_push(&fndecl->params, d->fn->params.data[j].symbol);
        }

        if (d->fn->body) {
            fndecl->body = lower_stmt(ctx, d->fn->body);
        } else {
            fndecl->body = NULL;
        }
        fndecl->ret_type = d->fn->ret_type;

        fndecl->locals = syms_array_init();
        collect_locals(&fndecl->locals, &fndecl->params, fndecl->body);

        hirfndecls_array_push(&hir->functions, fndecl);
    }

    return hir;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
}

static void print_type(TypeRef *type) {
    if (!type) {
        printf("<unknown>");
        return;
    }

    switch (type->type) {
        case TYPEREF_NAMED:
            printf("%.*s", string_fmt(type->named.name));
            break;
        case TYPEREF_POINTER:
            print_type(type->pointer.pointee);
            printf("*");
            break;
        case TYPEREF_ARRAY:
            print_type(type->array.elem);
            printf("[%zu]", type->array.length);
            break;
        case TYPEREF_FN:
            printf("fn ");
            print_type(type->fn.ret_type);
            printf(" (");
            for (size_t i = 0; i < type->fn.params.len; i++) {
                if (i > 0) printf(", ");
                print_type(type->fn.params.data[i].type);
                if (type->fn.params.data[i].name.length > 0) {
                    printf(" %.*s", string_fmt(type->fn.params.data[i].name));
                }
            }
            printf(")");
            break;
    }
}

static const char *unary_op_name(UnaryOp op) {
    switch (op) {
        case UOP_NEG: return "-";
        case UOP_NOT: return "!";
        case UOP_DEREF: return "*";
        case UOP_ADDR: return "&";
        default: return "?";
    }
}

static const char *binary_op_name(BinaryOp op) {
    switch (op) {
        case BINOP_MUL: return "*";
        case BINOP_DIV: return "/";
        case BINOP_MOD: return "%";
        case BINOP_ADD: return "+";
        case BINOP_SUB: return "-";
        case BINOP_SHL: return "<<";
        case BINOP_SHR: return ">>";
        case BINOP_LT: return "<";
        case BINOP_LE: return "<=";
        case BINOP_GT: return ">";
        case BINOP_GE: return ">=";
        case BINOP_EQ: return "==";
        case BINOP_NE: return "!=";
        case BINOP_AND: return "&";
        case BINOP_XOR: return "^";
        case BINOP_OR: return "|";
        case BINOP_LOG_AND: return "&&";
        case BINOP_LOG_OR: return "||";
        case BINOP_ASSIGN: return "=";
        case BINOP_ADD_ASSIGN: return "+=";
        default: return "?";
    }
}

static void print_literal(Literal lit) {
    switch (lit.type) {
        case LITERAL_INT:
            printf("%lld", lit._int);
            break;
        case LITERAL_FLOAT:
            printf("%g", lit._float);
            break;
        case LITERAL_STRING:
            printf("\"%.*s\"", string_fmt(lit.string));
            break;
        case LITERAL_BOOL:
            printf(lit._bool ? "true" : "false");
            break;
        case LITERAL_CHAR:
            printf("'%c'", lit._char);
            break;
        case LITERAL_NULL:
            printf("null");
            break;
    }
}

static void print_hir_expr(HirExpr *expr) {
    if (!expr) {
        printf("<null>");
        return;
    }

    switch (expr->type) {
        case HIR_EXPR_LIT:
            print_literal(expr->literal);
            break;
        case HIR_EXPR_VAR:
            printf("%.*s", string_fmt(expr->var.symbol->name));
            break;
        case HIR_EXPR_UNARY:
            printf("%s", unary_op_name(expr->unary.op));
            print_hir_expr(expr->unary.operand);
            break;
        case HIR_EXPR_BINARY:
            printf("(");
            print_hir_expr(expr->binary.left);
            printf(" %s ", binary_op_name(expr->binary.op));
            print_hir_expr(expr->binary.right);
            printf(")");
            break;
        case HIR_EXPR_CALL:
            printf("%.*s(", string_fmt(expr->call.callee->name));
            for (size_t i = 0; i < expr->call.args.len; i++) {
                if (i > 0) printf(", ");
                print_hir_expr(expr->call.args.data[i]);
            }
            printf(")");
            break;
        case HIR_EXPR_FIELD_OFFSET:
            print_hir_expr(expr->field_offset.base);
            printf(" + %zu", expr->field_offset.byte_offset);
            break;
        case HIR_EXPR_ARRAY_INDEX:
            print_hir_expr(expr->array_index.base);
            printf("[");
            print_hir_expr(expr->array_index.index);
            printf("]");
            break;
        case HIR_EXPR_CAST:
            printf("(");
            print_type(expr->cast.to_type);
            printf(")");
            print_hir_expr(expr->cast.expr);
            break;
    }
}

static void print_hir_stmt(HirStmt *stmt, int indent) {
    if (!stmt) return;

    switch (stmt->type) {
        case HIR_STMT_ASSIGN:
            print_indent(indent);
            print_hir_expr(stmt->assign.target);
            printf(" = ");
            print_hir_expr(stmt->assign.value);
            printf(";\n");
            break;
        case HIR_STMT_EXPR:
            print_indent(indent);
            print_hir_expr(stmt->expr);
            printf(";\n");
            break;
        case HIR_STMT_RETURN:
            print_indent(indent);
            printf("return");
            if (stmt->_return.value) {
                printf(" ");
                print_hir_expr(stmt->_return.value);
            }
            printf(";\n");
            break;
        case HIR_STMT_BLOCK:
            print_indent(indent);
            printf("{\n");
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                print_hir_stmt(stmt->block.stmts.data[i], indent + 2);
            }
            print_indent(indent);
            printf("}\n");
            break;
        case HIR_STMT_IF:
            print_indent(indent);
            printf("if (");
            print_hir_expr(stmt->_if.cond);
            printf(")");
            if (stmt->_if.then_block && stmt->_if.then_block->type == HIR_STMT_BLOCK) {
                printf(" ");
                print_hir_stmt(stmt->_if.then_block, indent);
            } else {
                printf("\n");
                print_hir_stmt(stmt->_if.then_block, indent + 2);
            }
            if (stmt->_if.else_block) {
                print_indent(indent);
                printf("else");
                if (stmt->_if.else_block->type == HIR_STMT_BLOCK) {
                    printf(" ");
                    print_hir_stmt(stmt->_if.else_block, indent);
                } else {
                    printf("\n");
                    print_hir_stmt(stmt->_if.else_block, indent + 2);
                }
            }
            break;
        case HIR_STMT_WHILE:
            print_indent(indent);
            printf("while (");
            print_hir_expr(stmt->_while.cond);
            printf(")");
            if (stmt->_while.body && stmt->_while.body->type == HIR_STMT_BLOCK) {
                printf(" ");
                print_hir_stmt(stmt->_while.body, indent);
            } else {
                printf("\n");
                print_hir_stmt(stmt->_while.body, indent + 2);
            }
            break;
    }
}

void hir_pretty_print(HirModule *mod) {
    printf("--- HIR Module ---\n");
    for (size_t i = 0; i < mod->functions.len; i++) {
        HirFnDecl *fn = mod->functions.data[i];
        printf("function ");
        print_type(fn->ret_type);
        printf(" %.*s(", string_fmt(fn->symbol->name));
        for (size_t j = 0; j < fn->params.len; j++) {
            if (j > 0) printf(", ");
            print_type(fn->params.data[j]->type);
            printf(" %.*s", string_fmt(fn->params.data[j]->name));
        }
        printf(") ");
        if (fn->body) {
            if (fn->body->type == HIR_STMT_BLOCK) {
                print_hir_stmt(fn->body, 0);
            } else {
                printf("{\n");
                print_hir_stmt(fn->body, 2);
                printf("}\n");
            }
        } else {
            printf(";\n");
        }
        printf("\n");
    }
    printf("------------------\n\n");
}