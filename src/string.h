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
        return string.data[string.length];
    }

    return string.data[index];
}

static String string_make(char *cstr) {
    return (String){
        .data = cstr,
        .length = strlen(cstr)
    };
}

static size_t string_cmp(String a, String b) {
    return strncmp(a.data, b.data, a.length);
}

static bool string_find(String str, String needle) {
    char *found = memmem(str.data, str.length, needle.data, needle.length);
    return found ? true : false;
}

#endif