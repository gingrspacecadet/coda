#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <string.h>
#include <stdbool.h>

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

static String string_make(char *cstr) {
    return (String){
        .data = cstr,
        .length = strlen(cstr)
    };
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

#endif