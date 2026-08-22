#include "common.h"

void recover_statement(Parser *p) {
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

Stmt *stmt_new(Parser *p, StmtKind kind, Span span) {
    Stmt *stmt = arena_calloc(p->arena, sizeof(*stmt));

    stmt->span = span;
    stmt->kind = kind;

    return stmt;
}

Stmt *parse_return_stmt(Parser *p) {
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

    expect(p, TK_SEMICOLON);
    return stmt;
}

Stmt *parse_expr_stmt(Parser *p) {
    Expr *expr = parse_expression(p);

    Stmt *stmt = stmt_new(p, STMT_EXPR, expr->span);
    stmt->expr.expr = expr;

    expect(p, TK_SEMICOLON);
    return stmt;
}

Stmt *parse_var_stmt(Parser *p) {
    VarDecl *var = arena_calloc(p->arena, sizeof(*var));

    var->span = p->current.span;
    var->type = parse_type(p);
    var->name = parse_name(p);

    if (match(p, TK_EQ))
        var->init = parse_expression(p);

    Stmt *stmt = stmt_new(p, STMT_VAR, var->span);
    stmt->var.var = var;

    expect(p, TK_SEMICOLON);
    return stmt;
}

bool try_parse_var_stmt(Parser *p, Stmt **out) {
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

Stmt *parse_if_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    expect(p, TK_LPAREN);

    Expr *cond = parse_expression(p);

    expect(p, TK_RPAREN);

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

Stmt *parse_while_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    expect(p, TK_LPAREN);

    Expr *cond = parse_expression(p);

    expect(p, TK_RPAREN);

    Stmt *body = parse_statement(p);

    Stmt *stmt = stmt_new(p, STMT_WHILE, start.span);

    stmt->_while.cond = cond;
    stmt->_while.body = body;

    return stmt;
}

Stmt *parse_for_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    expect(p, TK_LPAREN);

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

    expect(p, TK_SEMICOLON);

    Expr *post = NULL;

    if (!at(p, TK_RPAREN))
        post = parse_expression(p);

    expect(p, TK_RPAREN);

    Stmt *body = parse_statement(p);

    Stmt *stmt = stmt_new(p, STMT_FOR, start.span);

    stmt->_for.init = init;
    stmt->_for.cond = cond;
    stmt->_for.post = post;
    stmt->_for.body = body;

    return stmt;
}

Stmt *parse_defer_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_DEFER, start.span);
    stmt->defer.deferred = parse_statement(p);

    return stmt;
}

Stmt *parse_break_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_BREAK, start.span);
    stmt->_break.value = NULL;

    expect(p, TK_SEMICOLON);
    return stmt;
}

Stmt *parse_continue_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_CONTINUE, start.span);
    stmt->_continue.value = NULL;

    expect(p, TK_SEMICOLON);
    return stmt;
}

Stmt *parse_block(Parser *p) {
    Token start = p->current;
    expect(p, TK_LBRACE);

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

    expect(p, TK_RBRACE);

    return stmt;
}

Pattern *parse_pattern(Parser *p) {
    Pattern *pattern = arena_calloc(p->arena, sizeof(*pattern));

    pattern->span = p->current.span;

    if (token_is_identifier(p, "_")) {
        advance(p);
        pattern->kind = PATTERN_WILDCARD;
        return pattern;
    }

    if (at(p, TK_NUMBER) ||
        at(p, TK_STRING) ||
        at(p, TK_CHAR) ||
        at(p, TK_KW_TRUE) ||
        at(p, TK_KW_FALSE) ||
        at(p, TK_KW_NULL)) {
        pattern->kind = PATTERN_LITERAL;
        pattern->literal = parse_literal(p)->lit.literal;
        return pattern;
    }

    if (at(p, TK_IDENT)) {
        AstName name = parse_name(p);

        if (at(p, TK_IDENT)) {
            pattern->kind = PATTERN_VARIANT;
            pattern->variant.name = name;
            pattern->variant.binding = parse_name(p);
            return pattern;
        }

        pattern->kind = PATTERN_BINDING;
        pattern->binding = name;
        return pattern;
    }

    pattern->kind = PATTERN_EXPR;
    pattern->expr = parse_expression(p);
    return pattern;
}

MatchCase parse_match_case(Parser *p) {
    MatchCase case_ = {
        .span = p->current.span,
    };

    case_.pattern = parse_pattern(p);
    case_.body = parse_statement(p);

    return case_;
}

Stmt *parse_match_stmt(Parser *p) {
    Token start = p->current;
    advance(p);

    Stmt *stmt = stmt_new(p, STMT_MATCH, start.span);

    stmt->match.cases =
        array_create(p->arena, sizeof(MatchCase));

    expect(p, TK_LPAREN);

    stmt->match.expr = parse_expression(p);

    expect(p, TK_RPAREN);
    expect(p, TK_LBRACE);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        size_t before = p->current.span.offset;

        MatchCase case_ = parse_match_case(p);
        array_push(&stmt->match.cases, &case_);

        if (!at(p, TK_EOF) &&
            p->current.span.offset == before) {
            recover_statement(p);
        }
    }

    expect(p, TK_RBRACE);

    return stmt;
}

Stmt *parse_statement(Parser *p) {
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
