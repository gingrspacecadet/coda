#ifndef AST_H
#define AST_H

#include <stdint.h>
#include "string.h"
#include "array.h"
#include "optional.h"
#include "arena.h"
#include "lexer.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;
typedef struct VarDecl VarDecl;
typedef struct FnDecl FnDecl;
typedef struct StructType StructType;
typedef struct UnionType UnionType;
typedef struct EnumType EnumType;
typedef struct TypeDecl TypeDecl;
typedef struct TypeRef TypeRef;
typedef struct Symbol Symbol;
typedef struct Scope Scope;
typedef struct Module Module;
typedef struct Include Include;
typedef struct Literal Literal;
typedef struct Param Param;

INSTANTIATE(FnDecl *, fndecls, ARRAY_TEMPLATE)
INSTANTIATE(String, string, ARRAY_TEMPLATE)
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
INSTANTIATE(Attribute, attr, ARRAY_TEMPLATE);

struct Param {
    TypeRef *type;
    String name;
    attr_array attributes;
    Expr *default_value;
    Symbol *symbol;
    Token token;
};

INSTANTIATE(Param, param, ARRAY_TEMPLATE)

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
        EXPR_LAMBDA,
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
        struct {
            TypeRef *ret_type;
            param_array params;
            Stmt *body;
            Symbol *symbol;
        } lambda;
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

typedef struct {
    String name;
    fndecls_array constraints;
    Token token;
} GenericParam;

INSTANTIATE(GenericParam, genparam, ARRAY_TEMPLATE)

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
        DECL_TYPE,
        DECL_NAMESPACE,
    } type;

    union {
        FnDecl *fn;
        VarDecl *var;
        TypeDecl *_type;
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

struct StructType {
    vardecls_array members;
    size_t size;
    size_t align;
    size_array field_offsets;
};

struct UnionType {
    vardecls_array members;
    size_t size;
    size_t align;
};

struct EnumType {
    enumvar_array variants;
    TypeRef *underlying;
};

struct TypeRef {
    enum {
        TYPEREF_NONE,
        TYPEREF_NAMED,
        TYPEREF_PATH,
        TYPEREF_POINTER,
        TYPEREF_ARRAY,
        TYPEREF_FN,
        TYPEREF_SUM,
        TYPEREF_STRUCT,
        TYPEREF_UNION,
        TYPEREF_ENUM,
    } type;

    union {
        struct {
            String name;
            typerefs_array generic_args;
        } named;

        struct {
            string_array components;
            typerefs_array generic_args;
        } path;

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

        StructType _struct;
        UnionType _union;
        EnumType _enum;
    };

    bool is_mutable;
    bool is_optional;
    Symbol *type_symbol;
    Token token;
};

struct TypeDecl {
    String name;
    genparam_array generic_params;
    TypeRef *alias;
    Symbol *symbol;
    Token token;
};

// INSTANTIATE(String, string, OPTIONAL_TEMPLATE)

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

static String type_to_string(TypeRef *t);

static void append_string_to_char_array(char_array *cs, String s) {
    for (size_t i = 0; i < s.length; ++i) {
        char_array_push(cs, s.data[i]);
    }
}

static void append_type_to_char_array(char_array *cs, TypeRef *t) {
    append_string_to_char_array(cs, type_to_string(t));
}

static void append_type_list_to_char_array(char_array *cs, typerefs_array *types) {
    for (size_t i = 0; i < types->len; ++i) {
        append_type_to_char_array(cs, types->data[i]);
        if (i + 1 < types->len) char_array_push(cs, ',');
    }
}

static void append_vardecls_to_char_array(char_array *cs, vardecls_array *members) {
    for (size_t i = 0; i < members->len; ++i) {
        VarDecl *v = members->data[i];
        if (v->name.data && v->name.length) {
            append_string_to_char_array(cs, v->name);
            char_array_push(cs, ':');
            char_array_push(cs, ' ');
        }
        append_string_to_char_array(cs, type_to_string(v->type));
        if (i + 1 < members->len) {
            append_string_to_char_array(cs, string_make(", "));
        }
    }
}

static void append_enum_variants_to_char_array(char_array *cs, enumvar_array *variants) {
    for (size_t i = 0; i < variants->len; ++i) {
        EnumVariant *v = &variants->data[i];
        append_string_to_char_array(cs, v->name);
        if (v->value) {
            append_string_to_char_array(cs, string_make(" = "));
            append_string_to_char_array(cs, type_to_string(v->value->resolved_type));
        }
        if (i + 1 < variants->len) {
            append_string_to_char_array(cs, string_make(", "));
        }
    }
}

static String type_to_string(TypeRef *t) {
    if (!t) return string_make("(null)");

    if (t->type == TYPEREF_NAMED) {
        if (t->type_symbol) return t->type_symbol->name;
        return t->named.name;
    }

    char_array cs = char_array_init(arena_create());

    if (t->type == TYPEREF_POINTER) {
        String pointee = type_to_string(t->pointer.pointee);
        append_string_to_char_array(&cs, pointee);
        if (t->is_mutable) append_string_to_char_array(&cs, string_make(" mut"));
        char_array_push(&cs, '*');
        if (t->is_optional) char_array_push(&cs, '?');
        return (String){ .data = cs.data, .length = cs.len };
    } else if (t->type == TYPEREF_ARRAY) {
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
    } else if (t->type == TYPEREF_FN) {
        append_string_to_char_array(&cs, string_make("fn "));
        append_string_to_char_array(&cs, type_to_string(t->fn.ret_type));
        char_array_push(&cs, '(');
        for (size_t i = 0; i < t->fn.params.len; ++i) {
            Param *p = &t->fn.params.data[i];
            append_string_to_char_array(&cs, type_to_string(p->type));
            if (i + 1 < t->fn.params.len) append_string_to_char_array(&cs, string_make(", "));
        }
        char_array_push(&cs, ')');
        return (String){ .data = cs.data, .length = cs.len };
    } else if (t->type == TYPEREF_SUM) {
        for (size_t i = 0; i < t->sum.cases.len; ++i) {
            TypeRef *case_t = t->sum.cases.data[i];
            append_string_to_char_array(&cs, type_to_string(case_t));
            if (i + 1 < t->sum.cases.len) char_array_push(&cs, '|');
        }
        return (String){ .data = cs.data, .length = cs.len };
    } else if (t->type == TYPEREF_PATH) {
        for (size_t i = 0; i < t->path.components.len; ++i) {
            append_string_to_char_array(&cs, t->path.components.data[i]);
            if (i + 1 < t->path.components.len) {
                char_array_push(&cs, ':');
                char_array_push(&cs, ':');
            }
        }
        return (String){ .data = cs.data, .length = cs.len };
    } else if (t->type == TYPEREF_STRUCT) {
        append_string_to_char_array(&cs, string_make("struct { "));
        append_vardecls_to_char_array(&cs, &t-> _struct.members);
        append_string_to_char_array(&cs, string_make(" }"));
        return (String){ .data = cs.data, .length = cs.len };
    } else if (t->type == TYPEREF_UNION) {
        append_string_to_char_array(&cs, string_make("union { "));
        append_vardecls_to_char_array(&cs, &t-> _union.members);
        append_string_to_char_array(&cs, string_make(" }"));
        return (String){ .data = cs.data, .length = cs.len };
    } else if (t->type == TYPEREF_ENUM) {
        append_string_to_char_array(&cs, string_make("enum { "));
        append_enum_variants_to_char_array(&cs, &t-> _enum.variants);
        append_string_to_char_array(&cs, string_make(" }"));
        return (String){ .data = cs.data, .length = cs.len };
    }

    return string_make("Unknown");
}

static String module_name_to_string(Arena *a, string_array *s) {
    char_array cs = char_array_init(a);
    for (size_t i = 0; i < s->len; i++) {
        append_string_to_char_array(&cs, s->data[i]);
        if (i + 1 != s->len) append_string_to_char_array(&cs, string_make("::"));
    }
    return (String){ .data = cs.data, .length = cs.len };
}

#endif
