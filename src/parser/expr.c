#include "common.h"

int binary_binding_power(TokenType type, bool *right_assoc, BinaryOp *op) {
    *right_assoc = false;

    switch (type) {
    case TK_STAR:
        *op = BINARY_MUL;
        return 60;

    case TK_SLASH:
        *op = BINARY_DIV;
        return 60;

    case TK_PERCENT:
        *op = BINARY_MOD;
        return 60;

    case TK_PLUS:
        *op = BINARY_ADD;
        return 50;

    case TK_MINUS:
        *op = BINARY_SUB;
        return 50;

    case TK_SHL:
        *op = BINARY_SHL;
        return 45;

    case TK_SHR:
        *op = BINARY_SHR;
        return 45;

    case TK_LT:
        *op = BINARY_LT;
        return 40;

    case TK_LT_EQ:
        *op = BINARY_LTE;
        return 40;

    case TK_GT:
        *op = BINARY_GT;
        return 40;

    case TK_GT_EQ:
        *op = BINARY_GTE;
        return 40;

    case TK_EQ_EQ:
        *op = BINARY_EQUAL;
        return 35;

    case TK_BANG_EQ:
        *op = BINARY_NOT_EQUAL;
        return 35;

    case TK_AMP:
        *op = BINARY_BIT_AND;
        return 32;

    case TK_CARET:
        *op = BINARY_BIT_XOR;
        return 28;

    case TK_PIPE:
        *op = BINARY_BIT_OR;
        return 24;

    case TK_BANG_AMP:
        *op = BINARY_NAND;
        return 24;

    case TK_TILDE_PIPE:
        *op = BINARY_NOR;
        return 24;

    case TK_AMP_AMP:
        *op = BINARY_LOGICAL_AND;
        return 15;

    case TK_PIPE_PIPE:
        *op = BINARY_LOGICAL_OR;
        return 10;

    case TK_EQ:
        *op = BINARY_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_PLUS_EQ:
        *op = BINARY_ADD_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_MINUS_EQ:
        *op = BINARY_SUB_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_STAR_EQ:
        *op = BINARY_MUL_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_SLASH_EQ:
        *op = BINARY_DIV_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_PERCENT_EQ:
        *op = BINARY_MOD_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_AMP_EQ:
        *op = BINARY_BIT_AND_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_PIPE_EQ:
        *op = BINARY_BIT_OR_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_CARET_EQ:
        *op = BINARY_BIT_XOR_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_SHL_EQ:
        *op = BINARY_SHL_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_SHR_EQ:
        *op = BINARY_SHR_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_BANG_AMP_EQ:
        *op = BINARY_NAND_ASSIGN;
        *right_assoc = true;
        return 5;

    case TK_TILDE_PIPE_EQ:
        *op = BINARY_NOR_ASSIGN;
        *right_assoc = true;
        return 5;

    default:
        return 0;
    }
}

Expr *parse_literal(Parser *p) {
    Expr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = p->current.span;
    expr->kind = EXPR_LITERAL;

    switch (p->current.type) {
    case TK_NUMBER:
        expr->lit.literal.kind = number_is_float(span_to_string(p->current.span))
            ? LIT_FLOAT
            : LIT_INTEGER;
        break;

    case TK_STRING:
        expr->lit.literal.kind = LIT_STRING;
        break;

    case TK_CHAR:
        expr->lit.literal.kind = LIT_CHAR;
        break;

    case TK_KW_TRUE:
    case TK_KW_FALSE:
        expr->lit.literal.kind = LIT_BOOL;
        break;

    case TK_KW_NULL:
        expr->lit.literal.kind = LIT_NULL;
        break;

    default:
        expr->kind = EXPR_ERROR;
        return expr;
    }

    expr->lit.literal.raw = span_to_string(p->current.span);
    advance(p);

    return expr;
}

Expr *parse_identifier_or_path(Parser *p) {
    Expr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = p->current.span;

    Path path = parse_path(p);

    if (path.parts.len == 1) {
        expr->kind = EXPR_IDENT;
        expr->ident.name = (AstName){.kind = NAME_IDENT, .ident = *(String *)array_at(&path.parts, 0)};
    } else {
        expr->kind = EXPR_PATH;
        expr->path.path = path;
    }

    return expr;
}

bool token_to_unary(TokenType type, UnaryOp *op) {
    switch (type) {
    case TK_PLUS:
        *op = UNARY_POS;
        return true;

    case TK_MINUS:
        *op = UNARY_NEG;
        return true;

    case TK_BANG:
        *op = UNARY_NOT;
        return true;

    case TK_TILDE:
        *op = UNARY_BIT_NOT;
        return true;

    case TK_STAR:
        *op = UNARY_DEREF;
        return true;

    case TK_AMP:
        *op = UNARY_ADDRESS;
        return true;

    default:
        return false;
    }
}

Expr *parse_expression_prefix(Parser *p) {
    if (at(p, TK_DOLLAR)) {
        advance(p);

        Expr *expr = parse_expression(p);
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

        Type *type = parse_type(p);

        if (type->kind != TYPE_ERROR && match(p, TK_RPAREN)) {
            Expr *expr = arena_calloc(p->arena, sizeof(*expr));

            expr->span = start.span;
            expr->kind = EXPR_CAST;
            expr->cast.type = type;
            expr->cast.operand = parse_expression_bp(p, 70);

            return expr;
        }

        restore(p, cp);

        advance(p);

        Expr *expr = parse_expression(p);

        expect(p, TK_RPAREN);

        return expr;
    }

    if (at(p, TK_LBRACE))
        return parse_init_expression(p);

    UnaryOp op;

    if (token_to_unary(p->current.type, &op)) {
        Token start = p->current;
        advance(p);

        Expr *expr = arena_calloc(p->arena, sizeof(*expr));

        expr->span = start.span;
        expr->kind = EXPR_UNARY;
        expr->unary.op = op;
        expr->unary.operand = parse_expression_bp(p, 80);

        return expr;
    }

    if (at(p, TK_KW_FN))
        return parse_lambda_expression(p);

    error_expected_expression(p->diags, p->current.span);

    Expr *error = arena_calloc(p->arena, sizeof(*error));
    error->span = p->current.span;
    error->kind = EXPR_ERROR;

    if (!at(p, TK_EOF))
        advance(p);

    return error;
}

Expr *parse_hash_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    if (match(p, TK_LPAREN)) {
        Expr *expr = arena_calloc(p->arena, sizeof(*expr));

        expr->span = start.span;
        expr->kind = EXPR_SPLICE;
        expr->splice.expression = parse_expression(p);

        expect(p, TK_RPAREN);

        return expr;
    }

    Expr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = start.span;
    expr->kind = EXPR_INTRINSIC;
    expr->intrinsic.name = parse_path(p);
    expr->intrinsic.args = array_create(
        p->arena,
        sizeof(Expr *)
    );

    if (match(p, TK_LPAREN)) {
        if (!at(p, TK_RPAREN)) {
            for (;;) {
                Expr *arg = parse_expression(p);
                array_push(&expr->intrinsic.args, &arg);

                if (!match(p, TK_COMMA))
                    break;
            }
        }

        expect(p, TK_RPAREN);
    }

    return expr;
}

Expr *parse_init_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    Expr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = start.span;
    expr->kind = EXPR_INIT;
    expr->init.fields = array_create(
        p->arena,
        sizeof(InitField)
    );

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        InitField field = {
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

Expr *parse_lambda_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    Expr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = start.span;
    expr->kind = EXPR_LAMBDA;

    expr->lambda.generics = array_create(p->arena, sizeof(GenericParam));

    expr->lambda.params = array_create(p->arena, sizeof(Param));

    expr->lambda.ret = parse_type(p);

    expect(p, TK_LPAREN);

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            Param param = parse_param(p);
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

    Array parsed = array_create(p->arena, sizeof(Type *));

    if (at(p, TK_GT)) {
        restore(p, cp);
        return false;
    }

    for (;;) {
        Type *type = parse_type(p);

        if (type->kind == TYPE_ERROR) {
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

Expr *parse_expression_postfix(Parser *p, Expr *left) {
    for (;;) {
        if (match(p, TK_LPAREN)) {
            Expr *call = arena_calloc(p->arena, sizeof(*call));

            call->span = left->span;
            call->kind = EXPR_CALL;
            call->call.callee = left;
            call->call.generic_args =
                array_create(p->arena, sizeof(Type *));
            call->call.args =
                array_create(p->arena, sizeof(Expr *));

            if (!at(p, TK_RPAREN)) {
                for (;;) {
                    Expr *arg = parse_expression(p);
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

            Expr *call = arena_calloc(p->arena, sizeof(*call));

            call->span = left->span;
            call->kind = EXPR_CALL;
            call->call.callee = left;
            call->call.generic_args = generic_args;
            call->call.args =
                array_create(p->arena, sizeof(Expr *));

            if (!at(p, TK_RPAREN)) {
                for (;;) {
                    Expr *arg = parse_expression(p);
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
            Expr *index = arena_calloc(p->arena, sizeof(*index));

            index->span = p->previous.span;
            index->kind = EXPR_INDEX;
            index->index.object = left;
            index->index.index = parse_expression(p);

            expect(p, TK_RBRACK);

            left = index;
            continue;
        }

        if (match(p, TK_DOT)) {
            Expr *member = arena_calloc(p->arena, sizeof(*member));

            member->span = p->previous.span;
            member->kind = EXPR_MEMBER;
            member->member.object = left;
            member->member.member = parse_name(p);

            left = member;
            continue;
        }

        if (match(p, TK_QUERY)) {
            Expr *bubble = arena_calloc(p->arena, sizeof(*bubble));

            bubble->span = p->previous.span;
            bubble->kind = EXPR_BUBBLE;
            bubble->bubble.operand = left;

            left = bubble;
            continue;
        }

        break;
    }

    return left;
}

Expr *parse_expression_bp(Parser *p, int min_bp) {
    Expr *left = parse_expression_prefix(p);
    left = parse_expression_postfix(p, left);

    for (;;) {
        BinaryOp op;
        bool right_assoc;
        int bp = binary_binding_power(p->current.type, &right_assoc, &op);

        if (bp == 0 || bp <= min_bp) {
            break;
        }

        advance(p);

        int right_bp = right_assoc ? bp - 1 : bp;

        Expr *right = parse_expression_bp(p, right_bp);

        Expr *binary = arena_calloc(p->arena, sizeof(*binary));

        binary->span = left->span;
        binary->kind = EXPR_BINARY;
        binary->binary.op = op;
        binary->binary.left = left;
        binary->binary.right = right;

        left = binary;
        left = parse_expression_postfix(p, left);
    }

    return left;
}

Expr *parse_expression(Parser *p) {
    return parse_expression_bp(p, 0);
}

