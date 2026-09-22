#include "coda.h"

void coda_compiler_init(CodaCompiler *compiler, Arena *arena, Diags *diags) {
    *compiler = (CodaCompiler) {
        .arena = arena,
        .diags = diags,
    };
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

    compiler->compilation.ast = m;

    Sema sema = sema_create(compiler->arena, compiler->diags);
    Array(String) includes = array_create(compiler->arena, sizeof(String));
    array_push(&includes, &STRING("."));

    HirModule *hm = sema_analyse(&sema, m, includes);

    if (compiler->diags->diags.len != 0) 
        return false;

    compiler->compilation.hir = hm;

    LirModule *lir = lir_lower_module(compiler->arena, hm);
    compiler->compilation.lir = lir;

    return true;
}