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

    HirType *float_type = builtin_type(arena, BUILTIN_FLOAT32, 4, 4);
    HirType *int_type = builtin_type(arena, BUILTIN_INT32, 4, 4);
    Symbol *function_symbol = make_symbol(arena, "reject_float");

    LirFunction *function = lir_function_create(module, function_symbol, int_type, false, false);
    LirBlockId block = lir_block_create(function);
    Array(LirOperand) operands = array_create(arena, sizeof(LirOperand));
    LirValueId value = lir_emit(function, block, LIR_OP_ZERO, float_type, operands);
    (void)value;

    LirOperand ret = lir_operand_value(0, float_type);
    lir_set_terminator(function, block, (LirTerminator){
        .kind = LIR_TERM_RETURN,
        ._return = {
            .has_value = true,
            .value = ret,
        },
    });

    char *generated = NULL;
    size_t generated_len = 0;
    FILE *out = open_memstream(&generated, &generated_len);
    bool ok = backend->emit(&(Backend){.api = backend}, target, module, out);
    fclose(out);

    free(generated);
    arena_destroy(arena);

    return !ok;
}
