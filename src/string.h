#ifndef STRING_H
#define STRING_H

#include <stddef.h>

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

#endif