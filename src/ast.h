#ifndef AST_H
#define AST_H

#include <stdint.h>
#include "string.h"
#include "array.h"
#include "optional.h"
#include "arena.h"

INSTANTIATE(String, string, ARRAY_TEMPLATE)

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;
typedef struct VarDecl VarDecl;
typedef struct FnDecl FnDecl;
typedef struct StructDecl StructDecl;
typedef struct UnionDecl UnionDecl;
typedef struct TypeRef TypeRef;
typedef struct Symbol Symbol;
typedef struct Scope Scope;
typedef struct Module Module;
typedef struct Include Include;
typedef struct Literal Literal;
typedef struct Param Param;

INSTANTIATE(Expr *, exprs, ARRAY_TEMPLATE)

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
    BINOP_LOG_AND,
    BINOP_LOG_OR,
    BINOP_ASSIGN,
    BINOP_ADD_ASSIGN
} BinaryOp;

typedef enum {
    SYMFLAG_NONE = 0,
    SYMFLAG_FN = 1 << 0,
    SYMFLAG_VAR = 1 << 1,
    SYMFLAG_TYPE = 1 << 2,
    SYMFLAG_EXPORT = 1 << 3,
    SYMFLAG_EXTERN = 1 << 4,
    SYMFLAG_MUT = 1 << 5
} SymbolFlags;

typedef struct {
    String name;
    string_array args;
} Attribute;

struct TypeRef {
    enum {
        TYPEREF_NAMED,
        TYPEREF_POINTER,
        TYPEREF_ARRAY
    } type;
    union {
        struct {
            String name;
        } named;
        struct {
            TypeRef *pointee;
        } pointer;
        struct {
            TypeRef *elem;
            size_t length;
        } array;
    };

    bool is_mutable;
    bool is_optional;
    Symbol *type_symbol;
};

struct Literal {
    enum {
        LITERAL_INT,
        LITERAL_FLOAT,
        LITERAL_STRING,
        LITERAL_BOOL,
        LITERAL_CHAR
    } type;
    String raw;
    union {
        int64_t _int;
        double _float;
        String string;
        bool _bool;
        char _char;
    };
};

struct Expr {
    enum {
        EXPR_LIT,
        EXPR_IDENT,
        EXPR_PATH,
        EXPR_UNARY,
        EXPR_BINARY,
        EXPR_CALL,
        EXPR_INDEX,
        EXPR_MEMBER,
        EXPR_CAST
    } type;

    union {
        Literal literal;
        struct {
            String name;
        } ident;
        struct {
            string_array components;
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
            exprs_array args;
        } call;
        struct {
            Expr *base;
            Expr *index;
        } index;
        struct {
            Expr *base;
            String member;
        } member;
        struct {
            TypeRef *to;
            Expr *expr;
        } cast;
    };

    TypeRef *resolved_Type;
    Symbol *symbol;
    bool is_constant;
};

INSTANTIATE(Stmt *, stmts, ARRAY_TEMPLATE)

struct Stmt {
    enum {
        STMT_VAR,
        STMT_EXPR,
        STMT_BLOCK,
        STMT_RETURN,
        STMT_IF,
        STMT_FOR,
        STMT_WHILE,
        STMT_UNSAFE,
    } type;

    union {
        VarDecl *var;
        Expr *expr;
        struct {
            stmts_array stmts;
        } block;
        struct {
            Expr *value;
        } _return;
        struct {
            Expr *cond;
            Stmt *then;
            Stmt *_else;
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
            stmts_array stmts;
        } unsafe;
    };

    Scope *scope;
};

INSTANTIATE(Attribute, attr, ARRAY_TEMPLATE)

struct Param {
    TypeRef *type;
    String name;
    attr_array attributes;
    Symbol *symbol;
};

struct VarDecl {
    TypeRef *type;
    String name;
    Expr *init;
    attr_array attributes;
    Symbol *symbol;
    bool is_mutable;
    bool is_def_init;
};

INSTANTIATE(Param, param, ARRAY_TEMPLATE)

struct FnDecl {
    String name;
    TypeRef *ret_type;
    param_array params;
    Stmt *body;
    attr_array attributes;
    Symbol *symbol;
    Scope *local_scope;
    bool is_export;
};

struct Decl {
    enum {
        DECL_FN,
        DECL_VAR,
        DECL_STRUCT,
        DECL_UNION
    } type;
    union {
        FnDecl *fn;
        VarDecl *var;
        StructDecl *_struct;
        UnionDecl *_union;
    };
    Symbol *symbol;
};

INSTANTIATE(VarDecl *, vardecls, ARRAY_TEMPLATE)
INSTANTIATE(size_t, size, ARRAY_TEMPLATE)

struct StructDecl {
    String name;
    vardecls_array members;
    attr_array attributes;
    Symbol *symbol;
    size_t size;
    size_t align;
    size_array field_offsets;
    bool is_export;
};

struct UnionDecl {
    String name;
    vardecls_array members;
    attr_array attributes;
    Symbol *symbol;
    size_t size;
    size_t align;
    bool is_export;
};

INSTANTIATE(String, string, OPTIONAL_TEMPLATE)

struct Include {
    string_array path;
    string_optional alias;
    Module *resolved;
};

struct Symbol {
    String name;
    Decl *decl;
    TypeRef *type;
    uint32_t flags;
    Scope *defined_in;
};

INSTANTIATE(Symbol*, syms, ARRAY_TEMPLATE)

struct Scope {
    syms_array symbols;
    Scope *parent;
};

INSTANTIATE(Include *, includes, ARRAY_TEMPLATE)
INSTANTIATE(Decl *, decls, ARRAY_TEMPLATE)

struct Module {
    String name;
    includes_array includes;
    decls_array decls;
    Scope *scope;

    Arena *arena;
};

#endif