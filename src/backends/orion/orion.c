#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "orion.h"

static size_t type_size(HirType *type) {
    assert(type != NULL);
    return type->size;
}

static size_t type_align(HirType *type) {
    assert(type != NULL);
    return type->align;
}

static size_t align_up(size_t value, size_t align) {
    if (align == 0)
        return value;
    return (value + align - 1) & ~(align - 1);
}

static HirType *base_type(HirType *type) {
    while (type != NULL && type->base != NULL)
        type = type->base;
    return type;
}

static bool type_is_signed(HirType *type) {
    type = base_type(type);
    if (type != NULL && type->kind == HIR_TYPE_ENUM)
        type = base_type(type->_enum.underlying);
    if (type == NULL || type->kind != HIR_TYPE_BUILTIN)
        return false;

    switch (type->builtin) {
        case BUILTIN_INT8:
        case BUILTIN_INT16:
        case BUILTIN_INT32:
        case BUILTIN_INT64:
            return true;
        case BUILTIN_UINT8:
        case BUILTIN_UINT16:
        case BUILTIN_UINT32:
        case BUILTIN_UINT64:
        case BUILTIN_BOOL:
        case BUILTIN_NONE:
        case BUILTIN_FLOAT32:
        case BUILTIN_FLOAT64:
            return false;
    }

    assert(!"unhandled builtin type");
    return false;
}

static bool type_is_float(HirType *type) {
    type = base_type(type);
    return type != NULL && type->kind == HIR_TYPE_BUILTIN &&
           (type->builtin == BUILTIN_FLOAT32 || type->builtin == BUILTIN_FLOAT64);
}

static bool supports(const TargetInfo *target) {
    if (target == NULL)
        return false;

    return target->arch == TARGET_ARCH_ORION &&
           target->abi == TARGET_ABI_ORION &&
           target->endian == TARGET_ENDIAN_LITTLE &&
           target->pointer.size == 4 &&
           target->pointer.align == 4 &&
           (target->object == TARGET_OBJECT_NONE || target->object == TARGET_OBJECT_ELF32);
}

static void emit_global(FILE *out, const LirGlobal *global) {
    String name = symbol_link_name(global->symbol);
    const char *section = global->is_mutable ? ".data" : ".rodata";

    if (global->data.len == 0)
        section = ".bss";

    fprintf(out, "\n.section %s\n", section);
    if (global->is_export)
        fprintf(out, ".global %.*s\n", string_fmt(name));
    fprintf(out, "%.*s:\n", string_fmt(name));

    size_t offset = 0;
    for (size_t i = 0; i < global->data.len; i++) {
        const LirData *data = &((LirData *)global->data.data)[i];

        if (data->offset > offset)
            fprintf(out, "    .zero %zu\n", data->offset - offset);

        switch (data->kind) {
            case LIR_DATA_INTEGER: {
                size_t size = type_size(data->type);
                assert(size <= 4 && "Orion only supports 32-bit integer data");
                if (size == 1) {
                    fprintf(out, "    .byte %" PRIu32 "\n", (uint32_t)(uint8_t)data->integer);
                } else {
                    fprintf(out, "    .word %" PRIu32 "\n", (uint32_t)data->integer);
                }
                offset = data->offset + size;
                break;
            }

            case LIR_DATA_BOOL: {
                fprintf(out, "    .byte %d\n", data->boolean ? 1 : 0);
                offset = data->offset + 1;
                break;
            }

            case LIR_DATA_FLOAT: {
                assert(!"Orion backend does not implement float globals yet");
                break;
            }

            case LIR_DATA_BYTES: {
                for (size_t j = 0; j < data->bytes.length; j++)
                    fprintf(out, "    .byte %u\n", (unsigned char)data->bytes.data[j]);
                offset = data->offset + data->bytes.length;
                break;
            }

            case LIR_DATA_ADDRESS: {
                String symbol_name = symbol_link_name(data->symbol);
                fprintf(out, "    .word %.*s\n", string_fmt(symbol_name));
                offset = data->offset + type_size(data->type);
                break;
            }
        }
    }

    if (offset < type_size(global->type))
        fprintf(out, "    .zero %zu\n", type_size(global->type) - offset);
}

static void emit_string(FILE *out, const LirString *string) {
    String name = symbol_link_name(string->symbol);

    fprintf(out, "\n.section .rodata\n%.*s:\n", string_fmt(name));
    if (string->bytes.length == 0) {
        fprintf(out, "    .zero 1\n");
    } else {
        for (size_t i = 0; i < string->bytes.length; i++)
            fprintf(out, "    .byte %u\n", (unsigned char)string->bytes.data[i]);
    }
}

typedef struct {
    int64_t offset;
    size_t size;
} OrionSlot;

typedef struct {
    FILE *out;
    const TargetInfo *target;
    const LirFunction *function;
    size_t id;
    OrionSlot *slots;
    size_t slot_count;
    size_t frame_size;
    int64_t return_address_offset;
    int *value_regs;
    bool used_regs[8];
    size_t temp_reg_index;
    size_t next_label_id;
} OrionFunction;

static int reg_index_for_orion(int reg) {
    return reg - 4;
}

static int next_temp_reg(OrionFunction *function) {
    static const int regs[] = {0, 1, 2, 3, 12, 13, 14};
    int reg = regs[function->temp_reg_index % (sizeof(regs) / sizeof(regs[0]))];
    function->temp_reg_index++;
    return reg;
}

static int value_reg(OrionFunction *function, LirValueId value) {
    if (value >= function->slot_count)
        return -1;
    if (function->value_regs[value] == -1) {
        for (int reg = 4; reg <= 11; reg++) {
            int idx = reg_index_for_orion(reg);
            if (!function->used_regs[idx]) {
                function->used_regs[idx] = true;
                function->value_regs[value] = reg;
                return reg;
            }
        }

        int scratch = next_temp_reg(function);
        function->value_regs[value] = scratch;
        return scratch;
    }
    return function->value_regs[value];
}

static void emit_label_ref(OrionFunction *function, size_t id, FILE *out) {
    String name = symbol_link_name(function->function->symbol);
    fprintf(out, "%.*s_L%zu", string_fmt(name), id);
}

static size_t next_label(OrionFunction *function) {
    return function->next_label_id++;
}

static void emit_push_used_regs(OrionFunction *function) {
    bool first = true;
    for (int reg = 4; reg <= 11; reg++) {
        if (function->used_regs[reg_index_for_orion(reg)]) {
            if (first) {
                fprintf(function->out, "    push {r%d", reg);
                first = false;
            } else {
                fprintf(function->out, ", r%d", reg);
            }
        }
    }
    if (!first)
        fprintf(function->out, "}\n");
}

static void emit_pop_used_regs(OrionFunction *function) {
    bool first = true;
    for (int reg = 4; reg <= 11; reg++) {
        if (function->used_regs[reg_index_for_orion(reg)]) {
            if (first) {
                fprintf(function->out, "    pop {r%d", reg);
                first = false;
            } else {
                fprintf(function->out, ", r%d", reg);
            }
        }
    }
    if (!first)
        fprintf(function->out, "}\n");
}

static void reserve_value_reg(OrionFunction *function, LirValueId value) {
    if (value == LIR_INVALID_VALUE)
        return;
    if (value < function->slot_count && function->value_regs[value] == -1)
        value_reg(function, value);
}

static void allocate_slots(OrionFunction *function) {
    function->slot_count = function->function->next_value;
    function->value_regs = calloc(function->slot_count > 0 ? function->slot_count : 1, sizeof(*function->value_regs));
    assert(function->value_regs != NULL);
    for (size_t i = 0; i < function->slot_count; i++)
        function->value_regs[i] = -1;

    function->frame_size = 0;
    function->return_address_offset = 0;
    function->temp_reg_index = 0;
    function->next_label_id = function->function->blocks.len + 1;
    memset(function->used_regs, 0, sizeof(function->used_regs));

    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        for (size_t j = 0; j < block->instructions.len; j++) {
            LirInstruction *instruction = &((LirInstruction *)block->instructions.data)[j];
            if (instruction->result != LIR_INVALID_VALUE)
                reserve_value_reg(function, instruction->result);
            for (size_t k = 0; k < instruction->operands.len; k++) {
                LirOperand *operand = &((LirOperand *)instruction->operands.data)[k];
                if (operand->kind == LIR_OPERAND_VALUE)
                    reserve_value_reg(function, operand->value);
            }
        }

        if (block->terminator.kind == LIR_TERM_BRANCH) {
            LirOperand *cond = &block->terminator.branch.condition;
            if (cond->kind == LIR_OPERAND_VALUE)
                reserve_value_reg(function, cond->value);
        }
    }
}

static void emit_operand(OrionFunction *function, const LirOperand *operand, int destination_reg) {
    switch (operand->kind) {
        case LIR_OPERAND_INT:
            fprintf(function->out, "    mov r%d, #%d\n", destination_reg, (int32_t)operand->int_value);
            break;
        case LIR_OPERAND_UINT:
            fprintf(function->out, "    mov r%d, #%u\n", destination_reg, (uint32_t)operand->uint_value);
            break;
        case LIR_OPERAND_OFFSET:
            fprintf(function->out, "    mov r%d, #%zu\n", destination_reg, operand->offset);
            break;
        case LIR_OPERAND_BOOL:
            fprintf(function->out, "    mov r%d, #%d\n", destination_reg, operand->bool_value ? 1 : 0);
            break;
        case LIR_OPERAND_SYMBOL: {
            String name = symbol_link_name(operand->symbol);
            fprintf(function->out, "    mov r%d, %.*s\n", destination_reg, (int)name.length, name.data);
            break;
        }
        case LIR_OPERAND_VALUE: {
            int source = value_reg(function, operand->value);
            if (source == -1)
                fprintf(function->out, "    mov r%d, #0\n", destination_reg);
            else if (source != destination_reg)
                fprintf(function->out, "    mov r%d, r%d\n", destination_reg, source);
            break;
        }
        default:
            fprintf(function->out, "    mov r%d, #0\n", destination_reg);
            break;
    }
}

static void emit_instruction(OrionFunction *function, const LirInstruction *instruction) {
    int result_reg = instruction->result != LIR_INVALID_VALUE ? value_reg(function, instruction->result) : -1;
    LirOperand *operands = instruction->operands.data;

    switch (instruction->opcode) {
        case LIR_OP_NEG: {
            int src = next_temp_reg(function);
            emit_operand(function, &operands[0], src);
            fprintf(function->out, "    neg r%d, r%d\n", src, src);
            fprintf(function->out, "    mov r%d, r%d\n", result_reg, src);
            break;
        }
        case LIR_OP_NOT: {
            int src = next_temp_reg(function);
            emit_operand(function, &operands[0], src);
            fprintf(function->out, "    not r%d, r%d\n", src, src);
            fprintf(function->out, "    mov r%d, r%d\n", result_reg, src);
            break;
        }
        case LIR_OP_ADD:
        case LIR_OP_SUB:
        case LIR_OP_MUL:
        case LIR_OP_AND:
        case LIR_OP_OR:
        case LIR_OP_XOR:
        case LIR_OP_SHL:
        case LIR_OP_SHR:
        case LIR_OP_DIV: {
            const char *mnemonic = NULL;
            switch (instruction->opcode) {
                case LIR_OP_ADD: mnemonic = "add"; break;
                case LIR_OP_SUB: mnemonic = "sub"; break;
                case LIR_OP_MUL: mnemonic = "mul"; break;
                case LIR_OP_AND: mnemonic = "and"; break;
                case LIR_OP_OR: mnemonic = "or"; break;
                case LIR_OP_XOR: mnemonic = "xor"; break;
                case LIR_OP_SHL: mnemonic = "shl"; break;
                case LIR_OP_SHR: mnemonic = "shr"; break;
                case LIR_OP_DIV: mnemonic = "div"; break;
                default: break;
            }
            int lhs = next_temp_reg(function);
            int rhs = next_temp_reg(function);
            emit_operand(function, &operands[0], lhs);
            emit_operand(function, &operands[1], rhs);
            fprintf(function->out, "    %s r%d, r%d, r%d\n", mnemonic, lhs, lhs, rhs);
            fprintf(function->out, "    mov r%d, r%d\n", result_reg, lhs);
            break;
        }
        case LIR_OP_MOD: {
            int lhs = next_temp_reg(function);
            int rhs = next_temp_reg(function);
            int tmp = next_temp_reg(function);
            emit_operand(function, &operands[0], lhs);
            emit_operand(function, &operands[1], rhs);
            fprintf(function->out, "    div r%d, r%d, r%d\n", lhs, lhs, rhs);
            fprintf(function->out, "    mul r%d, r%d, r%d\n", tmp, lhs, rhs);
            fprintf(function->out, "    sub r%d, r%d, r%d\n", lhs, tmp, lhs);
            fprintf(function->out, "    mov r%d, r%d\n", result_reg, lhs);
            break;
        }
        case LIR_OP_LOAD: {
            int addr = next_temp_reg(function);
            emit_operand(function, &operands[0], addr);
            size_t size = type_size(instruction->result_type);
            if (size == 1)
                fprintf(function->out, "    ldrb r%d, [r%d]\n", result_reg, addr);
            else if (size == 2 || size == 4)
                fprintf(function->out, "    ldr r%d, [r%d]\n", result_reg, addr);
            else
                fprintf(function->out, "    mov r%d, #0\n", result_reg);
            break;
        }
        case LIR_OP_STORE: {
            int addr = next_temp_reg(function);
            int value = next_temp_reg(function);
            emit_operand(function, &operands[0], addr);
            emit_operand(function, &operands[1], value);
            size_t size = type_size(operands[1].type);
            if (size == 1)
                fprintf(function->out, "    strb r%d, [r%d]\n", value, addr);
            else if (size == 2 || size == 4)
                fprintf(function->out, "    str r%d, [r%d]\n", value, addr);
            break;
        }
        case LIR_OP_ADDR: {
            if (operands[0].kind == LIR_OPERAND_SYMBOL) {
                String name = symbol_link_name(operands[0].symbol);
                fprintf(function->out, "    mov r%d, %.*s\n", result_reg, (int)name.length, name.data);
            } else {
                emit_operand(function, &operands[0], result_reg);
            }
            break;
        }
        case LIR_OP_ADDR_ADD: {
            int base = next_temp_reg(function);
            int offset = next_temp_reg(function);
            emit_operand(function, &operands[0], base);
            emit_operand(function, &operands[1], offset);
            fprintf(function->out, "    add r%d, r%d, r%d\n", result_reg, base, offset);
            break;
        }
        case LIR_OP_ADDR_DIFF: {
            int lhs = next_temp_reg(function);
            int rhs = next_temp_reg(function);
            emit_operand(function, &operands[0], lhs);
            emit_operand(function, &operands[1], rhs);
            fprintf(function->out, "    sub r%d, r%d, r%d\n", result_reg, lhs, rhs);
            break;
        }
        case LIR_OP_LT:
        case LIR_OP_LE:
        case LIR_OP_GT:
        case LIR_OP_GE:
        case LIR_OP_EQ:
        case LIR_OP_NE: {
            int lhs = next_temp_reg(function);
            int rhs = next_temp_reg(function);
            size_t false_label = next_label(function);
            bool unsigned_compare = !type_is_signed(operands[0].type);

            emit_operand(function, &operands[0], lhs);
            emit_operand(function, &operands[1], rhs);
            fprintf(function->out, "    mov r%d, #0\n", result_reg);

            switch (instruction->opcode) {
                case LIR_OP_LT:
                    fprintf(function->out, "    %s r%d, r%d, ", unsigned_compare ? "jgeu" : "jge", lhs, rhs);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    break;
                case LIR_OP_LE:
                    fprintf(function->out, "    %s r%d, r%d, ", unsigned_compare ? "jltu" : "jlt", rhs, lhs);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    break;
                case LIR_OP_GT:
                    fprintf(function->out, "    %s r%d, r%d, ", unsigned_compare ? "jgeu" : "jge", rhs, lhs);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    break;
                case LIR_OP_GE:
                    fprintf(function->out, "    %s r%d, r%d, ", unsigned_compare ? "jltu" : "jlt", lhs, rhs);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    break;
                case LIR_OP_EQ:
                    fprintf(function->out, "    jne r%d, r%d, ", lhs, rhs);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    break;
                case LIR_OP_NE:
                    fprintf(function->out, "    jeq r%d, r%d, ", lhs, rhs);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    break;
                default:
                    break;
            }

            fprintf(function->out, "    mov r%d, #1\n", result_reg);
            emit_label_ref(function, false_label, function->out);
            fprintf(function->out, ":\n");
            break;
        }
        case LIR_OP_CALL: {
            size_t operand_count = instruction->operands.len;
            size_t arg_count = operand_count > 0 ? operand_count - 1 : 0;

            for (size_t i = 0; i < arg_count; i++) {
                if (i < 4) {
                    emit_operand(function, &operands[i + 1], (int)i);
                } else {
                    int tmp = next_temp_reg(function);
                    emit_operand(function, &operands[i + 1], tmp);
                    fprintf(function->out, "    push {r%d}\n", tmp);
                }
            }

            if (operand_count > 0 && operands[0].kind == LIR_OPERAND_SYMBOL) {
                String name = symbol_link_name(operands[0].symbol);
                fprintf(function->out, "    call $%.*s\n", (int)name.length, name.data);
            } else if (operand_count > 0) {
                int func_reg = next_temp_reg(function);
                emit_operand(function, &operands[0], func_reg);
                fprintf(function->out, "    callr r%d\n", func_reg);
            }

            if (result_reg != -1)
                fprintf(function->out, "    mov r%d, r0\n", result_reg);
            break;
        }
        case LIR_OP_EXTRACT:
        case LIR_OP_EXTRACT_DYNAMIC: {
            int base = next_temp_reg(function);
            int offset = next_temp_reg(function);
            int addr = next_temp_reg(function);
            emit_operand(function, &operands[0], base);
            emit_operand(function, &operands[1], offset);
            fprintf(function->out, "    add r%d, r%d, r%d\n", addr, base, offset);
            size_t size = type_size(instruction->result_type);
            if (size == 1)
                fprintf(function->out, "    ldrb r%d, [r%d]\n", result_reg, addr);
            else if (size == 2 || size == 4)
                fprintf(function->out, "    ldr r%d, [r%d]\n", result_reg, addr);
            else
                fprintf(function->out, "    mov r%d, #0\n", result_reg);
            break;
        }
        case LIR_OP_INSERT:
        case LIR_OP_INSERT_DYNAMIC: {
            int base = next_temp_reg(function);
            int offset = next_temp_reg(function);
            int value = next_temp_reg(function);
            int addr = next_temp_reg(function);
            emit_operand(function, &operands[0], base);
            emit_operand(function, &operands[1], offset);
            emit_operand(function, &operands[2], value);
            fprintf(function->out, "    add r%d, r%d, r%d\n", addr, base, offset);
            size_t size = type_size(operands[2].type);
            if (size == 1)
                fprintf(function->out, "    strb r%d, [r%d]\n", value, addr);
            else if (size == 2 || size == 4)
                fprintf(function->out, "    str r%d, [r%d]\n", value, addr);
            break;
        }
        case LIR_OP_STACK_SAVE: {
            fprintf(function->out, "    mov r%d, r15\n", result_reg);
            break;
        }
        case LIR_OP_STACK_RESTORE: {
            int sp = next_temp_reg(function);
            emit_operand(function, &operands[0], sp);
            fprintf(function->out, "    mov r15, r%d\n", sp);
            break;
        }
        case LIR_OP_STACK_ALLOC: {
            int count = next_temp_reg(function);
            int bytes = next_temp_reg(function);
            emit_operand(function, &operands[0], count);
            emit_operand(function, &operands[1], bytes);
            fprintf(function->out, "    mul r%d, r%d\n", count, bytes);
            fprintf(function->out, "    sub r15, r%d\n", count);
            fprintf(function->out, "    mov r%d, r15\n", result_reg);
            break;
        }
        case LIR_OP_CAST:
        case LIR_OP_ZERO: {
            if (instruction->opcode == LIR_OP_ZERO) {
                fprintf(function->out, "    mov r%d, #0\n", result_reg);
            } else {
                emit_operand(function, &operands[0], result_reg);
            }
            break;
        }
        default:
            break;
    }
}

static void emit_block_label(OrionFunction *function, size_t block_index) {
    emit_label_ref(function, block_index, function->out);
    fprintf(function->out, ":\n");
}

static void emit_terminator(OrionFunction *function, LirBlock *block, size_t block_index) {
    switch (block->terminator.kind) {
        case LIR_TERM_JUMP: {
            fprintf(function->out, "    jmp ");
            emit_label_ref(function, block->terminator.jump.target, function->out);
            fprintf(function->out, "\n");
            break;
        }
        case LIR_TERM_BRANCH: {
            int cond_reg = -1;
            if (block->terminator.branch.condition.kind == LIR_OPERAND_VALUE)
                cond_reg = value_reg(function, block->terminator.branch.condition.value);
            if (cond_reg < 0) {
                fprintf(function->out, "    mov r0, #0\n");
                cond_reg = 0;
            }
            fprintf(function->out, "    jeq r%d, #0, ", cond_reg);
            emit_label_ref(function, block->terminator.branch.else_block, function->out);
            fprintf(function->out, "\n");
            fprintf(function->out, "    jmp ");
            emit_label_ref(function, block->terminator.branch.then_block, function->out);
            fprintf(function->out, "\n");
            break;
        }
        case LIR_TERM_RETURN: {
            if (block->terminator._return.has_value) {
                LirOperand *operand = &block->terminator._return.value;
                emit_operand(function, operand, 0);
            }
            if (function->slot_count > 0)
                emit_pop_used_regs(function);
            fprintf(function->out, "    ret\n");
            break;
        }
        case LIR_TERM_NONE:
        case LIR_TERM_UNREACHABLE:
            break;
    }
}

static void emit_function(OrionFunction *function) {
    String name = symbol_link_name(function->function->symbol);

    fprintf(function->out, "\n.section .text\n");
    if (function->function->is_export)
        fprintf(function->out, ".global %.*s\n", string_fmt(name));
    fprintf(function->out, "%.*s:\n", string_fmt(name));

    if (function->slot_count > 0)
        emit_push_used_regs(function);

    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        emit_block_label(function, i);
        for (size_t j = 0; j < block->instructions.len; j++)
            emit_instruction(function, &((LirInstruction *)block->instructions.data)[j]);
        emit_terminator(function, block, i);
    }
}

static bool emit(Backend *backend, const TargetInfo *target, const LirModule *module, FILE *output) {
    (void)backend;

    if (!supports(target))
        return false;

    for (size_t i = 0; i < module->globals.len; i++)
        emit_global(output, &((LirGlobal *)module->globals.data)[i]);

    for (size_t i = 0; i < module->strings.len; i++)
        emit_string(output, &((LirString *)module->strings.data)[i]);

    for (size_t i = 0; i < module->functions.len; i++) {
        const LirFunction *function = ((LirFunction **)module->functions.data)[i];

        if (function->is_extern)
            continue;

        OrionFunction fn = {
            .out = output,
            .target = target,
            .function = function,
            .id = i,
        };

        allocate_slots(&fn);
        emit_function(&fn);
        free(fn.value_regs);
        free(fn.slots);
    }

    return true;
}

static const BackendApi backend_api = {
    .abi_version = CODA_BACKEND_ABI_VERSION,
    .lir_version = CODA_LIR_ABI_VERSION,
    .name = "orion",
    .supports = supports,
    .emit = emit,
    .destroy = NULL,
};

const BackendApi *orion_backend(void) {
    return &backend_api;
}
