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

    Diags *diags;
    Arena *arena;
} Sema;

Sema *sema_create(Arena *arena, Diags *diags);

HirModule *sema_analyse(Sema *sema, AstModule *module);

#endif /* SEMA_H */