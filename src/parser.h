#pragma once

typedef enum {
    EXPR_NUMBER,
    EXPR_BINOP
} ExprType;

typedef struct Expr {
    ExprType type;
    union {
        int number;
        struct {
            struct Expr *left;
            char op;
            struct Expr *right;
        } binop;
    };
} Expr;

typedef struct {
    char *name;
} Module;

typedef struct {
    Expr *ret;
} Function;

typedef struct {
    Module module;
    Function main_fn;
} Program;

Program parse_program(TokenBuffer *tokens);