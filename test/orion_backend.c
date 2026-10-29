#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/orion/orion.h"
#include "lir.h"
#include "target.h"
#include "test.h"

static HirType *builtin_type(Arena *arena, BuiltinType builtin, size_t size, size_t align) {
    HirType *type = arena_calloc(arena, sizeof(*type));
    *type = (HirType){
        .kind = HIR_TYPE_BUILTIN,
        .builtin = builtin,
        .size = size,
        .align = align,
    };
    return type;
}

static Symbol *make_symbol(Arena *arena, const char *name) {
    Symbol *symbol = arena_calloc(arena, sizeof(*symbol));
    *symbol = (Symbol){
        .kind = SYMBOL_FN,
        .name = (AstName){.kind = AST_NAME_IDENT, .ident = { .data = (char *)name, .length = strlen(name) }},
        .link_name = { .data = (char *)name, .length = strlen(name) },
    };
    return symbol;
}

bool test_orion_backend(void) {
    Arena *arena = arena_create();
    LirModule *module = lir_module_create(arena);
    const TargetInfo *target = target_builtin("orion");
    const BackendApi *backend = orion_backend();

    HirType *int64_type = builtin_type(arena, BUILTIN_INT64, 8, 8);
    HirType *pair_type = arena_calloc(arena, sizeof(*pair_type));
    *pair_type = (HirType){
        .kind = HIR_TYPE_STRUCT,
        .size = 16,
        .align = 8,
        .structure = {
            .fields = array_create(arena, sizeof(HirField)),
        },
    };
    HirField x_field = { .symbol = make_symbol(arena, "x"), .type = int64_type, .offset = 0 };
    HirField y_field = { .symbol = make_symbol(arena, "y"), .type = int64_type, .offset = 8 };
    array_push(&pair_type->structure.fields, &x_field);
    array_push(&pair_type->structure.fields, &y_field);

    Symbol *pair_symbol = make_symbol(arena, "make_pair");
    LirFunction *function = lir_function_create(module, pair_symbol, pair_type, false, false);
    LirBlockId block = lir_block_create(function);

    LirValueId x_value = lir_function_add_param(function, make_symbol(arena, "x"), int64_type);
    LirValueId y_value = lir_function_add_param(function, make_symbol(arena, "y"), int64_type);

    LirOperand zero = lir_operand_int(0, int64_type);
    LirOperand x_op = lir_operand_value(x_value, int64_type);
    LirOperand y_op = lir_operand_value(y_value, int64_type);

    LirOperand zero_insert_operands[3] = { lir_operand_value(LIR_INVALID_VALUE, pair_type), lir_operand_offset(0), x_op };
    (void)zero_insert_operands;
    LirOperand struct_base = lir_operand_value(0, pair_type);
    LirOperand init_zero[3] = { struct_base, lir_operand_offset(0), zero };
    LirValueId value0 = lir_emit(function, block, LIR_OP_INSERT, pair_type, (Array){ .data = init_zero, .len = 3 });

    LirOperand insert_x_operands[3] = { lir_operand_value(value0, pair_type), lir_operand_offset(0), x_op };
    LirValueId value1 = lir_emit(function, block, LIR_OP_INSERT, pair_type, (Array){ .data = insert_x_operands, .len = 3 });
    LirOperand insert_y_operands[3] = { lir_operand_value(value1, pair_type), lir_operand_offset(8), y_op };
    LirValueId result = lir_emit(function, block, LIR_OP_INSERT, pair_type, (Array){ .data = insert_y_operands, .len = 3 });

    lir_set_terminator(function, block, (LirTerminator){
        .kind = LIR_TERM_RETURN,
        ._return = {
            .has_value = true,
            .value = lir_operand_value(result, pair_type),
        },
    });

    char *generated = NULL;
    size_t generated_len = 0;
    FILE *out = open_memstream(&generated, &generated_len);
    bool ok = backend->emit(&(Backend){.api = backend}, target, module, out);
    fclose(out);

    bool produced_output = generated_len > 0;
    bool stack_pointer_not_reused = strstr(generated, "ldr r15, ") == NULL && strstr(generated, "str r15, ") == NULL;
    free(generated);
    arena_destroy(arena);

    return ok && produced_output && stack_pointer_not_reused;
}
