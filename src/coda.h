#ifndef CODA_H
#define CODA_H

#include <stdbool.h>
#include <stddef.h>

#include "lir_lower.h"
#include "backend.h"
#include "parser.h"
#include "source.h"
#include "arena.h"
#include "diag.h"
#include "sema.h"

typedef enum {
    CODA_STAGE_AST,
    CODA_STAGE_HIR,
    CODA_STAGE_LIR,
    CODA_STAGE_CODEGEN,
} CodaStage;

typedef struct {
    AstModule *ast;
    HirModule *hir;
    LirModule *lir;
} CodaCompilation;

typedef struct {
    Arena *arena;
    Diags *diags;

    const TargetInfo *target;
    const BackendApi *backend;
    Backend backend_instance;
    FILE *output;

    CodaCompilation compilation;
} CodaCompiler;

void coda_compiler_init(CodaCompiler *compiler, Arena *arena, Diags *diags, const TargetInfo *target);
bool coda_compile(CodaCompiler *compiler, Source *source, CodaStage stage);

#endif