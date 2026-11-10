#include <assert.h>
#include <math.h>
#include "common.h"

static bool comp_same_symbol(Symbol *a, Symbol *b) {
    if (a == b)
        return true;

    if (a == NULL || b == NULL)
        return false;

    if (a->kind != b->kind)
        return false;

    if (a->name.kind != AST_NAME_IDENT || b->name.kind != AST_NAME_IDENT)
        return false;

    return string_eq(a->name.ident, b->name.ident);
}

static HirExpr *comp_lookup(CompContext *context, Symbol *symbol) {
    if (context->frame == NULL)
        return NULL;

    for (size_t i = context->frame->bindings.len; i > 0; i--) {
        CompBinding *binding = &((CompBinding *)context->frame->bindings.data)[i - 1];

        if (comp_same_symbol(binding->symbol, symbol))
            return binding->value;
    }

    return NULL;
}

static bool store(CompContext *context, Symbol *symbol, HirExpr *value) {
    if (context->frame == NULL)
        return false;

    for (size_t i = context->frame->bindings.len; i > 0; i--) {
        CompBinding *binding = &((CompBinding *)context->frame->bindings.data)[i - 1];

        if (!comp_same_symbol(binding->symbol, symbol))
            continue;

        binding->value = value;
        return true;
    }

    return false;
}

static HirExpr *literal(Sema *sema, HirExpr *expr, HirLiteral literal, HirType *type) {
    HirExpr *result = arena_calloc(sema->arena, sizeof(*result));
    *result = *expr;

    result->kind = HIR_EXPR_LITERAL;
    result->type = type;
    result->literal = literal;

    return result;
}

static unsigned integer_bits(HirType *type) {
    return (unsigned)(type->size * 8);
}

static uint64_t integer_mask(HirType *type) {
    unsigned bits = integer_bits(type);

    if (bits >= 64)
        return UINT64_MAX;

    return (UINT64_C(1) << bits) - 1;
}

static uint64_t integer_wrap(HirType *type, uint64_t value) {
    return value & integer_mask(type);
}

static int64_t integer_signed(HirType *type, uint64_t value) {
    unsigned bits = integer_bits(type);
    uint64_t mask = integer_mask(type);

    value &= mask;

    if (bits == 64)
        return (int64_t)value;

    uint64_t sign = UINT64_C(1) << (bits - 1);

    if (value & sign)
        value |= ~mask;

    return (int64_t)value;
}

static HirType *comp_unwrap_type(HirType *type) {
    while (type != NULL && type->base != NULL)
        type = type->base;
    return type;
}

static bool comp_type_is_float(HirType *type) {
    type = comp_unwrap_type(type);
    return type != NULL && type->kind == HIR_TYPE_BUILTIN &&
           (type->builtin == BUILTIN_FLOAT32 || type->builtin == BUILTIN_FLOAT64);
}

static double comp_round_float(HirType *type, double value) {
    type = comp_unwrap_type(type);
    return type != NULL && type->size == sizeof(float) ? (double)(float)value : value;
}

static bool comp_literal_float_value(HirExpr *expr, double *value) {
    if (expr == NULL || expr->kind != HIR_EXPR_LITERAL || !comp_type_is_float(expr->type))
        return false;

    if (expr->literal.kind == HIR_LITERAL_FLOAT) {
        *value = comp_round_float(expr->type, expr->literal.floating);
        return true;
    }

    if (expr->literal.kind == HIR_LITERAL_INTEGER) {
        *value = comp_round_float(expr->type, (double)expr->literal.integer);
        return true;
    }

    return false;
}

static bool comp_eval_float_binary(AstBinaryOp op, HirType *type, double left, double right, HirLiteral *result) {
    type = comp_unwrap_type(type);
    if (type == NULL || (type->size != sizeof(float) && type->size != sizeof(double)))
        return false;

    if (type->size == sizeof(float)) {
        float a = (float)left;
        float b = (float)right;
        float value;

        switch (op) {
            case AST_BINARY_ADD: value = a + b; break;
            case AST_BINARY_SUB: value = a - b; break;
            case AST_BINARY_MUL: value = a * b; break;
            case AST_BINARY_DIV:
                if (b == 0.0f) return false;
                value = a / b;
                break;
            case AST_BINARY_EQUAL:     *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a == b}; return true;
            case AST_BINARY_NOT_EQUAL: *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a != b}; return true;
            case AST_BINARY_LT:        *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a < b}; return true;
            case AST_BINARY_LTE:       *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a <= b}; return true;
            case AST_BINARY_GT:        *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a > b}; return true;
            case AST_BINARY_GTE:       *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a >= b}; return true;
            default: return false;
        }

        *result = (HirLiteral){.kind = HIR_LITERAL_FLOAT, .floating = (double)value};
        return true;
    }

    double a = left;
    double b = right;
    double value;

    switch (op) {
        case AST_BINARY_ADD: value = a + b; break;
        case AST_BINARY_SUB: value = a - b; break;
        case AST_BINARY_MUL: value = a * b; break;
        case AST_BINARY_DIV:
            if (b == 0.0) return false;
            value = a / b;
            break;
        case AST_BINARY_EQUAL:     *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a == b}; return true;
        case AST_BINARY_NOT_EQUAL: *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a != b}; return true;
        case AST_BINARY_LT:        *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a < b}; return true;
        case AST_BINARY_LTE:       *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a <= b}; return true;
        case AST_BINARY_GT:        *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a > b}; return true;
        case AST_BINARY_GTE:       *result = (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = a >= b}; return true;
        default: return false;
    }

    *result = (HirLiteral){.kind = HIR_LITERAL_FLOAT, .floating = value};
    return true;
}


static HirExpr *eval_binary(CompContext *context, HirExpr *expr) {
    AstBinaryOp op = expr->binary.op;

    if (op == AST_BINARY_LOGICAL_AND || op == AST_BINARY_LOGICAL_OR) {
        HirExpr *left = comp_eval_expr(context, expr->binary.left);

        if (left->kind != HIR_EXPR_LITERAL || left->literal.kind != HIR_LITERAL_BOOL)
            return expr;

        bool lhs = left->literal.boolean;

        if (op == AST_BINARY_LOGICAL_AND && !lhs)
            return literal(context->sema, expr, (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = false}, expr->type);

        if (op == AST_BINARY_LOGICAL_OR && lhs)
            return literal(context->sema, expr, (HirLiteral){.kind = HIR_LITERAL_BOOL, .boolean = true}, expr->type);

        HirExpr *right = comp_eval_expr(context, expr->binary.right);

        if (right->kind != HIR_EXPR_LITERAL || right->literal.kind != HIR_LITERAL_BOOL)
            return expr;

        return literal(context->sema, expr, (HirLiteral){
            .kind = HIR_LITERAL_BOOL,
            .boolean = right->literal.boolean,
        }, expr->type);
    }

    HirExpr *left = comp_eval_expr(context, expr->binary.left);
    HirExpr *right = comp_eval_expr(context, expr->binary.right);

    if (left->kind != HIR_EXPR_LITERAL || right->kind != HIR_EXPR_LITERAL)
        return expr;

    double float_left;
    double float_right;
    if (comp_literal_float_value(left, &float_left) && comp_literal_float_value(right, &float_right)) {
        HirLiteral result;
        if (!comp_eval_float_binary(op, left->type, float_left, float_right, &result))
            return expr;
        return literal(context->sema, expr, result, expr->type);
    }

    if (left->literal.kind == HIR_LITERAL_BOOL &&
        right->literal.kind == HIR_LITERAL_BOOL) {
        bool a = left->literal.boolean;
        bool b = right->literal.boolean;
        bool result;

        switch (op) {
            case AST_BINARY_EQUAL: result = a == b; break;
            case AST_BINARY_NOT_EQUAL: result = a != b; break;
            default: return expr;
        }

        return literal(context->sema, expr, (HirLiteral){
            .kind = HIR_LITERAL_BOOL,
            .boolean = result,
        }, expr->type);
    }

    if (!is_integer(expr->type) && !is_integer(left->type))
        return expr;

    uint64_t a = left->literal.integer;
    uint64_t b = right->literal.integer;
    uint64_t result;

    switch (op) {
        case AST_BINARY_ADD:
            result = a + b;
            break;

        case AST_BINARY_SUB:
            result = a - b;
            break;

        case AST_BINARY_MUL:
            result = a * b;
            break;

        case AST_BINARY_DIV:
            if (b == 0) {
                //! TODO: division by zero diagnostic
                return expr;
            }

            if (is_signed(expr->type)) {
                int64_t sa = integer_signed(expr->type, a);
                int64_t sb = integer_signed(expr->type, b);
                result = (uint64_t)((__int128)sa / (__int128)sb);
            } else {
                result = a / b;
            }
            break;

        case AST_BINARY_MOD:
            if (b == 0) {
                //! TODO: division by zero diagnostic
                return expr;
            }

            if (is_signed(expr->type)) {
                int64_t sa = integer_signed(expr->type, a);
                int64_t sb = integer_signed(expr->type, b);
                result = (uint64_t)((__int128)sa % (__int128)sb);
            } else {
                result = a % b;
            }
            break;

        case AST_BINARY_BIT_AND:
            result = a & b;
            break;

        case AST_BINARY_BIT_XOR:
            result = a ^ b;
            break;

        case AST_BINARY_BIT_OR:
            result = a | b;
            break;

        case AST_BINARY_NAND:
            result = ~(a & b);
            break;

        case AST_BINARY_NOR:
            result = ~(a | b);
            break;

        case AST_BINARY_SHL:
            result = a << (b & 63);
            break;

        case AST_BINARY_SHR:
            if (is_signed(expr->type))
                result = (uint64_t)(integer_signed(expr->type, a) >> (b & 63));
            else
                result = a >> (b & 63);
            break;

        case AST_BINARY_EQUAL:
            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = is_signed(expr->type)
                    ? integer_signed(expr->type, a) == integer_signed(expr->type, b)
                    : a == b,
            }, expr->type);

        case AST_BINARY_NOT_EQUAL:
            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = is_signed(expr->type)
                    ? integer_signed(expr->type, a) != integer_signed(expr->type, b)
                    : a != b,
            }, expr->type);

        case AST_BINARY_LT:
            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = is_signed(expr->type)
                    ? integer_signed(expr->type, a) < integer_signed(expr->type, b)
                    : a < b,
            }, expr->type);

        case AST_BINARY_LTE:
            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = is_signed(expr->type)
                    ? integer_signed(expr->type, a) <= integer_signed(expr->type, b)
                    : a <= b,
            }, expr->type);

        case AST_BINARY_GT:
            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = is_signed(expr->type)
                    ? integer_signed(expr->type, a) > integer_signed(expr->type, b)
                    : a > b,
            }, expr->type);

        case AST_BINARY_GTE:
            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = is_signed(expr->type)
                    ? integer_signed(expr->type, a) >= integer_signed(expr->type, b)
                    : a >= b,
            }, expr->type);

        default:
            return expr;
    }

    result = integer_wrap(expr->type, result);

    return literal(context->sema, expr, (HirLiteral){
        .kind = HIR_LITERAL_INTEGER,
        .integer = result,
    }, expr->type);
}

static HirExpr *eval_unary(CompContext *context, HirExpr *expr) {
    switch (expr->unary.op) {
        case AST_UNARY_ADDRESS: {
            HirExpr *result = arena_calloc(context->sema->arena, sizeof(*result));
            *result = *expr;
            return result;
        }

        case AST_UNARY_DEREF: {
            HirExpr *operand = comp_eval_expr(context, expr->unary.operand);

            if (operand == NULL ||
                operand->kind != HIR_EXPR_UNARY ||
                operand->unary.op != AST_UNARY_ADDRESS)
                return expr;

            return comp_eval_expr(context, operand->unary.operand);
        }

        default:
            break;
    }

    HirExpr *operand = comp_eval_expr(context, expr->unary.operand);

    if (operand->kind != HIR_EXPR_LITERAL)
        return expr;

    double float_operand;
    if (comp_literal_float_value(operand, &float_operand)) {
        if (expr->unary.op == AST_UNARY_POS || expr->unary.op == AST_UNARY_NEG)
            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_FLOAT,
                .floating = comp_round_float(expr->type, expr->unary.op == AST_UNARY_NEG ? -float_operand : float_operand),
            }, expr->type);
    }

    switch (expr->unary.op) {
        case AST_UNARY_POS:
            if (!is_integer(operand->type))
                return expr;

            return operand;

        case AST_UNARY_NEG:
            if (!is_integer(operand->type))
                return expr;

            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_INTEGER,
                .integer = integer_wrap(expr->type, 0 - operand->literal.integer),
            }, expr->type);

        case AST_UNARY_BIT_NOT:
            if (!is_integer(operand->type))
                return expr;

            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_INTEGER,
                .integer = integer_wrap(expr->type, ~operand->literal.integer),
            }, expr->type);

        case AST_UNARY_NOT:
            if (operand->literal.kind != HIR_LITERAL_BOOL)
                return expr;

            return literal(context->sema, expr, (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = !operand->literal.boolean,
            }, expr->type);

        case AST_UNARY_DEREF:
        case AST_UNARY_ADDRESS:
            return expr;
    }

    return expr;
}

static HirGlobal *find_global(CompContext *context, Symbol *symbol) {
    Sema *sema = context->sema;

    for (size_t i = 0; i < sema->hir_module->globals.len; i++) {
        HirGlobal *global = &((HirGlobal *)sema->hir_module->globals.data)[i];

        if (global->symbol == symbol)
            return global;
    }

    return NULL;
}

static HirExpr *eval_global(CompContext *context, HirGlobal *global) {
    switch (global->comp_state) {
        case COMP_GLOBAL_EVALUATED:
            return global->init;

        case COMP_GLOBAL_EVALUATING:
            //! TODO: report comptime dependency cycle
            return global->init;

        case COMP_GLOBAL_FAILED:
            return global->init;

        case COMP_GLOBAL_UNVISITED:
            break;
    }

    global->comp_state = COMP_GLOBAL_EVALUATING;

    if (global->init == NULL) {
        global->comp_state = COMP_GLOBAL_FAILED;
        return global->init;
    }

    HirExpr *result = comp_eval_expr(context, global->init);

    if (!comp_expr_is_evaluable(result)) {
        global->comp_state = COMP_GLOBAL_FAILED;
        return global->init;
    }

    global->init = result;
    global->comp_state = COMP_GLOBAL_EVALUATED;
    return result;
}

bool comp_expr_is_evaluable(HirExpr *expr) {
    if (expr == NULL)
        return false;

    switch (expr->kind) {
        case HIR_EXPR_LITERAL:
        case HIR_EXPR_CODE:
            return true;

        case HIR_EXPR_VALUE:
            return expr->value.symbol != NULL;

        case HIR_EXPR_UNARY:
            return expr->unary.op == AST_UNARY_ADDRESS;

        case HIR_EXPR_INIT:
            for (size_t i = 0; i < expr->init.fields.len; i++) {
                HirInitField *field = &((HirInitField *)expr->init.fields.data)[i];

                if (!comp_expr_is_evaluable(field->value))
                    return false;
            }

            return true;

        case HIR_EXPR_CALL:
            return true;

        case HIR_EXPR_CAST:
            return comp_expr_is_evaluable(expr->cast.operand);

        default:
            return false;
    }
}

static HirFunction *find_function(CompContext *context, Symbol *symbol) {
    Sema *sema = context->sema;

    for (size_t i = 0; i < sema->hir_module->functions.len; i++) {
        HirFunction *function = &((HirFunction *)sema->hir_module->functions.data)[i];

        if (function->symbol == symbol)
            return function;
    }

    return NULL;
}

typedef enum {
    COMP_EXEC_NORMAL,
    COMP_EXEC_RETURN,
    COMP_EXEC_BREAK,
    COMP_EXEC_CONTINUE,
    COMP_EXEC_ERROR,
} CompExecKind;

typedef struct {
    CompExecKind kind;
    HirExpr *value;
} CompExecResult;

static inline HirType *base_type(HirType *type) {
    while (type != NULL && type->base)
        type = type->base;

    return type;
}

static HirExpr *zero(CompContext *context, HirType *type) {
    if (type == NULL)
        return NULL;

    HirType *base = base_type(type);

    HirExpr *result = arena_calloc(context->sema->arena, sizeof(*result));

    result->span = (Span){0};
    result->type = type;

    if (base->kind == HIR_TYPE_BUILTIN) {
        if (is_integer(base)) {
            result->kind = HIR_EXPR_LITERAL;
            result->literal = (HirLiteral){
                .kind = HIR_LITERAL_INTEGER,
                .integer = 0,
            };
            return result;
        }

        if (base->builtin == BUILTIN_BOOL) {
            result->kind = HIR_EXPR_LITERAL;
            result->literal = (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = false,
            };
            return result;
        }
    }

    if (base->kind == HIR_TYPE_POINTER && base->pointer.optional) {
        result->kind = HIR_EXPR_LITERAL;
        result->literal = (HirLiteral){
            .kind = HIR_LITERAL_NULL,
        };
        return result;
    }

    if (base->kind == HIR_TYPE_STRUCT ||
        base->kind == HIR_TYPE_UNION ||
        base->kind == HIR_TYPE_ARRAY) {
        result->kind = HIR_EXPR_INIT;
        result->init.fields = array_create(context->sema->arena, sizeof(HirInitField));
        return result;
    }

    return NULL;
}

static HirInitField *find_field(HirExpr *object, HirField *field) {
    for (size_t i = 0; i < object->init.fields.len; i++) {
        HirInitField *init_field = &((HirInitField *)object->init.fields.data)[i];

        if (init_field->field == field)
            return init_field;

        if (init_field->field != NULL &&
            comp_same_symbol(init_field->field->symbol, field->symbol))
            return init_field;
    }

    return NULL;
}

static HirInitField *find_index(HirExpr *object, size_t index) {
    HirType *type = base_type(object->type);

    if (type == NULL || type->kind != HIR_TYPE_ARRAY)
        return NULL;

    size_t offset = sizeof(uint64_t) + index * type->array.element->size;

    for (size_t i = 0; i < object->init.fields.len; i++) {
        HirInitField *field = &((HirInitField *)object->init.fields.data)[i];

        if (field->offset == offset)
            return field;
    }

    return NULL;
}

static HirExpr *eval_field(CompContext *context, HirExpr *expr) {
    HirExpr *object = comp_eval_expr(context, expr->field.object);

    if (object == NULL || object->kind != HIR_EXPR_INIT)
        return expr;

    HirInitField *field = find_field(object, expr->field.field);

    if (field == NULL)
        return zero(context, expr->type);

    return comp_eval_expr(context, field->value);
}

static HirExpr *eval_index(CompContext *context, HirExpr *expr) {
    HirExpr *object = comp_eval_expr(context, expr->index.object);
    HirExpr *index = comp_eval_expr(context, expr->index.index);

    if (object == NULL ||
        index == NULL ||
        object->kind != HIR_EXPR_INIT ||
        index->kind != HIR_EXPR_LITERAL ||
        index->literal.kind != HIR_LITERAL_INTEGER)
        return expr;

    HirType *type = base_type(object->type);

    if (type == NULL || type->kind != HIR_TYPE_ARRAY)
        return expr;

    size_t value = (size_t)index->literal.integer;

    if (value >= type->array.length)
        return expr;

    HirInitField *field = find_index(object, value);

    if (field == NULL)
        return zero(context, expr->type);

    return comp_eval_expr(context, field->value);
}

static HirExpr *update_field(CompContext *context, HirExpr *object, HirField *field, HirExpr *value) {
    HirExpr *result = arena_calloc(context->sema->arena, sizeof(*result));
    *result = *object;

    result->init.fields = array_create(context->sema->arena, sizeof(HirInitField));

    bool found = false;

    for (size_t i = 0; i < object->init.fields.len; i++) {
        HirInitField current = ((HirInitField *)object->init.fields.data)[i];

        if (current.field == field ||
            (current.field != NULL && comp_same_symbol(current.field->symbol, field->symbol))) {
            current.value = value;
            found = true;
        }

        array_push(&result->init.fields, &current);
    }

    if (!found) {
        HirInitField current = {
            .field = field,
            .offset = field->offset,
            .value = value,
        };

        array_push(&result->init.fields, &current);
    }

    return result;
}

static HirExpr *update_index(CompContext *context, HirExpr *object, size_t index, HirExpr *value) {
    HirType *type = base_type(object->type);

    if (type == NULL || type->kind != HIR_TYPE_ARRAY || index >= type->array.length)
        return NULL;

    size_t offset = sizeof(uint64_t) + index * type->array.element->size;

    HirExpr *result = arena_calloc(context->sema->arena, sizeof(*result));

    *result = *object;

    result->init.fields = array_create(context->sema->arena, sizeof(HirInitField));

    bool found = false;

    for (size_t i = 0; i < object->init.fields.len; i++) {
        HirInitField current = ((HirInitField *)object->init.fields.data)[i];

        if (current.offset == offset) {
            current.value = value;
            found = true;
        }

        array_push(&result->init.fields, &current);
    }

    if (!found) {
        HirInitField current = {
            .field = NULL,
            .offset = offset,
            .value = value,
        };

        array_push(&result->init.fields, &current);
    }

    return result;
}

static bool store_place(CompContext *context, HirExpr *place, HirExpr *value) {
    switch (place->kind) {
        case HIR_EXPR_VALUE: {
            Symbol *symbol = place->value.symbol;

            if (symbol == NULL ||
                (symbol->kind != SYMBOL_LOCAL && symbol->kind != SYMBOL_PARAMETER))
                return false;

            return store(context, symbol, value);
        }

        case HIR_EXPR_FIELD: {
            HirExpr *object = comp_eval_expr(context, place->field.object);

            if (object == NULL || object->kind != HIR_EXPR_INIT)
                return false;

            HirExpr *updated = update_field(context, object, place->field.field, value);

            return updated != NULL &&
                   store_place(context, place->field.object, updated);
        }

        case HIR_EXPR_INDEX: {
            HirExpr *object = comp_eval_expr(context, place->index.object);
            HirExpr *index = comp_eval_expr(context, place->index.index);

            assert(object != NULL);
            assert(index != NULL);
            assert(object->kind == HIR_EXPR_INIT);
            assert(index->kind == HIR_EXPR_LITERAL);
            assert(index->literal.kind == HIR_LITERAL_INTEGER);

            size_t value_index = (size_t)index->literal.integer;
            HirExpr *updated = update_index(context, object, value_index, value);

            assert(updated != NULL);
            assert(store_place(context, place->index.object, updated));

            return true;
        }

        case HIR_EXPR_UNARY: {
            if (place->unary.op != AST_UNARY_DEREF)
                return false;

            HirExpr *pointer = comp_eval_expr(context, place->unary.operand);

            if (pointer == NULL ||
                pointer->kind != HIR_EXPR_UNARY ||
                pointer->unary.op != AST_UNARY_ADDRESS)
                return false;

            return store_place(context, pointer->unary.operand, value);
        }

        default:
            return false;
    }
}

static CompExecResult exec_stmt(CompContext *context, HirStmt *stmt) {
    switch (stmt->kind) {
        case HIR_STMT_BLOCK:
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                HirStmt *child = ((HirStmt **)stmt->block.stmts.data)[i];
                CompExecResult result = exec_stmt(context, child);

                if (result.kind != COMP_EXEC_NORMAL)
                    return result;
            }

            return (CompExecResult){.kind = COMP_EXEC_NORMAL};

        case HIR_STMT_RETURN: {
            HirExpr *value = NULL;

            if (stmt->_return.value != NULL) {
                value = comp_eval_expr(context, stmt->_return.value);

                if (value == NULL || !comp_expr_is_evaluable(value))
                    return (CompExecResult){.kind = COMP_EXEC_ERROR};
            }

            return (CompExecResult){
                .kind = COMP_EXEC_RETURN,
                .value = value,
            };
        }

case HIR_STMT_ASSIGN: {
    HirExpr *value;
    if (stmt->assign.op == AST_BINARY_ASSIGN) {
        value = comp_eval_expr(context, stmt->assign.value);
    } else {
        HirExpr *left = comp_eval_expr(context, stmt->assign.target);
        HirExpr *right = comp_eval_expr(context, stmt->assign.value);
        if (left == NULL || right == NULL || !comp_expr_is_evaluable(left) || !comp_expr_is_evaluable(right))
            return (CompExecResult){.kind = COMP_EXEC_ERROR};

        if (stmt->assign.operation_type != NULL) {
            left = sema_coerce(context->sema, left, stmt->assign.operation_type);
            right = sema_coerce(context->sema, right, stmt->assign.right_type);
            if (left == NULL || right == NULL)
                return (CompExecResult){.kind = COMP_EXEC_ERROR};

            left = comp_eval_expr(context, left);
            right = comp_eval_expr(context, right);
            if (left == NULL || right == NULL)
                return (CompExecResult){.kind = COMP_EXEC_ERROR};
        }

        HirExpr binary = {
            .span = stmt->span,
            .kind = HIR_EXPR_BINARY,
            .type = stmt->assign.operation_type != NULL
                ? stmt->assign.operation_type
                : stmt->assign.target->type,
            .binary = {.op = stmt->assign.op, .left = left, .right = right},
        };
        value = comp_eval_expr(context, &binary);

        if (value != NULL && stmt->assign.operation_type != NULL) {
            value = sema_coerce(context->sema, value, stmt->assign.target->type);
            if (value != NULL)
                value = comp_eval_expr(context, value);
        }
    }

    if (value == NULL || !comp_expr_is_evaluable(value))
        return (CompExecResult){.kind = COMP_EXEC_ERROR};

    if (!store_place(context, stmt->assign.target, value)) {
        assert(!"comptime assignment failed");
        return (CompExecResult){.kind = COMP_EXEC_ERROR};
    }

    return (CompExecResult){.kind = COMP_EXEC_NORMAL};
}

        case HIR_STMT_EXPR:
            if (comp_eval_expr(context, stmt->expr) == NULL)
                return (CompExecResult){.kind = COMP_EXEC_ERROR};

            return (CompExecResult){.kind = COMP_EXEC_NORMAL};

        case HIR_STMT_IF: {
            HirExpr *condition = comp_eval_expr(context, stmt->_if.cond);

            if (condition == NULL ||
                condition->kind != HIR_EXPR_LITERAL ||
                condition->literal.kind != HIR_LITERAL_BOOL)
                return (CompExecResult){.kind = COMP_EXEC_ERROR};

            HirStmt *branch = condition->literal.boolean ? stmt->_if.then : stmt->_if._else;

            if (branch == NULL)
                return (CompExecResult){.kind = COMP_EXEC_NORMAL};

            return exec_stmt(context, branch);
        }

        case HIR_STMT_WHILE:
            for (;;) {
                HirExpr *condition = comp_eval_expr(context, stmt->_while.cond);

                if (condition == NULL ||
                    condition->kind != HIR_EXPR_LITERAL ||
                    condition->literal.kind != HIR_LITERAL_BOOL)
                    return (CompExecResult){.kind = COMP_EXEC_ERROR};

                if (!condition->literal.boolean)
                    return (CompExecResult){.kind = COMP_EXEC_NORMAL};

                CompExecResult result = exec_stmt(context, stmt->_while.body);

                switch (result.kind) {
                    case COMP_EXEC_NORMAL:
                    case COMP_EXEC_CONTINUE:
                        break;
                    
                    case COMP_EXEC_BREAK:
                        return (CompExecResult){.kind = COMP_EXEC_NORMAL};

                    default:
                        return result;
                }

                if (stmt->_while.post != NULL) {
                    CompExecResult post = exec_stmt(context, stmt->_while.post);
                    if (post.kind != COMP_EXEC_NORMAL)
                        return post;
                }

                continue;
            }

        case HIR_STMT_BREAK:
            return (CompExecResult){.kind = COMP_EXEC_BREAK};

        case HIR_STMT_CONTINUE:
            return (CompExecResult){.kind = COMP_EXEC_CONTINUE};

        default:
            return (CompExecResult){.kind = COMP_EXEC_ERROR};
    }
}

static CompExecResult eval_function(CompContext *context, HirFunction *function, Array args) {
    CompFrame frame = {
        .bindings = array_create(context->sema->arena, sizeof(CompBinding)),
    };

    for (size_t i = 0; i < function->params.len; i++) {
        HirParam *param = &((HirParam *)function->params.data)[i];
        HirExpr *value = ((HirExpr **)args.data)[i];

        CompBinding binding = {
            .symbol = param->symbol,
            .value = value,
        };

        array_push(&frame.bindings, &binding);
    }

    for (size_t i = 0; i < function->locals.len; i++) {
        HirLocal *local = &((HirLocal *)function->locals.data)[i];

        CompBinding binding = {
            .symbol = local->symbol,
            .value = NULL,
        };

        array_push(&frame.bindings, &binding);
    }

    CompFrame *previous = context->frame;
    context->frame = &frame;

    CompExecResult result = exec_stmt(context, function->body);

    context->frame = previous;

    return result;
}

static HirExpr *comp_code_splice_error(CompContext *context, AstExpr *splice, String message) {
    DiagBuilder diagnostic = diag_begin(
        context->sema->diags, DIAG_ERROR, E_TYPE_MISMATCH, splice->span, message);
    diag_finish(&diagnostic);

    HirExpr *error = arena_calloc(context->sema->arena, sizeof(*error));
    error->span = splice->span;
    error->kind = HIR_EXPR_ERROR;
    return error;
}

static HirCodeSplice *comp_find_code_splice(Array splices, AstExpr *ast) {
    for (size_t i = 0; i < splices.len; i++) {
        HirCodeSplice *splice = array_at(&splices, i);
        if (splice->splice == ast)
            return splice;
    }
    return NULL;
}

static HirCodeSplice *comp_find_code_name_splice(Array splices, AstName *name) {
    for (size_t i = 0; i < splices.len; i++) {
        HirCodeSplice *splice = array_at(&splices, i);
        if (splice->name_splice == name)
            return splice;
    }
    return NULL;
}

static AstName comp_expand_quoted_name(CompContext *context, AstName *ast, Array splices, bool *ok) {
    if (ast == NULL || ast->kind != AST_NAME_SPLICE || !*ok)
        return ast != NULL ? *ast : (AstName){.kind = AST_NAME_ERROR};

    HirCodeSplice *splice = comp_find_code_name_splice(splices, ast);
    if (splice == NULL) {
        *ok = false;
        return (AstName){.kind = AST_NAME_ERROR};
    }

    HirExpr *value = comp_eval_expr(context, splice->value);
    HirType *type = value != NULL ? base_type(value->type) : NULL;
    if (value == NULL || value->kind != HIR_EXPR_CODE || !value->code.is_name ||
        type == NULL || type->kind != HIR_TYPE_CODE ||
        type->code.category != CODE_CATEGORY_NAME ||
        value->code.name.kind != AST_NAME_IDENT) {
        *ok = false;
        return (AstName){.kind = AST_NAME_ERROR};
    }

    return value->code.name;
}

static Path comp_expand_quoted_path(CompContext *context, Path path, Array splices, bool *ok) {
    Path result = {
        .parts = array_create(context->sema->arena, sizeof(AstName)),
    };

    for (size_t i = 0; i < path.parts.len; i++) {
        AstName *source = array_at(&path.parts, i);
        AstName name = comp_expand_quoted_name(context, source, splices, ok);
        array_push(&result.parts, &name);
    }

    return result;
}

static AstExpr *comp_expand_quoted_expression(
    CompContext *context, AstExpr *ast, Array splices, bool *ok
) {
    if (ast == NULL || !*ok)
        return ast;

    if (ast->kind == AST_EXPR_SPLICE) {
        HirCodeSplice *splice = comp_find_code_splice(splices, ast);
        if (splice == NULL) {
            *ok = false;
            return NULL;
        }

        HirExpr *value = comp_eval_expr(context, splice->value);
        HirType *type = value != NULL ? base_type(value->type) : NULL;
        if (value == NULL || value->kind != HIR_EXPR_CODE ||
            value->code.is_name ||
            type == NULL || type->kind != HIR_TYPE_CODE ||
            type->code.category != CODE_CATEGORY_EXPR ||
            value->code.expression == NULL) {
            *ok = false;
            return NULL;
        }

        return value->code.expression;
    }

    /* A nested quotation owns its own splice scope. */
    if (ast->kind == AST_EXPR_QUOTE)
        return ast;

    AstExpr *copy = arena_calloc(context->sema->arena, sizeof(*copy));
    *copy = *ast;

#define EXPAND_EXPR(member) \
    copy->member = comp_expand_quoted_expression(context, ast->member, splices, ok)

    switch (ast->kind) {
        case AST_EXPR_UNARY:
            EXPAND_EXPR(unary.operand);
            break;

        case AST_EXPR_IDENT:
            copy->ident = comp_expand_quoted_name(
                context, &ast->ident, splices, ok);
            break;

        case AST_EXPR_PATH:
            copy->path = comp_expand_quoted_path(
                context, ast->path, splices, ok);
            break;

        case AST_EXPR_BINARY:
            EXPAND_EXPR(binary.left);
            EXPAND_EXPR(binary.right);
            break;

        case AST_EXPR_CALL: {
            EXPAND_EXPR(call.callee);
            copy->call.args = array_create(context->sema->arena, sizeof(AstCallArg));
            for (size_t i = 0; i < ast->call.args.len; i++) {
                AstCallArg arg = ((AstCallArg *)ast->call.args.data)[i];
                arg.name = comp_expand_quoted_name(
                    context, &((AstCallArg *)ast->call.args.data)[i].name, splices, ok);
                arg.value = comp_expand_quoted_expression(context, arg.value, splices, ok);
                array_push(&copy->call.args, &arg);
            }
            break;
        }

        case AST_EXPR_INFERRED_METHOD_CALL: {
            copy->inferred_method_call.name = comp_expand_quoted_name(
                context, &ast->inferred_method_call.name, splices, ok);
            copy->inferred_method_call.args = array_create(context->sema->arena, sizeof(AstCallArg));
            for (size_t i = 0; i < ast->inferred_method_call.args.len; i++) {
                AstCallArg arg = ((AstCallArg *)ast->inferred_method_call.args.data)[i];
                arg.name = comp_expand_quoted_name(
                    context, &((AstCallArg *)ast->inferred_method_call.args.data)[i].name, splices, ok);
                arg.value = comp_expand_quoted_expression(context, arg.value, splices, ok);
                array_push(&copy->inferred_method_call.args, &arg);
            }
            break;
        }

        case AST_EXPR_INDEX:
            EXPAND_EXPR(index.object);
            EXPAND_EXPR(index.index);
            break;

        case AST_EXPR_MEMBER:
            EXPAND_EXPR(member.object);
            copy->member.member = comp_expand_quoted_name(
                context, &ast->member.member, splices, ok);
            break;

        case AST_EXPR_CAST:
            EXPAND_EXPR(cast.operand);
            break;

        case AST_EXPR_INTRINSIC: {
            copy->intrinsic.name = comp_expand_quoted_path(
                context, ast->intrinsic.name, splices, ok);
            copy->intrinsic.args = array_create(context->sema->arena, sizeof(AstIntrinsicArg));
            for (size_t i = 0; i < ast->intrinsic.args.len; i++) {
                AstIntrinsicArg arg = ((AstIntrinsicArg *)ast->intrinsic.args.data)[i];
                if (arg.kind == AST_INTRINSIC_ARG_EXPR)
                    arg.expr = comp_expand_quoted_expression(context, arg.expr, splices, ok);
                else if (arg.kind == AST_INTRINSIC_ARG_MEMBER_PATH) {
                    arg.member_path = array_create(context->sema->arena, sizeof(AstName));
                    for (size_t j = 0; j < ((AstIntrinsicArg *)ast->intrinsic.args.data)[i].member_path.len; j++) {
                        AstName *source = array_at(
                            &((AstIntrinsicArg *)ast->intrinsic.args.data)[i].member_path, j);
                        AstName name = comp_expand_quoted_name(context, source, splices, ok);
                        array_push(&arg.member_path, &name);
                    }
                }
                array_push(&copy->intrinsic.args, &arg);
            }
            break;
        }

        case AST_EXPR_BUBBLE:
            EXPAND_EXPR(bubble.operand);
            break;

        case AST_EXPR_INIT: {
            copy->init.fields = array_create(context->sema->arena, sizeof(AstInitField));
            for (size_t i = 0; i < ast->init.fields.len; i++) {
                AstInitField field = ((AstInitField *)ast->init.fields.data)[i];
                field.name = comp_expand_quoted_name(
                    context, &((AstInitField *)ast->init.fields.data)[i].name, splices, ok);
                field.value = comp_expand_quoted_expression(context, field.value, splices, ok);
                array_push(&copy->init.fields, &field);
            }
            break;
        }

        case AST_EXPR_ERROR:
        case AST_EXPR_LITERAL:
        case AST_EXPR_TYPE_VALUE:
        case AST_EXPR_LAMBDA:
        case AST_EXPR_SPLICE:
        case AST_EXPR_QUOTE:
            break;
    }

#undef EXPAND_EXPR
    return copy;
}

static AstStmt *comp_expand_quoted_statement(
    CompContext *context, AstStmt *ast, Array splices, bool *ok
) {
    if (ast == NULL || !*ok)
        return ast;

    AstStmt *copy = arena_calloc(context->sema->arena, sizeof(*copy));
    *copy = *ast;

    switch (ast->kind) {
        case AST_STMT_BLOCK:
            copy->block.stmts = array_create(context->sema->arena, sizeof(AstStmt *));
            for (size_t i = 0; i < ast->block.stmts.len; i++) {
                AstStmt *child = ((AstStmt **)ast->block.stmts.data)[i];
                AstStmt *expanded = comp_expand_quoted_statement(context, child, splices, ok);
                array_push(&copy->block.stmts, &expanded);
            }
            break;

        case AST_STMT_VAR: {
            AstVarDecl *var = arena_calloc(context->sema->arena, sizeof(*var));
            *var = *ast->var;
            var->name = comp_expand_quoted_name(context, &ast->var->name, splices, ok);
            var->init = comp_expand_quoted_expression(context, ast->var->init, splices, ok);
            copy->var = var;
            break;
        }

        case AST_STMT_EXPR:
            copy->expr = comp_expand_quoted_expression(context, ast->expr, splices, ok);
            break;

        case AST_STMT_RETURN:
            copy->_return.value = comp_expand_quoted_expression(
                context, ast->_return.value, splices, ok);
            break;

        case AST_STMT_IF:
            copy->_if.cond = comp_expand_quoted_expression(context, ast->_if.cond, splices, ok);
            copy->_if.then = comp_expand_quoted_statement(context, ast->_if.then, splices, ok);
            copy->_if._else = comp_expand_quoted_statement(context, ast->_if._else, splices, ok);
            break;

        case AST_STMT_FOR:
            copy->_for.init = comp_expand_quoted_statement(context, ast->_for.init, splices, ok);
            copy->_for.cond = comp_expand_quoted_expression(context, ast->_for.cond, splices, ok);
            copy->_for.post = comp_expand_quoted_expression(context, ast->_for.post, splices, ok);
            copy->_for.body = comp_expand_quoted_statement(context, ast->_for.body, splices, ok);
            break;

        case AST_STMT_WHILE:
            copy->_while.cond = comp_expand_quoted_expression(context, ast->_while.cond, splices, ok);
            copy->_while.body = comp_expand_quoted_statement(context, ast->_while.body, splices, ok);
            break;

        case AST_STMT_MATCH:
            copy->match.expr = comp_expand_quoted_expression(context, ast->match.expr, splices, ok);
            copy->match.cases = array_create(context->sema->arena, sizeof(AstMatchCase));
            for (size_t i = 0; i < ast->match.cases.len; i++) {
                AstMatchCase case_ = ((AstMatchCase *)ast->match.cases.data)[i];
                case_.body = comp_expand_quoted_statement(context, case_.body, splices, ok);
                array_push(&copy->match.cases, &case_);
            }
            break;

        case AST_STMT_DEFER:
            copy->defer.deferred = comp_expand_quoted_statement(
                context, ast->defer.deferred, splices, ok);
            break;

        case AST_STMT_BREAK:
            copy->_break.value = comp_expand_quoted_expression(
                context, ast->_break.value, splices, ok);
            break;

        case AST_STMT_CONTINUE:
            copy->_continue.value = comp_expand_quoted_expression(
                context, ast->_continue.value, splices, ok);
            break;

        case AST_STMT_ERROR:
        default:
            break;
    }

    return copy;
}

static AstDecl *comp_expand_quoted_declaration(
    CompContext *context, AstDecl *ast, Array splices, bool *ok
) {
    if (ast == NULL || ast->kind != AST_DECL_FN || !*ok) {
        *ok = false;
        return NULL;
    }

    AstDecl *copy = arena_calloc(context->sema->arena, sizeof(*copy));
    *copy = *ast;
    copy->fn.name = comp_expand_quoted_name(context, &ast->fn.name, splices, ok);

    copy->fn.params = array_create(context->sema->arena, sizeof(AstParam));
    for (size_t i = 0; i < ast->fn.params.len; i++) {
        AstParam param = ((AstParam *)ast->fn.params.data)[i];
        param.name = comp_expand_quoted_name(context, &param.name, splices, ok);
        param.default_value = comp_expand_quoted_expression(
            context, param.default_value, splices, ok);
        array_push(&copy->fn.params, &param);
    }

    copy->fn.body = comp_expand_quoted_statement(context, ast->fn.body, splices, ok);
    return copy;
}

static HirExpr *comp_eval_code(CompContext *context, HirExpr *expr) {
    if (expr->code.is_name)
        return expr;

    if (expr->code.splices.len == 0)
        return expr;

    if (expr->code.is_decl) {
        bool ok = true;
        AstDecl *expanded = comp_expand_quoted_declaration(
            context, expr->code.declaration, expr->code.splices, &ok);
        if (!ok || expanded == NULL) {
            HirCodeSplice *first = array_at(&expr->code.splices, 0);
            return comp_code_splice_error(context, first->owner,
                STRING("failed to expand declaration splice; expected a #code::name<ident> value"));
        }

        HirExpr *result = arena_calloc(context->sema->arena, sizeof(*result));
        *result = *expr;
        result->code.declaration = expanded;
        result->code.splices = array_create(context->sema->arena, sizeof(HirCodeSplice));
        return result;
    }

    bool ok = true;
    AstExpr *expanded = comp_expand_quoted_expression(
        context, expr->code.expression, expr->code.splices, &ok);
    if (!ok || expanded == NULL) {
        HirCodeSplice *first = array_at(&expr->code.splices, 0);
        AstExpr *splice = first->owner;
        return comp_code_splice_error(context, splice,
            STRING("failed to expand code splice; expected a compatible #code value"));
    }

    HirExpr *result = arena_calloc(context->sema->arena, sizeof(*result));
    *result = *expr;
    result->code.expression = expanded;
    result->code.splices = array_create(context->sema->arena, sizeof(HirCodeSplice));
    return result;
}

HirExpr *comp_eval_expr(CompContext *context, HirExpr *expr) {
    switch (expr->kind) {
        case HIR_EXPR_LITERAL:
            return expr;

        case HIR_EXPR_CODE:
            return comp_eval_code(context, expr);

        case HIR_EXPR_VALUE: {
            Symbol *symbol = expr->value.symbol;

            if (symbol == NULL)
                return expr;

            if (symbol->kind == SYMBOL_LOCAL || symbol->kind == SYMBOL_PARAMETER) {
                HirExpr *value = comp_lookup(context, symbol);
                return value != NULL ? value : expr;
            }

            if (symbol->kind != SYMBOL_GLOBAL)
                return expr;

            HirGlobal *global = find_global(context, symbol);

            if (global == NULL || global->type->mutable)
                return expr;

            return eval_global(context, global);
        }

        case HIR_EXPR_UNARY:
            return eval_unary(context, expr);

        case HIR_EXPR_BINARY:
            return eval_binary(context, expr);

        case HIR_EXPR_FIELD:
            return eval_field(context, expr);

        case HIR_EXPR_INDEX:
            return eval_index(context, expr);

        case HIR_EXPR_CAST: {
            HirExpr *operand = comp_eval_expr(context, expr->cast.operand);
            HirType *source = operand != NULL ? operand->type : NULL;
            HirType *target = expr->type;

            if (operand == NULL || operand->kind != HIR_EXPR_LITERAL || target == NULL)
                return expr;
            if (source == NULL && operand->literal.kind == HIR_LITERAL_FLOAT)
                source = builtin_type(context->sema, BUILTIN_FLOAT64);
            if (source == NULL)
                return expr;

            if (operand->literal.kind == HIR_LITERAL_INTEGER && is_integer(source) && is_integer(target)) {
                return literal(context->sema, expr, (HirLiteral){
                    .kind = HIR_LITERAL_INTEGER,
                    .integer = integer_wrap(target, operand->literal.integer),
                }, target);
            }

            if (operand->literal.kind == HIR_LITERAL_INTEGER && comp_type_is_float(target)) {
                if (comp_type_is_float(source)) {
                    double value = comp_round_float(source, (double)operand->literal.integer);
                    return literal(context->sema, expr, (HirLiteral){
                        .kind = HIR_LITERAL_FLOAT,
                        .floating = comp_round_float(target, value),
                    }, target);
                }
                if (is_integer(source)) {
                    long double number = is_signed(source)
                        ? (long double)integer_signed(source, operand->literal.integer)
                        : (long double)operand->literal.integer;
                    double value = (double)number;
                    return literal(context->sema, expr, (HirLiteral){
                        .kind = HIR_LITERAL_FLOAT,
                        .floating = comp_round_float(target, value),
                    }, target);
                }
            }

            if (operand->literal.kind == HIR_LITERAL_FLOAT && comp_type_is_float(source)) {
                double value = comp_round_float(source, operand->literal.floating);
                if (comp_type_is_float(target)) {
                    return literal(context->sema, expr, (HirLiteral){
                        .kind = HIR_LITERAL_FLOAT,
                        .floating = comp_round_float(target, value),
                    }, target);
                }

                if (is_integer(target)) {
                    unsigned bits = integer_bits(target);
                    long double number = (long double)value;
                    uint64_t converted;

                    if (isnan(value)) {
                        converted = 0;
                    } else if (is_signed(target)) {
                        long double limit = bits == 64
                            ? 9223372036854775808.0L
                            : (long double)(UINT64_C(1) << (bits - 1));
                        uint64_t minimum = UINT64_C(1) << (bits - 1);
                        uint64_t maximum = bits == 64 ? (uint64_t)INT64_MAX : (UINT64_C(1) << (bits - 1)) - 1;

                        if (number <= -limit)
                            converted = minimum;
                        else if (number >= limit)
                            converted = maximum;
                        else
                            converted = (uint64_t)(int64_t)number;
                    } else {
                        long double limit = bits == 64
                            ? 18446744073709551616.0L
                            : (long double)(UINT64_C(1) << bits);
                        uint64_t maximum = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;

                        if (number <= 0.0L)
                            converted = 0;
                        else if (number >= limit)
                            converted = maximum;
                        else
                            converted = (uint64_t)number;
                    }

                    return literal(context->sema, expr, (HirLiteral){
                        .kind = HIR_LITERAL_INTEGER,
                        .integer = integer_wrap(target, converted),
                    }, target);
                }
            }

            return expr;
        }

        case HIR_EXPR_INIT: {
            HirExpr *result = arena_calloc(context->sema->arena, sizeof(*result));
            *result = *expr;

            result->init.fields = array_create(context->sema->arena, sizeof(HirInitField));

            for (size_t i = 0; i < expr->init.fields.len; i++) {
                HirInitField field = ((HirInitField *)expr->init.fields.data)[i];
                field.value = comp_eval_expr(context, field.value);
                array_push(&result->init.fields, &field);
            }

            return result;
        }

        case HIR_EXPR_CALL: {
            HirFunction *function = find_function(context, expr->call.function);

            if (function == NULL)
                return expr;

            Array args = array_create(context->sema->arena, sizeof(HirExpr *));

            for (size_t i = 0; i < expr->call.args.len; i++) {
                HirExpr *arg = ((HirExpr **)expr->call.args.data)[i];
                HirExpr *value = comp_eval_expr(context, arg);

                bool evaluable = value != NULL && comp_expr_is_evaluable(value);
                if (!evaluable)
                    return expr;

                array_push(&args, &value);
            }

            CompExecResult result = eval_function(context, function, args);

            if (result.kind != COMP_EXEC_RETURN)
                return expr;

            return result.value != NULL ? comp_eval_expr(context, result.value) : expr;
        }

        default:
            return expr;
    }
}

bool comp_exec_stmt(Sema *sema, HirStmt *stmt) {
    CompFrame frame = {
        .bindings = array_create(sema->arena, sizeof(CompBinding)),
    };

    for (size_t i = 0; i < sema->current_fn->locals.len; i++) {
        HirLocal *local = array_at(&sema->current_fn->locals, i);

        CompBinding binding = {
            .symbol = local->symbol,
            .value = NULL,
        };

        array_push(&frame.bindings, &binding);
    }

    CompContext context = {
        .sema = sema,
        .frame = &frame,
    };

    CompExecResult result = exec_stmt(&context, stmt);
    return result.kind == COMP_EXEC_NORMAL;
}