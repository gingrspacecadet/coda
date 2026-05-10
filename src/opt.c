#include "opt.h"

void opt_constant_folding(MirFunction *fn) {
    MirBlock *stack[1024]; // simple stack
    MirBlock *visited_blocks[1024];
    int stack_top = 0;
    int visited_count = 0;

    stack[stack_top++] = fn->entry_block;
    fn->entry_block->visited = true;
    visited_blocks[visited_count++] = fn->entry_block;

    while (stack_top > 0) {
        MirBlock *block = stack[--stack_top];
        for (MirInstr *instr = block->first; instr != NULL; instr = instr->next) {
            if (instr->lhs.type == MIR_VAL_LIT && instr->rhs.type == MIR_VAL_LIT) {
                if (instr->lhs.lit.type == LITERAL_INT && instr->rhs.lit.type == LITERAL_INT) {
                    int64_t left_val = instr->lhs.lit._int;
                    int64_t right_val = instr->rhs.lit._int;
                    int64_t result_val = 0;

                    switch (instr->op) {
                        case MIR_OP_ADD: result_val = left_val + right_val; break;
                        case MIR_OP_SUB: result_val = left_val - right_val; break;
                        case MIR_OP_MUL: result_val = left_val * right_val; break;
                        case MIR_OP_DIV:
                            if (right_val != 0) result_val = left_val / right_val;
                            break;
                        default: continue;
                    }

                    instr->op = MIR_OP_COPY;
                    instr->lhs = make_literal((Literal){.type = LITERAL_INT, ._int = result_val}, instr->lhs.resolved_type);
                    instr->rhs = null_op();
                }
            }
        }

        // Push successors
        if (block->succ_true && !block->succ_true->visited) {
            block->succ_true->visited = true;
            stack[stack_top++] = block->succ_true;
            visited_blocks[visited_count++] = block->succ_true;
        }
        if (block->succ_false && !block->succ_false->visited) {
            block->succ_false->visited = true;
            stack[stack_top++] = block->succ_false;
            visited_blocks[visited_count++] = block->succ_false;
        }
    }

    for (int i = 0; i < visited_count; i++) {
        visited_blocks[i]->visited = false;
    }
}