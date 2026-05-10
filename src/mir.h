#ifndef MIR_H
#define MIR_H

#include "ast.h"
#include "hir.h"

typedef struct MirInstr MirInstr;
typedef struct MirBlock MirBlock;

// TODO: more operators
typedef enum {
    MIR_OP_ADD, MIR_OP_SUB, MIR_OP_MUL, MIR_OP_DIV,
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
        MIR_VAL_LIT,
        MIR_VAL_SYMBOL,
        MIR_VAL_TEMP,
        MIR_VAL_LABEL,
    } type;
    union {
        Literal lit;
        Symbol *symbol;
        uint32_t temp;
        uint32_t label_id;
    };
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
} MirFunction;

INSTANTIATE(MirFunction *, mirfns, ARRAY_TEMPLATE)

typedef struct {
    mirfns_array functions;
} MirModule;

typedef struct {
    uint32_t temp_counter;
    uint32_t label_counter;
    uint32_t block_counter;
    MirBlock *current_block;
    MirFunction *current_fn;

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