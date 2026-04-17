#ifndef OPTIONAL_H
#define OPTIONAL_H

#include <stdbool.h>
#include "instantiate.h"

#define OPTIONAL_TEMPLATAE(T, N) \
typedef struct { \
    bool has_value; \
    T value; \
} N##_optional; \
static N##_optional N##_optional_empty = {}; \

#endif