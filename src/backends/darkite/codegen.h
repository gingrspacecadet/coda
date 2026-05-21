#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "lir.h"

typedef struct {
    String str;
    uint32_t id;
} StringConstant;

INSTANTIATE(StringConstant, string_const, ARRAY_TEMPLATE)

void codegen(FILE *out, LirFunction *fn, string_const_array *string_consts);
void collect_string_constants(LirFunction *fn, string_const_array *string_consts);

#endif