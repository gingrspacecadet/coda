#ifndef ERROR_H
#define ERROR_H

#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <stdarg.h>

#define error(T, msg) _Generic((T), \
    Parser*: error_parser, \
    Token: error_sema \
)(T, msg)

void error_set_source(Source source);

static char *format(char *msg, ...) {
    char *buf;
    va_list args;
    va_start(args, msg);
    int n = vasprintf(&buf, msg, args);
    if (n == -1) return msg;
    else return buf;
}

__attribute__((noreturn)) void error_parser(Parser *ctx, const char *msg);
__attribute__((noreturn)) void error_sema(Token t, const char *msg);

#endif