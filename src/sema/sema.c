#include "common.h"

HirModule *sema_analyse(Sema *sema, AstModule *module, Array(String) includes) {
    HirModule *hmod = arena_alloc(sema->arena, sizeof(HirModule));

    *hmod = (HirModule) {
        .functions = array_create(sema->arena, sizeof(HirFunction)),
        .globals = array_create(sema->arena, sizeof(HirGlobal)),
    };

    sema->root_scope = arena_alloc(sema->arena, sizeof(*sema->root_scope));
    *sema->root_scope = (Scope){
        .syms = array_create(sema->arena, sizeof(Symbol)),
        .defers = array_create(sema->arena, sizeof(HirStmt *)),
        .loop = false,
    };
    sema->module = module;
    sema->module_scope = sema->root_scope;
    sema->hir_module = hmod;

    sema_insert_builtin_types(sema);
    module_index_scan(&sema->modules, sema->arena, includes);

    collect_decls(sema, module->decls);
    *sema->root_scope = sema->global_scope;

    if (!sema_resolve_includes(sema, module))
        return NULL;
    *sema->root_scope = sema->global_scope;

    for (size_t i = 0; i < module->decls.len; i++) {
        AstDecl *decl = *(AstDecl **)array_at(&module->decls, i);
        sema_decl(sema, decl);
    }

    if (diags_has_errors(sema->diags))
        return NULL;

    return hmod;
}