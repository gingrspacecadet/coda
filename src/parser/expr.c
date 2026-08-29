#include "common.h"

int binary_binding_power(TokenType type, bool *right_assoc, AstBinaryOp *op) {
    *right_assoc = false;

    switch (type) {
    case TK_STAR:
        *op = AST_BINARY_MUL;
        return 60;

    case TK_SLASH:
        *op = AST_BINARY_DIV;
        return 60;

    case TK_PERCENT:
        *op = AST_BINARY_MOD;
        return 60;

    case TK_PLUS:
        *op = AST_BINARY_ADD;
        return 50;

    case TK_MINUS:
        *op = AST_BINARY_SUB;
        return 50;

    case TK_SHL:
        *op = AST_BINARY_SHL;
        return 45;

    case TK_SHR:
        *op = AST_BINARY_SHR;
        return 45;

    case TK_LT:
        *op = AST_BINARY_LT;
        return 40;

    case TK_LT_EQ:
        *op = AST_BINARY_LTE;
        return 40;

    case TK_GT:
        *op = AST_BINARY_GT;
        return 40;

    case TK_GT_EQ:
        *op = AST_BINARY_GTE;
        return 40;

    case TK_EQ_EQ:
        *op = AST_BINARY_EQUAL;
        return 35;

    case TK_BANG_EQ:
        *op = AST_BINARY_NOT_EQUAL;
        return 35;

    case TK_AMP:
        *op = AST_BINARY_BIT_AND;
        return 32;

    case TK_CARET:
        *op = AST_BINARY_BIT_XOR;
        return 28;

    case TK_PIPE:
        *op = AST_BINARY_BIT_OR;
        return 24;

    case TK_BANG_AMP:
        *op = AST_BINARY_NAND;
        return 24;

    case TK_TILDE_PIPE:
        *op = AST_BINARY_NOR;
        return 24;

    case TK_AMP_AMP:
        *op = AST_BINARY_LOGICAL_AND;
        return 15;

    case TK_PIPE_PIPE:
        *op = AST_BINARY_LOGICAL_OR;
        return 10;

    case TK_EQ:
        *op = AST_BINARY_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_PLUS_EQ:
        *op = AST_BINARY_ADD_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_MINUS_EQ:
        *op = AST_BINARY_SUB_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_STAR_EQ:
        *op = AST_BINARY_MUL_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_SLASH_EQ:
        *op = AST_BINARY_DIV_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_PERCENT_EQ:
        *op = AST_BINARY_MOD_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_AMP_EQ:
        *op = AST_BINARY_BIT_AND_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_PIPE_EQ:
        *op = AST_BINARY_BIT_OR_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_CARET_EQ:
        *op = AST_BINARY_BIT_XOR_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_SHL_EQ:
        *op = AST_BINARY_SHL_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_SHR_EQ:
        *op = AST_BINARY_SHR_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_BANG_AMP_EQ:
        *op = AST_BINARY_NAND_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_TILDE_PIPE_EQ:
        *op = AST_BINARY_NOR_ASSIGN;
        *right_assoc = true;
        return 5;

    default:
        return 0;
    }
}

AstExpr *parse_literal(Parser *p) {
    AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = p->current.span;
    expr->kind = AST_EXPR_LITERAL;

    switch (p->current.type) {
    case TK_NUMBER:
        expr->lit.literal.kind = number_is_float(span_to_string(p->current.span))
            ? AST_LIT_FLOAT
            : AST_LIT_INTEGER;
        break;

    case TK_STRING:
        expr->lit.literal.kind = AST_LIT_STRING;
        break;

    case TK_CHAR:
        expr->lit.literal.kind = AST_LIT_CHAR;
        break;

    case TK_KW_TRUE:
    case TK_KW_FALSE:
        expr->lit.literal.kind = AST_LIT_BOOL;
        break;

    case TK_KW_NULL:
        expr->lit.literal.kind = AST_LIT_NULL;
        break;

    default:
        expr->kind = AST_EXPR_ERROR;
        return expr;
    }

    expr->lit.literal.raw = span_to_string(p->current.span);
    advance(p);

    return expr;
}

AstExpr *parse_identifier_or_path(Parser *p) {
    AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = p->current.span;

    Path path = parse_path(p);

    if (path.parts.len == 1) {
        expr->kind = AST_EXPR_IDENT;
        expr->ident.name = (AstName){.kind = AST_NAME_IDENT, .ident = *(String *)array_at(&path.parts, 0)};
    } else {
        expr->kind = AST_EXPR_PATH;
        expr->path.path = path;
    }

    return expr;
}

bool token_to_unary(TokenType type, AstUnaryOp *op) {
    switch (type) {
    case TK_PLUS:
        *op = AST_UNARY_POS;
        return true;

    case TK_MINUS:
        *op = AST_UNARY_NEG;
        return true;

    case TK_BANG:
        *op = AST_UNARY_NOT;
        return true;

    case TK_TILDE:
        *op = AST_UNARY_BIT_NOT;
        return true;

    case TK_STAR:
        *op = AST_UNARY_DEREF;
        return true;

    case TK_AMP:
        *op = AST_UNARY_ADDRESS;
        return true;

    default:
        return false;
    }
}

AstExpr *parse_expression_prefix(Parser *p) {
    if (at(p, TK_DOLLAR)) {
        advance(p);

        AstExpr *expr = parse_expression(p);
        expr->comptime = true;

        return expr;
    }

    if (at(p, TK_NUMBER) ||
        at(p, TK_STRING) ||
        at(p, TK_CHAR) ||
        at(p, TK_KW_TRUE) ||
        at(p, TK_KW_FALSE) ||
        at(p, TK_KW_NULL)) {
        return parse_literal(p);
    }

    if (at(p, TK_IDENT))
        return parse_identifier_or_path(p);

    if (at(p, TK_POUND))
        return parse_hash_expression(p);

    if (at(p, TK_LPAREN)) {
        Checkpoint cp = checkpoint(p);
        Token start = p->current;

        advance(p);

        AstType *type = parse_type(p);

        if (type->kind != AST_TYPE_ERROR && match(p, TK_RPAREN)) {
            AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

            expr->span = start.span;
            expr->kind = AST_EXPR_CAST;
            expr->cast.type = type;
            expr->cast.operand = parse_expression_bp(p, 70);

            return expr;
        }

        restore(p, cp);

        advance(p);

        AstExpr *expr = parse_expression(p);

        expect(p, TK_RPAREN);

        return expr;
    }

    if (at(p, TK_LBRACE))
        return parse_init_expression(p);

    AstUnaryOp op;

    if (token_to_unary(p->current.type, &op)) {
        Token start = p->current;
        advance(p);

        AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

        expr->span = start.span;
        expr->kind = AST_EXPR_UNARY;
        expr->unary.op = op;
        expr->unary.operand = parse_expression_bp(p, 80);

        return expr;
    }

    if (at(p, TK_KW_FN))
        return parse_lambda_expression(p);

    error_expected_expression(p->diags, p->current.span);

    AstExpr *error = arena_calloc(p->arena, sizeof(*error));
    error->span = p->current.span;
    error->kind = AST_EXPR_ERROR;

    if (!at(p, TK_EOF))
        advance(p);

    return error;
}

AstExpr *parse_hash_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    if (match(p, TK_LPAREN)) {
        AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

        expr->span = start.span;
        expr->kind = AST_EXPR_SPLICE;
        expr->splice.expression = parse_expression(p);

        expect(p, TK_RPAREN);

        return expr;
    }

    AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = start.span;
    expr->kind = AST_EXPR_INTRINSIC;
    expr->intrinsic.name = parse_path(p);
    expr->intrinsic.args = array_create(
        p->arena,
        sizeof(AstExpr *)
    );

    if (match(p, TK_LPAREN)) {
        if (!at(p, TK_RPAREN)) {
            for (;;) {
                AstExpr *arg = parse_expression(p);
                array_push(&expr->intrinsic.args, &arg);

                if (!match(p, TK_COMMA))
                    break;
            }
        }

        expect(p, TK_RPAREN);
    }

    return expr;
}

AstExpr *parse_init_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = start.span;
    expr->kind = AST_EXPR_INIT;
    expr->init.fields = array_create(
        p->arena,
        sizeof(AstInitField)
    );

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        AstInitField field = {
            .span = p->current.span,
            .name = NULL,
        };

        if (match(p, TK_DOT)) {
            AstName name = parse_name(p);

            field.name = arena_calloc(
                p->arena,
                sizeof(*field.name)
            );

            *field.name = name;

            expect(p, TK_EQ);
        }

        field.value = parse_expression(p);

        array_push(&expr->init.fields, &field);

        if (match(p, TK_COMMA))
            continue;

        if (!expect(p, TK_RBRACE))
            error_expected_token(p->diags, TK_COMMA, p->current.span);
    }

    expect(p, TK_RBRACE);

    return expr;
}

AstExpr *parse_lambda_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    AstExpr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = start.span;
    expr->kind = AST_EXPR_LAMBDA;

    expr->lambda.generics = array_create(p->arena, sizeof(AstGenericParam));

    expr->lambda.params = array_create(p->arena, sizeof(AstParam));

    expr->lambda.ret = parse_type(p);

    expect(p, TK_LPAREN);

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            AstParam param = parse_param(p);
            array_push(&expr->lambda.params, &param);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    expect(p, TK_RPAREN);

    expr->lambda.body = parse_block(p);

    return expr;
}

bool try_parse_generic_arguments(Parser *p, Array *args) {
    Checkpoint cp = checkpoint(p);

    if (!match(p, TK_LT)) {
        restore(p, cp);
        return false;
    }

    Array parsed = array_create(p->arena, sizeof(AstType *));

    if (at(p, TK_GT)) {
        restore(p, cp);
        return false;
    }

    for (;;) {
        AstType *type = parse_type(p);

        if (type->kind == AST_TYPE_ERROR) {
            restore(p, cp);
            return false;
        }

        array_push(&parsed, &type);

        if (!match(p, TK_COMMA))
            break;
    }

    if (!match(p, TK_GT)) {
        restore(p, cp);
        return false;
    }

    if (!at(p, TK_LPAREN)) {
        restore(p, cp);
        return false;
    }

    *args = parsed;
    return true;
}

AstExpr *parse_expression_postfix(Parser *p, AstExpr *left) {
    for (;;) {
        if (match(p, TK_LPAREN)) {
            AstExpr *call = arena_calloc(p->arena, sizeof(*call));

            call->span = left->span;
            call->kind = AST_EXPR_CALL;
            call->call.callee = left;
            call->call.generic_args =
                array_create(p->arena, sizeof(AstType *));
            call->call.args =
                array_create(p->arena, sizeof(AstExpr *));

            if (!at(p, TK_RPAREN)) {
                for (;;) {
                    AstExpr *arg = parse_expression(p);
                    array_push(&call->call.args, &arg);

                    if (!match(p, TK_COMMA))
                        break;
                }
            }

            expect(p, TK_RPAREN);

            left = call;
            continue;
        }

        Array generic_args;
        if (at(p, TK_LT) &&
            try_parse_generic_arguments(p, &generic_args)) {

            expect(p, TK_LPAREN);

            AstExpr *call = arena_calloc(p->arena, sizeof(*call));

            call->span = left->span;
            call->kind = AST_EXPR_CALL;
            call->call.callee = left;
            call->call.generic_args = generic_args;
            call->call.args =
                array_create(p->arena, sizeof(AstExpr *));

            if (!at(p, TK_RPAREN)) {
                for (;;) {
                    AstExpr *arg = parse_expression(p);
                    array_push(&call->call.args, &arg);

                    if (!match(p, TK_COMMA))
                        break;
                }
            }

            expect(p, TK_RPAREN);

            left = call;
            continue;
        }

        if (match(p, TK_LBRACK)) {
            AstExpr *index = arena_calloc(p->arena, sizeof(*index));

            index->span = p->previous.span;
            index->kind = AST_EXPR_INDEX;
            index->index.object = left;
            index->index.index = parse_expression(p);

            expect(p, TK_RBRACK);

            left = index;
            continue;
        }

        if (match(p, TK_DOT)) {
            AstExpr *member = arena_calloc(p->arena, sizeof(*member));

            member->span = p->previous.span;
            member->kind = AST_EXPR_MEMBER;
            member->member.object = left;
            member->member.member = parse_name(p);

            left = member;
            continue;
        }

        if (match(p, TK_QUERY)) {
            AstExpr *bubble = arena_calloc(p->arena, sizeof(*bubble));

            bubble->span = p->previous.span;
            bubble->kind = AST_EXPR_BUBBLE;
            bubble->bubble.operand = left;

            left = bubble;
            continue;
        }

        break;
    }

    return left;
}

AstExpr *parse_expression_bp(Parser *p, int min_bp) {
    AstExpr *left = parse_expression_prefix(p);
    left = parse_expression_postfix(p, left);

    for (;;) {
        AstBinaryOp op;
        bool right_assoc;
        int bp = binary_binding_power(p->current.type, &right_assoc, &op);

        if (bp == 0 || bp <= min_bp) {
            break;
        }

        advance(p);

        int right_bp = right_assoc ? bp - 1 : bp;

        AstExpr *right = parse_expression_bp(p, right_bp);

        AstExpr *binary = arena_calloc(p->arena, sizeof(*binary));

        binary->span = left->span;
        binary->kind = AST_EXPR_BINARY;
        binary->binary.op = op;
        binary->binary.left = left;
        binary->binary.right = right;

        left = binary;
        left = parse_expression_postfix(p, left);
    }

    return left;
}

AstExpr *parse_expression(Parser *p) {
    return parse_expression_bp(p, 0);
}

