#ifndef AST_H
#define AST_H

#include <stdint.h>
#include "string.h"
#include "array.h"
#include "optional.h"
#include "arena.h"
#include "lexer.h"

INSTANTIATE(String, string, ARRAY_TEMPLATE)

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;
typedef struct VarDecl VarDecl;
typedef struct FnDecl FnDecl;
typedef struct EnumDecl EnumDecl;
typedef struct StructDecl StructDecl;
typedef struct UnionDecl UnionDecl;
typedef struct TypeDecl TypeDecl;
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
    SYMFLAG_MUT = 1 << 5,
    SYMFLAG_GENERIC = 1 << 6,
    SYMFLAG_NAMESPACE = 1 << 7,
} SymbolFlags;

struct Literal {
    enum {
        LITERAL_INT,
        LITERAL_UINT,
        LITERAL_FLOAT,
        LITERAL_STRING,
        LITERAL_BOOL,
        LITERAL_CHAR,
        LITERAL_NULL,
    } type;
    String raw;
    union {
        int64_t _int;
        double _float;
        String string;
        bool _bool;
        char _char;
    };
    size_t str_id;
    Token token;
};

INSTANTIATE(Literal, lit, ARRAY_TEMPLATE)

typedef struct {
    String name;
    lit_array args;
    bool consumed;
    Token token;
} Attribute;

typedef struct {
    String name;
    bool is_arg_type;
    union {
        Expr *expr;
        TypeRef *type;
    };
} Intrinsic;

typedef struct {
    string_optional field_name;
    Expr *value;
    Token token;
} InitField;

INSTANTIATE(InitField, initfield, ARRAY_TEMPLATE)
INSTANTIATE(TypeRef *, typerefs, ARRAY_TEMPLATE)

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
        EXPR_CAST,
        EXPR_INTRINSIC,
        EXPR_BUBBLE,
        EXPR_INIT,
        EXPR_SPECIALISE,
    } type;

    union {
        Literal literal;
        Intrinsic intrinsic;
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
            bool deref;
        } member;
        struct {
            TypeRef *to;
            Expr *expr;
        } cast;
        struct {
            Expr *expr;
        } bubble;
        struct {
            initfield_array fields;
        } init_list;
        struct {
            Expr *expr;
            typerefs_array args;
        } specialise;
    };

    TypeRef *resolved_type;
    Symbol *symbol;
    bool is_constant;
    Token token;
};

INSTANTIATE(Stmt *, stmts, ARRAY_TEMPLATE)

typedef struct {
    VarDecl *var;
    Stmt *body;
} Case;

INSTANTIATE(Case, case, ARRAY_TEMPLATE)

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
        STMT_DEFER,
        STMT_MATCH,
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
        struct  {
            Stmt *deferred;
        } defer;
        struct {
            Expr *expr;
            case_array cases;
        } match;
    };

    Scope *scope;
    Token token;
};

INSTANTIATE(Attribute, attr, ARRAY_TEMPLATE)

struct Param {
    TypeRef *type;
    String name;
    attr_array attributes;
    Expr *default_value;
    Symbol *symbol;
    Token token;
};

INSTANTIATE(Param, param, ARRAY_TEMPLATE)

INSTANTIATE(FnDecl *, fndecls, ARRAY_TEMPLATE)

typedef struct {
    String name;
    fndecls_array constraints;
    Token token;
} GenericParam;

INSTANTIATE(GenericParam, genparam, ARRAY_TEMPLATE)

struct TypeRef {
    enum {
        TYPEREF_NAMED,
        TYPEREF_POINTER,
        TYPEREF_ARRAY,
        TYPEREF_FN,
        TYPEREF_SUM,
    } type;
    union {
        struct {
            String name;
            typerefs_array generic_args;
        } named;
        struct {
            TypeRef *pointee;
        } pointer;
        struct {
            TypeRef *elem;
            size_t length;
        } array;
        struct {
            TypeRef *ret_type;
            param_array params;
        } fn;
        struct {
            typerefs_array cases;
        } sum;
        struct {
            TypeRef *base_type;
            typerefs_array arg_types;
        } generic;
    };

    bool is_mutable;
    bool is_optional;
    Symbol *type_symbol;
    Token token;
};

struct VarDecl {
    TypeRef *type;
    String name;
    Expr *init;
    attr_array attributes;
    Symbol *symbol;
    bool is_mutable;
    bool is_def_init;
    Token token;
};

struct FnDecl {
    String name;
    string_optional struct_name;
    genparam_array generic_params;
    TypeRef *ret_type;
    param_array params;
    Stmt *body;
    Symbol *symbol;
    Scope *local_scope;
    bool is_export;
    bool is_extern;
    Token token;
};

struct Decl {
    enum {
        DECL_FN,
        DECL_VAR,
        DECL_STRUCT,
        DECL_UNION,
        DECL_TYPE,
        DECL_ENUM,
        DECL_NAMESPACE,
    } type;
    union {
        FnDecl *fn;
        VarDecl *var;
        StructDecl *_struct;
        UnionDecl *_union;
        TypeDecl *_type;
        EnumDecl *_enum;
        Module *namespace;
    };
    attr_array attributes;
    Symbol *symbol;
    bool is_export;
    Token token;
};

INSTANTIATE(VarDecl *, vardecls, ARRAY_TEMPLATE)
INSTANTIATE(size_t, size, ARRAY_TEMPLATE)

typedef struct {
    String name;
    Expr *value;
    Token token;
} EnumVariant;

INSTANTIATE(EnumVariant, enumvar, ARRAY_TEMPLATE)

struct EnumDecl {
    String name;
    enumvar_array variants;
    Symbol *symbol;
    Token token;
    TypeRef *type;
};

struct StructDecl {
    String name;
    genparam_array generic_params;
    vardecls_array members;
    Symbol *symbol;
    size_t size;
    size_t align;
    size_array field_offsets;
    Token token;
};

struct UnionDecl {
    String name;
    genparam_array generic_params;
    vardecls_array members;
    Symbol *symbol;
    size_t size;
    size_t align;
    Token token;
};

struct TypeDecl {
    String name;
    genparam_array generic_params;
    TypeRef *alias;
    Symbol *symbol;
    Token token;
};

INSTANTIATE(String, string, OPTIONAL_TEMPLATE)

struct Include {
    string_array path;
    string_array alias;
    Module *resolved;
    Token token;
};

struct Symbol {
    String name;
    String mangled;
    Decl *decl;
    TypeRef *type;
    uint32_t flags;
    Scope *defined_in;
    uint32_t vreg;  // for LIR
};

INSTANTIATE(Symbol*, syms, ARRAY_TEMPLATE)

struct Scope {
    syms_array symbols;
    Scope *parent;
};

INSTANTIATE(Include *, includes, ARRAY_TEMPLATE)
INSTANTIATE(Decl *, decls, ARRAY_TEMPLATE)

struct Module {
    string_array name;
    includes_array includes;
    decls_array decls;
    Scope *scope;
    Token token;

    Arena *arena;
};

INSTANTIATE(char, char, ARRAY_TEMPLATE)

static void append_string_to_char_array(char_array *cs, String s) {
    for (size_t i = 0; i < s.length; ++i) {
        char_array_push(cs, s.data[i]);
    }
}

static String type_to_string(TypeRef *t) {
    if (!t) return string_make("(null)"); // keep existing behavior for null

    if (t->type == TYPEREF_NAMED) {
        if (t->type_symbol) {
            return t->type_symbol->name;
        }
        return t->named.name;
    }

    char_array cs = char_array_init();

    if (t->type == TYPEREF_POINTER) {
        String pointee = type_to_string(t->pointer.pointee);
        append_string_to_char_array(&cs, pointee);
        if (t->is_mutable) append_string_to_char_array(&cs, string_make(" mut"));
        char_array_push(&cs, '*');
        if (t->is_optional) char_array_push(&cs, '?');
        return (String){ .data = cs.data, .length = cs.len };
    }
    else if (t->type == TYPEREF_ARRAY) {
        String base = type_to_string(t->array.elem);
        append_string_to_char_array(&cs, base);
        char_array_push(&cs, '[');

        char buf[32];
        int n = snprintf(buf, sizeof buf, "%zu", t->array.length);
        if (n > 0) {
            for (int i = 0; i < n; ++i) char_array_push(&cs, buf[i]);
        }

        char_array_push(&cs, ']');
        return (String){ .data = cs.data, .length = cs.len };
    }
    else if (t->type == TYPEREF_FN) {
        char_array_push(&cs, 'f');
        char_array_push(&cs, 'n');
        char_array_push(&cs, ' ');

        String ret = type_to_string(t->fn.ret_type);
        append_string_to_char_array(&cs, ret);

        char_array_push(&cs, '(');
        // TODO: append parameters here, using the same pattern
        char_array_push(&cs, ')');

        return (String){ .data = cs.data, .length = cs.len };
    }
    else if (t->type == TYPEREF_SUM) {
        for (size_t i = 0; i < t->sum.cases.len; ++i) {
            TypeRef *case_t = t->sum.cases.data[i];
            String cs_str = type_to_string(case_t);
            append_string_to_char_array(&cs, cs_str);
            if (i + 1 < t->sum.cases.len) char_array_push(&cs, '|');
        }
        return (String){ .data = cs.data, .length = cs.len };
    }

    return string_make("Unknown");
}

static String module_name_to_string(string_array *s) {
    char_array cs = char_array_init();
    for (size_t i = 0; i < s->len; i++) {
        append_string_to_char_array(&cs, s->data[i]);
        if (i + 1 != s->len) append_string_to_char_array(&cs, string_make("::"));
    }
    return (String){ .data = cs.data, .length = cs.len };
}

#endif
