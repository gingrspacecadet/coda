#ifndef CODA_LIR_H
#define CODA_LIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "arena.h"
#include "array.h"
#include "hir.h"

typedef uint32_t LirValueId;
typedef uint32_t LirBlockId;

#define LIR_INVALID_VALUE UINT32_MAX
#define LIR_INVALID_BLOCK UINT32_MAX

typedef enum {
    LIR_OPERAND_INVALID,
    LIR_OPERAND_VALUE,
    LIR_OPERAND_INT,
    LIR_OPERAND_UINT,
    LIR_OPERAND_FLOAT,
    LIR_OPERAND_BOOL,
    LIR_OPERAND_SYMBOL,
} LirOperandKind;

typedef struct {
    LirOperandKind kind;
    HirType *type;

    union {
        LirValueId value;
        int64_t int_value;
        uint64_t uint_value;
        double float_value;
        bool bool_value;
        Symbol *symbol;
    };
} LirOperand;

typedef enum {
    LIR_OP_NEG,
    LIR_OP_NOT,

    LIR_OP_ADD,
    LIR_OP_SUB,
    LIR_OP_MUL,
    LIR_OP_DIV,
    LIR_OP_MOD,

    LIR_OP_SHL,
    LIR_OP_SHR,

    LIR_OP_AND,
    LIR_OP_OR,
    LIR_OP_XOR,

    LIR_OP_LT,
    LIR_OP_LE,
    LIR_OP_GT,
    LIR_OP_GE,
    LIR_OP_EQ,
    LIR_OP_NE,

    LIR_OP_CAST,

    LIR_OP_LOAD,
    LIR_OP_STORE,

    LIR_OP_ADDR,
    LIR_OP_ADDR_ADD,

    LIR_OP_CALL,
} LirOpcode;

typedef struct {
    LirValueId value;
    HirType *type;
} LirBlockParam;

typedef struct {
    LirOpcode opcode;
    LirValueId result;
    HirType *result_type;
    Array(LirOperand) operands;
} LirInstruction;

typedef enum {
    LIR_TERM_NONE,
    LIR_TERM_RETURN,
    LIR_TERM_JUMP,
    LIR_TERM_BRANCH,
} LirTerminatorKind;

typedef struct {
    LirTerminatorKind kind;

    union {
        struct {
            bool has_value;
            LirOperand value;
        } _return;

        struct {
            LirBlockId target;
            Array(LirOperand) args;
        } jump;

        struct {
            LirOperand condition;
            LirBlockId then_block;
            Array(LirOperand) then_args;
            LirBlockId else_block;
            Array(LirOperand) else_args;
        } branch;
    };
} LirTerminator;

typedef struct {
    Symbol *symbol;
    HirType *type;
    LirValueId value;
} LirFunctionParam;

typedef struct {
    LirBlockId id;
    Array(LirBlockParam) params;
    Array(LirInstruction) instructions;
    LirTerminator terminator;
} LirBlock;

typedef struct {
    Arena *arena;

    Symbol *symbol;
    HirType *return_type;

    bool is_extern;
    bool is_export;

    Array(LirFunctionParam) params;
    Array(LirBlock *) blocks;

    LirBlockId entry;
    LirValueId next_value;
} LirFunction;

typedef struct {
    Arena *arena;
    Array(LirFunction *) functions;
} LirModule;

LirModule *lir_module_create(Arena *arena);
LirFunction *lir_function_create(LirModule *module, Symbol *symbol, HirType *return_type, bool is_extern, bool is_export);
LirBlockId lir_block_create(LirFunction *function);

LirValueId lir_function_add_param(LirFunction *function, Symbol *symbol, HirType *type);
LirValueId lir_block_add_param(LirFunction *function, LirBlockId block, HirType *type);

LirValueId lir_emit(LirFunction *function, LirBlockId block_id, LirOpcode opcode, HirType *result_type, Array(LirOperand) operands);
void lir_return(LirFunction *function, LirBlockId block_id, LirOperand *value);
void lir_jump(LirFunction *function, LirBlockId block_id, LirBlockId target, Array(LirOperand) args);
void lir_branch(LirFunction *function, LirBlockId block_id, LirOperand condition, LirBlockId then_block, Array(LirOperand) then_args, LirBlockId else_block, Array(LirOperand) else_args);

LirOperand lir_operand_value(LirValueId value, HirType *type);
LirOperand lir_operand_int(int64_t value, HirType *type);
LirOperand lir_operand_uint(uint64_t value, HirType *type);
LirOperand lir_operand_float(double value, HirType *type);
LirOperand lir_operand_bool(bool value, HirType *type);
LirOperand lir_operand_symbol(Symbol *symbol, HirType *type);
LirOperand lir_operand_invalid(void);

const char *lir_opcode_name(LirOpcode opcode);
void lir_print(FILE *out, const LirModule *module);

#endif