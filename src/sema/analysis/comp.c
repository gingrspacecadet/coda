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

static bool comp_store(CompContext *context, Symbol *symbol, HirExpr *value) {
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
    HirExpr *result = arena_alloc(sema->arena, sizeof(*result));
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
    HirExpr *operand = comp_eval_expr(context, expr->unary.operand);

    if (operand->kind != HIR_EXPR_LITERAL)
        return expr;

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
            return true;

        case HIR_EXPR_VALUE:
            return expr->value.symbol != NULL;

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

typedef struct {
    bool ok;
    bool returned;
    HirExpr *value;
} CompExecResult;

static CompExecResult exec_stmt(CompContext *context, HirStmt *stmt) {
    switch (stmt->kind) {
        case HIR_STMT_BLOCK:
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                HirStmt *child = ((HirStmt **)stmt->block.stmts.data)[i];
                CompExecResult result = exec_stmt(context, child);

                if (!result.ok || result.returned)
                    return result;
            }

            return (CompExecResult){.ok = true};

        case HIR_STMT_RETURN: {
            HirExpr *value = NULL;

            if (stmt->_return.value != NULL) {
                value = comp_eval_expr(context, stmt->_return.value);

                if (value == NULL || !comp_expr_is_evaluable(value))
                    return (CompExecResult){.ok = false};
            }

            return (CompExecResult){
                .ok = true,
                .returned = true,
                .value = value,
            };
        }

        case HIR_STMT_ASSIGN: {
            HirExpr *target = stmt->assign.target;
            HirExpr *value = comp_eval_expr(context, stmt->assign.value);

            if (value == NULL || !comp_expr_is_evaluable(value))
                return (CompExecResult){.ok = false};

            if (target->kind != HIR_EXPR_VALUE)
                return (CompExecResult){.ok = false};

            Symbol *symbol = target->value.symbol;

            if (symbol == NULL ||
                (symbol->kind != SYMBOL_LOCAL && symbol->kind != SYMBOL_PARAMETER))
                return (CompExecResult){.ok = false};

            if (!comp_store(context, symbol, value))
                return (CompExecResult){.ok = false};

            return (CompExecResult){.ok = true};
        }

        case HIR_STMT_EXPR:
            if (comp_eval_expr(context, stmt->expr) == NULL)
                return (CompExecResult){.ok = false};

            return (CompExecResult){.ok = true};

        default:
            return (CompExecResult){.ok = false};
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

HirExpr *comp_eval_expr(CompContext *context, HirExpr *expr) {
    switch (expr->kind) {
        case HIR_EXPR_LITERAL:
            return expr;

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

        case HIR_EXPR_CAST: {
            HirExpr *operand = comp_eval_expr(context, expr->cast.operand);

            if (operand->kind != HIR_EXPR_LITERAL)
                return expr;

            if (operand->literal.kind == HIR_LITERAL_INTEGER &&
                expr->type->kind == HIR_TYPE_BUILTIN &&
                is_integer(expr->type)) {
                return literal(context->sema, expr, (HirLiteral){
                    .kind = HIR_LITERAL_INTEGER,
                    .integer = integer_wrap(expr->type, operand->literal.integer),
                }, expr->type);
            }

            return expr;
        }

        case HIR_EXPR_INIT: {
            HirExpr *result = arena_alloc(context->sema->arena, sizeof(*result));
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

            if (!result.ok || !result.returned)
                return expr;

            return result.value != NULL ? comp_eval_expr(context, result.value) : expr;
        }

        default:
            return expr;
    }
}