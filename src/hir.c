#include <stdio.h>
#include "error.h"
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
            hir->call.callee = lower_expr(ctx, ast_expr->call.callee);
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

            TypeRef *base_type = ast_expr->member.base->resolved_type;
            Symbol *type_sym = base_type->type_symbol;
            size_t offset = 0;
            
            if (base_type->type == TYPEREF_ARRAY || (base_type->type == TYPEREF_NAMED && type_sym && string_eq(type_sym->name, string_make("string")))) {
                if (string_eq(ast_expr->member.member, string_make("ptr"))) {
                    offset = 0;
                } else if (string_eq(ast_expr->member.member, string_make("len"))) {
                    offset = 8;
                }
            } else if (type_sym && type_sym->decl && type_sym->decl->type == DECL_STRUCT) {
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
        case EXPR_INIT: {
            hir->type = HIR_EXPR_INIT;
            hir->init_list.fields = hirinitfields_array_init();

            for (size_t i = 0; i < ast_expr->init_list.fields.len; i++) {
                InitField *ast_field = &ast_expr->init_list.fields.data[i];
                HirInitField field = {0};
                field.field_name = ast_field->field_name;
                field.token = ast_field->token;
                field.value = lower_expr(ctx, ast_field->value);
                hirinitfields_array_push(&hir->init_list.fields, field);
            }
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
            if (!ast_stmt->var->init) {
                hir->type = HIR_STMT_BLOCK;
                hir->block.stmts = hirstmts_array_init();
                break;
            }

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
        case STMT_DEFER: {
            hir->type = HIR_STMT_DEFER;
            hir->defer.stmt = lower_stmt(ctx, ast_stmt->defer.deferred);
            break;
        }
    }

    return hir;
}

static bool symbol_in_array(syms_array *array, Symbol *sym) {
    for (size_t i = 0; i < array->len; i++) {
        if (array->data[i] == sym) return true;
    }
    return false;
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

                if (!is_param && !symbol_in_array(locals, sym)) {
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
        case HIR_STMT_DEFER: {
            collect_locals(locals, params, stmt->defer.stmt);
            break;
        }
        default:
            break;
    }
}

static void collect_ast_locals(syms_array *locals, syms_array *params, Stmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case STMT_VAR: {
            Symbol *sym = stmt->var->symbol;
            bool is_param = false;
            for (size_t i = 0; i < params->len; i++) {
                if (params->data[i] == sym) { is_param = true; break; }
            }
            if (!is_param && !symbol_in_array(locals, sym)) {
                syms_array_push(locals, sym);
            }
            break;
        }
        case STMT_BLOCK: {
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                collect_ast_locals(locals, params, stmt->block.stmts.data[i]);
            }
            break;
        }
        case STMT_IF: {
            collect_ast_locals(locals, params, stmt->_if.then);
            collect_ast_locals(locals, params, stmt->_if._else);
            break;
        }
        case STMT_WHILE: {
            collect_ast_locals(locals, params, stmt->_while.body);
            break;
        }
        case STMT_FOR: {
            collect_ast_locals(locals, params, stmt->_for.init);
            collect_ast_locals(locals, params, stmt->_for.body);
            break;
        }
        case STMT_UNSAFE: {
            for (size_t i = 0; i < stmt->unsafe.stmts.len; i++) {
                collect_ast_locals(locals, params, stmt->unsafe.stmts.data[i]);
            }
            break;
        }
        case STMT_DEFER: {
            collect_ast_locals(locals, params, stmt->defer.deferred);
            break;
        }
        default:
            break;
    }
}

HirModule *hir_lower_module(Analyser *ctx, Module *ast_mod) {
    HirModule *hir = arena_calloc(ctx->arena, sizeof(HirModule));
    hir->functions = hirfndecls_array_init();
    for (size_t i = 0; i < ast_mod->decls.len; i++) {
        Decl *d = ast_mod->decls.data[i];
        if (d->type != DECL_FN) continue;
        if (d->fn->generic_params.len > 0) continue;

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
        collect_ast_locals(&fndecl->locals, &fndecl->params, d->fn->body);

        hirfndecls_array_push(&hir->functions, fndecl);
    }

    return hir;
}

static void hir_resolve_defers_stmt(Analyser *ctx, HirStmt *stmt);

static void hir_resolve_defers_block(Analyser *ctx, HirStmt *block) {
    hirstmts_array active_defers = hirstmts_array_init();
    hirstmts_array new_stmts = hirstmts_array_init();

    for (size_t i = 0; i < block->block.stmts.len; i++) {
        HirStmt *s = block->block.stmts.data[i];

        if (s->type == HIR_STMT_DEFER) {
            hir_resolve_defers_stmt(ctx, s->defer.stmt);
            hirstmts_array_push(&active_defers, s->defer.stmt);
        }
        else if (s->type == HIR_STMT_RETURN) {
            for (int j = (int)active_defers.len - 1; j >= 0; j--) {
                hirstmts_array_push(&new_stmts, active_defers.data[j]);
            }
            hirstmts_array_push(&new_stmts, s);
            break;  // basic dead-code elimination
        }
        else {
            hir_resolve_defers_stmt(ctx, s);
            hirstmts_array_push(&new_stmts, s);
        }
    }

    if (new_stmts.len == 0 || new_stmts.data[new_stmts.len - 1]->type != HIR_STMT_RETURN) {
        for (int j = (int)active_defers.len - 1; j >= 0; j--) {
            hirstmts_array_push(&new_stmts, active_defers.data[j]);
        }
    }

    block->block.stmts = new_stmts;
}

static void hir_resolve_defers_stmt(Analyser *ctx, HirStmt *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case HIR_STMT_BLOCK: {
            hir_resolve_defers_block(ctx, stmt);
            break;
        }
        case HIR_STMT_IF: {
            hir_resolve_defers_stmt(ctx, stmt->_if.then_block);
            hir_resolve_defers_stmt(ctx, stmt->_if.else_block);
            break;
        }
        case HIR_STMT_WHILE: {
            hir_resolve_defers_stmt(ctx, stmt->_while.body);
            break;
        }
        default: {
            break;
        }
    }
}

void hir_pass_resolve_defers(Analyser *ctx, HirModule *mod) {
    for (size_t i = 0; i < mod->functions.len; i++) {
        hir_resolve_defers_stmt(ctx, mod->functions.data[i]->body);
    }
}

static HirFnDecl *get_hir_fn_by_symbol(HirModule *mod, Symbol *sym) {
    for (size_t i = 0; i < mod->functions.len; i++) {
        if (mod->functions.data[i]->symbol == sym) {
            return mod->functions.data[i];
        }
    }
    return NULL;
}

static HirFnDecl *lookup_monomorphized_fn(HirModule *mod, String name) {
    for (size_t i = 0; i < mod->functions.len; i++) {
        if (string_eq(mod->functions.data[i]->symbol->name, name)) {
            return mod->functions.data[i];
        }
    }
    return NULL;
}

static Symbol *create_symbol(Analyser *ctx, String name) {
    Symbol *sym = arena_calloc(ctx->arena, sizeof(Symbol));
    sym->name = name;
    return sym;
}

static TypeRef *instantiate_type(Analyser *ctx, TypeRef *generic_type, String placeholder_name, TypeRef *concrete) {
    if (!generic_type) return NULL;

    if (generic_type->type == TYPEREF_NAMED && string_eq(generic_type->named.name, placeholder_name)) {
        return concrete;
    }

    TypeRef *new_t = arena_calloc(ctx->arena, sizeof(TypeRef));
    *new_t = *generic_type;

    if (new_t->type == TYPEREF_POINTER) {
        new_t->pointer.pointee = instantiate_type(ctx, generic_type->pointer.pointee, placeholder_name, concrete);
    } else if (new_t->type == TYPEREF_ARRAY) {
        new_t->array.elem = instantiate_type(ctx, generic_type->array.elem, placeholder_name, concrete);
    }

    return new_t;
}

static HirExpr *clone_hir_expr(Analyser *ctx, HirExpr *src, String placeholder_name, TypeRef *concrete) {
    if (!src) return NULL;

    HirExpr *dst = arena_calloc(ctx->arena, sizeof(HirExpr));
    dst->type = src->type;
    dst->resolved_type = instantiate_type(ctx, src->resolved_type, placeholder_name, concrete);

    switch (src->type) {
        case HIR_EXPR_LIT:
            dst->literal = src->literal;
            break;
            
        case HIR_EXPR_VAR:
            dst->var.symbol = src->var.symbol;
            break;
            
        case HIR_EXPR_UNARY:
            dst->unary.op = src->unary.op;
            dst->unary.operand = clone_hir_expr(ctx, src->unary.operand, placeholder_name, concrete);
            break;
            
        case HIR_EXPR_BINARY:
            dst->binary.op = src->binary.op;
            dst->binary.left = clone_hir_expr(ctx, src->binary.left, placeholder_name, concrete);
            dst->binary.right = clone_hir_expr(ctx, src->binary.right, placeholder_name, concrete);
            break;
            
        case HIR_EXPR_CALL:
            dst->call.callee = clone_hir_expr(ctx, src->call.callee, placeholder_name, concrete);
            dst->call.args = hirexprs_array_init();
            for (size_t i = 0; i < src->call.args.len; i++) {
                hirexprs_array_push(&dst->call.args, 
                    clone_hir_expr(ctx, src->call.args.data[i], placeholder_name, concrete));
            }
            break;
            
        case HIR_EXPR_FIELD_OFFSET:
            dst->field_offset.base = clone_hir_expr(ctx, src->field_offset.base, placeholder_name, concrete);
            dst->field_offset.byte_offset = src->field_offset.byte_offset;
            break;
        
        case HIR_EXPR_ARRAY_INDEX:
            dst->array_index.base = clone_hir_expr(ctx, src->array_index.base, placeholder_name, concrete);
            dst->array_index.index = clone_hir_expr(ctx, src->array_index.index, placeholder_name, concrete);
            dst->array_index.elem_size = src->array_index.elem_size; // Note: recalculate size if it depended on T
            break;
        
        case HIR_EXPR_INIT:
            dst->init_list.fields = hirinitfields_array_init();
            for (size_t i = 0; i < src->init_list.fields.len; i++) {
                HirInitField src_field = src->init_list.fields.data[i];
                HirInitField dst_field = {0};
                dst_field.field_name = src_field.field_name;
                dst_field.token = src_field.token;
                dst_field.value = clone_hir_expr(ctx, src_field.value, placeholder_name, concrete);
                hirinitfields_array_push(&dst->init_list.fields, dst_field);
            }
            break;
        
        case HIR_EXPR_CAST:
            dst->cast.to_type = instantiate_type(ctx, src->cast.to_type, placeholder_name, concrete);
            dst->cast.expr = clone_hir_expr(ctx, src->cast.expr, placeholder_name, concrete);
            break;
    }

    return dst;
}

static HirStmt *clone_hir_stmt(Analyser *ctx, HirStmt *src, String placeholder_name, TypeRef *concrete) {
    if (!src) return NULL;

    HirStmt *dst = arena_calloc(ctx->arena, sizeof(HirStmt));
    dst->type = src->type;

    switch (src->type) {
        case HIR_STMT_EXPR:
            dst->expr = clone_hir_expr(ctx, src->expr, placeholder_name, concrete);
            break;
            
        case HIR_STMT_BLOCK:
            dst->block.stmts = hirstmts_array_init();
            for (size_t i = 0; i < src->block.stmts.len; i++) {
                hirstmts_array_push(&dst->block.stmts, 
                    clone_hir_stmt(ctx, src->block.stmts.data[i], placeholder_name, concrete));
            }
            break;
            
        case HIR_STMT_RETURN:
            dst->_return.value = clone_hir_expr(ctx, src->_return.value, placeholder_name, concrete);
            break;
            
        case HIR_STMT_IF:
            dst->_if.cond = clone_hir_expr(ctx, src->_if.cond, placeholder_name, concrete);
            dst->_if.then_block = clone_hir_stmt(ctx, src->_if.then_block, placeholder_name, concrete);
            dst->_if.else_block = clone_hir_stmt(ctx, src->_if.else_block, placeholder_name, concrete);
            break;
            
        case HIR_STMT_WHILE:
            dst->_while.cond = clone_hir_expr(ctx, src->_while.cond, placeholder_name, concrete);
            dst->_while.body = clone_hir_stmt(ctx, src->_while.body, placeholder_name, concrete);
            break;
            
        case HIR_STMT_ASSIGN:
            dst->assign.target = clone_hir_expr(ctx, src->assign.target, placeholder_name, concrete);
            dst->assign.value = clone_hir_expr(ctx, src->assign.value, placeholder_name, concrete);
            break;
            
        case HIR_STMT_DEFER:
            dst->defer.stmt = clone_hir_stmt(ctx, src->defer.stmt, placeholder_name, concrete);
            break;
    }

    return dst;
}

static void hir_scan_for_generics_expr(Analyser *ctx, HirModule *mod, HirExpr *expr) {
    if (!expr) return;
    
    if (expr->type == HIR_EXPR_CALL) {
        HirExpr *callee_expr = expr->call.callee;
        
        if (callee_expr && callee_expr->type == HIR_EXPR_VAR) {
            Symbol *func_sym = callee_expr->var.symbol;
            HirFnDecl *callee_fn = get_hir_fn_by_symbol(mod, func_sym);
            
            if (callee_fn && func_sym->decl && func_sym->decl->type == DECL_FN) {
                FnDecl *ast_fn = func_sym->decl->fn;
                
                if (ast_fn->generic_params.len > 0) { 
                    TypeRef *concrete_type = expr->call.args.data[0]->resolved_type;
                    
                    String placeholder_name = ast_fn->generic_params.data[0].name; 
                    
                    String mangled_name = string_make(format("%s_%s", 
                        callee_fn->symbol->name.data, 
                        type_to_string(concrete_type).data));
                    
                    HirFnDecl *instantiated_fn = lookup_monomorphized_fn(mod, mangled_name);
                    
                    if (!instantiated_fn) {
                        instantiated_fn = arena_calloc(ctx->arena, sizeof(HirFnDecl));
                        instantiated_fn->symbol = create_symbol(ctx, mangled_name);
                        instantiated_fn->symbol->decl = func_sym->decl;
                        instantiated_fn->ret_type = instantiate_type(ctx, callee_fn->ret_type, placeholder_name, concrete_type);
                        
                        instantiated_fn->body = clone_hir_stmt(ctx, callee_fn->body, placeholder_name, concrete_type);
                        
                        hirfndecls_array_push(&mod->functions, instantiated_fn);
                    }
                    
                    callee_expr->var.symbol = instantiated_fn->symbol;
                }
            }
        }
        
        for (size_t i = 0; i < expr->call.args.len; i++) {
            hir_scan_for_generics_expr(ctx, mod, expr->call.args.data[i]);
        }
    }
}

void hir_pass_monomorphise(Analyser *ctx, HirModule *mod) {
    for (size_t i = 0; i < mod->functions.len; i++) {
        HirFnDecl *fn = mod->functions.data[i];
        if (fn->body) {
            hir_scan_for_generics_expr(ctx, mod, fn->body->expr); 
        }
    }
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
            printf("%ld", lit._int);
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
            print_hir_expr(expr->call.callee); 
            printf("(");
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
        case HIR_EXPR_INIT:
            printf("{");
            for (size_t i = 0; i < expr->init_list.fields.len; i++) {
                if (i > 0) printf(", ");
                HirInitField *field = &expr->init_list.fields.data[i];
                if (field->field_name.has_value) {
                    printf("%.*s: ", string_fmt(field->field_name.value));
                }
                print_hir_expr(field->value);
            }
            printf("}");
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