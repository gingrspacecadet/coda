#include "arena.h"
#include <stdlib.h>
#include <string.h>

Arena *arena_create() {
    Arena *a = malloc(sizeof(Arena));
    *a = (Arena){
        .data = malloc(128),
        .index = 0,
        .capacity = 128,
    };
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    if (size + a->index >= a->capacity) {
        a->capacity *= 2;
        a->data = realloc(a->data, a->capacity);
    }

    void *data = (char*)a->data + a->index;
    a->index += size;
    return data;
}

void *arena_calloc(Arena *a, size_t size) {
    void *data = arena_alloc(a, size);
    memset(data, 0, size);
    return data;
}


void arena_clear(Arena *a) {
    a->index = 0;
}

void arena_destroy(Arena *a) {
    free(a->data);
    free(a);
}

char *arena_strdup(Arena *a, char *s) {
    size_t n = strlen(s) + 1;
    char *dst = arena_alloc(a, n);
    memcpy(dst, s, n);
    return dst;
}