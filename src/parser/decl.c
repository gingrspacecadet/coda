#include "common.h"

void recover_declaration(Parser *p) {
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

bool try_parse_fn_decl(Parser *p, AstFnDecl *fn) {
    Checkpoint cp = checkpoint(p);

    if (at(p, TK_AT)) {
        fn->attrs = parse_attributes(p);
    } else {
        fn->attrs = array_create(p->arena, sizeof(AstAttribute));
    }

    bool comptime = match(p, TK_DOLLAR);

    if (!match(p, TK_KW_FN)) {
        restore(p, cp);
        return false;
    }

    fn->span = p->previous.span;
    fn->comptime = comptime;

    fn->ret = parse_type(p);

    Checkpoint name_cp = checkpoint(p);

    AstType *receiver = parse_type(p);

    if (match(p, TK_DOT)) {
        fn->receiver = receiver;
        fn->name = parse_name(p);
    } else {
        restore(p, name_cp);
        fn->receiver = NULL;
        fn->name = parse_name(p);
    }

    fn->generics = parse_generic_params(p);
    fn->params = array_create(p->arena, sizeof(AstParam));

    if (!match(p, TK_LPAREN)) {
        restore(p, cp);
        return false;
    }

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            AstParam param = parse_param(p);
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


AstParam parse_param(Parser *p) {
    AstParam param = {
        .span = p->current.span
    };

    param.type = parse_type(p);
    param.name = parse_name(p);

    return param;
}

void parse_fn_decl(Parser *p, AstFnDecl *fn, Array attrs) {
    fn->span = p->current.span;
    fn->comptime = match(p, TK_DOLLAR);
    fn->attrs = attrs;

    expect(p, TK_KW_FN);

    fn->ret = parse_type(p);
    fn->receiver = NULL;

    Checkpoint cp = checkpoint(p);

    AstType *possible_receiver = parse_type(p);

    if (match(p, TK_DOT)) {
        fn->receiver = possible_receiver;
        fn->name = parse_name(p);
    } else {
        restore(p, cp);
        fn->name = parse_name(p);
    }

    fn->generics = parse_generic_params(p);
    fn->params = array_create(p->arena, sizeof(AstParam));

    expect(p, TK_LPAREN);

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            AstParam param = parse_param(p);
            array_push(&fn->params, &param);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    expect(p, TK_RPAREN);

    if (at(p, TK_LBRACE)) {
        fn->body = parse_block(p);
    } else {
        fn->body = NULL;
        expect(p, TK_SEMICOLON);
    }
}

void parse_var_decl(Parser *p, AstVarDecl *var) {
    var->span = p->current.span;

    var->type = parse_type(p);
    var->name = parse_name(p);

    if (match(p, TK_EQ))
        var->init = parse_expression(p);
    else
        var->init = NULL;

    expect(p, TK_SEMICOLON);
}

void parse_type_decl(Parser *p, AstTypeDecl *decl) {
    decl->span = p->current.span;

    expect(p, TK_KW_TYPE);

    decl->name = parse_name(p);
    decl->generics = parse_generic_params(p);

    expect(p, TK_EQ);

    decl->type = parse_type(p);

    expect(p, TK_SEMICOLON);
}

void parse_include_decl(Parser *p, AstIncludeDecl *decl) {
    expect(p, TK_KW_INCLUDE);

    decl->span = p->current.span;

    decl->path = parse_path(p);
    decl->alias = (Path) {
        .parts = array_create(p->arena, sizeof(String))
    };

    if (match(p, TK_COLON))
        decl->alias = parse_path(p);

    expect(p, TK_SEMICOLON);
}

bool try_parse_var_decl(Parser *p, AstVarDecl *var) {
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

void parse_constraint_decl(Parser *p, AstConstraintDecl *decl) {
    decl->span = p->current.span;

    expect(p, TK_KW_CONSTRAINT);

    decl->name = parse_name(p);

    decl->items = array_create(p->arena, sizeof(AstConstraintItem));

    expect(p, TK_EQ);
    expect(p, TK_LBRACE);

    parse_constraint_items(p, &decl->items);

    expect(p, TK_RBRACE);
}

AstDecl *parse_decl(Parser *p) {
    AstDecl *decl = arena_calloc(p->arena, sizeof(*decl));

    decl->attrs = parse_attributes(p);

    switch (p->current.type) {
    case TK_KW_INCLUDE:
        decl->kind = DECL_INCLUDE;
        parse_include_decl(p, &decl->include);
        decl->span = decl->include.span;
        return decl;

    case TK_KW_TYPE:
        decl->kind = DECL_TYPE;
        parse_type_decl(p, &decl->type);
        decl->span = decl->type.span;
        return decl;

    case TK_KW_FN:
    case TK_DOLLAR:
        decl->kind = DECL_FN;
        parse_fn_decl(p, &decl->fn, decl->attrs);
        decl->span = decl->fn.span;
        return decl;

    case TK_KW_CONSTRAINT:
        decl->kind = DECL_CONSTRAINT;
        parse_constraint_decl(p, &decl->constraint);
        decl->span = decl->constraint.span;
        return decl;

    default:
        break;
    }

    Checkpoint cp = checkpoint(p);

    AstVarDecl var = {0};

    if (try_parse_var_decl(p, &var)) {
        decl->kind = DECL_VAR;
        decl->var = var;
        return decl;
    }

    restore(p, cp);

    error_expected_declaration(p->diags, p->current.span);
    recover_declaration(p);
    return NULL;
}
