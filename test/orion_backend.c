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

    LirModule *lowered = lir_lower_module(arena, &hir, &(TargetInfo){.pointer = {.size = 4, .align = 4}});
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

static void add_unary_not_function(Arena *arena, LirModule *module,
                                   const char *name, HirType *type) {
    LirFunction *function = lir_function_create(module, make_symbol(arena, name), type, false, false);
    LirBlockId block = lir_block_create(function);
    LirValueId parameter = lir_function_add_param(function, make_symbol(arena, "value"), type);
    LirOperand operand = lir_operand_value(parameter, type);
    LirValueId result = lir_emit(function, block, LIR_OP_NOT, type,
                                 (Array){ .data = &operand, .len = 1 });
    lir_set_terminator(function, block, (LirTerminator){
        .kind = LIR_TERM_RETURN,
        ._return = {
            .has_value = true,
            .value = lir_operand_value(result, type),
        },
    });
}

bool test_orion_unary_not(void) {
    Arena *arena = arena_create();
    LirModule *module = lir_module_create(arena);
    const TargetInfo *target = target_builtin("orion");
    const BackendApi *backend = orion_backend();

    HirType *bool_type = builtin_type(arena, BUILTIN_BOOL, 1, 1);
    HirType *int32_type = builtin_type(arena, BUILTIN_INT32, 4, 4);
    add_unary_not_function(arena, module, "logical_not", bool_type);
    add_unary_not_function(arena, module, "bitwise_not", int32_type);

    char *generated = NULL;
    size_t generated_len = 0;
    FILE *out = open_memstream(&generated, &generated_len);
    bool emitted = out != NULL &&
                   backend->emit(&(Backend){.api = backend}, target, module, out);
    if (out != NULL && fclose(out) != 0)
        emitted = false;

    char *logical_not = generated != NULL ? strstr(generated, "logical_not:") : NULL;
    char *bitwise_not = logical_not != NULL ? strstr(logical_not, "bitwise_not:") : NULL;
    char *logical_xor = logical_not != NULL ? strstr(logical_not, "xor r0, r0, #1\n") : NULL;
    char *logical_lui = logical_not != NULL ? strstr(logical_not, "lui r12, #65535\n") : NULL;
    char *integer_lui = bitwise_not != NULL ? strstr(bitwise_not, "lui r12, #65535\n") : NULL;
    char *integer_xor = bitwise_not != NULL ? strstr(bitwise_not, "xor r0, r0, r12\n") : NULL;
    bool logical_uses_boolean_inversion =
        bitwise_not != NULL && logical_xor != NULL &&
        logical_xor < bitwise_not &&
        (logical_lui == NULL || logical_lui >= bitwise_not);
    bool integer_uses_bitwise_inversion =
        integer_lui != NULL && integer_xor != NULL &&
        integer_lui < integer_xor;

    free(generated);
    arena_destroy(arena);
    return emitted && generated_len > 0 &&
           logical_uses_boolean_inversion && integer_uses_bitwise_inversion;
}

bool test_orion_packed_field_access(void) {
    Arena *arena = arena_create();
    const TargetInfo *target = target_builtin("orion");
    HirType *uint16_type = builtin_type(arena, BUILTIN_UINT16, 2, 2);
    HirType *uint32_type = builtin_type(arena, BUILTIN_UINT32, 4, 4);
    HirType *packed_type = arena_calloc(arena, sizeof(*packed_type));
    *packed_type = (HirType){
        .kind = HIR_TYPE_STRUCT,
        .size = 6,
        .align = 1,
        .structure = {.fields = array_create(arena, sizeof(HirField))},
    };
    HirField prefix = {.symbol = make_symbol(arena, "prefix"), .type = uint16_type, .offset = 0};
    HirField word = {.symbol = make_symbol(arena, "word"), .type = uint32_type, .offset = 2};
    array_push(&packed_type->structure.fields, &prefix);
    array_push(&packed_type->structure.fields, &word);

    HirType *pointer_type = arena_calloc(arena, sizeof(*pointer_type));
    *pointer_type = (HirType){
        .kind = HIR_TYPE_POINTER,
        .size = 4,
        .align = 4,
        .pointer = {.pointee = packed_type, .optional = false},
    };

    Symbol *function_symbol = make_symbol(arena, "packed_field_roundtrip");
    Symbol *parameter_symbol = make_symbol(arena, "packed");
    parameter_symbol->kind = SYMBOL_PARAMETER;

    HirExpr *parameter = arena_calloc(arena, sizeof(*parameter));
    *parameter = (HirExpr){
        .kind = HIR_EXPR_VALUE,
        .type = pointer_type,
        .value = {.symbol = parameter_symbol},
    };

    HirExpr *dereference = arena_calloc(arena, sizeof(*dereference));
    *dereference = (HirExpr){
        .kind = HIR_EXPR_UNARY,
        .type = packed_type,
        .unary = {.op = AST_UNARY_DEREF, .operand = parameter},
    };

    HirExpr *field = arena_calloc(arena, sizeof(*field));
    *field = (HirExpr){
        .kind = HIR_EXPR_FIELD,
        .type = uint32_type,
        .field = {.object = dereference, .field = &((HirField *)packed_type->structure.fields.data)[1]},
    };

    HirExpr *value = arena_calloc(arena, sizeof(*value));
    *value = (HirExpr){
        .kind = HIR_EXPR_LITERAL,
        .type = uint32_type,
        .literal = {.kind = HIR_LITERAL_INTEGER, .integer = UINT32_C(0x12345678)},
    };

    HirStmt *assign = arena_calloc(arena, sizeof(*assign));
    *assign = (HirStmt){
        .kind = HIR_STMT_ASSIGN,
        .assign = {.op = AST_BINARY_ASSIGN, .target = field, .value = value},
    };

    HirExpr *return_field = arena_calloc(arena, sizeof(*return_field));
    *return_field = *field;
    HirStmt *ret = arena_calloc(arena, sizeof(*ret));
    *ret = (HirStmt){.kind = HIR_STMT_RETURN, ._return = {.value = return_field}};

    HirStmt *body = arena_calloc(arena, sizeof(*body));
    *body = (HirStmt){
        .kind = HIR_STMT_BLOCK,
        .block = {.stmts = array_create(arena, sizeof(HirStmt *))},
    };
    array_push(&body->block.stmts, &assign);
    array_push(&body->block.stmts, &ret);

    HirFunction hir_function = {
        .symbol = function_symbol,
        .return_type = uint32_type,
        .params = array_create(arena, sizeof(HirParam)),
        .locals = array_create(arena, sizeof(HirLocal)),
        .body = body,
    };
    HirParam parameter_decl = {.symbol = parameter_symbol, .type = pointer_type};
    array_push(&hir_function.params, &parameter_decl);

    HirModule hir = {
        .globals = array_create(arena, sizeof(HirGlobal)),
        .functions = array_create(arena, sizeof(HirFunction)),
    };
    array_push(&hir.functions, &hir_function);

    LirModule *module = lir_lower_module(arena, &hir, target);
    bool lowered_unaligned_load = false;
    bool lowered_unaligned_store = false;
    LirFunction *lowered_function = ((LirFunction **)module->functions.data)[0];
    for (size_t i = 0; i < lowered_function->blocks.len; i++) {
        LirBlock *block = ((LirBlock **)lowered_function->blocks.data)[i];
        for (size_t j = 0; j < block->instructions.len; j++) {
            LirInstruction *instruction = &((LirInstruction *)block->instructions.data)[j];
            if (instruction->opcode == LIR_OP_LOAD)
                lowered_unaligned_load = ((LirOperand *)instruction->operands.data)[0].alignment == 1;
            if (instruction->opcode == LIR_OP_STORE)
                lowered_unaligned_store = ((LirOperand *)instruction->operands.data)[0].alignment == 1;
        }
    }

    const BackendApi *backend = orion_backend();
    char *generated = NULL;
    size_t generated_len = 0;
    FILE *out = open_memstream(&generated, &generated_len);
    bool emitted = out != NULL && backend->emit(&(Backend){.api = backend}, target, module, out);
    if (out != NULL && fclose(out) != 0)
        emitted = false;

    bool byte_load = generated != NULL &&
        strstr(generated, "ldrb r12, [r14 + #3]\n") != NULL;
    bool byte_store = generated != NULL &&
        strstr(generated, "strb r12, [r14 + #3]\n") != NULL;

    free(generated);
    arena_destroy(arena);
    return emitted && generated_len > 0 && lowered_unaligned_load &&
           lowered_unaligned_store && byte_load && byte_store;
}
