#include <stdio.h>
#include "parser.h"
#include "error.h"

#define FALLTHROUGH

INSTANTIATE(Token, token, OPTIONAL_TEMPLATE)

static token_optional peek(Parser *ctx) {
    if (ctx->index >= ctx->tokens.len) return (token_optional){};
    return (token_optional){true, ctx->tokens.data[ctx->index]};
}
static token_optional ahead(Parser *ctx, size_t ahead) {
    if (ctx->index + ahead >= ctx->tokens.len) return (token_optional){};
    return (token_optional){true, ctx->tokens.data[ctx->index + ahead]};
}

static Token consume(Parser *ctx) {
    return ctx->tokens.data[ctx->index++];
}

static Token expect(Parser *ctx, TokenType type, char *msg) {
    token_optional t = peek(ctx);
    if (!t.has_value || t.value.type != type) {
        error(ctx, msg);
    }

    return consume(ctx);
}

static void backtrack(Parser *ctx, size_t num) {
    if (ctx->index < num) ctx->index = 0;
    else ctx->index -= num;
}

Include *parse_include(Parser *ctx) {
    Include *inc = arena_calloc(ctx->arena, sizeof(Include));
    inc->token = consume(ctx);
    inc->path = string_array_init();

    while (true) {
        token_optional t = peek(ctx);
        if (!t.has_value || t.value.type != TOKENTYPE_IDENT) {
            error(ctx, "Expected include path");
        }

        string_array_push(&inc->path, consume(ctx).value.value);

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_DOUBLECOLON) {
            consume(ctx);
            continue;
        }
        break;
    }

    if (peek(ctx).has_value && peek(ctx).value.type == TOKENTYPE_EQ) {
        consume(ctx);
        
        inc->alias = (string_optional){
            .has_value = true,
            .value = expect(ctx, TOKENTYPE_IDENT, "Expected module alias name").value.value
        };
    }

    expect(ctx, TOKENTYPE_SEMICOLON, "Expected semicolon after include");
    return inc;
}

Literal new_lit(Parser *ctx, Token t);

void collect_attributes(Parser *ctx, attr_array *out) {
    token_optional t = peek(ctx);
    while (t.has_value) {
        if (t.value.type != TOKENTYPE_AT) break;

        consume(ctx);
        Token name = consume(ctx);
        Attribute attr = {
            .name = name.value.value,
            .token = name
        };

        attr.args = lit_array_init();

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_LPAREN) {
            consume(ctx);
            t = peek(ctx);
            while (t.has_value) {
                lit_array_push(&attr.args, new_lit(ctx, consume(ctx)));

                t = peek(ctx);
                if (t.has_value && t.value.type == TOKENTYPE_COMMA) {
                    consume(ctx);
                    t = peek(ctx);
                    continue;
                }
                
                expect(ctx, TOKENTYPE_RPAREN, "Expected ')' to close attribute list");
                break;
            }
        }

        attr_array_push(out, attr);
        t = peek(ctx);
    }
}

bool looks_like_type(Parser *ctx, token_optional *after) {
    token_optional first = peek(ctx);
    bool is_mut = false;
    size_t stepped = 0;

    token_optional t = peek(ctx);
    if (t.has_value && t.value.type == TOKENTYPE_MUT) {
        consume(ctx); stepped++;
        is_mut = true;
    }

    t = peek(ctx);
    if (!t.has_value || t.value.type != TOKENTYPE_IDENT) {
        goto failed;
    }

    String type_name = consume(ctx).value.value; stepped++;

    while (true) {
        token_optional t = peek(ctx);
        if (t.has_value && t.value.type != TOKENTYPE_MUT && t.value.type != TOKENTYPE_STAR && t.value.type != TOKENTYPE_LBRACK) break;

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_MUT) {
            consume(ctx); stepped++;
        }

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_STAR) {
            Token star_tok = consume(ctx); stepped++;

            t = peek(ctx);
            if (t.has_value && t.value.type == TOKENTYPE_QUESTION) {
                Token q = consume(ctx); stepped++;
            }
            continue;
        }

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_LBRACK) {
            Token lb = consume(ctx); stepped++;
            t = peek(ctx);
            if (t.has_value && t.value.type == TOKENTYPE_INT_LIT) {
                consume(ctx); stepped++;
            }
            t = peek(ctx);
            if (t.has_value && t.value.type != TOKENTYPE_RBRACK) {
                goto failed;
            }
            Token rb = consume(ctx); stepped++;

            t = peek(ctx);
            if (t.has_value && t.value.type == TOKENTYPE_QUESTION) {
                consume(ctx); stepped++;
            }
            continue;
        }

        break;
    }

passed:
    if (after) *after = peek(ctx);
    backtrack(ctx, stepped);
    return true;
failed:
    if (after) *after = peek(ctx);
    backtrack(ctx, stepped);
    return false;
}

TypeRef *parse_type_single(Parser *ctx) {
    token_optional first = peek(ctx);
    bool is_mut = false;

    token_optional t = peek(ctx);
    if (t.has_value && t.value.type == TOKENTYPE_MUT) {
        consume(ctx);
        is_mut = true;
    }

    t = peek(ctx);
    TypeRef *base = arena_calloc(ctx->arena, sizeof(TypeRef));
    if (t.has_value && t.value.type == TOKENTYPE_FN) {
        // function pointer yippee!
        Token start = consume(ctx);
        TypeRef *ret_type = parse_type_single(ctx);
        expect(ctx, TOKENTYPE_LPAREN, "Expected '('");

        base->type = TYPEREF_FN;
        base->token = start;
        base->fn.params = param_array_init();
        base->fn.ret_type = ret_type;

        t = peek(ctx);
        while (t.has_value && t.value.type != TOKENTYPE_RPAREN) {
            start = t.value;
            attr_array attrs = attr_array_init();
            collect_attributes(ctx, &attrs);

            TypeRef *param_type = parse_type_single(ctx);
            t = peek(ctx);
            Token name;
            if (t.has_value) {
                name = t.value;
            }

            Param p = (Param){
                .type = param_type,
                .name = name.value.has_value ? name.value.value : (String){},
                .attributes = attrs,
                .token = start
            };

            param_array_push(&base->fn.params, p);
            t = peek(ctx);
            if (!t.has_value || t.value.type != TOKENTYPE_COMMA) break;
            consume(ctx);
            t = peek(ctx);
        }
        expect(ctx, TOKENTYPE_RPAREN, "Expecetd ')'");
    }
    else if (!t.has_value || t.value.type != TOKENTYPE_IDENT) {
        error(ctx, "Expected type name");
    } else {
        String type_name = consume(ctx).value.value;
    
        base->type = TYPEREF_NAMED;
        base->named.name = type_name;
        base->named.generic_args = typerefs_array_init();
        base->is_mutable = is_mut;
        
        if (t.has_value) {
            base->token = first.value;
        }

        token_optional gen_check = peek(ctx);
        if (gen_check.has_value && gen_check.value.type == TOKENTYPE_LT) {
            consume(ctx);
            while (true) {
                typerefs_array_push(&base->named.generic_args, parse_type_single(ctx));
                
                gen_check = peek(ctx);
                if (gen_check.has_value && gen_check.value.type == TOKENTYPE_COMMA) {
                    consume(ctx);
                    continue;
                }
                break;
            }
            expect(ctx, TOKENTYPE_GT, "Expected '>' to close type arguments");
        }
    }


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
            ptr->token = star_tok;

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
                error(ctx, "Expected ']'");
            }
            Token rb = consume(ctx);

            TypeRef *array = arena_calloc(ctx->arena, sizeof(TypeRef));
            array->type = TYPEREF_ARRAY;
            array->array.elem = base;
            array->array.length = length;
            array->is_mutable = is_mut;
            array->token = lb;

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

TypeRef *parse_type(Parser *ctx) {
    TypeRef *left = parse_type_single(ctx);

    token_optional t = peek(ctx);
    if (t.has_value && t.value.type == TOKENTYPE_PIPE) {
        TypeRef *sum_type = arena_calloc(ctx->arena, sizeof(TypeRef));
        sum_type->type = TYPEREF_SUM;
        sum_type->token = t.value;
        sum_type->sum.cases = typerefs_array_init();

        typerefs_array_push(&sum_type->sum.cases, left);
        while (t.has_value && t.value.type == TOKENTYPE_PIPE) {
            consume(ctx);

            TypeRef *next_case = parse_type_single(ctx);
            typerefs_array_push(&sum_type->sum.cases, next_case);

            t = peek(ctx);
        }
        return sum_type;
    }

    return left;
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

Literal new_lit(Parser *ctx, Token t) {
    Literal lit;
    if (t.type == TOKENTYPE_INT_LIT) {
        lit = (Literal){
            .type = LITERAL_INT,
            .raw = t.value.value,
            ._int = strtoll(t.value.value.data, NULL, 0),
            .token = t
        };
    }
    else if (t.type == TOKENTYPE_STR_LIT) {
        lit = (Literal){
            .type = LITERAL_STRING,
            .raw = t.value.value,
            .string = t.value.value,
            .token = t
        };
    }
    else if (t.type == TOKENTYPE_TRUE || t.type == TOKENTYPE_FALSE) {
        lit = (Literal){
            .type = LITERAL_BOOL,
            .raw = t.value.value,
            ._bool = (t.type == TOKENTYPE_TRUE),
            .token = t
        };
    }
    else if (t.type == TOKENTYPE_CHAR_LIT) {
        lit = (Literal){
            .type = LITERAL_CHAR,
            .raw = t.value.value,
            ._char = t.value.value.data[0],
            .token = t
        };
    }
    else if (t.type == TOKENTYPE_NULL) {
        lit = (Literal){
            .type = LITERAL_NULL,
            .raw = t.value.value,
            .token = t
        };
    }
    else {
        error(t, format("Unknown literal %.*s", string_fmt(t.value.value)));
    }

    return lit;
}

Expr *expr_new_lit(Parser *ctx, Token t) {
    Expr *e = arena_calloc(ctx->arena, sizeof(Expr));
    e->type = EXPR_LIT;
    e->token = t;
    
    e->literal = new_lit(ctx, t);

    return e;
}

Expr *expr_new_ident(Parser *ctx, Token t) {
    string_array comps = string_array_init();
    string_array_push(&comps, t.value.value);

    Token last = t;
    while (peek(ctx).has_value && peek(ctx).value.type == TOKENTYPE_DOUBLECOLON) {
        consume(ctx);
        Token comp = consume(ctx);
        if (comp.type != TOKENTYPE_IDENT) {
            error(ctx, "Expected ident after '::'");
        }

        string_array_push(&comps, comp.value.value);
        last = comp;
    }

    Expr *e = arena_calloc(ctx->arena, sizeof(Expr));
    e->token = t;

    if (comps.len == 1) {
        e->type = EXPR_IDENT;
        e->ident.name = comps.data[0];
    } else {
        e->type = EXPR_PATH;
        e->path.components = comps;
    }

    return e;
}

Expr *parse_expr_prefix(Parser *ctx) {
    token_optional t = peek(ctx);

    if (!t.has_value) {
        error(ctx, "Expected a token for expression");
    }

    if (t.has_value && t.value.type == TOKENTYPE_PLUS) {
        
    }

    if (t.value.type == TOKENTYPE_DOLLAR) {
        Token dollar = consume(ctx);
        Token name = expect(ctx, TOKENTYPE_IDENT, "Expected intrinsic name after '$'");
        expect(ctx, TOKENTYPE_LPAREN, "Expected '(' after intrinsic name");

        Expr *intr = arena_calloc(ctx->arena, sizeof(Expr));
        intr->type = EXPR_INTRINSIC;
        intr->token = dollar;
        intr->intrinsic.name = name.value.value;

        if (peek(ctx).has_value && looks_like_type(ctx, NULL)) {
            intr->intrinsic.is_arg_type = true;
            intr->intrinsic.type = parse_type(ctx);
        } else {
            intr->intrinsic.is_arg_type = false;
            intr->intrinsic.expr = parse_expr(ctx, 0);
        }

        expect(ctx, TOKENTYPE_RPAREN, "Expected ')' to close intrinsic");
        return intr;
    }

    if (t.value.type == TOKENTYPE_INT_LIT || t.value.type == TOKENTYPE_STR_LIT || t.value.type == TOKENTYPE_CHAR_LIT || t.value.type == TOKENTYPE_TRUE || t.value.type == TOKENTYPE_FALSE || t.value.type == TOKENTYPE_NULL) {
        Expr *e = expr_new_lit(ctx, t.value);
        consume(ctx);
        return e;
    }

    if (t.value.type == TOKENTYPE_IDENT) {
        consume(ctx);
        Expr *e = expr_new_ident(ctx, t.value);
        return e;
    }

    if (t.value.type == TOKENTYPE_LPAREN) {
        Token start = consume(ctx);

        if (looks_like_type(ctx, NULL)) {
            TypeRef *to = parse_type(ctx);
            expect(ctx, TOKENTYPE_RPAREN, "Expected ')' after type cast");
            Expr *target = parse_expr(ctx, 80);
            Expr *e = arena_calloc(ctx->arena, sizeof(Expr));
            e->type = EXPR_CAST;
            e->cast.to = to;
            e->cast.expr = target;
            e->token = start;
            return e;
        }
        Expr *inner = parse_expr(ctx, 0);
        expect(ctx, TOKENTYPE_RPAREN, "Expected ')'");
        return inner;
    }

    UnaryOp uop;
    

    if (token_to_unary(t.value.type, &uop)) {
        Token start = consume(ctx);
        Expr *operand = parse_expr(ctx, 80);
        Expr *e = arena_calloc(ctx->arena, sizeof(Expr));
        e->type = EXPR_UNARY;
        e->unary.op = uop;
        e->unary.operand = operand;
        e->token = start;
        return e;
    }

    /* Debug: print token type to help trace why parse_expr_prefix was called
       on an unexpected token. */
    error(ctx, "Unexpected token in expression");
    error(ctx, "Unexpected token in expression");
}

Expr *expr_handle_postfix(Parser *ctx, Expr *left) {
    while (true) {
        token_optional next = peek(ctx);
        if (next.has_value && next.value.type == TOKENTYPE_LPAREN) {
            Token lparen = consume(ctx);

            exprs_array args = exprs_array_init();
            token_optional argpeek = peek(ctx);
            if (!argpeek.has_value || argpeek.value.type != TOKENTYPE_RPAREN) {
                while (true) {
                    exprs_array_push(&args, parse_expr(ctx, 0));
                    token_optional comma = peek(ctx);
                    if (!comma.has_value || comma.value.type != TOKENTYPE_COMMA) break;
                    consume(ctx);
                }
            }

            Token rp = expect(ctx, TOKENTYPE_RPAREN, "Expected ')' after call arguments");

            Expr *call = arena_calloc(ctx->arena, sizeof(Expr));
            call->type = EXPR_CALL;
            call->call.callee = left;
            call->call.args = args;
            call->token = left->token;
            left = call;
            continue;
        } else if (next.has_value && next.value.type == TOKENTYPE_LBRACK) {
            Token lb = consume(ctx);
            Expr *len = parse_expr(ctx, 0);
            Token rb = consume(ctx);
            if (rb.type != TOKENTYPE_RBRACK) {
                error(ctx, "Expected ']'");
            }

            Expr *index = arena_calloc(ctx->arena, sizeof(Expr));
            index->token = lb;
            index->type = EXPR_INDEX;
            index->index.base = left;
            index->index.index = len;

            left = index;
            continue;
        }
        else if (next.has_value && (next.value.type == TOKENTYPE_DOT || next.value.type == TOKENTYPE_RARROW)) {
            consume(ctx);
            Token mem = consume(ctx);
            if (mem.type != TOKENTYPE_IDENT) {
                error(ctx, "Expected member name after '.'");
            }
            Expr *m = arena_calloc(ctx->arena, sizeof(Expr));
            m->token = mem;
            m->type = EXPR_MEMBER;
            m->member.base = left;
            m->member.member = mem.value.value;
            m->member.deref = next.value.type == TOKENTYPE_RARROW;
            left = m;
            continue;
        } else if (next.has_value && next.value.type == TOKENTYPE_QUESTION) {
            Token q = consume(ctx);
            Expr *e = arena_calloc(ctx->arena, sizeof(Expr));
            e->token = q;
            e->type = EXPR_BUBBLE;
            e->bubble.expr = left;
            left = e;
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
    token_optional start = peek(ctx);

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
        if (start.has_value) {
            b->token = start.value;
        }

        left = b;

        left = expr_handle_postfix(ctx, left);
    }

    return left;
}

FnDecl *parse_fn_signature(Parser *ctx);

static void parse_optional_generic_params(Parser *ctx, genparam_array *out_params) {
    token_optional t = peek(ctx);
    if (!t.has_value || t.value.type != TOKENTYPE_LT) {
        *out_params = genparam_array_init();
        return;
    }

    consume(ctx);
    *out_params = genparam_array_init();

    while (true) {
        Token param_tok = expect(ctx, TOKENTYPE_IDENT, "Expected generic parameter name");
        GenericParam param = {
            .name = param_tok.value.value,
            .token = param_tok,
            .constraints = fndecls_array_init()
        };

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_COLON) {
            consume(ctx);
            
            while (true) {
                FnDecl *method = parse_fn_signature(ctx);
                fndecls_array_push(&param.constraints, method);

                t = peek(ctx);
                if (t.has_value && t.value.type == TOKENTYPE_SEMICOLON) {
                    consume(ctx);
                    
                    t = peek(ctx);
                    if (t.has_value && t.value.type == TOKENTYPE_FN) {
                        continue;
                    }
                }
                break;
            }
        }

        genparam_array_push(out_params, param);

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_COMMA) {
            consume(ctx);
            continue;
        }
        break;
    }

    expect(ctx, TOKENTYPE_GT, "Expected '>' to close generic parameters");
}

Stmt *parse_return_stmt(Parser *ctx) {
    Token start = consume(ctx);
    Expr *value = NULL;
    token_optional t = peek(ctx);
    if (!t.has_value || t.value.type != TOKENTYPE_SEMICOLON) {
        value = parse_expr(ctx, 0);
    }

    expect(ctx, TOKENTYPE_SEMICOLON, "Expected semicolon");

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = start;
    s->type = STMT_RETURN;
    s->_return.value = value;

    return s;
}

Stmt *parse_for_stmt(Parser *ctx) {
    Token start = consume(ctx);

    expect(ctx, TOKENTYPE_LPAREN, "Expected '(' after 'for'");

    Stmt *init = NULL;
    token_optional t = peek(ctx);

    if (!t.has_value) {
        error(ctx, "Unexpected end of input in for-init");
    }

    if (t.value.type == TOKENTYPE_SEMICOLON) {
        expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';' after for-init");
    } else {
        if (looks_like_type(ctx, &t)) {
            init = parse_var_stmt(ctx);
            t = peek(ctx);
            if (t.has_value && t.value.type == TOKENTYPE_SEMICOLON) {
                expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';' after declaration in for-init");
            }
        } else {
            init = parse_expr_stmt(ctx);
            expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';' after for-init expression");
        }
    }

    Expr *cond = NULL;
    t = peek(ctx);
    if (!t.has_value) {
        error(ctx, "Unexpected end of input in for condition");
    }
    if (t.value.type != TOKENTYPE_SEMICOLON) {
        cond = parse_expr(ctx, 0);
    }
    expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';' after for condition");

    Expr *post = NULL;
    t = peek(ctx);
    if (!t.has_value) {
        error(ctx, "Unexpected end of input in for post expression");
    }
    if (t.value.type != TOKENTYPE_RPAREN) {
        post = parse_expr(ctx, 0);
    }
    expect(ctx, TOKENTYPE_RPAREN, "Expected ')' after for clauses");

    t = peek(ctx);
    if (!t.has_value || t.value.type != TOKENTYPE_LBRACE) {
        error(ctx, "Expected '{' to start for body");
    }
    Stmt *body = parse_block_stmt(ctx);

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = start;
    s->type = STMT_FOR;
    s->_for.init = init;
    s->_for.cond = cond;
    s->_for.post = post;
    s->_for.body = body;

    return s;
}


Stmt *parse_if_stmt(Parser *ctx) {
    Token start = consume(ctx);

    expect(ctx, TOKENTYPE_LPAREN, "Expected ')'");
    Expr *cond = parse_expr(ctx, 0);
    expect(ctx, TOKENTYPE_RPAREN, "Expected ')'");

    Stmt *then = parse_block_stmt(ctx);
    Stmt *_else = NULL;
    token_optional t = peek(ctx);
    if (t.has_value && t.value.type == TOKENTYPE_ELSE) {
        consume(ctx);
        t = peek(ctx);
        if (!t.has_value || t.value.type != TOKENTYPE_LBRACE) {
            error(ctx, "Expected '{'");
        }
        _else = parse_block_stmt(ctx);
    }

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = start;
    s->type = STMT_IF;
    s->_if.cond = cond;
    s->_if.then = then;
    s->_if._else = _else;

    return s;
}

Stmt *parse_while_stmt(Parser *ctx) {
    Token start = consume(ctx);

    token_optional t = peek(ctx);
    if (!t.has_value || t.value.type != TOKENTYPE_LPAREN) {
        error(ctx, "Expected '('");
    }
    Expr *cond = parse_expr(ctx, 0);

    t = peek(ctx);
    if (!t.has_value || t.value.type != TOKENTYPE_LBRACE) {
        error(ctx, "Expected '{'");
    }
    Stmt *body = parse_block_stmt(ctx);

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = start;
    s->type = STMT_WHILE;
    s->_while.cond = cond;
    s->_while.body = body;

    return s;
}

VarDecl *parse_var_decl(Parser *ctx) {
    TypeRef *type = NULL;

    if (looks_like_type(ctx, NULL)) {
        type = parse_type(ctx);
    }

    Token name = expect(ctx, TOKENTYPE_IDENT, "Expected variable name");

    VarDecl *v = arena_calloc(ctx->arena, sizeof(VarDecl));
    v->token = name;
    v->type = type;
    v->name = name.value.value;

    token_optional t = peek(ctx);
    if (t.has_value && t.value.type == TOKENTYPE_EQ) {
        consume(ctx);
        v->init = parse_expr(ctx, 0);
    }
    
    return v;
}

Stmt *parse_var_stmt(Parser *ctx) {
    VarDecl *v = parse_var_decl(ctx);
    expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';' after declaration");

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = v->token;
    s->type = STMT_VAR;
    s->var = v;

    return s;
}

Stmt *parse_expr_stmt(Parser *ctx) {
    Expr *e = parse_expr(ctx, 0);
    expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';'");

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = e->token;
    s->type = STMT_EXPR;
    s->expr = e;

    return s;
}

Stmt *parse_defer_stmt(Parser *ctx) {
    Token t = consume(ctx);

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = t;
    s->type = STMT_DEFER;
    s->defer.deferred = parse_stmt(ctx);

    return s; 
}

Stmt *parse_match_stmt(Parser *ctx) {
    Token m = consume(ctx);
    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->type = STMT_MATCH;
    expect(ctx, TOKENTYPE_LPAREN, "Expected '('");
    s->match.expr = parse_expr(ctx, 0);
    expect(ctx, TOKENTYPE_RPAREN, "Expected ')'");
    
    expect(ctx, TOKENTYPE_LBRACE, "Expected '{'");
    
    s->match.cases = case_array_init();
    token_optional t = peek(ctx);
    while (t.has_value && t.value.type != TOKENTYPE_RBRACE) {
        Case c = {
            .var = parse_var_decl(ctx),
            .body = parse_stmt(ctx),
        };
        
        case_array_push(&s->match.cases, c);
        t = peek(ctx);
    }

    expect(ctx, TOKENTYPE_RBRACE, "Expected '}'");

    return s;
}

Stmt *parse_stmt(Parser *ctx) {
    attr_array attrs = attr_array_init();
    collect_attributes(ctx, &attrs);

    token_optional t = peek(ctx);

    if (!t.has_value) {
        error(ctx, "Expected a statement");
    }

    switch (t.value.type) {
        case TOKENTYPE_RETURN: return parse_return_stmt(ctx);
        case TOKENTYPE_FOR: return parse_for_stmt(ctx);
        case TOKENTYPE_IF: return parse_if_stmt(ctx);
        case TOKENTYPE_WHILE: return parse_while_stmt(ctx);
        case TOKENTYPE_DEFER: return parse_defer_stmt(ctx);
        case TOKENTYPE_MATCH: return parse_match_stmt(ctx);
        case TOKENTYPE_LBRACE: return parse_block_stmt(ctx);
        default: break;
    }

    if (t.value.type == TOKENTYPE_MUT || t.value.type == TOKENTYPE_FN) {
        return parse_var_stmt(ctx);
    }

    if (t.value.type == TOKENTYPE_IDENT) {
        if (looks_like_type(ctx, &t) && t.has_value && t.value.type == TOKENTYPE_IDENT) {
            return parse_var_stmt(ctx);
        }

        token_optional next = ahead(ctx, 1);
        if (next.has_value && next.value.type == TOKENTYPE_LPAREN) {
            return parse_expr_stmt(ctx);
        }
    }

    return parse_expr_stmt(ctx);
}

Stmt *parse_block_stmt(Parser *ctx) {
    Token start = consume(ctx);

    stmts_array stmts = stmts_array_init();
    token_optional t = peek(ctx);
    while (t.has_value && t.value.type != TOKENTYPE_RBRACE) {
        stmts_array_push(&stmts, parse_stmt(ctx));
        t = peek(ctx);
    }

    expect(ctx, TOKENTYPE_RBRACE, "Expected '}'");

    Stmt *s = arena_calloc(ctx->arena, sizeof(Stmt));
    s->token = start;
    s->type = STMT_BLOCK;
    s->block.stmts = stmts;

    return s;
}

EnumDecl *parse_enum_decl(Parser *ctx) {
    EnumDecl *en = arena_calloc(ctx->arena, sizeof(EnumDecl));
    en->variants = enumvar_array_init();
    en->token = consume(ctx);
    en->name = expect(ctx, TOKENTYPE_IDENT, "Expected enum name").value.value;

    expect(ctx, TOKENTYPE_LBRACE, "Expected '{'");

    token_optional t = peek(ctx);
    while (t.has_value && t.value.type != TOKENTYPE_RBRACE) {
        Token var_name = expect(ctx, TOKENTYPE_IDENT, "Expected variant name");

        EnumVariant variant = {
            .name = var_name.value.value,
            .token = var_name,
            .value = NULL
        };

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_EQ) {
            consume(ctx);
            variant.value = parse_expr(ctx, 0);
        }

        enumvar_array_push(&en->variants, variant);

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_COMMA) {
            consume(ctx);
            t = peek(ctx);
        } else if (t.has_value && t.value.type != TOKENTYPE_RBRACE) {
            error(ctx, "Expected ',' or '}' after enum variant");
        }
    }

    expect(ctx, TOKENTYPE_RBRACE, "Expected '}'");

    return en;
}

StructDecl *parse_struct_decl(Parser *ctx) {
    StructDecl *str = arena_calloc(ctx->arena, sizeof(StructDecl));
    str->members = vardecls_array_init();
    str->field_offsets = size_array_init();
    str->token = consume(ctx);

    Token name = expect(ctx, TOKENTYPE_IDENT, "Expected struct name");
    str->name = name.value.value;

    parse_optional_generic_params(ctx, &str->generic_params);

    expect(ctx, TOKENTYPE_LBRACE, "Expected '{'");

    token_optional t = peek(ctx);
    while (t.has_value && t.value.type != TOKENTYPE_RBRACE) {
        TypeRef *type = parse_type(ctx);
        Token name = expect(ctx, TOKENTYPE_IDENT, "Expected member name");
        expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';'");

        VarDecl *decl = arena_calloc(ctx->arena, sizeof(VarDecl));
        decl->token = name;
        decl->type = type;
        decl->name = name.value.value;
        vardecls_array_push(&str->members, decl);

        t = peek(ctx);
    }

    expect(ctx, TOKENTYPE_RBRACE, "Expected '}'");

    return str;
}

UnionDecl *parse_union_decl(Parser *ctx) {
    Token start = consume(ctx);
    UnionDecl *un = arena_calloc(ctx->arena, sizeof(UnionDecl));
    un->token = start;
    un->members = vardecls_array_init();

    Token name = expect(ctx, TOKENTYPE_IDENT, "Expected union name");
    un->name = name.value.value;

    parse_optional_generic_params(ctx, &un->generic_params);

    expect(ctx, TOKENTYPE_LBRACE, "Expected '{'");

    token_optional t = peek(ctx);
    while (t.has_value && t.value.type != TOKENTYPE_RBRACE) {
        TypeRef *type = parse_type(ctx);
        Token name = expect(ctx, TOKENTYPE_IDENT, "Expected member name");
        expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';'");

        VarDecl *decl = arena_calloc(ctx->arena, sizeof(VarDecl));
        decl->token = name;
        decl->type = type;
        decl->name = name.value.value;
        vardecls_array_push(&un->members, decl);

        t = peek(ctx);
    }

    expect(ctx, TOKENTYPE_RBRACE, "Expected '}'");

    return un;
}

FnDecl *parse_fn_signature(Parser *ctx) {
    Token start = expect(ctx, TOKENTYPE_FN, "Expected 'fn'");
    FnDecl *fn = arena_calloc(ctx->arena, sizeof(FnDecl));
    fn->token = start;
    fn->params = param_array_init();

    fn->ret_type = parse_type(ctx);
    Token fn_name = expect(ctx, TOKENTYPE_IDENT, "Expected fn name");
    fn->name = fn_name.value.value;

    parse_optional_generic_params(ctx, &fn->generic_params);

    expect(ctx, TOKENTYPE_LPAREN, "Expected '('");

    token_optional t = peek(ctx);
    while (t.has_value && t.value.type != TOKENTYPE_RPAREN) {
        Token param_start = t.value;
        attr_array attrs = attr_array_init();
        collect_attributes(ctx, &attrs);

        TypeRef *param_type = parse_type(ctx);
        Token name = expect(ctx, TOKENTYPE_IDENT, "Expected param name");

        Param p = (Param){
            .type = param_type,
            .name = name.value.value,
            .attributes = attrs,
            .token = param_start,
        };

        t = peek(ctx);
        if (t.has_value && t.value.type == TOKENTYPE_EQ) {
            consume(ctx);
            p.default_value = parse_expr(ctx, 0);
        }

        param_array_push(&fn->params, p);
        t = peek(ctx);
        if (!t.has_value || t.value.type != TOKENTYPE_COMMA) break;
        consume(ctx);
        t = peek(ctx);
    }
    expect(ctx, TOKENTYPE_RPAREN, "Expected ')'");

    return fn;
}

FnDecl *parse_fn_decl(Parser *ctx) {
    FnDecl *fn = parse_fn_signature(ctx);

    token_optional t = peek(ctx);
    if (t.has_value && t.value.type == TOKENTYPE_LBRACE) {
        fn->body = parse_block_stmt(ctx);
    }
    else if (t.has_value && t.value.type == TOKENTYPE_SEMICOLON) {
        consume(ctx);
        fn->body = NULL;
    } else {
        error(ctx, "Expected ';' or function body");
    }

    return fn;
}

TypeDecl *parse_type_decl(Parser *ctx) {
    Token start = consume(ctx);
    TypeDecl *ty = arena_calloc(ctx->arena, sizeof(TypeDecl));
    ty->token = start;
    ty->name = expect(ctx, TOKENTYPE_IDENT, "Expected type alias name").value.value;
    expect(ctx, TOKENTYPE_EQ, "Expected '='");
    ty->alias = parse_type(ctx);
    expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';'");

    return ty;
}

Decl *parse_decl(Parser *ctx) {
    Decl *d = arena_calloc(ctx->arena, sizeof(Decl));
    d->attributes = attr_array_init();
    collect_attributes(ctx, &d->attributes);

    token_optional t = peek(ctx);
    if (!t.has_value) {
        error(ctx, "Unexpected end of file");
    }

    switch (t.value.type) {
        case TOKENTYPE_FN: {
            d->type = DECL_FN;
            d->fn = parse_fn_decl(ctx);
            d->token = d->fn->token;
            break;
        }
        case TOKENTYPE_STRUCT: {
            d->type = DECL_STRUCT;
            d->_struct = parse_struct_decl(ctx);
            d->token = d->_struct->token;
            break;
        }
        case TOKENTYPE_UNION: {
            d->type = DECL_UNION;
            d->_union = parse_union_decl(ctx);
            d->token = d->_union->token;
            break;
        }
        case TOKENTYPE_TYPE: {
            d->type = DECL_TYPE;
            d->_type = parse_type_decl(ctx);
            d->token = d->_type->token;
            break;
        }
        case TOKENTYPE_ENUM: {
            d->type = DECL_ENUM;
            d->_enum = parse_enum_decl(ctx);
            d->token = d->_enum->token;
            break;
        }
        default: {
            error(ctx, "Expected declaration");
        }
    }

    return d;
}

Module *parse_module(Parser *ctx) {
    Token start = expect(ctx, TOKENTYPE_MODULE, "Expected module");

    Module *m = arena_calloc(ctx->arena, sizeof(Module));
    m->token = start;
    m->includes = includes_array_init();
    m->decls = decls_array_init();

    Token modname = expect(ctx, TOKENTYPE_IDENT, "Expected module name");
    m->name = modname.value.value;

    expect(ctx, TOKENTYPE_SEMICOLON, "Expected ';'");

    token_optional t = peek(ctx);
    while (t.has_value) {
        if (t.value.type != TOKENTYPE_INCLUDE) break;
        includes_array_push(&m->includes, parse_include(ctx));
        t = peek(ctx);
    }

    while (t.has_value) {
        decls_array_push(&m->decls, parse_decl(ctx));
        t = peek(ctx);
    }

    return m;
}