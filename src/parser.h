#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lexer.h"
#include "arena.h"

typedef struct {
    size_t start_line;
    size_t start_col;
    size_t end_line;
    size_t end_col;
} Span;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;
typedef struct VarDecl VarDecl;
typedef struct FnDecl FnDecl;
typedef struct Param Param;
typedef struct StructDecl StructDecl;
typedef struct TypeRef TypeRef;
typedef struct Attribute Attribute;
typedef struct Include Include;
typedef struct Module Module;
typedef struct Symbol Symbol;
typedef struct Scope Scope;

typedef enum {
    TYPE_PRIMITIVE,
    TYPE_NAMED,
    TYPE_POINTER,
    TYPE_SLICE,
} TypeRefKind;

struct TypeRef {
    TypeRefKind kind;
    Span span;

    bool is_mutable : 1;
    bool is_optional : 1;

    union {
        char *primitive_name;
        char *named;
        TypeRef *pointee;
        struct {
            TypeRef *elem;
        } slice;
    };

    Symbol *type_symbol;
};

struct Attribute {
    char *name;
    char **args;
    size_t arg_count;
    Span span;
};

struct Param {
    TypeRef *type;
    char *name;
    Attribute *attributes;
    size_t attr_count;
    Span span;
    Symbol *symbol;
};

struct VarDecl {
    TypeRef *type;
    char *name;
    Expr *init;
    Attribute *attributes;
    size_t attr_count;
    Span span;
    Symbol *symbol;
    bool is_mutable : 1;
    bool is_global : 1;
    bool is_definitely_initialized : 1;
};

struct FnDecl {
    Attribute *attributes;
    size_t attr_count;
    bool is_export : 1;
    bool is_extern : 1;
    bool is_unsafe : 1;
    TypeRef *ret_type;
    char *name;
    Param *params;
    size_t param_count;
    Stmt *body;
    Span span;
    Symbol *symbol;
    Scope *local_scope;
};

struct StructDecl {
    char *name;
    Attribute *attributes;
    size_t attr_count;
    Decl **members;
    size_t member_count;
    bool is_export : 1;
    Span span;
    Symbol *symbol;
    size_t size_in_bytes;
    size_t align_in_bytes;
    size_t *field_offsets;
};

struct Include {
    char **path;
    size_t path_len;
    char *alias;
    Span span;
    Module *resolved_module;
};

typedef enum {
    LIT_INT,
    LIT_FLOAT,
    LIT_STRING,
    LIT_BOOL,
    LIT_NULL,
} LiteralKind;

typedef struct {
    LiteralKind kind;
    Span span;
    char *raw;
    int64_t int_value;
    double  float_value;
    char   *str_value;
    bool    bool_value : 1;
} Literal;

typedef enum {
    UOP_NEG,
    UOP_NOT,
    UOP_DEREF,
    UOP_ADDR,
} UnaryOp;

typedef enum {
    BINOP_MUL,
    BINOP_DIV,
    BINOP_MOD,
    BINOP_ADD,
    BINOP_SUB,
    BINOP_SHL,
    BINOP_SHR,
    BINOP_LT,
    BINOP_LE,
    BINOP_GT,
    BINOP_GE,
    BINOP_EQ,
    BINOP_NE,
    BINOP_AND,
    BINOP_XOR,
    BINOP_OR,
    BINOP_LOGICAL_AND,
    BINOP_LOGICAL_OR,
    BINOP_ASSIGN,
    BINOP_ADD_ASSIGN,   //TODO: add more of these
} BinaryOp;

typedef enum {
    EXPR_LIT,
    EXPR_IDENT,
    EXPR_PATH,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL,
    EXPR_INDEX,
    EXPR_MEMBER,
    EXPR_CAST,
    EXPR_ADDR_OF,
    EXPR_DEREF,
} ExprKind;

struct Expr {
    ExprKind kind;
    Span span;
    TypeRef *resolved_type;
    Symbol  *symbol;
    bool     is_constant : 1;
    union {
        Literal lit;
        struct {
            char *name;
        } ident;
        struct {
            char **components;
            size_t comp_count;
        } path;
        struct {
            UnaryOp op;
            Expr *operand;
        } unary;
        struct {
            BinaryOp op;
            Expr *left;
            Expr *right;
        } binary;
        struct {
            Expr *callee;
            Expr **args;
            size_t arg_count;
        } call;
        struct {
            Expr *base;
            Expr *index;
        } index;
        struct {
            Expr *base;
            char *member;
        } member;
        struct {
            TypeRef *to;
            Expr *expr;
        } cast;
    };
};

typedef enum {
    STMT_VAR,
    STMT_EXPR,
    STMT_BLOCK,
    STMT_RETURN,
    STMT_IF,
    STMT_FOR,
    STMT_WHILE,
    STMT_UNSAFE,
    STMT_EMPTY
} StmtKind;

struct Stmt {
    StmtKind kind;
    Span span;
    Scope *scope;
    union {
        VarDecl *var;
        Expr *expr;
        struct {
            Stmt **stmts;
            size_t stmt_count;
        } block;
        Expr *ret_expr;
        struct {
            Expr *cond;
            Stmt *then_branch;
            Stmt *else_branch;
        } _if;
        struct {
            Stmt *init;
            Expr *cond;
            Expr *post;
            Stmt *body;
        } _for;
        struct {
            Expr *cond;
            Stmt *body;
        } _while;
        struct {
            Stmt **stmts;
            size_t stmt_count;
        } unsafe_blk;
    };
};

typedef enum {
    DECL_FN,
    DECL_VAR,
    DECL_STRUCT,
    DECL_INCLUDE,
    DECL_ATTR,
} DeclKind;

struct Decl {
    DeclKind kind;
    Span span;
    Symbol *symbol;
    union {
        FnDecl    *fn;
        VarDecl   *var;
        StructDecl *strct;
        Include   *inc;
        Attribute *attr;
    };
};

struct Module {
    char *name;
    Include **includes;
    size_t include_count;
    Decl **decls;
    size_t decl_count;
    Span span;
    Scope *module_scope;
};

struct Symbol {
    char *name;
    Decl *decl;
    TypeRef *type;
    unsigned flags;
    Scope *defined_in;
};

struct Scope {
    Symbol **symbols;
    size_t sym_count;
    Scope *parent;
};

enum {
    SYMBOL_FLAG_FUNCTION = 1 << 0,
    SYMBOL_FLAG_VARIABLE = 1 << 1,
    SYMBOL_FLAG_TYPE     = 1 << 2,
    SYMBOL_FLAG_EXPORT   = 1 << 3,
    SYMBOL_FLAG_EXTERN   = 1 << 4,
    SYMBOL_FLAG_MUTABLE  = 1 << 5,
};

Module *parser(TokenBuffer *t, Arena *a);