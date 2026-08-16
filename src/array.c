#include "array.h"

void array_init(Array *array, Arena *arena, size_t T) {
    array->data = arena_calloc(arena, T);
    array->T = T;
    array->len = 0;
    array->cap = 1;
    array->alive = true;
    array->arena = arena;
}

void array_push(Array *v, void *item) {
    if (!v->alive) {
        fprintf(stderr, "array_push: uninitialised array\n");
        exit(1);
    }
    if (v->len == v->cap) {
        size_t old_cap = v->cap ? v->cap : 8;
        size_t new_cap = v->cap ? v->cap * 2 : 8;
        size_t old_bytes = old_cap * v->T;
        size_t new_bytes = new_cap * v->T;
        v->data = arena_realloc(v->arena, v->data, old_bytes, new_bytes);
        if (!v->data) {
            fprintf(stderr, "array_push: realloc failed\n");
            exit(1);
        }
        v->cap = new_cap;
    }
    
    void *dst = (char *)v->data + v->len * v->T;

    memcpy(dst, item, v->T);

    v->len += 1;
}

void array_append(Array *v, size_t num, void *item) {
    if (!v->alive) {
        fprintf(stderr, "array_append: uninitialised array\n");
        exit(1);
    }
    for (size_t i = 0; i < num; i++) {
        array_push(v, item);
    }
}

void array_free(Array *v) {
    if (!v->alive) {
        fprintf(stderr, "array_free: uninitialised array\n");
        exit(1);
    }
    v->len = v->cap = 0;
}

void *array_at(Array *v, size_t index) {
    if (!v->alive) {
        fprintf(stderr, "array_at: uninitialised array\n");
        exit(1);
    }

    if (index >= v->len) {
        fprintf(stderr, "array_at: index %zu out of bounds (len=%zu)\n", index, v->len);
        exit(1);
    }

    return (char *)v->data + index * v->T;
}

void array_resize(Array *v, size_t elems) {
    if (!v->alive) {
        fprintf(stderr, "array_resize: uninitialised array\n");
        exit(1);
    }
    if (elems < v->cap) return;
    v->data = arena_realloc(v->arena, v->data, v->cap * v->T, elems * v->T);
    if (!v->data) {
        fprintf(stderr, "array_resize: realloc failed\n");
        exit(1);
    }
    v->cap = elems;
}

void array_clear(Array *v) {
    if (!v->alive) {
        fprintf(stderr, "array_clear: uninitialised array\n");
        exit(1);
    }
    v->len = 0;
}
