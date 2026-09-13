#ifndef ARRAY_H
#define ARRAY_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "arena.h"

typedef struct {
    void *data;
    size_t T;

    size_t len;
    size_t cap;
    bool alive;
    Arena *arena;
} Array;

#define Array(T) Array

#define array_init(v, arena, T) \
    array_init_impl((v), (arena), (T), __FILE__, __LINE__)

#define array_create(arena, T) \
    array_create_impl((arena), (T), __FILE__, __LINE__)

#define array_push(v, item) \
    array_push_impl((v), (item), __FILE__, __LINE__)

#define array_append(v, num, item) \
    array_append_impl((v), (num), (item), __FILE__, __LINE__)

#define array_free(v) \
    array_free_impl((v), __FILE__, __LINE__)

#define array_at(v, index) \
    array_at_impl((v), (index), __FILE__, __LINE__)

#define array_resize(v, elems) \
    array_resize_impl((v), (elems), __FILE__, __LINE__)

#define array_clear(v) \
    array_clear_impl((v), __FILE__, __LINE__)

void array_init_impl(Array *v, Arena *arena, size_t T, const char *file, int line);
Array array_create_impl(Arena *arena, size_t T, const char *file, int line);
void array_push_impl(Array *v, void *item, const char *file, int line);
void array_append_impl(Array *v, size_t num, void *item, const char *file, int line);
void array_free_impl(Array *v, const char *file, int line);
void *array_at_impl(Array *v, size_t index, const char *file, int line);
void array_resize_impl(Array *v, size_t elems, const char *file, int line);
void array_clear_impl(Array *v, const char *file, int line);

#endif