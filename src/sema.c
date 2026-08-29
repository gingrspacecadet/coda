#include "sema.h"

static uint64_t hash_string(String s) {
    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t i = 0; i < s.length; ++i) {
        hash ^= (unsigned char)s.data[i];
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static void scope_init(Sema *sema, Scope *scope, size_t cap) {
    scope->cap = cap;
    scope->len = 0;

    scope->entries = arena_calloc(sema->arena, sizeof(*scope->entries) * cap);
}

static bool scope_grow(Sema *sema, Scope *scope) {
    size_t old_cap = scope->cap;
    size_t new_cap = old_cap ? old_cap * 2 : 64;

    ScopeEntry *old_entries = scope->entries;

    scope->entries = arena_calloc(sema->arena, sizeof(*scope->entries) * new_cap);

    scope->cap = new_cap;
    scope->len = 0;

    for (size_t i = 0; i < old_cap; ++i) {
        Symbol *symbol = old_entries[i].symbol;

        if (symbol == NULL)
            continue;

        size_t index = hash_string(symbol->name) % scope->cap;

        while (scope->entries[index].symbol != NULL)
            index = (index + 1) % scope->cap;

        scope->entries[index].symbol = symbol;
        scope->len++;
    }

    return true;
}

static Symbol *scope_lookup(const Scope *scope, String name) {
    if (scope->cap == 0)
        return NULL;

    size_t index = hash_string(name) % scope->cap;

    for (;;) {
        Symbol *symbol = scope->entries[index].symbol;

        if (symbol == NULL)
            return NULL;

        if (string_eq(symbol->name, name))
            return symbol;

        index = (index + 1) % scope->cap;
    }
}

static bool scope_insert(Sema *sema, Scope *scope, Symbol *symbol) {
    if (scope->cap == 0)
        scope_init(sema, scope, 64);

    if ((scope->len + 1) * 10 >= scope->cap * 7)
        scope_grow(sema, scope);

    size_t index = hash_string(symbol->name) % scope->cap;

    while (scope->entries[index].symbol != NULL) {
        Symbol *existing = scope->entries[index].symbol;

        if (string_eq(existing->name, symbol->name))
            return false;

        index = (index + 1) % scope->cap;
    }

    scope->entries[index].symbol = symbol;
    scope->len++;

    return true;
}

static Symbol *symbol_create(Sema *sema, SymbolKind kind, String name, AstDecl *decl) {
    Symbol *symbol = arena_calloc(sema->arena, sizeof(*symbol));

    symbol->kind = kind;
    symbol->name = name;
    symbol->decl = decl;

    return symbol;
}

static String decl_name(const AstDecl *decl) {
    switch (decl->kind) {
    case AST_DECL_TYPE:
        if (decl->type.name.kind == AST_NAME_IDENT)
            return decl->type.name.ident;
        break;

    case AST_DECL_FN:
        if (decl->fn.name.kind == AST_NAME_IDENT)
            return decl->fn.name.ident;
        break;

    case AST_DECL_VAR:
        if (decl->var.name.kind == AST_NAME_IDENT)
            return decl->var.name.ident;
        break;

    case AST_DECL_CONSTRAINT:
        if (decl->constraint.name.kind == AST_NAME_IDENT)
            return decl->constraint.name.ident;
        break;

    case AST_DECL_INCLUDE:
    case AST_DECL_ERROR:
        break;
    }

    return (String){0};
}

static SymbolKind decl_symbol_kind(const AstDecl *decl) {
    switch (decl->kind) {
    case AST_DECL_TYPE:
        return SYMBOL_TYPE;

    case AST_DECL_FN:
        return SYMBOL_FUNCTION;

    case AST_DECL_VAR:
        return SYMBOL_GLOBAL;

    case AST_DECL_CONSTRAINT:
        return SYMBOL_CONSTRAINT;

    case AST_DECL_INCLUDE:
    case AST_DECL_ERROR:
        return SYMBOL_INVALID;
    }

    return SYMBOL_INVALID;
}

static void collect_decl(Sema *sema, AstDecl *decl) {
    SymbolKind kind = decl_symbol_kind(decl);

    if (kind == SYMBOL_INVALID)
        return;

    String name = decl_name(decl);

    if (name.length == 0)
        return;

    Symbol *symbol = symbol_create(sema, kind, name, decl);

    if (!scope_insert(sema, &sema->module_scope, symbol)) {
        DiagBuilder b = diag_begin(sema->diags, DIAG_ERROR, 2000, decl->span, string_make("Duplicate declaration"));

        diag_finish(&b);
        return;
    }
}

Sema *sema_create(Arena *arena, Diags *diags) {
    Sema *sema = arena_calloc(arena, sizeof(*sema));

    sema->arena = arena;
    sema->diags = diags;
    sema->hir = NULL;

    scope_init(sema, &sema->module_scope, 64);

    return sema;
}

static void collect_module_symbols(Sema *sema, AstModule *module) {
    for (size_t i = 0; i < module->decls.len; ++i) {
        AstDecl **decl = array_at(
            &module->decls,
            i
        );
 
        collect_decl(sema, *decl);
    }
}

HirModule *sema_analyse(Sema *sema, AstModule *module) {
    collect_module_symbols(sema, module);

    /*
     * HIR construction comes next.
     */
    return sema->hir;
}