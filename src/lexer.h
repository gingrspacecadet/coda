#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "string.h"
#include "source.h"
#include "diag.h"
#include "token.h"

typedef struct {
    Source *source;
    size_t index;

    Diags *diags;
} Lexer;

Token lexer_next(Lexer *ctx);

#endif