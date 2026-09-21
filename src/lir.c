#include <assert.h>
#include <inttypes.h>

#include "lir.h"

static LirBlock *lir_get_block(LirFunction *function, LirBlockId id) {
    assert(id < function->blocks.len);
    return ((LirBlock **)function->blocks.data)[id];
}

static LirValueId lir_new_value(LirFunction *function) {
    assert(function->next_value != LIR_INVALID_VALUE);
    return function->next_value++;
}

static void lir_copy_operands(Array(LirOperand) *dst, const LirOperand *operands, size_t count) {
    for (size_t i = 0; i < count; i++) {
        array_push(dst, (LirOperand *)&operands[i]);
    }
}

static void lir_assert_unterminated(LirFunction *function, LirBlockId block) {
    LirBlock *b = lir_get_block(function, block);
    assert(b->terminator.kind == LIR_TERM_NONE);
}

LirModule *lir_module_create(Arena *arena) {
    LirModule *module = arena_calloc(arena, sizeof(*module));
    module->arena = arena;
    module->functions = array_create(arena, sizeof(LirFunction *));
    return module;
}

LirFunction *lir_function_create(LirModule *module, Symbol *symbol, HirType *return_type, bool is_extern, bool is_export) {
    LirFunction *function = arena_calloc(module->arena, sizeof(*function));

    function->arena = module->arena;
    function->symbol = symbol;
    function->return_type = return_type;
    function->is_extern = is_extern;
    function->is_export = is_export;
    function->entry = LIR_INVALID_BLOCK;
    function->next_value = 0;
    function->blocks = array_create(module->arena, sizeof(LirBlock *));
    function->params = array_create(module->arena, sizeof(LirFunctionParam));

    array_push(&module->functions, &function);

    return function;
}

LirBlockId lir_block_create(LirFunction *function) {
    LirBlock *block = arena_calloc(function->arena, sizeof(*block));
    block->id = (LirBlockId)function->blocks.len;
    block->params = array_create(function->arena, sizeof(LirBlockParam));
    block->instructions = array_create(function->arena, sizeof(LirInstruction));

    array_push(&function->blocks, &block);

    if (function->entry == LIR_INVALID_BLOCK) {
        function->entry = block->id;
    }

    return block->id;
}

LirValueId lir_function_add_param(LirFunction *function, Symbol *symbol, HirType *type) {
    assert(function->entry != LIR_INVALID_BLOCK);

    LirBlock *entry = lir_get_block(function, function->entry);
    LirValueId value = lir_new_value(function);

    LirBlockParam block_param = {
        .value = value,
        .type = type,
    };

    LirFunctionParam param = {
        .symbol = symbol,
        .type = type,
        .value = value,
    };

    array_push(&entry->params, &block_param);
    array_push(&function->params, &param);

    return value;
}

LirValueId lir_block_add_param(LirFunction *function, LirBlockId block_id, HirType *type) {
    LirBlock *block = lir_get_block(function, block_id);
    LirValueId value = lir_new_value(function);

    LirBlockParam param = {
        .value = value,
        .type = type,
    };

    array_push(&block->params, &param);

    return value;
}

LirValueId lir_emit(LirFunction *function, LirBlockId block_id, LirOpcode opcode, HirType *result_type, const LirOperand *operands, size_t operand_count) {
    LirBlock *block = lir_get_block(function, block_id);
    LirInstruction instruction = {
        .opcode = opcode,
        .result = LIR_INVALID_VALUE,
        .result_type = result_type,
    };

    instruction.operands = array_create(function->arena, sizeof(LirOperand));

    if (result_type) {
        instruction.result = lir_new_value(function);
    }

    lir_copy_operands(&instruction.operands, operands, operand_count);
    array_push(&block->instructions, &instruction);

    return instruction.result;
}

void lir_jump(LirFunction *function, LirBlockId block_id, LirBlockId target, const LirOperand *args, size_t arg_count) {
    LirBlock *block = lir_get_block(function, block_id);

    lir_assert_unterminated(function, block_id);

    block->terminator.kind = LIR_TERM_JUMP;
    block->terminator.jump.target = target;
    lir_copy_operands(&block->terminator.jump.args, args, arg_count);
}

void lir_branch(LirFunction *function, LirBlockId block_id, LirOperand condition, LirBlockId then_block, const LirOperand *then_args, size_t then_arg_count, LirBlockId else_block, const LirOperand *else_args, size_t else_arg_count) {
    LirBlock *block = lir_get_block(function, block_id);

    lir_assert_unterminated(function, block_id);

    block->terminator.kind = LIR_TERM_BRANCH;
    block->terminator.branch.condition = condition;
    block->terminator.branch.then_block = then_block;
    block->terminator.branch.else_block = else_block;

    lir_copy_operands(&block->terminator.branch.then_args, then_args, then_arg_count);

    lir_copy_operands(&block->terminator.branch.else_args, else_args, else_arg_count);
}

void lir_return(LirFunction *function, LirBlockId block_id, const LirOperand *value) {
    LirBlock *block = lir_get_block(function, block_id);

    lir_assert_unterminated(function, block_id);

    block->terminator.kind = LIR_TERM_RETURN;
    block->terminator._return.has_value = value != NULL;

    if (value) {
        block->terminator._return.value = *value;
    }
}

void lir_unreachable(LirFunction *function, LirBlockId block_id) {
    LirBlock *block = lir_get_block(function, block_id);

    lir_assert_unterminated(function, block_id);

    block->terminator.kind = LIR_TERM_UNREACHABLE;
}

LirOperand lir_operand_value(LirValueId value, HirType *type) {
    return (LirOperand){
        .kind = LIR_OPERAND_VALUE,
        .type = type,
        .value = value,
    };
}

LirOperand lir_operand_int(int64_t value, HirType *type) {
    return (LirOperand){
        .kind = LIR_OPERAND_INT,
        .type = type,
        .int_value = value,
    };
}

LirOperand lir_operand_uint(uint64_t value, HirType *type) {
    return (LirOperand){
        .kind = LIR_OPERAND_UINT,
        .type = type,
        .uint_value = value,
    };
}

LirOperand lir_operand_float(double value, HirType *type) {
    return (LirOperand){
        .kind = LIR_OPERAND_FLOAT,
        .type = type,
        .float_value = value,
    };
}

LirOperand lir_operand_bool(bool value, HirType *type) {
    return (LirOperand){
        .kind = LIR_OPERAND_BOOL,
        .type = type,
        .bool_value = value,
    };
}

LirOperand lir_operand_symbol(Symbol *symbol, HirType *type) {
    return (LirOperand){
        .kind = LIR_OPERAND_SYMBOL,
        .type = type,
        .symbol = symbol,
    };
}

LirOperand lir_operand_invalid(void) {
    return (LirOperand){
        .kind = LIR_OPERAND_INVALID,
        .type = NULL,
    };
}

const char *lir_opcode_name(LirOpcode opcode) {
    switch (opcode) {
        case LIR_OP_NEG: return "neg";
        case LIR_OP_NOT: return "not";

        case LIR_OP_ADD: return "add";
        case LIR_OP_SUB: return "sub";
        case LIR_OP_MUL: return "mul";
        case LIR_OP_DIV: return "div";
        case LIR_OP_MOD: return "mod";

        case LIR_OP_SHL: return "shl";
        case LIR_OP_SHR: return "shr";

        case LIR_OP_AND: return "and";
        case LIR_OP_OR: return "or";
        case LIR_OP_XOR: return "xor";

        case LIR_OP_LT: return "lt";
        case LIR_OP_LE: return "le";
        case LIR_OP_GT: return "gt";
        case LIR_OP_GE: return "ge";
        case LIR_OP_EQ: return "eq";
        case LIR_OP_NE: return "ne";

        case LIR_OP_CAST: return "cast";

        case LIR_OP_LOAD: return "load";
        case LIR_OP_STORE: return "store";

        case LIR_OP_ADDR: return "addr";
        case LIR_OP_ADDR_ADD: return "addr_add";

        case LIR_OP_CALL: return "call";
    }

    return "<invalid>";
}

static void lir_print_operand(FILE *out, LirOperand operand) {
    switch (operand.kind) {
        case LIR_OPERAND_VALUE:
            fprintf(out, "%%%u", operand.value);
            break;

        case LIR_OPERAND_INT:
            fprintf(out, "%" PRId64, operand.int_value);
            break;

        case LIR_OPERAND_UINT:
            fprintf(out, "%" PRIu64, operand.uint_value);
            break;

        case LIR_OPERAND_FLOAT:
            fprintf(out, "%g", operand.float_value);
            break;

        case LIR_OPERAND_BOOL:
            fprintf(out, "%s", operand.bool_value ? "true" : "false");
            break;

        case LIR_OPERAND_SYMBOL:
            fprintf(out, "@%.*s", string_fmt(operand.symbol->name.ident));
            break;

        case LIR_OPERAND_INVALID:
            fprintf(out, "<invalid>");
            break;
    }
}

static void lir_print_operand_array(FILE *out, const Array(LirOperand) *operands) {
    for (size_t i = 0; i < operands->len; i++) {
        if (i != 0) {
            fprintf(out, ", ");
        }

        lir_print_operand(out, ((LirOperand *)operands->data)[i]);
    }
}

static void lir_print_instruction(FILE *out, const LirInstruction *instruction) {
    if (instruction->result != LIR_INVALID_VALUE) {
        fprintf(out, "        %%%u = ", instruction->result);
    } else {
        fprintf(out, "        ");
    }

    fprintf(out, "%s", lir_opcode_name(instruction->opcode));

    if (instruction->operands.len != 0) {
        fprintf(out, " ");
        lir_print_operand_array(out, &instruction->operands);
    }

    fprintf(out, "\n");
}

static void lir_print_terminator(FILE *out, const LirTerminator *terminator) {
    switch (terminator->kind) {
        case LIR_TERM_NONE:
            fprintf(out, "        <unterminated>\n");
            break;

        case LIR_TERM_JUMP:
            fprintf(out, "        jump block%u", terminator->jump.target);

            if (terminator->jump.args.len != 0) {
                fprintf(out, "(");
                lir_print_operand_array(out, &terminator->jump.args);
                fprintf(out, ")");
            }

            fprintf(out, "\n");
            break;

        case LIR_TERM_BRANCH:
            fprintf(out, "        branch ");
            lir_print_operand(out, terminator->branch.condition);

            fprintf(out, ", block%u", terminator->branch.then_block);
            if (terminator->branch.then_args.len != 0) {
                fprintf(out, "(");
                lir_print_operand_array(out, &terminator->branch.then_args);
                fprintf(out, ")");
            }

            fprintf(out, ", block%u", terminator->branch.else_block);
            if (terminator->branch.else_args.len != 0) {
                fprintf(out, "(");
                lir_print_operand_array(out, &terminator->branch.else_args);
                fprintf(out, ")");
            }

            fprintf(out, "\n");
            break;

        case LIR_TERM_RETURN:
            fprintf(out, "        return");

            if (terminator->_return.has_value) {
                fprintf(out, " ");
                lir_print_operand(out, terminator->_return.value);
            }

            fprintf(out, "\n");
            break;

        case LIR_TERM_UNREACHABLE:
            fprintf(out, "        unreachable\n");
            break;
    }
}

static void lir_print_function(FILE *out, const LirFunction *function) {
    fprintf(out, "%.*s:\n", string_fmt(function->symbol->name.ident));

    for (size_t i = 0; i < function->blocks.len; i++) {
        const LirBlock *block = ((LirBlock **)function->blocks.data)[i];

        fprintf(out, "    block%u", block->id);

        if (block->params.len != 0) {
            fprintf(out, "(");

            for (size_t j = 0; j < block->params.len; j++) {
                if (j != 0) {
                    fprintf(out, ", ");
                }

                fprintf(out, "%%%u", ((LirBlockParam *)block->params.data)[j].value);
            }

            fprintf(out, ")");
        }

        fprintf(out, ":\n");

        for (size_t j = 0; j < block->instructions.len; j++) {
            lir_print_instruction(out, &((LirInstruction *)block->instructions.data)[j]);
        }

        lir_print_terminator(out, &block->terminator);
        fprintf(out, "\n");
    }
}

void lir_print(FILE *out, const LirModule *module) {
    for (size_t i = 0; i < module->functions.len; i++) {
        lir_print_function(out, ((LirFunction **)module->functions.data)[i]);
    }
}