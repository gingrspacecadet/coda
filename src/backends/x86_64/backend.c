#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../mir.h"
#include "../../sema.h"

static const char *ABI_ARG_REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

typedef struct {
    Symbol *sym;
    int64_t offset;
} SymbolOffset;

typedef struct {
    int64_t offset;
    size_t size;
} TempLayout;

static size_t align8(size_t n) {
    return (n + 7u) & ~(size_t)7u;
}

static void emit_escaped_string(FILE *out, String str) {
    for (size_t i = 0; i < str.length; i++) {
        char c = str.data[i];
        switch (c) {
            case '\n': fprintf(out, "\\n"); break;
            case '\r': fprintf(out, "\\r"); break;
            case '\t': fprintf(out, "\\t"); break;
            case '\\': fprintf(out, "\\\\"); break;
            case '"':  fprintf(out, "\\\""); break;
            default:
                if (c >= 32 && c <= 126) fputc(c, out);
                else fprintf(out, "\\%03o", (unsigned char)c);
                break;
        }
    }
}

static TypeRef *operand_type(MirOperand op) {
    return op.resolved_type;
}

static bool type_is_fn(TypeRef *t) {
    return t && t->type == TYPEREF_FN;
}

static bool fn_returns_indirect_type(TypeRef *fn_type) {
    return type_is_fn(fn_type) && get_type_size(fn_type->fn.ret_type) > 8;
}

static bool fn_returns_indirect(MirFunction *fn) {
    if (!fn || !fn->symbol || !fn->symbol->type) return false;
    return fn_returns_indirect_type(fn->symbol->type);
}

static TypeRef *function_type_from_operand(MirOperand op) {
    TypeRef *t = operand_type(op);
    if (!t && op.type == MIR_VAL_SYMBOL && op.symbol) t = op.symbol->type;
    if (!t) return NULL;
    if (t->type == TYPEREF_FN) return t;
    if (t->type == TYPEREF_POINTER && t->pointer.pointee && t->pointer.pointee->type == TYPEREF_FN) {
        return t->pointer.pointee;
    }
    return NULL;
}

static size_t operand_value_size(MirOperand op) {
    TypeRef *t = operand_type(op);
    if (!t) return 8;
    size_t sz = get_type_size(t);
    return sz ? align8(sz) : 8;
}

static void scan_operand_temp_sizes(size_t *temp_sizes, MirOperand op) {
    if (op.type == MIR_VAL_TEMP) {
        size_t sz = operand_value_size(op);
        if (sz > temp_sizes[op.temp]) temp_sizes[op.temp] = sz;
    }
}

static void scan_instr_temp_sizes(MirInstr *inst, size_t *temp_sizes) {
    if (!inst) return;

    scan_operand_temp_sizes(temp_sizes, inst->result);
    scan_operand_temp_sizes(temp_sizes, inst->lhs);
    scan_operand_temp_sizes(temp_sizes, inst->rhs);

    for (size_t i = 0; i < inst->arg_count; i++) {
        scan_operand_temp_sizes(temp_sizes, inst->call_args[i]);
    }
}

static void scan_block_temp_sizes(MirBlock *block, size_t *temp_sizes) {
    if (!block || block->visited) return;
    block->visited = true;

    for (MirInstr *inst = block->first; inst; inst = inst->next) {
        scan_instr_temp_sizes(inst, temp_sizes);
    }

    scan_block_temp_sizes(block->succ_true, temp_sizes);
    scan_block_temp_sizes(block->succ_false, temp_sizes);
}

static void clear_block_marks(MirBlock *block) {
    if (!block || !block->visited) return;
    block->visited = false;
    clear_block_marks(block->succ_true);
    clear_block_marks(block->succ_false);
}

static void format_rbp_operand(char *buf, size_t len, int64_t off) {
    if (off == 0) snprintf(buf, len, "QWORD PTR [rbp]");
    else if (off > 0) snprintf(buf, len, "QWORD PTR [rbp+%ld]", off);
    else snprintf(buf, len, "QWORD PTR [rbp%ld]", off);
}

static void format_rbp_address(char *buf, size_t len, int64_t off) {
    if (off == 0) snprintf(buf, len, "[rbp]");
    else if (off > 0) snprintf(buf, len, "[rbp+%ld]", off);
    else snprintf(buf, len, "[rbp%ld]", off);
}

static int64_t lookup_symbol_offset(MirFunction *fn, SymbolOffset *offsets, Symbol *sym) {
    size_t total_symbols = fn->params.len + fn->locals.len;
    for (size_t i = 0; i < total_symbols; i++) {
        if (offsets[i].sym == sym) return offsets[i].offset;
    }
    return 0;
}

static int64_t temp_offset(TempLayout *temps, size_t temp_id) {
    return temps[temp_id].offset;
}

static int64_t mem_operand_offset(MirFunction *fn, SymbolOffset *offsets, TempLayout *temps, MirOperand op) {
    if (op.base_symbol) return lookup_symbol_offset(fn, offsets, op.base_symbol) + op.offset;
    return temp_offset(temps, op.base_temp) + op.offset;
}

static void resolve_operand_string(MirFunction *fn, SymbolOffset *offsets, TempLayout *temps, MirOperand op, char *buf, size_t len) {
    switch (op.type) {
        case MIR_VAL_LIT:
            switch (op.lit.type) {
                case LITERAL_BOOL:   snprintf(buf, len, "%d", op.lit._bool ? 1 : 0); break;
                case LITERAL_INT:    snprintf(buf, len, "%ld", (long)op.lit._int); break;
                case LITERAL_UINT:   snprintf(buf, len, "%lu", (unsigned long)op.lit._int); break;
                case LITERAL_CHAR:   snprintf(buf, len, "%d", (int)op.lit._char); break;
                default:             snprintf(buf, len, "%ld", (long)op.lit._int); break;
            }
            break;
        case MIR_VAL_TEMP:
            format_rbp_operand(buf, len, temp_offset(temps, op.temp));
            break;
        case MIR_VAL_SYMBOL:
            format_rbp_operand(buf, len, lookup_symbol_offset(fn, offsets, op.symbol));
            break;
        case MIR_VAL_MEM:
            format_rbp_operand(buf, len, mem_operand_offset(fn, offsets, temps, op));
            break;
        case MIR_VAL_LABEL:
            snprintf(buf, len, ".Llabel_%d", op.label_id);
            break;
        default:
            snprintf(buf, len, "0");
            break;
    }
}

static void emit_operand_addr(FILE *out, const char *reg, MirFunction *fn, SymbolOffset *offsets, TempLayout *temps, MirOperand op) {
    if (op.type == MIR_VAL_SYMBOL) {
        int64_t off = lookup_symbol_offset(fn, offsets, op.symbol);
        fprintf(out, "    lea %s, [rbp%s%ld]\n", reg, off >= 0 ? "+" : "", off);
    } else if (op.type == MIR_VAL_TEMP) {
        int64_t off = temp_offset(temps, op.temp);
        fprintf(out, "    lea %s, [rbp%s%ld]\n", reg, off >= 0 ? "+" : "", off);
    } else if (op.type == MIR_VAL_LIT && op.lit.type == LITERAL_STRING) {
        fprintf(out, "    lea %s, [rip + .Lstr_%d]\n", reg, op.lit.str_id);
    } else if (op.type == MIR_VAL_MEM) {
        int64_t off = mem_operand_offset(fn, offsets, temps, op);
        fprintf(out, "    lea %s, [rbp%s%ld]\n", reg, off >= 0 ? "+" : "", off);
    }
}

static void emit_load(FILE *out, const char *reg, MirFunction *fn, SymbolOffset *offsets, TempLayout *temps, MirOperand op) {
    char op_str[128];

    if (op.type == MIR_VAL_SYMBOL && op.symbol && (op.symbol->flags & SYMFLAG_FN)) {
        fprintf(out, "    lea %s, [rip + %.*s]\n", reg, string_fmt(op.symbol->mangled.length ? op.symbol->mangled : op.symbol->name));
        return;
    }

    if (op.type == MIR_VAL_MEM && op.base_symbol && op.base_symbol->type && op.base_symbol->type->type == TYPEREF_POINTER) {
        int64_t sym_off = lookup_symbol_offset(fn, offsets, op.base_symbol);
        fprintf(out, "    mov rcx, QWORD PTR [rbp%s%ld]\n", sym_off >= 0 ? "+" : "", sym_off);
        fprintf(out, "    mov %s, QWORD PTR [rcx%s%ld]\n", reg, op.offset >= 0 ? "+" : "", op.offset);
        return;
    }

    resolve_operand_string(fn, offsets, temps, op, op_str, sizeof(op_str));
    if (op.type == MIR_VAL_LIT && op.lit.type == LITERAL_STRING) {
        fprintf(out, "    lea %s, [rip + .Lstr_%d]\n", reg, op.lit.str_id);
    } else {
        fprintf(out, "    mov %s, %s\n", reg, op_str);
    }
}

static void emit_store(FILE *out, MirOperand dest, const char *reg, MirFunction *fn, SymbolOffset *offsets, TempLayout *temps) {
    if (dest.type == MIR_VAL_NONE) return;
    char dest_str[128];
    resolve_operand_string(fn, offsets, temps, dest, dest_str, sizeof(dest_str));
    fprintf(out, "    mov %s, %s\n", dest_str, reg);
}

static void emit_copy_aggregate(FILE *out, const char *dst_reg, const char *src_reg, size_t size) {
    for (size_t off = 0; off < size; off += 8) {
        fprintf(out, "    mov rax, QWORD PTR [%s+%zu]\n", src_reg, off);
        fprintf(out, "    mov QWORD PTR [%s+%zu], rax\n", dst_reg, off);
    }
}

static size_t compute_temp_layout(MirFunction *fn, TempLayout *temps, const size_t *temp_sizes) {
    size_t total = 0;
    for (size_t i = 0; i < fn->temp_count; i++) {
        size_t sz = temp_sizes[i] ? temp_sizes[i] : 8;
        temps[i].size = align8(sz);
        total += temps[i].size;
    }
    return total;
}

static int64_t compute_stack_layout(MirFunction *fn, SymbolOffset *sym_offsets, TempLayout *temps, const size_t *temp_sizes, size_t *out_total_size, int64_t *out_ret_ptr_offset) {
    int64_t current_offset = 0;
    size_t index = 0;

    *out_ret_ptr_offset = 0;
    if (fn_returns_indirect(fn)) {
        current_offset -= 8;
        *out_ret_ptr_offset = current_offset;
    }

    for (size_t i = 0; i < fn->params.len; i++) {
        size_t sz = get_type_size(fn->params.data[i]->type);
        if (sz == 0) sz = 8;
        sz = align8(sz);
        current_offset -= (int64_t)sz;
        sym_offsets[index++] = (SymbolOffset){ .sym = fn->params.data[i], .offset = current_offset };
    }

    for (size_t i = 0; i < fn->locals.len; i++) {
        size_t sz = get_type_size(fn->locals.data[i]->type);
        if (sz == 0) sz = 8;
        sz = align8(sz);
        current_offset -= (int64_t)sz;
        sym_offsets[index++] = (SymbolOffset){ .sym = fn->locals.data[i], .offset = current_offset };
    }

    for (size_t i = 0; i < fn->temp_count; i++) {
        size_t sz = temp_sizes[i] ? temp_sizes[i] : 8;
        sz = align8(sz);
        current_offset -= (int64_t)sz;
        temps[i].offset = current_offset;
        temps[i].size = sz;
    }

    *out_total_size = (size_t)(-current_offset);
    *out_total_size = (*out_total_size + 15u) & ~(size_t)15u;
    return current_offset;
}

static void emit_block_recursive(FILE *out, MirFunction *fn, MirBlock *block, SymbolOffset *offsets, TempLayout *temps, int64_t ret_ptr_off);

static void emit_instruction(FILE *out, MirFunction *fn, MirInstr *inst, SymbolOffset *offsets, TempLayout *temps, int64_t ret_ptr_off) {
    switch (inst->op) {
        case MIR_OP_ADD:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temps, inst->rhs);
            fprintf(out, "    add rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_SUB:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temps, inst->rhs);
            fprintf(out, "    sub rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_MUL:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temps, inst->rhs);
            fprintf(out, "    imul rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_DIV:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temps, inst->rhs);
            fprintf(out, "    cqo\n");
            fprintf(out, "    idiv rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_LT:
        case MIR_OP_LE:
        case MIR_OP_GT:
        case MIR_OP_GE:
        case MIR_OP_EQ:
        case MIR_OP_NE: {
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temps, inst->rhs);
            fprintf(out, "    cmp rax, rbx\n");

            const char *set_op = "e";
            if (inst->op == MIR_OP_LT) set_op = "l";
            else if (inst->op == MIR_OP_LE) set_op = "le";
            else if (inst->op == MIR_OP_GT) set_op = "g";
            else if (inst->op == MIR_OP_GE) set_op = "ge";
            else if (inst->op == MIR_OP_NE) set_op = "ne";

            fprintf(out, "    set%s al\n", set_op);
            fprintf(out, "    movzx rax, al\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;
        }

        case MIR_OP_LOG_AND:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temps, inst->rhs);
            fprintf(out, "    and rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_LOG_OR:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            emit_load(out, "rbx", fn, offsets, temps, inst->rhs);
            fprintf(out, "    or rax, rbx\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_NEG:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            fprintf(out, "    neg rax\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_NOT:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            fprintf(out, "    xor rax, 1\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_COPY: {
            size_t size = 8;
            if (inst->result.resolved_type) {
                size = get_type_size(inst->result.resolved_type);
                if (size == 0) size = 8;
                size = align8(size);
            } else if (inst->lhs.resolved_type) {
                size = get_type_size(inst->lhs.resolved_type);
                if (size == 0) size = 8;
                size = align8(size);
            }

            if (size > 8) {
                emit_operand_addr(out, "rsi", fn, offsets, temps, inst->lhs);
                emit_operand_addr(out, "rdi", fn, offsets, temps, inst->result);
                emit_copy_aggregate(out, "rdi", "rsi", size);
            } else {
                bool emitted = false;
                if (inst->result.type == MIR_VAL_SYMBOL && inst->result.resolved_type &&
                    inst->result.resolved_type->type == TYPEREF_POINTER &&
                    inst->lhs.type == MIR_VAL_SYMBOL) {
                    int64_t off = lookup_symbol_offset(fn, offsets, inst->lhs.symbol);
                    fprintf(out, "    lea rax, [rbp%s%ld]\n", off >= 0 ? "+" : "", off);
                    emitted = true;
                }
                if (!emitted) emit_load(out, "rax", fn, offsets, temps, inst->lhs);
                emit_store(out, inst->result, "rax", fn, offsets, temps);
            }
            break;
        }

        case MIR_OP_LOAD:
            emit_load(out, "rax", fn, offsets, temps, inst->rhs);
            fprintf(out, "    mov rax, QWORD PTR [rax]\n");
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_STORE:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            if (inst->result.type == MIR_VAL_MEM) {
                char addr_str[128];
                int64_t off = mem_operand_offset(fn, offsets, temps, inst->result);
                format_rbp_address(addr_str, sizeof(addr_str), off);
                fprintf(out, "    mov QWORD PTR %s, rax\n", addr_str);
            } else {
                emit_load(out, "rbx", fn, offsets, temps, inst->result);
                fprintf(out, "    mov QWORD PTR [rbx], rax\n");
            }
            break;

        case MIR_OP_ADDR:
            emit_operand_addr(out, "rax", fn, offsets, temps, inst->lhs);
            emit_store(out, inst->result, "rax", fn, offsets, temps);
            break;

        case MIR_OP_JMP:
            fprintf(out, "    jmp .Lblock_%d\n", inst->label_id);
            break;

        case MIR_OP_BRANCH:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            fprintf(out, "    cmp rax, 0\n");
            fprintf(out, "    jne .Lblock_%d\n", inst->label_id);
            break;

        case MIR_OP_BRANCH_FALSE:
            emit_load(out, "rax", fn, offsets, temps, inst->lhs);
            fprintf(out, "    cmp rax, 0\n");
            fprintf(out, "    je .Lblock_%d\n", inst->label_id);
            break;

        case MIR_OP_CALL: {
            TypeRef *callee_t = function_type_from_operand(inst->lhs);
            bool indirect_ret = callee_t && callee_t->type == TYPEREF_FN && get_type_size(callee_t->fn.ret_type) > 8;

            size_t reg_idx = indirect_ret ? 1 : 0;
            if (indirect_ret) {
                emit_operand_addr(out, "rdi", fn, offsets, temps, inst->result);
            }

            for (size_t i = 0; i < inst->arg_count && reg_idx < 6; i++) {
                MirOperand arg = inst->call_args[i];
                size_t sz = arg.resolved_type ? get_type_size(arg.resolved_type) : 8;
                if (sz == 0) sz = 8;
                sz = align8(sz);

                if (sz > 8) {
                    emit_operand_addr(out, "rax", fn, offsets, temps, arg);
                    for (size_t off = 0; off < sz && reg_idx < 6; off += 8) {
                        fprintf(out, "    mov %s, QWORD PTR [rax+%zu]\n", ABI_ARG_REGS[reg_idx++], off);
                    }
                } else {
                    emit_load(out, ABI_ARG_REGS[reg_idx++], fn, offsets, temps, arg);
                }
            }

            if (inst->lhs.type == MIR_VAL_SYMBOL && (inst->lhs.symbol->flags & SYMFLAG_FN)) {
                fprintf(out, "    call %.*s\n", string_fmt(inst->lhs.symbol->mangled.length ? inst->lhs.symbol->mangled : inst->lhs.symbol->name));
            } else {
                emit_load(out, "rax", fn, offsets, temps, inst->lhs);
                fprintf(out, "    call rax\n");
            }

            if (!indirect_ret) {
                emit_store(out, inst->result, "rax", fn, offsets, temps);
            }
            break;
        }

        case MIR_OP_RET: {
            MirOperand ret_val = null_op();
            if (inst->rhs.type != MIR_VAL_NONE) ret_val = inst->rhs;
            else if (inst->lhs.type != MIR_VAL_NONE) ret_val = inst->lhs;
            else if (inst->result.type != MIR_VAL_NONE) ret_val = inst->result;

            if (fn_returns_indirect(fn)) {
                if (ret_val.type != MIR_VAL_NONE) {
                    TypeRef *rt = ret_val.resolved_type ? ret_val.resolved_type : (fn->symbol ? fn->symbol->type->fn.ret_type : NULL);
                    size_t size = rt ? get_type_size(rt) : 0;
                    if (size == 0) size = 8;
                    size = align8(size);

                    fprintf(out, "    mov rdi, QWORD PTR [rbp%+ld]\n", ret_ptr_off);
                    emit_operand_addr(out, "rsi", fn, offsets, temps, ret_val);
                    emit_copy_aggregate(out, "rdi", "rsi", size);
                }

                fprintf(out, "    mov rsp, rbp\n");
                fprintf(out, "    pop rbp\n");
                fprintf(out, "    ret\n");
                break;
            }

            if (ret_val.type != MIR_VAL_NONE) {
                emit_load(out, "rax", fn, offsets, temps, ret_val);
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

static void emit_block_recursive(FILE *out, MirFunction *fn, MirBlock *block, SymbolOffset *offsets, TempLayout *temps, int64_t ret_ptr_off) {
    if (!block || block->visited) return;
    block->visited = true;

    fprintf(out, ".Lblock_%d:\n", block->id);
    for (MirInstr *inst = block->first; inst; inst = inst->next) {
        emit_instruction(out, fn, inst, offsets, temps, ret_ptr_off);
    }

    emit_block_recursive(out, fn, block->succ_true, offsets, temps, ret_ptr_off);
    emit_block_recursive(out, fn, block->succ_false, offsets, temps, ret_ptr_off);
}

__attribute__((visibility("default")))
void backend(FILE *out, MirBuilder *builder, MirModule *mod) {
    (void)builder;

    fprintf(out, ".section .rodata\n");
    for (size_t i = 0; i < mod->strings.len; i++) {
        String str = mod->strings.data[i];
        fprintf(out, ".Lstr_bytes_%zu:\n", i);
        fprintf(out, "    .string \"");
        emit_escaped_string(out, str);
        fprintf(out, "\"\n");
    }

    fprintf(out, "\n.section .data\n");
    for (size_t i = 0; i < mod->strings.len; i++) {
        String str = mod->strings.data[i];
        fprintf(out, "    .align 8\n");
        fprintf(out, ".Lstr_%zu:\n", i);
        fprintf(out, "    .quad %zu\n", str.length);
        fprintf(out, "    .quad .Lstr_bytes_%zu\n", i);
    }

    for (size_t i = 0; i < mod->globals.len; i++) {
        MirVarDecl *g = mod->globals.data[i];
        fprintf(out, "    .align 8\n");
        if (g->is_export) {
            fprintf(out, ".global %.*s\n", string_fmt(g->symbol->name));
        }
        fprintf(out, "%.*s:\n", string_fmt(g->symbol->name));

        if (g->init_vals.len > 0) {
            for (size_t j = 0; j < g->init_vals.len; j++) {
                MirInitVal *val = &g->init_vals.data[j];
                if (val->type == MIR_INIT_SYMBOL) {
                    fprintf(out, "    .quad %.*s\n", string_fmt(val->symbol_val->name));
                } else if (val->type == MIR_INIT_INT) {
                    fprintf(out, "    .quad %lld\n", (long long)val->int_val);
                } else if (val->type == MIR_INIT_ZERO) {
                    fprintf(out, "    .quad 0\n");
                }
            }
        } else {
            fprintf(out, "    .quad 0\n");
        }
    }

    fprintf(out, "\n.section .text\n");
    fprintf(out, ".intel_syntax noprefix\n");

    for (size_t f = 0; f < mod->functions.len; f++) {
        MirFunction *fn = mod->functions.data[f];
        if (fn->is_export) fprintf(out, ".global %.*s\n", string_fmt(fn->symbol->mangled));
        fprintf(out, "%.*s:\n", string_fmt(fn->symbol->mangled));

        size_t total_syms = fn->params.len + fn->locals.len;
        SymbolOffset *offsets = calloc(total_syms ? total_syms : 1, sizeof(SymbolOffset));
        TempLayout *temps = calloc(fn->temp_count ? fn->temp_count : 1, sizeof(TempLayout));
        size_t *temp_sizes = calloc(fn->temp_count ? fn->temp_count : 1, sizeof(size_t));

        scan_block_temp_sizes(fn->entry_block, temp_sizes);
        clear_block_marks(fn->entry_block);

        size_t total_stack_size = 0;
        int64_t ret_ptr_off = 0;
        compute_stack_layout(fn, offsets, temps, temp_sizes, &total_stack_size, &ret_ptr_off);

        fprintf(out, "    push rbp\n");
        fprintf(out, "    mov rbp, rsp\n");
        if (total_stack_size > 0) {
            fprintf(out, "    sub rsp, %zu\n", total_stack_size);
        }

        size_t reg_idx = fn_returns_indirect(fn) ? 1 : 0;
        if (fn_returns_indirect(fn)) {
            fprintf(out, "    mov QWORD PTR [rbp%+ld], rdi\n", ret_ptr_off);
        }

        for (size_t i = 0; i < fn->params.len && reg_idx < 6; i++) {
            Symbol *param_sym = fn->params.data[i];
            int64_t off = lookup_symbol_offset(fn, offsets, param_sym);
            size_t sz = param_sym->type ? get_type_size(param_sym->type) : 8;
            if (sz == 0) sz = 8;
            sz = align8(sz);

            if (sz > 8) {
                fprintf(out, "    mov QWORD PTR [rbp%s%ld], %s\n", off >= 0 ? "+" : "", off, ABI_ARG_REGS[reg_idx++]);
                if (reg_idx < 6) {
                    fprintf(out, "    mov QWORD PTR [rbp%s%ld], %s\n", (off + 8) >= 0 ? "+" : "", off + 8, ABI_ARG_REGS[reg_idx++]);
                }
            } else {
                fprintf(out, "    mov QWORD PTR [rbp%s%ld], %s\n", off >= 0 ? "+" : "", off, ABI_ARG_REGS[reg_idx++]);
            }
        }

        emit_block_recursive(out, fn, fn->entry_block, offsets, temps, ret_ptr_off);

        free(temp_sizes);
        free(temps);
        free(offsets);
        fprintf(out, "\n");
    }
}
