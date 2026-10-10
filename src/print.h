#ifndef PRINT_H
#define PRINT_H

#include <stdio.h>
#include "ast.h"
#include "hir.h"
#include "diag.h"

void print_ast_module(FILE *out, const AstModule *module);
void print_hir_module(FILE *out, const HirModule *module);
void print_diags(Diags *diags);

#endif