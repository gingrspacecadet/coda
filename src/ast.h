#ifndef AST_H
#define AST_H

#include "string.h"
#include "array.h"
#include "source.h"

typedef struct Module Module;
typedef struct Decl Decl;
typedef struct IncludeDecl IncludeDecl;
typedef struct TypeDecl TypeDecl;
typedef struct VarDecl VarDecl;
typedef struct FnDecl FnDecl;

typedef struct Type Type;
typedef struct Stmt Stmt;
typedef struct Expr Expr;
typedef struct Pattern Pattern;
typedef struct ConstraintDecl ConstraintDecl;

typedef struct {
    enum {
        NAME_IDENT,
        NAME_SPLICE,
    } kind;

    union {
        String ident;
        Expr *splice;
    };
} AstName;

typedef struct {
    Array /* String */ parts;
} Path;

typedef struct {
    Span span;
    String name;
    Array /* Expr * */ args;
} Attribute;

typedef struct {
    Span span;
    AstName name;
    Array /* Path */ constraints;
} GenericParam;

typedef struct {
    Span span;
    Type *type;
    AstName name;
} Param;

typedef struct {
    Span span;
    AstName name;
    Type *type;
} Field;

typedef enum {
    TYPE_NONE,
    TYPE_NAMED,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_FN,
    TYPE_SUM,
    TYPE_SPLICE,
} TypeKind;

struct Type {
    Span span;
    TypeKind kind;

    union {
        struct {
            Path path;
            Array /* Type * */ args;
        } named;

        struct {
            Type *pointee;
            bool mutable;
            bool optional;
        } pointer;

        struct {
            Type *element;

            /*
             * false: T[]
             * true:  T[n]
             */
            bool sized;
            Expr *length;
        } array;

        struct {
            Type *ret;
            Array /* Type * */ params;
        } fn;

        struct {
            Array /* Type * */ members;
        } sum;

        struct {
            Expr *expr;
        } splice;
    };
};

typedef enum {
    LIT_INTEGER,
    LIT_FLOAT,
    LIT_STRING,
    LIT_CHAR,
    LIT_BOOL,
    LIT_NONE,
} LiteralKind;

typedef struct {
    LiteralKind kind;
    String raw;
} Literal;

typedef enum {
    UNARY_POS,
    UNARY_NEG,
    UNARY_NOT,
    UNARY_BIT_NOT,
    UNARY_DEREF,
    UNARY_ADDRESS,
} UnaryOp;

typedef enum {
    BINARY_ADD,
    BINARY_SUB,
    BINARY_MUL,
    BINARY_DIV,
    BINARY_MOD,

    BINARY_EQUAL,
    BINARY_NOT_EQUAL,
    BINARY_LT,
    BINARY_LTE,
    BINARY_GT,
    BINARY_GTE,

    BINARY_LOGICAL_AND,
    BINARY_LOGICAL_OR,

    BINARY_BIT_AND,
    BINARY_BIT_OR,
    BINARY_BIT_XOR,

    BINARY_SHL,
    BINARY_SHR,

    BINARY_ASSIGN,

    BINARY_ADD_ASSIGN,
    BINARY_SUB_ASSIGN,
    BINARY_MUL_ASSIGN,
    BINARY_DIV_ASSIGN,
    BINARY_MOD_ASSIGN,

    BINARY_BIT_AND_ASSIGN,
    BINARY_BIT_OR_ASSIGN,
    BINARY_BIT_XOR_ASSIGN,

    BINARY_SHL_ASSIGN,
    BINARY_SHR_ASSIGN,

    BINARY_NAND,
    BINARY_NAND_ASSIGN,

    BINARY_NOR,
    BINARY_NOR_ASSIGN,
} BinaryOp;

typedef struct {
    Span span;
    AstName *name; /* NULL = positional */
    Expr *value;
} InitField;

typedef enum {
    EXPR_NONE,
    EXPR_ERROR,

    EXPR_LITERAL,
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
    EXPR_LAMBDA,

    EXPR_SPLICE,
} ExprKind;

struct Expr {
    Span span;

    /*
     * `$expr`
     */
    bool comptime;

    ExprKind kind;

    union {
        struct {
            Literal literal;
        } lit;

        struct {
            AstName name;
        } ident;

        struct {
            Path path;
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

            /*
             * foo(x)
             * foo<int>(x)
             */
            Array /* Type * */ generic_args;
            Array /* Expr * */ args;
        } call;

        struct {
            Expr *object;
            Expr *index;
        } index;

        struct {
            Expr *object;
            AstName member;
        } member;

        struct {
            Type *type;
            Expr *operand;
        } cast;

        struct {
            Path name;
            Array /* Expr * */ args;
        } intrinsic;

        struct {
            Expr *operand;
        } bubble;

        struct {
            /*
             * e.g.:
             *
             * Foo {
             *     x = 1,
             *     y = 2,
             * }
             */
            Array /* InitField */ fields;
        } init;

        struct {
            Array /* GenericParam */ generics;
            Array /* Param */ params;
            Type *ret;
            Stmt *body;
        } lambda;

        struct {
            Expr *expression;
        } splice;
    };
};

typedef enum {
    STMT_NONE,
    STMT_ERROR,

    STMT_VAR,
    STMT_EXPR,
    STMT_BLOCK,

    STMT_RETURN,

    STMT_IF,
    STMT_FOR,
    STMT_WHILE,
    STMT_MATCH,

    STMT_BREAK,
    STMT_CONTINUE,

    STMT_DEFER,
} StmtKind;

struct Stmt {
    Span span;

    /*
     * `$stmt`
     */
    bool comptime;

    StmtKind kind;

    union {
        struct {
            VarDecl *var;
        } var;

        struct {
            Expr *expr;
        } expr;

        struct {
            Array /* Stmt * */ stmts;
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
            Expr *expr;
            Array /* MatchCase */ cases;
        } match;

        struct {
            Expr *value;
        } _break;

        struct {
            Expr *value;
        } _continue;

        struct {
            Stmt *deferred;
        } defer;
    };
};

struct IncludeDecl {
    Path path;
    String alias;
};

typedef enum {
    TYPEDEF_ALIAS,
    TYPEDEF_STRUCT,
    TYPEDEF_UNION,
    TYPEDEF_ENUM,
} TypeDefKind;

struct TypeDecl {
    Span span;

    AstName name;
    Array /* GenericParam */ generics;

    TypeDefKind kind;

    union {
        Type *alias;

        struct {
            Array /* Field */ fields;
        } structure;

        struct {
            Array /* Field */ fields;
        } union_;

        struct {
            Type *underlying;
            Array /* EnumItem */ items;
        } enumeration;
    };
};

typedef struct {
    Span span;

    AstName name;
    Expr *value;
} EnumItem;

struct FnDecl {
    Span span;

    bool comptime;

    AstName name;

    Array /* GenericParam */ generics;
    Array /* Param */ params;

    Type *ret;
    Stmt *body;
};

struct VarDecl {
    Span span;

    Type *type;
    AstName name;
    Expr *init;
};

struct ConstraintDecl {
    Span span;

    AstName name;

    Array /* ConstraintItem */ items;
};

typedef enum {
    CONSTRAINT_METHOD,
    CONSTRAINT_FIELD,
    CONSTRAINT_EXPR,
} ConstraintItemKind;

typedef struct {
    Span span;

    ConstraintItemKind kind;

    union {
        FnDecl *method;
        Field field;
        Expr *expr;
    };
} ConstraintItem;

struct ConstraintMethod {
    AstName name;

    Array /* GenericParam */ generics;
    Array /* Param */ params;

    Type *ret;
};

struct ConstraintField {
    Type *type;
    AstName name;
};

struct Decl {
    Span span;

    enum {
        DECL_NONE,
        DECL_INCLUDE,
        DECL_TYPE,
        DECL_VAR,
        DECL_FN,
        DECL_CONSTRAINT,
    } kind;

    Array /* Attribute */ attrs;

    union {
        IncludeDecl include;
        TypeDecl type;
        VarDecl var;
        FnDecl fn;
        ConstraintDecl constraint;
    };
};

struct Module {
    Span span;

    Path path;
    Array /* Decl * */ decls;
};

#endif /* AST_H */