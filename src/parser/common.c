#include "common.h"

Path parse_path(Parser *p) {
    Path path = {
        .parts = array_create(p->arena, sizeof(String))
    };

    if (!expect(p, TK_IDENT))
        return path;

    String part = span_to_string(p->previous.span);
    array_push(&path.parts, &part);

    while (match(p, TK_COLON_COLON)) {
        if (!expect(p, TK_IDENT))
            break;

        part = span_to_string(p->previous.span);
        array_push(&path.parts, &part);
    }

    return path;
}

AstAttribute parse_attribute(Parser *p) {
    AstAttribute attr = {
        .span = p->current.span,
        .args = array_create(p->arena, sizeof(AstExpr *))
    };

    advance(p);

    if (!expect(p, TK_IDENT))
        return attr;

    attr.name = span_to_string(p->previous.span);

    if (!match(p, TK_LPAREN))
        return attr;

    if (!at(p, TK_RPAREN)) {
        for (;;) {
            AstExpr *expr = parse_expression(p);
            array_push(&attr.args, &expr);

            if (!match(p, TK_COMMA))
                break;
        }
    }

    expect(p, TK_RPAREN);

    return attr;
}

Array parse_attributes(Parser *p) {
    Array attrs = array_create(p->arena, sizeof(AstAttribute));

    while (at(p, TK_AT)) {
        AstAttribute attr = parse_attribute(p);
        array_push(&attrs, &attr);
    }

    return attrs;
}

AstName parse_name(Parser *p) {
    AstName name = {
        .kind = AST_NAME_IDENT
    };

    if (at(p, TK_IDENT)) {
        name.ident = span_to_string(p->current.span);
        advance(p);
        return name;
    }

    if (at(p, TK_POUND)) {
        name.kind = AST_NAME_SPLICE;

        advance(p);

        if (!expect(p, TK_LPAREN)) {
            return name;
        }

        name.splice = parse_expression(p);

        expect(p, TK_RPAREN);
        return name;
    }

    error_expected_identifier(p->diags, p->current.span);
    return name;
}


bool token_is_identifier(Parser *p, const char *text) {
    if (!at(p, TK_IDENT))
        return false;

    String s = span_to_string(p->current.span);

    size_t n = strlen(text);

    return s.length == n &&
           memcmp(s.data, text, n) == 0;
}
