#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "orion.h"

enum {
    ORION_R0 = 0,
    ORION_R1 = 1,
    ORION_R2 = 2,
    ORION_R3 = 3,
    ORION_R12 = 12,
    ORION_R13 = 13,
    ORION_R14 = 14,
    ORION_FP = 11,
    ORION_SP = 15,
};

typedef struct {
    size_t offset;
    size_t storage_size;
} OrionSlot;

typedef struct {
    FILE *out;
    const TargetInfo *target;
    const LirFunction *function;
    size_t function_index;

    OrionSlot *slots;
    HirType **value_types;
    size_t slot_count;
    size_t frame_size;

    size_t edge_scratch_offset;
    size_t edge_scratch_size;
    size_t next_label_id;
} OrionFunction;

static bool fail(OrionFunction *function, const char *message) {
    fprintf(stderr, "Orion backend: %s", message);
    if (function != NULL && function->function != NULL) {
        String name = symbol_link_name(function->function->symbol);
        fprintf(stderr, " in %.*s", (int)name.length, name.data != NULL ? name.data : "");
    }
    fputc('\n', stderr);
    return false;
}

static HirType *base_type(HirType *type) {
    while (type != NULL && type->base != NULL)
        type = type->base;
    return type;
}

static size_t type_size(HirType *type) {
    type = base_type(type);
    if (type == NULL)
        return 0;
    if (type->kind == HIR_TYPE_POINTER)
        return 4;
    return type->size;
}

static size_t type_align(HirType *type) {
    type = base_type(type);
    if (type == NULL)
        return 1;
    if (type->kind == HIR_TYPE_POINTER)
        return 4;
    return type->align != 0 ? type->align : 1;
}

static size_t align_up(size_t value, size_t alignment) {
    if (alignment <= 1)
        return value;
    size_t remainder = value % alignment;
    if (remainder == 0)
        return value;
    if (value > SIZE_MAX - (alignment - remainder))
        return SIZE_MAX;
    return value + alignment - remainder;
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
        default:
            return false;
    }
}

static bool type_is_float(HirType *type) {
    type = base_type(type);
    return type != NULL && type->kind == HIR_TYPE_BUILTIN &&
           (type->builtin == BUILTIN_FLOAT32 || type->builtin == BUILTIN_FLOAT64);
}

static bool type_is_void_like(HirType *type) {
    type = base_type(type);
    return type != NULL && type->kind == HIR_TYPE_BUILTIN &&
           type->builtin == BUILTIN_NONE && type->size == 0;
}

static bool type_is_integer(HirType *type) {
    type = base_type(type);
    if (type == NULL)
        return false;
    if (type->kind == HIR_TYPE_ENUM)
        return type_is_integer(type->_enum.underlying);
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
            return true;
        default:
            return false;
    }
}

static bool type_is_aggregate(HirType *type) {
    type = base_type(type);
    if (type == NULL)
        return false;
    switch (type->kind) {
        case HIR_TYPE_ARRAY:
        case HIR_TYPE_SLICE:
        case HIR_TYPE_SUM:
        case HIR_TYPE_STRUCT:
        case HIR_TYPE_UNION:
        case HIR_TYPE_ANY:
        case HIR_TYPE_ANY_CONSTRAINT:
            return true;
        default:
            return false;
    }
}

static bool type_is_scalar(HirType *type) {
    type = base_type(type);
    if (type == NULL || type_is_float(type) || type_is_void_like(type))
        return false;
    if (type->kind == HIR_TYPE_POINTER)
        return type_size(type) == 4 && type_align(type) == 4;
    if (type->kind == HIR_TYPE_ENUM)
        return type_is_scalar(type->_enum.underlying);
    return type_is_integer(type) && type->size > 0 && type->size <= 8;
}

static bool type_is_supported_value(HirType *type) {
    if (type == NULL)
        return false;
    if (type_is_void_like(type))
        return true;
    if (type_is_scalar(type))
        return true;
    if (type_is_aggregate(type))
        return true;
    return false;
}

static bool supports(const TargetInfo *target) {
    return target != NULL &&
           target->arch == TARGET_ARCH_ORION &&
           target->abi == TARGET_ABI_ORION &&
           target->endian == TARGET_ENDIAN_LITTLE &&
           target->pointer.size == 4 &&
           target->pointer.align == 4 &&
           (target->object == TARGET_OBJECT_NONE || target->object == TARGET_OBJECT_ELF32);
}

static void emit_mem_operand(FILE *out, int base, int64_t offset) {
    if (offset == 0) {
        fprintf(out, "[r%d]", base);
    } else if (offset > 0) {
        fprintf(out, "[r%d + #%" PRIu64 "]", base, (uint64_t)offset);
    } else {
        fprintf(out, "[r%d - #%" PRIu64 "]", base, (uint64_t)(-offset));
    }
}

static void emit_mov_imm(FILE *out, int reg, uint32_t value) {
    uint16_t high = (uint16_t)(value >> 16);
    uint16_t low = (uint16_t)value;
    int32_t signed_low = (int16_t)low;

    if (value <= UINT32_C(0x7fff) || value >= UINT32_C(0xffff8000)) {
        fprintf(out, "    xor r%d, r%d, r%d\n", reg, reg, reg);
        if (signed_low != 0)
            fprintf(out, "    add r%d, r%d, #%" PRId32 "\n", reg, reg, signed_low);
        return;
    }

    fprintf(out, "    lui r%d, #%u\n", reg, (unsigned)high);
    if (low != 0)
        fprintf(out, "    or r%d, r%d, #%u\n", reg, reg, (unsigned)low);
}

static void emit_mov_size(FILE *out, int reg, size_t value) {
    emit_mov_imm(out, reg, (uint32_t)value);
}

static void emit_reg_copy(FILE *out, int dest, int source) {
    if (dest != source)
        fprintf(out, "    add r%d, r%d, #0\n", dest, source);
}

static void emit_bitwise_not(FILE *out, int reg) {
    fputs("    lui r12, #65535\n    or r12, r12, #65535\n", out);
    fprintf(out, "    xor r%d, r%d, r12\n", reg, reg);
}

static void emit_adjust_sp(OrionFunction *function, bool subtract, size_t amount) {
    if (amount == 0)
        return;
    emit_mov_size(function->out, ORION_R12, amount);
    fprintf(function->out, "    %s r15, r15, r12\n", subtract ? "sub" : "add");
}

static int64_t slot_relative(const OrionSlot *slot, size_t byte_offset) {
    return -(int64_t)slot->offset + (int64_t)byte_offset;
}

static bool slot_for(OrionFunction *function, LirValueId value, OrionSlot **out) {
    if (value >= function->slot_count || function->slots[value].offset == 0)
        return fail(function, "LIR value has no stack slot");
    *out = &function->slots[value];
    return true;
}

static bool value_type_for(OrionFunction *function, LirValueId value, HirType **out) {
    if (value >= function->slot_count || function->value_types[value] == NULL)
        return fail(function, "LIR value has no recorded type");
    *out = function->value_types[value];
    return true;
}

static bool record_value_type(OrionFunction *function, LirValueId value, HirType *type) {
    if (value == LIR_INVALID_VALUE)
        return true;
    if (value >= function->slot_count)
        return fail(function, "LIR value ID exceeds next_value");
    if (type == NULL)
        return fail(function, "LIR value is missing its type");
    if (function->value_types[value] == NULL)
        function->value_types[value] = type;
    return true;
}

static bool record_operand_type(OrionFunction *function, const LirOperand *operand) {
    if (operand->kind == LIR_OPERAND_VALUE)
        return record_value_type(function, operand->value, operand->type);
    return true;
}

static bool emit_slot_address(OrionFunction *function, int reg, LirValueId value) {
    OrionSlot *slot;
    if (!slot_for(function, value, &slot))
        return false;
    int64_t displacement = -(int64_t)slot->offset;
    emit_mov_imm(function->out, reg, (uint32_t)displacement);
    fprintf(function->out, "    add r%d, r11, r%d\n", reg, reg);
    return true;
}

static bool emit_load_word_at(OrionFunction *function, int base, int64_t offset, int dest) {
    fprintf(function->out, "    ldr r%d, ", dest);
    emit_mem_operand(function->out, base, offset);
    fputc('\n', function->out);
    return true;
}

static bool emit_store_word_at(OrionFunction *function, int base, int64_t offset, int src) {
    fprintf(function->out, "    str r%d, ", src);
    emit_mem_operand(function->out, base, offset);
    fputc('\n', function->out);
    return true;
}

static bool emit_load_byte_at(OrionFunction *function, int base, int64_t offset, int dest) {
    fprintf(function->out, "    ldrb r%d, ", dest);
    emit_mem_operand(function->out, base, offset);
    fputc('\n', function->out);
    return true;
}

static bool emit_store_byte_at(OrionFunction *function, int base, int64_t offset, int src) {
    fprintf(function->out, "    strb r%d, ", src);
    emit_mem_operand(function->out, base, offset);
    fputc('\n', function->out);
    return true;
}

static bool emit_load_slot_chunk(OrionFunction *function, LirValueId value, size_t byte_offset, int dest) {
    OrionSlot *slot;
    HirType *type;
    if (!slot_for(function, value, &slot) || !value_type_for(function, value, &type))
        return false;
    size_t size = type_size(type);
    int64_t offset = slot_relative(slot, byte_offset);

    if (byte_offset + 4 <= (type_is_aggregate(type) ? size : slot->storage_size))
        return emit_load_word_at(function, ORION_FP, offset, dest);

    emit_mov_imm(function->out, dest, 0);
    size_t bytes = byte_offset < size ? size - byte_offset : 0;
    if (bytes > 4)
        bytes = 4;
    for (size_t i = 0; i < bytes; i++) {
        int temp = dest == ORION_R13 ? ORION_R12 : ORION_R13;
        emit_load_byte_at(function, ORION_FP, offset + (int64_t)i, temp);
        if (i != 0)
            fprintf(function->out, "    shl r%d, r%d, #%zu\n", temp, temp, i * 8);
        fprintf(function->out, "    or r%d, r%d, r%d\n", dest, dest, temp);
    }
    return true;
}

static bool emit_store_slot_word(OrionFunction *function, LirValueId value, size_t byte_offset, int src) {
    OrionSlot *slot;
    if (!slot_for(function, value, &slot))
        return false;
    return emit_store_word_at(function, ORION_FP, slot_relative(slot, byte_offset), src);
}

static void emit_sign_extend(FILE *out, int reg, size_t size) {
    if (size == 1) {
        fprintf(out, "    shl r%d, r%d, #24\n", reg, reg);
        fprintf(out, "    ashr r%d, r%d, #24\n", reg, reg);
    } else if (size == 2) {
        fprintf(out, "    shl r%d, r%d, #16\n", reg, reg);
        fprintf(out, "    ashr r%d, r%d, #16\n", reg, reg);
    }
}

static bool emit_load_scalar_memory(OrionFunction *function, int base, int64_t offset,
                                    HirType *type, int dest) {
    size_t size = type_size(type);
    if (size == 1) {
        emit_load_byte_at(function, base, offset, dest);
    } else if (size == 2) {
        int temp = dest == ORION_R13 ? ORION_R12 : ORION_R13;
        emit_load_byte_at(function, base, offset, dest);
        emit_load_byte_at(function, base, offset + 1, temp);
        fprintf(function->out, "    shl r%d, r%d, #8\n", temp, temp);
        fprintf(function->out, "    or r%d, r%d, r%d\n", dest, dest, temp);
    } else if (size == 4) {
        emit_load_word_at(function, base, offset, dest);
    } else {
        return fail(function, "scalar memory load requires a 1-, 2-, or 4-byte type");
    }
    if (type_is_signed(type) && size < 4)
        emit_sign_extend(function->out, dest, size);
    return true;
}

static bool emit_store_scalar_memory(OrionFunction *function, int base, int64_t offset,
                                     HirType *type, int src) {
    size_t size = type_size(type);
    if (size == 1) {
        emit_store_byte_at(function, base, offset, src);
    } else if (size == 2) {
        int temp = src == ORION_R12 ? ORION_R13 : ORION_R12;
        emit_store_byte_at(function, base, offset, src);
        fprintf(function->out, "    shr r%d, r%d, #8\n", temp, src);
        emit_store_byte_at(function, base, offset + 1, temp);
    } else if (size == 4) {
        emit_store_word_at(function, base, offset, src);
    } else {
        return fail(function, "scalar memory store requires a 1-, 2-, or 4-byte type");
    }
    return true;
}

static bool emit_load_scalar_slot(OrionFunction *function, LirValueId value, int dest) {
    OrionSlot *slot;
    HirType *type;
    if (!slot_for(function, value, &slot) || !value_type_for(function, value, &type))
        return false;
    if (type_size(type) == 8)
        return fail(function, "internal error: scalar-slot load requested for a 64-bit value");
    return emit_load_scalar_memory(function, ORION_FP, slot_relative(slot, 0), type, dest);
}

static bool emit_operand_chunk(OrionFunction *function, const LirOperand *operand,
                               size_t byte_offset, int dest);

static bool emit_copy_bytes(OrionFunction *function, int source_base, int64_t source_offset, int destination_base, int64_t destination_offset, size_t size) {
    for (size_t i = 0; i < size; i++) {
        emit_load_byte_at(function, source_base, source_offset + (int64_t)i, ORION_R12);
        emit_store_byte_at(function, destination_base, destination_offset + (int64_t)i, ORION_R12);
    }
    return true;
}

static bool emit_copy_slot_to_slot(OrionFunction *function, LirValueId source,
                                   LirValueId destination, size_t size) {
    OrionSlot *src;
    OrionSlot *dst;
    if (!slot_for(function, source, &src) || !slot_for(function, destination, &dst))
        return false;
    return emit_copy_bytes(function, ORION_FP, slot_relative(src, 0),
                           ORION_FP, slot_relative(dst, 0), size);
}

static bool emit_copy_address_to_slot(OrionFunction *function, int source_base,
                                      LirValueId destination, size_t size) {
    OrionSlot *dst;
    if (!slot_for(function, destination, &dst))
        return false;
    return emit_copy_bytes(function, source_base, 0, ORION_FP, slot_relative(dst, 0), size);
}

static bool emit_copy_slot_to_address(OrionFunction *function, LirValueId source,
                                      int destination_base, size_t size) {
    OrionSlot *src;
    if (!slot_for(function, source, &src))
        return false;
    return emit_copy_bytes(function, ORION_FP, slot_relative(src, 0), destination_base, 0, size);
}

static bool emit_operand_address(OrionFunction *function, const LirOperand *operand, int dest) {
    if (operand->kind == LIR_OPERAND_SYMBOL) {
        String name = symbol_link_name(operand->symbol);
        if (name.data == NULL || name.length == 0)
            return fail(function, "symbol operand has no link name");
        fprintf(function->out, "    mov r%d, $%.*s\n", dest, (int)name.length, name.data);
        return true;
    }
    if (operand->kind == LIR_OPERAND_VALUE && type_is_aggregate(operand->type))
        return emit_slot_address(function, dest, operand->value);
    return fail(function, "address operand must be a symbol or aggregate LIR value");
}

static bool emit_operand_chunk(OrionFunction *function, const LirOperand *operand,
                               size_t byte_offset, int dest) {
    switch (operand->kind) {
        case LIR_OPERAND_INT: {
            uint64_t bits = (uint64_t)operand->int_value;
            emit_mov_imm(function->out, dest, byte_offset < 8 ? (uint32_t)(bits >> (byte_offset * 8)) : 0);
            return true;
        }
        case LIR_OPERAND_UINT: {
            uint64_t bits = operand->uint_value;
            emit_mov_imm(function->out, dest, byte_offset < 8 ? (uint32_t)(bits >> (byte_offset * 8)) : 0);
            return true;
        }
        case LIR_OPERAND_OFFSET: {
            uint64_t bits = (uint64_t)operand->offset;
            emit_mov_imm(function->out, dest, byte_offset < 8 ? (uint32_t)(bits >> (byte_offset * 8)) : 0);
            return true;
        }
        case LIR_OPERAND_BOOL:
            emit_mov_imm(function->out, dest, byte_offset == 0 && operand->bool_value ? 1u : 0u);
            return true;
        case LIR_OPERAND_SYMBOL: {
            if (byte_offset != 0)
                return fail(function, "symbol operand does not fit in one word");
            String name = symbol_link_name(operand->symbol);
            if (name.data == NULL || name.length == 0)
                return fail(function, "symbol operand has no link name");
            fprintf(function->out, "    mov r%d, $%.*s\n", dest, (int)name.length, name.data);
            return true;
        }
        case LIR_OPERAND_VALUE: {
            OrionSlot *slot;
            if (!slot_for(function, operand->value, &slot))
                return false;
            HirType *type = operand->type != NULL ? operand->type : function->value_types[operand->value];
            if (type == NULL)
                return fail(function, "value operand has no type");
            size_t size = type_size(type);

            if (type_is_aggregate(type))
                return emit_load_slot_chunk(function, operand->value, byte_offset, dest);
            if (!type_is_scalar(type))
                return fail(function, "unsupported scalar operand type");
            if (byte_offset >= size) {
                if (size < 8 && type_is_signed(type) && size != 0) {
                    if (size < 4) {
                        if (!emit_load_scalar_slot(function, operand->value, dest))
                            return false;
                    } else if (!emit_load_word_at(function, ORION_FP, slot_relative(slot, 0), dest)) {
                        return false;
                    }
                    fprintf(function->out, "    ashr r%d, r%d, #31\n", dest, dest);
                } else {
                    emit_mov_imm(function->out, dest, 0);
                }
                return true;
            }
            if (size == 8)
                return emit_load_word_at(function, ORION_FP, slot_relative(slot, byte_offset), dest);
            if (byte_offset != 0)
                return fail(function, "invalid chunk offset for narrow scalar");
            return emit_load_scalar_slot(function, operand->value, dest);
        }
        case LIR_OPERAND_FLOAT:
            return fail(function, "floating-point operands are not supported by the Orion backend");
        case LIR_OPERAND_INVALID:
        default:
            return fail(function, "invalid LIR operand");
    }
}

static bool emit_operand_scalar(OrionFunction *function, const LirOperand *operand, int dest) {
    HirType *type = operand->type;
    if (type != NULL && type_is_aggregate(type))
        return fail(function, "aggregate used where scalar operand is required");
    return emit_operand_chunk(function, operand, 0, dest);
}

static bool emit_zero_slot(OrionFunction *function, LirValueId value, size_t size) {
    OrionSlot *slot;
    if (!slot_for(function, value, &slot))
        return false;
    emit_mov_imm(function->out, ORION_R0, 0);
    for (size_t i = 0; i < size; i++)
        emit_store_byte_at(function, ORION_FP, slot_relative(slot, i), ORION_R0);
    return true;
}

static bool emit_data_zeros(FILE *out, size_t count) {
    while (count >= 4) {
        fprintf(out, "    .word #0\n");
        count -= 4;
    }
    for (size_t i = 0; i < count; i++)
        fprintf(out, "    .byte #0\n");
    return true;
}

static bool emit_global(FILE *out, const LirGlobal *global) {
    if (global->type == NULL) {
        fprintf(stderr, "Orion backend: global has no type\n");
        return false;
    }
    String name = symbol_link_name(global->symbol);
    if (name.data == NULL || name.length == 0) {
        fprintf(stderr, "Orion backend: global has no link name\n");
        return false;
    }

    const char *section = global->is_mutable ? ".data" : ".rodata";
    if (global->data.len == 0)
        section = ".bss";

    fprintf(out, "\n.section %s\n", section);
    if (global->is_export)
        fprintf(out, ".global %.*s\n", (int)name.length, name.data);
    fprintf(out, ".align #%zu\n", type_align(global->type));
    fprintf(out, "%.*s:\n", (int)name.length, name.data);

    size_t offset = 0;
    for (size_t i = 0; i < global->data.len; i++) {
        const LirData *data = &((LirData *)global->data.data)[i];
        if (data->offset < offset) {
            fprintf(stderr, "Orion backend: overlapping or unsorted data entries in %.*s\n",
                    (int)name.length, name.data);
            return false;
        }
        emit_data_zeros(out, data->offset - offset);

        switch (data->kind) {
            case LIR_DATA_INTEGER: {
                size_t size = type_size(data->type);
                if (size != 1 && size != 2 && size != 4 && size != 8) {
                    fprintf(stderr, "Orion backend: unsupported integer global width %zu\n", size);
                    return false;
                }
                for (size_t byte = 0; byte < size; byte++)
                    fprintf(out, "    .byte #%u\n", (unsigned)((data->integer >> (byte * 8)) & 0xffu));
                offset = data->offset + size;
                break;
            }
            case LIR_DATA_BOOL:
                fprintf(out, "    .byte #%u\n", data->boolean ? 1u : 0u);
                offset = data->offset + 1;
                break;
            case LIR_DATA_BYTES:
                for (size_t byte = 0; byte < data->bytes.length; byte++)
                    fprintf(out, "    .byte #%u\n", (unsigned char)data->bytes.data[byte]);
                offset = data->offset + data->bytes.length;
                break;
            case LIR_DATA_ADDRESS: {
                if (type_size(data->type) != 4) {
                    fprintf(stderr, "Orion backend: Orion symbol addresses must be 32-bit\n");
                    return false;
                }
                String target_name = symbol_link_name(data->symbol);
                if (target_name.data == NULL || target_name.length == 0) {
                    fprintf(stderr, "Orion backend: address initializer has no symbol name\n");
                    return false;
                }
                fprintf(out, "    .word $%.*s\n", (int)target_name.length, target_name.data);
                offset = data->offset + 4;
                break;
            }
            case LIR_DATA_FLOAT:
                fprintf(stderr, "Orion backend: floating-point globals are not supported\n");
                return false;
        }
    }

    if (offset > type_size(global->type)) {
        fprintf(stderr, "Orion backend: global initializer exceeds object size for %.*s\n",
                (int)name.length, name.data);
        return false;
    }
    emit_data_zeros(out, type_size(global->type) - offset);
    return true;
}

static void emit_string(FILE *out, const LirString *string) {
    String name = symbol_link_name(string->symbol);
    fprintf(out, "\n.section .rodata\n%.*s:\n", (int)name.length, name.data);
    if (string->bytes.length == 0) {
        fprintf(out, "    .byte #0\n");
        return;
    }
    for (size_t i = 0; i < string->bytes.length; i++)
        fprintf(out, "    .byte #%u\n", (unsigned char)string->bytes.data[i]);
}

static LirBlock *find_block(OrionFunction *function, LirBlockId id) {
    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        if (block != NULL && block->id == id)
            return block;
    }
    return NULL;
}

static void emit_block_label(OrionFunction *function, LirBlockId id) {
    String name = symbol_link_name(function->function->symbol);
    fprintf(function->out, "%.*s_L%u:\n", (int)name.length, name.data, id);
}

static void emit_block_ref(OrionFunction *function, LirBlockId id) {
    String name = symbol_link_name(function->function->symbol);
    fprintf(function->out, "$%.*s_L%u", (int)name.length, name.data, id);
}

static size_t next_label(OrionFunction *function) {
    return function->next_label_id++;
}

static void emit_local_label(OrionFunction *function, size_t id) {
    String name = symbol_link_name(function->function->symbol);
    fprintf(function->out, "%.*s__orion_%zu", (int)name.length, name.data, id);
}

static void emit_local_ref(OrionFunction *function, size_t id) {
    fputc('$', function->out);
    emit_local_label(function, id);
}

static void emit_branch_to_label(OrionFunction *function, const char *condition,
                                 int lhs, int rhs, size_t target) {
    size_t skip = next_label(function);
    fprintf(function->out, "    %s r%d, r%d, #1\n", condition, lhs, rhs);
    fprintf(function->out, "    jmp ");
    emit_local_ref(function, skip);
    fputc('\n', function->out);
    fprintf(function->out, "    jmp ");
    emit_local_ref(function, target);
    fputc('\n', function->out);
    emit_local_label(function, skip);
    fputs(":\n", function->out);
}

static bool emit_store_result_word(OrionFunction *function, LirValueId result, size_t byte_offset, int reg) {
    return emit_store_slot_word(function, result, byte_offset, reg);
}

static bool emit_copy_operand_to_scratch(OrionFunction *function, const LirOperand *operand,
                                         size_t scratch_offset) {
    size_t size = type_size(operand->type);
    size_t chunks = (size + 3) / 4;
    for (size_t chunk = 0; chunk < chunks; chunk++) {
        if (!emit_operand_chunk(function, operand, chunk * 4, ORION_R14))
            return false;
        emit_store_word_at(function, ORION_FP,
                           -(int64_t)function->edge_scratch_offset + (int64_t)scratch_offset + (int64_t)(chunk * 4),
                           ORION_R14);
    }
    return true;
}

static size_t block_param_storage(const LirBlock *block) {
    size_t total = 0;
    for (size_t i = 0; i < block->params.len; i++) {
        const LirBlockParam *param = &((LirBlockParam *)block->params.data)[i];
        size_t size = type_size(param->type);
        size_t storage = align_up(size < 4 ? 4 : size, 4);
        if (total > SIZE_MAX - storage)
            return SIZE_MAX;
        total += storage;
    }
    return total;
}

static bool emit_edge(OrionFunction *function, LirBlockId target_id, Array(LirOperand) args) {
    LirBlock *target = find_block(function, target_id);
    if (target == NULL)
        return fail(function, "control-flow edge targets an unknown block");
    if (args.len != target->params.len)
        return fail(function, "control-flow edge argument count does not match block parameters");

    size_t scratch_at = 0;
    for (size_t i = 0; i < args.len; i++) {
        LirOperand *arg = &((LirOperand *)args.data)[i];
        LirBlockParam *param = &((LirBlockParam *)target->params.data)[i];
        size_t arg_size = type_size(arg->type);
        if (arg_size != type_size(param->type))
            return fail(function, "control-flow edge argument size does not match block parameter");
        if (!emit_copy_operand_to_scratch(function, arg, scratch_at))
            return false;
        scratch_at += align_up(arg_size < 4 ? 4 : arg_size, 4);
    }

    scratch_at = 0;
    for (size_t i = 0; i < args.len; i++) {
        LirBlockParam *param = &((LirBlockParam *)target->params.data)[i];
        size_t size = type_size(param->type);
        size_t chunks = (size + 3) / 4;
        for (size_t chunk = 0; chunk < chunks; chunk++) {
            emit_load_word_at(function, ORION_FP,
                             -(int64_t)function->edge_scratch_offset + (int64_t)scratch_at + (int64_t)(chunk * 4),
                             ORION_R14);
            if (!emit_store_slot_word(function, param->value, chunk * 4, ORION_R14))
                return false;
        }
        scratch_at += align_up(size < 4 ? 4 : size, 4);
    }
    return true;
}

static bool emit_return_epilogue(OrionFunction *function) {
    emit_reg_copy(function->out, ORION_SP, ORION_FP);
    emit_load_word_at(function, ORION_SP, 0, ORION_FP);
    fputs("    add r15, r15, #4\n", function->out);
    fputs("    ret\n", function->out);
    return !ferror(function->out);
}

static bool emit_function_parameters(OrionFunction *function) {
    size_t word_index = 0;
    for (size_t i = 0; i < function->function->params.len; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)function->function->params.data)[i];
        size_t size = type_size(param->type);
        size_t words = (size + 3) / 4;
        for (size_t word = 0; word < words; word++, word_index++) {
            if (word_index < 4) {
                if (!emit_store_slot_word(function, param->value, word * 4, (int)word_index))
                    return false;
            } else {
                size_t stack_offset = 8 + (word_index - 4) * 4;
                emit_load_word_at(function, ORION_FP, (int64_t)stack_offset, ORION_R14);
                if (!emit_store_slot_word(function, param->value, word * 4, ORION_R14))
                    return false;
            }
        }
    }
    return true;
}

static bool emit_binary32(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
        !emit_operand_chunk(function, &operands[1], 0, ORION_R1))
        return false;

    const char *mnemonic = NULL;
    switch (instruction->opcode) {
        case LIR_OP_ADD: mnemonic = "add"; break;
        case LIR_OP_SUB: mnemonic = "sub"; break;
        case LIR_OP_MUL: mnemonic = "mul"; break;
        case LIR_OP_DIV: mnemonic = type_is_signed(operands[0].type) ? "div" : "divu"; break;
        case LIR_OP_SHL: mnemonic = "shl"; break;
        case LIR_OP_SHR: mnemonic = "shr"; break;
        case LIR_OP_AND: mnemonic = "and"; break;
        case LIR_OP_OR: mnemonic = "or"; break;
        case LIR_OP_XOR: mnemonic = "xor"; break;
        default: return fail(function, "internal error: invalid 32-bit binary opcode");
    }

    fprintf(function->out, "    %s r0, r0, r1\n", mnemonic);
    return emit_store_result_word(function, instruction->result, 0, ORION_R0);
}

static bool emit_binary64(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
        !emit_operand_chunk(function, &operands[0], 4, ORION_R1) ||
        !emit_operand_chunk(function, &operands[1], 0, ORION_R2) ||
        !emit_operand_chunk(function, &operands[1], 4, ORION_R3))
        return false;

    switch (instruction->opcode) {
        case LIR_OP_ADD:
            emit_reg_copy(function->out, ORION_R12, ORION_R0);
            fputs("    add r0, r0, r2\n", function->out);
            fputs("    add r1, r1, r3\n", function->out);
            fputs("    jgeu r0, r12, #1\n", function->out);
            fputs("    add r1, r1, #1\n", function->out);
            break;
        case LIR_OP_SUB:
            emit_reg_copy(function->out, ORION_R12, ORION_R0);
            fputs("    sub r0, r0, r2\n", function->out);
            fputs("    sub r1, r1, r3\n", function->out);
            fputs("    jgeu r12, r2, #1\n", function->out);
            fputs("    sub r1, r1, #1\n", function->out);
            break;
        case LIR_OP_AND:
            fputs("    and r0, r0, r2\n    and r1, r1, r3\n", function->out);
            break;
        case LIR_OP_OR:
            fputs("    or r0, r0, r2\n    or r1, r1, r3\n", function->out);
            break;
        case LIR_OP_XOR:
            fputs("    xor r0, r0, r2\n    xor r1, r1, r3\n", function->out);
            break;
        case LIR_OP_MUL: {
            fputs("    mul r12, r0, r2\n", function->out);
            if (!emit_store_result_word(function, instruction->result, 0, ORION_R12))
                return false;
            fputs("    and r12, r0, #65535\n", function->out);
            fputs("    and r13, r2, #65535\n", function->out);
            fputs("    mul r14, r12, r13\n", function->out);
            fputs("    shr r14, r14, #16\n", function->out);
            fputs("    shr r12, r0, #16\n", function->out);
            fputs("    mul r12, r12, r13\n    and r12, r12, #65535\n", function->out);
            fputs("    add r14, r14, r12\n", function->out);
            fputs("    and r12, r0, #65535\n", function->out);
            fputs("    shr r13, r2, #16\n", function->out);
            fputs("    mul r12, r12, r13\n    and r12, r12, #65535\n", function->out);
            fputs("    add r14, r14, r12\n", function->out);
            fputs("    mul r12, r0, r3\n    add r14, r14, r12\n", function->out);
            fputs("    mul r12, r1, r2\n    add r14, r14, r12\n", function->out);
            return emit_store_result_word(function, instruction->result, 4, ORION_R14);
        }
        case LIR_OP_DIV:
        case LIR_OP_MOD:
        case LIR_OP_SHL:
        case LIR_OP_SHR:
            return fail(function, "64-bit divide, remainder and shifts are not implemented yet");
        default:
            return fail(function, "unsupported 64-bit binary operation");
    }

    return emit_store_result_word(function, instruction->result, 0, ORION_R0) &&
           emit_store_result_word(function, instruction->result, 4, ORION_R1);
}

static bool emit_boolean_compare32(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
        !emit_operand_chunk(function, &operands[1], 0, ORION_R1))
        return false;

    bool unsig = !type_is_signed(operands[0].type);
    const char *true_cond = NULL;
    bool default_true = false;
    int lhs = ORION_R0, rhs = ORION_R1;

    switch (instruction->opcode) {
        case LIR_OP_LT: true_cond = unsig ? "jltu" : "jlt"; break;
        case LIR_OP_LE: true_cond = unsig ? "jltu" : "jlt"; lhs = ORION_R1; rhs = ORION_R0; default_true = true; break;
        case LIR_OP_GT: true_cond = unsig ? "jltu" : "jlt"; lhs = ORION_R1; rhs = ORION_R0; break;
        case LIR_OP_GE: true_cond = unsig ? "jltu" : "jlt"; default_true = true; break;
        case LIR_OP_EQ: true_cond = "jeq"; break;
        case LIR_OP_NE: true_cond = "jne"; break;
        default: return fail(function, "internal error: invalid comparison opcode");
    }

    size_t true_label = next_label(function);
    size_t end_label = next_label(function);
    emit_mov_imm(function->out, ORION_R14, default_true ? 1u : 0u);
    emit_branch_to_label(function, true_cond, lhs, rhs, true_label);
    fprintf(function->out, "    jmp ");
    emit_local_ref(function, end_label);
    fputc('\n', function->out);
    emit_local_label(function, true_label);
    fputs(":\n", function->out);
    emit_mov_imm(function->out, ORION_R14, default_true ? 0u : 1u);
    emit_local_label(function, end_label);
    fputs(":\n", function->out);
    return emit_store_result_word(function, instruction->result, 0, ORION_R14);
}

static bool emit_boolean_compare64(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
        !emit_operand_chunk(function, &operands[0], 4, ORION_R1) ||
        !emit_operand_chunk(function, &operands[1], 0, ORION_R2) ||
        !emit_operand_chunk(function, &operands[1], 4, ORION_R3))
        return false;

    size_t true_label = next_label(function);
    size_t false_label = next_label(function);
    size_t end_label = next_label(function);
    bool unsig = !type_is_signed(operands[0].type);
    const char *hi_less = unsig ? "jltu" : "jlt";
    const char *hi_greater = unsig ? "jltu" : "jlt";

    emit_mov_imm(function->out, ORION_R14, 0);
    switch (instruction->opcode) {
        case LIR_OP_EQ:
            emit_branch_to_label(function, "jne", ORION_R0, ORION_R2, false_label);
            emit_branch_to_label(function, "jne", ORION_R1, ORION_R3, false_label);
            emit_mov_imm(function->out, ORION_R14, 1);
            break;
        case LIR_OP_NE:
            emit_branch_to_label(function, "jne", ORION_R0, ORION_R2, true_label);
            emit_branch_to_label(function, "jne", ORION_R1, ORION_R3, true_label);
            fputs("    jmp ", function->out); emit_local_ref(function, end_label); fputc('\n', function->out);
            emit_local_label(function, true_label); fputs(":\n", function->out);
            emit_mov_imm(function->out, ORION_R14, 1);
            emit_local_label(function, end_label); fputs(":\n", function->out);
            return emit_store_result_word(function, instruction->result, 0, ORION_R14);
        case LIR_OP_LT:
            emit_branch_to_label(function, hi_less, ORION_R1, ORION_R3, true_label);
            emit_branch_to_label(function, hi_greater, ORION_R3, ORION_R1, false_label);
            emit_branch_to_label(function, "jltu", ORION_R0, ORION_R2, true_label);
            break;
        case LIR_OP_LE:
            emit_branch_to_label(function, hi_greater, ORION_R3, ORION_R1, false_label);
            emit_branch_to_label(function, hi_less, ORION_R1, ORION_R3, true_label);
            emit_branch_to_label(function, "jltu", ORION_R2, ORION_R0, false_label);
            emit_mov_imm(function->out, ORION_R14, 1);
            break;
        case LIR_OP_GT:
            emit_branch_to_label(function, hi_greater, ORION_R3, ORION_R1, true_label);
            emit_branch_to_label(function, hi_less, ORION_R1, ORION_R3, false_label);
            emit_branch_to_label(function, "jltu", ORION_R2, ORION_R0, true_label);
            break;
        case LIR_OP_GE:
            emit_branch_to_label(function, hi_less, ORION_R1, ORION_R3, false_label);
            emit_branch_to_label(function, hi_greater, ORION_R3, ORION_R1, true_label);
            emit_branch_to_label(function, "jltu", ORION_R0, ORION_R2, false_label);
            emit_mov_imm(function->out, ORION_R14, 1);
            break;
        default:
            return fail(function, "internal error: invalid 64-bit comparison opcode");
    }

    fputs("    jmp ", function->out); emit_local_ref(function, end_label); fputc('\n', function->out);
    emit_local_label(function, true_label); fputs(":\n", function->out);
    emit_mov_imm(function->out, ORION_R14, 1);
    fputs("    jmp ", function->out); emit_local_ref(function, end_label); fputc('\n', function->out);
    emit_local_label(function, false_label); fputs(":\n", function->out);
    emit_mov_imm(function->out, ORION_R14, 0);
    emit_local_label(function, end_label); fputs(":\n", function->out);
    return emit_store_result_word(function, instruction->result, 0, ORION_R14);
}

static bool emit_compare(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    if (type_size(operands[0].type) == 8 || type_size(operands[1].type) == 8)
        return emit_boolean_compare64(function, instruction);
    return emit_boolean_compare32(function, instruction);
}

static bool emit_binary(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    size_t size = type_size(instruction->result_type);
    if (size == 8)
        return emit_binary64(function, instruction);
    if (size > 4)
        return fail(function, "unsupported binary result width");

    if (instruction->opcode == LIR_OP_MOD) {
        if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
            !emit_operand_chunk(function, &operands[1], 0, ORION_R1))
            return false;
        emit_reg_copy(function->out, ORION_R2, ORION_R0);
        fprintf(function->out, "    %s r0, r0, r1\n", type_is_signed(operands[0].type) ? "div" : "divu");
        fputs("    mul r0, r0, r1\n    sub r2, r2, r0\n", function->out);
        return emit_store_result_word(function, instruction->result, 0, ORION_R2);
    }
    return emit_binary32(function, instruction);
}

static bool emit_unary(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operand = (LirOperand *)instruction->operands.data;
    size_t size = type_size(instruction->result_type);

    if (size == 8) {
        if (!emit_operand_chunk(function, &operand[0], 0, ORION_R0) ||
            !emit_operand_chunk(function, &operand[0], 4, ORION_R1))
            return false;
        if (instruction->opcode == LIR_OP_NOT) {
            emit_bitwise_not(function->out, ORION_R0);
            emit_bitwise_not(function->out, ORION_R1);
        } else if (instruction->opcode == LIR_OP_NEG) {
            emit_reg_copy(function->out, ORION_R2, ORION_R0);
            fputs("    xor r3, r3, r3\n    sub r3, r3, r1\n", function->out);
            fputs("    xor r12, r12, r12\n    sub r12, r12, r0\n", function->out);
            fputs("    jeq r2, #0, #1\n    sub r3, r3, #1\n", function->out);
            emit_reg_copy(function->out, ORION_R0, ORION_R12);
            emit_reg_copy(function->out, ORION_R1, ORION_R3);
        } else {
            return fail(function, "unsupported 64-bit unary operation");
        }
        return emit_store_result_word(function, instruction->result, 0, ORION_R0) &&
               emit_store_result_word(function, instruction->result, 4, ORION_R1);
    }

    if (!emit_operand_chunk(function, &operand[0], 0, ORION_R0))
        return false;
    if (instruction->opcode == LIR_OP_NEG) {
        fputs("    xor r12, r12, r12\n    sub r0, r12, r0\n", function->out);
    } else if (instruction->opcode == LIR_OP_NOT) {
        emit_bitwise_not(function->out, ORION_R0);
    } else {
        return fail(function, "unsupported unary opcode");
    }
    return emit_store_result_word(function, instruction->result, 0, ORION_R0);
}

static bool emit_load(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    HirType *type = instruction->result_type;
    size_t size = type_size(type);
    if (!type_is_supported_value(type))
        return fail(function, "unsupported load type");
    if (!emit_operand_scalar(function, &operands[0], ORION_R14))
        return false;

    if (type_is_aggregate(type))
        return emit_copy_address_to_slot(function, ORION_R14, instruction->result, size);
    if (size == 8) {
        emit_load_word_at(function, ORION_R14, 0, ORION_R0);
        emit_load_word_at(function, ORION_R14, 4, ORION_R1);
        return emit_store_result_word(function, instruction->result, 0, ORION_R0) &&
               emit_store_result_word(function, instruction->result, 4, ORION_R1);
    }
    if (!emit_load_scalar_memory(function, ORION_R14, 0, type, ORION_R0))
        return false;
    return emit_store_result_word(function, instruction->result, 0, ORION_R0);
}

static bool emit_store(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    HirType *type = operands[1].type;
    size_t size = type_size(type);
    if (!type_is_supported_value(type))
        return fail(function, "unsupported store type");
    if (!emit_operand_scalar(function, &operands[0], ORION_R14))
        return false;
    if (type_is_aggregate(type)) {
        if (operands[1].kind != LIR_OPERAND_VALUE)
            return fail(function, "aggregate store source must be an aggregate LIR value");
        return emit_copy_slot_to_address(function, operands[1].value, ORION_R14, size);
    }
    if (size == 8) {
        if (!emit_operand_chunk(function, &operands[1], 0, ORION_R0) ||
            !emit_operand_chunk(function, &operands[1], 4, ORION_R1))
            return false;
        emit_store_word_at(function, ORION_R14, 0, ORION_R0);
        emit_store_word_at(function, ORION_R14, 4, ORION_R1);
        return true;
    }
    if (!emit_operand_chunk(function, &operands[1], 0, ORION_R0))
        return false;
    return emit_store_scalar_memory(function, ORION_R14, 0, type, ORION_R0);
}

static bool emit_call(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    if (instruction->operands.len == 0)
        return fail(function, "call instruction has no callee operand");

    size_t total_words = 0;
    for (size_t i = 1; i < instruction->operands.len; i++) {
        HirType *type = operands[i].type;
        if (!type_is_supported_value(type) || type_is_void_like(type))
            return fail(function, "unsupported call argument type");
        size_t words = (type_size(type) + 3) / 4;
        if (total_words > SIZE_MAX - words)
            return fail(function, "call argument list is too large");
        total_words += words;
    }
    if (instruction->result != LIR_INVALID_VALUE && type_is_aggregate(instruction->result_type) &&
        type_size(instruction->result_type) > 16)
        return fail(function, "aggregate call results larger than 16 bytes are not supported");

    size_t stack_words = total_words > 4 ? total_words - 4 : 0;
    size_t stack_bytes = align_up(stack_words * 4, 8);
    emit_adjust_sp(function, true, stack_bytes);

    size_t word_index = 0;
    for (size_t i = 1; i < instruction->operands.len; i++) {
        LirOperand *arg = &operands[i];
        size_t words = (type_size(arg->type) + 3) / 4;
        for (size_t word = 0; word < words; word++, word_index++) {
            if (word_index < 4)
                continue;
            if (!emit_operand_chunk(function, arg, word * 4, ORION_R14))
                return false;
            emit_store_word_at(function, ORION_SP, (int64_t)((word_index - 4) * 4), ORION_R14);
        }
    }

    word_index = 0;
    for (size_t i = 1; i < instruction->operands.len; i++) {
        LirOperand *arg = &operands[i];
        size_t words = (type_size(arg->type) + 3) / 4;
        for (size_t word = 0; word < words; word++, word_index++) {
            if (word_index >= 4)
                continue;
            if (!emit_operand_chunk(function, arg, word * 4, (int)word_index))
                return false;
        }
    }

    if (operands[0].kind == LIR_OPERAND_SYMBOL) {
        String name = symbol_link_name(operands[0].symbol);
        if (name.data == NULL || name.length == 0)
            return fail(function, "callee symbol has no link name");
        fprintf(function->out, "    call $%.*s\n", (int)name.length, name.data);
    } else {
        if (!emit_operand_chunk(function, &operands[0], 0, ORION_R14))
            return false;
        fputs("    callr r14\n", function->out);
    }

    emit_adjust_sp(function, false, stack_bytes);

    if (instruction->result == LIR_INVALID_VALUE || type_is_void_like(instruction->result_type))
        return true;

    HirType *result_type = instruction->result_type;
    size_t result_size = type_size(result_type);
    if (type_is_aggregate(result_type)) {
        for (size_t chunk = 0; chunk < result_size; chunk += 4) {
            size_t word = chunk / 4;
            if (word > 3)
                return fail(function, "aggregate call return exceeds R0-R3");
            if (!emit_store_result_word(function, instruction->result, chunk, (int)word))
                return false;
        }
        return true;
    }
    if (result_size == 8)
        return emit_store_result_word(function, instruction->result, 0, ORION_R0) &&
               emit_store_result_word(function, instruction->result, 4, ORION_R1);
    return emit_store_result_word(function, instruction->result, 0, ORION_R0);
}

static bool emit_extract(OrionFunction *function, const LirInstruction *instruction, bool dynamic) {
    LirOperand *operands = instruction->operands.data;
    if (operands[0].kind != LIR_OPERAND_VALUE || !type_is_aggregate(operands[0].type))
        return fail(function, "extract source must be an aggregate SSA value");
    if (!emit_slot_address(function, ORION_R14, operands[0].value))
        return false;
    if (dynamic) {
        if (!emit_operand_chunk(function, &operands[1], 0, ORION_R12))
            return false;
        fputs("    add r14, r14, r12\n", function->out);
    } else {
        if (operands[1].kind != LIR_OPERAND_OFFSET)
            return fail(function, "static extract requires a constant offset");
        emit_mov_size(function->out, ORION_R12, operands[1].offset);
        fputs("    add r14, r14, r12\n", function->out);
    }

    HirType *result_type = instruction->result_type;
    size_t size = type_size(result_type);
    if (type_is_aggregate(result_type))
        return emit_copy_address_to_slot(function, ORION_R14, instruction->result, size);
    if (size == 8) {
        emit_load_word_at(function, ORION_R14, 0, ORION_R0);
        emit_load_word_at(function, ORION_R14, 4, ORION_R1);
        return emit_store_result_word(function, instruction->result, 0, ORION_R0) &&
               emit_store_result_word(function, instruction->result, 4, ORION_R1);
    }
    if (!emit_load_scalar_memory(function, ORION_R14, 0, result_type, ORION_R0))
        return false;
    return emit_store_result_word(function, instruction->result, 0, ORION_R0);
}

static bool emit_insert(OrionFunction *function, const LirInstruction *instruction, bool dynamic) {
    LirOperand *operands = instruction->operands.data;
    if (operands[0].kind != LIR_OPERAND_VALUE || !type_is_aggregate(operands[0].type))
        return fail(function, "insert destination source must be an aggregate SSA value");

    HirType *aggregate_type = operands[0].type;
    size_t aggregate_size = type_size(aggregate_type);
    if (!emit_copy_slot_to_slot(function, operands[0].value, instruction->result, aggregate_size))
        return false;
    if (!emit_slot_address(function, ORION_R14, instruction->result))
        return false;

    if (dynamic) {
        if (!emit_operand_chunk(function, &operands[1], 0, ORION_R12))
            return false;
    } else {
        if (operands[1].kind != LIR_OPERAND_OFFSET)
            return fail(function, "static insert requires a constant offset");
        emit_mov_size(function->out, ORION_R12, operands[1].offset);
    }
    fputs("    add r14, r14, r12\n", function->out);

    HirType *value_type = operands[2].type;
    size_t value_size = type_size(value_type);
    if (type_is_aggregate(value_type)) {
        if (!emit_operand_address(function, &operands[2], ORION_R13))
            return false;
        return emit_copy_bytes(function, ORION_R13, 0, ORION_R14, 0, value_size);
    }
    if (value_size == 8) {
        if (!emit_operand_chunk(function, &operands[2], 0, ORION_R0) ||
            !emit_operand_chunk(function, &operands[2], 4, ORION_R1))
            return false;
        emit_store_word_at(function, ORION_R14, 0, ORION_R0);
        emit_store_word_at(function, ORION_R14, 4, ORION_R1);
        return true;
    }
    if (!emit_operand_chunk(function, &operands[2], 0, ORION_R0))
        return false;
    return emit_store_scalar_memory(function, ORION_R14, 0, value_type, ORION_R0);
}

static bool emit_cast(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operand = (LirOperand *)instruction->operands.data;
    HirType *source_type = operand[0].type;
    HirType *target_type = instruction->result_type;
    size_t source_size = type_size(source_type);
    size_t target_size = type_size(target_type);

    if (!type_is_scalar(source_type) || !type_is_scalar(target_type)) {
        if (type_is_aggregate(source_type) && type_is_aggregate(target_type) && source_size == target_size && operand[0].kind == LIR_OPERAND_VALUE)
            return emit_copy_slot_to_slot(function, operand[0].value, instruction->result, source_size);
        return fail(function, "unsupported cast involving non-scalar type");
    }

    if (!emit_operand_chunk(function, &operand[0], 0, ORION_R0))
        return false;
    if (target_size == 8) {
        if (source_size == 8) {
            if (!emit_operand_chunk(function, &operand[0], 4, ORION_R1))
                return false;
        } else if (type_is_signed(source_type)) {
            fputs("    ashr r1, r0, #31\n", function->out);
        } else {
            emit_mov_imm(function->out, ORION_R1, 0);
        }
        return emit_store_result_word(function, instruction->result, 0, ORION_R0) &&
               emit_store_result_word(function, instruction->result, 4, ORION_R1);
    }
    return emit_store_result_word(function, instruction->result, 0, ORION_R0);
}

static bool emit_instruction(OrionFunction *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    if (instruction->result != LIR_INVALID_VALUE &&
        !type_is_supported_value(instruction->result_type))
        return fail(function, "instruction result type is not supported on Orion");

    switch (instruction->opcode) {
        case LIR_OP_NEG:
        case LIR_OP_NOT:
            return emit_unary(function, instruction);

        case LIR_OP_ADD:
        case LIR_OP_SUB:
        case LIR_OP_MUL:
        case LIR_OP_DIV:
        case LIR_OP_MOD:
        case LIR_OP_SHL:
        case LIR_OP_SHR:
        case LIR_OP_AND:
        case LIR_OP_OR:
        case LIR_OP_XOR:
            return emit_binary(function, instruction);

        case LIR_OP_LT:
        case LIR_OP_LE:
        case LIR_OP_GT:
        case LIR_OP_GE:
        case LIR_OP_EQ:
        case LIR_OP_NE:
            return emit_compare(function, instruction);

        case LIR_OP_CAST:
            return emit_cast(function, instruction);

        case LIR_OP_LOAD:
            return emit_load(function, instruction);

        case LIR_OP_STORE:
            return emit_store(function, instruction);

        case LIR_OP_ADDR:
            if (operands[0].kind == LIR_OPERAND_SYMBOL) {
                if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0))
                    return false;
            } else if (operands[0].kind == LIR_OPERAND_VALUE) {
                if (!emit_slot_address(function, ORION_R0, operands[0].value))
                    return false;
            } else {
                return fail(function, "addr requires a symbol or an LIR value");
            }
            return emit_store_result_word(function, instruction->result, 0, ORION_R0);

        case LIR_OP_ADDR_ADD:
            if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
                !emit_operand_chunk(function, &operands[1], 0, ORION_R1))
                return false;
            fputs("    add r0, r0, r1\n", function->out);
            return emit_store_result_word(function, instruction->result, 0, ORION_R0);

        case LIR_OP_ADDR_DIFF:
            if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
                !emit_operand_chunk(function, &operands[1], 0, ORION_R1))
                return false;
            fputs("    sub r0, r0, r1\n", function->out);
            if (instruction->operands.len >= 3) {
                if (!emit_operand_chunk(function, &operands[2], 0, ORION_R2))
                    return false;
                fputs("    div r0, r0, r2\n", function->out);
            }
            if (type_size(instruction->result_type) == 8) {
                fputs("    ashr r1, r0, #31\n", function->out);
                return emit_store_result_word(function, instruction->result, 0, ORION_R0) &&
                       emit_store_result_word(function, instruction->result, 4, ORION_R1);
            }
            return emit_store_result_word(function, instruction->result, 0, ORION_R0);

        case LIR_OP_CALL:
            return emit_call(function, instruction);

        case LIR_OP_EXTRACT:
            return emit_extract(function, instruction, false);
        case LIR_OP_EXTRACT_DYNAMIC:
            return emit_extract(function, instruction, true);
        case LIR_OP_INSERT:
            return emit_insert(function, instruction, false);
        case LIR_OP_INSERT_DYNAMIC:
            return emit_insert(function, instruction, true);

        case LIR_OP_ZERO:
            return emit_zero_slot(function, instruction->result, type_size(instruction->result_type));

        case LIR_OP_STACK_SAVE:
            emit_reg_copy(function->out, ORION_R0, ORION_SP);
            return emit_store_result_word(function, instruction->result, 0, ORION_R0);

        case LIR_OP_STACK_RESTORE:
            if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0))
                return false;
            emit_reg_copy(function->out, ORION_SP, ORION_R0);
            return true;

        case LIR_OP_STACK_ALLOC: {
            if (instruction->operands.len < 2)
                return fail(function, "stack_alloc requires a count and element size");
            if (!emit_operand_chunk(function, &operands[0], 0, ORION_R0) ||
                !emit_operand_chunk(function, &operands[1], 0, ORION_R1))
                return false;
            fputs("    mul r0, r0, r1\n", function->out);
            if (instruction->operands.len >= 3) {
                if (operands[2].kind != LIR_OPERAND_OFFSET || operands[2].offset == 0 ||
                    (operands[2].offset & (operands[2].offset - 1)) != 0)
                    return fail(function, "stack_alloc alignment must be a nonzero power-of-two constant");
                if (!emit_operand_chunk(function, &operands[2], 0, ORION_R2))
                    return false;
            } else {
                emit_mov_imm(function->out, ORION_R2, 8);
            }
            
            fputs("    sub r1, r2, #1\n", function->out);
            emit_bitwise_not(function->out, ORION_R1);
            fputs("    sub r15, r15, r0\n    and r15, r15, r1\n", function->out);
            emit_reg_copy(function->out, ORION_R0, ORION_SP);
            return emit_store_result_word(function, instruction->result, 0, ORION_R0);
        }
    }

    return fail(function, "unknown or unsupported LIR opcode");
}

static bool emit_terminator(OrionFunction *function, LirBlock *block) {
    switch (block->terminator.kind) {
        case LIR_TERM_JUMP:
            if (!emit_edge(function, block->terminator.jump.target, block->terminator.jump.args))
                return false;
            fputs("    jmp ", function->out);
            emit_block_ref(function, block->terminator.jump.target);
            fputc('\n', function->out);
            return true;

        case LIR_TERM_BRANCH: {
            LirOperand *condition = &block->terminator.branch.condition;
            if (!emit_operand_chunk(function, condition, 0, ORION_R0))
                return false;
            if (condition->type != NULL && type_size(condition->type) == 8) {
                if (!emit_operand_chunk(function, condition, 4, ORION_R1))
                    return false;
                fputs("    or r0, r0, r1\n", function->out);
            }
            
            size_t else_dispatch = next_label(function);
            fputs("    jne r0, #0, #1\n    jmp ", function->out);
            emit_local_ref(function, else_dispatch);
            fputc('\n', function->out);
            if (!emit_edge(function, block->terminator.branch.then_block, block->terminator.branch.then_args))
                return false;
            fputs("    jmp ", function->out);
            emit_block_ref(function, block->terminator.branch.then_block);
            fputc('\n', function->out);
            emit_local_label(function, else_dispatch);
            fputs(":\n", function->out);
            if (!emit_edge(function, block->terminator.branch.else_block, block->terminator.branch.else_args))
                return false;
            fputs("    jmp ", function->out);
            emit_block_ref(function, block->terminator.branch.else_block);
            fputc('\n', function->out);
            return true;
        }

        case LIR_TERM_RETURN: {
            if (block->terminator._return.has_value) {
                LirOperand *value = &block->terminator._return.value;
                size_t size = type_size(value->type);
                if (type_is_aggregate(value->type)) {
                    if (size > 16)
                        return fail(function, "aggregate returns larger than 16 bytes are not supported");
                    for (size_t chunk = 0; chunk < size; chunk += 4) {
                        if (!emit_operand_chunk(function, value, chunk, (int)(chunk / 4)))
                            return false;
                    }
                } else if (size == 8) {
                    if (!emit_operand_chunk(function, value, 0, ORION_R0) ||
                        !emit_operand_chunk(function, value, 4, ORION_R1))
                        return false;
                } else if (!type_is_void_like(value->type)) {
                    if (!emit_operand_chunk(function, value, 0, ORION_R0))
                        return false;
                }
            }
            return emit_return_epilogue(function);
        }

        case LIR_TERM_NONE:
            return fail(function, "unterminated LIR block");

        case LIR_TERM_UNREACHABLE:
            fputs("    halt\n", function->out);
            return true;
    }
    return fail(function, "unknown LIR terminator");
}

static bool gather_types_and_size_frame(OrionFunction *function) {
    function->slot_count = function->function->next_value;
    size_t count = function->slot_count == 0 ? 1 : function->slot_count;
    function->slots = calloc(count, sizeof(*function->slots));
    function->value_types = calloc(count, sizeof(*function->value_types));
    if (function->slots == NULL || function->value_types == NULL)
        return fail(function, "out of memory while allocating LIR stack slots");

    for (size_t i = 0; i < function->function->params.len; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)function->function->params.data)[i];
        if (!record_value_type(function, param->value, param->type))
            return false;
    }

    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        for (size_t j = 0; j < block->params.len; j++) {
            LirBlockParam *param = &((LirBlockParam *)block->params.data)[j];
            if (!record_value_type(function, param->value, param->type))
                return false;
        }
        for (size_t j = 0; j < block->instructions.len; j++) {
            LirInstruction *instruction = &((LirInstruction *)block->instructions.data)[j];
            if (instruction->result != LIR_INVALID_VALUE &&
                !record_value_type(function, instruction->result, instruction->result_type))
                return false;
            for (size_t k = 0; k < instruction->operands.len; k++)
                if (!record_operand_type(function, &((LirOperand *)instruction->operands.data)[k]))
                    return false;
        }

        switch (block->terminator.kind) {
            case LIR_TERM_RETURN:
                if (block->terminator._return.has_value &&
                    !record_operand_type(function, &block->terminator._return.value))
                    return false;
                break;
            case LIR_TERM_JUMP:
                for (size_t k = 0; k < block->terminator.jump.args.len; k++)
                    if (!record_operand_type(function, &((LirOperand *)block->terminator.jump.args.data)[k]))
                        return false;
                break;
            case LIR_TERM_BRANCH:
                if (!record_operand_type(function, &block->terminator.branch.condition))
                    return false;
                for (size_t k = 0; k < block->terminator.branch.then_args.len; k++)
                    if (!record_operand_type(function, &((LirOperand *)block->terminator.branch.then_args.data)[k]))
                        return false;
                for (size_t k = 0; k < block->terminator.branch.else_args.len; k++)
                    if (!record_operand_type(function, &((LirOperand *)block->terminator.branch.else_args.data)[k]))
                        return false;
                break;
            case LIR_TERM_NONE:
            case LIR_TERM_UNREACHABLE:
                break;
        }
    }

    size_t cursor = 0;
    for (size_t i = 0; i < function->slot_count; i++) {
        HirType *type = function->value_types[i];
        if (type == NULL)
            return fail(function, "unused LIR value ID has no type");
        if (!type_is_supported_value(type)) {
            HirType *resolved = base_type(type);
            String function_name = symbol_link_name(function->function->symbol);
            fprintf(stderr,
                    "Orion backend: unsupported LIR value in %.*s: id=%zu "
                    "raw(kind=%d,size=%zu,align=%zu,base=%p) "
                    "resolved(kind=%d,size=%zu,align=%zu) "
                    "effective(size=%zu,align=%zu) "
                    "builtin=%d scalar=%d aggregate=%d void=%d",
                    (int)function_name.length,
                    function_name.data != NULL ? function_name.data : "",
                    i,
                    (int)type->kind, type->size, type->align, (void *)type->base,
                    (int)resolved->kind, resolved->size, resolved->align,
                    type_size(type), type_align(type),
                    resolved->kind == HIR_TYPE_BUILTIN ? (int)resolved->builtin : -1,
                    type_is_scalar(type), type_is_aggregate(type), type_is_void_like(type));
            if (resolved->kind == HIR_TYPE_POINTER && resolved->pointer.pointee != NULL) {
                HirType *pointee = base_type(resolved->pointer.pointee);
                fprintf(stderr, " pointee(kind=%d,size=%zu,align=%zu)",
                        (int)pointee->kind, pointee->size, pointee->align);
            }
            fputc('\n', stderr);
            return false;
        }

        size_t actual_size = type_size(type);
        size_t storage_size = align_up(actual_size < 4 ? 4 : actual_size, 4);
        size_t alignment = type_align(type);
        if (alignment < 4)
            alignment = 4;
        size_t offset_base = cursor > SIZE_MAX - storage_size ? SIZE_MAX : cursor + storage_size;
        size_t offset = align_up(offset_base, alignment);
        if (offset == SIZE_MAX)
            return fail(function, "stack frame size overflow");
        function->slots[i].offset = offset;
        function->slots[i].storage_size = storage_size;
        cursor = offset;
    }

    function->edge_scratch_size = 0;
    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        if (block->terminator.kind == LIR_TERM_JUMP) {
            LirBlock *target = find_block(function, block->terminator.jump.target);
            if (target == NULL)
                return fail(function, "jump targets an unknown block");
            size_t size = block_param_storage(target);
            if (size == SIZE_MAX)
                return fail(function, "edge scratch size overflow");
            if (size > function->edge_scratch_size)
                function->edge_scratch_size = size;
        } else if (block->terminator.kind == LIR_TERM_BRANCH) {
            LirBlock *then_target = find_block(function, block->terminator.branch.then_block);
            LirBlock *else_target = find_block(function, block->terminator.branch.else_block);
            if (then_target == NULL || else_target == NULL)
                return fail(function, "branch targets an unknown block");
            size_t then_size = block_param_storage(then_target);
            size_t else_size = block_param_storage(else_target);
            if (then_size == SIZE_MAX || else_size == SIZE_MAX)
                return fail(function, "edge scratch size overflow");
            if (then_size > function->edge_scratch_size)
                function->edge_scratch_size = then_size;
            if (else_size > function->edge_scratch_size)
                function->edge_scratch_size = else_size;
        }
    }

    function->edge_scratch_offset = cursor;
    if (function->edge_scratch_size != 0) {
        size_t base = cursor > SIZE_MAX - function->edge_scratch_size ? SIZE_MAX : cursor + function->edge_scratch_size;
        function->edge_scratch_offset = align_up(base, 8);
        if (function->edge_scratch_offset == SIZE_MAX)
            return fail(function, "edge scratch frame overflow");
        cursor = function->edge_scratch_offset;
    }
    function->frame_size = align_up(cursor, 8);
    if (function->frame_size == SIZE_MAX)
        return fail(function, "stack frame size overflow");
    return true;
}

static bool emit_function(OrionFunction *function) {
    String name = symbol_link_name(function->function->symbol);
    if (name.data == NULL || name.length == 0)
        return fail(function, "function has no link name");
    if (function->function->blocks.len == 0)
        return fail(function, "function has no blocks");

    LirBlock *entry = find_block(function, function->function->entry);
    if (entry == NULL)
        return fail(function, "function entry block does not exist");

    /* Function parameters are represented twice in LIR: the function
     * signature records their ABI order, while the entry block exposes the
     * same values as block parameters. The prologue initializes those value
     * slots directly from the incoming argument registers/stack. */
    if (entry->params.len != function->function->params.len)
        return fail(function, "entry block parameters do not match function parameters");
    for (size_t i = 0; i < entry->params.len; i++) {
        LirBlockParam *block_param = &((LirBlockParam *)entry->params.data)[i];
        LirFunctionParam *function_param = &((LirFunctionParam *)function->function->params.data)[i];
        if (block_param->value != function_param->value)
            return fail(function, "entry block parameter value does not match function parameter");
    }

    fprintf(function->out, "\n.section .text\n");
    if (function->function->is_export)
        fprintf(function->out, ".global %.*s\n", (int)name.length, name.data);
    fprintf(function->out, "%.*s:\n", (int)name.length, name.data);

    fputs("    sub r15, r15, #4\n", function->out);
    emit_store_word_at(function, ORION_SP, 0, ORION_FP);
    emit_reg_copy(function->out, ORION_FP, ORION_SP);
    emit_adjust_sp(function, true, function->frame_size);

    if (!emit_function_parameters(function))
        return false;

    if (((LirBlock **)function->function->blocks.data)[0]->id != function->function->entry) {
        fputs("    jmp ", function->out);
        emit_block_ref(function, function->function->entry);
        fputc('\n', function->out);
    }

    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];
        emit_block_label(function, block->id);
        for (size_t j = 0; j < block->instructions.len; j++) {
            if (!emit_instruction(function, &((LirInstruction *)block->instructions.data)[j]))
                return false;
        }
        if (!emit_terminator(function, block))
            return false;
    }
    return !ferror(function->out);
}

static bool emit(Backend *backend, const TargetInfo *target, const LirModule *module, FILE *output) {
    (void)backend;
    if (!supports(target))
        return false;

    for (size_t i = 0; i < module->globals.len; i++)
        if (!emit_global(output, &((LirGlobal *)module->globals.data)[i]))
            return false;

    for (size_t i = 0; i < module->strings.len; i++)
        emit_string(output, &((LirString *)module->strings.data)[i]);

    for (size_t i = 0; i < module->functions.len; i++) {
        const LirFunction *lir = ((LirFunction **)module->functions.data)[i];
        if (lir == NULL || lir->is_extern)
            continue;

        OrionFunction function = {
            .out = output,
            .target = target,
            .function = lir,
            .function_index = i,
            .next_label_id = 0,
        };
        bool ok = gather_types_and_size_frame(&function) && emit_function(&function);
        free(function.slots);
        free(function.value_types);
        if (!ok)
            return false;
    }
    return !ferror(output);
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
