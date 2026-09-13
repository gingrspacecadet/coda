#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "hir.h"
#include "diag.h"
#include "arena.h"

typedef struct {
    AstModule *module;

    Array(Scope) scopes;
    Scope global_scope;

    HirFunction *current_fn;

    ModuleIndex modules;

    Diags *diags;
    Arena *arena;
} Sema;

static inline Sema sema_create(Arena *arena, Diags *diags) {
    return (Sema){
        .module = NULL,
        .scopes = array_create(arena, sizeof(Scope)),
        .current_fn = NULL,
        .global_scope = (Scope){
            .syms = array_create(arena, sizeof(Symbol)),
            .defers = array_create(arena, sizeof(HirStmt *)),
            .loop = false,
        },
        .modules = {
            .entries = array_create(arena, sizeof(ModuleEntry)),
        },
        .diags = diags,
        .arena = arena,
    };
}

HirModule *sema_analyse(Sema *sema, AstModule *module, Array(String) includes);

#endif /* SEMA_H */