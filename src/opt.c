#include "opt.h"

void opt_constant_folding(MirFunction *fn) {
    for (MirBlock *block = fn->entry_block; block != NULL; block = block->succ_true) {
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
    }
}