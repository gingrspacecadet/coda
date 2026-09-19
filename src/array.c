#include "array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void array_init_impl(Array *array, Arena *arena, size_t T, const char *file, int line) {
    *array = (Array) {
        .data = arena_calloc(arena, T),
        .T = T,
        .len = 0,
        .cap = 1,
        .alive = true,
        .arena = arena,
    };

    if (!array->data) {
        fprintf(stderr, "array_init: calloc failed at %s:%d\n", file, line);
        exit(1);
    }
}

Array array_create_impl(Arena *arena, size_t T, const char *file, int line) {
    Array a;
    array_init_impl(&a, arena, T, file, line);
    return a;
}

void array_push_impl(Array *v, void *item, const char *file, int line) {
    if (!v || !v->alive) {
        fprintf(stderr, "array_push: uninitialised array at %s:%d\n", file, line);
        exit(1);
    }

    if (v->len == v->cap) {
        size_t old_cap = v->cap ? v->cap : 8;
        size_t new_cap = v->cap ? v->cap * 2 : 8;
        size_t old_bytes = old_cap * v->T;
        size_t new_bytes = new_cap * v->T;

        v->data = arena_realloc(v->arena, v->data, old_bytes, new_bytes);
        if (!v->data) {
            fprintf(stderr, "array_push: realloc failed at %s:%d\n", file, line);
            exit(1);
        }

        v->cap = new_cap;
    }

    void *dst = (char *)v->data + v->len * v->T;
    memcpy(dst, item, v->T);
    v->len += 1;
}

void array_append_impl(Array *v, size_t num, void *item, const char *file, int line) {
    if (!v || !v->alive) {
        fprintf(stderr, "array_append: uninitialised array at %s:%d\n", file, line);
        exit(1);
    }

    for (size_t i = 0; i < num; i++) {
        array_push_impl(v, item, file, line);
    }
}

void array_free_impl(Array *v, const char *file, int line) {
    if (!v || !v->alive) {
        fprintf(stderr, "array_free: uninitialised array at %s:%d\n", file, line);
        exit(1);
    }

    v->len = 0;
    v->cap = 0;
    v->alive = false;
}

void *array_at_impl(Array *v, size_t index, const char *file, int line) {
    if (!v || !v->alive) {
        fprintf(stderr, "array_at: uninitialised array at %s:%d\n", file, line);
        exit(1);
    }

    if (index >= v->len) {
        fprintf(stderr, "array_at: index %zu out of bounds (len=%zu) at %s:%d\n", index, v->len, file, line);
        exit(1);
    }

    return (char *)v->data + index * v->T;
}

void array_resize_impl(Array *v, size_t elems, const char *file, int line) {
    if (!v || !v->alive) {
        fprintf(stderr, "array_resize: uninitialised array at %s:%d\n", file, line);
        exit(1);
    }

    if (elems < v->cap)
        return;

    size_t old_bytes = v->cap * v->T;
    size_t new_bytes = elems * v->T;

    v->data = arena_realloc(v->arena, v->data, old_bytes, new_bytes);
    if (!v->data) {
        fprintf(stderr, "array_resize: realloc failed at %s:%d\n", file, line);
        exit(1);
    }

    v->cap = elems;
}

void array_clear_impl(Array *v, const char *file, int line) {
    if (!v || !v->alive) {
        fprintf(stderr, "array_clear: uninitialised array at %s:%d\n", file, line);
        exit(1);
    }

    v->len = 0;
}
