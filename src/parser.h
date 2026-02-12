#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

typedef struct {
    enum {
        IDENT,
        ATTR,
        KEYWORD,
        INT_LIT,
        CHAR_LIT,
        STR_LIT,
        PUNCT,
        OP,
        ASM,
        _EOF,
        BAD
    } type;
    char *data;
    size_t len;
} Token;

size_t parse(char *src, Token **out);

#endif