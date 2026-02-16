#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "lexer.h"
#include "parser.h"
#include "arena.h"
#include "internal.h"

static TokenBuffer *tokens = NULL;
static bool failed = false;
static Arena *arena = NULL;

static Token peek() {
    return tokens->data[tokens->pos];
}

static Token consume() {
    return tokens->data[tokens->pos++];
}

static int match(TokenType expected) {
    if (peek().type == expected) {
        consume();
        return 1;
    }
    return 0;
}

static void verror(const char *msg, va_list args) {
    fprintf(stderr, "Parse error at line %zu, column %zu: ", peek().line, peek().column);
    vfprintf(stderr, msg, args);
    fprintf(stderr, " (got %d)\n", peek().type);
    failed = true;
    while (peek().type != SEMICOLON) consume();  // skip to sync point
}

static void error(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    verror(msg, args);
}

static void expect(TokenType expected, const char *msg, ...) {
    va_list args;
    if (!match(expected)) {
        va_start(args, msg);
        verror(msg, args);
    }
}

static Span span(Token a, Token b) {
    return (Span){
        .start_col = a.column,
        .start_line = a.line,
        .end_col = b.column,
        .end_line = b.line,
    };
}

TypeRef *parse_type(void) {
    Token first_tok = peek();

    /* leading mut that applies to the next node (base) */
    bool next_is_mut = false;
    while (peek().type == MUT) {
        consume();
        next_is_mut = true;
    }

    TypeRef *base = NULL;
    Token last_tok = peek();

    if (is_builtin_type_token(peek().type) || peek().type == IDENT) {
        Token t = consume();
        last_tok = t;

        base = arena_calloc(arena, sizeof(TypeRef));
        base->kind = TYPE_NAMED;
        base->is_mutable = next_is_mut;
        base->is_optional = false;
        base->type_symbol = NULL;

        if (t.type == IDENT) {
            base->named = arena_strdup(arena, t.value);
        } else {
            base->named = arena_strdup(arena, builtin_name_for_token(t.type));
        }
    }
    else if (peek().type == LPAREN) {
        Token l = consume();
        TypeRef *inner = parse_type();
        expect(RPAREN, "Missing closing parenthesis in type");
        base = inner;
        last_tok = peek();
        base->is_mutable = next_is_mut;
    }
    else {
        error("Expected type name");
        Token fake = consume();
        last_tok = fake;
        base = arena_calloc(arena, sizeof(TypeRef));
        base->kind = TYPE_NAMED;
        base->named = arena_strdup(arena, "<error>");
        base->is_mutable = next_is_mut;
        base->is_optional = false;
        base->type_symbol = NULL;
    }

    for (;;) {
        if (peek().type != MUT && peek().type != STAR && peek().type != LBRACK) break;

        bool op_is_mut = false;
        while (peek().type == MUT) {
            consume();
            op_is_mut = true;
        }

        if (peek().type == STAR) {
            Token star_tok = consume();
            last_tok = star_tok;

            TypeRef *ptr = arena_calloc(arena, sizeof(TypeRef));
            ptr->kind = TYPE_POINTER;
            ptr->pointee = base;
            ptr->is_mutable = op_is_mut;
            ptr->is_optional = false;
            ptr->type_symbol = NULL;

            if (peek().type == QUESTION) {
                Token q = consume();
                ptr->is_optional = true;
                last_tok = q;
            }

            base = ptr;
            continue;
        }

        if (peek().type == LBRACK) {
            Token lb = consume();
            if (peek().type != RBRACK) {
                error("Expected ']' for slice operator");
                while (peek().type != RBRACK && peek().type != _EOF) consume();
            }
            Token rb = consume();
            last_tok = rb;

            TypeRef *slice = arena_calloc(arena, sizeof(TypeRef));
            slice->kind = TYPE_SLICE;
            slice->slice.elem = base;
            slice->is_mutable = op_is_mut;
            slice->is_optional = false;
            slice->type_symbol = NULL;

            if (peek().type == QUESTION) {
                Token q = consume();
                slice->is_optional = true;
                last_tok = q;
            }

            base = slice;
            continue;
        }

        break;
    }

    base->span = span(first_tok, last_tok);
    return base;
}

FnDecl *parse_fn_sig() {
    Token start = peek();
    FnDecl *fn = arena_calloc(arena, sizeof(FnDecl));
    for (int i = 0; peek().type == ATTR; i++) {
        Attribute *new = arena_calloc(arena, sizeof(Attribute) * (i + 1));
        if (fn->attr_count > 0) memcpy(new, fn->attributes, sizeof(Param) * fn->attr_count);
        fn->attributes = new;

        Token tattr = consume();
        if (peek().type == LPAREN) {     // Attribute has argument(s)
            // TODO: add expression parsing here
        }

        Attribute attr = (Attribute){
            .name = arena_strdup(arena, tattr.value),
            .args = 0,  // TODO: placeholder
            .span = span(tattr, peek()),
        };
        fn->attributes[fn->attr_count++] = attr;
    }
    consume();  // FN

    TypeRef *ret_type = parse_type();
    Token fn_name = consume();
    if (fn_name.type != IDENT) error("Missing function name");
    expect(LPAREN, "Missing function opening parenthesis");

    for (int i = 0; peek().type != RPAREN; i++) {
        Token start = peek();
        TypeRef *param_type = parse_type();
        Token name = consume();
        if (name.type != IDENT) error("Missing argument name");
        
        Param param = {
            .type = param_type,
            .span = span(start, peek()),
            .name = arena_strdup(arena, name.value),  // TODO: this segfaulted somewhere else so untrustworthy
        };

        Param *new = arena_alloc(arena, sizeof(Param) * (i + 1));
        if (fn->param_count > 0)  memcpy(new, fn->params, sizeof(Param) * fn->param_count);
        fn->params = new;

        fn->params[fn->param_count++] = param;
        if (peek().type != COMMA) break;
        consume();  // ,
    }
    consume();  // )

    fn->name = arena_strdup(arena, fn_name.value);
    fn->ret_type = ret_type;

    fn->span = span(start, peek());

    return fn;
}

Include *parse_include() {
    Token start = peek();
    Include *inc = arena_calloc(arena, sizeof(Include));

    // caller ensures INCLUDE token but does not consume
    consume();

    for (int i = 0; true; i++) {
        Token path = consume();
        if (path.type != IDENT) error("Expected include path");
        char **new = arena_alloc(arena, sizeof(char*) * (i + 1));
        if (inc->path_len > 0) memcpy(new, inc->path, sizeof(char*) * inc->path_len);
        inc->path = new;

        inc->path[inc->path_len++] = path.value;
        if (peek().type != DOUBLECOLON) break;
        consume();
    }

    expect(SEMICOLON, "Missing include semicolon");

    inc->span = span(start, peek());
    return inc;
}

Module *parse_module() {
    Token start = peek();
    expect(MODULE, "Missing module declaration");

    Module *m = arena_calloc(arena, sizeof(Module));
    
    // Parse and store module name
    Token modname = consume();
    if (modname.type != IDENT) error("Missing module name");
    m->name = arena_strdup(arena, modname.value);

    consume();  // ;

    // Parse all INCLUDEs
    for (int i = 0; peek().type == INCLUDE; i++) {
        // Increase the size of the includes buffer
        Include **new = arena_alloc(arena, sizeof(Include*) * (i + 1));
        if (m->include_count > 0) memcpy(new, m->includes, sizeof(Include*) * m->include_count);
        m->includes = new;

        // now append the node
        m->includes[m->include_count++] = parse_include();
    }

    //TODO: temporarily parse a function signature
    m->decls = arena_calloc(arena, sizeof(Decl*));
    m->decl_count = 1;
    m->decls[0] = arena_calloc(arena, sizeof(Decl));
    m->decls[0]->kind = DECL_FN;
    m->decls[0]->fn = parse_fn_sig();

    m->span = span(start, peek());
    return m;
}

Module *parser(TokenBuffer *t, Arena *a) {
    tokens = t;
    arena = a;
    Module *m = parse_module();
    if (failed) {
        fprintf(stderr, "Compilation failed.\n");
        exit(1);
    }
    return m;
}