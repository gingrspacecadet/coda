#ifndef SOURCE_H
#define SOURCE_H

#include "string.h"
#include <stddef.h>

typedef struct {
    String path;
    String contents;
} Source;

typedef struct {
    Source *source;
    size_t offset, length;
    size_t line, column;
} Span;

#endif