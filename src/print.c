#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "parser.h"
#include "internal.h"

static void print_indent(int indent) {
    for (int i = 0; i < indent; ++i) putchar(' ');
}

static const char *safe_str(const char *s) {
    return s ? s : "<null>";
}

static void print_attributes(Attribute *attrs, size_t count, int indent) {
    if (!attrs || count == 0) return;
    for (size_t i = 0; i < count; ++i) {
        print_indent(indent + 2);
        printf("@%s", safe_str(attrs[i].name));
        if (attrs[i].arg_count > 0) {
            putchar('(');
            for (size_t j = 0; j < attrs[i].arg_count; ++j) {
                if (j) printf(", ");
                printf("%s", safe_str(attrs[i].args[j]));
            }
            putchar(')');
        }
        putchar('\n');
    }
}

static void format_type(TypeRef *type) {
    bool pointer = false;
    bool slice = false;
    switch (type->kind) {
        case TYPE_NAMED: {
            printf(": %s", type->named); 
            break;
        }
        case TYPE_POINTER: {
            printf(": *");
            pointer = true;
            break;
        }
        case TYPE_SLICE: {
            printf(": slice"); 
            slice = true;
            break;
        }
        default: {
            printf(": (unknown)");
            break;
        }
    }

    if (type->is_mutable) printf(" mut");
    if (type->is_optional) printf(" opt");
    if (pointer) format_type(type->pointee);
    if (slice) format_type(type->slice.elem);
}

static void print_params(Param *params, size_t count, int indent) {
    if (!params || count == 0) {
        print_indent(indent + 2);
        puts("(no params)");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        print_indent(indent + 2);
        printf("- Param %zu: %s", i, safe_str(params[i].name));
        if (params[i].type) {
            format_type(params[i].type);
        }
        putchar('\n');
        print_attributes(params[i].attributes, params[i].attr_count, indent + 2);
    }
}

static void print_literal(const Literal *lit, int indent) {
    if (!lit) return;
    print_indent(indent + 2);
    switch (lit->kind) {
        case LIT_INT:    printf("Literal int: %lld\n", (long long)lit->int_value); break;
        case LIT_FLOAT:  printf("Literal float: %g\n", lit->float_value); break;
        case LIT_STRING: printf("Literal string: \"%s\"\n", safe_str(lit->str_value)); break;
        case LIT_BOOL:   printf("Literal bool: %s\n", lit->bool_value ? "true" : "false"); break;
        case LIT_NULL:   printf("Literal null\n"); break;
        default:         printf("Literal <unknown>\n"); break;
    }
}

static void print_expr(const Expr *e, int indent) {
    if (!e) {
        print_indent(indent + 2);
        puts("<expr null>");
        return;
    }
    switch (e->kind) {
        case EXPR_LIT:
            print_literal(&e->lit, indent + 2);
            break;
        case EXPR_IDENT:
            print_indent(indent + 2); printf("Ident: %s\n", safe_str(e->ident.name));
            break;
        case EXPR_PATH:
            print_indent(indent + 2); printf("Path:");
            for (size_t i = 0; i < e->path.comp_count; ++i) {
                printf(" %s", safe_str(e->path.components[i]));
                if (i + 1 < e->path.comp_count) printf("::");
            }
            putchar('\n');
            break;
        case EXPR_UNARY:
            print_indent(indent + 2); printf("Unary op %d\n", e->unary.op);
            print_expr(e->unary.operand, indent + 2);
            break;
        case EXPR_BINARY:
            print_indent(indent + 2); printf("Binary op %d\n", e->binary.op);
            print_expr(e->binary.left, indent + 2);
            print_expr(e->binary.right, indent + 2);
            break;
        case EXPR_CALL:
            print_indent(indent + 2); printf("Call:\n");
            print_expr(e->call.callee, indent + 2);
            for (size_t i = 0; i < e->call.arg_count; ++i) {
                print_expr(e->call.args[i], indent + 2);
            }
            break;
        default:
            print_indent(indent + 2); printf("Expr kind %d (not printed in detail)\n", e->kind);
            break;
    }
}

static void print_stmt(const Stmt *s, int indent) {
    if (!s) {
        print_indent(indent + 2);
        puts("<stmt null>");
        return;
    }
    print_indent(indent + 2);
    switch (s->kind) {
        case STMT_RETURN:
            puts("Return:");
            print_expr(s->ret_expr, indent + 2);
            break;
        case STMT_EXPR:
            puts("Expr stmt:");
            print_expr(s->expr, indent + 2);
            break;
        case STMT_BLOCK:
            puts("Block:");
            for (size_t i = 0; i < s->block.stmt_count; ++i) {
                print_stmt(s->block.stmts[i], indent + 2);
            }
            break;
        case STMT_IF:
            puts("If:");
            print_expr(s->_if.cond, indent + 2);
            print_stmt(s->_if.then_branch, indent + 2);
            if (s->_if.else_branch) {
                print_indent(indent + 2);
                puts("Else:");
                print_stmt(s->_if.else_branch, indent + 2);
            }
            break;
        case STMT_WHILE:
            puts("While:");
            print_expr(s->_while.cond, indent + 2);
            print_stmt(s->_while.body, indent + 2);
            break;
        case STMT_FOR:
            puts("For:");
            if (s->_for.init) { print_stmt(s->_for.init, indent + 2); }
            if (s->_for.cond) { print_expr(s->_for.cond, indent + 2); }
            if (s->_for.post) { print_expr(s->_for.post, indent + 2); }
            print_stmt(s->_for.body, indent + 2);
            break;
        case STMT_EMPTY:
            puts("Empty stmt");
            break;
        case STMT_VAR:
            printf("Var");
            format_type(s->var->type);
            printf(" %s", s->var->name);
            if (s->var->init) {
                printf(" = ");
                print_expr(s->var->init, -4);
            } else putc('\n', stdout);
            print_attributes(s->var->attributes, s->var->attr_count, indent + 2);
            break;
        default:
            printf("Stmt kind %d (not printed in detail)\n", s->kind);
            break;
    }
}

static void print_fn_decl(const FnDecl *fn, int indent) {
    if (!fn) return;
    print_indent(indent + 2);
    printf("Function: %s\n", safe_str(fn->name));
    print_attributes(fn->attributes, fn->attr_count, indent + 2);

    print_indent(indent + 2);
    printf("Return type");
    format_type(fn->ret_type);
    putc('\n', stdout);

    print_indent(indent + 2);
    puts("Parameters:");
    print_params(fn->params, fn->param_count, indent + 2);

    print_indent(indent + 2);
    puts("Body:");
    if (fn->body) print_stmt(fn->body, indent + 2);
    else { print_indent(indent + 2); puts("<no body>"); }
}

static void print_include(const Include *inc, int indent) {
    if (!inc) return;
    print_indent(indent + 2);
    printf("Include:");
    if (inc->alias) printf(" (alias %s)", inc->alias);
    putchar('\n');

    print_indent(indent + 2);
    printf("Path: ");
    if (inc->path_len == 0) {
        printf(" <empty>");
    } else {
        for (size_t i = 0; i < inc->path_len; ++i) {
            if (i) printf("::");
            printf("%s", safe_str(inc->path[i]));
        }
    }
    putchar('\n');
}

static void print_decl(const Decl *d, int index, int indent) {
    if (!d) {
        print_indent(indent + 2);
        printf("- Decl %d: <null>\n", index);
        return;
    }
    print_indent(indent + 2);
    printf("- Decl %d: kind=%d\n", index, d->kind);
    switch (d->kind) {
        case DECL_FN:
            print_fn_decl(d->fn, indent + 2);
            break;
        case DECL_INCLUDE:
            print_include(d->inc, indent + 2);
            break;
        case DECL_VAR:
            print_indent(indent + 2);
            printf("Var decl (not fully printed)\n");
            break;
        case DECL_STRUCT:
            print_indent(indent + 2);
            printf("Struct decl (name %s)\n", d->strct ? safe_str(d->strct->name) : "<null>");
            break;
        case DECL_ATTR:
            print_indent(indent + 2);
            printf("Attribute decl: %s\n", d->attr ? safe_str(d->attr->name) : "<null>");
            break;
        default:
            print_indent(indent + 2);
            puts("Unknown decl kind");
            break;
    }
}

void pretty_print_module(Module *m) {
    if (!m) return;
    puts("=== Module ===");
    printf("Name: %s\n", safe_str(m->name));
    printf("Includes (total %zu):\n", m->include_count ? m->include_count : 0);
    for (size_t i = 0; i < m->include_count; ++i) {
        print_include(m->includes[i], 0);
    }

    printf("Declarations (total %zu):\n", m->decl_count ? m->decl_count : 0);
    for (size_t i = 0; i < m->decl_count; ++i) {
        print_decl(m->decls[i], (int)i, 0);
    }
    puts("=== End Module ===");
}

void pretty_print_tokens(TokenBuffer *tokens) {
    for (int i = 0; i < tokens->len; i++) {
        Token t = tokens->data[i];
        switch (t.type) {
            case MODULE: puts("MODULE"); break;
            case INCLUDE: puts("INCLUDE"); break;
            case FN: puts("FN"); break;
            case RETURN: puts("RETURN"); break;
            case IDENT: printf("IDENT %s\n", t.value); break;
            case ATTR: printf("ATTR %s\n", t.value); break;
            case MUT: puts("MUT"); break;

            case STRING_LIT: printf("STRING_LIT %s\n", t.value); break;
            case CHAR_LIT: printf("CHAR_LIT %s\n", t.value); break;

            case CHAR: puts("CHAR"); break;
            case STRING: puts("STRING"); break;
            case INT8: puts("INT8"); break;
            case INT16: puts("INT16"); break;
            case INT32: puts("INT32"); break;
            case INT64: puts("INT64"); break;
            case UINT8: puts("UINT8"); break;
            case UINT16: puts("UINT16"); break;
            case UINT32: puts("UINT32"); break;
            case UINT64: puts("UINT64"); break;
            case _NULL: puts("NULL"); break;

            case LPAREN: puts("LPAREN"); break;
            case RPAREN: puts("RPAREN"); break;
            case LBRACE: puts("LBRACE"); break;
            case RBRACE: puts("RBRACE"); break;
            case LBRACK: puts("LBRACK"); break;
            case RBRACK: puts("RBRACK"); break;
            case NUMBER: printf("NUMBER %s\n", t.value); break;
            case COLON: puts("COLON"); break;
            case SEMICOLON: puts("SEMICOLON"); break;
            case DOUBLECOLON: puts("DOUBLECOLON"); break;
            case COMMA: puts("COMMA"); break;
            case DOT: puts("DOT"); break;
            case AMP: puts("AMP"); break;
            case QUESTION: puts("QUESTION"); break;

            case PLUS: puts("PLUS"); break;
            case MINUS: puts("MINUS"); break;
            case STAR: puts("STAR"); break;
            case SLASH: puts("SLASH"); break;
            case LSHIFT: puts("LSHIFT"); break;
            case RSHIFT: puts("RSHIFT"); break;

            case GREATER: puts("GREATER"); break;
            case LESS: puts("LESS"); break;
            case EQ_EQ: puts("EQ_EQ"); break;
            case BANG_EQ: puts("BANG_EQ"); break;
            case GREATER_EQ: puts("GREATER_EQ"); break;
            case LESS_EQ: puts("LESS_EQ"); break;

            case EQ: puts("EQ"); break;
            case PLUS_EQ: puts("PLUS_EQ"); break;
            case MINUS_EQ: puts("MINUS_EQ"); break;
            case STAR_EQ: puts("STAR_EQ"); break;
            case DIV_EQ: puts("DIV_EQ"); break;
            case RSHIFT_EQ: puts("RSHIFT_EQ"); break;
            case LSHIFT_EQ: puts("LSHIFT_EQ"); break;

            case _EOF: puts("EOF"); break;
        }
    }
}