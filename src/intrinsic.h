#ifndef CODA_INTRINSIC_H
#define CODA_INTRINSIC_H

#include "ast.h"

typedef enum {
    INTRINSIC_SIZEOF,
    INTRINSIC_ALIGNOF,
    INTRINSIC_OFFSETOF,
} IntrinsicId;

typedef struct {
    const char *name;
    IntrinsicId id;
    const AstIntrinsicArgKind *arg_kinds;
    size_t arg_count;
} IntrinsicDefinition;

const IntrinsicDefinition *intrinsic_lookup(Path name);

#endif
