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
void analyse(Analyser *ctx);
size_t get_type_size(TypeRef *type);
void ast_pass_monomorphise(Module *mod);

#endif