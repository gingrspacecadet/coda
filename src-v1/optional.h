#ifndef OPTIONAL_H
#define OPTIONAL_H

#include <stdbool.h>
#include "instantiate.h"

#define OPTIONAL_TEMPLATE(T, N) \
typedef struct N##_optional { \
    bool has_value; \
    T value; \
} N##_optional; \

#endif