#ifndef MIR_H
#define MIR_H

#include "ast.h"
#include "hir.h"

typedef struct MirInstr MirInstr;
typedef struct MirBlock MirBlock;

// TODO: more operators
typedef enum {
    MIR_OP_ADD, MIR_OP_SUB, MIR_OP_MUL, MIR_OP_DIV,
    MIR_OP_LT, MIR_OP_LE, MIR_OP_GT, MIR_OP_GE, MIR_OP_EQ, MIR_OP_NE,
    MIR_OP_LOG_AND, MIR_OP_LOG_OR,
    MIR_OP_NEG, MIR_OP_NOT,
    MIR_OP_COPY,
    MIR_OP_LOAD,
    MIR_OP_STORE,
    MIR_OP_JMP,
    MIR_OP_BRANCH, MIR_OP_BRANCH_FALSE,
    MIR_OP_CALL,
    MIR_OP_RET,
    MIR_OP_LABEL
} MirOp;

typedef struct {
    enum {
        MIR_VAL_NONE,
        MIR_VAL_LIT,
        MIR_VAL_SYMBOL,
        MIR_VAL_TEMP,
        MIR_VAL_LABEL,
        MIR_VAL_MEM,
    } type;
    union {
        Literal lit;
        Symbol *symbol;
        uint32_t temp;
        uint32_t label_id;
    };
    Symbol *base_symbol;
    uint32_t base_temp;
    int64_t offset;
    TypeRef *resolved_type;
} MirOperand;

struct MirInstr {
    MirOp op;
    MirOperand result;
    MirOperand lhs;
    MirOperand rhs;
    uint32_t label_id;

    MirOperand *call_args;
    size_t arg_count;

    MirInstr *next;
    MirInstr *prev;
};

struct MirBlock {
    uint32_t id;
    MirInstr *first;
    MirInstr *last;

    MirBlock *succ_true;
    MirBlock *succ_false;

    bool visited;
};

typedef struct {
    Symbol *symbol;
    MirBlock *entry_block;
    uint32_t temp_count;
    syms_array params;
    syms_array locals;
    bool is_export;
} MirFunction;

INSTANTIATE(MirFunction *, mirfns, ARRAY_TEMPLATE)

typedef enum {
    MIR_INIT_INT,
    MIR_INIT_SYMBOL,
    MIR_INIT_ZERO
} MirInitType;

typedef struct {
    MirInitType type;
    union {
        int64_t int_val;
        Symbol *symbol_val;
    };
} MirInitVal;

INSTANTIATE(MirInitVal, mirinitvals, ARRAY_TEMPLATE)

typedef struct {
    Symbol *symbol;
    TypeRef *type;
    bool is_export;
    mirinitvals_array init_vals; 
} MirVarDecl;

INSTANTIATE(MirVarDecl *, mirvardecls, ARRAY_TEMPLATE)

typedef struct {
    mirfns_array functions;
    string_array strings;
    mirvardecls_array globals;
} MirModule;

typedef struct {
    uint32_t temp_counter;
    uint32_t label_counter;
    uint32_t block_counter;
    MirBlock *current_block;
    MirFunction *current_fn;
    string_array strings;

    Arena *arena;
    Scope *global_scope;
} MirBuilder;

MirModule *mir_lower_module(MirBuilder *ctx, HirModule *hir);
void mir_pretty_print(MirModule *mod);

static inline MirOperand make_temp(MirBuilder *ctx, TypeRef *type) {
    return (MirOperand){
        .type = MIR_VAL_TEMP,
        .temp = ctx->temp_counter++,
        .resolved_type = type
    };
}

static inline MirOperand make_symbol(Symbol *sym) {
    return (MirOperand){
        .type = MIR_VAL_SYMBOL,
        .symbol = sym,
        .resolved_type = sym->type
    };
}

static inline MirOperand make_literal(Literal lit, TypeRef *type) {
    return (MirOperand){
        .type = MIR_VAL_LIT,
        .lit = lit,
        .resolved_type = type
    };
}

static inline MirOperand null_op() {
    return (MirOperand){0};
}

#endif