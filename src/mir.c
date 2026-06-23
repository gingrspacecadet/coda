#include "hir.h"
#include "mir.h"
#include <stdio.h>

static size_t intern_string(MirBuilder *ctx, String str) {
    for (size_t i = 0; i < ctx->strings.len; i++) {
        if (string_eq(ctx->strings.data[i], str)) {
            return i;
        }
    }

    size_t id = ctx->strings.len;
    string_array_push(&ctx->strings, str);
    return id;
}

static Symbol *lookup_symbol(MirBuilder *ctx, String name) {
    Scope *scope = ctx->global_scope;

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

MirBlock *new_block(MirBuilder *ctx) {
    MirBlock *b = arena_calloc(ctx->arena, sizeof(MirBlock));
    b->id = ctx->block_counter++;
    b->visited = false;
    return b;
}

void terminate_and_link(MirBuilder *ctx, MirBlock *next) {
    if (!ctx->current_block) return;

    if (!ctx->current_block->succ_true) {
        ctx->current_block->succ_true = next;
    }
    ctx->current_block = next;
}

uint32_t make_label(MirBuilder *ctx) {
    return ctx->label_counter++;
}

void emit_label(MirBuilder *ctx, uint32_t label_id) {
    MirInstr *instr = arena_calloc(ctx->arena, sizeof(MirInstr));
    instr->op = MIR_OP_LABEL;
    instr->label_id = label_id;


    if (!ctx->current_block->first) {
        ctx->current_block->first = instr;
        ctx->current_block->last = instr;
    } else {
        instr->prev = ctx->current_block->last;
        ctx->current_block->last->next = instr;
        ctx->current_block->last = instr;
    }
}

MirInstr *emit(MirBuilder *ctx, MirOp type, MirOperand result, MirOperand lhs, MirOperand rhs) {
    MirInstr *instr = arena_calloc(ctx->arena, sizeof(MirInstr));
    instr->op = type;
    instr->result = result;
    instr->lhs = lhs;
    instr->rhs = rhs;

    if (!ctx->current_block->first) {
        ctx->current_block->first = instr;
        ctx->current_block->last = instr;
    } else {
        instr->prev = ctx->current_block->last;
        ctx->current_block->last->next = instr;
        ctx->current_block->last = instr;
    }

    return instr;
}

MirOperand mir_lower_expr(MirBuilder *ctx, HirExpr *hir);

MirOperand mir_lower_lvalue(MirBuilder *ctx, HirExpr *hir) {
    switch (hir->type) {
        case HIR_EXPR_UNARY: {
            if (hir->unary.op == UOP_DEREF) {
                MirOperand base = mir_lower_expr(ctx, hir->unary.operand);
                MirOperand addr_tmp = make_temp(ctx, base.resolved_type);
                emit(ctx, MIR_OP_COPY, addr_tmp, base, null_op());
                return addr_tmp;
            }
            break;
        }
        case HIR_EXPR_VAR:
            return make_symbol(hir->var.symbol);
        case HIR_EXPR_FIELD_OFFSET: {
            MirOperand base_addr = mir_lower_lvalue(ctx, hir->field_offset.base);

            if (base_addr.type == 0) {
                base_addr = mir_lower_expr(ctx, hir->field_offset.base);
            }

            if (base_addr.type == MIR_VAL_MEM) {
                base_addr.offset += hir->field_offset.byte_offset;
                return base_addr;
            }

            MirOperand field_mem = {0};
            field_mem.type = MIR_VAL_MEM;
            field_mem.resolved_type = hir->resolved_type;
            field_mem.offset = hir->field_offset.byte_offset;

            if (base_addr.type == MIR_VAL_SYMBOL) {
                field_mem.base_symbol = base_addr.symbol;
            } else if (base_addr.type == MIR_VAL_TEMP) {
                field_mem.base_temp = base_addr.temp;
            } else {
                // Fallback: compute the address via pointer arithmetic.
                Literal offset_lit = {
                    .type = LITERAL_INT,
                    ._int = hir->field_offset.byte_offset
                };
                MirOperand offset = make_literal(offset_lit, lookup_symbol(ctx, string_make("int"))->type);
                MirOperand field_addr = make_temp(ctx, hir->resolved_type->pointer.pointee);
                emit(ctx, MIR_OP_ADD, field_addr, base_addr, offset);
                return field_addr;
            }

            return field_mem;
        }
        case HIR_EXPR_ARRAY_INDEX: {
            MirOperand base_addr = mir_lower_lvalue(ctx, hir->array_index.base);
            MirOperand index = mir_lower_expr(ctx, hir->array_index.index);

            Literal size_lit = {
                .type = LITERAL_INT,
                ._int = hir->array_index.elem_size
            };
            MirOperand elem_size = make_literal(size_lit, lookup_symbol(ctx, string_make("int"))->type);

            MirOperand offset = make_temp(ctx, lookup_symbol(ctx, string_make("int"))->type);
            emit(ctx, MIR_OP_MUL, offset, index, elem_size);

            MirOperand element_addr = make_temp(ctx, hir->resolved_type);
            emit(ctx, MIR_OP_ADD, element_addr, base_addr, offset);

            return element_addr;
        }
        default: {
            return (MirOperand){0};
        }
    }
}

static void mir_lower_init_expr(MirBuilder *ctx, HirExpr *expr, MirOperand target, TypeRef *target_type);

MirOperand mir_lower_expr(MirBuilder *ctx, HirExpr *hir) {
    if (!hir) return (MirOperand){};
    switch (hir->type) {
        case HIR_EXPR_LIT: {
            MirOperand op = make_literal(hir->literal, hir->resolved_type);
            
            if (op.lit.type == LITERAL_STRING) {
                op.lit.str_id = intern_string(ctx, op.lit.string);
            }

            return op;
        }

        case HIR_EXPR_UNARY: {
            if (hir->unary.op == UOP_ADDR) {
                MirOperand operand = mir_lower_lvalue(ctx, hir->unary.operand);
                MirOperand result = make_temp(ctx, hir->resolved_type);
                emit(ctx, MIR_OP_ADDR, result, operand, null_op());
                return result;
            }

            MirOperand operand = mir_lower_expr(ctx, hir->unary.operand);
            MirOperand result = make_temp(ctx, hir->resolved_type);
            MirOp op;

            switch (hir->unary.op) {
                case UOP_NEG: op = MIR_OP_NEG; break;
                case UOP_NOT: op = MIR_OP_NOT; break;
                case UOP_DEREF:
                    emit(ctx, MIR_OP_LOAD, result, operand, null_op());
                    return result;
                default:
                    op = MIR_OP_COPY;
                    break;
            }

            emit(ctx, op, result, operand, null_op());
            return result;
        }
        case HIR_EXPR_BINARY: {
            if (hir->binary.op == BINOP_ASSIGN || hir->binary.op == BINOP_ADD_ASSIGN) {
                MirOperand target = mir_lower_lvalue(ctx, hir->binary.left);
                MirOperand rhs = mir_lower_expr(ctx, hir->binary.right);
                MirOperand assigned = rhs;

                if (hir->binary.op == BINOP_ADD_ASSIGN) {
                    MirOperand lhs_value = mir_lower_expr(ctx, hir->binary.left);
                    assigned = make_temp(ctx, hir->resolved_type);
                    emit(ctx, MIR_OP_ADD, assigned, lhs_value, rhs);
                }

                if (target.type == MIR_VAL_SYMBOL) {
                    emit(ctx, MIR_OP_COPY, target, assigned, null_op());
                } else {
                    emit(ctx, MIR_OP_STORE, target, assigned, null_op());
                }
                return assigned;
            }

            MirOperand lhs = mir_lower_expr(ctx, hir->binary.left);
            MirOperand rhs = mir_lower_expr(ctx, hir->binary.right);

            MirOperand result = make_temp(ctx, hir->resolved_type);

            MirOp op;
            switch (hir->binary.op) {
                case BINOP_ADD: op = MIR_OP_ADD; break;
                case BINOP_SUB: op = MIR_OP_SUB; break;
                case BINOP_MUL: op = MIR_OP_MUL; break;
                case BINOP_DIV: op = MIR_OP_DIV; break;
                case BINOP_LT: op = MIR_OP_LT; break;
                case BINOP_LE: op = MIR_OP_LE; break;
                case BINOP_GT: op = MIR_OP_GT; break;
                case BINOP_GE: op = MIR_OP_GE; break;
                case BINOP_EQ: op = MIR_OP_EQ; break;
                case BINOP_NE: op = MIR_OP_NE; break;
                case BINOP_LOG_AND: op = MIR_OP_LOG_AND; break;
                case BINOP_LOG_OR: op = MIR_OP_LOG_OR; break;
                default: op = MIR_OP_COPY; break;
            }

            emit(ctx, op, result, lhs, rhs);
            return result;
        }
        case HIR_EXPR_CAST:
            return mir_lower_expr(ctx, hir->cast.expr);
        case HIR_EXPR_VAR:
        case HIR_EXPR_FIELD_OFFSET:
        case HIR_EXPR_ARRAY_INDEX: {
            MirOperand address = mir_lower_lvalue(ctx, hir);

            if (address.type == MIR_VAL_SYMBOL || address.type == MIR_VAL_MEM) {
                return address;
            }


            MirOperand value_tmp = make_temp(ctx, hir->resolved_type);
            emit(ctx, MIR_OP_LOAD, value_tmp, address, null_op());

            return value_tmp;
        }

        case HIR_EXPR_CALL: {
            MirOperand *args = arena_calloc(ctx->arena, sizeof(MirOperand) * hir->call.args.len);
            for (size_t i = 0; i < hir->call.args.len; i++) {
                args[i] = mir_lower_expr(ctx, hir->call.args.data[i]);
            }

            MirOperand callee = mir_lower_expr(ctx, hir->call.callee);

            MirOperand result = make_temp(ctx, hir->resolved_type);
            MirInstr *instr = emit(ctx, MIR_OP_CALL, result, callee, null_op());
            instr->call_args = args;
            instr->arg_count = hir->call.args.len;

            return result;
        }

        case HIR_EXPR_INIT: {
            MirOperand result = make_temp(ctx, hir->resolved_type);
            mir_lower_init_expr(ctx, hir, result, hir->resolved_type);
            return result;
        }
    }

    return (MirOperand){0};
}

static TypeRef *get_init_field_type(TypeRef *target_type, size_t index, string_optional name) {
    if (!target_type) return NULL;

    if (target_type->type == TYPEREF_ARRAY) {
        return target_type->array.elem;
    }

    if (target_type->type == TYPEREF_NAMED && target_type->type_symbol && target_type->type_symbol->decl) {
        if (target_type->type_symbol->decl->type == DECL_STRUCT) {
            StructDecl *str = target_type->type_symbol->decl->_struct;
            if (name.has_value) {
                for (size_t i = 0; i < str->members.len; i++) {
                    if (string_eq(str->members.data[i]->name, name.value)) {
                        return str->members.data[i]->type;
                    }
                }
            }
            if (index < str->members.len) {
                return str->members.data[index]->type;
            }
        }
        if (target_type->type_symbol->decl->type == DECL_UNION) {
            UnionDecl *un = target_type->type_symbol->decl->_union;
            if (name.has_value) {
                for (size_t i = 0; i < un->members.len; i++) {
                    if (string_eq(un->members.data[i]->name, name.value)) {
                        return un->members.data[i]->type;
                    }
                }
            }
            if (index < un->members.len) {
                return un->members.data[index]->type;
            }
        }
    }

    return NULL;
}

static size_t get_init_field_offset(TypeRef *target_type, size_t index, string_optional name) {
    if (!target_type) return 0;

    if (target_type->type == TYPEREF_ARRAY) {
        return get_type_size(target_type->array.elem) * index;
    }

    if (target_type->type == TYPEREF_POINTER && target_type->pointer.pointee) {
        target_type = target_type->pointer.pointee;
    }

    if (target_type->type == TYPEREF_NAMED && target_type->type_symbol && target_type->type_symbol->decl) {
        if (target_type->type_symbol->decl->type == DECL_STRUCT) {
            StructDecl *str = target_type->type_symbol->decl->_struct;
            
            if (name.has_value) {
                for (size_t i = 0; i < str->members.len; i++) {
                    if (string_eq(str->members.data[i]->name, name.value)) {
                        if (str->field_offsets.len > i) {
                            return str->field_offsets.data[i];
                        }
                        return i * 8;
                    }
                }
            }
            
            if (index < str->members.len) {
                if (str->field_offsets.len > index) {
                    return str->field_offsets.data[index];
                }
                return index * 8;
            }
        }
        if (target_type->type_symbol->decl->type == DECL_UNION) {
            return 0;
        }
    }

    return 0;
}

static MirOperand make_init_field_addr(MirBuilder *ctx, MirOperand base, TypeRef *target_type, size_t index, string_optional name) {
    size_t offset = get_init_field_offset(target_type, index, name);
    
    // If base is a symbol or memory location, create a MIR_VAL_MEM directly
    if (base.type == MIR_VAL_SYMBOL) {
        MirOperand field_mem = {0};
        field_mem.type = MIR_VAL_MEM;
        field_mem.base_symbol = base.symbol;
        field_mem.offset = offset;
        field_mem.resolved_type = get_init_field_type(target_type, index, name);
        return field_mem;
    } else if (base.type == MIR_VAL_TEMP) {
        MirOperand field_mem = {0};
        field_mem.type = MIR_VAL_MEM;
        field_mem.base_temp = base.temp;
        field_mem.offset = offset;
        field_mem.resolved_type = get_init_field_type(target_type, index, name);
        return field_mem;
    } else if (base.type == MIR_VAL_MEM) {
        MirOperand field_mem = {0};
        field_mem.type = MIR_VAL_MEM;
        field_mem.base_symbol = base.base_symbol;
        field_mem.base_temp = base.base_temp;
        field_mem.offset = base.offset + offset;
        field_mem.resolved_type = get_init_field_type(target_type, index, name);
        return field_mem;
    }
    
    // Fallback: compute the address via pointer arithmetic
    Literal offset_lit = {.type = LITERAL_INT, ._int = offset};
    MirOperand offset_op = make_literal(offset_lit, lookup_symbol(ctx, string_make("int"))->type);
    TypeRef *field_type = get_init_field_type(target_type, index, name);
    MirOperand addr = make_temp(ctx, field_type);
    emit(ctx, MIR_OP_ADD, addr, base, offset_op);
    return addr;
}

static void mir_lower_init_expr(MirBuilder *ctx, HirExpr *expr, MirOperand target, TypeRef *target_type) {
    if (!expr || expr->type != HIR_EXPR_INIT) return;

    for (size_t i = 0; i < expr->init_list.fields.len; i++) {
        HirInitField *field = &expr->init_list.fields.data[i];
        TypeRef *field_type = get_init_field_type(target_type, i, field->field_name);
        MirOperand field_target = make_init_field_addr(ctx, target, target_type, i, field->field_name);

        if (field->value && field->value->type == HIR_EXPR_INIT) {
            mir_lower_init_expr(ctx, field->value, field_target, field_type);
        } else {
            MirOperand value = mir_lower_expr(ctx, field->value);
            if (field_target.type == MIR_VAL_SYMBOL) {
                emit(ctx, MIR_OP_COPY, field_target, value, null_op());
            } else {
                emit(ctx, MIR_OP_STORE, field_target, value, null_op());
            }
        }
    }
}

void mir_lower_stmt(MirBuilder *ctx, HirStmt *hir) {
    switch(hir->type) {
        case HIR_STMT_ASSIGN: {
            MirOperand target = mir_lower_lvalue(ctx, hir->assign.target);

            if (hir->assign.value && hir->assign.value->type == HIR_EXPR_INIT) {
                mir_lower_init_expr(ctx, hir->assign.value, target, hir->assign.target->resolved_type);
                break;
            }

            MirOperand value = mir_lower_expr(ctx, hir->assign.value);
            if (target.type == MIR_VAL_SYMBOL) {
                emit(ctx, MIR_OP_COPY, target, value, null_op());
            } else {
                emit(ctx, MIR_OP_STORE, target, value, null_op());
            }
            break;
        }

        case HIR_STMT_IF: {
            MirBlock *then_block = new_block(ctx);
            MirBlock *else_block = new_block(ctx);
            MirBlock *merge_block = new_block(ctx);

            MirOperand cond = mir_lower_expr(ctx, hir->_if.cond);
            emit(ctx, MIR_OP_BRANCH_FALSE, null_op(), cond, null_op())->label_id = else_block->id;
            ctx->current_block->succ_true = then_block;
            ctx->current_block->succ_false = else_block;

            ctx->current_block = then_block;
            mir_lower_stmt(ctx, hir->_if.then_block);
            if (ctx->current_block) {
                emit(ctx, MIR_OP_JMP, null_op(), null_op(), null_op())->label_id = merge_block->id;
                ctx->current_block->succ_true = merge_block;
            }

            ctx->current_block = else_block;
            if (hir->_if.else_block) {
                mir_lower_stmt(ctx, hir->_if.else_block);
                if (ctx->current_block) {
                    emit(ctx, MIR_OP_JMP, null_op(), null_op(), null_op())->label_id = merge_block->id;
                    ctx->current_block->succ_true = merge_block;
                }
            } else {
                ctx->current_block->succ_true = merge_block;
            }

            ctx->current_block = merge_block;
            break;
        }

        case HIR_STMT_WHILE: {
            MirBlock *cond_block = new_block(ctx);
            MirBlock *body_block = new_block(ctx);
            MirBlock *exit_block = new_block(ctx);

            terminate_and_link(ctx, cond_block);

            ctx->current_block = cond_block;
            MirOperand cond = mir_lower_expr(ctx, hir->_while.cond);
            emit(ctx, MIR_OP_BRANCH_FALSE, null_op(), cond, null_op())->label_id = exit_block->id;
            cond_block->succ_true = body_block;
            cond_block->succ_false = exit_block;

            ctx->current_block = body_block;
            mir_lower_stmt(ctx, hir->_while.body);
            if (ctx->current_block) {
                emit(ctx, MIR_OP_JMP, null_op(), null_op(), null_op())->label_id = cond_block->id;
                ctx->current_block->succ_true = cond_block;
            }

            ctx->current_block = exit_block;
            break;
        }

        case HIR_STMT_RETURN: {
            MirOperand val = null_op();
            if (hir->_return.value) {
                val = mir_lower_expr(ctx, hir->_return.value);
            }
            emit(ctx, MIR_OP_RET, null_op(), val, null_op());

            ctx->current_block = NULL;
            break;
        }

        case HIR_STMT_EXPR: {
            mir_lower_expr(ctx, hir->expr);
            break;
        }

        // TODO: the rest of these

        case HIR_STMT_BLOCK: {
            for (size_t i = 0; i < hir->block.stmts.len; i++) {
                mir_lower_stmt(ctx, hir->block.stmts.data[i]);
            }
            break;
        }

        default: {
            return;
        }
    }
}

MirFunction *mir_lower_fn(MirBuilder *ctx, HirFnDecl *hir_fn) {
    MirFunction *mir_fn = arena_calloc(ctx->arena, sizeof(MirFunction));
    mir_fn->params = hir_fn->params;
    mir_fn->locals = hir_fn->locals;
    mir_fn->symbol = hir_fn->symbol;
    mir_fn->is_export = hir_fn->is_export;
    mir_fn->ret_type = hir_fn->ret_type;
    ctx->temp_counter = 0;

    MirBlock *entry = new_block(ctx);
    ctx->current_block = entry;
    mir_fn->entry_block = entry;

    if (hir_fn->body) {
        mir_lower_stmt(ctx, hir_fn->body);
    }

    if (ctx->current_block) {
        emit(ctx, MIR_OP_RET, null_op(), null_op(), null_op());
    }

    mir_fn->temp_count = ctx->temp_counter;

    return mir_fn;
}

static void mir_lower_global(MirBuilder *builder, MirModule *mir_mod, HirVarDecl *hir_var) {
    MirVarDecl *mir_var = arena_calloc(builder->arena, sizeof(MirVarDecl));
    mir_var->symbol = hir_var->symbol;
    mir_var->type = hir_var->type;
    mir_var->is_export = hir_var->is_export;
    mir_var->init_vals = mirinitvals_array_init(builder->arena);

    if (hir_var->init) {
        if (hir_var->init->type == HIR_EXPR_INIT) {
            for (size_t i = 0; i < hir_var->init->init_list.fields.len; i++) {
                HirExpr *val = hir_var->init->init_list.fields.data[i].value;
                MirInitVal init_val = {0};

                if (val->type == HIR_EXPR_VAR) {
                    init_val.type = MIR_INIT_SYMBOL;
                    init_val.symbol_val = val->var.symbol;
                } else if (val->type == HIR_EXPR_LIT) {
                    if (val->resolved_type->type == TYPEREF_NAMED && 
                        string_eq(val->resolved_type->type_symbol->name, string_make("$null"))) {
                        init_val.type = MIR_INIT_ZERO;
                    } else {
                        init_val.type = MIR_INIT_INT;
                        init_val.int_val = val->literal._int;
                    }
                }
                mirinitvals_array_push(&mir_var->init_vals, init_val);
            }
        } else if (hir_var->init->type == HIR_EXPR_LIT) {
            MirInitVal init_val = {
                .type = MIR_INIT_INT,
                .int_val = hir_var->init->literal._int
            };
            mirinitvals_array_push(&mir_var->init_vals, init_val);
        }
    }

    mirvardecls_array_push(&mir_mod->globals, mir_var);
}

MirModule *mir_lower_module(MirBuilder *ctx, HirModule *hir) {
    MirModule *mod = arena_calloc(ctx->arena, sizeof(MirModule));
    mod->functions = mirfns_array_init(ctx->arena);
    mod->globals = mirvardecls_array_init(ctx->arena);

    for (size_t i = 0; i < hir->globals.len; i++) {
        mir_lower_global(ctx, mod, hir->globals.data[i]);
    }

    for (size_t i = 0; i < hir->functions.len; i++) {
        if (hir->functions.data[i]->is_extern) continue;
        mirfns_array_push(&mod->functions, mir_lower_fn(ctx, hir->functions.data[i]));
    }
    
    mod->strings = ctx->strings;

    return mod;
}

static void print_type(TypeRef *type) {
    if (!type) {
        printf("<unknown type>");
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
                Param p = type->fn.params.data[i];
                print_type(p.type);
            }
            printf(")");
            break;
        default:
            printf("<type>");
            break;
    }
}

static void print_operand(MirOperand op) {
    switch (op.type) {
        case MIR_VAL_LIT:
            if (op.lit.type == LITERAL_INT) {
                printf("%ld", op.lit._int);
            } else if (op.lit.type == LITERAL_STRING) {
                printf("\"%.*s (id:%zu)\"", string_fmt(op.lit.string), op.lit.str_id);
            } else if (op.lit.type == LITERAL_NULL) {
                printf("null");
            } else if (op.lit.type == LITERAL_BOOL) {
                printf("%s", op.lit._bool ? "true" : "false");
            } else if (op.lit.type == LITERAL_CHAR) {
                printf("%c", op.lit._char);
            } else {
                printf("<lit>");
            }
            break;
        case MIR_VAL_SYMBOL:
            printf("%.*s", string_fmt(op.symbol->name));
            break;
        case MIR_VAL_TEMP:
            printf("t%d", op.temp);
            break;
        case MIR_VAL_MEM:
            if (op.base_symbol) {
                printf("mem(sym=%.*s, off=%ld)", string_fmt(op.base_symbol->name), op.offset);
            } else {
                printf("mem(tmp=t%d, off=%ld)", op.base_temp, op.offset);
            }
            break;
        case MIR_VAL_LABEL:
            printf("L%d", op.label_id);
            break;
        case MIR_VAL_NONE: {
            break;
        }
    }
}

static void print_instr(MirInstr *instr) {
    if (instr->result.type != MIR_VAL_NONE &&
        (instr->result.type != MIR_VAL_LIT || instr->result.lit.type != LITERAL_INT || instr->result.lit._int != 0)) {
        print_type(instr->result.resolved_type);
        putc(' ', stdout);
        print_operand(instr->result);
        printf(" = ");
    }

    switch (instr->op) {
        case MIR_OP_ADD: printf("add "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_SUB: printf("sub "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_MUL: printf("mul "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_DIV: printf("div "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_LT: printf("lt "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_LE: printf("le "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_GT: printf("gt "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_GE: printf("ge "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_EQ: printf("eq "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_NE: printf("ne "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_LOG_AND: printf("and "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_LOG_OR: printf("or "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
        case MIR_OP_NEG: printf("neg "); print_operand(instr->lhs); break;
        case MIR_OP_NOT: printf("not "); print_operand(instr->lhs); break;
        case MIR_OP_COPY: printf("copy "); print_operand(instr->lhs); break;
        case MIR_OP_LOAD: printf("load "); print_operand(instr->lhs); break;
        case MIR_OP_STORE: printf("store "); print_operand(instr->result); printf(", "); print_operand(instr->lhs); break;
        case MIR_OP_JMP: printf("jmp L%d", instr->label_id); break;
        case MIR_OP_BRANCH: printf("br "); print_operand(instr->lhs); printf(", L%d", instr->label_id); break;
        case MIR_OP_BRANCH_FALSE: printf("brf "); print_operand(instr->lhs); printf(", L%d", instr->label_id); break;
        case MIR_OP_CALL: 
            printf("call "); print_operand(instr->lhs); printf("(");
            for (size_t i = 0; i < instr->arg_count; i++) {
                if (i > 0) printf(", ");
                print_operand(instr->call_args[i]);
            }
            printf(")");
            break;
        case MIR_OP_RET: printf("ret"); if (instr->lhs.type != MIR_VAL_LIT || instr->lhs.lit._int != 0) { printf(" "); print_operand(instr->lhs); } break;
        case MIR_OP_LABEL: printf("L%d:", instr->label_id); break;
        case MIR_OP_ADDR: printf("&"); print_operand(instr->lhs); break;
        default: printf("<unknown op %d>", instr->op);
    }
    printf("\n");
}

static void print_block(MirBlock *block) {
    printf("block %d:\n", block->id);
    for (MirInstr *instr = block->first; instr != NULL; instr = instr->next) {
        printf("  ");
        print_instr(instr);
    }
    if (block->succ_true) printf("  succ_true: L%d\n", block->succ_true->id);
    if (block->succ_false) printf("  succ_false: L%d\n", block->succ_false->id);
    printf("\n");
}

void mir_pretty_print(MirModule *mod) {
    for (size_t i = 0; i < mod->functions.len; i++) {
        MirFunction *fn = mod->functions.data[i];
        printf("function ");
        print_type(fn->symbol->decl->fn->ret_type);
        printf(" %.*s(", string_fmt(fn->symbol->name));
        
        if (fn->symbol->type && fn->symbol->type->type == TYPEREF_FN) {
            for (size_t j = 0; j < fn->symbol->type->fn.params.len; j++) {
                if (j > 0) printf(", ");
                Param p = fn->symbol->type->fn.params.data[j];
                print_type(p.type);
                putc(' ', stdout);
                printf("%.*s", string_fmt(p.name));
            }
        }
        printf("):\n");
        MirBlock *blocks[1024];
        int block_count = 0;
        MirBlock *stack[1024];
        int stack_top = 0;
        stack[stack_top++] = fn->entry_block;
        fn->entry_block->visited = true;
        blocks[block_count++] = fn->entry_block;

        while (stack_top > 0) {
            MirBlock *block = stack[--stack_top];
            if (block->succ_true && !block->succ_true->visited) {
                block->succ_true->visited = true;
                stack[stack_top++] = block->succ_true;
                blocks[block_count++] = block->succ_true;
            }
            if (block->succ_false && !block->succ_false->visited) {
                block->succ_false->visited = true;
                stack[stack_top++] = block->succ_false;
                blocks[block_count++] = block->succ_false;
            }
        }

        for (int j = 0; j < block_count; j++) {
            print_block(blocks[j]);
        }

        for (int j = 0; j < block_count; j++) {
            blocks[j]->visited = false;
        }
    }
}