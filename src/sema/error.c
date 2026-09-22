#include "common.h"

static String sema_type_string(Arena *arena, HirType *type) {
    if (type == NULL)
        return STRING("<unknown>");

    switch (type->kind) {
        case HIR_TYPE_ERROR:
            return STRING("<error>");

        case HIR_TYPE_BUILTIN:
            switch (type->builtin) {
                case BUILTIN_UINT8:  return STRING("uint8");
                case BUILTIN_UINT16: return STRING("uint16");
                case BUILTIN_UINT32: return STRING("uint32");
                case BUILTIN_UINT64: return STRING("uint64");
                case BUILTIN_INT8:   return STRING("int8");
                case BUILTIN_INT16:  return STRING("int16");
                case BUILTIN_INT32:  return STRING("int32");
                case BUILTIN_INT64:  return STRING("int64");
                case BUILTIN_BOOL:   return STRING("bool");
                case BUILTIN_NONE:   return STRING("none");
            }
            break;

        case HIR_TYPE_NAMED:
            return type->named.symbol->name.ident;

        case HIR_TYPE_POINTER: {
            String pointee = sema_type_string(arena, type->pointer.pointee);
            return format(
                arena,
                "%.*s*%s",
                string_fmt(pointee),
                type->pointer.optional ? "?" : ""
            );
        }

        case HIR_TYPE_SLICE: {
            String element = sema_type_string(arena, type->slice.element);
            return format(arena, "%.*s[]", string_fmt(element));
        }

        case HIR_TYPE_ARRAY: {
            String element = sema_type_string(arena, type->array.element);
            return format(
                arena,
                "%.*s[%zu]",
                string_fmt(element),
                type->array.length
            );
        }

        case HIR_TYPE_FUNCTION:
            // TODO: print function parameters
            return STRING("function");

        case HIR_TYPE_SUM:
            return STRING("sum");

        case HIR_TYPE_STRUCT:
            return STRING("struct");

        case HIR_TYPE_UNION:
            return STRING("union");

        case HIR_TYPE_ENUM:
            return STRING("enum");
    }

    return STRING("<unknown>");
}

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
        format(
            diags->arena,
            "Unknown name '%.*s'.",
            string_fmt(name)
        )
    );

    diag_finish(&b);
}

void error_unknown_path(Diags *diags, Path path, Span span) {
    String name = path_string(diags->arena, path);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNKNOWN_NAME,
        span,
        format(
            diags->arena,
            "Unknown name '%.*s'.",
            string_fmt(name)
        )
    );

    diag_finish(&b);
}

void error_duplicate_symbol(Diags *diags, String name, Span span, Span previous) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_DUPLICATE_SYMBOL,
        span,
        format(
            diags->arena,
            "Duplicate declaration '%.*s'.",
            string_fmt(name)
        )
    );

    diag_label(
        &b,
        previous,
        STRING("previous declaration is here")
    );

    diag_finish(&b);
}

void error_shadowing(Diags *diags, String name, Span span, Span previous) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_SHADOWING,
        span,
        format(
            diags->arena,
            "Declaration '%.*s' shadows another declaration.",
            string_fmt(name)
        )
    );

    diag_label(
        &b,
        previous,
        STRING("shadowed declaration is here")
    );

    diag_help(
        &b,
        STRING("rename this declaration or use the existing name")
    );

    diag_finish(&b);
}

void error_unknown_type(Diags *diags, Path path, Span span) {
    String name = path_string(diags->arena, path);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNKNOWN_TYPE,
        span,
        format(
            diags->arena,
            "Unknown type '%.*s'.",
            string_fmt(name)
        )
    );

    diag_finish(&b);
}

void error_expected_type_symbol(Diags *diags, String name, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_TYPE,
        span,
        format(
            diags->arena,
            "'%.*s' does not name a type.",
            string_fmt(name)
        )
    );

    diag_finish(&b);
}

void error_type_mismatch(Diags *diags, HirType *expected, HirType *found, Span span) {
    String expected_name = sema_type_string(diags->arena, expected);
    String found_name = sema_type_string(diags->arena, found);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_TYPE_MISMATCH,
        span,
        format(
            diags->arena,
            "Type mismatch: expected '%.*s', found '%.*s'.",
            string_fmt(expected_name),
            string_fmt(found_name)
        )
    );

    diag_finish(&b);
}

void error_expected_function(Diags *diags, HirType *found, Span span) {
    String found_name = sema_type_string(diags->arena, found);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_FUNCTION,
        span,
        format(
            diags->arena,
            "Expected a function, found '%.*s'.",
            string_fmt(found_name)
        )
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
            "Wrong number of arguments: expected %zu, found %zu.",
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

void error_invalid_unary_operation(Diags *diags, String operation, HirType *operand, Span span) {
    String operand_name = sema_type_string(diags->arena, operand);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_UNARY_OPERATION,
        span,
        format(
            diags->arena,
            "Invalid unary operation '%.*s' for type '%.*s'.",
            string_fmt(operation),
            string_fmt(operand_name)
        )
    );

    diag_finish(&b);
}

void error_invalid_binary_operation(Diags *diags, String operation, HirType *left, HirType *right, Span span) {
    String left_name = sema_type_string(diags->arena, left);
    String right_name = sema_type_string(diags->arena, right);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_BINARY_OPERATION,
        span,
        format(
            diags->arena,
            "Invalid binary operation '%.*s' for types '%.*s' and '%.*s'.",
            string_fmt(operation),
            string_fmt(left_name),
            string_fmt(right_name)
        )
    );

    diag_finish(&b);
}

void error_missing_return_value(Diags *diags, HirType *expected, Span span) {
    String expected_name = sema_type_string(diags->arena, expected);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_RETURN,
        span,
        format(
            diags->arena,
            "Return value required; function returns '%.*s'.",
            string_fmt(expected_name)
        )
    );

    diag_finish(&b);
}

void error_invalid_return(Diags *diags, HirType *expected, HirType *found, Span span) {
    String expected_name = sema_type_string(diags->arena, expected);
    String found_name = sema_type_string(diags->arena, found);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_RETURN,
        span,
        format(
            diags->arena,
            "Invalid return value: expected '%.*s', found '%.*s'.",
            string_fmt(expected_name),
            string_fmt(found_name)
        )
    );

    diag_finish(&b);
}

void error_invalid_break(Diags *diags, size_t level, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_BREAK,
        span,
        level == 1
            ? STRING("Break statement is not inside a loop.")
            : format(
                diags->arena,
                "Cannot break %zu levels; there are not enough enclosing loops.",
                level
            )
    );

    diag_finish(&b);
}

void error_invalid_continue(Diags *diags, size_t level, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_INVALID_CONTINUE,
        span,
        level == 1
            ? STRING("Continue statement is not inside a loop.")
            : format(
                diags->arena,
                "Cannot continue %zu levels; there are not enough enclosing loops.",
                level
            )
    );

    diag_finish(&b);
}

void error_namespace_value(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_NAMESPACE_VALUE,
        span,
        STRING("A namespace cannot be used as a value.")
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

void error_not_mutable(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_NOT_MUTABLE,
        span,
        STRING("(TMP) This is not mutable.")
    );

    diag_finish(&b);
}