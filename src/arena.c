#include "arena.h"
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE (1024 * 1024)

Arena *arena_create() {
    Arena *a = malloc(sizeof(*a));
    if (!a)
        exit(1);

    a->block_size = BLOCK_SIZE;
    a->block_count = 1;
    a->current_index = 0;
    a->blocks = malloc(sizeof(*a->blocks));
    a->block_sizes = malloc(sizeof(*a->block_sizes));

    if (!a->blocks || !a->block_sizes) {
        free(a->blocks);
        free(a->block_sizes);
        free(a);
        exit(1);
    }

    a->blocks[0] = malloc(a->block_size);
    if (!a->blocks[0]) {
        free(a->blocks);
        free(a->block_sizes);
        free(a);
        exit(1);
    }

    a->block_sizes[0] = a->block_size;
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    size_t alignment = _Alignof(max_align_t);
    size_t padding = (alignment - a->current_index % alignment) % alignment;
    size_t block_index = a->block_count - 1;
    size_t remaining = a->block_sizes[block_index] - a->current_index;

    if (padding > remaining || size > remaining - padding) {
        size_t capacity = size > a->block_size ? size : a->block_size;
        size_t new_count = a->block_count + 1;

        void **blocks = realloc(a->blocks, new_count * sizeof(*blocks));
        if (!blocks)
            exit(1);
        a->blocks = blocks;

        size_t *block_sizes = realloc(a->block_sizes, new_count * sizeof(*block_sizes));
        if (!block_sizes)
            exit(1);
        a->block_sizes = block_sizes;

        void *block = malloc(capacity);
        if (!block)
            exit(1);

        a->blocks[a->block_count] = block;
        a->block_sizes[a->block_count] = capacity;
        a->block_count = new_count;

        a->current_index = 0;
        padding = 0;
    }

    a->current_index += padding;

    void *data = (char *)a->blocks[a->block_count - 1] + a->current_index;
    a->current_index += size;

    return data;
}

void *arena_realloc(Arena *a, void *p, size_t old_size, size_t new_size) {
    if (new_size == 0) return NULL;
    void *d = arena_alloc(a, new_size);
    size_t to_copy = old_size < new_size ? old_size : new_size;
    if (p && to_copy) memcpy(d, p, to_copy);
    return d;
}

void *arena_calloc(Arena *a, size_t size) {
    void *data = arena_alloc(a, size);
    memset(data, 0, size);
    return data;
}

void arena_clear(Arena *a) {
    a->current_index = 0;
}

char *arena_strdup(Arena *a, char *s) {
    size_t n = strlen(s) + 1;
    char *dst = arena_calloc(a, n);
    memcpy(dst, s, n);
    return dst;
}

void arena_destroy(Arena *a) {
    for (size_t i = 0; i < a->block_count; i++)
        free(a->blocks[i]);

    free(a->block_sizes);
    free(a->blocks);
    free(a);
}