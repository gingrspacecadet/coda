#ifndef MIR_H
#define MIR_H

#include "ast.h"

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
    MIR_OP_BRANCH,
    MIR_OP_CALL,
    MIR_OP_RET,
    MIR_OP_LABEL
} MirOp;

typedef struct {
    enum {
        MIR_VAL_LIT,
        MIR_VAL_SYMBOL,
        MIR_VAL_TEMP
    } type;
    union {
        Literal lit;
        Symbol *symbol;
        uint32_t temp;
    };
    TypeRef *resolved_type;
} MirOperand;


struct MirInstr {
    MirOp op;
    MirOperand result;
    MirOperand lhs;
    MirOperand rhs;

    MirInstr *next;
    MirInstr *prev;
};

INSTANTIATE(MirBlock*, mirblocks, ARRAY_TEMPLATE)

struct MirBlock {
    uint32_t id;
    MirInstr *first;
    MirInstr *last;

    mirblocks_array successors;
};

typedef struct {
    Symbol *symbol;
    MirBlock *entry_block;
    uint32_t temp_count;
} MirFunction;

#endif