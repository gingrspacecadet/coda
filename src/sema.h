#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "arena.h"

typedef struct {
    String path;
    string_array name;

    uint64_t mtime;
    uint64_t hash;

    bool is_parsed;
    Module *ast;
} ModuleEntry;

INSTANTIATE(ModuleEntry, modentry, ARRAY_TEMPLATE)

typedef struct {
    Module *module;

    Scope *global_scope;
    Scope *current_scope;
    FnDecl *current_function;
    size_t lambda_count;

    // include resolution!
    string_array include_paths;
    modentry_array module_map;

    Arena *arena;
} Analyser;

Analyser analyser_init(Module *m, Arena *a);
void analyse(Analyser *ctx);
size_t get_type_size(TypeRef *type);
void ast_pass_monomorphise(Module *mod);
void scan_dir(Analyser *ctx, char *dir_path);
void resolve_includes(Analyser *ctx, Module *mod);
void populate_module_namespaces(Analyser *ctx, Module *mod);
TypeRef *unwrap_type(TypeRef *type);

#endif