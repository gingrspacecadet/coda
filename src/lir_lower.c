#include <assert.h>

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
} LirLoop;

typedef struct {
    LirFunction *function;
    LirBlockId block;

    Array(LirBinding) bindings;
    Array(LirLoop) loops;
} LirLower;

static LirBlock *lir_lower_block(LirLower *lower) {
    return ((LirBlock **)lower->function->blocks.data)[lower->block];
}

static bool lir_lower_block_terminated(LirLower *lower) {
    return lir_lower_block(lower)->terminator.kind != LIR_TERM_NONE;
}

static LirBinding *lir_find_binding(LirLower *lower, Symbol *symbol) {
    for (size_t i = lower->bindings.len; i > 0; i--) {
        LirBinding *binding = &((LirBinding *)lower->bindings.data)[i - 1];

        if (binding->symbol == symbol || string_eq(binding->symbol->name.ident, symbol->name.ident)) {
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

static LirOperand lir_lower_logical_and(LirLower *lower, HirExpr *expr);
static LirOperand lir_lower_logical_or(LirLower *lower, HirExpr *expr);

static LirPlace lir_lower_place(LirLower *lower, HirExpr *expr);
static LirOperand lir_lower_load(LirLower *lower, LirPlace place);
static void lir_lower_store(LirLower *lower, LirPlace place, LirOperand value);

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

        case HIR_EXPR_UNARY:
            switch (expr->unary.op) {
                case AST_UNARY_DEREF: {
                    LirPlace place = lir_lower_place(lower, expr);

                    return lir_lower_load(lower, place);
                }

                case AST_UNARY_ADDRESS: {
                    LirPlace place = lir_lower_place(lower, expr->unary.operand);

                    return place.address;
                }

                default: {
                    LirOperand operand = lir_lower_expr(lower, expr->unary.operand);

                    LirValueId result = lir_emit(lower->function, lower->block, lir_lower_unary_op(expr->unary.op), expr->type, (Array){.data = &operand, .len = 1});

                    return lir_operand_value(result, expr->type);
                }
            }

        case HIR_EXPR_BINARY:
            if (expr->binary.op == AST_BINARY_LOGICAL_AND) {
                return lir_lower_logical_and(lower, expr);
            }

            if (expr->binary.op == AST_BINARY_LOGICAL_OR) {
                return lir_lower_logical_or(lower, expr);
            }

            LirOperand operands[2] = {
                lir_lower_expr(lower, expr->binary.left),
                lir_lower_expr(lower, expr->binary.right),
            };

            LirValueId result = lir_emit(lower->function, lower->block, lir_lower_binary_op(expr->binary.op), expr->type, (Array){.data = operands, .len = 2});

            return lir_operand_value(result, expr->type);

        case HIR_EXPR_CALL: {
            size_t operand_count = expr->call.args.len + 1;
            LirOperand *operands = arena_alloc(lower->function->arena, sizeof(LirOperand) * operand_count);

            operands[0] = lir_operand_symbol(expr->call.function, expr->call.function->type);

            for (size_t i = 0; i < expr->call.args.len; i++) {
                operands[i + 1] = lir_lower_expr(lower, ((HirExpr **)expr->call.args.data)[i]);
            }

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CALL, expr->type, (Array){.data = operands, .len = operand_count});

            if (expr->type->kind == HIR_TYPE_BUILTIN && expr->type->builtin == BUILTIN_NONE) {
                return lir_operand_invalid();
            }

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_CAST: {
            LirOperand operand = lir_lower_expr(lower, expr->cast.operand);

            LirValueId result = lir_emit(lower->function, lower->block, LIR_OP_CAST, expr->type, (Array){.data = &operand, .len = 1});

            return lir_operand_value(result, expr->type);
        }

        case HIR_EXPR_INDEX: {
            LirPlace place = lir_lower_place(lower, expr);
            return lir_lower_load(lower, place);
        }

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

            if (object_type->kind == HIR_TYPE_POINTER) {
                base = lir_lower_expr(lower, expr->index.object);
            } else {
                LirPlace object = lir_lower_place(lower, expr->index.object);
                base = object.address;
            }

            LirOperand index = lir_lower_expr(lower, expr->index.index);
            HirType *element_type = expr->type;

            LirOperand scale = hir_type_is_signed_integer(index.type)
                ? lir_operand_int((int64_t)element_type->size, index.type)
                : lir_operand_uint((uint64_t)element_type->size, index.type);

            LirOperand mul_operands[2] = {
                index,
                scale,
            };

            LirValueId offset = lir_emit(lower->function, lower->block, LIR_OP_MUL, index.type, (Array){.data = mul_operands, .len = 2});

            LirOperand add_operands[2] = {
                base,
                lir_operand_value(offset, index.type),
            };

            LirValueId address = lir_emit(lower->function, lower->block, LIR_OP_ADDR_ADD, base.type, (Array){.data = add_operands, .len = 2});

            return (LirPlace){
                .address = lir_operand_value(address, base.type),
                .type = element_type,
            };
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

static void lir_lower_stmt(LirLower *lower, HirStmt *stmt);

static void lir_lower_if(LirLower *lower, HirStmt *stmt) {
    LirFunction *function = lower->function;
    LirBlockId entry_block = lower->block;
    LirOperand condition = lir_lower_expr(lower, stmt->_if.cond);

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
    Array(LirOperand) args = array_create(lower->function->arena, sizeof(LirOperand));

    lir_collect_branch_args(lower, &loop->bindings, &args);
    lir_jump(lower->function, lower->block, loop->exit, args);
}

static void lir_lower_continue(LirLower *lower) {
    assert(lower->loops.len != 0);

    LirLoop *loop = &((LirLoop *)lower->loops.data)[lower->loops.len - 1];
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

    Array(LirOperand) exit_args = array_create(function->arena, sizeof(LirOperand));
    lir_collect_branch_args(lower, &snapshot, &exit_args);

    lir_branch(function, header, condition, body, (Array){0}, exit, exit_args);

    LirLoop loop = {
        .header = header,
        .exit = exit,
        .bindings = snapshot,
    };

    array_push(&lower->loops, &loop);

    lower->block = body;
    lower->bindings.len = bindings_len;
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

static void lir_lower_stmt(LirLower *lower, HirStmt *stmt) {
    if (lir_lower_block_terminated(lower)) {
        return;
    }

    switch (stmt->kind) {
        case HIR_STMT_BLOCK:
            for (size_t i = 0; i < stmt->block.stmts.len; i++) {
                lir_lower_stmt(lower, ((HirStmt **)stmt->block.stmts.data)[i]);

                if (lir_lower_block_terminated(lower)) {
                    break;
                }
            }
            return;

        case HIR_STMT_ASSIGN: {
            HirExpr *target = stmt->assign.target;
            LirOperand value = lir_lower_expr(lower, stmt->assign.value);

            if (target->kind == HIR_EXPR_VALUE) {
                Symbol *symbol = target->value.symbol;

                assert(symbol->kind == SYMBOL_LOCAL || symbol->kind == SYMBOL_PARAMETER);

                lir_bind(lower, symbol, value);
                return;
            }

            LirPlace place = lir_lower_place(lower, target);
            lir_lower_store(lower, place, value);
            return;
        }

        case HIR_STMT_EXPR:
            (void)lir_lower_expr(lower, stmt->expr);
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
        .bindings = array_create(module->arena, sizeof(LirBinding)),
        .loops = array_create(module->arena, sizeof(LirLoop))
    };

    lower.block = lir_block_create(function);

    for (size_t i = 0; i < hir->params.len; i++) {
        HirParam *param = &((HirParam *)hir->params.data)[i];

        LirValueId value = lir_function_add_param(function, param->symbol, param->type);

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