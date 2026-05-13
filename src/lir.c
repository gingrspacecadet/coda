#include "lir.h"
#include "sema.h"
#include "mir.h"

LirOperand lower_operand(MirOperand mir_op) {
    LirOperand lir_op = {0};
    lir_op.resolved_type = mir_op.resolved_type;

    switch (mir_op.type) {
        case MIR_VAL_LIT:   //TODO: mroe literals
            lir_op.type = LIR_IMM;
            lir_op.imm = mir_op.lit._int; 
            break;
            
        case MIR_VAL_TEMP:
            lir_op.type = LIR_REG_VIRTUAL;
            lir_op.vreg = mir_op.temp;
            break;
            
        case MIR_VAL_SYMBOL:
            lir_op.type = LIR_REG_VIRTUAL;
            lir_op.vreg = mir_op.symbol->vreg;
            break;
            
        case MIR_VAL_LABEL:
            lir_op.type = LIR_IMM;
            lir_op.imm = mir_op.label_id;
            break;
    }
    return lir_op;
}

LirInstr *emit_lir(LirFunction *fn, MirBuilder *ctx, LirOpcode op, LirOperand dest, LirOperand src) {
    LirInstr *instr = arena_calloc(ctx->arena, sizeof(LirInstr));
    instr->opcode = op;
    instr->dest = dest;
    instr->src = src;
    instr->cond = COND_NONE;

    // Append to flat list
    if (!fn->first) {
        fn->first = instr;
        fn->last = instr;
    } else {
        instr->prev = fn->last;
        fn->last->next = instr;
        fn->last = instr;
    }
    return instr;
}

LirFunction *lir_lower_fn(MirBuilder *ctx, MirFunction *mir_fn) {
    LirFunction *lir_fn = arena_calloc(ctx->arena, sizeof(LirFunction));
    // TODO: vreg setup

    MirBlock *blocks[1024];
    int block_count = 0;
    MirBlock *stack[1024];
    int stack_top = 0;

    stack[stack_top++] = mir_fn->entry_block;
    mir_fn->entry_block->visited = true;
    blocks[block_count++] = mir_fn->entry_block;

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

    for (int i = 0; i < block_count; i++) {
        MirBlock *block = blocks[i];
        block->visited = false;

        LirOperand label_op = { .type = LIR_IMM, .imm = block->id };
        emit_lir(lir_fn, ctx, LIR_LABEL, label_op, (LirOperand){0});


        for (MirInstr *mir = block->first; mir != NULL; mir = mir->next) {
            switch (mir->op) {
                case MIR_OP_ADD:
                case MIR_OP_SUB:
                case MIR_OP_MUL:
                case MIR_OP_DIV: {
                    LirOperand dest = lower_operand(mir->result);
                    LirOperand lhs = lower_operand(mir->lhs);
                    LirOperand rhs = lower_operand(mir->rhs);

                    emit_lir(lir_fn, ctx, LIR_MOV, dest, lhs);
                    LirOpcode op = LIR_ADD;
                    if (mir->op == MIR_OP_SUB) op = LIR_SUB;
                    if (mir->op == MIR_OP_MUL) op = LIR_IMUL;
                    if (mir->op == MIR_OP_DIV) op = LIR_IDIV;

                    emit_lir(lir_fn, ctx, op, dest, rhs);
                    break;
                }
                case MIR_OP_COPY: {
                    emit_lir(lir_fn, ctx, LIR_MOV, lower_operand(mir->result), lower_operand(mir->lhs));
                    break;
                }
                case MIR_OP_JMP: {
                    LirOperand target = { .type = LIR_IMM, .imm = mir->label_id };
                    emit_lir(lir_fn, ctx, LIR_JMP, target, (LirOperand){0});
                    break;
                }
                case MIR_OP_BRANCH_FALSE: {
                    LirOperand cond = lower_operand(mir->lhs);
                    LirOperand zero = { .type = LIR_IMM, .imm = 0 };
                    emit_lir(lir_fn, ctx, LIR_CMP, cond, zero);
                    
                    LirOperand target = { .type = LIR_IMM, .imm = mir->label_id };
                    LirInstr *jmp = emit_lir(lir_fn, ctx, LIR_JCC, target, (LirOperand){0});
                    jmp->cond = COND_E; 
                    break;
                }

                case MIR_OP_RET: {
                    if (mir->lhs.type != 0) {
                        LirOperand ret_val = lower_operand(mir->lhs);
                        LirOperand rax = { .type = LIR_REG_PHYSICAL, .preg = REG_RAX };
                        emit_lir(lir_fn, ctx, LIR_MOV, rax, ret_val);
                    }
                    emit_lir(lir_fn, ctx, LIR_RET, (LirOperand){0}, (LirOperand){0});
                    break;
                }

                // TODO: op_store, load, call etc
            }
        }
    }

    return lir_fn;
}