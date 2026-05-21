#include "lir.h"
#include "../../sema.h"
#include "../../mir.h"

LirOperand lower_operand(MirOperand mir_op) {
    LirOperand lir_op = {0};
    lir_op.resolved_type = mir_op.resolved_type;

    switch (mir_op.type) {
        case MIR_VAL_LIT:   //TODO: mroe literals
            switch (mir_op.lit.type) {
                case LITERAL_STRING:
                    lir_op.type = LIR_IMM;  // Will be replaced with string label in codegen
                    lir_op.string_const.str = mir_op.lit.string;
                    lir_op.string_const.id = 0;  // Will be assigned by codegen
                    break;
                default:
                    lir_op.type = LIR_IMM;
                    switch (mir_op.lit.type) {
                        case LITERAL_INT: lir_op.imm = mir_op.lit._int; break;
                        case LITERAL_BOOL: lir_op.imm = mir_op.lit._bool; break;
                        case LITERAL_CHAR: lir_op.imm = mir_op.lit._char; break;
                        default: lir_op.imm = mir_op.lit._int;
                    }
            }
            break;
            
        case MIR_VAL_TEMP:
            lir_op.type = LIR_REG_VIRTUAL;
            lir_op.vreg = mir_op.temp;
            break;
            
        case MIR_VAL_SYMBOL:
            if (mir_op.symbol->type && mir_op.symbol->type->type == TYPEREF_FN) {
                lir_op.type = LIR_GLOBAL;
                lir_op.symbol = mir_op.symbol;
            } else {
                lir_op.type = LIR_REG_VIRTUAL;
                lir_op.vreg = mir_op.symbol->vreg;
            }
            break;

        case MIR_VAL_MEM:
            if (mir_op.base_symbol) {
                lir_op.type = LIR_STACK;
                lir_op.mem.base_vreg = mir_op.base_symbol->vreg;
                lir_op.mem.offset = mir_op.offset;
            } else {
                lir_op.type = LIR_MEM;
                lir_op.mem.base_vreg = mir_op.base_temp;
                lir_op.mem.offset = mir_op.offset;
            }
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
    lir_fn->symbol = mir_fn->symbol;
    
    ctx->temp_counter = 0;
    uint32_t current_vreg = 0;

    for (size_t i = 0; i < mir_fn->params.len; i++) {
        Symbol *param_sym = mir_fn->params.data[i];
        param_sym->vreg = current_vreg++;
    }

    for (size_t i = 0; i < mir_fn->locals.len; i++) {
        Symbol *local_sym = mir_fn->locals.data[i];
        local_sym->vreg = current_vreg++;
    }

    lir_fn->vreg_count = current_vreg;

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

                    if (mir->op == MIR_OP_DIV) {
                        LirOperand rax = { .type = LIR_REG_PHYSICAL, .preg = REG_RAX };
                        emit_lir(lir_fn, ctx, LIR_MOV, rax, lhs);
                        emit_lir(lir_fn, ctx, LIR_CQO, (LirOperand){0}, (LirOperand){0});
                        emit_lir(lir_fn, ctx, LIR_IDIV, rhs, (LirOperand){0});
                        emit_lir(lir_fn, ctx, LIR_MOV, dest, rax);
                    } else {
                        emit_lir(lir_fn, ctx, LIR_MOV, dest, lhs);
                        LirOpcode op = LIR_ADD;
                        if (mir->op == MIR_OP_SUB) op = LIR_SUB;
                        if (mir->op == MIR_OP_MUL) op = LIR_IMUL;
                        if (mir->op == MIR_OP_DIV) op = LIR_IDIV;
    
                        emit_lir(lir_fn, ctx, op, dest, rhs);
                    }

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
                case MIR_OP_LT:
                case MIR_OP_LE:
                case MIR_OP_GT:
                case MIR_OP_GE:
                case MIR_OP_EQ:
                case MIR_OP_NE: {
                    LirOperand dest = lower_operand(mir->result);
                    LirOperand lhs = lower_operand(mir->lhs);
                    LirOperand rhs = lower_operand(mir->rhs);
                    emit_lir(lir_fn, ctx, LIR_CMP, lhs, rhs);
                    LirInstr *set = emit_lir(lir_fn, ctx, LIR_SETCC, dest, (LirOperand){0});
                    switch (mir->op) {
                        case MIR_OP_LT: set->cond = COND_L; break;
                        case MIR_OP_LE: set->cond = COND_LE; break;
                        case MIR_OP_GT: set->cond = COND_G; break;
                        case MIR_OP_GE: set->cond = COND_GE; break;
                        case MIR_OP_EQ: set->cond = COND_E; break;
                        case MIR_OP_NE: set->cond = COND_NE; break;
                        default: set->cond = COND_NONE; break;
                    }
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
                    if (mir->lhs.type != MIR_VAL_NONE) {
                        LirOperand ret_val = lower_operand(mir->lhs);
                        LirOperand rax = { .type = LIR_REG_PHYSICAL, .preg = REG_RAX };
                        emit_lir(lir_fn, ctx, LIR_MOV, rax, ret_val);
                    }
                    emit_lir(lir_fn, ctx, LIR_RET, (LirOperand){0}, (LirOperand){0});
                    break;
                }

                case MIR_OP_CALL: {
                    PhysReg arg_regs[] = { REG_RDI, REG_RSI, REG_RDX, REG_R8, REG_R9 };

                    for (size_t j = 0; j < mir->arg_count && j < 6; j++) {
                        LirOperand preg = { .type = LIR_REG_PHYSICAL, .preg = arg_regs[i] };
                        LirOperand arg_val = lower_operand(mir->call_args[j]);
                        emit_lir(lir_fn, ctx, LIR_MOV, preg, arg_val);
                    }
                    //TODO: if more than 6 args, push them to stack

                    LirOperand callee = lower_operand(mir->lhs);
                    emit_lir(lir_fn, ctx, LIR_CALL, callee, (LirOperand){0});

                    LirOperand rax = { .type = LIR_REG_PHYSICAL, .preg = REG_RAX };
                    LirOperand dest = lower_operand(mir->result);
                    emit_lir(lir_fn, ctx, LIR_MOV, dest, rax);
                    break;
                }

                case MIR_OP_LOAD: {
                    LirOperand dest = lower_operand(mir->result);
                    LirOperand ptr = lower_operand(mir->lhs);

                    if (ptr.type == LIR_REG_VIRTUAL || ptr.type == LIR_REG_PHYSICAL) {
                        ptr.type = LIR_MEM;
                        ptr.mem.base_vreg = ptr.vreg;
                        ptr.mem.offset = 0;
                    }

                    emit_lir(lir_fn, ctx, LIR_MOV, dest, ptr);
                    break;
                }

                case MIR_OP_STORE: {
                    LirOperand ptr = lower_operand(mir->result);
                    LirOperand val = lower_operand(mir->lhs);

                    if (ptr.type == LIR_REG_VIRTUAL || ptr.type == LIR_REG_PHYSICAL) {
                        ptr.type = LIR_MEM;
                        ptr.mem.base_vreg = ptr.vreg;
                        ptr.mem.offset = 0;
                    }

                    emit_lir(lir_fn, ctx, LIR_MOV, ptr, val);
                    break;
                }
            }
        }
    }

    return lir_fn;
}

// TODO: move all the printing stuffs into their own file

static const char* phys_reg_names[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "none"
};

static const char* lir_opcode_names[] = {
    "mov", "add", "sub", "imul", "idiv", "cmp", "setcc", "jmp", "jcc",
    "call", "ret", "label", "push", "pop", "cqo"
};

static const char* lir_cond_names[] = {
    "e", "ne", "l", "le", "g", "ge", ""
};

static void print_lir_operand(LirOperand op) {
    switch (op.type) {
        case LIR_REG_PHYSICAL:
            printf("%s", phys_reg_names[op.preg]);
            break;
        case LIR_REG_VIRTUAL:
            printf("v%u", op.vreg);
            break;
        case LIR_IMM:
            printf("%lld", op.imm);
            break;
        case LIR_MEM:
            if (op.mem.offset == 0) {
                printf("[v%u]", op.mem.base_vreg);
            } else if (op.mem.offset > 0) {
                printf("[v%u + %d]", op.mem.base_vreg, op.mem.offset);
            } else {
                printf("[v%u - %d]", op.mem.base_vreg, -op.mem.offset);
            }
            break;
        case LIR_STACK:
            if (op.mem.offset == 0) {
                printf("[rbp - v%u]", op.mem.base_vreg);
            } else if (op.mem.offset > 0) {
                printf("[rbp - v%u + %d]", op.mem.base_vreg, op.mem.offset);
            } else {
                printf("[rbp - v%u - %d]", op.mem.base_vreg, -op.mem.offset);
            }
            break;
    }
}

static void print_lir_instr(LirInstr *instr) {
    if (instr->opcode == LIR_LABEL) {
        printf("L%lld:\n", instr->dest.imm);
        return;
    }

    printf("  ");

    if (instr->opcode == LIR_JCC) {
        printf("j%s ", lir_cond_names[instr->cond]);
    } else {
        printf("%s ", lir_opcode_names[instr->opcode]);
    }

    switch (instr->opcode) {
        case LIR_RET:
        case LIR_CQO:
            break;

        case LIR_JMP:
        case LIR_JCC:
            printf("L%lld", instr->dest.imm);
            break;
        case LIR_SETCC:
            print_lir_operand(instr->dest);
            break;
        case LIR_CALL:
        case LIR_PUSH:
        case LIR_POP:
        case LIR_IDIV:
            print_lir_operand(instr->dest);
            break;

        default: 
            print_lir_operand(instr->dest);
            printf(", ");
            print_lir_operand(instr->src);
            break;
    }
    printf("\n");
}

void lir_pretty_print(LirFunction *fn) {
    printf("--- LIR for %.*s ---\n", string_fmt(fn->symbol->name));
    printf("vregs used: %u\n", fn->vreg_count);
    
    for (LirInstr *instr = fn->first; instr != NULL; instr = instr->next) {
        print_lir_instr(instr);
    }
    printf("-------------------\n\n");
}