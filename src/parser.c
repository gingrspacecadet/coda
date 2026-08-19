#include "parser.h"

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
        .parts = array_init(p->arena, sizeof(String))
    };

    if (!at(p, TK_IDENTIFIER)) {
        error_expected_identifier(p->current.span);
        return path;
    }

    String part = p->current.text;
    array_push(&path.parts, &part);
    advance(p);

    while (match(p, TK_DOUBLE_COLON)) {
        if (!at(p, TK_IDENTIFIER)) {
            error_expected_identifier(p->current.span);
            break;
        }

        part = p->current.text;
        array_push(&path.parts, &part);
        advance(p);
    }

    return path;
}

static Attribute parse_attribute(Parser *p) {
    Attribute attr = {
        .span = p->current.span,
        .args = array_init(p->arena, sizeof(Expr *))
    };

    advance(p); /* @ */

    if (!at(p, TK_IDENTIFIER)) {
        error_expected_identifier(p->current.span);
        return attr;
    }

    attr.name = p->current.text;
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

static ConstraintItem parse_constraint_item(Parser *p);

static void parse_constraint_items(Parser *p, Array *items ) {
    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        ConstraintItem item = parse_constraint_item(p);
        array_push(items, &item);
    }
}

static ConstraintDecl *parse_inline_constraint(Parser *p) {
    ConstraintDecl *constraint = arena_calloc(p->arena, sizeof(*constraint));

    constraint->span = p->current.span;
    constraint->items = array_init(p->arena, sizeof(ConstraintItem));

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
        .constraints = array_init(p->arena, sizeof(ConstraintRef))
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
    Array params = array_init(p->arena, sizeof(GenericParam));

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
        base->type.fn.params = array_init(p->arena, sizeof(Type *));

        if (!at(p, TK_RPAREN)) {
            for (;;) {
                Type *param = parse_type(p);
                array_push(&base->type.fn.params, &param);

                if (!match(p, TK_COMMA))
                    break;
            }
        }

        expect(p, TK_RPAREN, error_missing_rparen);
    } else if (at(p, TK_POUND)) {
        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_SPLICE;
        base->span = p->current.span;

        advance(p);

        expect(p, TK_LPAREN, error_missing_lparen);

        base->splice.expr = parse_expression(p);

        expect(p, TK_RPAREN, error_missing_rparen);
    } else {
        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_NAMED;
        base->span = p->current.span;
        base->type.named.path = parse_path(p);
        base->type.named.args = array_init(p->arena, sizeof(Type *));

        if (match(p, TK_LT)) {
            if (!at(p, TK_GT)) {
                for (;;) {
                    Type *arg = parse_type(p);
                    array_push(&base->type.named.args, &arg);

                    if (!match(p, TK_COMMA))
                        break;
                }
            }

            expect(p, TK_GT, error_missing_gt);
        }
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
            ptr->pointer.optional = false;

            if (match(p, TK_QUERY))
                ptr->pointer.optional = true;

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
    sum->sum.members = array_init(p->arena, sizeof(Type *));

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

static void parse_fn_decl(Parser *p, FnDecl *fn) {
    fn->span = p->current.span;
    fn->comptime = match(p, TK_DOLLAR);

    expect(p, TK_KW_FN, error_expected_fn);

    fn->ret = parse_type(p);
    fn->name = parse_name(p);
    fn->generics = parse_generic_params(p);

    fn->params = array_init(p->arena, sizeof(Param));

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

    if (at(p, TK_LBRACE))
        fn->body = parse_block(p);
    else {
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

static Decl *parse_decl(Parser *p) {
    Decl *decl = arena_calloc(p->arena, sizeof(*decl));

    decl->attrs = array_init(p->arena, sizeof(Attribute));

    while (at(p, TK_AT)) {
        Attribute attr = parse_attribute(p);
        array_push(&decl->attrs, &attr);
    }

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
        parse_fn_decl(p, &decl->fn);
        return decl;

    case TK_KW_CONSTRAINT:
        decl->kind = DECL_CONSTRAINT;
        parse_constraint_decl(p, &decl->constraint);
        return decl;

    default:
        error_expected_declaration(p->current.span);
        recover_declaration(p);
        return NULL;
    }
}

Module *parser_parse_module(Parser *p) {
    Module *module = arena_calloc(p->arena, sizeof(*module));

    module->decls = array_init(p->arena, sizeof(Decl *));
    if (!match(p, TK_KW_MODULE)) {
        error_expected_module(p->current.span);

        while (!at(p, TK_EOF) &&
            !at(p, TK_KW_MODULE)) {
            advance(p);
        }

        if (!match(p, TK_KW_MODULE))
            return module;
    }

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