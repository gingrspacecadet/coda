#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>
#include "string.h"
#include "array.h"

typedef struct {
    String path;
    String contents;

    Array(size_t) line_offsets;
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

void source_build_lines(Source *source, Arena *arena);
size_t source_line(const Source *source, size_t offset);
size_t source_column(const Source *source, size_t offset);

#endif