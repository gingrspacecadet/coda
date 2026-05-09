#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "arena.h"
#include "mir.h"

typedef struct {
    Module *module;

    Scope *global_scope;
    Scope *current_scope;
    FnDecl *current_function;

    Arena *arena;

    uint32_t temp_counter;
    MirBlock *current_block;
} Analyser;

Analyser analyser_init(Module *m, Arena *a);
void analyse(Analyser *ctx);
size_t get_type_size(TypeRef *type);
Symbol *lookup_symbol(Analyser *ctx, String name);

#endif