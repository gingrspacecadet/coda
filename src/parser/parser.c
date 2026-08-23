#include "common.h"

AstModule *parser_parse_module(Parser *p) {
    AstModule *module = arena_calloc(p->arena, sizeof(*module));

    module->decls = array_create(p->arena, sizeof(AstDecl *));
    if (!match(p, TK_KW_MODULE)) {
        error_expected_module(p->diags, p->current.span);

        while (!at(p, TK_EOF) &&
            !at(p, TK_KW_MODULE)) {
            advance(p);
        }

        if (!match(p, TK_KW_MODULE))
            return module;
    }

    module->span = p->current.span;

    module->path = parse_path(p);

    expect(p, TK_SEMICOLON);

    while (!at(p, TK_EOF)) {
        size_t before = p->current.span.offset;

        AstDecl *decl = parse_decl(p);

        if (decl != NULL)
            array_push(&module->decls, &decl);

        if (!at(p, TK_EOF) &&
            p->current.span.offset == before) {
            advance(p);
        }
    }

    return module;
}