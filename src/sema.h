#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "arena.h"

typedef struct {
    Module *module;

    Scope *global_scope;
    Scope *current_scope;
    FnDecl *current_function;

    Arena *arena;
} Analyser;

Analyser analyser_init(Module *m, Arena *a);

#endif