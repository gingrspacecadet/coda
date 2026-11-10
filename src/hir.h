#ifndef HIR_H
#define HIR_H

#include <stdint.h>
#include "ast.h"

typedef struct Symbol Symbol;

typedef struct HirType HirType;
typedef struct HirField HirField;
typedef struct HirEnumItem HirEnumItem;

typedef struct HirExpr HirExpr;
typedef struct HirCodeSplice HirCodeSplice;
typedef struct HirInitField HirInitField;
typedef struct HirParam HirParam;

typedef struct HirStmt HirStmt;
typedef struct HirLocal HirLocal;

struct HirCodeSplice {
    AstExpr *splice;
    AstName *name_splice;
    AstExpr *owner;
    HirExpr *value;
};

typedef struct HirFunction HirFunction;
typedef struct HirGlobal HirGlobal;
typedef struct HirModule HirModule;

typedef enum {
    CODE_CATEGORY_EXPR,
    CODE_CATEGORY_STMT,
    CODE_CATEGORY_DECL,
    CODE_CATEGORY_TYPE,
    CODE_CATEGORY_NAME,
} CodeCategory;

typedef enum {
    CODE_NODE_ANY,

    CODE_NODE_EXPR_LITERAL,
    CODE_NODE_EXPR_IDENT,
    CODE_NODE_EXPR_PATH,
    CODE_NODE_EXPR_TYPE_VALUE,
    CODE_NODE_EXPR_UNARY,
    CODE_NODE_EXPR_BINARY,
    CODE_NODE_EXPR_CALL,
    CODE_NODE_EXPR_INFERRED_METHOD_CALL,
    CODE_NODE_EXPR_INDEX,
    CODE_NODE_EXPR_MEMBER,
    CODE_NODE_EXPR_CAST,
    CODE_NODE_EXPR_INTRINSIC,
    CODE_NODE_EXPR_BUBBLE,
    CODE_NODE_EXPR_INIT,
    CODE_NODE_EXPR_LAMBDA,
    CODE_NODE_EXPR_SPLICE,

    CODE_NODE_STMT_VAR,
    CODE_NODE_STMT_EXPR,
    CODE_NODE_STMT_BLOCK,
    CODE_NODE_STMT_RETURN,
    CODE_NODE_STMT_IF,
    CODE_NODE_STMT_FOR,
    CODE_NODE_STMT_WHILE,
    CODE_NODE_STMT_MATCH,
    CODE_NODE_STMT_BREAK,
    CODE_NODE_STMT_CONTINUE,
    CODE_NODE_STMT_DEFER,

    CODE_NODE_DECL_INCLUDE,
    CODE_NODE_DECL_TYPE,
    CODE_NODE_DECL_VAR,
    CODE_NODE_DECL_FN,
    CODE_NODE_DECL_CONSTRAINT,

    CODE_NODE_TYPE_NAMED,
    CODE_NODE_TYPE_POINTER,
    CODE_NODE_TYPE_ARRAY,
    CODE_NODE_TYPE_FN,
    CODE_NODE_TYPE_SUM,
    CODE_NODE_TYPE_STRUCT,
    CODE_NODE_TYPE_UNION,
    CODE_NODE_TYPE_ENUM,
    CODE_NODE_TYPE_SPLICE,

    CODE_NODE_NAME_IDENT,
    CODE_NODE_NAME_SPLICE,
    CODE_NODE_EXPR_QUOTE,
} CodeNodeKind;

typedef enum {
    HIR_TYPE_ERROR,
    HIR_TYPE_BUILTIN,
    HIR_TYPE_POINTER,
    HIR_TYPE_SLICE,
    HIR_TYPE_ARRAY,
    HIR_TYPE_FUNCTION,
    HIR_TYPE_SUM,
    HIR_TYPE_STRUCT,
    HIR_TYPE_UNION,
    HIR_TYPE_ENUM,
    HIR_TYPE_ANY,
    HIR_TYPE_ANY_CONSTRAINT,
    HIR_TYPE_CODE,
} HirTypeKind;

typedef enum {
    BUILTIN_UINT8,
    BUILTIN_UINT16,
    BUILTIN_UINT32,
    BUILTIN_UINT64,
    BUILTIN_INT8,
    BUILTIN_INT16,
    BUILTIN_INT32,
    BUILTIN_INT64,
    BUILTIN_BOOL,
    BUILTIN_NONE,
    BUILTIN_FLOAT32,
    BUILTIN_FLOAT64,
} BuiltinType;

struct HirType {
    HirTypeKind kind;
    bool mutable;

    size_t size;
    size_t align;

    Symbol *nominal;
    HirType *base;
    HirType *super;
    bool is_vla;
    HirExpr *vla_length;

    union {
        BuiltinType builtin;

        struct {
            HirType *pointee;
            bool optional;
        } pointer;

        struct {
            HirType *element;
            uint64_t length;
            Array(HirField) fields;
        } array;

        struct {
            HirType *element;
            Array(HirField) fields;
        } slice;

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
        } _enum;

        struct {
            Symbol *constraint;
        } any_constraint;

        struct {
            CodeCategory category;
            CodeNodeKind node;
        } code;
    };
};

struct HirField {
    Symbol *symbol;
    HirType *type;
    size_t offset;
};

struct HirEnumItem {
    Symbol *symbol;
    HirExpr *value;
};

typedef enum {
    HIR_LITERAL_ERROR,
    HIR_LITERAL_INTEGER,
    HIR_LITERAL_FLOAT,
    HIR_LITERAL_STRING,
    HIR_LITERAL_BOOL,
    HIR_LITERAL_NULL,
} HirLiteralKind;

typedef struct {
    HirLiteralKind kind;

    union {
        uint64_t integer;
        double floating;
        String string;
        bool boolean;
    };
} HirLiteral;

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
    HIR_EXPR_SUM_INJECT,
    HIR_EXPR_VLA_ALLOC,
    HIR_EXPR_LAMBDA,
    HIR_EXPR_ANY_BOX,
    HIR_EXPR_ANY_CALL,
    HIR_EXPR_CODE,
} HirExprKind;

struct HirExpr {
    Span span;
    HirExprKind kind;
    HirType *type;

    union {
        HirLiteral literal;

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
            HirExpr *callee;
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
            HirExpr *value;
            size_t member_index;
        } sum_inject;

        struct {
            Symbol *symbol;
            Array(HirParam) params;
            HirType *ret;
            HirStmt *body;
        } lambda;
        struct {
            HirType *element;
            HirExpr *length;
        } vla_alloc;

        struct {
            HirExpr *address;
            Symbol *vtable;
            HirType *vtable_type;
            Array(Symbol *) methods;
        } any_box;

        struct {
            HirExpr *object;
            HirType *function_type;
            HirType *function_pointer_type;
            HirType *byte_pointer_type;
            size_t method_index;
            bool has_receiver;
            Array(HirExpr *) args;
        } any_call;

        struct {
            AstExpr *expression;
            AstName name;
            Array(HirCodeSplice) splices;
            bool is_name;
        } code;

    };
};

struct HirInitField {
    HirField *field;
    size_t offset;
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
    HIR_STMT_UNREACHABLE,
} HirStmtKind;

struct HirStmt {
    Span span;
    HirStmtKind kind;

    union {
        HirExpr *expr;

        struct {
            Array(HirStmt *) stmts;
        } block;

        struct {
            AstBinaryOp op;
            HirType *operation_type;
            HirType *right_type;
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
            HirStmt *post;
        } _while;

        struct {
            size_t level;
        } _break;

        struct {
            size_t level;
        } _continue;
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
    bool is_comptime;
    bool is_noreturn;
};

typedef enum {
    COMP_GLOBAL_UNVISITED,
    COMP_GLOBAL_EVALUATING,
    COMP_GLOBAL_EVALUATED,
    COMP_GLOBAL_FAILED,
} CompGlobalState;

struct HirGlobal {
    Symbol *symbol;
    HirType *type;
    HirExpr *init;

    bool is_extern;
    bool is_export;
    CompGlobalState comp_state;
};

struct HirModule {
    AstModule *owner_module;
    Array(HirFunction) functions;
    Array(HirGlobal) globals;
    String name;
};

typedef struct {
    Array(Symbol) syms;
    Array(HirStmt *) defers;
    bool loop; // used for break/continue semantics
} Scope;

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
    SYMBOL_NAMESPACE,
} SymbolKind;

struct Symbol {
    SymbolKind kind;
    AstDecl *decl;
    AstName name;
    String link_name;
    HirType *type;
    Span span;

    Scope *namespace_scope;
    AstModule *owner_module;
    Scope *owner_scope;
    Symbol *canonical;
    bool is_exported;
};

static inline String symbol_link_name(const Symbol *symbol) {
    if (symbol != NULL && symbol->link_name.data != NULL && symbol->link_name.length != 0)
        return symbol->link_name;
    if (symbol != NULL && symbol->name.kind == AST_NAME_IDENT)
        return symbol->name.ident;
    return (String){0};
}

static inline Symbol *symbol_canonical(Symbol *symbol) {
    return symbol != NULL && symbol->canonical != NULL ? symbol->canonical : symbol;
}

typedef struct {
    Path path;
    Array(String) filenames;
    AstModule *ast;
    Scope scope;
    Scope export_scope;
    bool parsed;
    bool included;
    bool analysing;
    bool analysed;
    bool analysis_failed;
} ModuleEntry;

typedef struct {
    Array(ModuleEntry) entries;
} ModuleIndex;

#endif