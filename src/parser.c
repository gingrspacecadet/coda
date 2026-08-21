#include "parser.h"

static Type *parse_type(Parser *p);
static Expr *parse_expression(Parser *p);
static Expr *parse_expression_bp(Parser *p, int min_bp);
static Expr *parse_expression_prefix(Parser *p);
static Expr *parse_expression_postfix(Parser *p, Expr *left);

static Stmt *parse_statement(Parser *p);
static Stmt *parse_block(Parser *p);

static Param parse_param(Parser *p);
static Field parse_field(Parser *p);

static ConstraintItem parse_constraint_item(Parser *p);
static ConstraintDecl *parse_inline_constraint(Parser *p);

static Type *parse_struct_type(Parser *p);
static Type *parse_union_type(Parser *p);
static Type *parse_enum_type(Parser *p);

static void parse_constraint_decl(Parser *p, ConstraintDecl *decl);

static bool number_is_float(String s) {
    for (size_t i = 0; i < s.length; i++) {
        if (s.data[i] == '.' || s.data[i] == 'e' || s.data[i] == 'E')
            return true;
    }

    return false;
}

typedef struct {
    size_t lexer_index;

    Token current;
    Token previous;

    size_t diagnostic_count;
} Checkpoint;

static Checkpoint checkpoint(Parser *p) {
    return (Checkpoint) {
        .lexer_index = p->lexer->index,

        .current = p->current,
        .previous = p->previous,

        .diagnostic_count = p->diags->diags.len,
    };
}

static void restore(Parser *p, Checkpoint cp) {
    p->lexer->index = cp.lexer_index;

    p->current = cp.current;
    p->previous = cp.previous;

    p->diags->diags.len = cp.diagnostic_count;
}

static void advance(Parser *p) {
    p->previous = p->current;
    p->current = lexer_next(p->lexer);
}

static bool at(Parser *p, TokenType type) {
    return p->current.type == type;
}

static bool match(Parser *p, TokenType type) {
    if (!at(p, type))
        return false;

    advance(p);
    return true;
}

static bool expect(Parser *p, TokenType type, void (*error)(Span span)) {
    if (match(p, type))
        return true;

    error(p->current.span);
    return false;
}

static void recover_declaration(Parser *p) {
    while (!at(p, TK_EOF)) {
        switch (p->current.type) {
        case TK_KW_INCLUDE:
        case TK_KW_TYPE:
        case TK_KW_FN:
        case TK_KW_CONSTRAINT:
        case TK_RBRACE:
            return;

        default:
            advance(p);
            break;
        }
    }
}

static void recover_statement(Parser *p) {
    while (!at(p, TK_EOF)) {
        if (match(p, TK_SEMICOLON))
            return;

        switch (p->current.type) {
        case TK_RBRACE:
        case TK_KW_IF:
        case TK_KW_FOR:
        case TK_KW_WHILE:
        case TK_KW_MATCH:
        case TK_KW_RETURN:
        case TK_KW_BREAK:
        case TK_KW_CONTINUE:
        case TK_KW_DEFER:
            return;

        default:
            advance(p);
            break;
        }
    }
}

static Path parse_path(Parser *p) {
    Path path = {
        .parts = array_create(p->arena, sizeof(String))
    };

    if (!at(p, TK_IDENT)) {
        error_expected_identifier(p->current.span);
        return path;
    }

    String part = span_to_string(p->current.span);
    array_push(&path.parts, &part);
    advance(p);

    while (match(p, TK_COLON_COLON)) {
        if (!at(p, TK_IDENT)) {
            error_expected_identifier(p->current.span);
            break;
        }

        part = span_to_string(p->current.span);
        array_push(&path.parts, &part);
        advance(p);
    }

    return path;
}

static Attribute parse_attribute(Parser *p) {
    Attribute attr = {
        .span = p->current.span,
        .args = array_create(p->arena, sizeof(Expr *))
    };

    advance(p); /* @ */

    if (!at(p, TK_IDENT)) {
        error_expected_identifier(p->current.span);
        return attr;
    }

    attr.name = span_to_string(p->current.span);
    advance(p);

    if (!match(p, TK_LPAREN))
        return attr;

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            Expr *expr = parse_expression(p);
            array_push(&attr.args, &expr);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    if (!match(p, TK_RPAREN))
        error_missing_rparen(p->current.span);

    return attr;
}

static Array parse_attributes(Parser *p) {
    Array attrs = array_create(p->arena, sizeof(Attribute));

    while (at(p, TK_AT)) {
        Attribute attr = parse_attribute(p);
        array_push(&attrs, &attr);
    }

    return attrs;
}

static int binary_binding_power(TokenType type, bool *right_assoc, BinaryOp *op) {
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

static Expr *parse_literal(Parser *p) {
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

static Expr *parse_identifier_or_path(Parser *p) {
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

static Expr *parse_expression_prefix(Parser *p) {
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
            expr->cast.operand = parse_expression(p, 70);

            return expr;
        }

        restore(p, cp);

        advance(p);

        Expr *expr = parse_expression(p);

        if (!match(p, TK_RPAREN))
            error_missing_rparen(p->current.span);

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
        expr->unary.operand = parse_expression(p, 80);

        return expr;
    }

    if (at(p, TK_KW_FN))
        return parse_lambda_expression(p);

    error_expected_expression(p->current.span);

    Expr *error = arena_calloc(p->arena, sizeof(*error));
    error->span = p->current.span;
    error->kind = EXPR_ERROR;

    if (!at(p, TK_EOF))
        advance(p);

    return error;
}

static Expr *parse_hash_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    if (match(p, TK_LPAREN)) {
        Expr *expr = arena_calloc(p->arena, sizeof(*expr));

        expr->span = start.span;
        expr->kind = EXPR_SPLICE;
        expr->splice.expression = parse_expression(p);

        if (!match(p, TK_RPAREN))
            error_missing_rparen(p->current.span);

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

        if (!match(p, TK_RPAREN))
            error_missing_rparen(p->current.span);
    }

    return expr;
}

static Expr *parse_init_expression(Parser *p) {
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

            if (!match(p, TK_EQ))
                error_missing_equals(p->current.span);
        }

        field.value = parse_expression(p);

        array_push(&expr->init.fields, &field);

        if (match(p, TK_COMMA))
            continue;

        if (!at(p, TK_RBRACE))
            error_expected_token(TK_COMMA, p->current.span);
    }

    if (!match(p, TK_RBRACE))
        error_missing_rbrace(p->current.span);

    return expr;
}

static Expr *parse_lambda_expression(Parser *p) {
    Token start = p->current;
    advance(p);

    Expr *expr = arena_calloc(p->arena, sizeof(*expr));

    expr->span = start.span;
    expr->kind = EXPR_LAMBDA;

    expr->lambda.generics = array_create(p->arena, sizeof(GenericParam));

    expr->lambda.params = array_create(p->arena, sizeof(Param));

    expr->lambda.ret = parse_type(p);

    expect(p, TK_LPAREN, error_missing_lparen);

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            Param param = parse_param(p);
            array_push(&expr->lambda.params, &param);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    expect(p, TK_RPAREN, error_missing_rparen);

    expr->lambda.body = parse_block(p);

    return expr;
}

static bool try_parse_generic_arguments(Parser *p, Array *args) {
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

static Expr *parse_expression_postfix(Parser *p, Expr *left) {
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

            if (!match(p, TK_RPAREN))
                error_missing_rparen(p->current.span);

            left = call;
            continue;
        }

        Array generic_args;
        if (at(p, TK_LT) &&
            try_parse_generic_arguments(p, &generic_args)) {

            expect(p, TK_LPAREN, error_missing_lparen);

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

            if (!match(p, TK_RPAREN))
                error_missing_rparen(p->current.span);

            left = call;
            continue;
        }

        if (match(p, TK_LBRACK)) {
            Expr *index = arena_calloc(p->arena, sizeof(*index));

            index->span = p->previous.span;
            index->kind = EXPR_INDEX;
            index->index.object = left;
            index->index.index = parse_expression(p);

            if (!match(p, TK_RBRACK))
                error_missing_rbracket(p->current.span);

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

static Expr *parse_expression_bp(Parser *p, int min_bp) {
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

static Expr *parse_expression(Parser *p) {
    return parse_expression_bp(p, 0);
}

static Stmt *stmt_new(Parser *p, StmtKind kind, Span span) {
    Stmt *stmt = arena_calloc(p->arena, sizeof(*stmt));

    stmt->span = span;
    stmt->kind = kind;

    return stmt;
}

static Stmt *parse_return_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_RETURN, start.span);

    if (!at(p, TK_SEMICOLON) &&
        !at(p, TK_RBRACE) &&
        !at(p, TK_EOF)) {
        stmt->_return.value = parse_expression(p);
    } else {
        stmt->_return.value = NULL;
    }

    expect(p, TK_SEMICOLON, error_missing_semi);
    return stmt;
}

static Stmt *parse_expr_stmt(Parser *p) {
    Expr *expr = parse_expression(p);

    Stmt *stmt = stmt_new(p, STMT_EXPR, expr->span);
    stmt->expr.expr = expr;

    expect(p, TK_SEMICOLON, error_missing_semi);
    return stmt;
}

static Stmt *parse_var_stmt(Parser *p) {
    VarDecl *var = arena_calloc(p->arena, sizeof(*var));

    var->span = p->current.span;
    var->type = parse_type(p);
    var->name = parse_name(p);

    if (match(p, TK_EQ))
        var->init = parse_expression(p);

    Stmt *stmt = stmt_new(p, STMT_VAR, var->span);
    stmt->var.var = var;

    expect(p, TK_SEMICOLON, error_missing_semi);
    return stmt;
}

static bool try_parse_var_stmt(Parser *p, Stmt **out) {
    Checkpoint cp = checkpoint(p);

    VarDecl *var = arena_calloc(p->arena, sizeof(*var));

    var->span = p->current.span;
    var->type = parse_type(p);

    if (var->type->kind == TYPE_ERROR) {
        restore(p, cp);
        return false;
    }

    var->name = parse_name(p);

    if (var->name.kind != NAME_IDENT) {
        restore(p, cp);
        return false;
    }

    if (match(p, TK_EQ))
        var->init = parse_expression(p);

    if (!match(p, TK_SEMICOLON)) {
        restore(p, cp);
        return false;
    }

    Stmt *stmt = stmt_new(p, STMT_VAR, var->span);
    stmt->var.var = var;

    *out = stmt;
    return true;
}

static Stmt *parse_if_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    expect(p, TK_LPAREN, error_missing_lparen);

    Expr *cond = parse_expression(p);

    expect(p, TK_RPAREN, error_missing_rparen);

    Stmt *then = parse_statement(p);

    Stmt *else_branch = NULL;

    if (match(p, TK_KW_ELSE))
        else_branch = parse_statement(p);

    Stmt *stmt = stmt_new(p, STMT_IF, start.span);

    stmt->_if.cond = cond;
    stmt->_if.then = then;
    stmt->_if._else = else_branch;

    return stmt;
}

static Stmt *parse_while_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    expect(p, TK_LPAREN, error_missing_lparen);

    Expr *cond = parse_expression(p);

    expect(p, TK_RPAREN, error_missing_rparen);

    Stmt *body = parse_statement(p);

    Stmt *stmt = stmt_new(p, STMT_WHILE, start.span);

    stmt->_while.cond = cond;
    stmt->_while.body = body;

    return stmt;
}

static Stmt *parse_for_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    expect(p, TK_LPAREN, error_missing_lparen);

    Stmt *init = NULL;

    if (!at(p, TK_SEMICOLON)) {
        if (!try_parse_var_stmt(p, &init))
            init = parse_expr_stmt(p);
    } else {
        advance(p);
    }

    Expr *cond = NULL;

    if (!at(p, TK_SEMICOLON))
        cond = parse_expression(p);

    expect(p, TK_SEMICOLON, error_missing_semi);

    Expr *post = NULL;

    if (!at(p, TK_RPAREN))
        post = parse_expression(p);

    expect(p, TK_RPAREN, error_missing_rparen);

    Stmt *body = parse_statement(p);

    Stmt *stmt = stmt_new(p, STMT_FOR, start.span);

    stmt->_for.init = init;
    stmt->_for.cond = cond;
    stmt->_for.post = post;
    stmt->_for.body = body;

    return stmt;
}

static Stmt *parse_defer_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_DEFER, start.span);
    stmt->defer.deferred = parse_statement(p);

    return stmt;
}

static Stmt *parse_break_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_BREAK, start.span);
    stmt->_break.value = NULL;

    expect(p, TK_SEMICOLON, error_missing_semi);
    return stmt;
}

static Stmt *parse_continue_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_CONTINUE, start.span);
    stmt->_continue.value = NULL;

    expect(p, TK_SEMICOLON, error_missing_semi);
    return stmt;
}

static Stmt *parse_block(Parser *p) {
    Token start = p->current;
    expect(p, TK_LBRACE, error_missing_lbrace);

    Stmt *stmt = stmt_new(p, STMT_BLOCK, start.span);

    stmt->block.stmts = array_create(p->arena, sizeof(Stmt *));

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        size_t before = p->current.span.offset;

        Stmt *child = parse_statement(p);

        if (child != NULL)
            array_push(&stmt->block.stmts, &child);

        if (!at(p, TK_EOF) &&
            p->current.span.offset == before) {
            recover_statement(p);
        }
    }

    expect(p, TK_RBRACE, error_missing_rbrace);

    return stmt;
}

static Stmt *parse_match_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_MATCH, start.span);

    stmt->match.cases = array_create(
        p->arena,
        sizeof(MatchCase)
    );

    expect(p, TK_LPAREN, error_missing_lparen);

    stmt->match.expr = parse_expression(p);

    expect(p, TK_RPAREN, error_missing_rparen);

    expect(p, TK_LBRACE, error_missing_lbrace);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        size_t before = p->current.span.offset;

        MatchCase case_ = parse_match_case(p);

        array_push(&stmt->match.cases, &case_);

        if (!at(p, TK_EOF) &&
            p->current.span.offset == before) {
            recover_statement(p);
        }
    }

    expect(p, TK_RBRACE, error_missing_rbrace);

    return stmt;
}

static Stmt *parse_statement(Parser *p) {
    bool comptime = match(p, TK_DOLLAR);

    Stmt *stmt = NULL;

    switch (p->current.type) {
    case TK_KW_RETURN:
        stmt = parse_return_stmt(p);
        break;

    case TK_KW_IF:
        stmt = parse_if_stmt(p);
        break;

    case TK_KW_FOR:
        stmt = parse_for_stmt(p);
        break;

    case TK_KW_WHILE:
        stmt = parse_while_stmt(p);
        break;

    case TK_KW_DEFER:
        stmt = parse_defer_stmt(p);
        break;

    case TK_KW_BREAK:
        stmt = parse_break_stmt(p);
        break;

    case TK_KW_CONTINUE:
        stmt = parse_continue_stmt(p);
        break;

    case TK_KW_MATCH:
        stmt = parse_match_stmt(p);
        break;

    case TK_LBRACE:
        stmt = parse_block(p);
        break;

    default:
        break;
    }

    if (stmt == NULL) {
        if (!try_parse_var_stmt(p, &stmt)) {
            stmt = parse_expr_stmt(p);
        }
    }

    if (stmt != NULL)
        stmt->comptime = comptime;

    return stmt;
}

static bool try_parse_fn_decl(Parser *p, FnDecl *fn) {
    Checkpoint cp = checkpoint(p);

    if (at(p, TK_AT)) {
        fn->attrs = parse_attributes(p);
    } else {
        fn->attrs = array_create(p->arena, sizeof(Attribute));
    }

    bool comptime = match(p, TK_DOLLAR);

    if (!match(p, TK_KW_FN)) {
        restore(p, cp);
        return false;
    }

    /*
     * From here this is definitely a function declaration.
     */
    fn->span = p->previous.span;
    fn->comptime = comptime;

    fn->ret = parse_type(p);

    Checkpoint name_cp = checkpoint(p);

    Type *receiver = parse_type(p);

    if (match(p, TK_DOT)) {
        fn->receiver = receiver;
        fn->name = parse_name(p);
    } else {
        restore(p, name_cp);
        fn->receiver = NULL;
        fn->name = parse_name(p);
    }

    fn->generics = parse_generic_params(p);
    fn->params = array_create(p->arena, sizeof(Param));

    if (!match(p, TK_LPAREN)) {
        restore(p, cp);
        return false;
    }

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            Param param = parse_param(p);
            array_push(&fn->params, &param);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    if (!match(p, TK_RPAREN)) {
        restore(p, cp);
        return false;
    }

    if (at(p, TK_LBRACE)) {
        fn->body = parse_block(p);
    } else if (match(p, TK_SEMICOLON)) {
        fn->body = NULL;
    } else {
        restore(p, cp);
        return false;
    }

    return true;
}

static ConstraintItem parse_constraint_item(Parser *p) {
    ConstraintItem item = {
        .span = p->current.span,
    };

    Checkpoint cp = checkpoint(p);

    FnDecl *fn = arena_calloc(p->arena, sizeof(*fn));

    if (try_parse_fn_decl(p, fn)) {
        item.kind = CONSTRAINT_METHOD;
        item.method = fn;
        return item;
    }

    restore(p, cp);

    cp = checkpoint(p);

    Field field = parse_field(p);

    if (match(p, TK_SEMICOLON)) {
        item.kind = CONSTRAINT_FIELD;
        item.field = field;
        return item;
    }

    restore(p, cp);

    item.kind = CONSTRAINT_EXPR;
    item.expr = parse_expression(p);

    expect(p, TK_SEMICOLON, error_missing_semi);

    return item;
}

static void parse_constraint_items(Parser *p, Array *items ) {
    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        ConstraintItem item = parse_constraint_item(p);
        array_push(items, &item);
    }
}

static ConstraintDecl *parse_inline_constraint(Parser *p) {
    ConstraintDecl *constraint = arena_calloc(p->arena, sizeof(*constraint));

    constraint->span = p->current.span;
    constraint->items = array_create(p->arena, sizeof(ConstraintItem));

    expect(p, TK_LBRACE, error_missing_lbrace);

    parse_constraint_items(p, &constraint->items);

    expect(p, TK_RBRACE, error_missing_rbrace);

    return constraint;
}

static AstName parse_name(Parser *p) {
    AstName name = {
        .kind = NAME_IDENT
    };

    if (!at(p, TK_IDENT)) {
        error_expected_identifier(p->current.span);
        return name;
    }

    name.ident = p->current.text;
    advance(p);

    return name;
}

static ConstraintRef parse_constraint_ref(Parser *p) {
    ConstraintRef ref = {
        .span = p->current.span
    };

    if (at(p, TK_LBRACE)) {
        ref.kind = CONSTRAINT_REF_INLINE;
        ref.inline_constraint = parse_inline_constraint(p);
    } else {
        ref.kind = CONSTRAINT_REF_NAMED;
        ref.path = parse_path(p);
    }

    return ref;
}

static GenericParam parse_generic_param(Parser *p) {
    GenericParam param = {
        .span = p->current.span,
        .constraints = array_create(p->arena, sizeof(ConstraintRef))
    };

    param.name = parse_name(p);

    if (!match(p, TK_COLON))
        return param;

    for (;;) {
        ConstraintRef ref = parse_constraint_ref(p);
        array_push(&param.constraints, &ref);

        if (!match(p, TK_PLUS))
            break;
    }

    return param;
}

static Array parse_generic_params(Parser *p) {
    Array params = array_create(p->arena, sizeof(GenericParam));

    if (!match(p, TK_LT))
        return params;

    if (!at(p, TK_GT)) {
        for (;;) {
            GenericParam param = parse_generic_param(p);
            array_push(&params, &param);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    expect(p, TK_GT, error_missing_gt);

    return params;
}

static Field parse_field(Parser *p) {
    Field field = {
        .span = p->current.span,
    };

    field.type = parse_type(p);
    field.name = parse_name(p);

    return field;
}

static Type *parse_struct_type(Parser *p) {
    Type *type = arena_calloc(p->arena, sizeof(*type));

    type->kind = TYPE_STRUCT;
    type->span = p->current.span;
    type->structure.fields = array_create(p->arena, sizeof(Field));

    advance(p); /* struct */

    expect(p, TK_LBRACE, error_missing_lbrace);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        Field field = parse_field(p);
        array_push(&type->structure.fields, &field);

        if (!match(p, TK_SEMICOLON))
            error_missing_semi(p->current.span);
    }

    expect(p, TK_RBRACE, error_missing_rbrace);

    return type;
}

static Type *parse_union_type(Parser *p) {
    Type *type = arena_calloc(p->arena, sizeof(*type));

    type->kind = TYPE_UNION;
    type->span = p->current.span;
    type->union_.fields = array_create(p->arena, sizeof(Field));

    advance(p);

    expect(p, TK_LBRACE, error_missing_lbrace);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        Field field = parse_field(p);
        array_push(&type->union_.fields, &field);

        if (!match(p, TK_SEMICOLON))
            error_missing_semi(p->current.span);
    }

    expect(p, TK_RBRACE, error_missing_rbrace);

    return type;
}

static Type *parse_enum_type(Parser *p) {
    Type *type = arena_calloc(p->arena, sizeof(*type));

    type->kind = TYPE_ENUM;
    type->span = p->current.span;
    type->enumeration.underlying = NULL;
    type->enumeration.items =
        array_create(p->arena, sizeof(EnumItem));

    advance(p);

    if (match(p, TK_COLON))
        type->enumeration.underlying = parse_type(p);

    expect(p, TK_LBRACE, error_missing_lbrace);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        EnumItem item = {
            .span = p->current.span,
            .name = parse_name(p),
            .value = NULL,
        };

        if (match(p, TK_EQUAL))
            item.value = parse_expression(p);

        array_push(&type->enumeration.items, &item);

        if (!match(p, TK_COMMA) && !at(p, TK_RBRACE))
            error_expected_token(TK_COMMA, p->current.span);
    }

    expect(p, TK_RBRACE, error_missing_rbrace);

    return type;
}

static Type *parse_type_single(Parser *p) {
    bool mutable = match(p, TK_KW_MUT);

    Type *base = NULL;

    if (at(p, TK_KW_FN)) {
        Token start = p->current;
        advance(p);

        Type *ret = parse_type(p);

        expect(p, TK_LPAREN, error_missing_lparen);

        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_FN;
        base->span = start.span;
        base->type.fn.ret = ret;
        base->type.fn.params =
            array_create(p->arena, sizeof(Type *));

        if (!at(p, TK_RPAREN)) {
            for (;;) {
                Type *param = parse_type(p);
                array_push(&base->type.fn.params, &param);

                if (!match(p, TK_COMMA))
                    break;
            }
        }

        expect(p, TK_RPAREN, error_missing_rparen);
    } else if (at(p, TK_KW_STRUCT)) {
        base = parse_struct_type(p);
    } else if (at(p, TK_KW_UNION)) {
        base = parse_union_type(p);
    } else if (at(p, TK_KW_ENUM)) {
        base = parse_enum_type(p);
    } else if (at(p, TK_POUND)) {
        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_SPLICE;
        base->span = p->current.span;

        advance(p);

        expect(p, TK_LPAREN, error_missing_lparen);

        base->splice.expr = parse_expression(p);

        expect(p, TK_RPAREN, error_missing_rparen);
    } else if (at(p, TK_IDENT)) {
        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_NAMED;
        base->span = p->current.span;
        base->named.path = parse_path(p);
        base->named.args = array_create(p->arena, sizeof(Type *));

        if (match(p, TK_LT)) {
            if (!at(p, TK_GT)) {
                for (;;) {
                    Type *arg = parse_type(p);
                    array_push(&base->named.args, &arg);

                    if (!match(p, TK_COMMA))
                        break;
                }
            }

            expect(p, TK_GT, error_missing_gt);
        }
    } else {
        error_expected_type(p->current.span);

        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_ERROR;
        base->span = p->current.span;
    }

    base->mutable = mutable;

    for (;;) {
        bool postfix_mut = match(p, TK_KW_MUT);

        if (match(p, TK_STAR)) {
            Type *ptr = arena_calloc(p->arena, sizeof(*ptr));

            ptr->kind = TYPE_POINTER;
            ptr->span = p->previous.span;
            ptr->mutable = postfix_mut;
            ptr->pointer.pointee = base;
            ptr->pointer.optional = match(p, TK_QUERY);

            base = ptr;
            continue;
        }

        if (match(p, TK_LBRACK)) {
            Type *array = arena_calloc(p->arena, sizeof(*array));

            array->kind = TYPE_ARRAY;
            array->span = p->previous.span;
            array->mutable = postfix_mut;
            array->array.element = base;

            if (match(p, TK_RBRACK)) {
                array->array.sized = false;
                array->array.length = NULL;
            } else {
                array->array.sized = true;
                array->array.length = parse_expression(p);

                expect(p, TK_RBRACK, error_missing_rbracket);
            }

            base = array;
            continue;
        }

        if (postfix_mut)
            error_unexpected_token(p->previous.span);

        break;
    }

    return base;
}

static Type *parse_type(Parser *p) {
    Type *left = parse_type_single(p);

    if (!match(p, TK_PIPE))
        return left;

    Type *sum = arena_calloc(p->arena, sizeof(*sum));

    sum->kind = TYPE_SUM;
    sum->span = left->span;
    sum->sum.members = array_create(p->arena, sizeof(Type *));

    array_push(&sum->sum.members, &left);

    do {
        Type *member = parse_type_single(p);
        array_push(&sum->sum.members, &member);
    } while (match(p, TK_PIPE));

    return sum;
}

static Param parse_param(Parser *p) {
    Param param = {
        .span = p->current.span
    };

    param.type = parse_type(p);
    param.name = parse_name(p);

    return param;
}

static void parse_fn_decl(Parser *p, FnDecl *fn, Array attrs) {
    fn->span = p->current.span;
    fn->comptime = match(p, TK_DOLLAR);
    fn->attrs = attrs;

    expect(p, TK_KW_FN, error_expected_fn);

    fn->ret = parse_type(p);
    fn->receiver = NULL;

    Checkpoint cp = checkpoint(p);

    Type *possible_receiver = parse_type(p);

    if (match(p, TK_DOT)) {
        fn->receiver = possible_receiver;
        fn->name = parse_name(p);
    } else {
        restore(p, cp);
        fn->name = parse_name(p);
    }

    fn->generics = parse_generic_params(p);
    fn->params = array_create(p->arena, sizeof(Param));

    expect(p, TK_LPAREN, error_missing_lparen);

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            Param param = parse_param(p);
            array_push(&fn->params, &param);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    expect(p, TK_RPAREN, error_missing_rparen);

    if (at(p, TK_LBRACE)) {
        fn->body = parse_block(p);
    } else {
        fn->body = NULL;
        expect(p, TK_SEMICOLON, error_missing_semi);
    }
}

static void parse_var_decl(Parser *p, VarDecl *var) {
    var->span = p->current.span;

    var->type = parse_type(p);
    var->name = parse_name(p);

    if (match(p, TK_EQUAL))
        var->init = parse_expression(p);
    else
        var->init = NULL;

    expect(p, TK_SEMICOLON, error_missing_semi);
}

static void parse_type_decl(Parser *p, TypeDecl *decl) {
    decl->span = p->current.span;

    expect(p, TK_KW_TYPE, error_expected_type);

    decl->name = parse_name(p);
    decl->generics = parse_generic_params(p);

    expect(p, TK_EQ, error_missing_equals);

    decl->type = parse_type(p);

    expect(p, TK_SEMICOLON, error_missing_semi);
}

static void parse_include_decl(Parser *p, IncludeDecl *decl) {
    expect(p, TK_KW_INCLUDE, error_expected_include);

    decl->path = parse_path(p);
    decl->alias = (String) {0};

    if (match(p, TK_COLON))
        decl->alias = parse_path(p);

    expect(p, TK_SEMICOLON, error_missing_semi);
}

static bool try_parse_var_decl(Parser *p, VarDecl *var) {
    Checkpoint cp = checkpoint(p);

    var->span = p->current.span;
    var->type = parse_type(p);

    if (var->type->kind == TYPE_ERROR) {
        restore(p, cp);
        return false;
    }

    var->name = parse_name(p);

    if (var->name.kind != NAME_IDENT) {
        restore(p, cp);
        return false;
    }

    if (match(p, TK_EQ))
        var->init = parse_expression(p);
    else
        var->init = NULL;

    if (!match(p, TK_SEMICOLON)) {
        restore(p, cp);
        return false;
    }

    return true;
}

static void parse_constraint_decl(Parser *p, ConstraintDecl *decl) {
    decl->span = p->current.span;

    expect(p, TK_KW_CONSTRAINT, error_expected_constraint);

    decl->name = parse_name(p);

    decl->items = array_create(p->arena, sizeof(ConstraintItem));

    expect(p, TK_EQ, error_missing_equals);
    expect(p, TK_LBRACE, error_missing_lbrace);

    parse_constraint_items(p, &decl->items);

    expect(p, TK_RBRACE, error_missing_rbrace);
}

static Decl *parse_decl(Parser *p) {
    Decl *decl = arena_calloc(p->arena, sizeof(*decl));

    decl->attrs = parse_attributes(p);

    switch (p->current.type) {
    case TK_KW_INCLUDE:
        decl->kind = DECL_INCLUDE;
        parse_include_decl(p, &decl->include);
        return decl;

    case TK_KW_TYPE:
        decl->kind = DECL_TYPE;
        parse_type_decl(p, &decl->type);
        return decl;

    case TK_KW_FN:
    case TK_DOLLAR:
        decl->kind = DECL_FN;
        parse_fn_decl(p, &decl->fn, decl->attrs);
        return decl;

    case TK_KW_CONSTRAINT:
        decl->kind = DECL_CONSTRAINT;
        parse_constraint_decl(p, &decl->constraint);
        return decl;

    default:
        break;
    }

    Checkpoint cp = checkpoint(p);

    VarDecl var = {0};

    if (try_parse_var_decl(p, &var)) {
        decl->kind = DECL_VAR;
        decl->var = var;
        return decl;
    }

    restore(p, cp);

    error_expected_declaration(p->current.span);
    recover_declaration(p);
    return NULL;
}

Module *parser_parse_module(Parser *p) {
    Module *module = arena_calloc(p->arena, sizeof(*module));

    module->decls = array_create(p->arena, sizeof(Decl *));
    if (!match(p, TK_KW_MODULE)) {
        error_expected_module(p->current.span);

        while (!at(p, TK_EOF) &&
            !at(p, TK_KW_MODULE)) {
            advance(p);
        }

        if (!match(p, TK_KW_MODULE))
            return module;
    }

    module->path = parse_path(p);

    if (!match(p, TK_SEMICOLON))
        error_missing_semi(p->current.span);

    while (!at(p, TK_EOF)) {
        size_t before = p->current.span.offset;

        Decl *decl = parse_decl(p);

        if (decl != NULL)
            array_push(&module->decls, &decl);

        if (!at(p, TK_EOF) &&
            p->current.span.offset == before) {
            advance(p);
        }
    }

    return module;
}