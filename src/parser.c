#include "parser.h"

INSTANTIATE(Token, token, OPTIONAL_TEMPLATE)

static token_optional peek(Parser *ctx) {
    if (ctx->index >= ctx->tokens.idx) return token_optional_empty;
    return (token_optional){true, ctx->tokens.data[ctx->index]};
}

static Token consume(Parser *ctx) {
    return ctx->tokens.data[ctx->index++];
}

static void expect(Parser *ctx, TokenType type, char *msg) {
    token_optional t = peek(ctx);
    if (!t.has_value || t.value.type != type) {
        printf(msg);
        exit(1);
    }

    consume(ctx);
}

Include *parse_include(Parser *ctx) {
    Include *inc = arena_calloc(ctx->arena, sizeof(Include));

    consume(ctx);

    while (true) {
        token_optional t = peek(ctx);
        if (!t.has_value || t.value.type != TOKENTYPE_IDENT) {
            printf("Expected include path\n");
            exit(1);
        }

        string_array_push(&inc->path, consume(ctx).value.value);

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_DOUBLECOLON) {
            consume(ctx);
            continue;
        }
        break;
    }

    expect(ctx, TOKENTYPE_SEMICOLON, "Expected semicolon after include\n");
    return inc;
}

void collect_attributes(Parser *ctx, attr_array *out) {
    token_optional t = peek(ctx);
    while (t.has_value) {
        if (t.value.type != TOKENTYPE_AT) break;

        consume(ctx);
        Token name = consume(ctx);
        Attribute attr;
        attr.name = name.value.value;

        // handle args here

        attr_array_push(out, attr);
    }
}

TypeRef *parse_type(Parser *ctx) {
    token_optional first = peek(ctx);
    bool is_mut = false;

    token_optional t = peek(ctx);
    if (t.has_value && t.value.type == TOKENTYPE_MUT) {
        consume(ctx);
        is_mut = true;
    }

    t = peek(ctx);
    if (!t.has_value || t.value.type != TOKENTYPE_IDENT) {
        printf("Expected type name\n");
        exit(1);
    }

    String type_name = consume(ctx).value.value;

    TypeRef *base = arena_calloc(ctx->arena, sizeof(TypeRef));
    base->type = TYPEREF_NAMED;
    base->named.name = type_name;
    base->is_mutable = is_mut;

    while (true) {
        token_optional t = peek(ctx);
        if (t.has_value && t.value.type != TOKENTYPE_MUT && t.value.type != TOKENTYPE_STAR && t.value.type != TOKENTYPE_LBRACK) break;

        bool is_mut = false;
        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_MUT) {
            consume(ctx);
            is_mut = true;
        }

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_STAR) {
            Token star_tok = consume();

            TypeRef *ptr = arena_calloc(ctx->arena, sizeof(TypeRef));
            ptr->type = TYPEREF_POINTER;
            ptr->pointer.pointee = base;
            ptr->is_mutable = is_mut;

            t = peek(ctx);
            if (t.has_value && t.value.type == TOKENTYPE_QUESTION) {
                Token q = consume(ctx);
                ptr->is_optional = true;
            }

            base = ptr;
            continue;
        }

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_LBRACK) {
            Token lb = consume(ctx);
            size_t length = 0;
            t = peek(ctx);
            if (t.has_value && t.value.type == TOKENTYPE_INT_LIT) {
                consume(ctx);
                length = strtoul(t.value.value.value.data, NULL, 10);
            }
            t = peek(ctx);
            if (t.has_value && t.value.type != TOKENTYPE_RBRACK) {
                printf("Expected ']'\n");
                exit(1);
            }
            Token rb = consume(ctx);

            TypeRef *array = arena_calloc(ctx->arena, sizeof(TypeRef));
            array->type = TYPEREF_ARRAY;
            array->array.elem = base;
            array->array.length = length;
            array->is_mutable = is_mut;

            t = peek(ctx);
            if (t.has_value && t.value.type == TOKENTYPE_QUESTION) {
                consume(ctx);
                array->is_optional = true;
            }

            base = array;
            continue;
        }

        break;
    }

    return base;
}