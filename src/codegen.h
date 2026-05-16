#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "lir.h"

void codegen(FILE *out, LirFunction *fn);

#endif