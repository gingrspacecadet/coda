#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../mir.h"
#include "../../sema.h"

static const char *ABI_ARG_REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

typedef struct {
    Symbol *sym;
    int64_t offset;
} SymbolOffset;

static void emit_escaped_string(FILE *out, String str) {
    for (size_t i = 0; i < str.length; i++) {
        char c = str.data[i];
        switch (c) {
            case '\n': fprintf(out, "\\n"); break;
            case '\r': fprintf(out, "\\r"); break;
            case '\t': fprintf(out, "\\t"); break;
            case '\\': fprintf(out, "\\\\"); break;
            case '\"': fprintf(out, "\\\""); break;
            default:
                if (c >= 32 && c <= 126) {
                    fputc(c, out);
                } else {
                    fprintf(out, "\\%03o", (unsigned char)c);
                }
                break;
        }
    }
}

static int64_t compute_stack_layout(MirFunction *fn, SymbolOffset *sym_offsets, size_t *out_total_size) {
    int64_t current_offset = 0;
    size_t index = 0;

    for (size_t i = 0; i < fn->params.len; i++) {
        size_t param_size = get_type_size(fn->params.data[i]->type);
        if (param_size == 0) param_size = 8;  // default to 8 if unknown
        // Align to 8 bytes for parameters
        param_size = (param_size + 7) & ~7;
        current_offset -= param_size;
        sym_offsets[index].sym = fn->params.data[i];
        sym_offsets[index].offset = current_offset;
        index++;
    }

    for (size_t i = 0; i < fn->locals.len; i++) {
        size_t local_size = get_type_size(fn->locals.data[i]->type);
        if (local_size == 0) local_size = 8;  // default to 8 if unknown
        // Align to 8 bytes for locals
        local_size = (local_size + 7) & ~7;
        current_offset -= local_size;
        sym_offsets[index].sym = fn->locals.data[i];
        sym_offsets[index].offset = current_offset;
        index++;
    }

    int64_t temp_start_offset = current_offset;
    
    size_t total_bytes = 0;
    for (size_t i = 0; i < fn->params.len; i++) {
        size_t param_size = get_type_size(fn->params.data[i]->type);
        if (param_size == 0) param_size = 8;
        param_size = (param_size + 7) & ~7;
        total_bytes += param_size;
    }
    for (size_t i = 0; i < fn->locals.len; i++) {
        size_t local_size = get_type_size(fn->locals.data[i]->type);
        if (local_size == 0) local_size = 8;
        local_size = (local_size + 7) & ~7;
        total_bytes += local_size;
    }
    total_bytes += fn->temp_count * 8;
    *out_total_size = (total_bytes + 15) & ~15;

    return temp_start_offset;
}

static int64_t lookup_symbol_offset(MirFunction *fn, SymbolOffset *offsets, Symbol *sym) {
    size_t total_symbols = fn->params.len + fn->locals.len;
    for (size_t i = 0; i < total_symbols; i++) {
        if (offsets[i].sym == sym) {
            return offsets[i].offset;
        }
    }
    return 0;
}

static void format_rbp_operand(char *buf, size_t len, int64_t off) {
    if (off == 0) {
        snprintf(buf, len, "QWORD PTR [rbp]");
    } else if (off > 0) {
        snprintf(buf, len, "QWORD PTR [rbp+%ld]", off);
    } else {
        snprintf(buf, len, "QWORD PTR [rbp%ld]", off);
    }
}

static void format_rbp_address(char *buf, size_t len, int64_t off) {
    if (off == 0) {
        snprintf(buf, len, "[rbp]");
    } else if (off > 0) {
        snprintf(buf, len, "[rbp+%ld]", off);
    } else {
        snprintf(buf, len, "[rbp%ld]", off);
    }
}

static void resolve_operand_string(MirFunction *fn, SymbolOffset *offsets, int64_t temp_start, MirOperand op, char *buf, size_t len) {
    switch (op.type) {
        case MIR_VAL_LIT:
            if (op.lit.type == LITERAL_BOOL) { 
                snprintf(buf, len, "%d", op.lit._bool ? 1 : 0);
            } else if (op.lit.type == LITERAL_INT) {
                snprintf(buf, len, "%d", op.lit._int);
            } else if (op.lit.type == LITERAL_UINT) {
                snprintf(buf, len, "%u", op.lit._int);
            } else {
                snprintf(buf, len, "%ld", (long)op.lit._int);
            }
            break;
        case MIR_VAL_TEMP: {
            int64_t off = temp_start - ((op.temp + 1) * 8);
            format_rbp_operand(buf, len, off);
            break;
        }
        case MIR_VAL_SYMBOL: {
            int64_t off = lookup_symbol_offset(fn, offsets, op.symbol);
            format_rbp_operand(buf, len, off);
            break;
        }
        case MIR_VAL_MEM:
            if (op.base_symbol) {
                int64_t off = lookup_symbol_offset(fn, offsets, op.base_symbol) + op.offset;
                format_rbp_operand(buf, len, off);
            } else {
                int64_t off = temp_start - ((op.base_temp + 1) * 8) + op.offset;
                format_rbp_operand(buf, len, off);
            }
            break;
        case MIR_VAL_LABEL:
            snprintf(buf, len, ".Llabel_%d", op.label_id);
            break;
        default:
            snprintf(buf, len, "0");
            break;
    }
}

static void emit_operand_addr(FILE *out, const char *reg, MirFunction *fn, SymbolOffset *offsets, int64_t temp_start, MirOperand op) {
    if (op.type == MIR_VAL_SYMBOL) {
        int64_t off = lookup_symbol_offset(fn, offsets, op.symbol);
        fprintf(out, "    lea %s, [rbp%s%ld]\n", reg, off >= 0 ? "+" : "", off);
    } else if (op.type == MIR_VAL_TEMP) {
        int64_t off = temp_start - ((op.temp + 1) * 8);
        fprintf(out, "    lea %s, [rbp%s%ld]\n", reg, off >= 0 ? "+" : "", off);
    } else if (op.type == MIR_VAL_LIT && op.lit.type == LITERAL_STRING) {
        fprintf(out, "    lea %s, [rip + .Lstr_%d]\n", reg, op.lit.str_id);
    } else if (op.type == MIR_VAL_MEM) {
        int64_t off = op.base_symbol 
            ? lookup_symbol_offset(fn, offsets, op.base_symbol) + op.offset
            : temp_start - ((op.base_temp + 1) * 8) + op.offset;
        fprintf(out, "    lea %s, [rbp%s%ld]\n", reg, off >= 0 ? "+" : "", off);
    }
}

static void emit_load(FILE *out, const char *reg, MirFunction *fn, SymbolOffset *offsets, int64_t temp_start, MirOperand op) {
    char op_str[128];
    resolve_operand_string(fn, offsets, temp_start, op, op_str, sizeof(op_str));
    if (op.type == MIR_VAL_LIT) {
        if (op.lit.type == LITERAL_STRING) {
            fprintf(out, "    lea %s, [rip + .Lstr_%d]\n", reg, op.lit.str_id);
        } else {
            fprintf(out, "    mov %s, %s\n", reg, op_str);
        }
    } else {
        fprintf(out, "    mov %s, %s\n", reg, op_str);
    }
}

static void emit_store(FILE *out, MirOperand dest, const char *reg, MirFunction *fn, SymbolOffset *offsets, int64_t temp_start) {
    char dest_str[128];
    resolve_operand_string(fn, offsets, temp_start, dest, dest_str, sizeof(dest_str));
    if (dest.type != MIR_VAL_NONE) {
        fprintf(out, "    mov %s, %s\n", dest_str, reg);
    }
}

static void emit_instruction(FILE *out, MirFunction *fn, MirInstr *inst, SymbolOffset *offsets, int64_t temp_start);

static void emit_block_recursive(FILE *out, MirFunction *fn, MirBlock *block, SymbolOffset *offsets, int64_t temp_start) {
    if (!block || block->visited) return;
    block->visited = true;

    fprintf(out, ".Lblock_%d:\n", block->id);
    MirInstr *inst = block->first;
    while (inst) {
        emit_instruction(out, fn, inst, offsets, temp_start);
        inst = inst->next;
    }

    emit_block_recursive(out, fn, block->succ_true, offsets, temp_start);
    emit_block_recursive(out, fn, block->succ_false, offsets, temp_start);
}

static void emit_instruction(FILE *out, MirFunction *fn, MirInstr *inst, SymbolOffset *offsets, int64_t temp_start) {
    char res_str[128], lhs_str[128], rhs_str[128];
    resolve_operand_string(fn, offsets, temp_start, inst->result, res_str, sizeof(res_str));
    resolve_operand_string(fn, offsets, temp_start, inst->lhs, lhs_str, sizeof(lhs_str));
    resolve_operand_string(fn, offsets, temp_start, inst->rhs, rhs_str, sizeof(rhs_str));

    switch (inst->op) {
        case MIR_OP_ADD:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    add rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_SUB:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    sub rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_MUL:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    imul rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_DIV:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    cqo\n");
            fprintf(out, "    idiv rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_LT:  case MIR_OP_LE: case MIR_OP_GT:
        case MIR_OP_GE:  case MIR_OP_EQ: case MIR_OP_NE:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    cmp rax, rbx\n");
            
            const char *set_op = "e";
            if (inst->op == MIR_OP_LT) set_op = "l";
            else if (inst->op == MIR_OP_LE) set_op = "le";
            else if (inst->op == MIR_OP_GT) set_op = "g";
            else if (inst->op == MIR_OP_GE) set_op = "ge";
            else if (inst->op == MIR_OP_EQ) set_op = "e";
            else if (inst->op == MIR_OP_NE) set_op = "ne";

            fprintf(out, "    set%s al\n", set_op);
            fprintf(out, "    movzx rax, al\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_LOG_AND:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    and rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_LOG_OR:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    or rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_NEG:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            fprintf(out, "    neg rax\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_NOT:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            fprintf(out, "    xor rax, 1\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_COPY: {
            size_t size = 8;
            if (inst->result.resolved_type) {
                size = get_type_size(inst->result.resolved_type);
                if (size == 0) size = 8;
                size = (size + 7) & ~7;
            }

            if (size > 8) {
                emit_operand_addr(out, "rsi", fn, offsets, temp_start, inst->lhs);
                emit_operand_addr(out, "rdi", fn, offsets, temp_start, inst->result);

                for (size_t offset = 0; offset < size; offset += 8) {
                    fprintf(out, "    mov rax, QWORD PTR [rsi+%zu]\n", offset);
                    fprintf(out, "    mov QWORD PTR [rdi+%zu], rax\n", offset);
                }
            } else {
                bool emitted = false;
                if (inst->result.type == MIR_VAL_SYMBOL && inst->result.resolved_type &&
                    inst->result.resolved_type->type == TYPEREF_POINTER &&
                    inst->lhs.type == MIR_VAL_SYMBOL) {
                    int64_t off = lookup_symbol_offset(fn, offsets, inst->lhs.symbol);
                    fprintf(out, "    lea rax, [rbp%s%ld]\n", off >= 0 ? "+" : "", off);
                    emitted = true;
                }

                if (!emitted) {
                    emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
                }
                emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            }
            break;
        }

        case MIR_OP_LOAD:
            emit_load(out, "rax", fn, offsets, temp_start, inst->rhs);
            fprintf(out, "    mov rax, QWORD PTR [rax]\n");
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;

        case MIR_OP_STORE:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            
            // If result is a direct memory location, use it directly
            if (inst->result.type == MIR_VAL_MEM) {
                char addr_str[128];
                int64_t off;
                if (inst->result.base_symbol) {
                    off = lookup_symbol_offset(fn, offsets, inst->result.base_symbol) + inst->result.offset;
                } else {
                    off = temp_start - ((inst->result.base_temp + 1) * 8) + inst->result.offset;
                }
                format_rbp_address(addr_str, sizeof(addr_str), off);
                fprintf(out, "    mov QWORD PTR %s, rax\n", addr_str);
            } else {
                emit_load(out, "rbx", fn, offsets, temp_start, inst->result);
                fprintf(out, "    mov QWORD PTR [rbx], rax\n");
            }
            break;

        case MIR_OP_JMP:
            fprintf(out, "    jmp .Lblock_%d\n", inst->label_id);
            break;

        case MIR_OP_BRANCH:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            fprintf(out, "    cmp rax, 0\n");
            fprintf(out, "    jne .Lblock_%d\n", inst->label_id);
            break;

        case MIR_OP_BRANCH_FALSE:
            emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
            fprintf(out, "    cmp rax, 0\n");
            fprintf(out, "    je .Lblock_%d\n", inst->label_id);
            break;

        case MIR_OP_CALL: {
            size_t reg_idx = 0;
            for (size_t i = 0; i < inst->arg_count && reg_idx < 6; i++) {
                MirOperand arg = inst->call_args[i];
                size_t sz = arg.resolved_type ? get_type_size(arg.resolved_type) : 8;
                if (sz == 0) sz = 8;
                sz = (sz + 7) & ~7;

                if (sz > 8) {
                    emit_operand_addr(out, "rax", fn, offsets, temp_start, arg);

                    for (size_t offset = 0; offset < sz && reg_idx < 6; offset += 8) {
                        fprintf(out, "    mov %s, QWORD PTR [rax+%zu]\n", ABI_ARG_REGS[reg_idx++], offset);
                    }
                } else {
                    emit_load(out, ABI_ARG_REGS[reg_idx++], fn, offsets, temp_start, arg);
                }
            }
            
            if (inst->lhs.type == MIR_VAL_SYMBOL) {
                fprintf(out, "    call %.*s\n", string_fmt(inst->lhs.symbol->mangled));
            } else {
                emit_load(out, "rax", fn, offsets, temp_start, inst->lhs);
                fprintf(out, "    call rax\n");
            }
            
            emit_store(out, inst->result, "rax", fn, offsets, temp_start);
            break;
        }

        case MIR_OP_RET: {
            MirOperand ret_val = null_op();
            if (inst->rhs.type != MIR_VAL_NONE) ret_val = inst->rhs;
            else if (inst->lhs.type != MIR_VAL_NONE) ret_val = inst->lhs;
            else if (inst->result.type != MIR_VAL_NONE) ret_val = inst->result;

            if (ret_val.type != MIR_VAL_NONE) {
                emit_load(out, "rax", fn, offsets, temp_start, ret_val);
            }
            
            fprintf(out, "    mov rsp, rbp\n");
            fprintf(out, "    pop rbp\n");
            fprintf(out, "    ret\n");
            break;
        }

        case MIR_OP_LABEL:
            fprintf(out, ".Llabel_%d:\n", inst->label_id);
            break;
    }
}

__attribute__((visibility("default")))
void backend(FILE *out, MirBuilder *builder, MirModule *mod) {
    fprintf(out, ".section .rodata\n");

    for (size_t i = 0; i < mod->strings.len; i++) {
        String str = mod->strings.data[i];

        fprintf(out, ".Lstr_bytes_%zu:\n", i);
        fprintf(out, "    .string \"");
        emit_escaped_string(out, str);
        fprintf(out, "\"\n", string_fmt(str));

    }

    fprintf(out, "\n.section .data\n");
    for (size_t i = 0; i < mod->strings.len; i++) {
        String str = mod->strings.data[i];
        fprintf(out, "    .align 8\n");
    
        fprintf(out, ".Lstr_%zu:\n", i);
        fprintf(out, "    .quad %zu\n", str.length);
        fprintf(out, "    .quad .Lstr_bytes_%zu\n", i);
    }

    fprintf(out, "\n.section .text\n");
    fprintf(out, ".intel_syntax noprefix\n");

    for (size_t f = 0; f < mod->functions.len; f++) {
        MirFunction *fn = mod->functions.data[f];
        
        if (fn->is_export) fprintf(out, ".global %.*s\n", string_fmt(fn->symbol->mangled));
        fprintf(out, "%.*s:\n", string_fmt(fn->symbol->mangled));

        SymbolOffset *offsets = malloc(sizeof(SymbolOffset) * (fn->params.len + fn->locals.len));
        size_t total_stack_size = 0;
        int64_t temp_start = compute_stack_layout(fn, offsets, &total_stack_size);

        fprintf(out, "    push rbp\n");
        fprintf(out, "    mov rbp, rsp\n");
        if (total_stack_size > 0) {
            fprintf(out, "    sub rsp, %zu\n", total_stack_size);
        }

        size_t reg_idx = 0;
        for (size_t i = 0; i < fn->params.len && reg_idx < 6; i++) {
            Symbol *param_sym = fn->params.data[i]; 
            int64_t off = lookup_symbol_offset(fn, offsets, param_sym);
            
            size_t sz = param_sym->type ? get_type_size(param_sym->type) : 8;
            if (sz == 0) sz = 8;
            sz = (sz + 7) & ~7;

            if (off != 0) {
                if (sz > 8) {
                    fprintf(out, "    mov QWORD PTR [rbp%s%ld], %s\n", 
                            (off >= 0 ? "+" : ""), off, ABI_ARG_REGS[reg_idx++]);
                    
                    if (reg_idx < 6) {
                        int64_t next_off = off + 8;
                        fprintf(out, "    mov QWORD PTR [rbp%s%ld], %s\n", 
                                (next_off >= 0 ? "+" : ""), next_off, ABI_ARG_REGS[reg_idx++]);
                    }
                } else {
                    fprintf(out, "    mov QWORD PTR [rbp%s%ld], %s\n", 
                            (off >= 0 ? "+" : ""), off, ABI_ARG_REGS[reg_idx++]);
                }
            } else {
                reg_idx += (sz > 8) ? 2 : 1;
            }
        }

        void emit_block_recursive(FILE *out, MirFunction *fn, MirBlock *block, SymbolOffset *offsets, int64_t temp_start) {
            if (!block || block->visited) return;
            block->visited = true;

            fprintf(out, ".Lblock_%d:\n", block->id);
            MirInstr *inst = block->first;
            while (inst) {
                emit_instruction(out, fn, inst, offsets, temp_start);
                inst = inst->next;
            }

            emit_block_recursive(out, fn, block->succ_true, offsets, temp_start);
            emit_block_recursive(out, fn, block->succ_false, offsets, temp_start);
        }

        emit_block_recursive(out, fn, fn->entry_block, offsets, temp_start);

        free(offsets);
        fprintf(out, "\n");
    }
}