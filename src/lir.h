#ifndef LIR_H
#define LIR_H

#include "ast.h"

typedef enum {
    LIR_REG_PHYSICAL,
    LIR_REG_VIRTUAL,
    LIR_IMM,
    LIR_MEM
} LirOperandType;

typedef enum {
    REG_RAX,
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
    LIR_CMP, LIR_JMP, LIR_JCC,
    LIR_CALL, LIR_RET,
    LIR_LABEL,
    LIR_PUSH, LIR_POP
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
} LirFunction;

#endif