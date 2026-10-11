#include "intrinsic.h"

static const AstIntrinsicArgKind sizeof_args[] = {
    AST_INTRINSIC_ARG_TYPE,
};

static const AstIntrinsicArgKind alignof_args[] = {
    AST_INTRINSIC_ARG_TYPE,
};

static const AstIntrinsicArgKind offsetof_args[] = {
    AST_INTRINSIC_ARG_TYPE,
    AST_INTRINSIC_ARG_MEMBER_PATH,
};

static const IntrinsicDefinition definitions[] = {
    {"sizeof", INTRINSIC_SIZEOF, sizeof_args, sizeof(sizeof_args) / sizeof(sizeof_args[0])},
    {"alignof", INTRINSIC_ALIGNOF, alignof_args, sizeof(alignof_args) / sizeof(alignof_args[0])},
    {"offsetof", INTRINSIC_OFFSETOF, offsetof_args, sizeof(offsetof_args) / sizeof(offsetof_args[0])},
};

const IntrinsicDefinition *intrinsic_lookup(Path name) {
    if (name.parts.len != 1)
        return NULL;

    AstName *part = array_at(&name.parts, 0);
    if (part->kind != AST_NAME_IDENT)
        return NULL;

    for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); i++) {
        size_t length = strlen(definitions[i].name);
        if (part->ident.length == length && memcmp(part->ident.data, definitions[i].name, length) == 0)
            return &definitions[i];
    }

    return NULL;
}
