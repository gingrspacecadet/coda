#ifndef ERROR_H
#define ERROR_H

#include "lexer.h"
#include "parser.h"

#define error(T, msg) _Generic((T), \
    Parser*: error_parser \
)(ctx, msg)

__attribute__((noreturn)) void error_parser(Parser *ctx, const char *msg);

#endif