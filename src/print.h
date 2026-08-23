#ifndef PRINT_H
#define PRINT_H

#include <stdio.h>
#include "ast.h"

void print_ast_module(FILE *out, const AstModule *module);

#endif