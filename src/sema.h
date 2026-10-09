#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "hir.h"
#include "diag.h"
#include "arena.h"
#include "target.h"

typedef struct {
    Symbol *generic;
    Symbol *instance;
    Array(HirType *) arguments;
} GenericInstance;

typedef struct {
    Symbol *generic;
    Symbol *instance;
    Array(HirType *) arguments;
    size_t pointer_depth;
    bool complete;
} GenericTypeInstance;

typedef struct {
    AstModule *module;

    Array(Scope) scopes;
    Scope global_scope;

    HirModule *hir_module;
    HirFunction *current_fn;

    ModuleIndex modules;

    Array(GenericInstance) generic_instances;
    size_t next_generic_instance;
    Array(GenericTypeInstance) generic_type_instances;
    size_t type_indirection_depth;

    const TargetInfo *target;

    bool comptime;

    Diags *diags;
    Arena *arena;
} Sema;

static inline Sema sema_create(Arena *arena, Diags *diags) {
    return (Sema){
        .module = NULL,
        .scopes = array_create(arena, sizeof(Scope)),
        .current_fn = NULL,
        .generic_instances = array_create(arena, sizeof(GenericInstance)),
        .next_generic_instance = 0,
        .generic_type_instances = array_create(arena, sizeof(GenericTypeInstance)),
        .type_indirection_depth = 0,
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
        .target = target_native(),
    };
}

HirModule *sema_analyse(Sema *sema, AstModule *module, Array(String) includes);

#endif /* SEMA_H */