#ifndef CODA_LIR_LOWER_H
#define CODA_LIR_LOWER_H

#include "lir.h"

LirModule *lir_lower_module(Arena *arena, HirModule *hir);

#endif