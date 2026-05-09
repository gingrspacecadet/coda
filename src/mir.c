#include "sema.h"
#include "hir.h"
#include "mir.h"

MirOperand make_temp(Analyser *ctx, TypeRef *type) {
    return (MirOperand){
        .type = MIR_VAL_TEMP,
        .temp = ctx->temp_counter++,
        .resolved_type = type
    };
}

MirOperand make_symbol(Symbol *sym) {
    return (MirOperand){
        .type = MIR_VAL_SYMBOL,
        .symbol = sym,
        .resolved_type = sym->type
    };
}

MirOperand make_literal(Literal lit, TypeRef *type) {
    return (MirOperand){
        .type = MIR_VAL_LIT,
        .lit = lit,
        .resolved_type = type
    };
}

MirInstr *emit(Analyser *ctx, MirOp type, MirOperand result, MirOperand lhs, MirOperand rhs) {
    MirInstr *instr = arena_calloc(ctx->arena, sizeof(MirInstr));
    instr->op = type;
    instr->result = result;
    instr->lhs = lhs;
    instr->rhs = rhs;

    if (!ctx->current_block->first) {
        ctx->current_block->first = instr;
        ctx->current_block->last = instr;
    } else {
        instr->prev = ctx->current_block->last;
        ctx->current_block->last->next = instr;
        ctx->current_block->last = instr;
    }

    return instr;
}

MirOperand null_op() {
    return (MirOperand){0};
}

MirOperand mir_lower_expr(Analyser *ctx, HirExpr *hir) {
    switch (hir->type) {
        case HIR_EXPR_LIT:
            return make_literal(hir->literal, hir->resolved_type);
        case HIR_EXPR_VAR:
            return make_symbol(hir->var.symbol);
        case HIR_EXPR_BINARY: {
            MirOperand lhs = mir_lower_expr(ctx, hir->binary.left);
            MirOperand rhs = mir_lower_expr(ctx, hir->binary.right);

            MirOperand result = make_temp(ctx, hir->resolved_type);

            MirOp op;
            switch (hir->binary.op) {   //TODO: more
                case BINOP_ADD: op = MIR_OP_ADD; break;
                case BINOP_SUB: op = MIR_OP_SUB; break;
                case BINOP_MUL: op = MIR_OP_MUL; break;
                case BINOP_DIV: op = MIR_OP_DIV; break;
            }

            emit(ctx, op, result, lhs, rhs);
            return result;
        }
        case HIR_EXPR_FIELD_OFFSET: {
            MirOperand base = mir_lower_expr(ctx, hir->field_offset.base);

            Literal offset_lit = {
                .type = LITERAL_INT,
                ._int = hir->field_offset.byte_offset,
            };
            MirOperand offset = make_literal(offset_lit, lookup_symbol(ctx, string_make("int"))->type);
            MirOperand addr_temp = make_temp(ctx, hir->resolved_type->pointer.pointee);
            emit(ctx, MIR_OP_ADD, addr_temp, base, offset);

            return addr_temp;
        }

        // TODO: call, array, etc
    }
}