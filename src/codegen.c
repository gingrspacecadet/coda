#include "codegen.h"

static const char* phys_reg_names[] = {
    [REG_RAX] = "rax", 
    [REG_RCX] = "rcx", 
    [REG_RDX] = "rdx", 
    [REG_RBX] = "rbx", 
    [REG_RSP] = "rsp", 
    [REG_RBP] = "rbp", 
    [REG_RSI] = "rsi", 
    [REG_RDI] = "rdi",
    [REG_R8] = "r8", 
    [REG_R9] = "r9", 
    [REG_R10] = "r10", 
    [REG_R11] = "r11", 
    [REG_R12] = "r12", 
    [REG_R13] = "r13", 
    [REG_R14] = "r14", 
    [REG_R15] = "r15"
};

static int get_vreg_offset(uint32_t vreg_id) {
    return (vreg_id + 1) * 8;
}

static void emit_operand(FILE *out, LirOperand op, PhysReg fallback_reg) {
    static const char* reg_names[] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };
    switch (op.type) {
        case LIR_REG_PHYSICAL: {
            fprintf(out, "%s", reg_names[op.preg]);
            break;
        }

        case LIR_REG_VIRTUAL: {
            fprintf(out, "QWORD PTR [rbp - %d]", get_vreg_offset(op.vreg));
            break;
        }
        
        case LIR_IMM: {
            fprintf(out, "%lld", op.imm);
            break;
        }

        case LIR_MEM: {
            fprintf(out, "QWORD PTR [%s]", reg_names[fallback_reg]);
            break;
        }
    }
}

void codegen(FILE *out, LirFunction *fn, MirBuilder *ctx) {
    fprintf(out, ".intel_syntax noprefix\n");
    fprintf(out, ".global %.*s\n", string_fmt(fn->symbol->name));
    fprintf(out, "%.*s:\n", string_fmt(fn->symbol->name));

    /* Reserve stack for virtual registers and parameter slots (whichever is larger). */
    size_t param_count = 0;
    if (fn->symbol->type && fn->symbol->type->type == TYPEREF_FN) {
        param_count = fn->symbol->type->fn.params.len;
    }

    size_t required_vregs = fn->vreg_count > param_count ? fn->vreg_count : param_count;
    int total_stack_bytes = (int)(required_vregs * 8);
    if (total_stack_bytes % 16 != 0) {
        total_stack_bytes += 16 - (total_stack_bytes % 16);
    }

    fprintf(out, "    push rbp\n");
    fprintf(out, "    mov rbp, rsp\n");
    if (total_stack_bytes > 0) {
        fprintf(out, "    sub rsp, %d\n", total_stack_bytes);
    }

    if (fn->symbol->type && fn->symbol->type->type == TYPEREF_FN) {
        PhysReg arg_regs[] = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
        size_t param_count_local = fn->symbol->type->fn.params.len;
        for (size_t i = 0; i < param_count_local && i < sizeof(arg_regs) / sizeof(arg_regs[0]); i++) {
            int slot = get_vreg_offset((uint32_t)i);
            fprintf(out, "    mov QWORD PTR [rbp - %d], %s\n", slot, phys_reg_names[arg_regs[i]]);
        }
        // TODO: copy stack-passed args for param_count > 6
    }

    for (LirInstr *instr = fn->first; instr != NULL; instr = instr->next) {
        switch (instr->opcode) {
            case LIR_LABEL: {
                fprintf(out, ".L%lld:\n", instr->dest.imm);
                break;
            }
            case LIR_MOV: {
                if (instr->dest.type == LIR_REG_VIRTUAL && instr->src.type == LIR_REG_VIRTUAL) {
                    // TODO: when we allocate registers, something like r10 will become the scratch reg
                    fprintf(out, "    mov rax, "); emit_operand(out, instr->src, REG_RAX); fprintf(out, "\n");
                    fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_RAX); fprintf(out, ", rax\n");
                }
                else if (instr->src.type == LIR_MEM) {
                    fprintf(out, "    mov rbx, QWORD PTR [rbp - %d]\n", get_vreg_offset(instr->src.mem.base_vreg));
                    fprintf(out, "    mov rax, QWORD PTR [rbx]\n");
                    fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_RCX); fprintf(out, ", rax\n");
                }
                else if (instr->dest.type == LIR_MEM) {
                    fprintf(out, "    mov rbx, QWORD PTR [rbp - %d]\n", get_vreg_offset(instr->dest.mem.base_vreg));
                    fprintf(out, "    mov rax, "); emit_operand(out, instr->src, REG_RAX); fprintf(out, "\n");
                    fprintf(out, "    mov QWORD PTR [rbx], rax");
                }
                else {
                    fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_RAX);
                    fprintf(out, ", "); emit_operand(out, instr->src, REG_RCX); fprintf(out, "\n");
                }
                break;
            }

            case LIR_ADD:
            case LIR_SUB:
            case LIR_IMUL: {
                const char *op_str = (instr->opcode == LIR_ADD) ? "add" : (instr->opcode == LIR_SUB) ? "sub" : "imul";

                fprintf(out, "    mov rax, "); emit_operand(out, instr->dest, REG_RAX); fprintf(out, "\n");
                fprintf(out, "    %s rax, ", op_str); emit_operand(out, instr->src, REG_RCX); fprintf(out, "\n");
                fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_RAX); fprintf(out, ", rax\n");
                break;
            }

            case LIR_CQO: {
                fprintf(out, "    cqo\n");
                break;
            }

            case LIR_IDIV: {
                fprintf(out, "    idiv "); emit_operand(out, instr->dest, REG_RCX); fprintf(out, "\n");
                break;
            }

            case LIR_CMP: {
                fprintf(out, "    mov rax, "); emit_operand(out, instr->dest, REG_RAX); fprintf(out, "\n");
                fprintf(out, "    cmp rax, "); emit_operand(out, instr->src, REG_RCX); fprintf(out, "\n");
                break;
            }

            case LIR_SETCC: {
                static const char* setcc_strs[] = { "e", "ne", "l", "le", "g", "ge", "" };
                fprintf(out, "    set%s al\n", setcc_strs[instr->cond]);
                fprintf(out, "    movzx rax, al\n");
                fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_RAX); fprintf(out, ", rax\n");
                break;
            }

            case LIR_JMP: {
                fprintf(out, "    jmp .L%lld\n", instr->dest.imm);
                break;
            }

            case LIR_JCC: {
                static const char* jcc_strs[] = { "je", "jne", "jl", "jle", "jg", "jge", "" };
                fprintf(out, "    %s .L%lld\n", jcc_strs[instr->cond], instr->dest.imm);
                break;
            }

            case LIR_CALL: {
                if (instr->dest.type == LIR_GLOBAL) {
                    fprintf(out, "    call %.*s\n", string_fmt(instr->dest.symbol->name));
                } else {
                    fprintf(out, "    call "); emit_operand(out, instr->dest, REG_RAX); fprintf(out, "\n");
                }
                break;
            }

            case LIR_RET: {
                fprintf(out, "    mov rsp, rbp\n");
                fprintf(out, "    pop rbp\n");
                fprintf(out, "    ret\n");
                break;
            }
        }
    }

    if (ctx->strings.len != 0) {
        fprintf(out, "\n.section .rodata\n");
        for (size_t i = 0; i < ctx->strings.len; i++) {
            fprintf(out, ".Lstr%u:\n", ctx->strings.data[i].id);
            fprintf(out, "    .ascii \"%.*s\"\n", string_fmt(ctx->strings.data[i].value));
        }
    }
}