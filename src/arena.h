#pragma once

#include <stddef.h>

typedef struct {
    void *data;
    size_t cap, idx;
} Arena;

Arena *arena_create();
void *arena_alloc(Arena *a, size_t size);
void *arena_calloc(Arena *a, size_t size);
void arena_reset(Arena *a);
void arena_destroy(Arena *a);
