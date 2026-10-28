#include <assert.h>
#include "backends/orion/orion.h"
#include "backends/x86_64/x86_64.h"
#include "coda.h"

static const BackendApi *select_backend(const TargetInfo *target) {
    switch (target->arch) {
        case TARGET_ARCH_X86_64:
            return x86_64_backend();

        case TARGET_ARCH_ORION:
            return orion_backend();

        default:
            return NULL;
    }
}

void coda_compiler_init(CodaCompiler *compiler, Arena *arena, Diags *diags, const TargetInfo *target) {
    *compiler = (CodaCompiler) {
        .arena = arena,
        .diags = diags,
        .target = target,
        .backend = select_backend(target),
        .include_paths = array_create(arena, sizeof(String)),
        .forced_includes = array_create(arena, sizeof(AstIncludeDecl)),
    };

    if (compiler->backend != NULL)
        compiler->backend_instance = (Backend){.api = compiler->backend};
}

bool coda_compile(CodaCompiler *compiler, Source *source, CodaStage stage) {
    source_build_lines(source, compiler->arena);

    Lexer lexer = {
        .source = source,
        .diags = compiler->diags
    };

    Parser p; parser_init(&p, &lexer, compiler->arena);

    AstModule *m = parser_parse_module(&p);

    if (compiler->diags->diags.len != 0)
        return false;

    if (compiler->forced_includes.len != 0) {
        Array(AstDecl *) decls = array_create(compiler->arena, sizeof(AstDecl *));
        for (size_t i = 0; i < compiler->forced_includes.len; i++) {
            AstDecl *decl = arena_calloc(compiler->arena, sizeof(*decl));
            AstIncludeDecl include = *(AstIncludeDecl *)array_at(&compiler->forced_includes, i);
            include.span = m->span;
            *decl = (AstDecl){
                .span = m->span,
                .kind = AST_DECL_INCLUDE,
                .attrs = array_create(compiler->arena, sizeof(AstAttribute)),
                .include = include,
            };
            array_push(&decls, &decl);
        }
        for (size_t i = 0; i < m->decls.len; i++) {
            AstDecl *decl = *(AstDecl **)array_at(&m->decls, i);
            array_push(&decls, &decl);
        }
        m->decls = decls;
    }

    compiler->compilation.ast = m;

    Sema sema = sema_create(compiler->arena, compiler->diags);
    sema.target = compiler->target;

    Array(String) includes = array_create(compiler->arena, sizeof(String));
    array_push(&includes, &STRING("."));
    for (size_t i = 0; i < compiler->include_paths.len; i++) {
        String *path = array_at(&compiler->include_paths, i);
        array_push(&includes, path);
    }

    HirModule *hm = sema_analyse(&sema, m, includes);

    if (hm == NULL || diags_has_errors(compiler->diags)) {
        if (hm == NULL && !diags_has_errors(compiler->diags))
            fprintf(stderr, "Internal compiler error: semantic analysis failed without diagnostics\n");
        return false;
    }

    compiler->compilation.hir = hm;

    LirModule *lir = lir_lower_module(compiler->arena, hm);
    compiler->compilation.lir = lir;
    // lir_print(stderr, lir);

    if (stage >= CODA_STAGE_CODEGEN) {
        assert(compiler->backend != NULL);
        assert(compiler->output != NULL);

        return compiler->backend->emit(&compiler->backend_instance, compiler->target, lir, compiler->output);
    }

    return true;
}