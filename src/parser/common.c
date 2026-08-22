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

Attribute parse_attribute(Parser *p) {
    Attribute attr = {
        .span = p->current.span,
        .args = array_create(p->arena, sizeof(Expr *))
    };

    advance(p);

    if (!expect(p, TK_IDENT))
        return attr;

    attr.name = span_to_string(p->previous.span);

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

    expect(p, TK_RPAREN);

    return attr;
}

Array parse_attributes(Parser *p) {
    Array attrs = array_create(p->arena, sizeof(Attribute));

    while (at(p, TK_AT)) {
        Attribute attr = parse_attribute(p);
        array_push(&attrs, &attr);
    }

    return attrs;
}

AstName parse_name(Parser *p) {
    AstName name = {
        .kind = NAME_IDENT
    };

    if (!at(p, TK_IDENT)) {
        error_expected_identifier(p->diags, p->current.span);
        return name;
    }

    name.ident = span_to_string(p->current.span);
    advance(p);

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
