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
    if (extra > SIZE_MAX - buffer->length - 1)
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
    if (string.data == NULL && string.length != 0)
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

static bool buffer_append_qualified_symbol(MangleBuffer *buffer, const Symbol *symbol) {
    if (symbol == NULL || symbol->name.kind != AST_NAME_IDENT)
        return buffer_append(buffer, "<unknown>");

    if (symbol->owner_module != NULL) {
        Path *path = &symbol->owner_module->path;
        for (size_t i = 0; i < path->parts.len; i++) {
            AstName *part = array_at(&path->parts, i);
            if (i != 0 && !buffer_append(buffer, "::"))
                return false;
            if (part->kind != AST_NAME_IDENT || !buffer_append_n(buffer, part->ident.data, part->ident.length))
                return false;
        }
        if (path->parts.len != 0 && !buffer_append(buffer, "::"))
            return false;
    }

    return buffer_append_n(buffer, symbol->name.ident.data, symbol->name.ident.length);
}

static bool buffer_append_type(MangleBuffer *buffer, Sema *sema, HirType *type, size_t depth);

static bool buffer_append_type_arguments(MangleBuffer *buffer, Sema *sema, Array arguments, size_t depth) {
    if (!buffer_append(buffer, "<"))
        return false;

    for (size_t i = 0; i < arguments.len; i++) {
        if (i != 0 && !buffer_append(buffer, ", "))
            return false;
        HirType *argument = ((HirType **)arguments.data)[i];
        if (!buffer_append_type(buffer, sema, argument, depth + 1))
            return false;
    }

    return buffer_append(buffer, ">");
}

static bool buffer_append_fields(MangleBuffer *buffer, Sema *sema, Array fields, bool is_union, size_t depth) {
    if (!buffer_append(buffer, is_union ? "union{" : "struct{"))
        return false;

    for (size_t i = 0; i < fields.len; i++) {
        HirField *field = array_at(&fields, i);
        if (i != 0 && !buffer_append(buffer, ";"))
            return false;
        if (field->symbol != NULL && field->symbol->name.kind == AST_NAME_IDENT) {
            if (!buffer_append_n(buffer, field->symbol->name.ident.data, field->symbol->name.ident.length))
                return false;
        } else if (!buffer_appendf(buffer, "field%zu", i)) {
            return false;
        }
        if (!buffer_append(buffer, ":") || !buffer_append_type(buffer, sema, field->type, depth + 1))
            return false;
    }

    return buffer_append(buffer, "}");
}

static bool buffer_append_type(MangleBuffer *buffer, Sema *sema, HirType *type, size_t depth) {
    if (type == NULL)
        return buffer_append(buffer, "<null>");
    if (depth > 128)
        return buffer_append(buffer, "<depth-limit>");
    /* Nominal types must be encoded by identity, not by their underlying layout. */
    if (type->nominal != NULL) {
        if (!buffer_append_qualified_symbol(buffer, type->nominal))
            return false;
        GenericTypeInstance *instance = sema_generic_type_instance_for_type(sema, type, NULL);
        if (instance != NULL && instance->arguments.len != 0 &&
            !buffer_append_type_arguments(buffer, sema, instance->arguments, depth))
            return false;
        return true;
    }

    switch (type->kind) {
        case HIR_TYPE_ERROR:
            return buffer_append(buffer, "<error>");

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
                return buffer_append(buffer, "<builtin>");
            return buffer_append(buffer, names[type->builtin]);
        }

        case HIR_TYPE_POINTER:
            return buffer_append_type(buffer, sema, type->pointer.pointee, depth + 1) &&
                   buffer_append(buffer, type->pointer.optional ? "*?" : "*");

        case HIR_TYPE_SLICE:
            return buffer_append_type(buffer, sema, type->slice.element, depth + 1) && buffer_append(buffer, "[]");

        case HIR_TYPE_ARRAY:
            return buffer_append_type(buffer, sema, type->array.element, depth + 1) &&
                   buffer_appendf(buffer, "[%llu]", (unsigned long long)type->array.length);

        case HIR_TYPE_SUM:
            if (!buffer_append(buffer, "("))
                return false;
            for (size_t i = 0; i < type->sum.members.len; i++) {
                if (i != 0 && !buffer_append(buffer, " | "))
                    return false;
                if (!buffer_append_type(buffer, sema, ((HirType **)type->sum.members.data)[i], depth + 1))
                    return false;
            }
            return buffer_append(buffer, ")");

        case HIR_TYPE_FUNCTION:
            if (!buffer_append(buffer, "fn("))
                return false;
            for (size_t i = 0; i < type->function.params.len; i++) {
                if (i != 0 && !buffer_append(buffer, ", "))
                    return false;
                if (!buffer_append_type(buffer, sema, ((HirType **)type->function.params.data)[i], depth + 1))
                    return false;
            }
            return buffer_append(buffer, ")->") && buffer_append_type(buffer, sema, type->function.ret, depth + 1);

        case HIR_TYPE_STRUCT:
            return buffer_append_fields(buffer, sema, type->structure.fields, false, depth);

        case HIR_TYPE_UNION:
            return buffer_append_fields(buffer, sema, type->union_.fields, true, depth);

        case HIR_TYPE_ENUM:
            return buffer_append(buffer, "enum(") && buffer_append_type(buffer, sema, type->_enum.underlying, depth + 1) && buffer_append(buffer, ")");
    }

    return buffer_append(buffer, "<unknown-type>");
}

static String mangle_type_text(Sema *sema, HirType *type) {
    MangleBuffer buffer = {0};
    if (!buffer_append_type(&buffer, sema, type, 0)) {
        buffer_discard(&buffer);
        return (String){0};
    }
    return buffer_finish(sema->arena, &buffer);
}

static bool type_text_char_is_safe(unsigned char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static bool buffer_append_type_text(MangleBuffer *buffer, String text) {
    static const char hex[] = "0123456789ABCDEF";

    if (text.data == NULL && text.length != 0)
        return false;

    for (size_t i = 0; i < text.length; i++) {
        unsigned char c = (unsigned char)text.data[i];

        if (type_text_char_is_safe(c)) {
            if (!buffer_append_n(buffer, &text.data[i], 1))
                return false;
        } else {
            char encoded[] = {'_', hex[c >> 4], hex[c & 0x0f]};
            if (!buffer_append_n(buffer, encoded, sizeof(encoded)))
                return false;
        }
    }

    return true;
}

static bool buffer_append_encoded(MangleBuffer *buffer, String text) {
    MangleBuffer encoded = {0};

    if (!buffer_append_type_text(&encoded, text)) {
        buffer_discard(&encoded);
        return false;
    }

    String component = {
        .data = encoded.data,
        .length = encoded.length,
    };

    bool ok = buffer_append_component(buffer, component);
    buffer_discard(&encoded);
    return ok;
}

static String mangle_type_link_text(Sema *sema, HirType *type) {
    String text = mangle_type_text(sema, type);
    if (text.data == NULL)
        return (String){0};

    MangleBuffer buffer = {0};
    if (!buffer_append_type_text(&buffer, text)) {
        buffer_discard(&buffer);
        return (String){0};
    }

    return buffer_finish(sema->arena, &buffer);
}

static String mangle_function_signature(Sema *sema, Symbol *symbol) {
    if (symbol == NULL || symbol->type == NULL || symbol->type->kind != HIR_TYPE_FUNCTION)
        return (String){0};

    HirType *type = symbol->type;
    MangleBuffer buffer = {0};
    bool ok = buffer_append(&buffer, "fn(");

    for (size_t i = 0; ok && i < type->function.params.len; i++) {
        if (i != 0)
            ok = buffer_append(&buffer, ",");
        if (ok)
            ok = buffer_append_type(&buffer, sema, ((HirType **)type->function.params.data)[i], 0);
    }

    if (ok)
        ok = buffer_append(&buffer, ")->") && buffer_append_type(&buffer, sema, type->function.ret, 0);

    if (!ok) {
        buffer_discard(&buffer);
        return (String){0};
    }
    return buffer_finish(sema->arena, &buffer);
}

static bool buffer_append_type_argument(MangleBuffer *buffer, Sema *sema, HirType *type) {
    String type_text = mangle_type_text(sema, type);
    return type_text.data != NULL &&
           buffer_append(buffer, "T") &&
           buffer_append_encoded(buffer, type_text);
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

    String receiver_text = {0};
    if (has_receiver) {
        HirType *receiver_type = sema_type(sema, symbol->decl->fn.receiver);
        if (receiver_type == NULL || receiver_type->kind == HIR_TYPE_ERROR)
            return (String){0};
        receiver_text = mangle_type_link_text(sema, receiver_type);
        if (receiver_text.data == NULL)
            return (String){0};
    }

    MangleBuffer buffer = {0};
    bool ok = buffer_append(&buffer, "_C") && buffer_append_symbol_path(&buffer, symbol);

    if (ok && receiver_text.length != 0)
        ok = buffer_append(&buffer, "_R") && buffer_append_component(&buffer, receiver_text);

    if (ok && arguments.len != 0)
        ok = buffer_appendf(&buffer, "_G%zu", arguments.len);
    for (size_t i = 0; ok && i < arguments.len; i++) {
        HirType *argument = ((HirType **)arguments.data)[i];
        ok = buffer_append_type_argument(&buffer, sema, argument);
    }

    if (!ok) {
        buffer_discard(&buffer);
        return (String){0};
    }
    return buffer_finish(sema->arena, &buffer);
}

static bool cursor_expect(MangleCursor *cursor, const char *text) {
    size_t length = strlen(text);
    if (length > cursor->input.length - cursor->position ||
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

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool cursor_type_component(Arena *arena, MangleCursor *cursor, String *result) {
    String encoded;
    if (!cursor_component(arena, cursor, &encoded))
        return false;

    MangleBuffer buffer = {0};
    for (size_t i = 0; i < encoded.length; i++) {
        unsigned char c = (unsigned char)encoded.data[i];
        if (c == '_') {
            if (i + 2 >= encoded.length) {
                buffer_discard(&buffer);
                return false;
            }
            int high = hex_value(encoded.data[++i]);
            int low = hex_value(encoded.data[++i]);
            if (high < 0 || low < 0) {
                buffer_discard(&buffer);
                return false;
            }
            char decoded = (char)((high << 4) | low);
            if (decoded == '\0' || !buffer_append_n(&buffer, &decoded, 1)) {
                buffer_discard(&buffer);
                return false;
            }
        } else {
            if (!type_text_char_is_safe(c) || !buffer_append_n(&buffer, (const char *)&encoded.data[i], 1)) {
                buffer_discard(&buffer);
                return false;
            }
        }
    }

    *result = buffer_finish(arena, &buffer);
    return result->data != NULL;
}

static String cursor_tagged_argument(Arena *arena, char tag, String value) {
    if (value.length > SIZE_MAX - 3)
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

    Array(String) components = array_create(arena, sizeof(String));
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
        if (!cursor_type_component(arena, &cursor, &result.receiver))
            return false;
    }

    if (cursor.position < mangled.length && cursor_expect(&cursor, "_G")) {
        size_t argument_count;
        if (!cursor_number(&cursor, &argument_count) || argument_count == 0 || argument_count > mangled.length)
            return false;
        for (size_t i = 0; i < argument_count; i++) {
            if (cursor.position >= mangled.length)
                return false;
            char tag = mangled.data[cursor.position++];
            if (tag != 'T')
                return false;
            String value;
            if (!cursor_type_component(arena, &cursor, &value))
                return false;
            String tagged = cursor_tagged_argument(arena, tag, value);
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
        ok = buffer_append_n(&buffer, symbol->receiver.data, symbol->receiver.length) &&
             buffer_append(&buffer, ".");
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
