#include "common.h"

AstConstraintItem parse_constraint_item(Parser *p) {
    AstConstraintItem item = {
        .span = p->current.span,
    };

    Checkpoint cp = checkpoint(p);

    AstFnDecl *fn = arena_calloc(p->arena, sizeof(*fn));

    if (try_parse_fn_decl(p, fn)) {
        item.kind = CONSTRAINT_METHOD;
        item.method = fn;
        return item;
    }

    restore(p, cp);

    cp = checkpoint(p);

    AstField field = parse_field(p);

    if (match(p, TK_SEMICOLON)) {
        item.kind = CONSTRAINT_FIELD;
        item.field = field;
        return item;
    }

    restore(p, cp);

    item.kind = CONSTRAINT_EXPR;
    item.expr = parse_expression(p);

    expect(p, TK_SEMICOLON);

    return item;
}

void parse_constraint_items(Parser *p, Array *items ) {
    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        AstConstraintItem item = parse_constraint_item(p);
        array_push(items, &item);
    }
}

AstConstraintDecl *parse_inline_constraint(Parser *p) {
    AstConstraintDecl *constraint = arena_calloc(p->arena, sizeof(*constraint));

    constraint->span = p->current.span;
    constraint->items = array_create(p->arena, sizeof(AstConstraintItem));

    expect(p, TK_LBRACE);

    parse_constraint_items(p, &constraint->items);

    expect(p, TK_RBRACE);

    return constraint;
}

AstConstraintRef parse_constraint_ref(Parser *p) {
    AstConstraintRef ref = {
        .span = p->current.span
    };

    if (at(p, TK_LBRACE)) {
        ref.kind = CONSTRAINTREF_INLINE;
        ref._inline = parse_inline_constraint(p);
    } else {
        ref.kind = CONSTRAINTREF_NAMED;
        ref.path = parse_path(p);
    }

    return ref;
}

AstGenericParam parse_generic_param(Parser *p) {
    AstGenericParam param = {
        .span = p->current.span,
        .constraints = array_create(p->arena, sizeof(AstConstraintRef))
    };

    param.name = parse_name(p);

    if (!match(p, TK_COLON))
        return param;

    for (;;) {
        AstConstraintRef ref = parse_constraint_ref(p);
        array_push(&param.constraints, &ref);

        if (!match(p, TK_PLUS))
            break;
    }

    return param;
}

Array parse_generic_params(Parser *p) {
    Array params = array_create(p->arena, sizeof(AstGenericParam));

    if (!match(p, TK_LT))
        return params;

    if (!at(p, TK_GT)) {
        for (;;) {
            AstGenericParam param = parse_generic_param(p);
            array_push(&params, &param);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    expect(p, TK_GT);

    return params;
}

AstField parse_field(Parser *p) {
    AstField field = {
        .span = p->current.span,
    };

    field.type = parse_type(p);
    field.name = parse_name(p);

    return field;
}

AstType *parse_struct_type(Parser *p) {
    AstType *type = arena_calloc(p->arena, sizeof(*type));

    type->kind = TYPE_STRUCT;
    type->span = p->current.span;
    type->structure.fields = array_create(p->arena, sizeof(AstField));

    advance(p); /* struct */

    expect(p, TK_LBRACE);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        AstField field = parse_field(p);
        array_push(&type->structure.fields, &field);

        expect(p, TK_SEMICOLON);
    }

    expect(p, TK_RBRACE);

    return type;
}

AstType *parse_union_type(Parser *p) {
    AstType *type = arena_calloc(p->arena, sizeof(*type));

    type->kind = TYPE_UNION;
    type->span = p->current.span;
    type->union_.fields = array_create(p->arena, sizeof(AstField));

    advance(p);

    expect(p, TK_LBRACE);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        AstField field = parse_field(p);
        array_push(&type->union_.fields, &field);

        expect(p, TK_SEMICOLON);
    }

    expect(p, TK_RBRACE);

    return type;
}

AstType *parse_enum_type(Parser *p) {
    AstType *type = arena_calloc(p->arena, sizeof(*type));

    type->kind = TYPE_ENUM;
    type->span = p->current.span;
    type->enumeration.underlying = NULL;
    type->enumeration.items =
        array_create(p->arena, sizeof(AstEnumItem));

    advance(p);

    if (match(p, TK_COLON))
        type->enumeration.underlying = parse_type(p);

    expect(p, TK_LBRACE);

    while (!at(p, TK_RBRACE) && !at(p, TK_EOF)) {
        AstEnumItem item = {
            .span = p->current.span,
            .name = parse_name(p),
            .value = NULL,
        };

        if (match(p, TK_EQ))
            item.value = parse_expression(p);

        array_push(&type->enumeration.items, &item);

        if (!match(p, TK_COMMA) && !at(p, TK_RBRACE))
            error_expected_token(p->diags, TK_COMMA, p->current.span);
    }

    expect(p, TK_RBRACE);

    return type;
}

AstType *parse_type_single(Parser *p) {
    bool mutable = match(p, TK_KW_MUT);

    AstType *base = NULL;

    if (at(p, TK_KW_FN)) {
        Token start = p->current;
        advance(p);

        AstType *ret = parse_type(p);

        expect(p, TK_LPAREN);

        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_FN;
        base->span = start.span;
        base->fn.ret = ret;
        base->fn.params =
            array_create(p->arena, sizeof(AstType *));

        if (!at(p, TK_RPAREN)) {
            for (;;) {
                AstType *param = parse_type(p);
                array_push(&base->fn.params, &param);

                if (!match(p, TK_COMMA))
                    break;
            }
        }

        expect(p, TK_RPAREN);
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

        expect(p, TK_LPAREN);

        base->splice.expr = parse_expression(p);

        expect(p, TK_RPAREN);
    } else if (at(p, TK_IDENT)) {
        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_NAMED;
        base->span = p->current.span;
        base->named.path = parse_path(p);
        base->named.args = array_create(p->arena, sizeof(AstType *));

        if (match(p, TK_LT)) {
            if (!at(p, TK_GT)) {
                for (;;) {
                    AstType *arg = parse_type(p);
                    array_push(&base->named.args, &arg);

                    if (!match(p, TK_COMMA))
                        break;
                }
            }

            expect(p, TK_GT);
        }
    } else {
        error_expected_type(p->diags, p->current.span);

        base = arena_calloc(p->arena, sizeof(*base));
        base->kind = TYPE_ERROR;
        base->span = p->current.span;
    }

    base->mutable = mutable;

    for (;;) {
        bool postfix_mut = match(p, TK_KW_MUT);

        if (match(p, TK_STAR)) {
            AstType *ptr = arena_calloc(p->arena, sizeof(*ptr));

            ptr->kind = TYPE_POINTER;
            ptr->span = p->previous.span;
            ptr->mutable = postfix_mut;
            ptr->pointer.pointee = base;
            ptr->pointer.optional = match(p, TK_QUERY);

            base = ptr;
            continue;
        }

        if (match(p, TK_LBRACK)) {
            AstType *array = arena_calloc(p->arena, sizeof(*array));

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

                expect(p, TK_RBRACK);
            }

            base = array;
            continue;
        }

        if (postfix_mut)
            error_unexpected_token(p->diags, p->current.type, p->previous.span);

        break;
    }

    return base;
}

AstType *parse_type(Parser *p) {
    AstType *left = parse_type_single(p);

    if (!match(p, TK_PIPE))
        return left;

    AstType *sum = arena_calloc(p->arena, sizeof(*sum));

    sum->kind = TYPE_SUM;
    sum->span = left->span;
    sum->sum.members = array_create(p->arena, sizeof(AstType *));

    array_push(&sum->sum.members, &left);

    do {
        AstType *member = parse_type_single(p);
        array_push(&sum->sum.members, &member);
    } while (match(p, TK_PIPE));

    return sum;
}
