#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "parser.h"

static Arena *arena;
static char *src;

Scope *scope_new(Scope *parent) {
    Scope *s = arena_alloc(arena, sizeof(Scope));
    s->parent = parent;
    s->symbols = NULL; s->sym_count = 0;
    return s;
}

Symbol *scope_lookup_local(Scope *sc, char *name) {
    for (size i = 0; i < sc->sym_count; i++) {
        if (strcmp(sc->symbols[i]->name, name) == 0) return sc->symbols[i];
    }

    return NULL;
}

Symbol *scope_lookup(Scope *sc, char *name) {
    while (sc != NULL) {
        Symbol *s = scope_lookup_local(sc, name);
        if (s) return s;
        sc = sc->parent;
    }

    return NULL;
}

bool scope_insert(Scope *sc, Symbol *sym) {
    if (scope_lookup_local(sc, sym->name) != NULL) {
        return false;   // Dulplicate in same scope
    }
    sc->symbols[sc->sym_count++] = sym;
    return true;
}

static void error(Span span, char *msg, ...) {
    va_list args;
    va_start(args, msg);
    fprintf(stderr, "Sema error: ");
    vfprintf(stderr, msg, args);
    putc('\n', stderr);
    exit(1);
}

void module_insert_decls(Module *m) {
    m->module_scope = scope_new(NULL);

    for (size i = 0; i < m->decl_count; i++) {
        Decl *decl = m->decls[i];
        Symbol *sym = arena_alloc(arena, sizeof(Symbol));

        if (decl->kind == DECL_FN) {
            char *name = decl->fn->name;
            sym->decl = decl;
            sym->flags = SYMBOL_FLAG_FUNCTION | (decl->fn->is_export ? SYMBOL_FLAG_EXPORT : 0);
            if (!scope_insert(m->module_scope, sym)) {
                error(decl->span, "Duplicate top-level symbol '%s'", name);
            } else {
                decl->symbol = sym;
            }
        }
        else if (decl->kind == DECL_VAR) {
            char *name = decl->var->name;
            sym->flags = SYMBOL_FLAG_VARIABLE;
            if (!scope_insert(m->module_scope, sym)) {
                error(decl->span, "Duplicate top-level symbol '%s'", name);
            } else {
                decl->symbol = sym;
            }
        }
        else if (decl->kind == DECL_STRUCT) {
            char *name = decl->strct->name;
            sym->flags = SYMBOL_FLAG_TYPE;
            if (!scope_insert(m->module_scope, sym)) {
                error(decl->span, "Duplicate top-level symbol '%s'", name);
            } else {
                decl->symbol = sym;
            }
        }
        else {
            error(decl->span, "Unknown declaration type");
        }
    }
}

Module *load_module_file(char *path) {
    FILE *f = fopen(path, "r");
    fseek(f, 0, SEEK_END);
    size size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';

    LexerContext lctx = lexer(buf);
    Arena *a = arena_create();

    Module *m = parser(&lctx.tokens, a);
    arena_destroy(a);

    return m;
}

void module_resolve_includes(Module *m) {
    for (size_t i = 0; i < m->include_count; i++) {
        
    }
}