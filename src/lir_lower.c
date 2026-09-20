#include <assert.h>

#include "lir_lower.h"

typedef struct {
    Symbol *symbol;
    LirOperand operand;
} LirBinding;

typedef struct {
    LirFunction *function;
    LirBlockId block;

    Array(LirBinding) bindings;
} LirLower;

static LirBlock *lir_lower_block(LirLower *lower) {
    return ((LirBlock **)lower->function->blocks.data)[lower->block];
}

static LirBinding *lir_find_binding(LirLower *lower, Symbol *symbol) {
    for (size_t i = lower->bindings.len; i > 0; i--) {
        LirBinding *binding = &((LirBinding *)lower->bindings.data)[i - 1];

        if (binding->symbol == symbol) {
            return binding;
        }
    }

    return NULL;
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
            return false;
    }

    assert(!"unhandled builtin type");
    return false;
}

static LirOperand lir_lower_literal(HirExpr *expr) {
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

        case HIR_LITERAL_STRING:
            assert(!"string literals are not lowered yet");
            return lir_operand_invalid();
    }

    assert(!"unhandled HIR literal");
    return lir_operand_invalid();
}

static LirOperand lir_lower_expr(LirLower *lower, HirExpr *expr) {
    switch (expr->kind) {
        case HIR_EXPR_LITERAL:
            return lir_lower_literal(expr);

        case HIR_EXPR_VALUE: {
            Symbol *symbol = expr->value.symbol;

            switch (symbol->kind) {
                case SYMBOL_LOCAL:
                case SYMBOL_PARAMETER: {
                    LirBinding *binding = lir_find_binding(lower, symbol);
                    assert(binding);
                    return binding->operand;
                }

                case SYMBOL_GLOBAL:
                case SYMBOL_FN:
                    return lir_operand_symbol(symbol, expr->type);

                default:
                    assert(!"unexpected symbol kind in HIR value expression");
                    return lir_operand_invalid();
            }
        }

        case HIR_EXPR_UNARY: {
            LirOperand operand = lir_lower_expr(lower, expr->unary.operand);

            LirValueId result = lir_emit(lower->function, lower->block, lir_lower_unary_op(expr->unary.op), expr->type, &operand, 1);

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_BINARY: {
            LirOperand operands[2] = {
                lir_lower_expr(lower, expr->binary.left),
                lir_lower_expr(lower, expr->binary.right),
            };

            LirValueId result = lir_emit(lower->function, lower->block, lir_lower_binary_op(expr->binary.op), expr->type, operands, 2);

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_CALL: {
            size_t operand_count = expr->call.args.len + 1;
            LirOperand *operands = arena_alloc(lower->function->arena, sizeof(LirOperand) * operand_count);

            operands[0] = lir_operand_symbol(expr->call.function, expr->call.function->type);

            for (size_t i = 0; i < expr->call.args.len; i++) {
                operands[i + 1] = lir_lower_expr(lower, ((HirExpr **)expr->call.args.data)[i]);
            }

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CALL, expr->type, operands, operand_count);

            if (expr->type->kind == HIR_TYPE_BUILTIN && expr->type->builtin == BUILTIN_NONE) {
                return lir_operand_invalid();
            }

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_CAST: {
            LirOperand operand = lir_lower_expr(lower, expr->cast.operand);

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CAST, expr->type, &operand, 1);

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_INDEX:
        case HIR_EXPR_FIELD:
        case HIR_EXPR_INIT:
        case HIR_EXPR_LAMBDA:
            assert(!"HIR expression not yet lowered");

        case HIR_EXPR_ERROR:
            assert(!"error expression reached LIR");
    }

    assert(!"unhandled HIR expression");
    return lir_operand_invalid();
}

static void lir_lower_stmt(LirLower *lower, HirStmt *stmt) {
    switch (stmt->kind) {
        case HIR_STMT_BLOCK:
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                lir_lower_stmt(lower, ((HirStmt **)stmt->block.stmts.data)[i]);
            }
            return;

        case HIR_STMT_ASSIGN: {
            HirExpr *target = stmt->assign.target;

            assert(target->kind == HIR_EXPR_VALUE);

            LirOperand value = lir_lower_expr(lower, stmt->assign.value);

            Symbol *symbol = target->value.symbol;

            assert(symbol->kind == SYMBOL_LOCAL || symbol->kind == SYMBOL_PARAMETER);

            lir_bind(lower, symbol, value);
            return;
        }

        case HIR_STMT_EXPR:
            (void)lir_lower_expr(lower, stmt->expr);
            return;

        case HIR_STMT_RETURN: {
            if (stmt->_return.value) {
                LirOperand value = lir_lower_expr(lower, stmt->_return.value);

                lir_return(lower->function, lower->block, &value);
            } else {
                lir_return(lower->function, lower->block, NULL);
            }

            return;
        }

        case HIR_STMT_IF:
        case HIR_STMT_WHILE:
        case HIR_STMT_BREAK:
        case HIR_STMT_CONTINUE:
            assert(!"control-flow lowering not implemented yet");

        case HIR_STMT_ERROR:
            assert(!"error statement reached LIR");
    }

    assert(!"unhandled HIR statement");
}

static void lir_lower_function(LirModule *module, HirFunction *hir) {
    LirFunction *function = lir_function_create(module, hir->symbol, hir->return_type, hir->is_extern, hir->is_export);

    if (hir->is_extern) {
        return;
    }

    LirLower lower = {
        .function = function,
    };

    lower.block = lir_block_create(function);

    for (size_t i = 0; i < hir->params.len; i++) {
        HirParam *param = &((HirParam *)hir->params.data)[i];

        LirValueId value = lir_block_add_param(function, lower.block, param->type);

        lir_bind(&lower, param->symbol, lir_operand_value(value, param->type));
    }

    if (hir->body) {
        lir_lower_stmt(&lower, hir->body);
    }
}

LirModule *lir_lower_module(Arena *arena, HirModule *hir) {
    LirModule *module = lir_module_create(arena);

    for (size_t i = 0; i < hir->functions.len; i++) {
        lir_lower_function(module, &((HirFunction *)hir->functions.data)[i]);
    }

    return module;
}