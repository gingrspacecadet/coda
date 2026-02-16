#pragma once

#include <stdbool.h>

typedef struct {
    size start_line, start_col;
    size end_line, end_col;
} Span;

typedef struct {
    enum {
        EXPR_LIT,
        EXPR_IDENT,
        EXPR_PATH,
        EXPR_UNARY,
        EXPR_BINARY,
        EXPR_CALL,
        EXPR_INDEX,
        EXPR_MEMBER_ACCESS,
        EXPR_CAST,
        EXPR_ADDR_OF,
        EXPR_DEREF,
    } type;
    union {

    };
} Expr;

typedef struct {
    enum {
        STMT_VAR,
        STMT_EXPR,
        STMT_BLOCK,
        STMT_RETURN,
        STMT_IF,
        STMT_FOR,
        STMT_WHILE,
        STMT_UNSAFE,
        STMT_EMPTY
    };
    union {
        VarDecl *var;
        struct {
            Expr *expr;
            Stmt *body;
            Stmt *_else;    // optional
        } _if;
        struct {
            
        } _for;
        Expr *_return;
    };
} Stmt;

typedef struct {
    Attribute *attributes;
    bool export : 1;
    bool extern : 1;
    TypeRef ret_type;
    char *name;
    Param *parameters;
    Stmt *body;
    Span span;
} FnDecl;

typedef struct {
    TypeRef type;
    char *name;
    Attribute *attributes;
    Span span;
} Param;

typedef struct {
    TypeRef type;
    char *name;
    Expr *init; // optional
    Attribute *attributes;
    Span span;
} VarDecl;

typedef struct {
    char *name;
    Attribute *attributes;
    Decl **decls;
    bool export : 1;
    Span span;
} Struct;

typedef struct TypeRef {
    enum {
        TYPE_PRIMATIVE,
        TYPE_NAMED,
        TYPE_POINTER,
        TYPE_ARRAY,
    } type;
    union {
        char *name;
        struct TypeRef *pointer;
    };
    bool mutable : 1;
    Span span;
} TypeRef;

typedef struct {
    char *name;
    char **arguments;   // optional
    Span span;
} Attribute;

typedef struct {
    char **path;
    char *alias;    // optional
    Span span;
} Include;

typedef struct {
    char *name;
    Include **includes;
    size include_count;
    Decl **decls;
    size decl_count;
    Span span;
} Module;