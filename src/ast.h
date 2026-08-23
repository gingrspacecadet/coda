#ifndef AST_H
#define AST_H

#include "string.h"
#include "array.h"
#include "source.h"

typedef struct AstModule AstModule;
typedef struct AstDecl AstDecl;
typedef struct AstIncludeDecl AstIncludeDecl;
typedef struct AstTypeDecl AstTypeDecl;
typedef struct AstVarDecl AstVarDecl;
typedef struct AstFnDecl AstFnDecl;

typedef struct AstType AstType;
typedef struct AstStmt AstStmt;
typedef struct AstExpr AstExpr;
typedef struct AstPattern AstPattern;
typedef struct AstConstraintDecl AstConstraintDecl;

typedef struct {
    enum {
        NAME_IDENT,
        NAME_SPLICE,
    } kind;

    union {
        String ident;
        AstExpr *splice;
    };
} AstName;

typedef struct {
    Array(String) parts;
} Path;

typedef struct {
    Span span;
    String name;
    Array(AstExpr *) args;
} AstAttribute;

typedef enum {
    CONSTRAINT_NONE,
    CONSTRAINT_NAMED,
    CONSTRAINT_INLINE,
} AstConstraintRefKind;

typedef struct {
    Span span;

    AstConstraintRefKind kind;

    union {
        Path path;
        AstConstraintDecl *_inline;
    };
} AstConstraintRef;

typedef struct {
    Span span;
    AstName name;
    Array(AstConstraintRef) constraints;
} AstGenericParam;

typedef struct {
    Span span;
    AstType *type;
    AstName name;
} AstParam;

typedef struct {
    Span span;
    AstName name;
    AstType *type;
} AstField;

typedef enum {
    TYPE_NONE,
    TYPE_NAMED,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_FN,
    TYPE_SUM,
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_ENUM,
    TYPE_SPLICE,
    TYPE_ERROR,
} AstTypeKind;

struct AstType {
    Span span;
    AstTypeKind kind;
    bool mutable;

    union {
        struct {
            Path path;
            Array(AstType *) args;
        } named;

        struct {
            AstType *pointee;
            bool optional;
        } pointer;

        struct {
            AstType *element;
            bool sized;
            AstExpr *length;
        } array;

        struct {
            AstType *ret;
            Array(AstType *) params;
        } fn;

        struct {
            Array(AstType *) members;
        } sum;

        struct {
            Array(AstField) fields;
        } structure;

        struct {
            Array(AstField) fields;
        } union_;

        struct {
            AstType *underlying;
            Array(AstEnumItem) items;
        } enumeration;

        struct {
            AstExpr *expr;
        } splice;
    };
};

typedef enum {
    LIT_NONE,
    LIT_INTEGER,
    LIT_FLOAT,
    LIT_STRING,
    LIT_CHAR,
    LIT_BOOL,
    LIT_NULL,
} AstLiteralKind;

typedef struct {
    AstLiteralKind kind;
    String raw;
} AstLiteral;

typedef enum {
    UNARY_POS,
    UNARY_NEG,
    UNARY_NOT,
    UNARY_BIT_NOT,
    UNARY_DEREF,
    UNARY_ADDRESS,
} AstUnaryOp;

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
} AstBinaryOp;

typedef struct {
    Span span;
    AstName *name; /* NULL = positional */
    AstExpr *value;
} AstInitField;

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
} AstExprKind;

struct AstExpr {
    Span span;

    bool comptime;

    AstExprKind kind;

    union {
        struct {
            AstLiteral literal;
        } lit;

        struct {
            AstName name;
        } ident;

        struct {
            Path path;
        } path;

        struct {
            AstUnaryOp op;
            AstExpr *operand;
        } unary;

        struct {
            AstBinaryOp op;
            AstExpr *left;
            AstExpr *right;
        } binary;

        struct {
            AstExpr *callee;

            Array(AstType *) generic_args;
            Array(AstExpr *) args;
        } call;

        struct {
            AstExpr *object;
            AstExpr *index;
        } index;

        struct {
            AstExpr *object;
            AstName member;
        } member;

        struct {
            AstType *type;
            AstExpr *operand;
        } cast;

        struct {
            Path name;
            Array(AstExpr *) args;
        } intrinsic;

        struct {
            AstExpr *operand;
        } bubble;

        struct {
            Array(AstInitField) fields;
        } init;

        struct {
            Array(AstGenericParam) generics;
            Array(AstParam) params;
            AstType *ret;
            AstStmt *body;
        } lambda;

        struct {
            AstExpr *expression;
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
} AstStmtKind;

typedef enum {
    PATTERN_ERROR,
    PATTERN_WILDCARD,
    PATTERN_LITERAL,
    PATTERN_BINDING,
    PATTERN_VARIANT,
    PATTERN_EXPR,
} AstPatternKind;

struct AstPattern {
    Span span;
    AstPatternKind kind;

    union {
        AstLiteral literal;

        AstName binding;

        struct {
            AstName name;
            AstName binding;
        } variant;

        AstExpr *expr;
    };
};

typedef struct {
    Span span;

    AstPattern *pattern;
    AstStmt *body;
} AstMatchCase;

struct AstStmt {
    Span span;

    bool comptime;

    AstStmtKind kind;

    union {
        struct {
            AstVarDecl *var;
        } var;

        struct {
            AstExpr *expr;
        } expr;

        struct {
            Array(AstStmt *) stmts;
        } block;

        struct {
            AstExpr *value;
        } _return;

        struct {
            AstExpr *cond;
            AstStmt *then;
            AstStmt *_else;
        } _if;

        struct {
            AstStmt *init;
            AstExpr *cond;
            AstExpr *post;
            AstStmt *body;
        } _for;

        struct {
            AstExpr *cond;
            AstStmt *body;
        } _while;

        struct {
            AstExpr *expr;
            Array(AstMatchCase) cases;
        } match;

        struct {
            AstExpr *value;
        } _break;

        struct {
            AstExpr *value;
        } _continue;

        struct {
            AstStmt *deferred;
        } defer;
    };
};

struct AstIncludeDecl {
    Span span;

    Path path;
    Path alias;
};

struct AstTypeDecl {
    Span span;
    
    AstName name;
    Array(AstGenericParam) generics;
    AstType *type;
};

typedef struct {
    Span span;

    AstName name;
    AstExpr *value;
} AstEnumItem;

struct AstFnDecl {
    Span span;

    bool comptime;

    AstType *receiver;

    AstName name;

    Array(AstAttribute) attrs;
    Array(AstGenericParam) generics;
    Array(AstParam) params;

    AstType *ret;
    AstStmt *body;
};

struct AstVarDecl {
    Span span;

    AstType *type;
    AstName name;
    AstExpr *init;
};

struct AstConstraintDecl {
    Span span;

    AstName name;

    Array(AstConstraintItem) items;
};

typedef enum {
    CONSTRAINT_METHOD,
    CONSTRAINT_FIELD,
    CONSTRAINT_EXPR,
} AstConstraintItemKind;

typedef struct {
    Span span;

    AstConstraintItemKind kind;

    union {
        AstFnDecl *method;
        AstField field;
        AstExpr *expr;
    };
} AstConstraintItem;

struct AstDecl {
    Span span;

    enum {
        DECL_NONE,
        DECL_INCLUDE,
        DECL_TYPE,
        DECL_VAR,
        DECL_FN,
        DECL_CONSTRAINT,
    } kind;

    Array(AstAttribute) attrs;

    union {
        AstIncludeDecl include;
        AstTypeDecl type;
        AstVarDecl var;
        AstFnDecl fn;
        AstConstraintDecl constraint;
    };
};

struct AstModule {
    Span span;

    Path path;
    Array(AstDecl *) decls;
};

#endif /* AST_H */