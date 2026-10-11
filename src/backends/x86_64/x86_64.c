#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "x86_64.h"

typedef struct {
    int64_t offset;
    size_t size;
} X86Slot;

typedef enum {
    X86_RAX,
    X86_RCX,
    X86_RDX,
    X86_RSI,
    X86_RDI,
    X86_R8,
    X86_R9,
    X86_R10,
    X86_R11,
} X86Reg;

typedef struct {
    FILE *out;
    const TargetInfo *target;
    const LirFunction *function;
    size_t id;
    X86Slot *slots;
    size_t slot_count;
    size_t frame_size;
    int64_t return_address_offset;
} X86Function;

static bool function_returns_indirect(X86Function *function);

static size_t type_size(HirType *type) {
    assert(type != NULL);
    return type->size;
}

static size_t type_align(HirType *type) {
    assert(type != NULL);
    return type->align;
}

static size_t align_up(size_t value, size_t align) {
    assert(align != 0);
    return (value + align - 1) & ~(align - 1);
}

static HirType *base_type(HirType *type) {
    while (type != NULL && type->base != NULL)
        type = type->base;

    return type;
}

static bool type_is_signed(HirType *type) {
    type = base_type(type);

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

static const char *reg_name(X86Reg reg, size_t size) {
    static const char *names[][4] = {
        [X86_RAX] = {"al", "ax", "eax", "rax"},
        [X86_RCX] = {"cl", "cx", "ecx", "rcx"},
        [X86_RDX] = {"dl", "dx", "edx", "rdx"},
        [X86_RSI] = {"sil", "si", "esi", "rsi"},
        [X86_RDI] = {"dil", "di", "edi", "rdi"},
        [X86_R8] = {"r8b", "r8w", "r8d", "r8"},
        [X86_R9] = {"r9b", "r9w", "r9d", "r9"},
        [X86_R10] = {"r10b", "r10w", "r10d", "r10"},
        [X86_R11] = {"r11b", "r11w", "r11d", "r11"},
    };

    switch (size) {
        case 1: return names[reg][0];
        case 2: return names[reg][1];
        case 4: return names[reg][2];
        case 8: return names[reg][3];
    }

    assert(!"invalid register size");
    return NULL;
}

static void emit_block_label(X86Function *function, LirBlockId block) {
    fprintf(function->out, ".L%zu_%u:\n", function->id, block);
}

static void emit_block_jump(X86Function *function, LirBlockId block) {
    fprintf(function->out, "    jmp .L%zu_%u\n", function->id, block);
}

static const char *size_name(size_t size) {
    switch (size) {
        case 1: return "byte ptr";
        case 2: return "word ptr";
        case 4: return "dword ptr";
        case 8: return "qword ptr";
    }

    assert(!"invalid operand size");
    return NULL;
}

typedef enum {
    X86_ABI_NO_CLASS,
    X86_ABI_INTEGER,
    X86_ABI_SSE,
    X86_ABI_MEMORY,
} X86AbiClass;

typedef struct {
    X86AbiClass classes[2];
    size_t count;
    size_t size;
    bool memory;
} X86AbiClassification;

typedef struct {
    X86AbiClassification classification;
    bool stack;
    size_t stack_offset;
    size_t gp_register[2];
    size_t fp_register[2];
} X86AbiArgument;

static bool abi_is_aggregate(HirType *type) {
    type = base_type(type);
    if (type == NULL)
        return false;

    return type->kind == HIR_TYPE_STRUCT || type->kind == HIR_TYPE_UNION ||
           type->kind == HIR_TYPE_ARRAY || type->kind == HIR_TYPE_SLICE ||
           type->kind == HIR_TYPE_SUM;
}

static X86AbiClass abi_merge_class(X86AbiClass left, X86AbiClass right) {
    if (left == X86_ABI_NO_CLASS)
        return right;
    if (right == X86_ABI_NO_CLASS || left == right)
        return left;
    if (left == X86_ABI_MEMORY || right == X86_ABI_MEMORY)
        return X86_ABI_MEMORY;
    return X86_ABI_INTEGER;
}

static void abi_mark(X86AbiClassification *classification, size_t offset, size_t size, X86AbiClass class_) {
    if (size == 0)
        return;
    if (offset > classification->size || size > classification->size - offset) {
        classification->memory = true;
        return;
    }

    size_t first = offset / 8;
    size_t last = (offset + size - 1) / 8;
    for (size_t i = first; i <= last; i++) {
        if (i >= 2) {
            classification->memory = true;
            return;
        }
        classification->classes[i] = abi_merge_class(classification->classes[i], class_);
        if (classification->classes[i] == X86_ABI_MEMORY)
            classification->memory = true;
    }
}

static void abi_classify_at(HirType *type, size_t offset, X86AbiClassification *classification) {
    type = base_type(type);
    if (type == NULL || type->size == 0 || classification->memory)
        return;
    if (offset > classification->size || type->size > classification->size - offset) {
        classification->memory = true;
        return;
    }
    if (type->size > 16) {
        classification->memory = true;
        return;
    }
    if (type_is_float(type)) {
        abi_mark(classification, offset, type->size, X86_ABI_SSE);
        return;
    }

    switch (type->kind) {
        case HIR_TYPE_BUILTIN:
        case HIR_TYPE_POINTER:
        case HIR_TYPE_ENUM:
            abi_mark(classification, offset, type->size, X86_ABI_INTEGER);
            return;

        case HIR_TYPE_STRUCT:
            for (size_t i = 0; i < type->structure.fields.len; i++) {
                HirField *field = &((HirField *)type->structure.fields.data)[i];
                size_t field_offset = offset + field->offset;
                size_t field_align = type_align(field->type);
                if (field_align != 0 && field_offset % field_align != 0) {
                    classification->memory = true;
                    return;
                }
                abi_classify_at(field->type, field_offset, classification);
                if (classification->memory)
                    return;
            }
            return;

        case HIR_TYPE_UNION:
            for (size_t i = 0; i < type->union_.fields.len; i++) {
                HirField *field = &((HirField *)type->union_.fields.data)[i];
                size_t field_align = type_align(field->type);
                if (field_align != 0 && offset % field_align != 0) {
                    classification->memory = true;
                    return;
                }
                abi_classify_at(field->type, offset, classification);
                if (classification->memory)
                    return;
            }
            return;

        case HIR_TYPE_SLICE:
            for (size_t i = 0; i < type->slice.fields.len; i++) {
                HirField *field = &((HirField *)type->slice.fields.data)[i];
                size_t field_offset = offset + field->offset;
                size_t field_align = type_align(field->type);
                if (field_align != 0 && field_offset % field_align != 0) {
                    classification->memory = true;
                    return;
                }
                abi_classify_at(field->type, field_offset, classification);
                if (classification->memory)
                    return;
            }
            return;

        case HIR_TYPE_ARRAY: {
            if (type->array.length == 0)
                return;
            abi_mark(classification, offset, sizeof(uint64_t), X86_ABI_INTEGER);
            if (type_size(type->array.element) == 0)
                return;
            size_t data_offset = align_up(sizeof(uint64_t), type_align(type->array.element));
            for (uint64_t i = 0; i < type->array.length; i++) {
                size_t element_offset = data_offset + (size_t)i * type_size(type->array.element);
                if (element_offset >= type->size)
                    break;
                abi_classify_at(type->array.element, offset + element_offset, classification);
                if (classification->memory)
                    return;
            }
            return;
        }

        case HIR_TYPE_SUM:
            classification->memory = true;
            return;

        case HIR_TYPE_ERROR:
        case HIR_TYPE_FUNCTION:
            classification->memory = true;
            return;
    }

    classification->memory = true;
}

static X86AbiClassification abi_classify_type(HirType *type) {
    X86AbiClassification classification = {0};
    type = base_type(type);
    if (type == NULL)
        return classification;

    classification.size = type->size;
    classification.count = type->size > 16 ? 2 : (type->size + 7) / 8;
    if (type->size == 0)
        return classification;
    if (type->size > 16) {
        classification.memory = true;
        return classification;
    }

    if (!abi_is_aggregate(type)) {
        classification.classes[0] = type_is_float(type) ? X86_ABI_SSE : X86_ABI_INTEGER;
        return classification;
    }

    abi_classify_at(type, 0, &classification);
    if (classification.memory)
        return classification;

    for (size_t i = 0; i < classification.count; i++) {
        if (classification.classes[i] == X86_ABI_NO_CLASS)
            classification.classes[i] = X86_ABI_INTEGER;
    }
    return classification;
}

static bool function_returns_indirect(X86Function *function) {
    return function->function->return_type != NULL &&
           abi_classify_type(function->function->return_type).memory;
}

static size_t abi_assign_arguments(HirType **types, size_t count, bool hidden_return, X86AbiArgument *arguments) {
    size_t gp_count = hidden_return ? 1 : 0;
    size_t fp_count = 0;
    size_t stack_size = 0;

    for (size_t i = 0; i < count; i++) {
        X86AbiArgument *argument = &arguments[i];
        argument->classification = abi_classify_type(types[i]);
        argument->gp_register[0] = argument->gp_register[1] = SIZE_MAX;
        argument->fp_register[0] = argument->fp_register[1] = SIZE_MAX;

        size_t size = type_size(types[i]);
        if (size == 0 || argument->classification.count == 0)
            continue;

        size_t gp_needed = 0;
        size_t fp_needed = 0;
        for (size_t j = 0; j < argument->classification.count; j++) {
            if (argument->classification.classes[j] == X86_ABI_INTEGER)
                gp_needed++;
            else if (argument->classification.classes[j] == X86_ABI_SSE)
                fp_needed++;
        }

        if (argument->classification.memory || gp_count + gp_needed > 6 || fp_count + fp_needed > 8) {
            argument->stack = true;
            size_t alignment = type_align(types[i]);
            if (alignment < 8)
                alignment = 8;
            argument->stack_offset = align_up(stack_size, alignment);
            size_t stack_slot_size = align_up(size < 8 ? 8 : size, 8);
            stack_size = argument->stack_offset + stack_slot_size;
            continue;
        }

        for (size_t j = 0; j < argument->classification.count; j++) {
            if (argument->classification.classes[j] == X86_ABI_INTEGER)
                argument->gp_register[j] = gp_count++;
            else if (argument->classification.classes[j] == X86_ABI_SSE)
                argument->fp_register[j] = fp_count++;
        }
    }

    return stack_size == 0 ? 0 : align_up(stack_size, 16);
}

static size_t abi_chunk_size(HirType *type, size_t chunk) {
    size_t size = type_size(type);
    size_t offset = chunk * 8;
    assert(offset < size);
    return size - offset < 8 ? size - offset : 8;
}

static void abi_format_memory(char *buffer, size_t capacity, const char *base, size_t offset) {
    if (offset == 0)
        snprintf(buffer, capacity, "[%s]", base);
    else
        snprintf(buffer, capacity, "[%s+%zu]", base, offset);
}

static void emit_abi_load_gp_chunk(X86Function *function, X86Reg destination, const char *base, size_t offset, size_t size) {
    char memory[64];
    abi_format_memory(memory, sizeof(memory), base, offset);
    if (size == 8) {
        fprintf(function->out, "    mov %s, qword ptr %s\n", reg_name(destination, 8), memory);
    } else if (size == 4) {
        fprintf(function->out, "    mov %s, dword ptr %s\n", reg_name(destination, 4), memory);
    } else if (size == 2) {
        fprintf(function->out, "    movzx %s, word ptr %s\n", reg_name(destination, 8), memory);
    } else if (size == 1) {
        fprintf(function->out, "    movzx %s, byte ptr %s\n", reg_name(destination, 8), memory);
    } else {
        fprintf(function->out, "    xor %s, %s\n", reg_name(destination, 4), reg_name(destination, 4));
        for (size_t i = 0; i < size; i++) {
            abi_format_memory(memory, sizeof(memory), base, offset + i);
            fprintf(function->out, "    movzx r10, byte ptr %s\n", memory);
            if (i != 0)
                fprintf(function->out, "    shl r10, %zu\n", i * 8);
            fprintf(function->out, "    or %s, r10\n", reg_name(destination, 8));
        }
    }
}

static void emit_abi_store_gp_chunk(X86Function *function, const char *base, size_t offset, X86Reg source, size_t size) {
    char memory[64];
    abi_format_memory(memory, sizeof(memory), base, offset);
    if (size == 8) {
        fprintf(function->out, "    mov qword ptr %s, %s\n", memory, reg_name(source, 8));
    } else if (size == 4) {
        fprintf(function->out, "    mov dword ptr %s, %s\n", memory, reg_name(source, 4));
    } else if (size == 2) {
        fprintf(function->out, "    mov word ptr %s, %s\n", memory, reg_name(source, 2));
    } else if (size == 1) {
        fprintf(function->out, "    mov byte ptr %s, %s\n", memory, reg_name(source, 1));
    } else {
        fprintf(function->out, "    mov r10, %s\n", reg_name(source, 8));
        for (size_t i = 0; i < size; i++) {
            abi_format_memory(memory, sizeof(memory), base, offset + i);
            fprintf(function->out, "    mov byte ptr %s, r10b\n", memory);
            if (i + 1 < size)
                fprintf(function->out, "    shr r10, 8\n");
        }
    }
}

static void emit_abi_load_sse_chunk(X86Function *function, const char *destination, const char *base, size_t offset, size_t size) {
    char memory[64];
    abi_format_memory(memory, sizeof(memory), base, offset);
    if (size == 8) {
        fprintf(function->out, "    movq %s, qword ptr %s\n", destination, memory);
    } else if (size == 4) {
        fprintf(function->out, "    movd %s, dword ptr %s\n", destination, memory);
    } else {
        emit_abi_load_gp_chunk(function, X86_R10, base, offset, size);
        fprintf(function->out, "    movq %s, r10\n", destination);
    }
}

static void emit_abi_store_sse_chunk(X86Function *function, const char *base, size_t offset, const char *source, size_t size) {
    char memory[64];
    abi_format_memory(memory, sizeof(memory), base, offset);
    if (size == 8) {
        fprintf(function->out, "    movq qword ptr %s, %s\n", memory, source);
    } else if (size == 4) {
        fprintf(function->out, "    movd dword ptr %s, %s\n", memory, source);
    } else {
        fprintf(function->out, "    movq r10, %s\n", source);
        for (size_t i = 0; i < size; i++) {
            abi_format_memory(memory, sizeof(memory), base, offset + i);
            fprintf(function->out, "    mov byte ptr %s, r10b\n", memory);
            if (i + 1 < size)
                fprintf(function->out, "    shr r10, 8\n");
        }
    }
}


static X86Slot *slot(X86Function *function, LirValueId value) {
    assert(value < function->slot_count);
    return &function->slots[value];
}

static void allocate_slots(X86Function *function) {
    function->slot_count = function->function->next_value;

    if (function->slot_count == 0) {
        function->slots = NULL;
        function->frame_size = function_returns_indirect(function) ? 16 : 0;
        function->return_address_offset = function_returns_indirect(function) ? -8 : 0;
        return;
    }

    function->slots = calloc(function->slot_count, sizeof(*function->slots));
    assert(function->slots != NULL);

    size_t offset = function_returns_indirect(function) ? 8 : 0;
    function->return_address_offset = function_returns_indirect(function) ? -8 : 0;

    for (size_t i = 0; i < function->function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)function->function->blocks.data)[i];

        for (size_t j = 0; j < block->params.len; j++) {
            LirBlockParam *param = &((LirBlockParam *)block->params.data)[j];

            size_t size = type_size(param->type);
            size_t align = type_align(param->type);

            if (size == 0)
                continue;

            offset = align_up(offset, align);
            offset += size;

            function->slots[param->value] = (X86Slot) {
                .offset = -(int64_t)offset,
                .size = size,
            };
        }

        for (size_t j = 0; j < block->instructions.len; j++) {
            LirInstruction *instruction = &((LirInstruction *)block->instructions.data)[j];

            if (instruction->result == LIR_INVALID_VALUE)
                continue;

            size_t size = type_size(instruction->result_type);
            size_t align = type_align(instruction->result_type);

            if (size == 0)
                continue;

            offset = align_up(offset, align);
            offset += size;

            function->slots[instruction->result] = (X86Slot) {
                .offset = -(int64_t)offset,
                .size = size,
            };
        }
    }

    function->frame_size = align_up(offset, 16);
}

static void emit_memory_load(X86Function *function, X86Reg address, X86Reg reg, size_t size, bool signed_) {
    const char *address_name = reg_name(address, 8);

    if (size == 1)
        fprintf(function->out, signed_ ? "    movsx %s, byte ptr [%s]\n" : "    movzx %s, byte ptr [%s]\n", reg_name(reg, 8), address_name);
    else if (size == 2)
        fprintf(function->out, signed_ ? "    movsx %s, word ptr [%s]\n" : "    movzx %s, word ptr [%s]\n", reg_name(reg, 8), address_name);
    else if (size == 4)
        fprintf(function->out, signed_ ? "    movsxd %s, dword ptr [%s]\n" : "    mov %s, dword ptr [%s]\n", reg_name(reg, signed_ ? 8 : 4), address_name);
    else if (size == 8)
        fprintf(function->out, "    mov %s, qword ptr [%s]\n", reg_name(reg, 8), address_name);
    else
        assert(!"invalid scalar size");
}

static void emit_memory_store(X86Function *function, X86Reg address, X86Reg reg, size_t size) {
    assert(size <= 8);
    fprintf(function->out, "    mov %s [%s], %s\n", size_name(size), reg_name(address, 8), reg_name(reg, size));
}


static void emit_copy_memory(X86Function *function, X86Reg destination, X86Reg source, size_t size) {
    if (size == 0)
        return;

    fprintf(function->out, "    mov rcx, %zu\n", size);
    fprintf(function->out, "    rep movsb\n");
}

static void emit_copy_stack(X86Function *function, X86Slot *destination, X86Slot *source, size_t size) {
    if (size == 0)
        return;

    fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
    fprintf(function->out, "    lea rsi, [rbp%ld]\n", source->offset);
    emit_copy_memory(function, X86_RDI, X86_RSI, size);
}

static void emit_zero(X86Function *function, const LirInstruction *instruction) {
    X86Slot *destination = slot(function, instruction->result);
    size_t size = type_size(instruction->result_type);

    if (size == 0)
        return;

    fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
    fprintf(function->out, "    xor eax, eax\n");
    fprintf(function->out, "    mov rcx, %zu\n", size);
    fprintf(function->out, "    rep stosb\n");
}

static void load_operand(X86Function *function, const LirOperand *operand, X86Reg reg);
static void load_float_operand(X86Function *function, const LirOperand *operand, const char *reg);

static void emit_insert(X86Function *function, const LirInstruction *instruction, bool dynamic) {
    LirOperand *operands = instruction->operands.data;
    LirOperand *aggregate = &operands[0];
    LirOperand *offset = &operands[1];
    LirOperand *value = &operands[2];

    assert(aggregate->kind == LIR_OPERAND_VALUE);
    X86Slot *destination = slot(function, instruction->result);
    X86Slot *source = slot(function, aggregate->value);
    size_t aggregate_size = type_size(aggregate->type);
    size_t value_size = type_size(value->type);

    emit_copy_stack(function, destination, source, aggregate_size);

    if (dynamic) {
        fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
        load_operand(function, offset, X86_RCX);
        fprintf(function->out, "    add rdi, rcx\n");
    } else {
        assert(offset->kind == LIR_OPERAND_OFFSET);
        fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset + (int64_t)offset->offset);
    }

    if (value_size > 8) {
        assert(value->kind == LIR_OPERAND_VALUE);
        X86Slot *value_slot = slot(function, value->value);
        fprintf(function->out, "    lea rsi, [rbp%ld]\n", value_slot->offset);
        emit_copy_memory(function, X86_RDI, X86_RSI, value_size);
    } else if (type_is_float(value->type)) {
        load_float_operand(function, value, "xmm0");
        fprintf(function->out, value_size == 4 ? "    movss dword ptr [rdi], xmm0\n" : "    movsd qword ptr [rdi], xmm0\n");
    } else {
        load_operand(function, value, X86_RAX);
        emit_memory_store(function, X86_RDI, X86_RAX, value_size);
    }
}

static void store_register(X86Function *function, X86Reg reg, LirValueId value, size_t size);

static void emit_extract(X86Function *function, const LirInstruction *instruction, bool dynamic) {
    LirOperand *operands = instruction->operands.data;
    LirOperand *aggregate = &operands[0];
    LirOperand *offset = &operands[1];

    assert(aggregate->kind == LIR_OPERAND_VALUE);

    X86Slot *source = slot(function, aggregate->value);
    X86Slot *destination = slot(function, instruction->result);
    size_t result_size = type_size(instruction->result_type);

    if (dynamic) {
        fprintf(function->out, "    lea rsi, [rbp%ld]\n", source->offset);
        load_operand(function, offset, X86_RCX);
        fprintf(function->out, "    add rsi, rcx\n");
    } else {
        assert(offset->kind == LIR_OPERAND_OFFSET);
        fprintf(function->out, "    lea rsi, [rbp%ld]\n", source->offset + (int64_t)offset->offset);
    }

    if (result_size > 8) {
        fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
        emit_copy_memory(function, X86_RDI, X86_RSI, result_size);
    } else {
        emit_memory_load(function, X86_RSI, X86_RAX, result_size, type_is_signed(instruction->result_type));
        store_register(function, X86_RAX, instruction->result, result_size);
    }
}

static void load_operand(X86Function *function, const LirOperand *operand, X86Reg reg) {
    if (operand->kind == LIR_OPERAND_OFFSET) {
        fprintf(function->out, "    mov %s, %zu\n", reg_name(reg, 8), operand->offset);
        return;
    }

    size_t size = type_size(operand->type);

    assert(size <= 8);

    switch (operand->kind) {
        case LIR_OPERAND_VALUE: {
            X86Slot *s = slot(function, operand->value);

            if (size == 1) {
                fprintf(function->out, type_is_signed(operand->type) ? "    movsx %s, byte ptr [rbp%ld]\n" : "    movzx %s, byte ptr [rbp%ld]\n", reg_name(reg, 8), s->offset);
            } else if (size == 2) {
                fprintf(function->out, type_is_signed(operand->type) ? "    movsx %s, word ptr [rbp%ld]\n" : "    movzx %s, word ptr [rbp%ld]\n", reg_name(reg, 8), s->offset);
            } else if (size == 4) {
                if (type_is_signed(operand->type))
                    fprintf(function->out, "    movsxd %s, dword ptr [rbp%ld]\n", reg_name(reg, 8), s->offset);
                else
                    fprintf(function->out, "    mov %s, dword ptr [rbp%ld]\n", reg_name(reg, 4), s->offset);
            } else {
                fprintf(function->out, "    mov %s, qword ptr [rbp%ld]\n", reg_name(reg, 8), s->offset);
            }

            break;
        }

        case LIR_OPERAND_INT:
            fprintf(function->out, "    mov %s, %" PRId64 "\n", reg_name(reg, 8), operand->int_value);
            break;

        case LIR_OPERAND_UINT:
            fprintf(function->out, "    mov %s, %" PRIu64 "\n", reg_name(reg, 8), operand->uint_value);
            break;

        case LIR_OPERAND_BOOL:
            fprintf(function->out, "    mov %s, %d\n", reg_name(reg, 8), operand->bool_value ? 1 : 0);
            break;

        case LIR_OPERAND_SYMBOL:
        case LIR_OPERAND_FLOAT:
        case LIR_OPERAND_INVALID:
            assert(!"unsupported operand");
    }
}

static void store_register(X86Function *function, X86Reg reg, LirValueId value, size_t size) {
    X86Slot *s = slot(function, value);

    assert(size <= 8);

    fprintf(function->out, "    mov %s [rbp%ld], %s\n", size_name(size), s->offset, reg_name(reg, size));
}

static void load_float_operand(X86Function *function, const LirOperand *operand, const char *reg) {
    size_t size = type_size(operand->type);
    assert(size == 4 || size == 8);

    if (operand->kind == LIR_OPERAND_VALUE) {
        X86Slot *s = slot(function, operand->value);
        fprintf(function->out, size == 4 ? "    movss %s, dword ptr [rbp%ld]\n" : "    movsd %s, qword ptr [rbp%ld]\n", reg, s->offset);
        return;
    }

    double value;
    switch (operand->kind) {
        case LIR_OPERAND_FLOAT: value = operand->float_value; break;
        case LIR_OPERAND_INT: value = (double)operand->int_value; break;
        case LIR_OPERAND_UINT: value = (double)operand->uint_value; break;
        case LIR_OPERAND_BOOL: value = operand->bool_value ? 1.0 : 0.0; break;
        default: assert(!"unsupported floating-point operand"); return;
    }

    if (size == 4) {
        float narrowed = (float)value;
        uint32_t bits;
        memcpy(&bits, &narrowed, sizeof(bits));
        fprintf(function->out, "    mov eax, 0x%08" PRIx32 "\n", bits);
        fprintf(function->out, "    movd %s, eax\n", reg);
    } else {
        uint64_t bits;
        memcpy(&bits, &value, sizeof(bits));
        fprintf(function->out, "    mov rax, 0x%016" PRIx64 "\n", bits);
        fprintf(function->out, "    movq %s, rax\n", reg);
    }
}

static void store_float_register(X86Function *function, const char *reg, LirValueId value, size_t size) {
    X86Slot *s = slot(function, value);
    assert(size == 4 || size == 8);
    fprintf(function->out, size == 4 ? "    movss dword ptr [rbp%ld], %s\n" : "    movsd qword ptr [rbp%ld], %s\n", s->offset, reg);
}

static void emit_unary(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *operand = &((LirOperand *)instruction->operands.data)[0];
    size_t size = type_size(instruction->result_type);

    if (instruction->opcode == LIR_OP_NEG && type_is_float(instruction->result_type)) {
        load_float_operand(function, operand, "xmm0");
        if (size == 4) {
            fprintf(function->out, "    movd eax, xmm0\n    btc eax, 31\n    movd xmm0, eax\n");
        } else {
            fprintf(function->out, "    movq rax, xmm0\n    btc rax, 63\n    movq xmm0, rax\n");
        }
        store_float_register(function, "xmm0", instruction->result, size);
        return;
    }

    load_operand(function, operand, X86_RAX);

    if (instruction->opcode == LIR_OP_NEG) {
        fprintf(function->out, "    neg rax\n");
    } else {
        HirType *type = base_type(instruction->result_type);

        if (type->kind == HIR_TYPE_BUILTIN && type->builtin == BUILTIN_BOOL) {
            fprintf(function->out, "    cmp rax, 0\n");
            fprintf(function->out, "    sete al\n");
            fprintf(function->out, "    movzx eax, al\n");
        } else {
            fprintf(function->out, "    not rax\n");
        }
    }

    store_register(function, X86_RAX, instruction->result, size);
}

static void emit_binary(X86Function *function, const LirInstruction *instruction, const char *opcode) {
    LirOperand *operands = instruction->operands.data;

    if (type_is_float(instruction->result_type)) {
        size_t size = type_size(instruction->result_type);
        load_float_operand(function, &operands[0], "xmm0");
        load_float_operand(function, &operands[1], "xmm1");
        const char *float_opcode = NULL;
        switch (instruction->opcode) {
            case LIR_OP_ADD: float_opcode = size == 4 ? "addss" : "addsd"; break;
            case LIR_OP_SUB: float_opcode = size == 4 ? "subss" : "subsd"; break;
            case LIR_OP_MUL: float_opcode = size == 4 ? "mulss" : "mulsd"; break;
            default: assert(!"invalid floating-point binary opcode"); return;
        }
        fprintf(function->out, "    %s xmm0, xmm1\n", float_opcode);
        store_float_register(function, "xmm0", instruction->result, size);
        return;
    }

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);
    fprintf(function->out, "    %s rax, rcx\n", opcode);
    store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
}

static void emit_shift(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    if (instruction->opcode == LIR_OP_SHL)
        fprintf(function->out, "    shl rax, cl\n");
    else if (type_is_signed(operands[0].type))
        fprintf(function->out, "    sar rax, cl\n");
    else
        fprintf(function->out, "    shr rax, cl\n");

    store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
}

static void emit_compare(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    if (type_is_float(operands[0].type)) {
        size_t size = type_size(operands[0].type);
        load_float_operand(function, &operands[0], "xmm0");
        load_float_operand(function, &operands[1], "xmm1");
        fprintf(function->out, size == 4 ? "    ucomiss xmm0, xmm1\n" : "    ucomisd xmm0, xmm1\n");
        switch (instruction->opcode) {
            case LIR_OP_LT:
                fprintf(function->out, "    setb al\n    setnp cl\n    and al, cl\n");
                break;
            case LIR_OP_LE:
                fprintf(function->out, "    setbe al\n    setnp cl\n    and al, cl\n");
                break;
            case LIR_OP_GT:
                fprintf(function->out, "    seta al\n");
                break;
            case LIR_OP_GE:
                fprintf(function->out, "    setae al\n    setnp cl\n    and al, cl\n");
                break;
            case LIR_OP_EQ:
                fprintf(function->out, "    sete al\n    setnp cl\n    and al, cl\n");
                break;
            case LIR_OP_NE:
                fprintf(function->out, "    setne al\n    setp cl\n    or al, cl\n");
                break;
            default:
                assert(!"invalid floating-point comparison opcode");
                return;
        }
        fprintf(function->out, "    movzx eax, al\n");
        store_register(function, X86_RAX, instruction->result, 1);
        return;
    }

    bool signed_ = type_is_signed(operands[0].type);
    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);
    fprintf(function->out, "    cmp rax, rcx\n");

    switch (instruction->opcode) {
        case LIR_OP_LT: fprintf(function->out, signed_ ? "    setl al\n" : "    setb al\n"); break;
        case LIR_OP_LE: fprintf(function->out, signed_ ? "    setle al\n" : "    setbe al\n"); break;
        case LIR_OP_GT: fprintf(function->out, signed_ ? "    setg al\n" : "    seta al\n"); break;
        case LIR_OP_GE: fprintf(function->out, signed_ ? "    setge al\n" : "    setae al\n"); break;
        case LIR_OP_EQ: fprintf(function->out, "    sete al\n"); break;
        case LIR_OP_NE: fprintf(function->out, "    setne al\n"); break;
        default: assert(!"invalid comparison opcode");
    }

    fprintf(function->out, "    movzx eax, al\n");
    store_register(function, X86_RAX, instruction->result, 1);
}

static void emit_load(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *address = &((LirOperand *)instruction->operands.data)[0];
    size_t size = type_size(instruction->result_type);

    if (size == 0)
        return;

    load_operand(function, address, X86_RAX);

    if (size > 8) {
        X86Slot *destination = slot(function, instruction->result);
        fprintf(function->out, "    mov rsi, rax\n");
        fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
        emit_copy_memory(function, X86_RDI, X86_RSI, size);
        return;
    }

    assert(size == 1 || size == 2 || size == 4 || size == 8);

    if (size == 1)
        fprintf(function->out, type_is_signed(instruction->result_type) ? "    movsx rcx, byte ptr [rax]\n" : "    movzx rcx, byte ptr [rax]\n");
    else if (size == 2)
        fprintf(function->out, type_is_signed(instruction->result_type) ? "    movsx rcx, word ptr [rax]\n" : "    movzx rcx, word ptr [rax]\n");
    else if (size == 4)
        fprintf(function->out, type_is_signed(instruction->result_type) ? "    movsxd rcx, dword ptr [rax]\n" : "    mov ecx, dword ptr [rax]\n");
    else
        fprintf(function->out, "    mov rcx, qword ptr [rax]\n");

    store_register(function, X86_RCX, instruction->result, size);
}

static void emit_store(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    size_t size = type_size(operands[1].type);

    if (size == 0)
        return;

    if (type_is_float(operands[1].type)) {
        load_float_operand(function, &operands[1], "xmm0");
        load_operand(function, &operands[0], X86_RAX);
        fprintf(function->out, size == 4 ? "    movss dword ptr [rax], xmm0\n" : "    movsd qword ptr [rax], xmm0\n");
        return;
    }

    load_operand(function, &operands[0], X86_RAX);

    if (size > 8) {
        assert(operands[1].kind == LIR_OPERAND_VALUE);
        X86Slot *source = slot(function, operands[1].value);
        fprintf(function->out, "    mov rdi, rax\n");
        fprintf(function->out, "    lea rsi, [rbp%ld]\n", source->offset);
        emit_copy_memory(function, X86_RDI, X86_RSI, size);
        return;
    }

    assert(size == 1 || size == 2 || size == 4 || size == 8);
    load_operand(function, &operands[1], X86_RCX);
    fprintf(function->out, "    mov %s [rax], %s\n", size_name(size), reg_name(X86_RCX, size));
}

static void emit_addr_add(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    fprintf(function->out, "    add rax, rcx\n");

    store_register(function, X86_RAX, instruction->result, 8);
}

static void emit_addr_diff(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);
    fprintf(function->out, "    sub rax, rcx\n");

    load_operand(function, &operands[2], X86_RCX);
    fprintf(function->out, "    cqo\n");
    fprintf(function->out, "    idiv rcx\n");

    store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
}

static void emit_addr(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *operand = &((LirOperand *)instruction->operands.data)[0];

    if (operand->kind == LIR_OPERAND_SYMBOL) {
        fprintf(function->out, "    lea rax, %.*s[rip]\n", (int)operand->symbol->name.ident.length, operand->symbol->name.ident.data);
    } else if (operand->kind == LIR_OPERAND_VALUE) {
        X86Slot *source = slot(function, operand->value);
        fprintf(function->out, "    lea rax, [rbp%ld]\n", source->offset);
    } else {
        assert(!"address operand must be a symbol or stack value");
    }

    store_register(function, X86_RAX, instruction->result, 8);
}

static void emit_aggregate_return(X86Function *function, const LirOperand *operand) {
    assert(operand->kind == LIR_OPERAND_VALUE);

    X86Slot *source = slot(function, operand->value);
    size_t size = type_size(operand->type);

    fprintf(function->out, "    mov rdi, qword ptr [rbp%ld]\n", function->return_address_offset);
    fprintf(function->out, "    mov rax, rdi\n");
    fprintf(function->out, "    lea rsi, [rbp%ld]\n", source->offset);
    fprintf(function->out, "    mov rcx, %zu\n", size);
    fprintf(function->out, "    rep movsb\n");
}

static bool type_is_integer_numeric(HirType *type) {
    type = base_type(type);
    if (type == NULL || type->kind != HIR_TYPE_BUILTIN)
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
            return true;

        default:
            return false;
    }
}

static void emit_float_power_of_two(X86Function *function, size_t source_size, const char *reg, unsigned exponent, bool negative) {
    if (source_size == sizeof(float)) {
        uint32_t bits = (uint32_t)(exponent + 127u) << 23;
        if (negative)
            bits |= UINT32_C(0x80000000);
        fprintf(function->out, "    mov eax, 0x%08" PRIx32 "\n    movd %s, eax\n", bits, reg);
    } else {
        uint64_t bits = (uint64_t)(exponent + 1023u) << 52;
        if (negative)
            bits |= UINT64_C(0x8000000000000000);
        fprintf(function->out, "    mov rax, 0x%016" PRIx64 "\n    movq %s, rax\n", bits, reg);
    }
}

static void emit_float_zero_xmm1(X86Function *function, size_t source_size) {
    (void)source_size;
    fprintf(function->out, "    pxor xmm1, xmm1\n");
}

static void emit_float_compare(X86Function *function, size_t source_size) {
    fprintf(function->out, source_size == sizeof(float) ? "    ucomiss xmm0, xmm1\n" : "    ucomisd xmm0, xmm1\n");
}

static void emit_float_to_integer_cast(X86Function *function, size_t source_size, HirType *target_type, LirValueId result) {
    unsigned bits = (unsigned)(type_size(target_type) * 8);
    bool signed_ = type_is_signed(target_type);
    uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    uint64_t minimum = signed_ ? (UINT64_C(1) << (bits - 1)) : 0;
    uint64_t maximum = signed_ ? mask >> 1 : mask;
    const char *convert = source_size == sizeof(float) ? "cvttss2si" : "cvttsd2si";
    const char *subtract = source_size == sizeof(float) ? "subss" : "subsd";

    if (signed_) {
        fprintf(function->out, "    ucomi%s xmm0, xmm0\n    jp 3f\n", source_size == sizeof(float) ? "ss" : "sd");
        emit_float_power_of_two(function, source_size, "xmm1", bits - 1, false);
        emit_float_compare(function, source_size);
        fprintf(function->out, "    jae 2f\n");
        emit_float_power_of_two(function, source_size, "xmm1", bits - 1, true);
        emit_float_compare(function, source_size);
        fprintf(function->out, "    jb 1f\n    %s rax, xmm0\n    jmp 4f\n1:\n    mov rax, 0x%016" PRIx64 "\n    jmp 4f\n2:\n    mov rax, 0x%016" PRIx64 "\n    jmp 4f\n3:\n    xor eax, eax\n4:\n", convert, minimum, maximum);
    } else {
        emit_float_zero_xmm1(function, source_size);
        emit_float_compare(function, source_size);
        fprintf(function->out, "    jbe 3f\n");
        emit_float_power_of_two(function, source_size, "xmm1", bits, false);
        emit_float_compare(function, source_size);
        fprintf(function->out, "    jae 2f\n");

        if (bits == 64) {
            emit_float_power_of_two(function, source_size, "xmm1", 63, false);
            emit_float_compare(function, source_size);
            fprintf(function->out, "    jb 5f\n    %s xmm0, xmm1\n    %s rax, xmm0\n    bts rax, 63\n    jmp 4f\n5:\n    %s rax, xmm0\n    jmp 4f\n", subtract, convert, convert);
        } else {
            fprintf(function->out, "    %s rax, xmm0\n    jmp 4f\n", convert);
        }
        fprintf(function->out, "2:\n    mov rax, 0x%016" PRIx64 "\n    jmp 4f\n3:\n    xor eax, eax\n4:\n", maximum);
    }

    store_register(function, X86_RAX, result, type_size(target_type));
}

static void emit_cast(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *operand = &((LirOperand *)instruction->operands.data)[0];
    HirType *source_type = base_type(operand->type);
    HirType *target_type = base_type(instruction->result_type);

    assert(source_type != NULL && target_type != NULL);

    if (type_is_float(source_type)) {
        size_t source_size = type_size(source_type);
        size_t target_size = type_size(target_type);
        load_float_operand(function, operand, "xmm0");

        if (type_is_float(target_type)) {
            if (source_size != target_size)
                fprintf(function->out, source_size == 4 ? "    cvtss2sd xmm0, xmm0\n" : "    cvtsd2ss xmm0, xmm0\n");
            store_float_register(function, "xmm0", instruction->result, target_size);
            return;
        }

        assert(type_is_integer_numeric(target_type));
        emit_float_to_integer_cast(function, source_size, target_type, instruction->result);
        return;
    }

    if (type_is_float(target_type)) {
        assert(type_is_integer_numeric(source_type));
        load_operand(function, operand, X86_RAX);
        size_t target_size = type_size(target_type);

        if (source_type->builtin == BUILTIN_UINT64) {
            fprintf(function->out, "    test rax, rax\n    jns 1f\n    mov rcx, rax\n    shr rax, 1\n    and ecx, 1\n    or rax, rcx\n");
            fprintf(function->out, target_size == 4 ? "    cvtsi2ss xmm0, rax\n    addss xmm0, xmm0\n    jmp 2f\n1:\n    cvtsi2ss xmm0, rax\n2:\n" : "    cvtsi2sd xmm0, rax\n    addsd xmm0, xmm0\n    jmp 2f\n1:\n    cvtsi2sd xmm0, rax\n2:\n");
        } else {
            fprintf(function->out, target_size == 4 ? "    cvtsi2ss xmm0, rax\n" : "    cvtsi2sd xmm0, rax\n");
        }

        store_float_register(function, "xmm0", instruction->result, target_size);
        return;
    }

    load_operand(function, operand, X86_RAX);
    store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
}

static void emit_division(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    if (type_is_float(operands[0].type)) {
        assert(instruction->opcode == LIR_OP_DIV);
        size_t size = type_size(operands[0].type);
        load_float_operand(function, &operands[0], "xmm0");
        load_float_operand(function, &operands[1], "xmm1");
        fprintf(function->out, size == 4 ? "    divss xmm0, xmm1\n" : "    divsd xmm0, xmm1\n");
        store_float_register(function, "xmm0", instruction->result, size);
        return;
    }

    bool signed_ = type_is_signed(operands[0].type);
    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    if (signed_)
        fprintf(function->out, "    cqo\n");
    else
        fprintf(function->out, "    xor rdx, rdx\n");

    fprintf(function->out, signed_ ? "    idiv rcx\n" : "    div rcx\n");

    if (instruction->opcode == LIR_OP_DIV)
        store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
    else
        store_register(function, X86_RDX, instruction->result, type_size(instruction->result_type));
}

static X86Reg integer_argument_register(size_t index) {
    static const X86Reg registers[] = {
        X86_RDI,
        X86_RSI,
        X86_RDX,
        X86_RCX,
        X86_R8,
        X86_R9,
    };

    assert(index < sizeof(registers) / sizeof(*registers));
    return registers[index];
}

static void emit_operand_address(X86Function *function, const LirOperand *operand, X86Reg reg) {
    switch (operand->kind) {
        case LIR_OPERAND_VALUE: {
            X86Slot *source = slot(function, operand->value);
            fprintf(function->out, "    lea %s, [rbp%ld]\n", reg_name(reg, 8), source->offset);
            return;
        }

        case LIR_OPERAND_SYMBOL:
            fprintf(function->out, "    lea %s, %.*s[rip]\n", reg_name(reg, 8), (int)operand->symbol->name.ident.length, operand->symbol->name.ident.data);
            return;

        default:
            assert(!"operand has no addressable aggregate storage");
    }
}

static const char *float_argument_register(size_t index) {
    static const char *registers[] = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    assert(index < sizeof(registers) / sizeof(*registers));
    return registers[index];
}

static void emit_abi_return_registers(X86Function *function, const LirOperand *operand) {
    X86AbiClassification classification = abi_classify_type(operand->type);
    assert(!classification.memory && classification.count <= 2);
    emit_operand_address(function, operand, X86_R11);

    size_t gp_return = 0;
    size_t fp_return = 0;
    for (size_t i = 0; i < classification.count; i++) {
        size_t chunk_size = abi_chunk_size(operand->type, i);
        size_t offset = i * 8;
        if (classification.classes[i] == X86_ABI_INTEGER) {
            X86Reg reg = gp_return++ == 0 ? X86_RAX : X86_RDX;
            emit_abi_load_gp_chunk(function, reg, "r11", offset, chunk_size);
        } else if (classification.classes[i] == X86_ABI_SSE) {
            char reg[8];
            snprintf(reg, sizeof(reg), "xmm%zu", fp_return++);
            emit_abi_load_sse_chunk(function, reg, "r11", offset, chunk_size);
        }
    }
}

static void emit_abi_store_call_result(X86Function *function, HirType *type, LirValueId result) {
    X86AbiClassification classification = abi_classify_type(type);
    assert(!classification.memory && classification.count <= 2);
    X86Slot *destination = slot(function, result);
    fprintf(function->out, "    lea r11, [rbp%ld]\n", destination->offset);

    size_t gp_return = 0;
    size_t fp_return = 0;
    for (size_t i = 0; i < classification.count; i++) {
        size_t chunk_size = abi_chunk_size(type, i);
        size_t offset = i * 8;
        if (classification.classes[i] == X86_ABI_INTEGER) {
            X86Reg reg = gp_return++ == 0 ? X86_RAX : X86_RDX;
            emit_abi_store_gp_chunk(function, "r11", offset, reg, chunk_size);
        } else if (classification.classes[i] == X86_ABI_SSE) {
            char reg[8];
            snprintf(reg, sizeof(reg), "xmm%zu", fp_return++);
            emit_abi_store_sse_chunk(function, "r11", offset, reg, chunk_size);
        }
    }
}

static void emit_abi_stack_copy(X86Function *function, const LirOperand *operand, size_t stack_offset, size_t size) {
    emit_operand_address(function, operand, X86_RSI);
    fprintf(function->out, "    lea rdi, [rsp+%zu]\n", stack_offset);
    fprintf(function->out, "    mov rcx, %zu\n", size);
    fprintf(function->out, "    rep movsb\n");
}

static void emit_call(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    Symbol *symbol = operands[0].symbol;
    size_t arg_count = instruction->operands.len - 1;
    HirType *callee_type = base_type(symbol->type);
    assert(callee_type != NULL && callee_type->kind == HIR_TYPE_FUNCTION);

    HirType *return_type = base_type(callee_type->function.ret);
    bool indirect_return = return_type != NULL && abi_classify_type(return_type).memory;
    HirType **arg_types = calloc(arg_count ? arg_count : 1, sizeof(*arg_types));
    X86AbiArgument *arguments = calloc(arg_count ? arg_count : 1, sizeof(*arguments));
    assert(arg_types != NULL && arguments != NULL);
    for (size_t i = 0; i < arg_count; i++)
        arg_types[i] = operands[i + 1].type;

    size_t stack_size = abi_assign_arguments(arg_types, arg_count, indirect_return, arguments);
    if (stack_size != 0)
        fprintf(function->out, "    sub rsp, %zu\n", stack_size);

    for (size_t i = 0; i < arg_count; i++) {
        LirOperand *arg = &operands[i + 1];
        size_t size = type_size(arg->type);
        if (size == 0 || !arguments[i].stack)
            continue;

        if (abi_is_aggregate(arg->type)) {
            emit_abi_stack_copy(function, arg, arguments[i].stack_offset, size);
        } else if (type_is_float(arg->type)) {
            load_float_operand(function, arg, "xmm15");
            fprintf(function->out, size == 4 ? "    movss dword ptr [rsp+%zu], xmm15\n" : "    movsd qword ptr [rsp+%zu], xmm15\n", arguments[i].stack_offset);
        } else {
            load_operand(function, arg, X86_RAX);
            fprintf(function->out, "    mov %s [rsp+%zu], %s\n", size_name(size), arguments[i].stack_offset, reg_name(X86_RAX, size));
        }
    }

    if (indirect_return) {
        assert(instruction->result != LIR_INVALID_VALUE);
        X86Slot *destination = slot(function, instruction->result);
        fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
    }

    for (size_t i = 0; i < arg_count; i++) {
        LirOperand *arg = &operands[i + 1];
        size_t size = type_size(arg->type);
        if (size == 0 || arguments[i].stack)
            continue;

        if (abi_is_aggregate(arg->type)) {
            emit_operand_address(function, arg, X86_R11);
            for (size_t j = 0; j < arguments[i].classification.count; j++) {
                size_t chunk_size = abi_chunk_size(arg->type, j);
                size_t offset = j * 8;
                X86AbiClass class_ = arguments[i].classification.classes[j];
                if (class_ == X86_ABI_INTEGER) {
                    assert(arguments[i].gp_register[j] != SIZE_MAX);
                    emit_abi_load_gp_chunk(function, integer_argument_register(arguments[i].gp_register[j]), "r11", offset, chunk_size);
                } else if (class_ == X86_ABI_SSE) {
                    assert(arguments[i].fp_register[j] != SIZE_MAX);
                    emit_abi_load_sse_chunk(function, float_argument_register(arguments[i].fp_register[j]), "r11", offset, chunk_size);
                }
            }
        } else if (type_is_float(arg->type)) {
            assert(arguments[i].fp_register[0] != SIZE_MAX);
            load_float_operand(function, arg, float_argument_register(arguments[i].fp_register[0]));
        } else {
            assert(arguments[i].gp_register[0] != SIZE_MAX);
            load_operand(function, arg, integer_argument_register(arguments[i].gp_register[0]));
        }
    }

    fprintf(function->out, "    call %.*s\n", (int)symbol->name.ident.length, symbol->name.ident.data);
    if (stack_size != 0)
        fprintf(function->out, "    add rsp, %zu\n", stack_size);

    if (!indirect_return && instruction->result != LIR_INVALID_VALUE && type_size(instruction->result_type) != 0) {
        if (abi_is_aggregate(instruction->result_type))
            emit_abi_store_call_result(function, instruction->result_type, instruction->result);
        else if (type_is_float(instruction->result_type))
            store_float_register(function, "xmm0", instruction->result, type_size(instruction->result_type));
        else
            store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
    }

    free(arguments);
    free(arg_types);
}

static void emit_instruction(X86Function *function, const LirInstruction *instruction) {
    switch (instruction->opcode) {
        case LIR_OP_NEG:
        case LIR_OP_NOT:
            emit_unary(function, instruction);
            break;

        case LIR_OP_ADD:
            emit_binary(function, instruction, "add");
            break;

        case LIR_OP_SUB:
            emit_binary(function, instruction, "sub");
            break;

        case LIR_OP_MUL:
            emit_binary(function, instruction, "imul");
            break;

        case LIR_OP_AND:
            emit_binary(function, instruction, "and");
            break;

        case LIR_OP_OR:
            emit_binary(function, instruction, "or");
            break;

        case LIR_OP_XOR:
            emit_binary(function, instruction, "xor");
            break;

        case LIR_OP_SHL:
        case LIR_OP_SHR:
            emit_shift(function, instruction);
            break;

        case LIR_OP_DIV:
        case LIR_OP_MOD:
            emit_division(function, instruction);
            break;

        case LIR_OP_LT:
        case LIR_OP_LE:
        case LIR_OP_GT:
        case LIR_OP_GE:
        case LIR_OP_EQ:
        case LIR_OP_NE:
            emit_compare(function, instruction);
            break;

        case LIR_OP_CAST:
            emit_cast(function, instruction);
            break;

        case LIR_OP_LOAD:
            emit_load(function, instruction);
            break;

        case LIR_OP_STORE:
            emit_store(function, instruction);
            break;

        case LIR_OP_ADDR:
            emit_addr(function, instruction);
            break;

        case LIR_OP_ADDR_ADD:
            emit_addr_add(function, instruction);
            break;

        case LIR_OP_ADDR_DIFF:
            emit_addr_diff(function, instruction);
            break;

        case LIR_OP_CALL:
            emit_call(function, instruction);
            break;

        case LIR_OP_ZERO:
            emit_zero(function, instruction);
            break;

        case LIR_OP_EXTRACT:
            emit_extract(function, instruction, false);
            break;

        case LIR_OP_EXTRACT_DYNAMIC:
            emit_extract(function, instruction, true);
            break;

        case LIR_OP_INSERT:
            emit_insert(function, instruction, false);
            break;

        case LIR_OP_INSERT_DYNAMIC:
            emit_insert(function, instruction, true);
            break;
        
        default:
            assert(!"Unimplemented instruction");
            break;
    }
}

static void emit_parameters(X86Function *function) {
    LirFunction *lir = (LirFunction *)function->function;
    size_t count = lir->params.len;
    HirType **types = calloc(count ? count : 1, sizeof(*types));
    X86AbiArgument *arguments = calloc(count ? count : 1, sizeof(*arguments));
    assert(types != NULL && arguments != NULL);
    for (size_t i = 0; i < count; i++)
        types[i] = ((LirFunctionParam *)lir->params.data)[i].type;

    size_t stack_size = abi_assign_arguments(types, count, function_returns_indirect(function), arguments);
    (void)stack_size;

    for (size_t i = 0; i < count; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)lir->params.data)[i];
        X86Slot *destination = slot(function, param->value);
        size_t size = type_size(param->type);
        if (size == 0 || arguments[i].stack)
            continue;

        if (abi_is_aggregate(param->type)) {
            fprintf(function->out, "    lea r11, [rbp%ld]\n", destination->offset);
            for (size_t j = 0; j < arguments[i].classification.count; j++) {
                size_t chunk_size = abi_chunk_size(param->type, j);
                size_t offset = j * 8;
                X86AbiClass class_ = arguments[i].classification.classes[j];
                if (class_ == X86_ABI_INTEGER) {
                    assert(arguments[i].gp_register[j] != SIZE_MAX);
                    emit_abi_store_gp_chunk(function, "r11", offset, integer_argument_register(arguments[i].gp_register[j]), chunk_size);
                } else if (class_ == X86_ABI_SSE) {
                    assert(arguments[i].fp_register[j] != SIZE_MAX);
                    emit_abi_store_sse_chunk(function, "r11", offset, float_argument_register(arguments[i].fp_register[j]), chunk_size);
                }
            }
        } else if (type_is_float(param->type)) {
            store_float_register(function, float_argument_register(arguments[i].fp_register[0]), param->value, size);
        } else {
            assert(arguments[i].gp_register[0] != SIZE_MAX);
            X86Reg reg = integer_argument_register(arguments[i].gp_register[0]);
            fprintf(function->out, "    mov %s [rbp%ld], %s\n", size_name(size), destination->offset, reg_name(reg, size));
        }
    }

    for (size_t i = 0; i < count; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)lir->params.data)[i];
        X86Slot *destination = slot(function, param->value);
        size_t size = type_size(param->type);
        if (size == 0 || !arguments[i].stack)
            continue;

        size_t stack_offset = 16 + arguments[i].stack_offset;
        if (abi_is_aggregate(param->type)) {
            fprintf(function->out, "    lea rsi, [rbp+%zu]\n", stack_offset);
            fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
            fprintf(function->out, "    mov rcx, %zu\n", size);
            fprintf(function->out, "    rep movsb\n");
        } else if (type_is_float(param->type)) {
            fprintf(function->out, size == 4 ? "    movss xmm15, dword ptr [rbp+%zu]\n" : "    movsd xmm15, qword ptr [rbp+%zu]\n", stack_offset);
            store_float_register(function, "xmm15", param->value, size);
        } else {
            fprintf(function->out, "    lea r11, [rbp+%zu]\n", stack_offset);
            emit_abi_load_gp_chunk(function, X86_RAX, "r11", 0, size);
            fprintf(function->out, "    mov %s [rbp%ld], %s\n", size_name(size), destination->offset, reg_name(X86_RAX, size));
        }
    }

    free(arguments);
    free(types);
}

static void emit_prologue(X86Function *function) {
    fprintf(function->out, "    push rbp\n");
    fprintf(function->out, "    mov rbp, rsp\n");

    if (function->frame_size != 0)
        fprintf(function->out, "    sub rsp, %zu\n", function->frame_size);

    if (function_returns_indirect(function))
        fprintf(function->out, "    mov qword ptr [rbp%ld], rdi\n", function->return_address_offset);

    emit_parameters(function);
}

static void emit_epilogue(X86Function *function) {
    fprintf(function->out, "    leave\n");
    fprintf(function->out, "    ret\n");
}


static void emit_block_args(X86Function *function, LirBlockId block_id, Array(LirOperand) args) {
    LirBlock *block = ((LirBlock **)function->function->blocks.data)[block_id];

    assert(block->params.len == args.len);

    for (size_t i = 0; i < args.len; i++) {
        LirBlockParam *param = &((LirBlockParam *)block->params.data)[i];
        LirOperand *operand = &((LirOperand *)args.data)[i];
        size_t size = type_size(param->type);

        if (size == 0)
            continue;

        if (size > 8 || abi_is_aggregate(param->type)) {
            X86Slot *destination = slot(function, param->value);
            fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
            emit_operand_address(function, operand, X86_RSI);
            emit_copy_memory(function, X86_RDI, X86_RSI, size);
        } else if (type_is_float(param->type)) {
            load_float_operand(function, operand, "xmm0");
            store_float_register(function, "xmm0", param->value, size);
        } else {
            load_operand(function, operand, X86_RAX);
            store_register(function, X86_RAX, param->value, size);
        }
    }
}

static void emit_terminator(X86Function *function, const LirTerminator *terminator) {
    switch (terminator->kind) {
        case LIR_TERM_JUMP:
            emit_block_args(function, terminator->jump.target, terminator->jump.args);
            emit_block_jump(function, terminator->jump.target);
            break;

        case LIR_TERM_BRANCH:
            load_operand(function, &terminator->branch.condition, X86_RAX);
            fprintf(function->out, "    cmp rax, 0\n");
            fprintf(function->out, "    je .L%zu_else_%u\n", function->id, terminator->branch.else_block);

            emit_block_args(function, terminator->branch.then_block, terminator->branch.then_args);
            emit_block_jump(function, terminator->branch.then_block);

            fprintf(function->out, ".L%zu_else_%u:\n", function->id, terminator->branch.else_block);

            emit_block_args(function, terminator->branch.else_block, terminator->branch.else_args);
            emit_block_jump(function, terminator->branch.else_block);
            break;

        case LIR_TERM_RETURN:
            if (terminator->_return.has_value) {
                if (function_returns_indirect(function)) {
                    emit_aggregate_return(function, &terminator->_return.value);
                } else if (abi_is_aggregate(terminator->_return.value.type) && type_size(terminator->_return.value.type) != 0) {
                    emit_abi_return_registers(function, &terminator->_return.value);
                } else if (type_is_float(terminator->_return.value.type)) {
                    load_float_operand(function, &terminator->_return.value, "xmm0");
                } else {
                    load_operand(function, &terminator->_return.value, X86_RAX);
                }
            }

            emit_epilogue(function);
            break;

        default:
            assert(!"terminator not implemented yet");
    }
}

static void emit_function(X86Function *function) {
    const LirFunction *lir = function->function;
    String name = lir->symbol->name.ident;

    fprintf(function->out, "\n.section .text\n");

    if (lir->is_export)
        fprintf(function->out, ".globl %.*s\n", (int)name.length, name.data);

    fprintf(function->out, ".type %.*s, @function\n", (int)name.length, name.data);
    fprintf(function->out, "%.*s:\n", (int)name.length, name.data);

    emit_prologue(function);

    for (size_t i = 0; i < lir->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)lir->blocks.data)[i];

        emit_block_label(function, block->id);

        for (size_t j = 0; j < block->instructions.len; j++)
            emit_instruction(function, &((LirInstruction *)block->instructions.data)[j]);

        emit_terminator(function, &block->terminator);
    }

    fprintf(function->out, ".size %.*s, .-%.*s\n", (int)name.length, name.data, (int)name.length, name.data);
}

static bool supports(const TargetInfo *target) {
    return target->arch == TARGET_ARCH_X86_64 &&
           target->os == TARGET_OS_LINUX &&
           target->abi == TARGET_ABI_SYSV &&
           target->object == TARGET_OBJECT_ELF64 &&
           target->endian == TARGET_ENDIAN_LITTLE;
}

static bool global_is_zero(const LirGlobal *global) {
    return global->data.len == 0;
}

static void emit_global(FILE *out, const LirGlobal *global) {
    String name = global->symbol->name.ident;
    const char *section = global->is_mutable ? ".data" : ".rodata";

    if (global_is_zero(global))
        section = ".bss";

    fprintf(out, "\n.section %s\n", section);
    fprintf(out, ".balign %zu\n", type_align(global->type));

    if (global->is_export)
        fprintf(out, ".globl %.*s\n", (int)name.length, name.data);

    fprintf(out, ".type %.*s, @object\n", (int)name.length, name.data);
    fprintf(out, "%.*s:\n", (int)name.length, name.data);

    size_t offset = 0;

    for (size_t i = 0; i < global->data.len; i++) {
        const LirData *data = &((LirData *)global->data.data)[i];

        if (data->offset > offset)
            fprintf(out, "    .zero %zu\n", data->offset - offset);

        switch (data->kind) {
            case LIR_DATA_INTEGER:
                switch (type_size(data->type)) {
                    case 1: fprintf(out, "    .byte %" PRIu64 "\n", data->integer); break;
                    case 2: fprintf(out, "    .short %" PRIu64 "\n", data->integer); break;
                    case 4: fprintf(out, "    .long %" PRIu64 "\n", data->integer); break;
                    case 8: fprintf(out, "    .quad %" PRIu64 "\n", data->integer); break;
                    default: assert(!"invalid global integer size");
                }
                offset = data->offset + type_size(data->type);
                break;

            case LIR_DATA_BOOL:
                fprintf(out, "    .byte %d\n", data->boolean ? 1 : 0);
                offset = data->offset + 1;
                break;

            case LIR_DATA_FLOAT: {
                size_t size = type_size(data->type);

                if (size == sizeof(float)) {
                    float value = (float)data->floating;
                    uint32_t bits;
                    memcpy(&bits, &value, sizeof(bits));
                    fprintf(out, "    .long 0x%08" PRIx32 "\n", bits);
                } else if (size == sizeof(double)) {
                    uint64_t bits;
                    memcpy(&bits, &data->floating, sizeof(bits));
                    fprintf(out, "    .quad 0x%016" PRIx64 "\n", bits);
                } else {
                    assert(!"invalid floating-point global size");
                }

                offset = data->offset + size;
                break;
            }

            case LIR_DATA_BYTES:
                for (size_t j = 0; j < data->bytes.length; j++)
                    fprintf(out, "    .byte %u\n", (unsigned char)data->bytes.data[j]);

                offset = data->offset + data->bytes.length;
                break;

            case LIR_DATA_ADDRESS:
                fprintf(out, "    .quad %.*s\n", (int)data->symbol->name.ident.length, data->symbol->name.ident.data);
                offset = data->offset + type_size(data->type);
                break;
        }
    }

    if (offset < type_size(global->type))
        fprintf(out, "    .zero %zu\n", type_size(global->type) - offset);

    fprintf(out, ".size %.*s, %zu\n", (int)name.length, name.data, type_size(global->type));
}

static void emit_string(FILE *out, const LirString *string) {
    String name = string->symbol->name.ident;

    fprintf(out, "\n.section .rodata\n.balign 1\n%.*s:\n", (int)name.length, name.data);

    if (string->bytes.length == 0) {
        fprintf(out, "    .zero 1\n");
    } else {
        for (size_t i = 0; i < string->bytes.length; i++)
            fprintf(out, "    .byte %u\n", (unsigned char)string->bytes.data[i]);
    }

    fprintf(out, ".size %.*s, %zu\n", (int)name.length, name.data, string->bytes.length);
}

static bool emit(Backend *backend, const TargetInfo *target, const LirModule *module, FILE *output) {
    (void)backend;

    if (!supports(target))
        return false;

    fprintf(output, ".intel_syntax noprefix\n");

    for (size_t i = 0; i < module->globals.len; i++)
        emit_global(output, &((LirGlobal *)module->globals.data)[i]);

    for (size_t i = 0; i < module->strings.len; i++)
        emit_string(output, &((LirString *)module->strings.data)[i]);

    for (size_t i = 0; i < module->functions.len; i++) {
        const LirFunction *function = ((LirFunction **)module->functions.data)[i];

        if (function->is_extern)
            continue;

        X86Function x86 = {
            .out = output,
            .target = target,
            .function = function,
            .id = i,
        };

        allocate_slots(&x86);
        emit_function(&x86);
        free(x86.slots);
    }

    return true;
}

static const BackendApi backend_api = {
    .abi_version = CODA_BACKEND_ABI_VERSION,
    .lir_version = CODA_LIR_ABI_VERSION,
    .name = "x86_64",
    .supports = supports,
    .emit = emit,
    .destroy = NULL,
};

const BackendApi *x86_64_backend(void) {
    return &backend_api;
}