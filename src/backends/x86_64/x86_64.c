#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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

static bool function_returns_indirect(X86Function *function) {
    return function->function->return_type != NULL &&
           function->function->return_type->size > 8;
}

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
            return false;
    }

    assert(!"unhandled builtin type");
    return false;
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

static void emit_unary(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *operand = &((LirOperand *)instruction->operands.data)[0];
    size_t size = type_size(instruction->result_type);

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
    bool signed_ = type_is_signed(operands[0].type);

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    fprintf(function->out, "    cmp rax, rcx\n");

    switch (instruction->opcode) {
        case LIR_OP_LT:
            fprintf(function->out, signed_ ? "    setl al\n" : "    setb al\n");
            break;

        case LIR_OP_LE:
            fprintf(function->out, signed_ ? "    setle al\n" : "    setbe al\n");
            break;

        case LIR_OP_GT:
            fprintf(function->out, signed_ ? "    setg al\n" : "    seta al\n");
            break;

        case LIR_OP_GE:
            fprintf(function->out, signed_ ? "    setge al\n" : "    setae al\n");
            break;

        case LIR_OP_EQ:
            fprintf(function->out, "    sete al\n");
            break;

        case LIR_OP_NE:
            fprintf(function->out, "    setne al\n");
            break;

        default:
            assert(!"invalid comparison opcode");
    }

    fprintf(function->out, "    movzx eax, al\n");
    store_register(function, X86_RAX, instruction->result, 1);
}

static void emit_load(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *address = &((LirOperand *)instruction->operands.data)[0];
    size_t size = type_size(instruction->result_type);

    assert(size == 1 || size == 2 || size == 4 || size == 8);

    load_operand(function, address, X86_RAX);

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

    assert(size == 1 || size == 2 || size == 4 || size == 8);

    load_operand(function, &operands[0], X86_RAX);
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

static void emit_addr(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *operand = &((LirOperand *)instruction->operands.data)[0];

    assert(operand->kind == LIR_OPERAND_SYMBOL);

    fprintf(function->out, "    lea rax, %.*s[rip]\n", (int)operand->symbol->name.ident.length, operand->symbol->name.ident.data);

    store_register(function, X86_RAX, instruction->result, 8);
}

static void emit_aggregate_return(X86Function *function, const LirOperand *operand) {
    assert(operand->kind == LIR_OPERAND_VALUE);

    X86Slot *source = slot(function, operand->value);
    size_t size = type_size(operand->type);

    assert(size > 8);

    fprintf(function->out, "    mov rdi, qword ptr [rbp%ld]\n", function->return_address_offset);
    fprintf(function->out, "    mov rax, rdi\n");
    fprintf(function->out, "    lea rsi, [rbp%ld]\n", source->offset);
    fprintf(function->out, "    mov rcx, %zu\n", size);
    fprintf(function->out, "    rep movsb\n");
}

static void emit_cast(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *operand = &((LirOperand *)instruction->operands.data)[0];

    load_operand(function, operand, X86_RAX);
    store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
}

static void emit_division(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
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

static void emit_call(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    Symbol *symbol = operands[0].symbol;
    size_t arg_count = instruction->operands.len - 1;

    HirType *callee_type = base_type(symbol->type);
    assert(callee_type != NULL && callee_type->kind == HIR_TYPE_FUNCTION);

    HirType *return_type = base_type(callee_type->function.ret);
    bool indirect_return = return_type != NULL && return_type->size > 8;
    size_t arg_shift = indirect_return ? 1 : 0;

    assert(arg_count + arg_shift <= 6);

    if (indirect_return) {
        assert(instruction->result != LIR_INVALID_VALUE);
        X86Slot *destination = slot(function, instruction->result);
        fprintf(function->out, "    lea rdi, [rbp%ld]\n", destination->offset);
    }

    for (size_t i = 0; i < arg_count; i++) {
        LirOperand *arg = &operands[i + 1];
        X86Reg reg = integer_argument_register(i + arg_shift);

        if (type_size(arg->type) > 8)
            emit_operand_address(function, arg, reg);
        else
            load_operand(function, arg, reg);
    }

    fprintf(function->out, "    call %.*s\n", (int)symbol->name.ident.length, symbol->name.ident.data);

    if (!indirect_return && instruction->result != LIR_INVALID_VALUE && type_size(instruction->result_type) != 0)
        store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
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
    size_t arg_shift = function_returns_indirect(function) ? 1 : 0;

    for (size_t i = 0; i < lir->params.len; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)lir->params.data)[i];
        X86Slot *s = slot(function, param->value);
        size_t size = type_size(param->type);
        X86Reg reg = integer_argument_register(i + arg_shift);

        if (size == 0)
            continue;

        if (size > 8)
            fprintf(function->out, "    mov qword ptr [rbp%ld], %s\n", s->offset, reg_name(reg, 8));
        else
            fprintf(function->out, "    mov %s [rbp%ld], %s\n", size_name(size), s->offset, reg_name(reg, size));
    }

    for (size_t i = 0; i < lir->params.len; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)lir->params.data)[i];
        X86Slot *s = slot(function, param->value);
        size_t size = type_size(param->type);

        if (size <= 8)
            continue;

        fprintf(function->out, "    mov rsi, qword ptr [rbp%ld]\n", s->offset);
        fprintf(function->out, "    lea rdi, [rbp%ld]\n", s->offset);
        fprintf(function->out, "    mov rcx, %zu\n", size);
        fprintf(function->out, "    rep movsb\n");
    }
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

        load_operand(function, operand, X86_RAX);
        store_register(function, X86_RAX, param->value, type_size(param->type));
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
                if (function_returns_indirect(function))
                    emit_aggregate_return(function, &terminator->_return.value);
                else
                    load_operand(function, &terminator->_return.value, X86_RAX);
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

            case LIR_DATA_FLOAT:
                assert(!"global floating-point emission not implemented yet");
                break;

            case LIR_DATA_BYTES:
                for (size_t j = 0; j < data->bytes.length; j++)
                    fprintf(out, "    .byte %u\n", data->bytes.data[j]);

                offset = data->offset + data->bytes.length;
                break;
        }
    }

    if (offset < type_size(global->type))
        fprintf(out, "    .zero %zu\n", type_size(global->type) - offset);

    fprintf(out, ".size %.*s, %zu\n", (int)name.length, name.data, type_size(global->type));
}

static bool emit(Backend *backend, const TargetInfo *target, const LirModule *module, FILE *output) {
    (void)backend;

    if (!supports(target))
        return false;

    fprintf(output, ".intel_syntax noprefix\n");

    for (size_t i = 0; i < module->globals.len; i++)
        emit_global(output, &((LirGlobal *)module->globals.data)[i]);

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