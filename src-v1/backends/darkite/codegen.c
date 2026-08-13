#include "codegen.h"
#include <string.h>

static const char* phys_reg_names[] = {
    "r1", "rcx", "rdx", "rbx", "sp", "r20", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static int get_vreg_offset(uint32_t vreg_id) {
    return (vreg_id + 1) * 8;
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
                    StringConstant sc = {
                        .str = instr->src.string_const.str,
                        .id = string_consts->len
                    };
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

static void emit_imm(FILE *out, LirOperand op, string_const_array *string_consts) {
    if (op.type == LIR_IMM) {
        if (op.string_const.str.length > 0) {
            fprintf(out, ".LC%u", op.string_const.id);
        } else {
            fprintf(out, "%lld", op.imm);
        }
    }
}

void codegen(FILE *out, LirFunction *fn, string_const_array *string_consts) {
    fprintf(out, ">%.*s\n", string_fmt(fn->symbol->name));

    int total_stack_bytes = fn->vreg_count * 8;
    if (total_stack_bytes % 16 != 0) {
        total_stack_bytes += 16 - (total_stack_bytes % 16);
    }

    fprintf(out, "    push r20\n");
    fprintf(out, "    add zr, sp, r20\n");
    if (total_stack_bytes > 0) {
        fprintf(out, "    subi sp, %d, sp\n", total_stack_bytes);
    }

    if (fn->symbol->type && fn->symbol->type->type == TYPEREF_FN) {
        PhysReg arg_regs[] = { REG_R2, REG_R3, REG_R4, REG_R5, REG_R6 };
        size_t param_count = fn->symbol->type->fn.params.len;
        for (size_t i = 0; i < param_count && i < sizeof(arg_regs) / sizeof(arg_regs[0]); i++) {
            int slot = get_vreg_offset((uint32_t)i);

            fprintf(out, "    subi r20, %d, r19\n", slot);

            fprintf(out, "    store r19, %s\n", phys_reg_names[arg_regs[i]]);
        }
        // TODO: copy stack-passed args for param_count > number of arg_regs
    }

    for (LirInstr *instr = fn->first; instr != NULL; instr = instr->next) {
        switch (instr->opcode) {
            case LIR_LABEL: {
                fprintf(out, ">L%lld\n", instr->dest.imm);
                break;
            }

            case LIR_MOV: {
                if (instr->dest.type == LIR_REG_VIRTUAL && instr->src.type == LIR_REG_VIRTUAL) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.vreg));
                    fprintf(out, "    load r19, r1\n");
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                    fprintf(out, "    store r19, r1\n");
                }
                else if (instr->src.type == LIR_STACK) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.mem.base_vreg));
                    if (instr->src.mem.offset == 0) {
                        fprintf(out, "    load r19, r1\n");
                    } else {
                        fprintf(out, "    load r19, r1\n");
                    }
                    if (instr->dest.type == LIR_REG_PHYSICAL) {
                        fprintf(out, "    add zr, r1, %s\n", phys_reg_names[instr->dest.preg]);
                    } else {
                        fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                        fprintf(out, "    store r19, r1\n");
                    }
                }
                else if (instr->src.type == LIR_MEM) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.mem.base_vreg));
                    fprintf(out, "    load r19, r1\n");
                    if (instr->dest.type == LIR_REG_PHYSICAL) {
                        fprintf(out, "    add zr, r1, %s\n", phys_reg_names[instr->dest.preg]);
                    } else {
                        fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                        fprintf(out, "    store r19, r1\n");
                    }
                }
                else if (instr->dest.type == LIR_MEM) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.mem.base_vreg));
                    if (instr->src.type == LIR_REG_PHYSICAL) {
                        fprintf(out, "    add zr, %s, r1\n", phys_reg_names[instr->src.preg]);
                    } else if (instr->src.type == LIR_IMM) {
                        fprintf(out, "    lli r1, %lld\n", instr->src.imm);
                    } else if (instr->src.type == LIR_STACK) {
                        fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.mem.base_vreg));
                        fprintf(out, "    load r19, r1\n");
                    } else {
                        fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.vreg));
                        fprintf(out, "    load r19, r1\n");
                    }
                    fprintf(out, "    store r19, r1\n");
                }
                else {
                    if (instr->src.type == LIR_REG_PHYSICAL) {
                        fprintf(out, "    add zr, %s, r1\n", phys_reg_names[instr->src.preg]);
                    } else if (instr->src.type == LIR_IMM) {
                        fprintf(out, "    lli r1, %lld\n", instr->src.imm);
                    } else if (instr->src.type == LIR_REG_VIRTUAL) {
                        fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.vreg));
                        fprintf(out, "    load r19, r1\n");
                    }

                    if (instr->dest.type == LIR_REG_PHYSICAL) {
                        fprintf(out, "    add zr, r1, %s\n", phys_reg_names[instr->dest.preg]);
                    } else {
                        fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                        fprintf(out, "    store r19, r1\n");
                    }
                }
                break;
            }

            case LIR_ADD:
            case LIR_SUB:
            case LIR_IMUL: {
                const char *op_str = (instr->opcode == LIR_ADD) ? "add" : (instr->opcode == LIR_SUB) ? "sub" : "imul";

                if (instr->src.type == LIR_REG_PHYSICAL) {
                    fprintf(out, "    add zr, %s, r1\n", phys_reg_names[instr->src.preg]);
                } else if (instr->src.type == LIR_IMM) {
                    fprintf(out, "    lli r1, %lld\n", instr->src.imm);
                } else if (instr->src.type == LIR_STACK) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.mem.base_vreg));
                    fprintf(out, "    load r19, r1\n");
                } else {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.vreg));
                    fprintf(out, "    load r19, r1\n");
                }

                if (instr->dest.type == LIR_REG_PHYSICAL) {
                    fprintf(out, "    add zr, %s, r2\n", phys_reg_names[instr->dest.preg]);
                } else if (instr->dest.type == LIR_REG_VIRTUAL) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                    fprintf(out, "    load r19, r2\n");
                } else if (instr->dest.type == LIR_STACK) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.mem.base_vreg));
                    fprintf(out, "    load r19, r2\n");
                } else if (instr->dest.type == LIR_IMM) {
                    fprintf(out, "    lli r2, %lld\n", instr->dest.imm);
                }

                fprintf(out, "    %s r2, r1, r3\n", op_str);

                if (instr->dest.type == LIR_REG_PHYSICAL) {
                    fprintf(out, "    add zr, r3, %s\n", phys_reg_names[instr->dest.preg]);
                } else {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                    fprintf(out, "    store r19, r3\n");
                }
                break;
            }

            case LIR_IDIV: {
                if (instr->dest.type == LIR_REG_PHYSICAL) {
                    fprintf(out, "    idiv %s\n", phys_reg_names[instr->dest.preg]);
                } else if (instr->dest.type == LIR_REG_VIRTUAL) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                    fprintf(out, "    load r19, r1\n");
                    fprintf(out, "    idiv r1\n");
                }
                break;
            }

            case LIR_CMP: {
                if (instr->dest.type == LIR_REG_PHYSICAL) {
                    fprintf(out, "    add zr, %s, r1\n", phys_reg_names[instr->dest.preg]);
                } else if (instr->dest.type == LIR_REG_VIRTUAL) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                    fprintf(out, "    load r19, r1\n");
                } else if (instr->dest.type == LIR_STACK) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.mem.base_vreg));
                    fprintf(out, "    load r19, r1\n");
                } else if (instr->dest.type == LIR_IMM) {
                    fprintf(out, "    lli r1, %lld\n", instr->dest.imm);
                }

                if (instr->src.type == LIR_REG_PHYSICAL) {
                    fprintf(out, "    add zr, %s, r2\n", phys_reg_names[instr->src.preg]);
                } else if (instr->src.type == LIR_REG_VIRTUAL) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.vreg));
                    fprintf(out, "    load r19, r2\n");
                } else if (instr->src.type == LIR_STACK) {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->src.mem.base_vreg));
                    fprintf(out, "    load r19, r2\n");
                } else if (instr->src.type == LIR_IMM) {
                    fprintf(out, "    lli r2, %lld\n", instr->src.imm);
                }

                fprintf(out, "    cmps r1, r2, r1\n");
                break;
            }

            case LIR_SETCC: {
                switch (instr->cond) {
                    case COND_E: {
                        fprintf(out, "    andi r1, 2, r1\n");
                        fprintf(out, "    shr r1, 1, r1\n");
                        break;
                    }
                    case COND_NE: {
                        fprintf(out, "    andi r1, 2, r1\n");
                        fprintf(out, "    shr r1, 1, r1\n");
                        fprintf(out, "    xori r1, 1, r1\n");
                        break;
                    }
                    case COND_L: {
                        fprintf(out, "    andi r1, 8, r1\n");
                        fprintf(out, "    shr r1, 3, r1\n");
                        break;
                    }
                    case COND_LE: {
                        fprintf(out, "    andi r1, 10, r1\n");
                        fprintf(out, "    add zr, r1, r2\n");
                        fprintf(out, "    shr r2, 1, r2\n");
                        fprintf(out, "    or r1, r2, r1\n");
                        fprintf(out, "    shr r2, 2, r2\n");
                        fprintf(out, "    or r1, r2, r1\n");
                        fprintf(out, "    andi r1, 1, r1\n");
                        break;
                    }
                    case COND_G: {
                        fprintf(out, "    andi r1, 10, r1\n");
                        fprintf(out, "    add zr, r1, r2\n");
                        fprintf(out, "    shr r2, 1, r2\n");
                        fprintf(out, "    or r1, r2, r1\n");
                        fprintf(out, "    shr r2, 2, r2\n");
                        fprintf(out, "    or r1, r2, r1\n");
                        fprintf(out, "    andi r1, 1, r1\n");
                        fprintf(out, "    xori r1, 1, r1\n");
                        break;
                    }
                    case COND_GE: {
                        fprintf(out, "    andi r1, 8, r1\n");
                        fprintf(out, "    add zr, r1, r2\n");
                        fprintf(out, "    shr r2, 1, r2\n");
                        fprintf(out, "    or r1, r2, r1\n");
                        fprintf(out, "    shr r2, 2, r2\n");
                        fprintf(out, "    or r1, r2, r1\n");
                        fprintf(out, "    andi r1, 1, r1\n");
                        fprintf(out, "    xori r1, 1, r1\n");
                        break;
                    }
                }

                if (instr->dest.type == LIR_REG_PHYSICAL) {
                    fprintf(out, "    add zr, r1, %s\n", phys_reg_names[instr->dest.preg]);
                } else {
                    fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                    fprintf(out, "    store r19, r1\n");
                }
                break;
            }

            case LIR_JMP: {
                fprintf(out, "    jmp <L%lld\n", instr->dest.imm);
                break;
            }

            case LIR_JCC: {
                switch (instr->cond) {
                    case COND_E:  fprintf(out, "    bei <L%lld\n", instr->dest.imm); break;
                    case COND_NE: fprintf(out, "    bnei <L%lld\n", instr->dest.imm); break;
                    case COND_L:  fprintf(out, "    bli <L%lld\n", instr->dest.imm); break;
                    case COND_LE: fprintf(out, "    blei <L%lld\n", instr->dest.imm); break;
                    case COND_G:  fprintf(out, "    bgi <L%lld\n", instr->dest.imm); break;
                    case COND_GE: fprintf(out, "    bgei <L%lld\n", instr->dest.imm); break;
                    default:      fprintf(out, "    jmp <L%lld\n", instr->dest.imm); break;
                }
                break;
            }

            case LIR_CALL: {
                if (instr->dest.type == LIR_GLOBAL) {
                    fprintf(out, "    bl %.*s\n", string_fmt(instr->dest.symbol->name));
                } else {
                    if (instr->dest.type == LIR_REG_PHYSICAL) {
                        fprintf(out, "    add zr, %s, r1\n", phys_reg_names[instr->dest.preg]);
                    } else if (instr->dest.type == LIR_REG_VIRTUAL) {
                        fprintf(out, "    subi r20, %d, r19\n", get_vreg_offset(instr->dest.vreg));
                        fprintf(out, "    load r19, r1\n");
                    }
                    fprintf(out, "    bl r1\n");
                }
                break;
            }

            case LIR_RET: {
                fprintf(out, "    add zr, r20, sp\n");
                fprintf(out, "    pull r20\n");
                fprintf(out, "    jmp lr\n");
                break;
            }

            default: {
                fprintf(out, "    // unhandled LIR opcode %d\n", instr->opcode);
                break;
            }
        }
    }
}
