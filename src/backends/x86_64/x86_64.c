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
    X86Slot *slots;
    size_t slot_count;
    size_t frame_size;
} X86Function;

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

static char suffix(size_t size) {
    switch (size) {
        case 1: return 'b';
        case 2: return 'w';
        case 4: return 'l';
        case 8: return 'q';
    }

    assert(!"invalid operand size");
    return '\0';
}

static X86Slot *slot(X86Function *function, LirValueId value) {
    assert(value < function->slot_count);
    return &function->slots[value];
}

static void allocate_slots(X86Function *function) {
    function->slot_count = function->function->next_value;

    if (function->slot_count == 0) {
        function->slots = NULL;
        function->frame_size = 0;
        return;
    }

    function->slots = calloc(function->slot_count, sizeof(*function->slots));
    assert(function->slots != NULL);

    size_t offset = 0;

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

            function->slots[param->value] = (X86Slot){
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

            function->slots[instruction->result] = (X86Slot){
                .offset = -(int64_t)offset,
                .size = size,
            };
        }
    }

    function->frame_size = align_up(offset, 16);
}

static void load_operand(X86Function *function, const LirOperand *operand, X86Reg reg) {
    size_t size = type_size(operand->type);

    assert(size <= 8);

    switch (operand->kind) {
        case LIR_OPERAND_VALUE: {
            X86Slot *s = slot(function, operand->value);

            if (size == 1) {
                if (type_is_signed(operand->type))
                    fprintf(function->out, "    movsbq %ld(%%rbp), %%%s\n", s->offset, reg_name(reg, 8));
                else
                    fprintf(function->out, "    movzbq %ld(%%rbp), %%%s\n", s->offset, reg_name(reg, 8));
            } else if (size == 2) {
                if (type_is_signed(operand->type))
                    fprintf(function->out, "    movswq %ld(%%rbp), %%%s\n", s->offset, reg_name(reg, 8));
                else
                    fprintf(function->out, "    movzwq %ld(%%rbp), %%%s\n", s->offset, reg_name(reg, 8));
            } else if (size == 4) {
                if (type_is_signed(operand->type))
                    fprintf(function->out, "    movslq %ld(%%rbp), %%%s\n", s->offset, reg_name(reg, 8));
                else
                    fprintf(function->out, "    movl %ld(%%rbp), %%%s\n", s->offset, reg_name(reg, 8));
            } else {
                fprintf(function->out, "    movq %ld(%%rbp), %%%s\n", s->offset, reg_name(reg, 8));
            }

            break;
        }

        case LIR_OPERAND_INT:
            if (operand->int_value >= INT32_MIN && operand->int_value <= INT32_MAX)
                fprintf(function->out, "    movq $%" PRId64 ", %%%s\n", operand->int_value, reg_name(reg, 8));
            else
                fprintf(function->out, "    movabsq $%" PRId64 ", %%%s\n", operand->int_value, reg_name(reg, 8));
            break;

        case LIR_OPERAND_UINT:
            if (operand->uint_value <= UINT32_MAX)
                fprintf(function->out, "    movq $%" PRIu64 ", %%%s\n", operand->uint_value, reg_name(reg, 8));
            else
                fprintf(function->out, "    movabsq $%" PRIu64 ", %%%s\n", operand->uint_value, reg_name(reg, 8));
            break;

        case LIR_OPERAND_BOOL:
            fprintf(function->out, "    movq $%d, %%%s\n", operand->bool_value ? 1 : 0, reg_name(reg, 8));
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

    fprintf(function->out, "    mov%c %%%s, %ld(%%rbp)\n", suffix(size), reg_name(reg, size), s->offset);
}

static void emit_unary(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *operand = &((LirOperand *)instruction->operands.data)[0];
    size_t size = type_size(instruction->result_type);

    load_operand(function, operand, X86_RAX);

    if (instruction->opcode == LIR_OP_NEG) {
        fprintf(function->out, "    negq %%rax\n");
    } else {
        HirType *type = base_type(instruction->result_type);

        if (type->kind == HIR_TYPE_BUILTIN && type->builtin == BUILTIN_BOOL) {
            fprintf(function->out, "    cmpq $0, %%rax\n");
            fprintf(function->out, "    sete %%al\n");
            fprintf(function->out, "    movzbl %%al, %%eax\n");
        } else {
            fprintf(function->out, "    notq %%rax\n");
        }
    }

    store_register(function, X86_RAX, instruction->result, size);
}

static void emit_binary(X86Function *function, const LirInstruction *instruction, const char *opcode) {
    LirOperand *operands = instruction->operands.data;

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    fprintf(function->out, "    %sq %%rcx, %%rax\n", opcode);

    store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
}

static void emit_shift(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    if (instruction->opcode == LIR_OP_SHL) {
        fprintf(function->out, "    shlq %%cl, %%rax\n");
    } else if (type_is_signed(operands[0].type)) {
        fprintf(function->out, "    sarq %%cl, %%rax\n");
    } else {
        fprintf(function->out, "    shrq %%cl, %%rax\n");
    }

    store_register(function, X86_RAX, instruction->result, type_size(instruction->result_type));
}

static void emit_compare(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    bool signed_ = type_is_signed(operands[0].type);

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    fprintf(function->out, "    cmpq %%rcx, %%rax\n");

    switch (instruction->opcode) {
        case LIR_OP_LT:
            fprintf(function->out, signed_ ? "    setl %%al\n" : "    setb %%al\n");
            break;

        case LIR_OP_LE:
            fprintf(function->out, signed_ ? "    setle %%al\n" : "    setbe %%al\n");
            break;

        case LIR_OP_GT:
            fprintf(function->out, signed_ ? "    setg %%al\n" : "    seta %%al\n");
            break;

        case LIR_OP_GE:
            fprintf(function->out, signed_ ? "    setge %%al\n" : "    setae %%al\n");
            break;

        case LIR_OP_EQ:
            fprintf(function->out, "    sete %%al\n");
            break;

        case LIR_OP_NE:
            fprintf(function->out, "    setne %%al\n");
            break;

        default:
            assert(!"invalid comparison opcode");
    }

    fprintf(function->out, "    movzbl %%al, %%eax\n");
    store_register(function, X86_RAX, instruction->result, 1);
}

static void emit_load(X86Function *function, const LirInstruction *instruction) {
    const LirOperand *address = &((LirOperand *)instruction->operands.data)[0];
    size_t size = type_size(instruction->result_type);

    assert(size <= 8);

    load_operand(function, address, X86_RAX);
    fprintf(function->out, "    movq (%%rax), %%rcx\n");

    if (size == 1) {
        if (type_is_signed(instruction->result_type))
            fprintf(function->out, "    movsbq %%cl, %%rcx\n");
        else
            fprintf(function->out, "    movzbq %%cl, %%rcx\n");
    } else if (size == 2) {
        if (type_is_signed(instruction->result_type))
            fprintf(function->out, "    movswq %%cx, %%rcx\n");
        else
            fprintf(function->out, "    movzwq %%cx, %%rcx\n");
    } else if (size == 4) {
        if (type_is_signed(instruction->result_type))
            fprintf(function->out, "    movslq %%ecx, %%rcx\n");
        else
            fprintf(function->out, "    movl %%ecx, %%ecx\n");
    }

    store_register(function, X86_RCX, instruction->result, size);
}

static void emit_store(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    size_t size = type_size(operands[1].type);

    assert(size <= 8);

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    fprintf(function->out, "    mov%c %%%s, (%%rax)\n", suffix(size), reg_name(X86_RCX, size));
}

static void emit_addr_add(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;

    load_operand(function, &operands[0], X86_RAX);
    load_operand(function, &operands[1], X86_RCX);

    fprintf(function->out, "    addq %%rcx, %%rax\n");

    store_register(function, X86_RAX, instruction->result, 8);
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
        fprintf(function->out, "    xorq %%rdx, %%rdx\n");

    fprintf(function->out, signed_ ? "    idivq %%rcx\n" : "    divq %%rcx\n");

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

static void emit_call(X86Function *function, const LirInstruction *instruction) {
    LirOperand *operands = instruction->operands.data;
    Symbol *symbol = operands[0].symbol;
    size_t arg_count = instruction->operands.len - 1;

    assert(arg_count <= 6);

    for (size_t i = 0; i < arg_count; i++)
        load_operand(function, &operands[i + 1], integer_argument_register(i));

    fprintf(function->out, "    call %.*s\n", (int)symbol->name.ident.length, symbol->name.ident.data);

    if (instruction->result != LIR_INVALID_VALUE && type_size(instruction->result_type) != 0)
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
            assert(!"ADDR should not reach x86 backend yet");
            break;

        case LIR_OP_ADDR_ADD:
            emit_addr_add(function, instruction);
            break;

        case LIR_OP_CALL:
            emit_call(function, instruction);
            break;
    }
}

static void emit_parameters(X86Function *function) {
    LirFunction *lir = (LirFunction *)function->function;

    for (size_t i = 0; i < lir->params.len; i++) {
        LirFunctionParam *param = &((LirFunctionParam *)lir->params.data)[i];
        X86Slot *s = slot(function, param->value);
        size_t size = type_size(param->type);

        assert(size == 1 || size == 2 || size == 4 || size == 8);

        X86Reg reg = integer_argument_register(i);

        fprintf(function->out, "    mov%c %%%s, %ld(%%rbp)\n", suffix(size), reg_name(reg, size), s->offset);
    }
}

static void emit_prologue(X86Function *function) {
    fprintf(function->out, "    pushq %%rbp\n");
    fprintf(function->out, "    movq %%rsp, %%rbp\n");

    if (function->frame_size != 0)
        fprintf(function->out, "    subq $%zu, %%rsp\n", function->frame_size);

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
            fprintf(function->out, "    jmp .L%u\n", terminator->jump.target);
            break;

        case LIR_TERM_BRANCH:
            load_operand(function, &terminator->branch.condition, X86_RAX);
            fprintf(function->out, "    cmpq $0, %%rax\n");
            fprintf(function->out, "    je .L%u_else\n", terminator->branch.else_block);

            emit_block_args(function, terminator->branch.then_block, terminator->branch.then_args);
            fprintf(function->out, "    jmp .L%u\n", terminator->branch.then_block);

            fprintf(function->out, ".L%u_else:\n", terminator->branch.else_block);
            emit_block_args(function, terminator->branch.else_block, terminator->branch.else_args);
            fprintf(function->out, "    jmp .L%u\n", terminator->branch.else_block);
            break;

        case LIR_TERM_RETURN:
            if (terminator->_return.has_value)
                load_operand(function, &terminator->_return.value, X86_RAX);

            emit_epilogue(function);
            break;

        default:
            assert(!"terminator not implemented yet");
    }
}

static void emit_function(X86Function *function) {
    const LirFunction *lir = function->function;
    String name = lir->symbol->name.ident;

    if (lir->is_export)
        fprintf(function->out, ".globl %.*s\n", (int)name.length, name.data);

    fprintf(function->out, ".type %.*s, @function\n", (int)name.length, name.data);
    fprintf(function->out, "%.*s:\n", (int)name.length, name.data);

    emit_prologue(function);

    for (size_t i = 0; i < lir->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)lir->blocks.data)[i];

        fprintf(function->out, ".L%.*s_%u:\n", (int)name.length, name.data, block->id);

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

static bool emit(Backend *backend, const TargetInfo *target, const LirModule *module, FILE *output) {
    (void)backend;

    if (!supports(target))
        return false;

    for (size_t i = 0; i < module->functions.len; i++) {
        const LirFunction *function = ((LirFunction **)module->functions.data)[i];

        if (function->is_extern)
            continue;

        X86Function x86 = {
            .out = output,
            .target = target,
            .function = function,
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