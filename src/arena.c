#include <stdlib.h>
#include <string.h>
#include "arena.h"

Arena *arena_create() {
    Arena *a = malloc(sizeof(Arena));
    *a = (Arena){
        .data = malloc(1024),
        .cap = 1024,
        .idx = 0,
    };
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    if (size + a->idx >= a->cap) {
        a->data = realloc(a->data, a->cap + 1024);
        a->cap += 1024;
        return arena_alloc(a, size);
    }

    void *res = a->data + a->idx;
    a->idx += size;
    return res;
}

void *arena_calloc(Arena *a, size_t size) {
    void *res = arena_alloc(a, size);
    memset(res, 0, size);
    return res;
}

void arena_reset(Arena *a) {
    a->idx = 0;
}

void arena_destroy(Arena *a) {
    free(a->data);
    free(a);
}