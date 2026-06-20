#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#define string_fmt(S) (int)(S).length, (S).data

typedef struct {
    char *data;
    size_t length;
} String;

static char string_at(String string, size_t index) {
    if (index >= string.length) {
        return '\0';
    }
    return string.data[index];
}

#define string_make(cstr) (String){ \
        .data = (cstr), \
        .length = (__builtin_constant_p(__builtin_strlen(cstr)) \
                   ? (sizeof(cstr) - 1) \
                   : strlen(cstr)) \
    }

static bool string_eq(String a, String b) {
    if (a.length != b.length) return false;
    return memcmp(a.data, b.data, a.length) == 0;
}

static int string_cmp(String a, String b) {
    if (a.length != b.length) return (a.length < b.length) ? -1 : 1;
    return memcmp(a.data, b.data, a.length);
}

static bool string_find(String str, String needle) {
    if (needle.length > str.length) return false;
    char *found = memmem(str.data, str.length, needle.data, needle.length);
    return found != NULL;
}

static String string_copy(String str) {
    String s = { .length = str.length };
    s.data = malloc(str.length);
    if (!s.data) {
        fprintf(stderr, "OOM\n");
        exit(1);
    }
    strncpy(s.data, str.data, str.length);
    return s;
}

static char *string_unmake(String s) {
    char *p = malloc(s.length + 1);
    if (!p) {
        fprintf(stderr, "OOM\n");
        exit(1);
    }
    strncpy(p, s.data, s.length);
    p[s.length] = 0;
    return p;
}

#endif