#ifndef HIR_H
#define HIR_H

#include "ast.h"

typedef struct Symbol Symbol;

typedef struct HirType HirType;
typedef struct HirField HirField;
typedef struct HirEnumItem HirEnumItem;

typedef struct HirExpr HirExpr;
typedef struct HirInitField HirInitField;
typedef struct HirParam HirParam;

typedef struct HirStmt HirStmt;
typedef struct HirLocal HirLocal;

typedef struct HirFunction HirFunction;
typedef struct HirGlobal HirGlobal;
typedef struct HirModule HirModule;

typedef enum {
    HIR_TYPE_ERROR,
    HIR_TYPE_BUILTIN,
    HIR_TYPE_NAMED,
    HIR_TYPE_POINTER,
    HIR_TYPE_SLICE,
    HIR_TYPE_ARRAY,
    HIR_TYPE_FUNCTION,
    HIR_TYPE_SUM,
    HIR_TYPE_STRUCT,
    HIR_TYPE_UNION,
    HIR_TYPE_ENUM,
} HirTypeKind;

//! TODO: built in types
typedef int BuiltinType;

struct HirType {
    HirTypeKind kind;
    bool mutable;

    union {
        BuiltinType builtin;

        struct {
            Symbol *symbol;
        } named;

        struct {
            HirType *pointee;
            bool optional;
        } pointer;

        struct {
            HirType *element;
        } slice;

        struct {
            HirType *element;
            size_t length;
        } array;

        struct {
            HirType *ret;
            Array(HirType *) params;
        } function;

        struct {
            Array(HirType *) members;
        } sum;

        struct {
            Array(HirField) fields;
        } structure;

        struct {
            Array(HirField) fields;
        } union_;

        struct {
            HirType *underlying;
            Array(HirEnumItem) items;
        } enumeration;
    };
};

struct HirField {
    Symbol *symbol;
    HirType *type;
};

struct HirEnumItem {
    Symbol *symbol;
    HirType *type;
    size_t value;
};

typedef enum {
    HIR_EXPR_ERROR,
    HIR_EXPR_LITERAL,
    HIR_EXPR_VALUE,
    HIR_EXPR_UNARY,
    HIR_EXPR_BINARY,
    HIR_EXPR_CALL,
    HIR_EXPR_INDEX,
    HIR_EXPR_FIELD,
    HIR_EXPR_CAST,
    HIR_EXPR_INIT,
    HIR_EXPR_LAMBDA,
} HirExprKind;

struct HirExpr {
    Span span;
    HirExprKind kind;
    HirType *type;

    union {
        AstLiteral literal;

        struct {
            Symbol *symbol;
        } value;

        struct {
            AstUnaryOp op;
            HirExpr *operand;
        } unary;

        struct {
            AstBinaryOp op;
            HirExpr *left;
            HirExpr *right;
        } binary;

        struct {
            Symbol *function;
            Array(HirExpr *) args;
        } call;

        struct {
            HirExpr *object;
            HirExpr *index;
        } index;

        struct {
            HirExpr *object;
            HirField *field;
        } field;

        struct {
            HirType *type;
            HirExpr *operand;
        } cast;

        struct {
            Array(HirInitField) fields;
        } init;

        struct {
            Symbol *symbol;
            Array(HirParam) params;
            HirType *ret;
            HirStmt *body;
        } lambda;
    };
};

struct HirInitField {
    HirField *field;
    HirExpr *value;
};

struct HirParam {
    Symbol *symbol;
    HirType *type;
};

typedef enum {
    HIR_STMT_ERROR,
    HIR_STMT_EXPR,
    HIR_STMT_BLOCK,
    HIR_STMT_ASSIGN,
    HIR_STMT_RETURN,
    HIR_STMT_IF,
    HIR_STMT_WHILE,
    HIR_STMT_BREAK,
    HIR_STMT_CONTINUE,
} HirStmtKind;

struct HirStmt {
    Span span;
    HirStmtKind kind;

    union {
        struct {
            HirExpr *expr;
        } expr;

        struct {
            Array(HirStmt *) stmts;
        } block;

        struct {
            HirExpr *target;
            HirExpr *value;
        } assign;

        struct {
            HirExpr *value;
        } _return;

        struct {
            HirExpr *cond;
            HirStmt *then;
            HirStmt *_else;
        } _if;

        struct {
            HirExpr *cond;
            HirStmt *body;
        } _while;
    };
};

struct HirLocal {
    Symbol *symbol;
    HirType *type;
};

struct HirFunction {
    Symbol *symbol;

    HirType *return_type;

    Array(HirParam) params;
    Array(HirLocal) locals;

    HirStmt *body;

    bool is_extern;
    bool is_export;
};

struct HirGlobal {
    Symbol *symbol;
    HirType *type;
    HirExpr *init;

    bool is_export;
};

struct HirModule {
    Array(HirFunction) functions;
    Array(HirGlobal) globals;
};

typedef enum {
    SYMBOL_ERROR,
    SYMBOL_TYPE,
    SYMBOL_FN,
    SYMBOL_GLOBAL,
    SYMBOL_LOCAL,
    SYMBOL_PARAMETER,
    SYMBOL_FIELD,
    SYMBOL_ENUM_ITEM,
    SYMBOL_CONSTRAINT,
} SymbolKind;

struct Symbol {
    SymbolKind kind;
    AstDecl *decl;
    AstName name;
};

typedef struct {
    Array(Symbol) syms;
} Scope;

#endif