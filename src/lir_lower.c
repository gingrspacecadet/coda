#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "sema/common.h"
#include "lir_lower.h"

typedef struct {
    Symbol *symbol;
    LirOperand operand;
} LirBinding;

typedef struct {
    LirOperand address;
    HirType *type;
} LirPlace;

typedef struct {
    LirBlockId header;
    LirBlockId exit;
    Array(LirBinding) bindings;
    size_t stack_save_depth;
} LirLoop;

typedef struct {
    LirModule *module;
    LirFunction *function;
    LirBlockId block;

    Array(LirBinding) bindings;
    Array(LirBinding) addresses;
    Array(Symbol *) address_taken;
    Array(LirOperand) stack_saves;
    Array(LirLoop) loops;

    const TargetInfo *target;
} LirLower;

static LirBlock *lir_lower_block(LirLower *lower) {
    return ((LirBlock **)lower->function->blocks.data)[lower->block];
}

static bool lir_lower_block_terminated(LirLower *lower) {
    return lir_lower_block(lower)->terminator.kind != LIR_TERM_NONE;
}

static bool lir_same_symbol(Symbol *a, Symbol *b) {
    if (a == b)
        return true;

    return a != NULL && b != NULL && a->kind == b->kind &&
           a->name.kind == AST_NAME_IDENT && b->name.kind == AST_NAME_IDENT &&
           string_eq(a->name.ident, b->name.ident);
}

static LirBinding *lir_find_binding(LirLower *lower, Symbol *symbol) {
    for (size_t i = lower->bindings.len; i > 0; i--) {
        LirBinding *binding = &((LirBinding *)lower->bindings.data)[i - 1];

        if (lir_same_symbol(binding->symbol, symbol)) {
            return binding;
        }
    }
    
    return NULL;
}

static LirBinding *lir_find_address(LirLower *lower, Symbol *symbol) {
    for (size_t i = lower->addresses.len; i > 0; i--) {
        LirBinding *binding = &((LirBinding *)lower->addresses.data)[i - 1];
        if (lir_same_symbol(binding->symbol, symbol))
            return binding;
    }

    return NULL;
}

static bool lir_is_address_taken(LirLower *lower, Symbol *symbol) {
    for (size_t i = 0; i < lower->address_taken.len; i++) {
        if (lir_same_symbol(((Symbol **)lower->address_taken.data)[i], symbol))
            return true;
    }

    return false;
}

static void lir_bind_address(LirLower *lower, Symbol *symbol, LirOperand address) {
    LirBinding binding = {.symbol = symbol, .operand = address};
    array_push(&lower->addresses, &binding);
}

static void lir_bind(LirLower *lower, Symbol *symbol, LirOperand operand) {
    LirBinding binding = {
        .symbol = symbol,
        .operand = operand,
    };

    array_push(&lower->bindings, &binding);
}

static LirOpcode lir_lower_unary_op(AstUnaryOp op) {
    switch (op) {
        case AST_UNARY_NEG:
            return LIR_OP_NEG;

        case AST_UNARY_NOT:
        case AST_UNARY_BIT_NOT:
            return LIR_OP_NOT;

        case AST_UNARY_DEREF:
            return LIR_OP_LOAD;

        case AST_UNARY_ADDRESS:
            return LIR_OP_ADDR;
    }

    assert(!"unhandled unary operator");
    return LIR_OP_NEG;
}

static LirOpcode lir_lower_binary_op(AstBinaryOp op) {
    switch (op) {
        case AST_BINARY_MUL:
            return LIR_OP_MUL;

        case AST_BINARY_DIV:
            return LIR_OP_DIV;

        case AST_BINARY_MOD:
            return LIR_OP_MOD;

        case AST_BINARY_ADD:
            return LIR_OP_ADD;

        case AST_BINARY_SUB:
            return LIR_OP_SUB;

        case AST_BINARY_SHL:
            return LIR_OP_SHL;

        case AST_BINARY_SHR:
            return LIR_OP_SHR;

        case AST_BINARY_LT:
            return LIR_OP_LT;

        case AST_BINARY_LTE:
            return LIR_OP_LE;

        case AST_BINARY_GT:
            return LIR_OP_GT;

        case AST_BINARY_GTE:
            return LIR_OP_GE;

        case AST_BINARY_EQUAL:
            return LIR_OP_EQ;

        case AST_BINARY_NOT_EQUAL:
            return LIR_OP_NE;

        case AST_BINARY_BIT_AND:
            return LIR_OP_AND;

        case AST_BINARY_BIT_XOR:
            return LIR_OP_XOR;

        case AST_BINARY_BIT_OR:
            return LIR_OP_OR;

        case AST_BINARY_LOGICAL_AND:
        case AST_BINARY_LOGICAL_OR:
            assert(!"logical operators require control-flow lowering");
            return LIR_OP_AND;

        case AST_BINARY_ASSIGN:
        case AST_BINARY_ADD_ASSIGN:
            assert(!"assignment operators should not reach HIR binary lowering");
            return LIR_OP_ADD;
    }

    assert(!"unhandled binary operator");
    return LIR_OP_ADD;
}

static bool hir_type_is_signed_integer(HirType *type) {
    while (type != NULL && type->base != NULL)
        type = type->base;

    if (type != NULL && type->kind == HIR_TYPE_ENUM) {
        type = type->_enum.underlying;
        while (type != NULL && type->base != NULL)
            type = type->base;
    }

    if (!type || type->kind != HIR_TYPE_BUILTIN)
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

static bool hir_type_is_integer(HirType *type) {
    if (!type || type->kind != HIR_TYPE_BUILTIN)
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

        case BUILTIN_BOOL:
        case BUILTIN_NONE:
        case BUILTIN_FLOAT32:
        case BUILTIN_FLOAT64:
            return false;
    }

    assert(!"unhandled builtin type");
    return false;
}

static Symbol *lir_add_string(LirModule *module, String bytes) {
    char *name = arena_alloc(module->arena, 64);
    int name_length = snprintf(name, 64, ".L_coda_string_%zu", module->strings.len);
    assert(name_length > 0 && name_length < 64);

    Symbol *symbol = arena_calloc(module->arena, sizeof(*symbol));
    *symbol = (Symbol) {
        .kind = SYMBOL_GLOBAL,
        .name = {
            .kind = AST_NAME_IDENT,
            .ident = {.data = name, .length = (size_t)name_length},
        },
    };

    LirString string = {.symbol = symbol, .bytes = bytes};
    array_push(&module->strings, &string);

    return symbol;
}

static LirOperand lir_lower_literal(LirLower *lower, HirExpr *expr) {
    HirLiteral literal = expr->literal;

    switch (literal.kind) {
        case HIR_LITERAL_INTEGER:
            if (hir_type_is_signed_integer(expr->type)) {
                return lir_operand_int((int64_t)literal.integer, expr->type);
            }

            return lir_operand_uint(literal.integer, expr->type);

        case HIR_LITERAL_FLOAT:
            return lir_operand_float(literal.floating, expr->type);

        case HIR_LITERAL_BOOL:
            return lir_operand_bool(literal.boolean, expr->type);

        case HIR_LITERAL_NULL:
            return lir_operand_uint(0, expr->type);

        case HIR_LITERAL_STRING: {
            HirType *type = expr->type;
            assert(type != NULL && type->kind == HIR_TYPE_SLICE && type->slice.fields.len == 2);

            HirField *length_field = &((HirField *)type->slice.fields.data)[0];
            HirField *pointer_field = &((HirField *)type->slice.fields.data)[1];
            Symbol *symbol = lir_add_string(lower->module, literal.string);

            LirOperand symbol_operand = lir_operand_symbol(symbol, pointer_field->type);
            LirValueId address = lir_emit(lower->function, lower->block, LIR_OP_ADDR, pointer_field->type, (Array){.data = &symbol_operand, .len = 1});
            LirValueId zero = lir_emit(lower->function, lower->block, LIR_OP_ZERO, type, (Array){0});
            LirOperand aggregate = lir_operand_value(zero, type);
            LirOperand length = lir_operand_uint((uint64_t)literal.string.length, length_field->type);
            LirOperand length_operands[3] = {aggregate, lir_operand_offset(length_field->offset), length};
            LirValueId with_length = lir_emit(lower->function, lower->block, LIR_OP_INSERT, type, (Array){.data = length_operands, .len = 3});
            aggregate = lir_operand_value(with_length, type);

            LirOperand pointer = lir_operand_value(address, pointer_field->type);
            LirOperand pointer_operands[3] = {aggregate, lir_operand_offset(pointer_field->offset), pointer};
            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT, type, (Array){.data = pointer_operands, .len = 3});

            return lir_operand_value(result, type);
        }
    }

    assert(!"unhandled HIR literal");
    return lir_operand_invalid();
}

static LirOperand lir_lower_logical_and(LirLower *lower, HirExpr *expr);
static LirOperand lir_lower_logical_or(LirLower *lower, HirExpr *expr);

static LirPlace lir_lower_place(LirLower *lower, HirExpr *expr);
static LirOperand lir_lower_load(LirLower *lower, LirPlace place);
static void lir_lower_store(LirLower *lower, LirPlace place, LirOperand value);
static HirExpr *lir_field_root(HirExpr *expr);
static bool lir_expr_is_ssa(LirLower *lower, HirExpr *expr);

static size_t array_data_offset(HirType *type) {
    assert(type->kind == HIR_TYPE_ARRAY);
    assert(type->array.fields.len > 0);

    HirField *length_field = &((HirField *)type->array.fields.data)[0];
    size_t size = length_field->offset + length_field->type->size;
    size_t align = type->array.element->align;
    size_t remainder = size % align;

    return remainder == 0 ? size : size + align - remainder;
}

static bool lir_constant_array_offset(HirExpr *expr, size_t *offset) {
    HirType *type = expr->index.object->type;

    if (type == NULL || type->kind != HIR_TYPE_ARRAY)
        return false;

    HirExpr *index = expr->index.index;

    if (index->kind != HIR_EXPR_LITERAL || index->literal.kind != HIR_LITERAL_INTEGER)
        return false;

    uint64_t value = index->literal.integer;

    assert(value < type->array.length || (type->array.length == 0 && value == 0));
    assert(value <= SIZE_MAX);

    HirType *element = type->array.element;
    size_t data_offset = array_data_offset(type);

    assert(element->size == 0 || value <= (uint64_t)(SIZE_MAX - data_offset) / element->size);

    *offset = data_offset + (size_t)value * element->size;
    return true;
}

static LirOperand lir_lower_scaled_index(LirLower *lower, LirOperand index, size_t element_size) {
    LirOperand scale = hir_type_is_signed_integer(index.type)
        ? lir_operand_int((int64_t)element_size, index.type)
        : lir_operand_uint((uint64_t)element_size, index.type);

    LirOperand operands[2] = {index, scale};
    LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_MUL, index.type, (Array){.data = operands, .len = 2});
    return lir_operand_value(result, index.type);
}

static size_t hir_type_pointer_stride(HirType *type) {
    size_t size = type->pointer.pointee->size;
    return size != 0 ? size : 1;
}

static bool hir_type_is_pointer(HirType *type);

static bool symbol_is_extern(const Symbol *symbol) {
    if (symbol == NULL || symbol->decl == NULL)
        return false;

    for (size_t i = 0; i < symbol->decl->attrs.len; i++) {
        AstAttribute *attribute = array_at(&symbol->decl->attrs, i);
        if (attribute != NULL && string_eq(attribute->name, STRING("extern")))
            return true;
    }

    return false;
}

static LirOperand lir_lower_binary_values(LirLower *lower, AstBinaryOp op, HirType *type, LirOperand left, LirOperand right) {
    if ((op == AST_BINARY_ADD || op == AST_BINARY_SUB) &&
        hir_type_is_pointer(left.type) && hir_type_is_integer(right.type)) {
        LirOperand offset = lir_lower_scaled_index(lower, right, hir_type_pointer_stride(left.type));
        if (op == AST_BINARY_SUB) {
            LirOperand zero = hir_type_is_signed_integer(offset.type)
                ? lir_operand_int(0, offset.type)
                : lir_operand_uint(0, offset.type);
            LirOperand negate_operands[2] = {zero, offset};
            LirValueId negated = lir_emit(lower->function, lower->block, LIR_OP_SUB, offset.type, (Array){.data = negate_operands, .len = 2});
            offset = lir_operand_value(negated, offset.type);
        }
        LirOperand operands[2] = {left, offset};
        LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_ADDR_ADD, type, (Array){.data = operands, .len = 2});
        return lir_operand_value(result, type);
    }

    if (op == AST_BINARY_ADD && hir_type_is_integer(left.type) && hir_type_is_pointer(right.type)) {
        LirOperand offset = lir_lower_scaled_index(lower, left, hir_type_pointer_stride(right.type));
        LirOperand operands[2] = {right, offset};
        LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_ADDR_ADD, type, (Array){.data = operands, .len = 2});
        return lir_operand_value(result, type);
    }

    if (op == AST_BINARY_SUB && hir_type_is_pointer(left.type) && hir_type_is_pointer(right.type)) {
        LirOperand element_size = lir_operand_offset(hir_type_pointer_stride(left.type));
        LirOperand operands[3] = {left, right, element_size};
        LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_ADDR_DIFF, type, (Array){.data = operands, .len = 3});
        return lir_operand_value(result, type);
    }

    if (op == AST_BINARY_NAND || op == AST_BINARY_NOR) {
        LirOperand operands[2] = {left, right};
        LirOpcode opcode = op == AST_BINARY_NAND ? LIR_OP_AND : LIR_OP_OR;
        LirValueId intermediate = lir_emit(lower->function, lower->block, opcode, type, (Array){.data = operands, .len = 2});
        LirOperand operand = lir_operand_value(intermediate, type);
        LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_NOT, type, (Array){.data = &operand, .len = 1});
        return lir_operand_value(result, type);
    }

    LirOperand operands[2] = {left, right};
    LirValueId result = lir_emit(lower->function, lower->block, lir_lower_binary_op(op), type, (Array){.data = operands, .len = 2});
    return lir_operand_value(result, type);
}

static bool hir_type_is_pointer(HirType *type) {
    return type && type->kind == HIR_TYPE_POINTER;
}

static bool lir_expr_is_place(HirExpr *expr) {
    if (expr == NULL)
        return false;

    switch (expr->kind) {
        case HIR_EXPR_VALUE:
            return expr->value.symbol != NULL &&
                   (expr->value.symbol->kind == SYMBOL_LOCAL ||
                    expr->value.symbol->kind == SYMBOL_PARAMETER ||
                    expr->value.symbol->kind == SYMBOL_GLOBAL);

        case HIR_EXPR_FIELD:
        case HIR_EXPR_INDEX:
            return true;

        case HIR_EXPR_UNARY:
            return expr->unary.op == AST_UNARY_DEREF;

        default:
            return false;
    }
}

static LirOperand lir_lower_expr(LirLower *lower, HirExpr *expr);

static LirOperand lir_lower_init(LirLower *lower, HirExpr *expr) {
    HirType *type = expr->type;

    LirValueId zero = lir_emit(lower->function, lower->block, LIR_OP_ZERO, type, (Array){0});
    LirOperand aggregate = lir_operand_value(zero, type);

    for (size_t i = 0; i < expr->init.fields.len; i++) {
        HirInitField *init = &((HirInitField *)expr->init.fields.data)[i];
        LirOperand value = lir_lower_expr(lower, init->value);
        LirOperand operands[3] = {
            aggregate,
            lir_operand_offset(init->offset),
            value,
        };

        LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT, type, (Array){.data = operands, .len = 3});
        aggregate = lir_operand_value(result, type);
    }

    return aggregate;
}

static LirOperand lir_lower_array_offset(LirLower *lower, HirExpr *expr);
static LirOperand lir_lower_array_aggregate_offset(LirLower *lower, HirExpr *expr);

static HirType *lir_unwrap_type(HirType *type) {
    while (type != NULL && type->base != NULL)
        type = type->base;
    return type;
}

static HirType *lir_sum_tag_type(LirLower *lower) {
    HirType *type = arena_calloc(lower->function->arena, sizeof(*type));
    *type = (HirType){
        .kind = HIR_TYPE_BUILTIN,
        .size = sizeof(uint64_t),
        .align = _Alignof(uint64_t),
        .builtin = BUILTIN_UINT64,
    };
    return type;
}

static size_t lir_sum_payload_offset(HirType *type) {
    type = lir_unwrap_type(type);
    assert(type != NULL && type->kind == HIR_TYPE_SUM);

    size_t payload_align = 1;

    for (size_t i = 0; i < type->sum.members.len; i++) {
        HirType *member = lir_unwrap_type(((HirType **)type->sum.members.data)[i]);
        if (member->align > payload_align)
            payload_align = member->align;
    }

    size_t tag_size = sizeof(uint64_t);
    size_t remainder = tag_size % payload_align;
    return remainder == 0 ? tag_size : tag_size + payload_align - remainder;
}


static HirType *lir_address_type(LirLower *lower, HirType *pointee);

static void lir_register_witness_global(LirModule *module, HirExpr *expr) {
    for (size_t i = 0; i < module->globals.len; i++) {
        LirGlobal *existing = array_at(&module->globals, i);
        if (existing->symbol == expr->any_box.vtable)
            return;
    }

    LirGlobal global = {
        .symbol = expr->any_box.vtable,
        .type = expr->any_box.vtable_type,
        .data = array_create(module->arena, sizeof(LirData)),
        .is_mutable = false,
        .is_export = false,
    };
    for (size_t i = 0; i < expr->any_box.methods.len; i++) {
        HirField *field = array_at(&expr->any_box.vtable_type->structure.fields, i);
        Symbol *method = ((Symbol **)expr->any_box.methods.data)[i];
        LirData entry = {
            .offset = field->offset,
            .type = field->type,
            .kind = LIR_DATA_ADDRESS,
            .symbol = method,
        };
        array_push(&global.data, &entry);
    }
    array_push(&module->globals, &global);
}

static LirOperand lir_lower_any_box(LirLower *lower, HirExpr *expr) {
    lir_register_witness_global(lower->module, expr);

    HirExpr *address_expr = expr->any_box.address;
    HirExpr *value_expr = address_expr->unary.operand;
    LirOperand address;

    if (lir_expr_is_place(value_expr)) {
        address = lir_lower_expr(lower, address_expr);
    } else {
        LirOperand value = lir_lower_expr(lower, value_expr);
        LirValueId storage_id = lir_emit(lower->function, lower->block, LIR_OP_ZERO, value_expr->type, (Array){0});
        LirOperand storage = lir_operand_value(storage_id, value_expr->type);
        HirType *storage_pointer_type = lir_address_type(lower, value_expr->type);
        LirValueId address_id = lir_emit(lower->function, lower->block, LIR_OP_ADDR, storage_pointer_type, (Array){.data = &storage, .len = 1});
        address = lir_operand_value(address_id, storage_pointer_type);
        lir_lower_store(lower, (LirPlace){.address = address, .type = value_expr->type}, value);
    }
    LirValueId zero = lir_emit(lower->function, lower->block, LIR_OP_ZERO, expr->type, (Array){0});
    LirOperand aggregate = lir_operand_value(zero, expr->type);

    LirOperand data_operands[3] = {aggregate, lir_operand_offset(0), address};
    LirValueId with_data = lir_emit(lower->function, lower->block, LIR_OP_INSERT, expr->type, (Array){.data = data_operands, .len = 3});
    aggregate = lir_operand_value(with_data, expr->type);

    HirType *table_pointer_type = lir_address_type(lower, expr->any_box.vtable_type);
    LirOperand table_pointer = lir_operand_symbol(expr->any_box.vtable, table_pointer_type);
    LirOperand table_operands[3] = {aggregate, lir_operand_offset(lower->target->pointer.size), table_pointer};
    LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT, expr->type, (Array){.data = table_operands, .len = 3});
    return lir_operand_value(result, expr->type);
}

static LirOperand lir_lower_expr(LirLower *lower, HirExpr *expr) {
    switch (expr->kind) {
        case HIR_EXPR_LITERAL:
            return lir_lower_literal(lower, expr);

        case HIR_EXPR_VALUE: {
            Symbol *symbol = expr->value.symbol;

            switch (symbol->kind) {
                case SYMBOL_LOCAL:
                case SYMBOL_PARAMETER: {
                    LirBinding *address = lir_find_address(lower, symbol);
                    if (address != NULL)
                        return lir_lower_load(lower, (LirPlace){.address = address->operand, .type = expr->type});

                    LirBinding *binding = lir_find_binding(lower, symbol);
                    assert(binding);
                    return binding->operand;
                }

                case SYMBOL_GLOBAL: {
                    if (symbol->decl != NULL) {
                        for (size_t i = 0; i < symbol->decl->attrs.len; i++) {
                            AstAttribute *attribute = array_at(&symbol->decl->attrs, i);
                        }
                    }
                    if (symbol_is_extern(symbol))
                        return lir_operand_symbol(symbol, expr->type);

                    LirPlace place = lir_lower_place(lower, expr);
                    return lir_lower_load(lower, place);
                }

                case SYMBOL_FN:
                    return lir_operand_symbol(symbol, expr->type);

                default:
                    assert(!"unexpected symbol kind in HIR value expression");
                    return lir_operand_invalid();
            }
        }

        case HIR_EXPR_UNARY:
            switch (expr->unary.op) {
                case AST_UNARY_DEREF: {
                    LirPlace place = lir_lower_place(lower, expr);

                    return lir_lower_load(lower, place);
                }

                case AST_UNARY_ADDRESS: {
                    if (!lir_expr_is_place(expr->unary.operand)) {
                        LirOperand value = lir_lower_expr(lower, expr->unary.operand);
                        LirValueId storage_id = lir_emit(lower->function, lower->block, LIR_OP_ZERO, value.type, (Array){0});
                        LirOperand storage = lir_operand_value(storage_id, value.type);
                        LirValueId address_id = lir_emit(lower->function, lower->block, LIR_OP_ADDR, expr->type, (Array){.data = &storage, .len = 1});
                        LirOperand address = lir_operand_value(address_id, expr->type);

                        lir_lower_store(lower, (LirPlace){.address = address, .type = value.type}, value);
                        return address;
                    }

                    LirPlace place = lir_lower_place(lower, expr->unary.operand);
                    return place.address;
                }

                default: {
                    LirOperand operand = lir_lower_expr(lower, expr->unary.operand);

                    LirValueId result = lir_emit(lower->function, lower->block, lir_lower_unary_op(expr->unary.op), expr->type, (Array){.data = &operand, .len = 1});

                    return lir_operand_value(result, expr->type);
                }
            }

        case HIR_EXPR_BINARY: {
            if (expr->binary.op == AST_BINARY_LOGICAL_AND)
                return lir_lower_logical_and(lower, expr);

            if (expr->binary.op == AST_BINARY_LOGICAL_OR)
                return lir_lower_logical_or(lower, expr);

            LirOperand left = lir_lower_expr(lower, expr->binary.left);
            LirOperand right = lir_lower_expr(lower, expr->binary.right);
            return lir_lower_binary_values(lower, expr->binary.op, expr->type, left, right);
        }
        case HIR_EXPR_CALL: {
            size_t operand_count = expr->call.args.len + 1;
            LirOperand *operands = arena_calloc(lower->function->arena, sizeof(LirOperand) * operand_count);
            if (expr->call.function != NULL)
                operands[0] = lir_operand_symbol(expr->call.function, expr->call.function->type);
            else {
                assert(expr->call.callee != NULL);
                operands[0] = lir_lower_expr(lower, expr->call.callee);
            }

            for (size_t i = 0; i < expr->call.args.len; i++) {
                operands[i + 1] = lir_lower_expr(lower, ((HirExpr **)expr->call.args.data)[i]);
            }

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CALL, expr->type, (Array){.data = operands, .len = operand_count});

            if (expr->type->kind == HIR_TYPE_BUILTIN && expr->type->builtin == BUILTIN_NONE) {
                return lir_operand_invalid();
            }

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_ANY_BOX:
            return lir_lower_any_box(lower, expr);

        case HIR_EXPR_ANY_CALL: {
            LirOperand object = lir_lower_expr(lower, expr->any_call.object);
            LirOperand data_operands[2] = {object, lir_operand_offset(0)};
            LirValueId data_id = lir_emit(lower->function, lower->block, LIR_OP_EXTRACT, expr->any_call.byte_pointer_type, (Array){.data = data_operands, .len = 2});
            LirOperand data = lir_operand_value(data_id, expr->any_call.byte_pointer_type);

            LirOperand table_operands[2] = {object, lir_operand_offset(lower->target->pointer.size)};
            LirValueId table_id = lir_emit(lower->function, lower->block, LIR_OP_EXTRACT, expr->any_call.byte_pointer_type, (Array){.data = table_operands, .len = 2});
            LirOperand table = lir_operand_value(table_id, expr->any_call.byte_pointer_type);

            LirOperand slot_offset = lir_operand_uint((uint64_t)(expr->any_call.method_index * sizeof(lower->target->pointer.size)), lir_sum_tag_type(lower));
            LirOperand address_operands[2] = {table, slot_offset};
            LirValueId address_id = lir_emit(lower->function, lower->block, LIR_OP_ADDR_ADD, expr->any_call.byte_pointer_type, (Array){.data = address_operands, .len = 2});
            LirOperand address = lir_operand_value(address_id, expr->any_call.byte_pointer_type);
            LirValueId function_id = lir_emit(lower->function, lower->block, LIR_OP_LOAD, expr->any_call.function_pointer_type, (Array){.data = &address, .len = 1});

            size_t operand_count = expr->any_call.args.len + (expr->any_call.has_receiver ? 1 : 0) + 1;
            LirOperand *operands = arena_calloc(lower->function->arena, sizeof(LirOperand) * operand_count);
            operands[0] = lir_operand_value(function_id, expr->any_call.function_pointer_type);
            size_t operand = 1;
            if (expr->any_call.has_receiver)
                operands[operand++] = data;
            for (size_t i = 0; i < expr->any_call.args.len; i++)
                operands[operand++] = lir_lower_expr(lower, ((HirExpr **)expr->any_call.args.data)[i]);

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CALL, expr->type, (Array){.data = operands, .len = operand_count});
            if (expr->type->kind == HIR_TYPE_BUILTIN && expr->type->builtin == BUILTIN_NONE)
                return lir_operand_invalid();
            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_CAST: {
            LirOperand operand = lir_lower_expr(lower, expr->cast.operand);

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CAST, expr->type, (Array){.data = &operand, .len = 1});

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_FIELD: {
            if (!lir_expr_is_ssa(lower, expr)) {
                LirPlace place = lir_lower_place(lower, expr);
                return lir_lower_load(lower, place);
            }

            LirOperand aggregate = lir_lower_expr(lower, expr->field.object);
            LirOperand offset = lir_operand_offset(expr->field.field->offset);
            LirOperand operands[2] = {aggregate, offset};
            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_EXTRACT, expr->type, (Array){.data = operands, .len = 2});
            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_INDEX: {
            HirExpr *object = expr->index.object;

            if (object->type->kind == HIR_TYPE_ARRAY && lir_expr_is_ssa(lower, object)) {
                size_t offset;

                if (lir_constant_array_offset(expr, &offset)) {
                    LirOperand aggregate = lir_lower_expr(lower, object);
                    LirOperand operands[2] = {aggregate, lir_operand_offset(offset)};
                    LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_EXTRACT, expr->type, (Array){.data = operands, .len = 2});
                    return lir_operand_value(result, expr->type);
                }

                LirOperand aggregate = lir_lower_expr(lower, object);
                LirOperand lir_offset = lir_lower_array_aggregate_offset(lower, expr);

                LirOperand extract_operands[2] = {aggregate, lir_offset};
                LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_EXTRACT_DYNAMIC, expr->type, (Array){.data = extract_operands, .len = 2});

                return lir_operand_value(result, expr->type);
            }

            LirPlace place = lir_lower_place(lower, expr);
            return lir_lower_load(lower, place);
        }

        case HIR_EXPR_INIT:
            return lir_lower_init(lower, expr);

        case HIR_EXPR_SUM_INJECT: {
            HirType *sum_type = lir_unwrap_type(expr->type);
            assert(sum_type != NULL && sum_type->kind == HIR_TYPE_SUM);
            assert(expr->sum_inject.member_index < sum_type->sum.members.len);

            HirType *member_type = lir_unwrap_type(
                ((HirType **)sum_type->sum.members.data)[expr->sum_inject.member_index]
            );

            LirOperand value = {0};
            if (expr->sum_inject.value != NULL)
                value = lir_lower_expr(lower, expr->sum_inject.value);
            else
                assert(member_type->size == 0);

            LirValueId zero = lir_emit(lower->function, lower->block, LIR_OP_ZERO, expr->type, (Array){0});
            LirOperand aggregate = lir_operand_value(zero, expr->type);

            HirType *tag_type = lir_sum_tag_type(lower);
            LirOperand tag = lir_operand_uint((uint64_t)expr->sum_inject.member_index, tag_type);
            LirOperand tag_operands[3] = {aggregate, lir_operand_offset(0), tag};

            LirValueId tagged = lir_emit(lower->function, lower->block, LIR_OP_INSERT, expr->type, (Array){.data = tag_operands, .len = 3});
            aggregate = lir_operand_value(tagged, expr->type);

            if (member_type->size != 0) {
                LirOperand payload_operands[3] = {aggregate, lir_operand_offset(lir_sum_payload_offset(sum_type)), value};
                LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT, expr->type, (Array){.data = payload_operands, .len = 3});
                aggregate = lir_operand_value(result, expr->type);
            }

            return aggregate;
        }

        case HIR_EXPR_VLA_ALLOC: {
            HirType *type = expr->type;
            HirType *element = expr->vla_alloc.element;
            assert(type != NULL && type->kind == HIR_TYPE_SLICE && type->slice.fields.len >= 2);
            HirField *length_field = &((HirField *)type->slice.fields.data)[0];
            HirField *pointer_field = &((HirField *)type->slice.fields.data)[1];
            LirOperand length = lir_lower_expr(lower, expr->vla_alloc.length);
            LirValueId cast_length = lir_emit(lower->function, lower->block, LIR_OP_CAST, length_field->type, (Array){.data = &length, .len = 1});
            length = lir_operand_value(cast_length, length_field->type);
            size_t alignment = element->align < 16 ? 16 : element->align;
            LirOperand allocation_operands[3] = {length, lir_operand_offset(element->size), lir_operand_offset(alignment)};
            LirValueId data = lir_emit(lower->function, lower->block, LIR_OP_STACK_ALLOC, pointer_field->type, (Array){.data = allocation_operands, .len = 3});
            LirValueId zero = lir_emit(lower->function, lower->block, LIR_OP_ZERO, type, (Array){0});
            LirOperand aggregate = lir_operand_value(zero, type);
            LirOperand length_operands[3] = {aggregate, lir_operand_offset(length_field->offset), length};
            LirValueId with_length = lir_emit(lower->function, lower->block, LIR_OP_INSERT, type, (Array){.data = length_operands, .len = 3});
            aggregate = lir_operand_value(with_length, type);
            LirOperand pointer = lir_operand_value(data, pointer_field->type);
            LirOperand pointer_operands[3] = {aggregate, lir_operand_offset(pointer_field->offset), pointer};
            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT, type, (Array){.data = pointer_operands, .len = 3});
            return lir_operand_value(result, type);
        }

        case HIR_EXPR_LAMBDA:
            assert(!"HIR expression not yet lowered");

        case HIR_EXPR_ERROR:
            assert(!"error expression reached LIR");
    }

    assert(!"unhandled HIR expression");
    return lir_operand_invalid();
}

static LirOperand lir_lower_logical_and(LirLower *lower, HirExpr *expr) {
    LirFunction *function = lower->function;

    LirOperand left = lir_lower_expr(lower, expr->binary.left);
    LirBlockId left_block = lower->block;

    LirBlockId rhs_block = lir_block_create(function);
    LirBlockId merge_block = lir_block_create(function);

    LirValueId result = lir_block_add_param(function, merge_block, expr->type);

    LirOperand false_value = lir_operand_bool(false, expr->type);

    lir_branch(function, left_block, left, rhs_block, (Array){0}, merge_block, (Array){.data = &false_value, .len = 1});

    lower->block = rhs_block;

    LirOperand right = lir_lower_expr(lower, expr->binary.right);
    LirBlockId right_block = lower->block;

    LirOperand true_value = lir_operand_bool(true, expr->type);

    lir_branch(function, right_block, right, merge_block, (Array){.data = &true_value, .len = 1}, merge_block, (Array){.data = &false_value, .len = 1});

    lower->block = merge_block;

    return lir_operand_value(result, expr->type);
}

static LirOperand lir_lower_logical_or(LirLower *lower, HirExpr *expr) {
    LirFunction *function = lower->function;

    LirOperand left = lir_lower_expr(lower, expr->binary.left);
    LirBlockId left_block = lower->block;

    LirBlockId rhs_block = lir_block_create(function);
    LirBlockId merge_block = lir_block_create(function);

    LirValueId result = lir_block_add_param(function, merge_block, expr->type);

    LirOperand true_value = lir_operand_bool(true, expr->type);

    lir_branch(function, left_block, left, merge_block, (Array){.data = &true_value, .len = 1}, rhs_block, (Array){0});

    lower->block = rhs_block;

    LirOperand right = lir_lower_expr(lower, expr->binary.right);
    LirBlockId right_block = lower->block;

    LirOperand false_value = lir_operand_bool(false, expr->type);

    lir_branch(function, right_block, right, merge_block, (Array){.data = &true_value, .len = 1}, merge_block, (Array){.data = &false_value, .len = 1});

    lower->block = merge_block;

    return lir_operand_value(result, expr->type);
}

static LirOperand lir_lower_load(LirLower *lower, LirPlace place) {
    LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_LOAD, place.type, (Array){.data = &place.address, .len = 1});

    return lir_operand_value(result, place.type);
}

static void lir_lower_store(LirLower *lower, LirPlace place, LirOperand value) {
    LirOperand operands[2] = {
        place.address,
        value,
    };

    lir_emit(lower->function, lower->block, LIR_OP_STORE, NULL, (Array){.data = operands, .len = 2});
}

static HirType *lir_address_type(LirLower *lower, HirType *pointee) {
    HirType *type = arena_calloc(lower->function->arena, sizeof(*type));
    *type = (HirType){
        .kind = HIR_TYPE_POINTER,
        .mutable = false,
        .size = lower->target->pointer.size,
        .align = lower->target->pointer.align,
        .pointer = {.pointee = pointee, .optional = false},
    };
    return type;
}

static LirPlace lir_lower_place(LirLower *lower, HirExpr *expr) {
    switch (expr->kind) {
        case HIR_EXPR_UNARY:
            if (expr->unary.op == AST_UNARY_DEREF) {
                LirOperand address = lir_lower_expr(lower, expr->unary.operand);

                return (LirPlace){
                    .address = address,
                    .type = expr->type,
                };
            }

            break;

        case HIR_EXPR_INDEX: {
            HirType *object_type = expr->index.object->type;
            LirOperand base;

            if (object_type->kind == HIR_TYPE_SLICE) {
                LirOperand aggregate = lir_lower_expr(lower, expr->index.object);
                assert(object_type->slice.fields.len >= 2);

                HirField *pointer_field = &((HirField *)object_type->slice.fields.data)[1];
                LirOperand operands[2] = {aggregate, lir_operand_offset(pointer_field->offset)};

                LirValueId data = lir_emit(lower->function, lower->block, LIR_OP_EXTRACT, pointer_field->type, (Array){.data = operands, .len = 2});
                base = lir_operand_value(data, pointer_field->type);
            } else if (object_type->kind == HIR_TYPE_POINTER) {
                base = lir_lower_expr(lower, expr->index.object);
            } else {
                LirPlace object = lir_lower_place(lower, expr->index.object);
                base = object.address;
            }

            LirOperand offset = lir_lower_array_offset(lower, expr);

            LirOperand add_operands[2] = {
                base,
                offset,
            };

            HirType *address_type = lir_address_type(lower, expr->type);
            LirValueId address = lir_emit(lower->function, lower->block, LIR_OP_ADDR_ADD, address_type, (Array){.data = add_operands, .len = 2});

            return (LirPlace){
                .address = lir_operand_value(address, address_type),
                .type = expr->type,
            };
        }

        case HIR_EXPR_FIELD: {
            HirType *object_type = expr->field.object->type;
            LirOperand base;

            if (object_type->kind == HIR_TYPE_POINTER) {
                base = lir_lower_expr(lower, expr->field.object);
            } else {
                LirPlace object = lir_lower_place(lower, expr->field.object);
                base = object.address;
            }

            HirType *address_type = lir_address_type(lower, expr->field.field->type);
            if (expr->field.field->offset == 0) {
                base.type = address_type;
                return (LirPlace){
                    .address = base,
                    .type = expr->field.field->type,
                };
            }

            LirOperand offset = lir_operand_offset(expr->field.field->offset);
            LirOperand operands[2] = {base, offset};

            LirValueId address = lir_emit(lower->function, lower->block, LIR_OP_ADDR_ADD, address_type, (Array){.data = operands, .len = 2});

            return (LirPlace){
                .address = lir_operand_value(address, address_type),
                .type = expr->field.field->type,
            };
        }

        case HIR_EXPR_VALUE: {
            Symbol *symbol = expr->value.symbol;

            if (symbol->kind == SYMBOL_GLOBAL) {
                HirType *pointer_type = arena_calloc(lower->function->arena, sizeof(*pointer_type));

                *pointer_type = (HirType) {
                    .kind = HIR_TYPE_POINTER,
                    .mutable = false,
                    .size = lower->target->pointer.size,
                    .align = lower->target->pointer.align,
                    .pointer = {
                        .pointee = expr->type,
                        .optional = false,
                    },
                };

                return (LirPlace) {
                    .address = lir_operand_symbol(symbol, pointer_type),
                    .type = expr->type,
                };
            }

            if (symbol->kind == SYMBOL_LOCAL || symbol->kind == SYMBOL_PARAMETER) {
                LirBinding *address = lir_find_address(lower, symbol);
                if (address != NULL) {
                    return (LirPlace){
                        .address = address->operand,
                        .type = expr->type,
                    };
                }
            }

            break;
        }
    }
    assert(!"expression is not an lvalue");
    return (LirPlace){0};
}

static bool lir_binding_contains(Array(LirBinding) *bindings, Symbol *symbol) {
    for (size_t i = 0; i < bindings->len; i++) {
        LirBinding *binding = &((LirBinding *)bindings->data)[i];

        if (binding->symbol == symbol) {
            return true;
        }
    }

    return false;
}

static LirBinding *lir_find_snapshot_binding(Array(LirBinding) *snapshot, Symbol *symbol) {
    for (size_t i = 0; i < snapshot->len; i++) {
        LirBinding *binding = &((LirBinding *)snapshot->data)[i];

        if (binding->symbol == symbol) {
            return binding;
        }
    }

    return NULL;
}

static void lir_snapshot_bindings(LirLower *lower, Array(LirBinding) *snapshot) {
    for (size_t i = 0; i < lower->bindings.len; i++) {
        LirBinding *binding = &((LirBinding *)lower->bindings.data)[i];
        LirBinding *existing = lir_find_snapshot_binding(snapshot, binding->symbol);

        if (existing) {
            existing->operand = binding->operand;
        } else {
            array_push(snapshot, binding);
        }
    }
}

static void lir_collect_branch_args(LirLower *lower, Array(LirBinding) *snapshot, Array(LirOperand) *args) {
    for (size_t i = 0; i < snapshot->len; i++) {
        LirBinding *binding = &((LirBinding *)snapshot->data)[i];
        LirBinding *current = lir_find_binding(lower, binding->symbol);

        assert(current);
        array_push(args, &current->operand);
    }
}

static void lir_snapshot_operands(Array(LirBinding) *snapshot, Array(LirOperand) *operands) {
    for (size_t i = 0; i < snapshot->len; i++) {
        LirBinding *binding = &((LirBinding *)snapshot->data)[i];

        array_push(operands, &binding->operand);
    }
}

static LirOperand lir_lower_cast_value(LirLower *lower, LirOperand value, HirType *type) {
    if (type == NULL || value.type == type)
        return value;

    /* Mutability does not affect LIR representation, but block arguments
     * still carry the exact type pointer chosen for the block parameter. */
    if (type_equal(value.type, type)) {
        value.type = type;
        return value;
    }

    LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CAST, type,
                                 (Array){.data = &value, .len = 1});
    return lir_operand_value(result, type);
}

static LirOperand lir_lower_compound_assignment_value(LirLower *lower, HirStmt *stmt,
                                                       LirOperand left, LirOperand right) {
    /* Preserve the type identity of the current SSA binding (e.g. a loop
     * block parameter), rather than the possibly differently-qualified HIR
     * place type. */
    HirType *target_type = left.type;
    if (stmt->assign.operation_type == NULL)
        return lir_lower_binary_values(lower, stmt->assign.op, target_type, left, right);

    left = lir_lower_cast_value(lower, left, stmt->assign.operation_type);
    right = lir_lower_cast_value(lower, right, stmt->assign.right_type);

    LirOperand result = lir_lower_binary_values(lower, stmt->assign.op,
                                                stmt->assign.operation_type, left, right);
    return lir_lower_cast_value(lower, result, target_type);
}

static void lir_lower_stmt(LirLower *lower, HirStmt *stmt);

static void lir_lower_if(LirLower *lower, HirStmt *stmt) {
    LirFunction *function = lower->function;
    LirOperand condition = lir_lower_expr(lower, stmt->_if.cond);
    /* Logical conditions may create blocks; branch from their final block. */
    LirBlockId entry_block = lower->block;

    Array(LirBinding) snapshot = array_create(function->arena, sizeof(LirBinding));

    lir_snapshot_bindings(lower, &snapshot);

    LirBlockId then = lir_block_create(function);
    LirBlockId _else = stmt->_if._else ? lir_block_create(function) : LIR_INVALID_BLOCK;
    LirBlockId merge_block = LIR_INVALID_BLOCK;

    size_t bindings_len = lower->bindings.len;

    lower->block = then;
    lir_lower_stmt(lower, stmt->_if.then);
    bool then_falls_through = !lir_lower_block_terminated(lower);

    Array(LirOperand) then_args = array_create(function->arena, sizeof(LirOperand));

    if (then_falls_through) {
        lir_collect_branch_args(lower, &snapshot, &then_args);
    }

    lower->bindings.len = bindings_len;

    bool else_falls_through = false;
    Array(LirOperand) else_args = array_create(function->arena, sizeof(LirOperand));

    if (_else != LIR_INVALID_BLOCK) {
        lower->block = _else;
        lir_lower_stmt(lower, stmt->_if._else);
        else_falls_through = !lir_lower_block_terminated(lower);

        if (else_falls_through) {
            lir_collect_branch_args(lower, &snapshot, &else_args);
        }

        lower->bindings.len = bindings_len;
    } else {
        else_falls_through = true;
    }

    if (!then_falls_through && !else_falls_through) {
        lir_branch(function, entry_block, condition, then, (Array){0}, _else, (Array){0});
        lower->block = entry_block;
        return;
    }

    merge_block = lir_block_create(function);

    for (size_t i = 0; i < snapshot.len; i++) {
        LirBinding *binding = &((LirBinding *)snapshot.data)[i];

        lir_block_add_param(function, merge_block, binding->operand.type);
    }

    if (then_falls_through) {
        lower->block = then;
        lir_jump(function, then, merge_block, then_args);
    }

    if (_else != LIR_INVALID_BLOCK) {
        if (else_falls_through) {
            lower->block = _else;
            lir_jump(function, _else, merge_block, else_args);
        }
    }

    Array(LirOperand) entry_else_args = array_create(function->arena, sizeof(LirOperand));

    if (_else == LIR_INVALID_BLOCK) {
        lir_snapshot_operands(&snapshot, &entry_else_args);
        lir_branch(function, entry_block, condition, then, (Array){0}, merge_block, entry_else_args);
    } else {
        lir_branch(function, entry_block, condition, then, (Array){0}, _else, (Array){0});
    }

    lower->block = merge_block;
    lower->bindings.len = bindings_len;

    for (size_t i = 0; i < snapshot.len; i++) {
        LirBinding *binding = &((LirBinding *)snapshot.data)[i];
        LirBlockParam *param = &((LirBlockParam *)lir_lower_block(lower)->params.data)[i];

        lir_bind(lower, binding->symbol, lir_operand_value(param->value, param->type));
    }
}

static void lir_add_snapshot_params(LirFunction *function, LirBlockId block, Array(LirBinding) *snapshot) {
    for (size_t i = 0; i < snapshot->len; i++) {
        LirBinding *binding = &((LirBinding *)snapshot->data)[i];

        lir_block_add_param(function, block, binding->operand.type);
    }
}

static void lir_bind_snapshot_params(LirLower *lower, LirBlockId block, Array(LirBinding) *snapshot) {
    LirBlock *target = ((LirBlock **)lower->function->blocks.data)[block];

    for (size_t i = 0; i < snapshot->len; i++) {
        LirBinding *binding = &((LirBinding *)snapshot->data)[i];
        LirBlockParam *param = &((LirBlockParam *)target->params.data)[i];

        lir_bind(lower, binding->symbol, lir_operand_value(param->value, param->type));
    }
}

static void lir_lower_break(LirLower *lower) {
    assert(lower->loops.len != 0);

    LirLoop *loop = &((LirLoop *)lower->loops.data)[lower->loops.len - 1];
    if (lower->stack_saves.len > loop->stack_save_depth) {
        LirOperand saved_stack = ((LirOperand *)lower->stack_saves.data)[loop->stack_save_depth];
        lir_emit(lower->function, lower->block, LIR_OP_STACK_RESTORE, NULL, (Array){.data = &saved_stack, .len = 1});
    }
    Array(LirOperand) args = array_create(lower->function->arena, sizeof(LirOperand));

    lir_collect_branch_args(lower, &loop->bindings, &args);
    lir_jump(lower->function, lower->block, loop->exit, args);
}

static void lir_lower_continue(LirLower *lower) {
    assert(lower->loops.len != 0);

    LirLoop *loop = &((LirLoop *)lower->loops.data)[lower->loops.len - 1];
    if (lower->stack_saves.len > loop->stack_save_depth) {
        LirOperand saved_stack = ((LirOperand *)lower->stack_saves.data)[loop->stack_save_depth];
        lir_emit(lower->function, lower->block, LIR_OP_STACK_RESTORE, NULL, (Array){.data = &saved_stack, .len = 1});
    }
    Array(LirOperand) args = array_create(lower->function->arena, sizeof(LirOperand));

    lir_collect_branch_args(lower, &loop->bindings, &args);
    lir_jump(lower->function, lower->block, loop->header, args);
}

static void lir_lower_while(LirLower *lower, HirStmt *stmt) {
    LirFunction *function = lower->function;
    LirBlockId entry_block = lower->block;
    size_t bindings_len = lower->bindings.len;

    Array(LirBinding) snapshot = array_create(function->arena, sizeof(LirBinding));
    lir_snapshot_bindings(lower, &snapshot);

    LirBlockId header = lir_block_create(function);
    LirBlockId body = lir_block_create(function);
    LirBlockId exit = lir_block_create(function);

    lir_add_snapshot_params(function, header, &snapshot);
    lir_add_snapshot_params(function, exit, &snapshot);

    Array(LirOperand) entry_args = array_create(function->arena, sizeof(LirOperand));
    lir_snapshot_operands(&snapshot, &entry_args);

    lir_jump(function, entry_block, header, entry_args);

    lower->block = header;
    lower->bindings.len = bindings_len;
    lir_bind_snapshot_params(lower, header, &snapshot);

    LirOperand condition = lir_lower_expr(lower, stmt->_while.cond);
    LirBlockId condition_block = lower->block;

    Array(LirOperand) exit_args = array_create(function->arena, sizeof(LirOperand));
    lir_collect_branch_args(lower, &snapshot, &exit_args);

    lir_branch(function, header, condition, body, (Array){0}, exit, exit_args);

    LirLoop loop = {
        .header = header,
        .exit = exit,
        .bindings = snapshot,
        .stack_save_depth = lower->stack_saves.len,
    };

    array_push(&lower->loops, &loop);

    lower->block = body;
    lower->bindings.len = bindings_len;
    lir_bind_snapshot_params(lower, header, &snapshot);
    lir_lower_stmt(lower, stmt->_while.body);

    bool falls_through = !lir_lower_block_terminated(lower);

    if (falls_through) {
        LirBlockId body_end = lower->block;
        Array(LirOperand) backedge_args = array_create(function->arena, sizeof(LirOperand));

        lir_collect_branch_args(lower, &snapshot, &backedge_args);
        lir_jump(function, body_end, header, backedge_args);
    }

    lower->loops.len--;

    lower->block = exit;
    lower->bindings.len = bindings_len;
    lir_bind_snapshot_params(lower, exit, &snapshot);
}

static HirExpr *lir_field_root(HirExpr *expr) {
    while (expr->kind == HIR_EXPR_FIELD)
        expr = expr->field.object;

    return expr;
}

static bool lir_expr_is_ssa(LirLower *lower, HirExpr *expr) {
    switch (expr->kind) {
        case HIR_EXPR_LITERAL:
            return expr->literal.kind == HIR_LITERAL_STRING;

        case HIR_EXPR_VALUE:
            return (expr->value.symbol->kind == SYMBOL_LOCAL || expr->value.symbol->kind == SYMBOL_PARAMETER) &&
                   lir_find_address(lower, expr->value.symbol) == NULL;

        case HIR_EXPR_FIELD:
            return lir_expr_is_ssa(lower, expr->field.object);

        case HIR_EXPR_INDEX:
            return expr->index.object->type->kind == HIR_TYPE_ARRAY && lir_expr_is_ssa(lower, expr->index.object);

        default:
            return false;
    }
}

static LirOperand lir_lower_array_offset(LirLower *lower, HirExpr *expr) {
    size_t constant_offset;

    if (lir_constant_array_offset(expr, &constant_offset))
        return lir_operand_offset(constant_offset);

    HirExpr *object = expr->index.object;
    if (object->type->kind == HIR_TYPE_ARRAY &&
        !(object->kind == HIR_EXPR_UNARY && object->unary.op == AST_UNARY_DEREF))
        return lir_lower_array_aggregate_offset(lower, expr);

    HirExpr *index = expr->index.index;
    LirOperand index_operand = lir_lower_expr(lower, index);

    return lir_lower_scaled_index(lower, index_operand, expr->type->size);
}

static LirOperand lir_lower_array_aggregate_offset(LirLower *lower, HirExpr *expr) {
    size_t constant_offset;

    if (lir_constant_array_offset(expr, &constant_offset))
        return lir_operand_offset(constant_offset);

    HirType *array_type = expr->index.object->type;
    assert(array_type->kind == HIR_TYPE_ARRAY);

    LirOperand index = lir_lower_expr(lower, expr->index.index);
    LirOperand offset = lir_lower_scaled_index(lower, index, expr->type->size);
    size_t data_offset = array_data_offset(array_type);

    LirOperand base_offset = hir_type_is_signed_integer(index.type)
        ? lir_operand_int((int64_t)data_offset, index.type)
        : lir_operand_uint((uint64_t)data_offset, index.type);
    LirOperand operands[2] = {offset, base_offset};

    LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_ADD, index.type, (Array){.data = operands, .len = 2});
    return lir_operand_value(result, index.type);
}

static LirOperand lir_lower_aggregate_store(LirLower *lower, HirExpr *target, LirOperand value) {
    HirExpr *object;
    LirOperand aggregate;
    LirOperand offset;
    LirOperand updated;

    switch (target->kind) {
        case HIR_EXPR_FIELD: {
            object = target->field.object;
            aggregate = lir_lower_expr(lower, object);
            offset = lir_operand_offset(target->field.field->offset);

            LirOperand operands[3] = {aggregate, offset, value};

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT, aggregate.type, (Array){.data = operands, .len = 3});

            updated = lir_operand_value(result, aggregate.type);
            break;
        }

        case HIR_EXPR_INDEX: {
            object = target->index.object;
            aggregate = lir_lower_expr(lower, object);
            offset = lir_lower_array_aggregate_offset(lower, target);

            bool constant = offset.kind == LIR_OPERAND_OFFSET;

            if (constant) {
                LirOperand operands[3] = {aggregate, offset, value};

                LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT, aggregate.type, (Array){.data = operands, .len = 3});

                updated = lir_operand_value(result, aggregate.type);
            } else {
                LirOperand operands[3] = {aggregate, offset, value};

                LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_INSERT_DYNAMIC, aggregate.type, (Array){.data = operands, .len = 3});

                updated = lir_operand_value(result, aggregate.type);
            }

            break;
        }

        default:
            assert(!"aggregate store target is not an aggregate expression");
    }

    if (object->kind == HIR_EXPR_VALUE) {
        Symbol *symbol = object->value.symbol;

        assert(symbol->kind == SYMBOL_LOCAL || symbol->kind == SYMBOL_PARAMETER);

        lir_bind(lower, symbol, updated);
        return updated;
    }

    if (object->kind == HIR_EXPR_FIELD || object->kind == HIR_EXPR_INDEX)
        return lir_lower_aggregate_store(lower, object, updated);

    assert(!"aggregate field/index does not terminate in an SSA value");
    return lir_operand_invalid();
}

static bool lir_stmt_contains_vla_alloc(HirStmt *stmt) {
    if (stmt == NULL)
        return false;

    switch (stmt->kind) {
        case HIR_STMT_BLOCK:
            for (size_t i = 0; i < stmt->block.stmts.len; i++)
                if (lir_stmt_contains_vla_alloc(((HirStmt **)stmt->block.stmts.data)[i])) return true;
            return false;
        case HIR_STMT_ASSIGN:
            return stmt->assign.value != NULL && stmt->assign.value->kind == HIR_EXPR_VLA_ALLOC;
        case HIR_STMT_IF:
            return lir_stmt_contains_vla_alloc(stmt->_if.then) || lir_stmt_contains_vla_alloc(stmt->_if._else);
        case HIR_STMT_WHILE:
            return lir_stmt_contains_vla_alloc(stmt->_while.body);
        case HIR_STMT_EXPR:
        case HIR_STMT_RETURN:
        case HIR_STMT_BREAK:
        case HIR_STMT_CONTINUE:
        case HIR_STMT_ERROR:
            return false;
    }

    return false;
}

static void lir_lower_stmt(LirLower *lower, HirStmt *stmt) {
    if (lir_lower_block_terminated(lower)) {
        return;
    }

    switch (stmt->kind) {
        case HIR_STMT_BLOCK: {
            size_t stack_save_depth = lower->stack_saves.len;
            bool has_vla = lir_stmt_contains_vla_alloc(stmt);
            LirOperand saved_stack = lir_operand_invalid();
            if (has_vla) {
                HirType *stack_pointer_type = lir_address_type(lower, lower->function->return_type);
                LirValueId stack_pointer = lir_emit(lower->function, lower->block, LIR_OP_STACK_SAVE, stack_pointer_type, (Array){0});
                saved_stack = lir_operand_value(stack_pointer, stack_pointer_type);
                array_push(&lower->stack_saves, &saved_stack);
            }
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                HirStmt *child = ((HirStmt **)stmt->block.stmts.data)[i];

                if (child == NULL) {
                    fprintf(stderr, "internal compiler error: null HIR statement in block at index %zu of %zu (span %zu:%zu)\n",
                            i, stmt->block.stmts.len, source_line(stmt->span.source, stmt->span.offset), source_column(stmt->span.source, stmt->span.offset));
                    abort();
                }

                lir_lower_stmt(lower, child);
                if (lir_lower_block_terminated(lower))
                    break;
            }
            if (has_vla && !lir_lower_block_terminated(lower))
                lir_emit(lower->function, lower->block, LIR_OP_STACK_RESTORE, NULL, (Array){.data = &saved_stack, .len = 1});
            lower->stack_saves.len = stack_save_depth;
            return;
        }

        case HIR_STMT_ASSIGN: {
            HirExpr *target = stmt->assign.target;

            if (stmt->assign.op == AST_BINARY_ASSIGN) {
                LirOperand value = lir_lower_expr(lower, stmt->assign.value);

                if (target->kind == HIR_EXPR_VALUE) {
                    Symbol *symbol = target->value.symbol;

                    if (symbol->kind == SYMBOL_LOCAL || symbol->kind == SYMBOL_PARAMETER) {
                        LirBinding *address = lir_find_address(lower, symbol);
                        if (address != NULL) {
                            lir_lower_store(lower, (LirPlace){.address = address->operand, .type = target->type}, value);
                        } else {
                            /* Keep an SSA variable's type identity stable across
                             * assignments. sema_coerce may leave a structurally
                             * equal value with a different HirType pointer (for
                             * example, a mutable parameter assigned an immutable
                             * global of the same underlying type). */
                            LirBinding *binding = lir_find_binding(lower, symbol);
                            HirType *type = binding != NULL ? binding->operand.type : target->type;
                            value = lir_lower_cast_value(lower, value, type);
                            lir_bind(lower, symbol, value);
                        }
                        return;
                    }

                    if (symbol->kind == SYMBOL_GLOBAL) {
                        LirPlace place = lir_lower_place(lower, target);
                        lir_lower_store(lower, place, value);
                        return;
                    }

                    assert(!"unexpected assignment target symbol");
                }

                if ((target->kind == HIR_EXPR_FIELD || target->kind == HIR_EXPR_INDEX) && lir_expr_is_ssa(lower, target)) {
                    lir_lower_aggregate_store(lower, target, value);
                    return;
                }

                LirPlace place = lir_lower_place(lower, target);
                lir_lower_store(lower, place, value);
                return;
            }

            if (target->kind == HIR_EXPR_VALUE) {
                Symbol *symbol = target->value.symbol;
                if ((symbol->kind == SYMBOL_LOCAL || symbol->kind == SYMBOL_PARAMETER) && lir_find_address(lower, symbol) == NULL) {
                    LirOperand left = lir_lower_expr(lower, target);
                    LirOperand right = lir_lower_expr(lower, stmt->assign.value);
                    LirOperand value = lir_lower_compound_assignment_value(lower, stmt, left, right);
                    lir_bind(lower, symbol, value);
                    return;
                }
            }

            LirPlace place = lir_lower_place(lower, target);
            LirOperand left = lir_lower_load(lower, place);
            LirOperand right = lir_lower_expr(lower, stmt->assign.value);
            LirOperand value = lir_lower_compound_assignment_value(lower, stmt, left, right);
            lir_lower_store(lower, place, value);
            return;
        }
        case HIR_STMT_EXPR:
            (void)lir_lower_expr(lower, stmt->expr);
            if (hir_expr_is_noreturn_call(stmt->expr))
                lir_set_terminator(lower->function, lower->block, (LirTerminator){.kind = LIR_TERM_UNREACHABLE});
            return;

        case HIR_STMT_RETURN:
            if (stmt->_return.value) {
                LirOperand value = lir_lower_expr(lower, stmt->_return.value);
                lir_return(lower->function, lower->block, &value);
            } else {
                lir_return(lower->function, lower->block, NULL);
            }
            return;

        case HIR_STMT_IF:
            lir_lower_if(lower, stmt);
            return;
        
        case HIR_STMT_WHILE:
            lir_lower_while(lower, stmt);
            return;

        case HIR_STMT_BREAK:
            lir_lower_break(lower);
            return;

        case HIR_STMT_CONTINUE:
            lir_lower_continue(lower);
            return;

        case HIR_STMT_UNREACHABLE:
            lir_set_terminator(lower->function, lower->block, (LirTerminator){.kind = LIR_TERM_UNREACHABLE});
            return;

        case HIR_STMT_ERROR:
            assert(!"error statement reached LIR");
    }

    assert(!"unhandled HIR statement");
}

static Symbol *lir_addressed_local_symbol(HirExpr *expr) {
    if (expr == NULL)
        return NULL;

    switch (expr->kind) {
        case HIR_EXPR_VALUE:
            if (expr->value.symbol->kind == SYMBOL_LOCAL || expr->value.symbol->kind == SYMBOL_PARAMETER)
                return expr->value.symbol;
            return NULL;

        case HIR_EXPR_FIELD:
            if (expr->field.object->type != NULL && expr->field.object->type->kind == HIR_TYPE_POINTER)
                return NULL;
            return lir_addressed_local_symbol(expr->field.object);

        case HIR_EXPR_INDEX:
            if (expr->index.object->type == NULL || expr->index.object->type->kind == HIR_TYPE_POINTER ||
                expr->index.object->type->kind == HIR_TYPE_SLICE)
                return NULL;
            return lir_addressed_local_symbol(expr->index.object);

        default:
            return NULL;
    }
}

static void lir_collect_address_taken_expr(Array(Symbol *) *symbols, HirExpr *expr);
static void lir_collect_address_taken_stmt(Array(Symbol *) *symbols, HirStmt *stmt);

static void lir_collect_address_taken_expr(Array(Symbol *) *symbols, HirExpr *expr) {
    if (expr == NULL)
        return;

    switch (expr->kind) {
        case HIR_EXPR_UNARY:
            if (expr->unary.op == AST_UNARY_ADDRESS) {
                Symbol *symbol = lir_addressed_local_symbol(expr->unary.operand);
                bool found = false;
                for (size_t i = 0; symbol != NULL && i < symbols->len; i++) {
                    if (lir_same_symbol(((Symbol **)symbols->data)[i], symbol)) {
                        found = true;
                        break;
                    }
                }
                if (symbol != NULL && !found)
                    array_push(symbols, &symbol);
            }
            lir_collect_address_taken_expr(symbols, expr->unary.operand);
            return;

        case HIR_EXPR_BINARY:
            lir_collect_address_taken_expr(symbols, expr->binary.left);
            lir_collect_address_taken_expr(symbols, expr->binary.right);
            return;

        case HIR_EXPR_ANY_BOX:
            lir_collect_address_taken_expr(symbols, expr->any_box.address);
            return;

        case HIR_EXPR_ANY_CALL:
            lir_collect_address_taken_expr(symbols, expr->any_call.object);
            for (size_t i = 0; i < expr->any_call.args.len; i++)
                lir_collect_address_taken_expr(symbols, ((HirExpr **)expr->any_call.args.data)[i]);
            return;

        case HIR_EXPR_CALL:
            if (expr->call.callee != NULL)
                lir_collect_address_taken_expr(symbols, expr->call.callee);
            for (size_t i = 0; i < expr->call.args.len; i++)
                lir_collect_address_taken_expr(symbols, ((HirExpr **)expr->call.args.data)[i]);
            return;

        case HIR_EXPR_INDEX:
            lir_collect_address_taken_expr(symbols, expr->index.object);
            lir_collect_address_taken_expr(symbols, expr->index.index);
            return;

        case HIR_EXPR_FIELD:
            lir_collect_address_taken_expr(symbols, expr->field.object);
            return;

        case HIR_EXPR_CAST:
            lir_collect_address_taken_expr(symbols, expr->cast.operand);
            return;

        case HIR_EXPR_INIT:
            for (size_t i = 0; i < expr->init.fields.len; i++)
                lir_collect_address_taken_expr(symbols, ((HirInitField *)expr->init.fields.data)[i].value);
            return;

        case HIR_EXPR_VLA_ALLOC:
            lir_collect_address_taken_expr(symbols, expr->vla_alloc.length);
            return;

        case HIR_EXPR_LAMBDA:
            lir_collect_address_taken_stmt(symbols, expr->lambda.body);
            return;

        case HIR_EXPR_SUM_INJECT:
            lir_collect_address_taken_expr(symbols, expr->sum_inject.value);
            return;

        case HIR_EXPR_ERROR:
        case HIR_EXPR_LITERAL:
        case HIR_EXPR_VALUE:
            return;
    }
}

static void lir_collect_address_taken_stmt(Array(Symbol *) *symbols, HirStmt *stmt) {
    if (stmt == NULL)
        return;

    switch (stmt->kind) {
        case HIR_STMT_BLOCK:
            for (size_t i = 0; i < stmt->block.stmts.len; i++)
                lir_collect_address_taken_stmt(symbols, ((HirStmt **)stmt->block.stmts.data)[i]);
            return;

        case HIR_STMT_EXPR:
            lir_collect_address_taken_expr(symbols, stmt->expr);
            return;

        case HIR_STMT_ASSIGN: {
            HirExpr *target = stmt->assign.target;
            if (stmt->assign.op != AST_BINARY_ASSIGN &&
                (target->kind == HIR_EXPR_FIELD || target->kind == HIR_EXPR_INDEX)) {
                Symbol *symbol = lir_addressed_local_symbol(target);
                bool found = false;
                for (size_t i = 0; symbol != NULL && i < symbols->len; i++) {
                    if (lir_same_symbol(((Symbol **)symbols->data)[i], symbol)) {
                        found = true;
                        break;
                    }
                }
                if (symbol != NULL && !found)
                    array_push(symbols, &symbol);
            }
            lir_collect_address_taken_expr(symbols, target);
            lir_collect_address_taken_expr(symbols, stmt->assign.value);
            return;
        }

        case HIR_STMT_RETURN:
            lir_collect_address_taken_expr(symbols, stmt->_return.value);
            return;

        case HIR_STMT_IF:
            lir_collect_address_taken_expr(symbols, stmt->_if.cond);
            lir_collect_address_taken_stmt(symbols, stmt->_if.then);
            lir_collect_address_taken_stmt(symbols, stmt->_if._else);
            return;

        case HIR_STMT_WHILE:
            lir_collect_address_taken_expr(symbols, stmt->_while.cond);
            lir_collect_address_taken_stmt(symbols, stmt->_while.body);
            return;

        case HIR_STMT_BREAK:
        case HIR_STMT_CONTINUE:
        case HIR_STMT_ERROR:
            return;
    }
}

static HirType *lir_symbol_type_in_function(HirFunction *hir, Symbol *symbol) {
    for (size_t i = 0; i < hir->params.len; i++) {
        HirParam *param = &((HirParam *)hir->params.data)[i];
        if (lir_same_symbol(param->symbol, symbol))
            return param->type;
    }

    for (size_t i = 0; i < hir->locals.len; i++) {
        HirLocal *local = &((HirLocal *)hir->locals.data)[i];
        if (lir_same_symbol(local->symbol, symbol))
            return local->type;
    }

    return NULL;
}

static LirOperand lir_allocate_address_storage(LirLower *lower, Symbol *symbol, HirType *type) {
    LirValueId storage_id = lir_emit(lower->function, lower->block, LIR_OP_ZERO, type, (Array){0});
    LirOperand storage = lir_operand_value(storage_id, type);

    HirType *pointer_type = arena_calloc(lower->function->arena, sizeof(*pointer_type));
    *pointer_type = (HirType){
        .kind = HIR_TYPE_POINTER,
        .mutable = false,
        .size = lower->target->pointer.size,
        .align = lower->target->pointer.align,
        .pointer = {.pointee = type, .optional = false},
    };

    LirValueId address_id = lir_emit(lower->function, lower->block, LIR_OP_ADDR, pointer_type, (Array){.data = &storage, .len = 1});
    LirOperand address = lir_operand_value(address_id, pointer_type);
    lir_bind_address(lower, symbol, address);
    return address;
}

static void lir_lower_function(LirModule *module, HirFunction *hir, const TargetInfo *target) {
    if (hir->is_comptime)
        return;

    LirFunction *function = lir_function_create(module, hir->symbol, hir->return_type, hir->is_extern, hir->is_export);

    Array(Symbol *) address_taken = array_create(module->arena, sizeof(Symbol *));
    lir_collect_address_taken_stmt(&address_taken, hir->body);

    LirLower lower = {
        .module = module,
        .function = function,
        .bindings = array_create(module->arena, sizeof(LirBinding)),
        .addresses = array_create(module->arena, sizeof(LirBinding)),
        .address_taken = address_taken,
        .stack_saves = array_create(module->arena, sizeof(LirOperand)),
        .loops = array_create(module->arena, sizeof(LirLoop)),
        .target = target,
    };

    lower.block = lir_block_create(function);

    for (size_t i = 0; i < hir->params.len; i++) {
        HirParam *param = &((HirParam *)hir->params.data)[i];
        LirValueId value = lir_function_add_param(function, param->symbol, param->type);
        if (!lir_is_address_taken(&lower, param->symbol))
            lir_bind(&lower, param->symbol, lir_operand_value(value, param->type));
    }

    for (size_t i = 0; i < lower.address_taken.len; i++) {
        Symbol *symbol = ((Symbol **)lower.address_taken.data)[i];
        HirType *type = lir_symbol_type_in_function(hir, symbol);

        assert(type != NULL);

        LirOperand address = lir_allocate_address_storage(&lower, symbol, type);

        if (symbol->kind == SYMBOL_PARAMETER) {
            LirFunctionParam *parameter = NULL;
            for (size_t j = 0; j < function->params.len; j++) {
                LirFunctionParam *candidate = &((LirFunctionParam *)function->params.data)[j];
                if (lir_same_symbol(candidate->symbol, symbol)) {
                    parameter = candidate;
                    break;
                }
            }
            assert(parameter != NULL);
            lir_lower_store(&lower, (LirPlace){.address = address, .type = type}, lir_operand_value(parameter->value, parameter->type));
        }
    }

    if (hir->body)
        lir_lower_stmt(&lower, hir->body);
}

static void lir_collect_static_data(LirModule *module, Array(LirData) *data, HirExpr *expr, size_t base_offset) {
    if (expr == NULL)
        return;

    switch (expr->kind) {
        case HIR_EXPR_LITERAL: {
            LirData value = {
                .offset = base_offset,
                .type = expr->type,
            };

            switch (expr->literal.kind) {
                case HIR_LITERAL_INTEGER:
                    if (expr->literal.integer == 0)
                        return;

                    value.kind = LIR_DATA_INTEGER;
                    value.integer = expr->literal.integer;
                    break;

                case HIR_LITERAL_FLOAT:
                    if (expr->literal.floating == 0.0 && !signbit(expr->literal.floating))
                        return;

                    value.kind = LIR_DATA_FLOAT;
                    value.floating = expr->literal.floating;
                    break;

                case HIR_LITERAL_BOOL:
                    if (!expr->literal.boolean)
                        return;

                    value.kind = LIR_DATA_BOOL;
                    value.boolean = true;
                    break;

                case HIR_LITERAL_NULL:
                    return;

                case HIR_LITERAL_STRING: {
                    HirType *type = expr->type;
                    assert(type != NULL && type->kind == HIR_TYPE_SLICE && type->slice.fields.len == 2);

                    HirField *length_field = &((HirField *)type->slice.fields.data)[0];
                    HirField *pointer_field = &((HirField *)type->slice.fields.data)[1];
                    Symbol *symbol = lir_add_string(module, expr->literal.string);

                    if (expr->literal.string.length != 0) {
                        LirData length = {
                            .offset = base_offset + length_field->offset,
                            .type = length_field->type,
                            .kind = LIR_DATA_INTEGER,
                            .integer = (uint64_t)expr->literal.string.length,
                        };
                        array_push(data, &length);
                    }

                    LirData address = {
                        .offset = base_offset + pointer_field->offset,
                        .type = pointer_field->type,
                        .kind = LIR_DATA_ADDRESS,
                        .symbol = symbol,
                    };
                    array_push(data, &address);
                    return;
                }

                case HIR_LITERAL_ERROR:
                    assert(!"error literal reached LIR");
            }

            array_push(data, &value);
            return;
        }

        case HIR_EXPR_INIT:
            for (size_t i = 0; i < expr->init.fields.len; i++) {
                HirInitField *field = &((HirInitField *)expr->init.fields.data)[i];

                lir_collect_static_data(module, data, field->value, base_offset + field->offset);
            }

            return;

        case HIR_EXPR_CAST:
            assert(!"static casts should be evaluated before LIR");

        default:
            assert(!"non-static expression reached global data lowering");
    }
}

static bool hir_symbol_belongs_to_module(const HirModule *hir, const Symbol *symbol) {
    return symbol == NULL || symbol->owner_module == NULL || symbol->owner_module == hir->owner_module;
}

static void lir_lower_global(LirModule *module, HirModule *hir, HirGlobal *global) {
    if (global->symbol != NULL && global->symbol->owner_module != NULL && global->symbol->owner_module != hir->owner_module)
        return;

    if (global->is_extern)
        return;

    LirGlobal lowered = {
        .symbol = global->symbol,
        .type = global->type,
        .data = array_create(module->arena, sizeof(LirData)),
        .is_mutable = global->type->mutable,
        .is_export = global->is_export,
    };

    lir_collect_static_data(module, &lowered.data, global->init, 0);
    array_push(&module->globals, &lowered);
}

LirModule *lir_lower_module(Arena *arena, HirModule *hir, const TargetInfo *target) {
    LirModule *module = lir_module_create(arena);

    for (size_t i = 0; i < hir->globals.len; i++) {
        HirGlobal *global = &((HirGlobal *)hir->globals.data)[i];
        if (hir_symbol_belongs_to_module(hir, global->symbol) && !global->is_extern)
            lir_lower_global(module, hir, global);
    }

    for (size_t i = 0; i < hir->functions.len; i++) {
        HirFunction *function = &((HirFunction *)hir->functions.data)[i];
        if (!hir_symbol_belongs_to_module(hir, function->symbol))
            continue;
        lir_lower_function(module, function, target);
    }

    return module;
}