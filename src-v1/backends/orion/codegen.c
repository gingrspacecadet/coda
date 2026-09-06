#include "codegen.h"
#include <string.h>

static const char* phys_reg_names[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "rfp", "rsp"
};

static int get_vreg_offset(uint32_t vreg_id) {
    return (vreg_id + 1) * 4;
}

// Collect all string constants from a function
void collect_string_constants(LirFunction *fn, string_const_array *string_consts) {
    for (LirInstr *instr = fn->first; instr != NULL; instr = instr->next) {
        if (instr->opcode == LIR_MOV) {
            if (instr->src.type == LIR_IMM && instr->src.string_const.str.length > 0) {
                // Check if this string constant already exists
                bool found = false;
                for (size_t i = 0; i < string_consts->len; i++) {
                    if (string_eq(string_consts->data[i].str, instr->src.string_const.str)) {
                        instr->src.string_const.id = i;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    StringConstant sc = { .str = instr->src.string_const.str, .id = string_consts->len };
                    string_const_array_push(string_consts, sc);
                    instr->src.string_const.id = string_consts->len - 1;
                }
            }
            if (instr->dest.type == LIR_IMM && instr->dest.string_const.str.length > 0) {
                // Check if this string constant already exists
                bool found = false;
                for (size_t i = 0; i < string_consts->len; i++) {
                    if (string_eq(string_consts->data[i].str, instr->dest.string_const.str)) {
                        instr->dest.string_const.id = i;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    StringConstant sc = {
                        .str = instr->dest.string_const.str,
                        .id = string_consts->len
                    };
                    string_const_array_push(string_consts, sc);
                    instr->dest.string_const.id = string_consts->len - 1;
                }
            }
        }
    }
}

static void emit_operand(FILE *out, LirOperand op, PhysReg fallback_reg, string_const_array *string_consts) {
    switch (op.type) {
        case LIR_REG_PHYSICAL: {
            fprintf(out, "%s", phys_reg_names[op.preg]);
            break;
        }

        case LIR_REG_VIRTUAL: {
            fprintf(out, "[rfp - #%d]", get_vreg_offset(op.vreg));
            break;
        }

        case LIR_STACK: {
            int64_t base_offset = get_vreg_offset(op.mem.base_vreg);
            if (op.mem.offset == 0) {
                fprintf(out, "[rfp - #%ld]", base_offset);
            } else if (op.mem.offset > 0) {
                fprintf(out, "[rfp - #%ld + #%d]", base_offset, op.mem.offset);
            } else {
                fprintf(out, "[rfp - #%ld - #%d]", base_offset, -op.mem.offset);
            }
            break;
        }
        
        case LIR_IMM: {
            // Check if this is a string constant
            if (op.string_const.str.length > 0) {
                fprintf(out, ".LC%u", op.string_const.id);
            } else {
                fprintf(out, "#%ld", op.imm);
            }
            break;
        }

        case LIR_MEM: {
            fprintf(out, "[%s]", phys_reg_names[fallback_reg]);
            break;
        }
    }
}

void codegen(FILE *out, LirFunction *fn, string_const_array *string_consts) {
    fprintf(out, "%.*s:\n", string_fmt(fn->symbol->name));

    fprintf(out, "    push rfp\n");
    fprintf(out, "    mov rfp, rsp\n");

    if (fn->symbol->type && fn->symbol->type->type == TYPEREF_FN) {
        PhysReg arg_regs[] = { REG_R0, REG_R1, REG_R2, REG_R3 };
        size_t param_count = fn->symbol->type->fn.params.len;
        for (size_t i = 0; i < param_count && i < sizeof(arg_regs) / sizeof(arg_regs[0]); i++) {
            int slot = get_vreg_offset((uint32_t)i);
            fprintf(out, "    mov [rfp - #%d], %s\n", slot, phys_reg_names[arg_regs[i]]);
        }
        // TODO: copy stack-passed args for param_count > 6
    }

    for (LirInstr *instr = fn->first; instr != NULL; instr = instr->next) {
        switch (instr->opcode) {
            case LIR_LABEL: {
                fprintf(out, ".L%ld:\n", instr->dest.imm);
                break;
            }
            case LIR_MOV: {
                // printf("#%d #%d\n", instr->src.type, instr->dest.type);
                if (instr->dest.type == LIR_REG_VIRTUAL && instr->src.type == LIR_REG_VIRTUAL) {
                    // TODO: when we allocate registers, something like r10 will become the scratch reg
                    fprintf(out, "    mov r0, "); emit_operand(out, instr->src, REG_R0, string_consts); fprintf(out, "\n");
                    fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_R12, string_consts); fprintf(out, ", r0\n");
                }
                else if (instr->src.type == LIR_MEM) {
                    fprintf(out, "    mov r3, [rfp - #%d]\n", get_vreg_offset(instr->src.mem.base_vreg));
                    if (instr->src.mem.offset == 0) {
                        fprintf(out, "    mov r0, [r3]\n");
                    } else if (instr->src.mem.offset > 0) {
                        fprintf(out, "    mov r0, [r3 + #%d]\n", instr->src.mem.offset);
                    } else {
                        fprintf(out, "    mov r0, [r3 - #%d]\n", -instr->src.mem.offset);
                    }
                    fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_R13, string_consts); fprintf(out, ", r0\n");
                }
                else if (instr->src.type == LIR_STACK) {
                    fprintf(out, "    mov r0, "); emit_operand(out, instr->src, REG_R12, string_consts); fprintf(out, "\n");
                    fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_R13, string_consts); fprintf(out, ", r0\n");
                }
                else if (instr->dest.type == LIR_MEM) {
                    fprintf(out, "    mov r3, [rfp - #%d]\n", get_vreg_offset(instr->dest.mem.base_vreg));
                    fprintf(out, "    mov r0, "); emit_operand(out, instr->src, REG_R12, string_consts); fprintf(out, "\n");
                    if (instr->dest.mem.offset == 0) {
                        fprintf(out, "    mov [r3], r0\n");
                    } else if (instr->dest.mem.offset > 0) {
                        fprintf(out, "    mov [r3 + #%d], r0\n", instr->dest.mem.offset);
                    } else {
                        fprintf(out, "    mov [r3 - #%d], r0\n", -instr->dest.mem.offset);
                    }
                }
                else if (instr->src.type == LIR_IMM && instr->src.string_const.str.length > 0) {
                    uint32_t id = instr->src.string_const.id;

                    /* IMM -> physical register */
                    if (instr->dest.type == LIR_REG_PHYSICAL) {
                        const char *preg = phys_reg_names[instr->dest.preg];
                        fprintf(out, "    lea %s, [rip + .LC%u]\n", preg, id);
                    }
                    /* IMM -> virtual register or stack slot */
                    else if (instr->dest.type == LIR_REG_VIRTUAL || instr->dest.type == LIR_STACK) {
                        int slot;
                        if (instr->dest.type == LIR_REG_VIRTUAL) {
                            slot = get_vreg_offset(instr->dest.vreg);
                        } else {
                            slot = get_vreg_offset(instr->dest.mem.base_vreg);
                        }
                        fprintf(out, "    lea r0, [rip + .LC%u]\n", id);
                        if (instr->dest.mem.offset == 0) {
                            fprintf(out, "    mov [rfp - #%d], r0\n", slot);
                        } else if (instr->dest.mem.offset > 0) {
                            fprintf(out, "    mov [rfp - #%d + #%d], r0\n", slot, instr->dest.mem.offset);
                        } else {
                            fprintf(out, "    mov [rfp - #%d - #%d], r0\n", slot, -instr->dest.mem.offset);
                        }
                    }
                    /* IMM -> indirect memory */
                    else if (instr->dest.type == LIR_MEM) {
                        fprintf(out, "    lea r0, [rip + .LC%u]\n", id);
                        fprintf(out, "    mov r3, [rfp - #%d]\n", get_vreg_offset(instr->dest.mem.base_vreg));
                        if (instr->dest.mem.offset == 0) {
                            fprintf(out, "    mov [r3], r0\n");
                        } else if (instr->dest.mem.offset > 0) {
                            fprintf(out, "    mov [r3 + #%d], r0\n", instr->dest.mem.offset);
                        } else {
                            fprintf(out, "    mov [r3 - #%d], r0\n", -instr->dest.mem.offset);
                        }
                    } else {
                        /* fallback for unusual dest kinds */
                        fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_R12, string_consts);
                        fprintf(out, ", "); emit_operand(out, instr->src, REG_R13, string_consts); fprintf(out, "\n");
                    }
                }
                else {
                    fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_R12, string_consts);
                    fprintf(out, ", "); emit_operand(out, instr->src, REG_R13, string_consts); fprintf(out, "\n");
                }
                break;
            }

            case LIR_ADD:
            case LIR_SUB:
            case LIR_IMUL: {
                const char *op_str = (instr->opcode == LIR_ADD) ? "add" : (instr->opcode == LIR_SUB) ? "sub" : "imul";

                fprintf(out, "    mov r0, "); emit_operand(out, instr->dest, REG_R12, string_consts); fprintf(out, "\n");
                fprintf(out, "    %s r0, r0, ", op_str); emit_operand(out, instr->src, REG_R13, string_consts); fprintf(out, "\n");
                fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_R12, string_consts); fprintf(out, ", r0\n");
                break;
            }

            case LIR_CQO: {
                fprintf(out, "    cqo\n");
                break;
            }

            case LIR_IDIV: {
                fprintf(out, "    idiv "); emit_operand(out, instr->dest, REG_R13, string_consts); fprintf(out, "\n");
                break;
            }

            case LIR_CMP: {
                fprintf(out, "    mov r0, "); emit_operand(out, instr->dest, REG_R12, string_consts); fprintf(out, "\n");
                fprintf(out, "    cmp r0, "); emit_operand(out, instr->src, REG_R13, string_consts); fprintf(out, "\n");
                break;
            }

            case LIR_SETCC: {
                static const char* setcc_strs[] = { "e", "ne", "l", "le", "g", "ge", "" };
                fprintf(out, "    set%s al\n", setcc_strs[instr->cond]);
                fprintf(out, "    movzx r0, al\n");
                fprintf(out, "    mov "); emit_operand(out, instr->dest, REG_R12, string_consts); fprintf(out, ", r0\n");
                break;
            }

            case LIR_JMP: {
                fprintf(out, "    jmp .L%ld\n", instr->dest.imm);
                break;
            }

            case LIR_JCC: {
                static const char* jcc_strs[] = { "je", "jne", "jl", "jle", "jg", "jge", "" };
                fprintf(out, "    %s .L%ld\n", jcc_strs[instr->cond], instr->dest.imm);
                break;
            }

            case LIR_CALL: {
                if (instr->dest.type == LIR_GLOBAL) {
                    fprintf(out, "    call %.*s\n", string_fmt(instr->dest.symbol->name));
                } else {
                    fprintf(out, "    call "); emit_operand(out, instr->dest, REG_R12, string_consts); fprintf(out, "\n");

                }
                break;
            }

            case LIR_RET: {
                fprintf(out, "    pop rfp\n");
                fprintf(out, "    ret\n");
                break;
            }
        }
    }
}