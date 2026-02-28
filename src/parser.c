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

static void error(const char *msg, ...);

static Token peek() {
    Token t = tokens->data[tokens->pos];
    return t;
}

static Token consume() {
    Token t = tokens->data[tokens->pos++];
    return t;
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
    putc('\n', stderr);
    exit(1);
}

static void error(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    verror(msg, args);
    va_end(args);
}

static void expect(TokenType expected, const char *msg, ...) {
    va_list args;
    if (!match(expected)) {
        va_start(args, msg);
        verror(msg, args);
        va_end(args);
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

Expr *expr_new_lit(Token t) {
    Expr *e = arena_calloc(arena, sizeof(Expr));
    e->kind = EXPR_LIT;
    e->lit.raw = arena_strdup(arena, t.value ? t.value : "");
    e->lit.span = span(t, t);

    if (t.type == NUMBER) {
        e->lit.kind = LIT_INT;
        e->lit.int_value = strtoll(t.value, NULL, 0);
    } else if (t.type == STRING) {
        e->lit.kind = LIT_STRING;
        e->lit.str_value = arena_strdup(arena, t.value);
    } else if (t.type == TRUE || t.type == FALSE) {
        e->lit.kind = LIT_BOOL;
        e->lit.bool_value = (t.type == TRUE);
    } else if (t.type == _NULL) {
        e->lit.kind = LIT_NULL;
    } else {
        error("Unknown expression literal %s", t.value);
    }
    e->span = e->lit.span;
    return e;
}

Expr *expr_new_ident(Token t) {
    char **comps = arena_calloc(arena, sizeof(char*));
    size n = 0;
    comps[n++] = arena_strdup(arena, t.value);

    Token last = t;
    while (peek().type == DOUBLECOLON) {
        consume();  // ::
        Token comp = consume();
        if (comp.type != IDENT) {
            error("Expected identifier after '::' in path");
            break;
        }
        char **new = arena_calloc(arena, sizeof(Stmt) * (n + 1));
        if (n > 0) memcpy(new, comps, sizeof(char*) * n);
        comps = new;

        comps[n++] = arena_strdup(arena, comp.value);
        last = comp;
    }

    Expr *e = arena_calloc(arena, sizeof(Expr));
    if (n == 1) {
        e->kind = EXPR_IDENT;
        e->ident.name = comps[0];
        e->span = span(t, last);
    } else {
        e->kind = EXPR_PATH;
        e->path.components = comps;
        e->path.comp_count = n;
        e->span = span(t, last);
    }

    return e;
}

int token_to_unary(TokenType t, UnaryOp *out) {
    switch (t) {
        case MINUS: *out = UOP_NEG; return 1;
        case BANG: *out = UOP_NOT; return 1;
        case STAR: *out = UOP_DEREF; return 1;
        case AMP: *out = UOP_ADDR; return 1;
        default: return 0;
    }
}

int bp_for_binary(TokenType t, int *right_assoc, BinaryOp *out) {
    *right_assoc = 0;

    switch (t) {
        case STAR:  *out = BINOP_MUL;  return 60;
        case SLASH:   *out = BINOP_DIV;  return 60;
        case PERCENT:*out = BINOP_MOD; return 60;

        case PLUS:  *out = BINOP_ADD;  return 50;
        case MINUS: *out = BINOP_SUB;  return 50;

        case LSHIFT:   *out = BINOP_SHL;  return 45;
        case RSHIFT:   *out = BINOP_SHR;  return 45;

        case LESS:    *out = BINOP_LT;   return 40;
        case LESS_EQ:    *out = BINOP_LE;   return 40;
        case GREATER:    *out = BINOP_GT;   return 40;
        case GREATER_EQ:    *out = BINOP_GE;   return 40;

        case EQ_EQ:  *out = BINOP_EQ;   return 35;
        case BANG_EQ:   *out = BINOP_NE;   return 35;

        case AMP:   *out = BINOP_AND;  return 32;
        case CARET: *out = BINOP_XOR;  return 28;
        case PIPE:  *out = BINOP_OR;   return 24;

        case AND_AND: *out = BINOP_LOGICAL_AND; return 15;
        case OR_OR:   *out = BINOP_LOGICAL_OR;  return 10;

        case EQ: *out = BINOP_ASSIGN; *right_assoc = 1; return 5;

        default:
            return 0;
    }
}

Expr *parse_expr_bp(int min_bp);

Expr *parse_expr_prefix() {
    Token t = peek();

    if (t.type == NUMBER || t.type == STRING || t.type == TRUE || t.type == FALSE || t.type == _NULL) {
        consume();
        return expr_new_lit(t);
    }

    if (t.type == IDENT) {
        consume();
        return expr_new_ident(t);
    }

    if (t.type == LPAREN) {
        consume();

        if (is_builtin_type_token(peek().type) || peek().type == MUT || peek().type == IDENT) {
            // TODO: type casting (maybe)
        }
        Expr *inner = parse_expr_bp(0);
        expect(RPAREN, "Expected ')'");
        return inner;
    }

    UnaryOp uop;
    if (token_to_unary(t.type, &uop)) {
        consume();
        Expr *operand = parse_expr_bp(80);
        Expr *e = arena_calloc(arena, sizeof(Expr));
        e->kind = EXPR_UNARY;
        e->unary.op = uop;
        e->unary.operand = operand;
        e->span = span(t, peek());
        return e;
    }

    error("Unexpected token in expression");
    consume();
    Expr *e = arena_calloc(arena, sizeof(Expr));
    e->kind = EXPR_LIT;
    e->lit.kind = LIT_INT;
    e->lit.int_value = 0;
    e->span = span(t, t);
    return e;
}

Expr *handle_postfix(Expr *left) {
    while (1) {
        Token next = peek();
        if (next.type == LPAREN) {
            Token start = consume();
            Expr **args = NULL;
            size arg_count = 0;
            if (peek().type != RPAREN) {
                while (1) {
                    Expr *arg = parse_expr_bp(0);
                    Expr **new = arena_calloc(arena, sizeof(Expr*) + (arg_count + 1));
                    if (arg_count > 0) memcpy(new, args, sizeof(Expr*) * arg_count);
                    args = new;
                    args[arg_count++] = arg;

                    if (peek().type != COMMA) break;
                    consume();  // ,
                }
            }
            Token end = consume();  // )
            Expr *call = arena_calloc(arena, sizeof(Expr));
            call->kind = EXPR_CALL;
            call->call.callee = left;
            call->call.args = args;
            call->call.arg_count = arg_count;
            call->span = span(start, end);
            left = call;
            continue;
        } else if (next.type == LBRACK) {
            Token lb = consume();
            Expr *idx = parse_expr_bp(0);
            Token rb = consume();
            if (rb.type != RBRACK) {
                error("Expected ']'");
            }
            Expr *index = arena_calloc(arena, sizeof(Expr));
            index->kind = EXPR_INDEX;
            index->index.base = left;
            index->index.index = idx;
            index->span = span(next, rb);
            left = index;
            consume();  // ]
            continue;
        } else if (next.type == DOT) {
            consume();
            Token mem = consume();
            if (mem.type != IDENT) {
                error("Expected identifier after '.'");
                mem = (Token){.type = IDENT, .value = "<error>", .line = mem.line, .column = mem.column};
            }
            Expr *m = arena_calloc(arena, sizeof(Expr));
            m->kind = EXPR_MEMBER;
            m->member.base = left;
            m->member.member = arena_strdup(arena, mem.value);
            m->span = span(next, peek());
            left = m;
            continue;
        } else {
            break;
        }
    }
    
    return left;
}

Expr *parse_expr_bp(int min_bp) {
    Token t = peek();
    Expr *left = parse_expr_prefix();

    left = handle_postfix(left);

    while (1) {
        Token next =  peek();
        if (next.type == _EOF || next.type == SEMICOLON || next.type == COMMA || next.type == RPAREN || next.type == RBRACK) {
            break;
        }

        BinaryOp binop;
        int right_assoc = 0;
        int bp = bp_for_binary(next.type, &right_assoc, &binop);
        if (bp == 0 || bp <= min_bp) break;

        Token op_tok = consume();

        int rbp = right_assoc ? bp - 1 : bp;

        Expr *right = parse_expr_bp(rbp);

        Expr *b = arena_calloc(arena, sizeof(Expr));
        b->kind = EXPR_BINARY;
        b->binary.op = binop;
        b->binary.left = left;
        b->binary.right = right;
        b->span = span(t, peek());
        left = b;

        left = handle_postfix(left);
    }

    return left;
}

Expr *parse_expr() {
    return parse_expr_bp(0);
}

Stmt *parse_return_stmt() {
    Token start = consume();    // return
    Expr *value = NULL;
    if (peek().type != SEMICOLON) {
        value = parse_expr();
    }

    expect(SEMICOLON, "Expected semicolon after return");

    Stmt *s = arena_calloc(arena, sizeof(Stmt));
    s->kind = STMT_RETURN;
    s->ret_expr = value;
    s->span = span(start, peek());

    return s;
}

// forward decl for recursive parse_block()
Stmt *parse_stmt();

Stmt *parse_block_stmt() {
    Token start = consume();    // {

    Stmt **stmts = NULL;
    size count = 0;
    for (int i = 0; peek().type != RBRACE; i++) {
        Stmt **new = arena_calloc(arena, sizeof(Stmt*) * (i + 1));
        if (count > 0) memcpy(new, stmts, sizeof(Stmt*) * count);
        stmts = new;

        stmts[count++] = parse_stmt();
    }

    expect(RBRACE, "Expected '}' to close the block");

    Stmt *s = arena_calloc(arena, sizeof(Stmt));
    s->kind = STMT_BLOCK;
    s->block.stmts = stmts;
    s->block.stmt_count = count;
    s->span = span(start, peek());

    return s;
}

Stmt *parse_var_stmt();

Stmt *parse_for_stmt() {
    Token start = consume();    // for

    expect(LPAREN, "Expected '(' after for");
    Stmt *init = parse_var_stmt();
    expect(SEMICOLON, "Expected ';' after for initialiser");
    Expr *cond = parse_expr();
    expect(SEMICOLON, "Expected ';' after for condition");
    Expr *inc = parse_expr();
    expect(RPAREN, "Expected ')' after for incrementer");
    
    if (peek().type != LBRACE) error("Expected '{' for for statement");
    Stmt *body = parse_block_stmt();

    Stmt *s = arena_calloc(arena, sizeof(Stmt));
    s->kind = STMT_FOR;
    s->_for.init = init;
    s->_for.cond = cond;
    s->_for.post = inc;
    s->_for.body = body;
    s->span = span(start, peek());

    return s;
}

Stmt *parse_while_stmt() {
    Token start = consume();    // while

    expect(LPAREN, "Expected '(' after while");
    Expr *cond = parse_expr();
    expect(RPAREN, "Expected ')' after while condition");

    if (peek().type != LBRACE) error("Expected '{' for while statement");
    Stmt *branch = parse_block_stmt();
    
    Stmt *s = arena_calloc(arena, sizeof(Stmt));
    s->kind = STMT_WHILE;
    s->_while.body = branch;
    s->_while.cond = cond;
    s->span = span(start, peek());

    return s;
}

Stmt *parse_if_stmt() {
    Token start = consume();    // if

    expect(LPAREN, "Expected '(' after if");
    Expr *cond = parse_expr();
    expect(RPAREN, "Expected ')' after if condition");

    if (peek().type != LBRACE) error("Expected '{' for if statement");
    Stmt *then = parse_block_stmt();
    Stmt *_else = NULL;
    if (match(ELSE)) {
        if (peek().type != LBRACE) error("Expected '{' for else statement");
        _else = parse_block_stmt();
    }

    Stmt *s = arena_calloc(arena, sizeof(Stmt));
    s->kind = STMT_IF;
    s->_if.cond = cond;
    s->_if.then_branch = then;
    s->_if.else_branch = _else;
    s->span = span(start, peek());

    return s;
}

Stmt *parse_expr_stmt() {
    Token start = peek();
    Expr *e = parse_expr();
    expect(SEMICOLON, "Expected semicolon after expression");

    Stmt *s = arena_calloc(arena, sizeof(Stmt));
    s->kind = STMT_EXPR;
    s->expr = e;
    s->span = span(start, peek());
    return s;
}

Stmt *parse_var_stmt() {
    Token start = peek();
    TypeRef *type = parse_type();

    Token name = consume();
    if (name.type != IDENT) error("Expected variable name");

    VarDecl *var = arena_calloc(arena, sizeof(VarDecl));
    var->type = type;
    var->name = name.value;
    var->span = span(start, name);

    Token next = peek();
    if (next.type == EQ) {
        consume();
        var->init = parse_expr();
    }

    Stmt *s = arena_calloc(arena, sizeof(Stmt));
    s->kind = STMT_VAR;
    s->var = var;
    s->span = next.type == EQ ? span(start, peek()) : span(start, next);

    return s;
}

Stmt *parse_stmt() {
    Token t = peek();

    switch (t.type) {
        case RETURN: return parse_return_stmt();
        case FOR: return parse_for_stmt();
        case IF: return parse_if_stmt();
        case WHILE: return parse_while_stmt();
        case SEMICOLON: {
            Token start = consume();
            Stmt *s = arena_calloc(arena, sizeof(Stmt));
            s->kind = STMT_EMPTY;
            s->span = span(start, start);
            return s;
        }
        default: break;
    }

    if (is_builtin_type_token(t.type) || t.type == MUT) {
        //Variable declaration
        return parse_var_stmt();
    }

    // fallback
    return parse_expr_stmt();
}

void collect_attributes(Attribute **out_attr, size *out_count) {
    if (!out_attr || !out_count) return;

    Attribute *attrs = NULL;
    size count = 0;

    for (int i = 0; peek().type == ATTR; i++) {
        Attribute *new = arena_calloc(arena, sizeof(Attribute) * (i + 1));
        if (count > 0) memcpy(new, attrs, sizeof(Attribute) * count);
        attrs = new;

        Token tattr = consume();

        Attribute attr = (Attribute){
            .name = arena_strdup(arena, tattr.value),
            .args = 0,
            .span = span(tattr, peek()),
        };

        if (peek().type == LPAREN) {
            consume();  // (
            size arg_count = 0;
            char **args = NULL;
            while (1) {
                char **new = arena_calloc(arena, sizeof(char*) * (arg_count + 1));
                if (arg_count > 0) memcpy(new, args, sizeof(char*) * arg_count);
                args = new;

                args[arg_count++] = arena_strdup(arena, consume().value);    // the argument data
                if (peek().type != COMMA) break;
                consume();  // ,
            }

            expect(RPAREN, "Expected ')' after attribute arguments");

            attr.args = args;
            attr.arg_count = arg_count;
        }

        attrs[count++] = attr;
    }

    *out_attr = attrs;
    *out_count = count;
}

// FnDecl *parse_fn_sig() {
//     Token start = peek();
//     FnDecl *fn = arena_calloc(arena, sizeof(FnDecl));
//     collect_attributes(&fn->attributes, &fn->attr_count);
//     consume();  // FN

//     TypeRef *ret_type = parse_type();
//     Token fn_name = consume();
//     if (fn_name.type != IDENT) error("Missing function name");
//     expect(LPAREN, "Missing function opening parenthesis");

//     for (int i = 0; peek().type != RPAREN; i++) {
//         Token start = peek();

//         Attribute *attrs = NULL;
//         size attr_count = 0;
//         collect_attributes(&attrs, &attr_count);

//         TypeRef *param_type = parse_type();
//         Token name = consume();
//         if (name.type != IDENT) error("Missing argument name");
        
//         Param param = {
//             .type = param_type,
//             .span = span(start, peek()),
//             .name = arena_strdup(arena, name.value),
//             .attr_count = attr_count,
//             .attributes = attrs,
//         };

//         Param *new = arena_calloc(arena, sizeof(Param) * (i + 1));
//         if (fn->param_count > 0)  memcpy(new, fn->params, sizeof(Param) * fn->param_count);
//         fn->params = new;

//         fn->params[fn->param_count++] = param;
//         if (peek().type != COMMA) break;
//         consume();  // ,
//     }
//     consume();  // )

//     fn->name = arena_strdup(arena, fn_name.value);
//     fn->ret_type = ret_type;

//     fn->span = span(start, peek());

//     return fn;
// }

FnDecl *parse_fn() {
    Token start = peek();
    FnDecl *fn = arena_calloc(arena, sizeof(FnDecl));
    collect_attributes(&fn->attributes, &fn->attr_count);
    consume();  // FN

    TypeRef *ret_type = parse_type();
    Token fn_name = consume();
    if (fn_name.type != IDENT) error("Missing function name");
    expect(LPAREN, "Missing function opening parenthesis");

    for (int i = 0; peek().type != RPAREN; i++) {
        Token start = peek();

        Attribute *attrs = NULL;
        size attr_count = 0;
        collect_attributes(&attrs, &attr_count);

        TypeRef *param_type = parse_type();
        Token name = consume();
        if (name.type != IDENT) error("Missing argument name");
        
        Param param = {
            .type = param_type,
            .span = span(start, peek()),
            .name = arena_strdup(arena, name.value),
            .attr_count = attr_count,
            .attributes = attrs,
        };

        Param *new = arena_calloc(arena, sizeof(Param) * (i + 1));
        if (fn->param_count > 0)  memcpy(new, fn->params, sizeof(Param) * fn->param_count);
        fn->params = new;

        fn->params[fn->param_count++] = param;
        if (peek().type != COMMA) break;
        consume();  // ,
    }
    consume();  // )

    if (peek().type == LBRACE) {
        fn->body = parse_block_stmt();
    }

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
        char **new = arena_calloc(arena, sizeof(char*) * (i + 1));
        if (inc->path_len > 0) memcpy(new, inc->path, sizeof(char*) * inc->path_len);
        inc->path = new;

        inc->path[inc->path_len++] = arena_strdup(arena, path.value);
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
        Include **new = arena_calloc(arena, sizeof(Include*) * (i + 1));
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
    m->decls[0]->fn = parse_fn();

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