#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define HASHMAP_TEMPLATE(K, V, N) \
typedef struct N##_entry N##_entry; \
struct N##_entry { \
    K key; \
    V value; \
    N##_entry *next; \
}; \
typedef struct N##_hashmap N##_hashmap; \
struct N##_hashmap { \
    N##_entry **buckets; \
    size_t cap; \
    size_t count; \
}; \
static inline N##_hashmap N##_hashmap_init(void) { \
    N##_hashmap hm = {0}; \
    hm.cap = 16; \
    hm.count = 0; \
    hm.buckets = (N##_entry**)calloc(hm.cap, sizeof(N##_entry*)); \
    return hm; \
} \
static inline void N##_hashmap_free(N##_hashmap *hm) { \
    if (!hm || !hm->buckets) return; \
    for (size_t i = 0; i < hm->cap; ++i) { \
        N##_entry *e = hm->buckets[i]; \
        while (e) { \
            N##_entry *next = e->next; \
            N##_free(e->key); \
            free(e); \
            e = next; \
        } \
    } \
    free(hm->buckets); \
    hm->buckets = NULL; \
    hm->cap = hm->count = 0; \
} \
static inline void N##_hashmap_rehash(N##_hashmap *hm, size_t newcap) { \
    N##_entry **old = hm->buckets; \
    size_t oldcap = hm->cap; \
    hm->buckets = (N##_entry**)calloc(newcap, sizeof(N##_entry*)); \
    hm->cap = newcap; \
    for (size_t i = 0; i < oldcap; ++i) { \
        N##_entry *e = old[i]; \
        while (e) { \
            N##_entry *next = e->next; \
            unsigned long h = N##_hash(&e->key) % hm->cap; \
            e->next = hm->buckets[h]; \
            hm->buckets[h] = e; \
            e = next; \
        } \
    } \
    free(old); \
} \
static inline void N##_hashmap_put(N##_hashmap *hm, const K *key, V value) { \
    if (!hm) return; \
    if (hm->count > hm->cap * 1.5) { \
        N##_hashmap_rehash(hm, hm->cap * 2); \
    } \
    unsigned long h = N##_hash(key) % hm->cap; \
    N##_entry *e = hm->buckets[h]; \
    while (e) { \
        if (N##_eq(&e->key, key)) { \
            e->value = value; \
            return; \
        } \
        e = e->next; \
    } \
    N##_entry *ne = (N##_entry*)malloc(sizeof(N##_entry)); \
    ne->key = N##_dup(key); \
    ne->value = value; \
    ne->next = hm->buckets[h]; \
    hm->buckets[h] = ne; \
    hm->count++; \
} \
static inline int N##_hashmap_get(N##_hashmap *hm, const K *key, V *out) { \
    if (!hm || !hm->buckets) return 0; \
    unsigned long h = N##_hash(key) % hm->cap; \
    N##_entry *e = hm->buckets[h]; \
    while (e) { \
        if (N##_eq(&e->key, key)) { \
            if (out) *out = e->value; \
            return 1; \
        } \
        e = e->next; \
    } \
    return 0; \
} \
static inline int N##_hashmap_remove(N##_hashmap *hm, const K *key) { \
    if (!hm || !hm->buckets) return 0; \
    unsigned long h = N##_hash(key) % hm->cap; \
    N##_entry *e = hm->buckets[h]; \
    N##_entry *prev = NULL; \
    while (e) { \
        if (N##_eq(&e->key, key)) { \
            if (prev) prev->next = e->next; else hm->buckets[h] = e->next; \
            N##_free(e->key); \
            free(e); \
            hm->count--; \
            return 1; \
        } \
        prev = e; \
        e = e->next; \
    } \
    return 0; \
} \

#endif