#include "hir.h"
#include "mir.h"
#include <stdio.h>

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
        case HIR_EXPR_VAR:
            return make_symbol(hir->var.symbol);
        case HIR_EXPR_FIELD_OFFSET: {
            MirOperand base_addr = mir_lower_lvalue(ctx, hir->field_offset.base);

            Literal offset_lit = {
                .type = LITERAL_INT,
                ._int = hir->field_offset.byte_offset
            };
            MirOperand offset = make_literal(offset_lit, lookup_symbol(ctx, string_make("int"))->type);
            MirOperand field_addr = make_temp(ctx, hir->resolved_type->pointer.pointee);
            emit(ctx, MIR_OP_ADD, field_addr, base_addr, offset);

            return field_addr;
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
    }
}

MirOperand mir_lower_expr(MirBuilder *ctx, HirExpr *hir) {
    if (!hir) return (MirOperand){};
    switch (hir->type) {
        case HIR_EXPR_LIT:
            return make_literal(hir->literal, hir->resolved_type);
        case HIR_EXPR_BINARY: {
            MirOperand lhs = mir_lower_expr(ctx, hir->binary.left);
            MirOperand rhs = mir_lower_expr(ctx, hir->binary.right);

            MirOperand result = make_temp(ctx, hir->resolved_type);

            MirOp op;
            switch (hir->binary.op) {   //TODO: more
                case BINOP_ADD: op = MIR_OP_ADD; break;
                case BINOP_SUB: op = MIR_OP_SUB; break;
                case BINOP_MUL: op = MIR_OP_MUL; break;
                case BINOP_DIV: op = MIR_OP_DIV; break;
            }

            emit(ctx, op, result, lhs, rhs);
            return result;
        }
        case HIR_EXPR_VAR:
        case HIR_EXPR_FIELD_OFFSET:
        case HIR_EXPR_ARRAY_INDEX: {
            MirOperand address = mir_lower_lvalue(ctx, hir);

            if (address.type == MIR_VAL_SYMBOL) {
                return address;
            }


            MirOperand value_tmp = make_temp(ctx, hir->resolved_type);
            emit(ctx, MIR_OP_LOAD, value_tmp, address, null_op());

            return value_tmp;
        }

        case HIR_EXPR_CALL: {
            MirOperand *args = arena_alloc(ctx->arena, sizeof(MirOperand) * hir->call.args.len);
            for (size_t i = 0; i < hir->call.args.len; i++) {
                args[i] = mir_lower_expr(ctx, hir->call.args.data[i]);
            }

            MirOperand result = make_temp(ctx, hir->resolved_type);

            MirInstr *instr = emit(ctx, MIR_OP_CALL, result, make_symbol(hir->call.callee), null_op());
            instr->call_args = args;
            instr->arg_count = hir->call.args.len;

            return result;
        }
    }
}

void mir_lower_stmt(MirBuilder *ctx, HirStmt *hir) {
    switch(hir->type) {
        case HIR_STMT_ASSIGN: {
            MirOperand target = mir_lower_lvalue(ctx, hir->assign.target);
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
    }
}

MirFunction *mir_lower_fn(MirBuilder *ctx, HirFnDecl *hir_fn) {
    MirFunction *mir_fn = arena_calloc(ctx->arena, sizeof(MirFunction));
    mir_fn->symbol = hir_fn->symbol;
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

    return mir_fn;
}

MirModule *mir_lower_module(MirBuilder *ctx, HirModule *hir) {
    MirModule *mod = arena_calloc(ctx->arena, sizeof(MirModule));
    mod->functions = mirfns_array_init();
    for (size_t i = 0; i < hir->functions.len; i++) {
        mirfns_array_push(&mod->functions, mir_lower_fn(ctx, hir->functions.data[i]));
    }

    return mod;
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
                printf("%lld", op.lit._int);
            } else if (op.lit.type == LITERAL_STRING) {
                printf("\"%.*s\"", (int)op.lit.string.length, op.lit.string.data);
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
            printf("%.*s", (int)op.symbol->name.length, op.symbol->name.data);
            break;
        case MIR_VAL_TEMP:
            printf("t%d", op.temp);
            break;
        case MIR_VAL_LABEL:
            printf("L%d", op.label_id);
            break;
    }
}

static void print_instr(MirInstr *instr) {
    if (instr->result.type != MIR_VAL_LIT || instr->result.lit.type != LITERAL_INT || instr->result.lit._int != 0) {
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
        case MIR_OP_NEG: printf("neg "); print_operand(instr->lhs); break;
        case MIR_OP_NOT: printf("not "); print_operand(instr->lhs); break;
        case MIR_OP_COPY: printf("copy "); print_operand(instr->lhs); break;
        case MIR_OP_LOAD: printf("load "); print_operand(instr->lhs); break;
        case MIR_OP_STORE: printf("store "); print_operand(instr->lhs); printf(", "); print_operand(instr->rhs); break;
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
        default: printf("<unknown op>");
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
        printf("function %.*s(", (int)fn->symbol->name.length, fn->symbol->name.data);
        
        if (fn->symbol->type && fn->symbol->type->type == TYPEREF_FN) {
            for (size_t j = 0; j < fn->symbol->type->fn.params.len; j++) {
                if (j > 0) printf(", ");
                Param p = fn->symbol->type->fn.params.data[j];
                print_type(p.type);
                putc(' ', stdout);
                printf("%.*s", (int)p.name.length, p.name.data);
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