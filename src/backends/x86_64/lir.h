#ifndef LIR_H
#define LIR_H

#include "../../ast.h"
#include "../../mir.h"

typedef enum {
    LIR_NONE,
    LIR_REG_PHYSICAL,
    LIR_REG_VIRTUAL,
    LIR_IMM,
    LIR_MEM,
    LIR_STACK,
    LIR_GLOBAL,
} LirOperandType;

typedef enum {
    REG_RAX,
    REG_RDI,
    REG_RSI,
    REG_RDX,
    REG_RCX,
    REG_R8,
    REG_R9
} PhysReg;

typedef struct {
    LirOperandType type;
    union {
        PhysReg preg;
        uint32_t vreg;
        int64_t imm;
        struct {
            uint32_t base_vreg;
            int32_t offset;
        } mem;
        Symbol *symbol;
    };
    TypeRef *resolved_type;
} LirOperand;

typedef enum {
    COND_E,
    COND_NE,
    COND_L,
    COND_LE,
    COND_G,
    COND_GE,
    COND_NONE
} LirCond;

typedef enum {
    LIR_MOV,
    LIR_ADD, LIR_SUB, LIR_IMUL, LIR_IDIV,
    LIR_CMP, LIR_SETCC, LIR_JMP, LIR_JCC,
    LIR_CALL, LIR_RET,
    LIR_LABEL,
    LIR_PUSH, LIR_POP,
    // x86 shit
    LIR_CQO
} LirOpcode;

typedef struct LirInstr LirInstr;

struct LirInstr {
    LirOpcode opcode;
    LirOperand dest;
    LirOperand src;
    LirCond cond;

    LirInstr *next;
    LirInstr *prev;
};

typedef struct {
    Symbol *symbol;
    LirInstr *first;
    LirInstr *last;
    uint32_t vreg_count;
} LirFunction;

INSTANTIATE(LirFunction *, lirfns, ARRAY_TEMPLATE)

typedef struct {
    lirfns_array functions;
} LirModule;

void lir_pretty_print(LirFunction *fn);
LirFunction *lir_lower_fn(MirBuilder *ctx, MirFunction *mir_fn);

#endif