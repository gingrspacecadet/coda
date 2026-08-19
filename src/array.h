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

void array_init(Array *array, Arena *arena, size_t T);
void array_push(Array *v, void *item);
void array_append(Array *v, size_t num, void *item);
void array_free(Array *v);
void *array_at(Array *v, size_t len);
void array_resize(Array *v, size_t elems);
void array_clear(Array *v);

#endif