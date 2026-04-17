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
            Token star_tok = consume(ctx);

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

static int bp_for_binary(TokenType type, int *right_assoc, BinaryOp *out) {
    *right_assoc = 0;

    switch (type) {
        case TOKENTYPE_STAR: *out = BINOP_MUL; return 60;
        case TOKENTYPE_SLASH: *out = BINOP_DIV; return 60;
        case TOKENTYPE_PERCENT: *out = BINOP_MOD; return 60;

        case TOKENTYPE_PLUS: *out = BINOP_ADD; return 50;
        case TOKENTYPE_MINUS: *out = BINOP_SUB; return 50;
        
        case TOKENTYPE_SHL: *out = BINOP_SHL; return 45;
        case TOKENTYPE_SHR: *out = BINOP_SHR; return 45;

        case TOKENTYPE_LT: *out = BINOP_LT; return 40;
        case TOKENTYPE_LE: *out = BINOP_LE; return 40;
        case TOKENTYPE_GT: *out = BINOP_GT; return 40;
        case TOKENTYPE_GE: *out = BINOP_GE; return 40;

        case TOKENTYPE_EQEQ: *out = BINOP_EQ; return 35;
        case TOKENTYPE_NEQ: *out = BINOP_NE; return 35;

        case TOKENTYPE_AMP: *out = BINOP_AND; return 32;
        case TOKENTYPE_CARET: *out = BINOP_XOR; return 28;
        case TOKENTYPE_PIPE: *out = BINOP_OR; return 24;

        case TOKENTYPE_AMPAMP: *out = BINOP_LOG_AND; return 15;
        case TOKENTYPE_PIPEPIPE: *out = BINOP_LOG_OR; return 10;

        case TOKENTYPE_EQ: *out = BINOP_ASSIGN; *right_assoc = 1; return 5;

        default: return 0;
    }
}

static int token_to_unary(TokenType type, UnaryOp *out) {
    switch (type) {
        case TOKENTYPE_MINUS: *out = UOP_NEG; return 1;
        case TOKENTYPE_NOT: *out = UOP_NOT; return 1;
        case TOKENTYPE_STAR: *out = UOP_DEREF; return 1;
        case TOKENTYPE_AMP: *out = UOP_ADDR; return 1;
        default: return 0;
    }
}

Expr *expr_new_lit(Parser *ctx, Token *t) {
    Expr *e = arena_calloc(ctx->arena, sizeof(Expr));
    e->type = EXPR_LIT;
    
    if (t->type == TOKENTYPE_INT_LIT) {
        e->literal = (Literal){
            .type = LITERAL_INT,
            .raw = t->value.value,
            ._int = strtoll(t->value.value.data, NULL, 0),
        };
    }
    else if (t->type == TOKENTYPE_STR_LIT) {
        e->literal = (Literal){
            .type = LITERAL_STRING,
            .raw = t->value.value,
            .string = t->value.value
        };
    }
    else if (t->type == TOKENTYPE_TRUE || t->type == TOKENTYPE_FALSE) {
        e->literal = (Literal){
            .type = LITERAL_BOOL,
            .raw = t->value.value,
            ._bool = (t->type == TOKENTYPE_TRUE)
        };
    }
    else {
        printf("Unknown expression literal\n");
        exit(1);
    }

    return e;
}

Expr *expr_new_ident(Parser *ctx, Token *t) {
    string_array comps;
    string_array_push(&comps, t->value.value);

    Token last = *t;
    while (peek(ctx).has_value && peek(ctx).value.type == TOKENTYPE_DOUBLECOLON) {
        consume(ctx);
        Token comp = consume(ctx);
        if (comp.type != TOKENTYPE_IDENT) {
            printf("Expected ident after '::'\n");
            exit(1);
        }

        string_array_push(&comps, comp.value.value);
        last = comp;
    }

    Expr *e = arena_calloc(ctx->arena, sizeof(Expr));

    if (comps.idx == 1) {
        e->type = EXPR_IDENT;
        e->ident.name = comps.data[0];
    } else {
        e->type = EXPR_PATH;
        e->path.components = comps;
    }

    return e;
}

Expr *parse_expr(Parser *ctx, int min_bp);

Expr *parse_expr_prefix(Parser *ctx) {
    token_optional t = peek(ctx);

    if (!t.has_value) {
        printf("Expected a token for expression\n");
        exit(1);
    }

    if (t.value.type == TOKENTYPE_INT_LIT || t.value.type == TOKENTYPE_STR_LIT || t.value.type == TOKENTYPE_CHAR_LIT || t.value.type == TOKENTYPE_TRUE || t.value.type == TOKENTYPE_FALSE) {
        consume(ctx);
        return expr_new_lit(ctx, &t.value);
    }

    if (t.value.type == TOKENTYPE_IDENT) {
        consume(ctx);
        return expr_new_ident(ctx, &t.value);
    }

    if (t.value.type == TOKENTYPE_LPAREN) {
        consume(ctx);

        t = peek(ctx);
        if (t.has_value && (t.value.type == TOKENTYPE_IDENT || t.value.type == TOKENTYPE_MUT)) {
            // TODO: type casting
        }
        Expr *inner = parse_expr(ctx, 0);
        expect(ctx, TOKENTYPE_RPAREN, "Expected ')'\n");
        return inner;
    }

    UnaryOp uop;
    if (token_to_unary(t.value.type, &uop)) {
        consume(ctx);
        Expr *operand = parse_expr(ctx, 80);
        Expr *e = arena_calloc(ctx->arena, sizeof(Expr));
        e->type = EXPR_UNARY;
        e->unary.op = uop;
        e->unary.operand = operand;
        return e;
    }

    printf("Unexpected token in expression\n");
    exit(1);
}

Expr *expr_handle_postfix(Parser *ctx, Expr *left) {
    while (true) {
        token_optional next = peek(ctx);
        if (next.has_value && next.value.type == TOKENTYPE_LPAREN) {
            consume(ctx);
            exprs_array args;
            next = peek(ctx);
            if (!next.has_value || next.value.type != TOKENTYPE_RPAREN) {
                while (true) {
                    exprs_array_push(&args, parse_expr(ctx, 0));

                    next = peek(ctx);
                    if (!next.has_value || next.value.type != TOKENTYPE_COMMA) break;
                    consume(ctx);
                }
            }

            consume(ctx);
            Expr *call = arena_calloc(ctx->arena, sizeof(Expr));
            call->type = EXPR_CALL;
            call->call.callee = left;
            call->call.args = args;
            left = call;
            continue;
        } else if (next.has_value && next.value.type == TOKENTYPE_LBRACK) {
            Token lb = consume(ctx);
            Expr *idx = parse_expr(ctx, 0);
            Token rb = consume(ctx);
            if (rb.type != TOKENTYPE_RBRACK) {
                printf("Expected ']'\n");
                exit(1);
            }

            Expr *index = arena_calloc(ctx->arena, sizeof(Expr));
            index->type = EXPR_INDEX;
            index->index.base = left;
            index->index.index = idx;

            left = index;
            continue;
        }
        else if (next.has_value && next.value.type == TOKENTYPE_DOT) {
            consume(ctx);
            Token mem = consume(ctx);
            if (mem.type != TOKENTYPE_IDENT) {
                printf("Expected member name after '.'\n");
                exit(1);
            }
            Expr *m = arena_calloc(ctx->arena, sizeof(Expr));
            m->type = EXPR_MEMBER;
            m->member.base = left;
            m->member.member = mem.value.value;
            left = m;
            continue;
        } else {
            break;
        }
    }

    return left;
}

Expr *parse_expr(Parser *ctx, int min_bp) {
    Expr *left = parse_expr_prefix(ctx);
    left = expr_handle_postfix(ctx, left);

    while (true) {
        token_optional next = peek(ctx);
        if (!next.has_value || next.value.type == TOKENTYPE_SEMICOLON || next.value.type == TOKENTYPE_COMMA || next.value.type == TOKENTYPE_RPAREN || next.value.type == TOKENTYPE_RBRACK) {
            break;
        }

        BinaryOp binop;
        int right_assoc = 1;
        int bp = bp_for_binary(next.value.type, &right_assoc, &binop);
        if (bp == 0 || bp <= min_bp) break;

        Token op_tok = consume(ctx);

        int rbp = right_assoc ? bp - 1 : bp;

        Expr *right = parse_expr(ctx, rbp);
        Expr *b = arena_calloc(ctx->arena, sizeof(Expr));
        b->type = EXPR_BINARY;
        b->binary.op = binop;
        b->binary.left = left;
        b->binary.right = right;

        left = b;

        left = expr_handle_postfix(ctx, left);
    }

    return left;
}