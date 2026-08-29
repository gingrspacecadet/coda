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
        AST_NAME_ERROR,
        AST_NAME_IDENT,
        AST_NAME_SPLICE,
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
    AST_CONSTRAINTREF_ERROR,
    AST_CONSTRAINTREF_NAMED,
    AST_CONSTRAINTREF_INLINE,
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
    AST_TYPE_ERROR,
    AST_TYPE_NAMED,
    AST_TYPE_POINTER,
    AST_TYPE_ARRAY,
    AST_TYPE_FN,
    AST_TYPE_SUM,
    AST_TYPE_STRUCT,
    AST_TYPE_UNION,
    AST_TYPE_ENUM,
    AST_TYPE_SPLICE,
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
    AST_LIT_ERROR,
    AST_LIT_INTEGER,
    AST_LIT_FLOAT,
    AST_LIT_STRING,
    AST_LIT_CHAR,
    AST_LIT_BOOL,
    AST_LIT_NULL,
} AstLiteralKind;

typedef struct {
    AstLiteralKind kind;
    String raw;
} AstLiteral;

typedef enum {
    AST_UNARY_ERROR,
    AST_UNARY_POS,
    AST_UNARY_NEG,
    AST_UNARY_NOT,
    AST_UNARY_BIT_NOT,
    AST_UNARY_DEREF,
    AST_UNARY_ADDRESS,
} AstUnaryOp;

typedef enum {
    AST_BINARY_ERROR,
    AST_BINARY_ADD,
    AST_BINARY_SUB,
    AST_BINARY_MUL,
    AST_BINARY_DIV,
    AST_BINARY_MOD,

    AST_BINARY_EQUAL,
    AST_BINARY_NOT_EQUAL,
    AST_BINARY_LT,
    AST_BINARY_LTE,
    AST_BINARY_GT,
    AST_BINARY_GTE,

    AST_BINARY_LOGICAL_AND,
    AST_BINARY_LOGICAL_OR,

    AST_BINARY_BIT_AND,
    AST_BINARY_BIT_OR,
    AST_BINARY_BIT_XOR,

    AST_BINARY_SHL,
    AST_BINARY_SHR,

    AST_BINARY_ASSIGN,

    AST_BINARY_ADD_ASSIGN,
    AST_BINARY_SUB_ASSIGN,
    AST_BINARY_MUL_ASSIGN,
    AST_BINARY_DIV_ASSIGN,
    AST_BINARY_MOD_ASSIGN,

    AST_BINARY_BIT_AND_ASSIGN,
    AST_BINARY_BIT_OR_ASSIGN,
    AST_BINARY_BIT_XOR_ASSIGN,

    AST_BINARY_SHL_ASSIGN,
    AST_BINARY_SHR_ASSIGN,

    AST_BINARY_NAND,
    AST_BINARY_NAND_ASSIGN,

    AST_BINARY_NOR,
    AST_BINARY_NOR_ASSIGN,
} AstBinaryOp;

typedef struct {
    Span span;
    AstName *name; /* NULL = positional */
    AstExpr *value;
} AstInitField;

typedef enum {
    AST_EXPR_ERROR,

    AST_EXPR_LITERAL,
    AST_EXPR_IDENT,
    AST_EXPR_PATH,

    AST_EXPR_UNARY,
    AST_EXPR_BINARY,

    AST_EXPR_CALL,
    AST_EXPR_INDEX,
    AST_EXPR_MEMBER,
    AST_EXPR_CAST,

    AST_EXPR_INTRINSIC,
    AST_EXPR_BUBBLE,
    AST_EXPR_INIT,
    AST_EXPR_LAMBDA,

    AST_EXPR_SPLICE,
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
    AST_STMT_ERROR,

    AST_STMT_VAR,
    AST_STMT_EXPR,
    AST_STMT_BLOCK,

    AST_STMT_RETURN,

    AST_STMT_IF,
    AST_STMT_FOR,
    AST_STMT_WHILE,
    AST_STMT_MATCH,

    AST_STMT_BREAK,
    AST_STMT_CONTINUE,

    AST_STMT_DEFER,
} AstStmtKind;

typedef enum {
    AST_PATTERN_ERROR,
    AST_PATTERN_WILDCARD,
    AST_PATTERN_LITERAL,
    AST_PATTERN_BINDING,
    AST_PATTERN_VARIANT,
    AST_PATTERN_EXPR,
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
    AST_CONSTRAINT_ERROR,
    AST_CONSTRAINT_METHOD,
    AST_CONSTRAINT_FIELD,
    AST_CONSTRAINT_EXPR,
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
        AST_DECL_ERROR,
        AST_DECL_INCLUDE,
        AST_DECL_TYPE,
        AST_DECL_VAR,
        AST_DECL_FN,
        AST_DECL_CONSTRAINT,
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