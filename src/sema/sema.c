#include "common.h"

HirModule *sema_analyse(Sema *sema, AstModule *module, Array(String) includes) {
    sema->module = module;
    HirModule *hmod = arena_alloc(sema->arena, sizeof(HirModule));

    sema_insert_builtin_types(sema);

    module_index_scan(&sema->modules, sema->arena, includes);

    collect_decls(sema, module->decls);

    if (!sema_resolve_includes(sema, module))
        return NULL;

    for (size_t i = 0; i < module->decls.len; i++) {
        AstDecl *d = ((AstDecl **)module->decls.data)[i];
        sema_decl(sema, d);
    }

    if (diags_has_errors(sema->diags))
        return NULL;

    return hmod;
}