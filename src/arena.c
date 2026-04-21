#include "arena.h"
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE (1024 * 1024)

Arena *arena_create() {
    Arena *a = malloc(sizeof(Arena));
    if (!a) {
        exit(1);
    }
    a->block_size = BLOCK_SIZE;
    a->block_count = 1;
    a->blocks = malloc(sizeof(void*));
    if (!a->blocks) {
        free(a);
        exit(1);
    }
    a->blocks[0] = malloc(a->block_size);
    if (!a->blocks[0]) {
        free(a->blocks);
        free(a);
        exit(1);
    }
    a->current_index = 0;
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    if (size > a->block_size - a->current_index) {
        a->block_count++;
        a->blocks = realloc(a->blocks, a->block_count * sizeof(void*));
        if (!a->blocks) {
            exit(1);
        }
        a->blocks[a->block_count - 1] = malloc(a->block_size);
        if (!a->blocks[a->block_count - 1]) {
            exit(1);
        }
        a->current_index = 0;
    }

    void *data = (char*)a->blocks[a->block_count - 1] + a->current_index;
    a->current_index += size;
    return data;
}

void *arena_calloc(Arena *a, size_t size) {
    void *data = arena_alloc(a, size);
    memset(data, 0, size);
    return data;
}


void arena_clear(Arena *a) {
    // Not implemented, since we don't reuse
}

char *arena_strdup(Arena *a, char *s) {
    size_t n = strlen(s) + 1;
    char *dst = arena_alloc(a, n);
    memcpy(dst, s, n);
    return dst;
}

void arena_destroy(Arena *a) {
    for (size_t i = 0; i < a->block_count; i++) {
        free(a->blocks[i]);
    }
    free(a->blocks);
    free(a);
}