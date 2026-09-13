#include "common.h"

static String path_string(Arena *arena, Path path) {
    size_t length = 0;

    for (size_t i = 0; i < path.parts.len; i++) {
        AstName *part = (AstName *)array_at(&path.parts, i);

        if (i != 0)
            length += 2;

        length += part->ident.length;
    }

    char *data = arena_alloc(arena, length + 1);
    size_t offset = 0;

    for (size_t i = 0; i < path.parts.len; i++) {
        AstName *part = (AstName *)array_at(&path.parts, i);

        if (i != 0) {
            data[offset++] = ':';
            data[offset++] = ':';
        }

        memcpy(data + offset, part->ident.data, part->ident.length);
        offset += part->ident.length;
    }

    data[offset] = '\0';

    return (String) {
        .data = data,
        .length = length,
    };
}

void error_unknown_name(Diags *diags, String name, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNKNOWN_NAME,
        span,
        format(diags->arena, "Unknown name '%.*s'.", string_fmt(name))
    );

    diag_finish(&b);
}

void error_duplicate_symbol(Diags *diags, String name, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_DUPLICATE_SYMBOL,
        span,
        format(diags->arena, "Duplicate declaration '%.*s'.", string_fmt(name))
    );

    diag_finish(&b);
}

void error_shadowing(Diags *diags, String name, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_SHADOWING,
        span,
        format(diags->arena, "Declaration '%.*s' shadows another declaration.", string_fmt(name))
    );

    diag_finish(&b);
}

void error_unknown_type(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNKNOWN_TYPE,
        span,
        STRING("Unknown type.")
    );

    diag_finish(&b);
}

void error_type_mismatch(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_TYPE_MISMATCH,
        span,
        STRING("Type mismatch.")
    );

    diag_finish(&b);
}

void error_expected_function(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_FUNCTION,
        span,
        STRING("Expected a function.")
    );

    diag_finish(&b);
}

void error_wrong_argument_count(Diags *diags, size_t expected, size_t actual, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_WRONG_ARGUMENT_COUNT,
        span,
        format(
            diags->arena,
            "Expected %zu arguments, got %zu.",
            expected,
            actual
        )
    );

    diag_finish(&b);
}

void error_unknown_field(Diags *diags, String name, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNKNOWN_FIELD,
        span,
        format(
            diags->arena,
            "Unknown field '%.*s'.",
            string_fmt(name)
        )
    );

    diag_finish(&b);
}

void error_invalid_unary_operation(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_UNARY_OPERATION,
        span,
        STRING("Invalid unary operation.")
    );

    diag_finish(&b);
}

void error_invalid_binary_operation(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_BINARY_OPERATION,
        span,
        STRING("Invalid binary operation.")
    );

    diag_finish(&b);
}

void error_invalid_return(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_RETURN,
        span,
        STRING("Invalid return statement.")
    );

    diag_finish(&b);
}

void error_invalid_break(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_BREAK,
        span,
        STRING("Break statement is not inside a loop.")
    );

    diag_finish(&b);
}

void error_invalid_continue(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_CONTINUE,
        span,
        STRING("Continue statement is not inside a loop.")
    );

    diag_finish(&b);
}

void error_namespace_value(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_NAMESPACE_VALUE,
        span,
        STRING("Namespace cannot be used as a value.")
    );

    diag_finish(&b);
}

void error_module_not_found(Diags *diags, Path path, Span span) {
    String name = path_string(diags->arena, path);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_MODULE_NOT_FOUND,
        span,
        format(
            diags->arena,
            "Module '%.*s' not found in include paths.",
            string_fmt(name)
        )
    );

    diag_finish(&b);
}

void error_expected_type_symbol(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_TYPE,
        span,
        STRING("Expected a type.")
    );

    diag_finish(&b);
}

void error_unknown_path(Diags *diags, Path path, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNKNOWN_NAME,
        span,
        STRING("Unknown path")
    );

    diag_finish(&b);
}