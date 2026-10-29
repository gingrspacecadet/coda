#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/orion/orion.h"
#include "lir.h"
#include "lir_lower.h"
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

bool test_extern_lowering(void) {
    Arena *arena = arena_create();
    AstModule current = {0};
    HirModule hir = {
        .owner_module = &current,
        .globals = array_create(arena, sizeof(HirGlobal)),
        .functions = array_create(arena, sizeof(HirFunction)),
    };

    AstModule foreign = {0};

    HirType *int32_type = arena_calloc(arena, sizeof(*int32_type));
    *int32_type = (HirType){ .kind = HIR_TYPE_BUILTIN, .builtin = BUILTIN_INT32, .size = 4, .align = 4 };

    Symbol *extern_global_symbol = arena_calloc(arena, sizeof(*extern_global_symbol));
    *extern_global_symbol = (Symbol){
        .kind = SYMBOL_GLOBAL,
        .name = (AstName){ .kind = AST_NAME_IDENT, .ident = STRING("extern_global") },
        .owner_module = &foreign,
    };

    HirGlobal extern_global = {
        .symbol = extern_global_symbol,
        .type = int32_type,
        .is_extern = true,
        .is_export = false,
    };
    array_push(&hir.globals, &extern_global);

    Symbol *local_fn_symbol = arena_calloc(arena, sizeof(*local_fn_symbol));
    *local_fn_symbol = (Symbol){
        .kind = SYMBOL_FN,
        .name = (AstName){ .kind = AST_NAME_IDENT, .ident = STRING("local_fn") },
        .owner_module = &current,
        .link_name = STRING("local_fn"),
    };

    HirFunction local_fn = {
        .symbol = local_fn_symbol,
        .return_type = int32_type,
        .params = array_create(arena, sizeof(HirParam)),
        .locals = array_create(arena, sizeof(HirLocal)),
        .is_extern = false,
        .is_export = false,
        .body = NULL,
    };
    array_push(&hir.functions, &local_fn);

    Symbol *foreign_fn_symbol = arena_calloc(arena, sizeof(*foreign_fn_symbol));
    *foreign_fn_symbol = (Symbol){
        .kind = SYMBOL_FN,
        .name = (AstName){ .kind = AST_NAME_IDENT, .ident = STRING("foreign_fn") },
        .owner_module = &foreign,
        .link_name = STRING("foreign_fn"),
    };

    HirFunction foreign_fn = {
        .symbol = foreign_fn_symbol,
        .return_type = int32_type,
        .params = array_create(arena, sizeof(HirParam)),
        .locals = array_create(arena, sizeof(HirLocal)),
        .is_extern = false,
        .is_export = false,
        .body = NULL,
    };
    array_push(&hir.functions, &foreign_fn);

    LirModule *lowered = lir_lower_module(arena, &hir);
    bool ok = lowered->globals.len == 0 && lowered->functions.len == 1 && ((LirFunction **)lowered->functions.data)[0]->symbol == local_fn_symbol;
    arena_destroy(arena);
    return ok;
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
