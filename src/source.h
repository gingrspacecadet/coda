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
} Span;

static inline String span_to_string(Span s) {
    return (String){
        .data = s.source->contents.data + s.offset,
        .length = s.length
    };
}

#endif