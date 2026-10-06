#include "common.h"

//! TODO: hash table / binary lookup
//! TODO: report duplicated symbols
void scope_insert(Scope *scope, Symbol *sym) {
    array_push(&scope->syms, sym);
}

Symbol *scope_lookup(Scope *scope, AstName name) {
    for (size_t i = scope->syms.len; i > 0; --i) {
        Symbol *sym = (Symbol *)array_at(&scope->syms, i - 1);

        if (ast_name_equal(&sym->name, &name))
            return sym;
    }

    return NULL;
}

Symbol *sema_lookup(Sema *sema, AstName name) {
    for (size_t i = sema->scopes.len; i > 0; --i) {
        Scope *scope = (Scope *)array_at(&sema->scopes, i - 1);
        Symbol *symbol = scope_lookup(scope, name);

        if (symbol != NULL)
            return symbol;
    }

    return scope_lookup(&sema->global_scope, name);
}

Symbol *sema_lookup_path(Sema *sema, Path path) {
    if (path.parts.len == 0)
        return NULL;

    AstName *part = (AstName *)array_at(&path.parts, 0);
    Symbol *symbol = sema_lookup(sema, *part);

    if (symbol == NULL)
        return NULL;

    for (size_t i = 1; i < path.parts.len; i++) {
        if (symbol->kind != SYMBOL_NAMESPACE)
            return NULL;

        part = (AstName *)array_at(&path.parts, i);
        symbol = scope_lookup(symbol->namespace_scope, *part);

        if (symbol == NULL)
            return NULL;
    }

    return symbol;
}