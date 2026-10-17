#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mangle.h"
#include "sema/analysis/common.h"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} MangleBuffer;

typedef struct {
    String input;
    size_t position;
} MangleCursor;

static bool buffer_reserve(MangleBuffer *buffer, size_t extra) {
    if (buffer->length == SIZE_MAX || extra > SIZE_MAX - buffer->length - 1)
        return false;

    size_t required = buffer->length + extra + 1;
    if (required <= buffer->capacity)
        return true;

    size_t capacity = buffer->capacity != 0 ? buffer->capacity : 128;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }

    char *data = realloc(buffer->data, capacity);
    if (data == NULL)
        return false;

    buffer->data = data;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_append_n(MangleBuffer *buffer, const char *text, size_t length) {
    if (length == 0)
        return true;
    if (text == NULL || !buffer_reserve(buffer, length))
        return false;

    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool buffer_append(MangleBuffer *buffer, const char *text) {
    return buffer_append_n(buffer, text, strlen(text));
}

static bool buffer_appendf(MangleBuffer *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);

    if (length < 0 || !buffer_reserve(buffer, (size_t)length)) {
        va_end(args);
        return false;
    }

    vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args);
    va_end(args);
    buffer->length += (size_t)length;
    return true;
}

static bool buffer_append_component(MangleBuffer *buffer, String string) {
    if (string.data == NULL || string.length == 0)
        return false;
    return buffer_appendf(buffer, "%zu", string.length) && buffer_append_n(buffer, string.data, string.length);
}

static String buffer_finish(Arena *arena, MangleBuffer *buffer) {
    String result = {0};
    if (buffer->data != NULL) {
        result.data = arena_calloc(arena, buffer->length + 1);
        if (result.data != NULL) {
            memcpy(result.data, buffer->data, buffer->length);
            result.data[buffer->length] = '\0';
            result.length = buffer->length;
        }
    }
    free(buffer->data);
    *buffer = (MangleBuffer){0};
    return result;
}

static void buffer_discard(MangleBuffer *buffer) {
    free(buffer->data);
    *buffer = (MangleBuffer){0};
}

static bool name_is_extern(const Symbol *symbol) {
    if (symbol == NULL || symbol->decl == NULL)
        return false;

    Array attributes = symbol->decl->attrs;
    for (size_t i = 0; i < attributes.len; i++) {
        AstAttribute *attribute = array_at(&attributes, i);
        if (string_eq(attribute->name, STRING("extern")))
            return true;
    }
    return false;
}

static bool buffer_append_module_path(MangleBuffer *buffer, AstModule *module) {
    size_t count = module != NULL ? module->path.parts.len : 0;
    for (size_t i = 0; i < count; i++) {
        AstName *part = array_at(&module->path.parts, i);
        if (part->kind != AST_NAME_IDENT || !buffer_append_component(buffer, part->ident))
            return false;
    }
    return true;
}

static bool buffer_append_symbol_path(MangleBuffer *buffer, const Symbol *symbol) {
    if (symbol == NULL || symbol->name.kind != AST_NAME_IDENT)
        return false;
    return buffer_append_module_path(buffer, symbol->owner_module) && buffer_append_component(buffer, symbol->name.ident);
}

static bool buffer_append_nominal_identity(MangleBuffer *buffer, const Symbol *symbol) {
    if (symbol == NULL || symbol->name.kind != AST_NAME_IDENT)
        return false;

    size_t module_count = symbol->owner_module != NULL ? symbol->owner_module->path.parts.len : 0;
    if (!buffer_appendf(buffer, "N%zu_", module_count + 1) ||
        !buffer_append_module_path(buffer, symbol->owner_module))
        return false;
    return buffer_append_component(buffer, symbol->name.ident);
}

static bool buffer_append_type(MangleBuffer *buffer, Sema *sema, HirType *type, size_t depth);

static bool buffer_append_type_arguments(MangleBuffer *buffer, Sema *sema, Array arguments, size_t depth) {
    if (!buffer_appendf(buffer, "_I%zu_", arguments.len))
        return false;

    for (size_t i = 0; i < arguments.len; i++) {
        HirType *argument = ((HirType **)arguments.data)[i];
        if (!buffer_append_type(buffer, sema, argument, depth + 1))
            return false;
    }
    return true;
}

static bool buffer_append_fields(MangleBuffer *buffer, Sema *sema, Array fields, bool is_union, size_t depth) {
    if (!buffer_appendf(buffer, "%c%zu_", is_union ? 'U' : 'C', fields.len))
        return false;

    for (size_t i = 0; i < fields.len; i++) {
        HirField *field = array_at(&fields, i);
        String name = {0};
        char fallback[32];
        if (field->symbol != NULL && field->symbol->name.kind == AST_NAME_IDENT) {
            name = field->symbol->name.ident;
        } else {
            int length = snprintf(fallback, sizeof(fallback), "field%zu", i);
            if (length < 0 || (size_t)length >= sizeof(fallback))
                return false;
            name = (String){.data = fallback, .length = (size_t)length};
        }

        if (!buffer_append_component(buffer, name) ||
            !buffer_append_type(buffer, sema, field->type, depth + 1))
            return false;
    }
    return true;
}

static bool buffer_append_type(MangleBuffer *buffer, Sema *sema, HirType *type, size_t depth) {
    if (type == NULL || depth > 128)
        return false;

    /* Nominal types encode identity, never their underlying layout. */
    if (type->nominal != NULL) {
        if (!buffer_append_nominal_identity(buffer, type->nominal))
            return false;
        GenericTypeInstance *instance = sema_generic_type_instance_for_type(sema, type, NULL);
        return instance == NULL || instance->arguments.len == 0 ||
               buffer_append_type_arguments(buffer, sema, instance->arguments, depth);
    }

    switch (type->kind) {
        case HIR_TYPE_ERROR:
            return false;

        case HIR_TYPE_BUILTIN: {
            static const char *names[] = {
                [BUILTIN_UINT8] = "uint8", [BUILTIN_UINT16] = "uint16",
                [BUILTIN_UINT32] = "uint32", [BUILTIN_UINT64] = "uint64",
                [BUILTIN_INT8] = "int8", [BUILTIN_INT16] = "int16",
                [BUILTIN_INT32] = "int32", [BUILTIN_INT64] = "int64",
                [BUILTIN_BOOL] = "bool", [BUILTIN_NONE] = "none",
                [BUILTIN_FLOAT32] = "float32", [BUILTIN_FLOAT64] = "float64",
            };
            if ((size_t)type->builtin >= sizeof(names) / sizeof(*names) || names[type->builtin] == NULL)
                return false;
            return buffer_append(buffer, "B") &&
                   buffer_append_component(buffer, (String){.data = (char *)names[type->builtin], .length = strlen(names[type->builtin])});
        }

        case HIR_TYPE_POINTER:
            return buffer_append(buffer, type->pointer.optional ? "Q" : "P") &&
                   buffer_append_type(buffer, sema, type->pointer.pointee, depth + 1);

        case HIR_TYPE_SLICE:
            return buffer_append(buffer, "L") && buffer_append_type(buffer, sema, type->slice.element, depth + 1);

        case HIR_TYPE_ARRAY:
            return buffer_appendf(buffer, "A%llu_", (unsigned long long)type->array.length) &&
                   buffer_append_type(buffer, sema, type->array.element, depth + 1);

        case HIR_TYPE_SUM:
            if (!buffer_appendf(buffer, "S%zu_", type->sum.members.len))
                return false;
            for (size_t i = 0; i < type->sum.members.len; i++) {
                if (!buffer_append_type(buffer, sema, ((HirType **)type->sum.members.data)[i], depth + 1))
                    return false;
            }
            return true;

        case HIR_TYPE_FUNCTION:
            if (!buffer_appendf(buffer, "F%zu_", type->function.params.len))
                return false;
            for (size_t i = 0; i < type->function.params.len; i++) {
                if (!buffer_append_type(buffer, sema, ((HirType **)type->function.params.data)[i], depth + 1))
                    return false;
            }
            return buffer_append_type(buffer, sema, type->function.ret, depth + 1);

        case HIR_TYPE_STRUCT:
            return buffer_append_fields(buffer, sema, type->structure.fields, false, depth);

        case HIR_TYPE_UNION:
            return buffer_append_fields(buffer, sema, type->union_.fields, true, depth);

        case HIR_TYPE_ANY:
            return buffer_append(buffer, "Y");

        case HIR_TYPE_ANY_CONSTRAINT:
            return buffer_append(buffer, "V") &&
                   buffer_append_nominal_identity(buffer, symbol_canonical(type->any_constraint.constraint));

        case HIR_TYPE_ENUM:
            return buffer_append(buffer, "E") && buffer_append_type(buffer, sema, type->_enum.underlying, depth + 1);
    }

    return false;
}

static size_t generic_type_index(AstFnDecl *fn, AstType *type) {
    if (fn == NULL || type == NULL || type->kind != AST_TYPE_NAMED ||
        type->named.path.parts.len != 1 || type->named.args.len != 0)
        return SIZE_MAX;

    AstName *name = array_at(&type->named.path.parts, 0);
    for (size_t i = 0; i < fn->generics.len; i++) {
        AstGenericParam *param = array_at(&fn->generics, i);
        if (ast_name_equal(name, &param->name))
            return i;
    }
    return SIZE_MAX;
}

static bool ast_type_contains_generic(AstFnDecl *fn, AstType *type) {
    if (type == NULL)
        return false;
    if (generic_type_index(fn, type) != SIZE_MAX)
        return true;

    switch (type->kind) {
        case AST_TYPE_NAMED:
            for (size_t i = 0; i < type->named.args.len; i++)
                if (ast_type_contains_generic(fn, ((AstType **)type->named.args.data)[i]))
                    return true;
            return false;

        case AST_TYPE_POINTER:
            return ast_type_contains_generic(fn, type->pointer.pointee);

        case AST_TYPE_ARRAY:
            return ast_type_contains_generic(fn, type->array.element);

        case AST_TYPE_FN:
            if (ast_type_contains_generic(fn, type->fn.ret))
                return true;
            for (size_t i = 0; i < type->fn.params.len; i++)
                if (ast_type_contains_generic(fn, ((AstType **)type->fn.params.data)[i]))
                    return true;
            return false;

        case AST_TYPE_SUM:
            for (size_t i = 0; i < type->sum.members.len; i++)
                if (ast_type_contains_generic(fn, ((AstType **)type->sum.members.data)[i]))
                    return true;
            return false;

        case AST_TYPE_STRUCT:
            for (size_t i = 0; i < type->structure.fields.len; i++) {
                AstField *field = array_at(&type->structure.fields, i);
                if (ast_type_contains_generic(fn, field->type))
                    return true;
            }
            return false;

        case AST_TYPE_UNION:
            for (size_t i = 0; i < type->union_.fields.len; i++) {
                AstField *field = array_at(&type->union_.fields, i);
                if (ast_type_contains_generic(fn, field->type))
                    return true;
            }
            return false;

        case AST_TYPE_ENUM:
            return ast_type_contains_generic(fn, type->_enum.underlying);

        case AST_TYPE_ERROR:
        case AST_TYPE_SPLICE:
            return false;
    }
    return false;
}

/* Generic receiver patterns are encoded only when their structure is supported.
   Any unsupported pattern falls back to its fully resolved HIR type. */
static bool buffer_append_type_pattern(MangleBuffer *buffer, Sema *sema, AstFnDecl *fn, AstType *type, size_t depth) {
    if (type == NULL || depth > 128)
        return false;

    size_t generic_index = generic_type_index(fn, type);
    if (generic_index != SIZE_MAX)
        return buffer_appendf(buffer, "V%zu", generic_index);

    if (!ast_type_contains_generic(fn, type)) {
        HirType *resolved = sema_type(sema, type);
        return resolved != NULL && buffer_append_type(buffer, sema, resolved, depth + 1);
    }

    switch (type->kind) {
        case AST_TYPE_POINTER:
            return buffer_append(buffer, type->pointer.optional ? "Q" : "P") &&
                   buffer_append_type_pattern(buffer, sema, fn, type->pointer.pointee, depth + 1);

        case AST_TYPE_ARRAY:
            if (type->array.sized)
                return false;
            return buffer_append(buffer, "L") &&
                   buffer_append_type_pattern(buffer, sema, fn, type->array.element, depth + 1);

        case AST_TYPE_ENUM:
            return buffer_append(buffer, "E") &&
                   buffer_append_type_pattern(buffer, sema, fn, type->_enum.underlying, depth + 1);

        case AST_TYPE_SUM:
            if (!buffer_appendf(buffer, "S%zu_", type->sum.members.len))
                return false;
            for (size_t i = 0; i < type->sum.members.len; i++)
                if (!buffer_append_type_pattern(buffer, sema, fn, ((AstType **)type->sum.members.data)[i], depth + 1))
                    return false;
            return true;

        case AST_TYPE_FN:
            if (!buffer_appendf(buffer, "F%zu_", type->fn.params.len))
                return false;
            for (size_t i = 0; i < type->fn.params.len; i++)
                if (!buffer_append_type_pattern(buffer, sema, fn, ((AstType **)type->fn.params.data)[i], depth + 1))
                    return false;
            return buffer_append_type_pattern(buffer, sema, fn, type->fn.ret, depth + 1);

        default:
            return false;
    }
}

String coda_mangle_symbol(Sema *sema, Symbol *symbol, Array arguments, bool specialization) {
    if (sema == NULL || symbol == NULL || symbol->name.kind != AST_NAME_IDENT)
        return (String){0};

    if (name_is_extern(symbol))
        return symbol->name.ident;

    bool has_receiver = symbol->kind == SYMBOL_FN && symbol->decl != NULL &&
                        symbol->decl->kind == AST_DECL_FN && symbol->decl->fn.receiver != NULL;
    bool is_entry_point = !specialization && symbol->kind == SYMBOL_FN && !has_receiver &&
                          arguments.len == 0 && string_eq(symbol->name.ident, STRING("main")) &&
                          symbol->owner_module == sema->root_module;
    if (is_entry_point)
        return symbol->name.ident;

    bool has_module = symbol->owner_module != NULL && symbol->owner_module->path.parts.len != 0;
    if (!specialization && !has_module && !has_receiver && arguments.len == 0)
        return symbol->name.ident;

    HirType *receiver_type = NULL;
    if (has_receiver) {
        receiver_type = sema_type(sema, symbol->decl->fn.receiver);
        if (receiver_type == NULL || receiver_type->kind == HIR_TYPE_ERROR)
            return (String){0};
    }

    MangleBuffer buffer = {0};
    bool ok = buffer_append(&buffer, "_C") && buffer_append_symbol_path(&buffer, symbol);

    if (ok && has_receiver) {
        ok = buffer_append(&buffer, "_R");
        AstFnDecl *fn = &symbol->decl->fn;
        if (ok && fn->generics.len != 0 && ast_type_contains_generic(fn, fn->receiver)) {
            MangleBuffer pattern = {0};
            if (buffer_append_type_pattern(&pattern, sema, fn, fn->receiver, 0)) {
                ok = buffer_append_n(&buffer, pattern.data, pattern.length);
                buffer_discard(&pattern);
            } else {
                buffer_discard(&pattern);
                ok = buffer_append_type(&buffer, sema, receiver_type, 0);
            }
        } else if (ok) {
            ok = buffer_append_type(&buffer, sema, receiver_type, 0);
        }
    }

    if (ok && arguments.len != 0)
        ok = buffer_appendf(&buffer, "_G%zu_", arguments.len);
    for (size_t i = 0; ok && i < arguments.len; i++) {
        HirType *argument = ((HirType **)arguments.data)[i];
        ok = buffer_append_type(&buffer, sema, argument, 0);
    }

    if (!ok) {
        buffer_discard(&buffer);
        return (String){0};
    }
    return buffer_finish(sema->arena, &buffer);
}

static bool cursor_expect(MangleCursor *cursor, const char *text) {
    size_t length = strlen(text);
    if (cursor->position > cursor->input.length ||
        length > cursor->input.length - cursor->position ||
        memcmp(cursor->input.data + cursor->position, text, length) != 0)
        return false;
    cursor->position += length;
    return true;
}

static bool cursor_number(MangleCursor *cursor, size_t *result) {
    if (cursor->position >= cursor->input.length || cursor->input.data[cursor->position] < '0' || cursor->input.data[cursor->position] > '9')
        return false;

    size_t value = 0;
    while (cursor->position < cursor->input.length) {
        char c = cursor->input.data[cursor->position];
        if (c < '0' || c > '9')
            break;
        size_t digit = (size_t)(c - '0');
        if (value > (SIZE_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
        cursor->position++;
    }

    *result = value;
    return true;
}

static bool cursor_count_suffix(MangleCursor *cursor, size_t *result) {
    return cursor_number(cursor, result) && cursor_expect(cursor, "_");
}

static bool cursor_component(Arena *arena, MangleCursor *cursor, String *result) {
    size_t length;
    if (!cursor_number(cursor, &length) || length == 0 || length > cursor->input.length - cursor->position)
        return false;

    char *data = arena_calloc(arena, length + 1);
    if (data == NULL)
        return false;
    memcpy(data, cursor->input.data + cursor->position, length);
    data[length] = '\0';
    cursor->position += length;
    *result = (String){.data = data, .length = length};
    return true;
}

static bool cursor_type(Arena *arena, MangleCursor *cursor, MangleBuffer *display, size_t depth);

static bool cursor_type_list(Arena *arena, MangleCursor *cursor, MangleBuffer *display, size_t count, const char *separator, size_t depth) {
    for (size_t i = 0; i < count; i++) {
        if (i != 0 && !buffer_append(display, separator))
            return false;
        if (!cursor_type(arena, cursor, display, depth + 1))
            return false;
    }
    return true;
}

static bool cursor_type(Arena *arena, MangleCursor *cursor, MangleBuffer *display, size_t depth) {
    if (depth > 128 || cursor->position >= cursor->input.length)
        return false;

    char tag = cursor->input.data[cursor->position++];
    size_t count;
    switch (tag) {
        case 'B': {
            String name;
            return cursor_component(arena, cursor, &name) && buffer_append_n(display, name.data, name.length);
        }

        case 'N': {
            if (!cursor_count_suffix(cursor, &count) || count == 0 || count > cursor->input.length)
                return false;
            for (size_t i = 0; i < count; i++) {
                String component;
                if (!cursor_component(arena, cursor, &component))
                    return false;
                if (i != 0 && !buffer_append(display, "::"))
                    return false;
                if (!buffer_append_n(display, component.data, component.length))
                    return false;
            }
            if (cursor->position + 2 <= cursor->input.length &&
                cursor->input.data[cursor->position] == '_' && cursor->input.data[cursor->position + 1] == 'I') {
                cursor->position += 2;
                if (!cursor_count_suffix(cursor, &count) || count == 0 || count > cursor->input.length ||
                    !buffer_append(display, "<"))
                    return false;
                if (!cursor_type_list(arena, cursor, display, count, ", ", depth + 1) ||
                    !buffer_append(display, ">"))
                    return false;
            }
            return true;
        }

        case 'V':
            return cursor_number(cursor, &count) && buffer_appendf(display, "T%zu", count);

        case 'P':
        case 'Q':
            if (!cursor_type(arena, cursor, display, depth + 1))
                return false;
            return buffer_append(display, tag == 'P' ? "*" : "*?");

        case 'L':
            return cursor_type(arena, cursor, display, depth + 1) && buffer_append(display, "[]");

        case 'A':
            if (!cursor_count_suffix(cursor, &count) || !cursor_type(arena, cursor, display, depth + 1))
                return false;
            return buffer_appendf(display, "[%zu]", count);

        case 'S':
            if (!cursor_count_suffix(cursor, &count) || count == 0 || count > cursor->input.length ||
                !buffer_append(display, "("))
                return false;
            return cursor_type_list(arena, cursor, display, count, " | ", depth + 1) && buffer_append(display, ")");

        case 'F':
            if (!cursor_count_suffix(cursor, &count) || count > cursor->input.length || !buffer_append(display, "fn("))
                return false;
            if (!cursor_type_list(arena, cursor, display, count, ", ", depth + 1) ||
                !buffer_append(display, ")->") || !cursor_type(arena, cursor, display, depth + 1))
                return false;
            return true;

        case 'C':
        case 'U':
            if (!cursor_count_suffix(cursor, &count) || count > cursor->input.length ||
                !buffer_append(display, tag == 'C' ? "struct{" : "union{"))
                return false;
            for (size_t i = 0; i < count; i++) {
                String name;
                if (!cursor_component(arena, cursor, &name))
                    return false;
                if (i != 0 && !buffer_append(display, ";"))
                    return false;
                if (!buffer_append_n(display, name.data, name.length) || !buffer_append(display, ":") ||
                    !cursor_type(arena, cursor, display, depth + 1))
                    return false;
            }
            return buffer_append(display, "}");

        case 'E':
            return buffer_append(display, "enum(") && cursor_type(arena, cursor, display, depth + 1) && buffer_append(display, ")");

        default:
            return false;
    }
}

static String cursor_tagged_argument(Arena *arena, char tag, String value) {
    if (value.data == NULL || value.length > SIZE_MAX - 3)
        return (String){0};
    char *data = arena_calloc(arena, value.length + 3);
    if (data == NULL)
        return (String){0};
    data[0] = tag;
    data[1] = ':';
    memcpy(data + 2, value.data, value.length);
    data[value.length + 2] = '\0';
    return (String){.data = data, .length = value.length + 2};
}

bool coda_demangle_symbol(Arena *arena, String mangled, CodaDemangledSymbol *symbol) {
    if (arena == NULL || symbol == NULL || mangled.data == NULL || mangled.length < 3 ||
        mangled.data[0] != '_' || mangled.data[1] != 'C')
        return false;

    MangleCursor cursor = {.input = mangled, .position = 2};
    CodaDemangledSymbol result = {
        .module_path = array_create(arena, sizeof(String)),
        .generic_arguments = array_create(arena, sizeof(String)),
    };
    Array components = array_create(arena, sizeof(String));

    while (cursor.position < mangled.length && mangled.data[cursor.position] >= '0' && mangled.data[cursor.position] <= '9') {
        String component;
        if (!cursor_component(arena, &cursor, &component))
            return false;
        array_push(&components, &component);
    }
    if (components.len == 0)
        return false;

    result.name = ((String *)components.data)[components.len - 1];
    for (size_t i = 0; i + 1 < components.len; i++) {
        String part = ((String *)components.data)[i];
        array_push(&result.module_path, &part);
    }

    if (cursor.position < mangled.length && cursor_expect(&cursor, "_R")) {
        MangleBuffer receiver = {0};
        if (!cursor_type(arena, &cursor, &receiver, 0)) {
            buffer_discard(&receiver);
            return false;
        }
        result.receiver = buffer_finish(arena, &receiver);
        if (result.receiver.data == NULL)
            return false;
    }

    if (cursor.position < mangled.length && cursor_expect(&cursor, "_G")) {
        size_t argument_count;
        if (!cursor_count_suffix(&cursor, &argument_count) || argument_count == 0 || argument_count > mangled.length)
            return false;
        for (size_t i = 0; i < argument_count; i++) {
            MangleBuffer argument_buffer = {0};
            if (!cursor_type(arena, &cursor, &argument_buffer, 0)) {
                buffer_discard(&argument_buffer);
                return false;
            }
            String value = buffer_finish(arena, &argument_buffer);
            if (value.data == NULL)
                return false;
            String tagged = cursor_tagged_argument(arena, 'T', value);
            if (tagged.data == NULL)
                return false;
            array_push(&result.generic_arguments, &tagged);
        }
    }

    if (cursor.position != mangled.length)
        return false;
    *symbol = result;
    return true;
}

String coda_demangled_symbol_name(Arena *arena, const CodaDemangledSymbol *symbol) {
    if (arena == NULL || symbol == NULL)
        return (String){0};

    MangleBuffer buffer = {0};
    bool ok = true;
    if (symbol->receiver.length != 0) {
        ok = buffer_append_n(&buffer, symbol->receiver.data, symbol->receiver.length) && buffer_append(&buffer, ".");
    } else {
        for (size_t i = 0; ok && i < symbol->module_path.len; i++) {
            String part = ((String *)symbol->module_path.data)[i];
            if (i != 0)
                ok = buffer_append(&buffer, "::");
            if (ok)
                ok = buffer_append_n(&buffer, part.data, part.length);
        }
        if (ok && symbol->module_path.len != 0)
            ok = buffer_append(&buffer, "::");
    }

    if (ok)
        ok = buffer_append_n(&buffer, symbol->name.data, symbol->name.length);
    if (ok && symbol->generic_arguments.len != 0)
        ok = buffer_append(&buffer, "<");

    for (size_t i = 0; ok && i < symbol->generic_arguments.len; i++) {
        String argument = ((String *)symbol->generic_arguments.data)[i];
        if (argument.length >= 2 && argument.data[1] == ':' &&
            (argument.data[0] == 'T' || argument.data[0] == 'V')) {
            argument.data += 2;
            argument.length -= 2;
        }
        if (i != 0)
            ok = buffer_append(&buffer, ", ");
        if (ok)
            ok = buffer_append_n(&buffer, argument.data, argument.length);
    }
    if (ok && symbol->generic_arguments.len != 0)
        ok = buffer_append(&buffer, ">");

    if (!ok) {
        buffer_discard(&buffer);
        return (String){0};
    }
    return buffer_finish(arena, &buffer);
}
