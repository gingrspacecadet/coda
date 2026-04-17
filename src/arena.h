#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct {
    void *data;
    size_t index;
    size_t capacity;
} Arena;

Arena *arena_create();
void *arena_alloc(Arena *a, size_t size);
void *arena_calloc(Arena *a, size_t size);
void arena_clear(Arena *a);
void arena_destroy(Arena *a);
char *arena_strdup(Arena *a, const char *s);

#endif