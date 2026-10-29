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

static bool type_is_supported_orion_scalar(HirType *type) {
    type = base_type(type);
    if (type == NULL)
        return false;

    if (type->kind == HIR_TYPE_POINTER)
        return type->size == 4 && type->align == 4;

    if (type_is_float(type))
        return false;

    if (type->kind == HIR_TYPE_ENUM)
        return type_is_supported_orion_scalar(type->_enum.underlying);

    if (type->kind != HIR_TYPE_BUILTIN)
        return false;

    switch (type->builtin) {
        case BUILTIN_INT8:
        case BUILTIN_INT16:
        case BUILTIN_INT32:
        case BUILTIN_INT64:
        case BUILTIN_UINT8:
        case BUILTIN_UINT16:
        case BUILTIN_UINT32:
        case BUILTIN_UINT64:
        case BUILTIN_BOOL:
            return type->size <= 8 && type->align <= 8;
        default:
            return false;
    }
}

static bool type_is_supported_orion_aggregate(HirType *type) {
    type = base_type(type);
    if (type == NULL)
        return false;

    switch (type->kind) {
        case HIR_TYPE_STRUCT:
        case HIR_TYPE_UNION: {
            size_t offset = 0;
            for (size_t i = 0; i < type->structure.fields.len; i++) {
                HirField *field = &((HirField *)type->structure.fields.data)[i];
                if (!type_is_supported_orion_scalar(field->type) && !type_is_supported_orion_aggregate(field->type))
                    return false;
                offset = align_up(offset, type_align(field->type));
                offset += type_size(field->type);
            }
            return type->size <= 16 && type->align <= 8;
        }
        default:
            return false;
    }
}

static bool type_is_supported_orion_value(HirType *type) {
    return type_is_supported_orion_scalar(type) || type_is_supported_orion_aggregate(type);
}

static bool type_is_supported_orion_global(HirType *type) {
    if (type == NULL)
        return false;
    if (type->kind == HIR_TYPE_POINTER)
        return type->size == 4 && type->align == 4;
    if (type_is_float(type))
        return false;
    if (type->kind == HIR_TYPE_ENUM)
        return type_is_supported_orion_global(type->_enum.underlying);
    if (type->kind != HIR_TYPE_BUILTIN)
        return false;
    switch (type->builtin) {
        case BUILTIN_INT8:
        case BUILTIN_INT16:
        case BUILTIN_INT32:
        case BUILTIN_UINT8:
        case BUILTIN_UINT16:
        case BUILTIN_UINT32:
        case BUILTIN_BOOL:
            return type->size <= 4 && type->align <= 4;
        default:
            return false;
    }
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

static bool emit_global(FILE *out, const LirGlobal *global) {
    String name = symbol_link_name(global->symbol);
    const char *section = global->is_mutable ? ".data" : ".rodata";

    if (!type_is_supported_orion_global(global->type)) {
        fprintf(stderr, "Orion backend: unsupported global type for %.*s\n", (int)name.length, name.data);
        return false;
    }

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
                if (size > 4) {
                    fprintf(stderr, "Orion backend: unsupported integer global size %zu\n", size);
                    return false;
                }
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
                fprintf(stderr, "Orion backend: floating-point globals are not supported\n");
                return false;
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

    return true;
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
    HirType **value_types;
    size_t temp_reg_index;
    size_t next_label_id;
} OrionFunction;

static int next_temp_reg(OrionFunction *function) {
    static const int regs[] = {0, 1, 2, 3, 12, 13, 14};
    int reg = regs[function->temp_reg_index % (sizeof(regs) / sizeof(regs[0]))];
    function->temp_reg_index++;
    return reg;
}

static void next_temp_reg_pair(OrionFunction *function, int *low_reg, int *high_reg) {
    static const int regs[] = {0, 1, 2, 3, 12, 13, 14};
    size_t index = function->temp_reg_index % (sizeof(regs) / sizeof(regs[0]));
    *low_reg = regs[index];
    *high_reg = regs[(index + 1) % (sizeof(regs) / sizeof(regs[0]))];
    function->temp_reg_index += 2;
}

static size_t value_slot_size(OrionFunction *function, LirValueId value) {
    if (value >= function->slot_count)
        return 0;
    if (function->value_types[value] == NULL)
        return 4;
    return type_size(function->value_types[value]);
}

static int64_t value_slot_offset(OrionFunction *function, LirValueId value) {
    if (value >= function->slot_count)
        return -1;
    return function->slots[value].offset;
}

static void emit_label_ref(OrionFunction *function, size_t id, FILE *out) {
    String name = symbol_link_name(function->function->symbol);
    fprintf(out, "%.*s_L%zu", string_fmt(name), id);
}

static size_t next_label(OrionFunction *function) {
    return function->next_label_id++;
}

static void emit_stack_load(OrionFunction *function, LirValueId value, int destination_reg) {
    int64_t offset = value_slot_offset(function, value);
    if (offset < 0) {
        fprintf(function->out, "    mov r%d, #0\n", destination_reg);
        return;
    }
    fprintf(function->out, "    ldr r%d, [r15, #%ld]\n", destination_reg, offset);
}

static void emit_stack_load64(OrionFunction *function, LirValueId value, int low_reg, int high_reg) {
    int64_t offset = value_slot_offset(function, value);
    if (offset < 0) {
        fprintf(function->out, "    mov r%d, #0\n", low_reg);
        fprintf(function->out, "    mov r%d, #0\n", high_reg);
        return;
    }
    fprintf(function->out, "    ldr r%d, [r15, #%ld]\n", low_reg, offset);
    fprintf(function->out, "    ldr r%d, [r15, #%ld]\n", high_reg, offset + 4);
}

static void emit_stack_store(OrionFunction *function, LirValueId value, int source_reg) {
    int64_t offset = value_slot_offset(function, value);
    if (offset < 0)
        return;
    fprintf(function->out, "    str r%d, [r15, #%ld]\n", source_reg, offset);
}

static void emit_stack_store64(OrionFunction *function, LirValueId value, int low_reg, int high_reg) {
    int64_t offset = value_slot_offset(function, value);
    if (offset < 0)
        return;
    fprintf(function->out, "    str r%d, [r15, #%ld]\n", low_reg, offset);
    fprintf(function->out, "    str r%d, [r15, #%ld]\n", high_reg, offset + 4);
}

static bool emit_operand(OrionFunction *function, const LirOperand *operand, int destination_reg) {
    switch (operand->kind) {
        case LIR_OPERAND_INT:
            fprintf(function->out, "    mov r%d, #%d\n", destination_reg, (int32_t)operand->int_value);
            return true;
        case LIR_OPERAND_UINT:
            fprintf(function->out, "    mov r%d, #%u\n", destination_reg, (uint32_t)operand->uint_value);
            return true;
        case LIR_OPERAND_OFFSET:
            fprintf(function->out, "    mov r%d, #%zu\n", destination_reg, operand->offset);
            return true;
        case LIR_OPERAND_BOOL:
            fprintf(function->out, "    mov r%d, #%d\n", destination_reg, operand->bool_value ? 1 : 0);
            return true;
        case LIR_OPERAND_SYMBOL: {
            String name = symbol_link_name(operand->symbol);
            fprintf(function->out, "    mov r%d, %.*s\n", destination_reg, (int)name.length, name.data);
            return true;
        }
        case LIR_OPERAND_VALUE: {
            if (type_is_supported_orion_aggregate(operand->type)) {
                int64_t offset = value_slot_offset(function, operand->value);
                if (offset < 0) {
                    fprintf(function->out, "    mov r%d, #0\n", destination_reg);
                    return true;
                }
                fprintf(function->out, "    mov r%d, #%ld\n", destination_reg, offset);
                fprintf(function->out, "    add r%d, r15, r%d\n", destination_reg, destination_reg);
                return true;
            }
            if (!type_is_supported_orion_scalar(operand->type)) {
                fprintf(stderr, "Orion backend: unsupported value type for operand\n");
                return false;
            }
            if (type_size(operand->type) == 8) {
                int high_reg = destination_reg == 14 ? 0 : destination_reg + 1;
                emit_stack_load64(function, operand->value, destination_reg, high_reg);
                return true;
            }
            emit_stack_load(function, operand->value, destination_reg);
            return true;
        }
        default:
            fprintf(stderr, "Orion backend: unsupported operand kind in emission\n");
            return false;
    }
}

static void allocate_slots(OrionFunction *function) {
    function->slot_count = function->function->next_value;
    function->slots = calloc(function->slot_count > 0 ? function->slot_count : 1, sizeof(*function->slots));
    function->value_types = calloc(function->slot_count > 0 ? function->slot_count : 1, sizeof(*function->value_types));
    assert(function->slots != NULL);
    assert(function->value_types != NULL);

    function->frame_size = 0;
    function->temp_reg_index = 0;
    function->next_label_id = function->function->blocks.len + 1;

    for (size_t i = 0; i < function->function->params.len; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)function->function->params.data)[i];
        function->value_types[param->value] = param->type;
    }

    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        for (size_t j = 0; j < block->instructions.len; j++) {
            LirInstruction *instruction = &((LirInstruction *)block->instructions.data)[j];
            if (instruction->result != LIR_INVALID_VALUE)
                function->value_types[instruction->result] = instruction->result_type;
            for (size_t k = 0; k < instruction->operands.len; k++) {
                LirOperand *operand = &((LirOperand *)instruction->operands.data)[k];
                if (operand->kind == LIR_OPERAND_VALUE)
                    function->value_types[operand->value] = operand->type;
            }
        }

        if (block->terminator.kind == LIR_TERM_BRANCH) {
            LirOperand *cond = &block->terminator.branch.condition;
            if (cond->kind == LIR_OPERAND_VALUE)
                function->value_types[cond->value] = cond->type;
        }
    }

    for (size_t i = 0; i < function->slot_count; i++) {
        size_t size = function->value_types[i] != NULL ? value_slot_size(function, i) : 4;
        if (size == 0)
            size = 4;
        function->slots[i].size = size;
        function->slots[i].offset = (int64_t)align_up(function->frame_size, 4);
        function->frame_size = align_up(function->frame_size + size, 4);
    }
}

static bool orion_value_type_supported(HirType *type) {
    return type_is_supported_orion_scalar(type) || type_is_supported_orion_aggregate(type);
}

static bool emit_instruction(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    if (instruction->result != LIR_INVALID_VALUE && function->value_types[instruction->result] != NULL && !orion_value_type_supported(function->value_types[instruction->result])) {
        fprintf(stderr, "Orion backend: unsupported instruction result type (opcode=%d, result=%u, kind=%d, size=%zu, align=%zu)\n",
                instruction->opcode,
                instruction->result,
                function->value_types[instruction->result] != NULL ? (int)function->value_types[instruction->result]->kind : -1,
                function->value_types[instruction->result] != NULL ? function->value_types[instruction->result]->size : 0,
                function->value_types[instruction->result] != NULL ? function->value_types[instruction->result]->align : 0);
        return false;
    }

    switch (instruction->opcode) {
        case LIR_OP_NEG: {
            int src = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], src))
                return false;
            fprintf(function->out, "    neg r%d, r%d\n", src, src);
            if (instruction->result != LIR_INVALID_VALUE)
                emit_stack_store(function, instruction->result, src);
            return true;
        }
        case LIR_OP_NOT: {
            int src = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], src))
                return false;
            fprintf(function->out, "    not r%d, r%d\n", src, src);
            if (instruction->result != LIR_INVALID_VALUE)
                emit_stack_store(function, instruction->result, src);
            return true;
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
            if (!emit_operand(function, &operands[0], lhs) || !emit_operand(function, &operands[1], rhs))
                return false;
            fprintf(function->out, "    %s r%d, r%d, r%d\n", mnemonic, lhs, lhs, rhs);
            if (instruction->result != LIR_INVALID_VALUE)
                emit_stack_store(function, instruction->result, lhs);
            return true;
        }
        case LIR_OP_MOD: {
            int lhs = next_temp_reg(function);
            int rhs = next_temp_reg(function);
            int tmp = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], lhs) || !emit_operand(function, &operands[1], rhs))
                return false;
            fprintf(function->out, "    div r%d, r%d, r%d\n", lhs, lhs, rhs);
            fprintf(function->out, "    mul r%d, r%d, r%d\n", tmp, lhs, rhs);
            fprintf(function->out, "    sub r%d, r%d, r%d\n", lhs, tmp, lhs);
            if (instruction->result != LIR_INVALID_VALUE)
                emit_stack_store(function, instruction->result, lhs);
            return true;
        }
        case LIR_OP_LOAD: {
            int addr = next_temp_reg(function);
            int value = next_temp_reg(function);
            int value_hi = next_temp_reg(function);
            size_t size = type_size(instruction->result_type);
            if (size > 8 || !type_is_supported_orion_value(instruction->result_type)) {
                fprintf(stderr, "Orion backend: unsupported load size\n");
                return false;
            }
            if (!emit_operand(function, &operands[0], addr))
                return false;
            if (size == 8) {
                int high_reg = value == 14 ? 0 : value + 1;
                fprintf(function->out, "    ldr r%d, [r%d]\n", value, addr);
                fprintf(function->out, "    ldr r%d, [r%d, #4]\n", high_reg, addr);
                emit_stack_store64(function, instruction->result, value, high_reg);
                return true;
            }
            if (size == 1)
                fprintf(function->out, "    ldrb r%d, [r%d]\n", value, addr);
            else
                fprintf(function->out, "    ldr r%d, [r%d]\n", value, addr);
            emit_stack_store(function, instruction->result, value);
            return true;
        }
        case LIR_OP_STORE: {
            int addr = next_temp_reg(function);
            int value = next_temp_reg(function);
            size_t size = type_size(operands[1].type);
            if (size > 8 || !type_is_supported_orion_value(operands[1].type)) {
                fprintf(stderr, "Orion backend: unsupported store size\n");
                return false;
            }
            if (!emit_operand(function, &operands[0], addr) || !emit_operand(function, &operands[1], value))
                return false;
            if (size == 8) {
                int high_reg = value == 14 ? 0 : value + 1;
                fprintf(function->out, "    str r%d, [r%d]\n", value, addr);
                fprintf(function->out, "    str r%d, [r%d, #4]\n", high_reg, addr);
                return true;
            }
            if (size == 1)
                fprintf(function->out, "    strb r%d, [r%d]\n", value, addr);
            else
                fprintf(function->out, "    str r%d, [r%d]\n", value, addr);
            return true;
        }
        case LIR_OP_ADDR: {
            if (operands[0].kind == LIR_OPERAND_SYMBOL) {
                String name = symbol_link_name(operands[0].symbol);
                fprintf(function->out, "    mov r0, %.*s\n", (int)name.length, name.data);
                emit_stack_store(function, instruction->result, 0);
            } else {
                int tmp = next_temp_reg(function);
                if (!emit_operand(function, &operands[0], tmp))
                    return false;
                emit_stack_store(function, instruction->result, tmp);
            }
            return true;
        }
        case LIR_OP_ADDR_ADD: {
            int base = next_temp_reg(function);
            int offset = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], base) || !emit_operand(function, &operands[1], offset))
                return false;
            fprintf(function->out, "    add r%d, r%d, r%d\n", base, base, offset);
            emit_stack_store(function, instruction->result, base);
            return true;
        }
        case LIR_OP_ADDR_DIFF: {
            int lhs = next_temp_reg(function);
            int rhs = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], lhs) || !emit_operand(function, &operands[1], rhs))
                return false;
            fprintf(function->out, "    sub r%d, r%d, r%d\n", lhs, lhs, rhs);
            emit_stack_store(function, instruction->result, lhs);
            return true;
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

            if (type_size(operands[0].type) == 8 || type_size(operands[1].type) == 8) {
                int lhs_lo, lhs_hi, rhs_lo, rhs_hi;
                next_temp_reg_pair(function, &lhs_lo, &lhs_hi);
                next_temp_reg_pair(function, &rhs_lo, &rhs_hi);
                if (!emit_operand(function, &operands[0], lhs_lo) || !emit_operand(function, &operands[1], rhs_lo))
                    return false;
                if (lhs_lo == 14) lhs_hi = 0; else lhs_hi = lhs_lo + 1;
                if (rhs_lo == 14) rhs_hi = 0; else rhs_hi = rhs_lo + 1;
                fprintf(function->out, "    mov r0, #0\n");
                if (instruction->opcode == LIR_OP_EQ || instruction->opcode == LIR_OP_NE) {
                    fprintf(function->out, "    jne r%d, r%d, ", lhs_lo, rhs_lo);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    fprintf(function->out, "    jne r%d, r%d, ", lhs_hi, rhs_hi);
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, "\n");
                    if (instruction->opcode == LIR_OP_NE) {
                        fprintf(function->out, "    mov r0, #1\n");
                    } else {
                        fprintf(function->out, "    mov r0, #1\n");
                    }
                    emit_label_ref(function, false_label, function->out);
                    fprintf(function->out, ":\n");
                    if (instruction->result != LIR_INVALID_VALUE)
                        emit_stack_store(function, instruction->result, 0);
                    return true;
                }
            }

            if (!emit_operand(function, &operands[0], lhs) || !emit_operand(function, &operands[1], rhs))
                return false;
            fprintf(function->out, "    mov r0, #0\n");

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

            fprintf(function->out, "    mov r0, #1\n");
            emit_label_ref(function, false_label, function->out);
            fprintf(function->out, ":\n");
            if (instruction->result != LIR_INVALID_VALUE)
                emit_stack_store(function, instruction->result, 0);
            return true;
        }
        case LIR_OP_CALL: {
            size_t operand_count = instruction->operands.len;
            size_t arg_count = operand_count > 0 ? operand_count - 1 : 0;
            for (size_t i = 0; i < arg_count; i++) {
                if (i < 4) {
                    if (!emit_operand(function, &operands[i + 1], (int)i))
                        return false;
                } else {
                    int tmp = next_temp_reg(function);
                    if (!emit_operand(function, &operands[i + 1], tmp))
                        return false;
                    fprintf(function->out, "    push {r%d}\n", tmp);
                }
            }

            if (operand_count > 0 && operands[0].kind == LIR_OPERAND_SYMBOL) {
                String name = symbol_link_name(operands[0].symbol);
                fprintf(function->out, "    call $%.*s\n", (int)name.length, name.data);
            } else if (operand_count > 0) {
                int func_reg = next_temp_reg(function);
                if (!emit_operand(function, &operands[0], func_reg))
                    return false;
                fprintf(function->out, "    callr r%d\n", func_reg);
            }

            if (instruction->result != LIR_INVALID_VALUE) {
                if (type_size(function->value_types[instruction->result]) == 8) {
                    emit_stack_store64(function, instruction->result, 0, 1);
                } else if (type_is_supported_orion_aggregate(function->value_types[instruction->result])) {
                    size_t aggregate_size = type_size(function->value_types[instruction->result]);
                    for (size_t i = 0; i < aggregate_size; i += 4) {
                        int reg = (int)(i / 4);
                        fprintf(function->out, "    str r%d, [r15, #%ld]\n", reg, value_slot_offset(function, instruction->result) + (int64_t)i);
                    }
                } else {
                    emit_stack_store(function, instruction->result, 0);
                }
            }
            return true;
        }
        case LIR_OP_EXTRACT:
        case LIR_OP_EXTRACT_DYNAMIC: {
            int base = next_temp_reg(function);
            int offset = next_temp_reg(function);
            int addr = next_temp_reg(function);
            int value = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], base) || !emit_operand(function, &operands[1], offset))
                return false;
            fprintf(function->out, "    add r%d, r%d, r%d\n", addr, base, offset);
            if (type_size(instruction->result_type) == 8) {
                int high_reg = value == 14 ? 0 : value + 1;
                fprintf(function->out, "    ldr r%d, [r%d]\n", value, addr);
                fprintf(function->out, "    ldr r%d, [r%d, #4]\n", high_reg, addr);
                emit_stack_store64(function, instruction->result, value, high_reg);
                return true;
            }
            if (type_size(instruction->result_type) == 1)
                fprintf(function->out, "    ldrb r%d, [r%d]\n", value, addr);
            else
                fprintf(function->out, "    ldr r%d, [r%d]\n", value, addr);
            emit_stack_store(function, instruction->result, value);
            return true;
        }
        case LIR_OP_INSERT:
        case LIR_OP_INSERT_DYNAMIC: {
            int base = next_temp_reg(function);
            int offset = next_temp_reg(function);
            int value = next_temp_reg(function);
            int addr = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], base) ||
                !emit_operand(function, &operands[1], offset) ||
                !emit_operand(function, &operands[2], value))
                return false;
            fprintf(function->out, "    add r%d, r%d, r%d\n", addr, base, offset);
            if (type_size(operands[2].type) == 8) {
                int high_reg = value == 14 ? 0 : value + 1;
                fprintf(function->out, "    str r%d, [r%d]\n", value, addr);
                fprintf(function->out, "    str r%d, [r%d, #4]\n", high_reg, addr);
                return true;
            }
            if (type_size(operands[2].type) == 1)
                fprintf(function->out, "    strb r%d, [r%d]\n", value, addr);
            else
                fprintf(function->out, "    str r%d, [r%d]\n", value, addr);
            return true;
        }
        case LIR_OP_STACK_SAVE: {
            fprintf(function->out, "    mov r0, r15\n");
            if (instruction->result != LIR_INVALID_VALUE)
                emit_stack_store(function, instruction->result, 0);
            return true;
        }
        case LIR_OP_STACK_RESTORE: {
            int sp = next_temp_reg(function);
            if (!emit_operand(function, &operands[0], sp))
                return false;
            fprintf(function->out, "    mov r15, r%d\n", sp);
            return true;
        }
        case LIR_OP_STACK_ALLOC: {
            fprintf(stderr, "Orion backend: stack allocation is not supported in the initial backend\n");
            return false;
        }
        case LIR_OP_CAST:
        case LIR_OP_ZERO: {
            if (instruction->opcode == LIR_OP_ZERO) {
                if (instruction->result_type != NULL && type_size(instruction->result_type) == 8) {
                    fprintf(function->out, "    mov r0, #0\n");
                    fprintf(function->out, "    mov r1, #0\n");
                    if (instruction->result != LIR_INVALID_VALUE)
                        emit_stack_store64(function, instruction->result, 0, 1);
                } else {
                    fprintf(function->out, "    mov r0, #0\n");
                    if (instruction->result != LIR_INVALID_VALUE)
                        emit_stack_store(function, instruction->result, 0);
                }
            } else {
                int tmp = next_temp_reg(function);
                if (!emit_operand(function, &operands[0], tmp))
                    return false;
                if (type_size(instruction->result_type) == 8) {
                    int high_reg = tmp == 14 ? 0 : tmp + 1;
                    emit_stack_store64(function, instruction->result, tmp, high_reg);
                } else {
                    emit_stack_store(function, instruction->result, tmp);
                }
            }
            return true;
        }
        default:
            fprintf(stderr, "Orion backend: unsupported LIR opcode %d\n", instruction->opcode);
            return false;
    }
}

static void emit_block_label(OrionFunction *function, size_t block_index) {
    emit_label_ref(function, block_index, function->out);
    fprintf(function->out, ":\n");
}

static bool emit_terminator(OrionFunction *function, LirBlock *block, size_t block_index) {
    switch (block->terminator.kind) {
        case LIR_TERM_JUMP: {
            fprintf(function->out, "    jmp ");
            emit_label_ref(function, block->terminator.jump.target, function->out);
            fprintf(function->out, "\n");
            return true;
        }
        case LIR_TERM_BRANCH: {
            int cond_reg = 0;
            if (block->terminator.branch.condition.kind == LIR_OPERAND_VALUE) {
                if (!emit_operand(function, &block->terminator.branch.condition, cond_reg))
                    return false;
            } else if (!emit_operand(function, &block->terminator.branch.condition, cond_reg)) {
                return false;
            }
            fprintf(function->out, "    jeq r%d, #0, ", cond_reg);
            emit_label_ref(function, block->terminator.branch.else_block, function->out);
            fprintf(function->out, "\n");
            fprintf(function->out, "    jmp ");
            emit_label_ref(function, block->terminator.branch.then_block, function->out);
            fprintf(function->out, "\n");
            return true;
        }
        case LIR_TERM_RETURN: {
            if (block->terminator._return.has_value) {
                LirOperand *operand = &block->terminator._return.value;
                if (!emit_operand(function, operand, 0))
                    return false;
            }
            if (function->frame_size > 0) {
                fprintf(function->out, "    mov r12, #%zu\n", function->frame_size);
                fprintf(function->out, "    add r15, r12\n");
            }
            fprintf(function->out, "    ret\n");
            return true;
        }
        case LIR_TERM_NONE:
        case LIR_TERM_UNREACHABLE:
            return true;
        default:
            return false;
    }
}

static bool emit_function(OrionFunction *function) {
    String name = symbol_link_name(function->function->symbol);

    fprintf(function->out, "\n.section .text\n");
    if (function->function->is_export)
        fprintf(function->out, ".global %.*s\n", string_fmt(name));
    fprintf(function->out, "%.*s:\n", string_fmt(name));

    if (function->frame_size > 0) {
        fprintf(function->out, "    mov r12, #%zu\n", function->frame_size);
        fprintf(function->out, "    sub r15, r12\n");
    }

    for (size_t i = 0; i < function->function->params.len; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)function->function->params.data)[i];
        int reg = (int)i;
        if (reg >= 4)
            reg = 0;
        fprintf(function->out, "    str r%d, [r15, #%ld]\n", reg, value_slot_offset(function, param->value));
    }

    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        emit_block_label(function, i);
        for (size_t j = 0; j < block->instructions.len; j++) {
            if (!emit_instruction(function, &((LirInstruction *)block->instructions.data)[j]))
                return false;
        }
        if (!emit_terminator(function, block, i))
            return false;
    }

    return true;
}

static bool emit(Backend *backend, const TargetInfo *target, const LirModule *module, FILE *output) {
    (void)backend;

    if (!supports(target))
        return false;

    char *buffer = NULL;
    size_t buffer_size = 0;
    FILE *tmp = open_memstream(&buffer, &buffer_size);
    if (tmp == NULL)
        return false;

    for (size_t i = 0; i < module->globals.len; i++) {
        if (!emit_global(tmp, &((LirGlobal *)module->globals.data)[i])) {
            fclose(tmp);
            free(buffer);
            return false;
        }
    }

    for (size_t i = 0; i < module->strings.len; i++)
        emit_string(tmp, &((LirString *)module->strings.data)[i]);

    for (size_t i = 0; i < module->functions.len; i++) {
        const LirFunction *function = ((LirFunction **)module->functions.data)[i];

        if (function->is_extern)
            continue;

        OrionFunction fn = {
            .out = tmp,
            .target = target,
            .function = function,
            .id = i,
        };

        allocate_slots(&fn);
        if (!emit_function(&fn)) {
            free(fn.value_types);
            free(fn.slots);
            fclose(tmp);
            free(buffer);
            return false;
        }
        free(fn.value_types);
        free(fn.slots);
    }

    if (fflush(tmp) != 0) {
        fclose(tmp);
        free(buffer);
        return false;
    }

    if (fwrite(buffer, 1, buffer_size, output) != buffer_size) {
        fclose(tmp);
        free(buffer);
        return false;
    }

    fclose(tmp);
    free(buffer);
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
