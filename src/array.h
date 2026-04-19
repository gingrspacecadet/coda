#ifndef ARRAY_H
#define ARRAY_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "instantiate.h"

#define ARRAY_TEMPLATE(T, N) \
typedef struct { \
    T *data; \
    size_t idx; \
    size_t cap; \
} N##_array; \
static N##_array N##_array_empty = {}; \
static inline N##_array N##_array##_init(void) { \
    N##_array v = {}; \
    v.data = (T*)calloc(1, sizeof(T)); \
    if (!v.data) { \
        fprintf(stderr, #N "_array_init: malloc failed\n"); \
        exit(1); \
    } \
    v.idx = 0; \
    v.cap = 1; \
    return v; \
} \
static inline void N##_array##_push(N##_array *v, T item) { \
    if (v->idx == v->cap) { \
        v->cap = v->cap ? v->cap * 2 : 8; \
        v->data = (T*)realloc(v->data, v->cap * sizeof(T)); \
        if (!v->data) { \
            fprintf(stderr, #N "_array_push: realloc failed\n"); \
            exit(1); \
        } \
    } \
    v->data[v->idx++] = item; \
} \
static inline void N##_array_append(N##_array *v, size_t num, T item) { \
    for (size_t i = 0; i < num; i++) { \
        N##_array_push(v, item); \
    } \
} \
static inline void N##_array##_free(N##_array *v) { \
    free(v->data); \
    v->idx = v->cap = 0; \
} \
static inline T N##_array##_at(N##_array *v, size_t idx) { \
    return v->data[idx >= v->idx ? (v->cap - 1) : idx]; \
} \
static inline void N##_array##_resize(N##_array *v, size_t elems) { \
    if (elems < v->cap) return; \
    v->data = (T*)realloc(v->data, elems * sizeof(T)); \
    if (!v->data) { \
        fprintf(stderr, #N "_array_resize: realloc failed\n"); \
        exit(1); \
    } \
    v->cap = elems; \
} \
static inline void N##_array_clear(N##_array *v) { \
    v->idx = 0; \
} \

#endif