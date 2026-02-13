#pragma once

typedef struct {
    char *name;
} Module;

typedef struct {
    int ret_val;
} Function;

typedef struct {
    Module module;
    Function main_fn;
} Program;

Module parse_module(TokenBuffer *tokens);
Function parse_main(TokenBuffer *tokens);
Program parse_program(TokenBuffer *tokens);