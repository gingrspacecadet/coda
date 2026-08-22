#include "source.h"

void source_build_lines(Source *source, Arena *arena) {
    source->line_offsets =
        array_create(arena, sizeof(size_t));

    size_t zero = 0;
    array_push(&source->line_offsets, &zero);

    for (size_t i = 0; i < source->contents.length; ++i) {
        if (source->contents.data[i] == '\n') {
            size_t next = i + 1;
            array_push(&source->line_offsets, &next);
        }
    }
}

size_t source_line(const Source *source, size_t offset) {
    size_t lo = 0;
    size_t hi = source->line_offsets.len;

    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;

        const size_t *line =
            array_at((Array *)&source->line_offsets, mid);

        if (*line <= offset)
            lo = mid;
        else
            hi = mid;
    }

    return lo + 1;
}

size_t source_column(const Source *source, size_t offset) {
    size_t line = source_line(source, offset);

    const size_t *line_start =
        array_at(
            (Array *)&source->line_offsets,
            line - 1
        );

    return offset - *line_start + 1;
}
