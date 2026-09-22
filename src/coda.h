#ifndef CODA_H
#define CODA_H

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "diag.h"
#include "lir_lower.h"
#include "parser.h"
#include "sema.h"
#include "source.h"

typedef enum {
    CODA_STAGE_AST,
    CODA_STAGE_HIR,
    CODA_STAGE_LIR,
} CodaStage;

typedef struct {
    AstModule *ast;
    HirModule *hir;
    LirModule *lir;
} CodaCompilation;

typedef struct {
    Arena *arena;
    Diags *diags;
    CodaCompilation compilation;
} CodaCompiler;

void coda_compiler_init(CodaCompiler *compiler, Arena *arena, Diags *diags);
bool coda_compile(CodaCompiler *compiler, Source *source, CodaStage stage);

#endif