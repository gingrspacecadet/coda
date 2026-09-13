#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>
#include "source.h"
#include "array.h"
#include "token.h"

typedef enum {
    DIAG_ERROR
} DiagSeverity;

typedef enum {
    E_EXPECTED_TOKEN = 1000,
    E_EXPECTED_IDENTIFIER,
    E_EXPECTED_TYPE,
    E_EXPECTED_EXPRESSION,
    E_EXPECTED_DECLARATION,
    E_EXPECTED_PATTERN,
    E_UNEXPECTED_TOKEN,

    E_UNKNOWN_NAME = 2000,
    E_DUPLICATE_SYMBOL,
    E_SHADOWING,
    E_UNKNOWN_TYPE,
    E_TYPE_MISMATCH,
    E_EXPECTED_FUNCTION,
    E_WRONG_ARGUMENT_COUNT,
    E_UNKNOWN_FIELD,
    E_INVALID_UNARY_OPERATION,
    E_INVALID_BINARY_OPERATION,
    E_INVALID_RETURN,
    E_INVALID_BREAK,
    E_INVALID_CONTINUE,
    E_NAMESPACE_VALUE,
    E_MODULE_NOT_FOUND,
} DiagCode;

typedef struct {
    Span span;
    String message;
} DiagLabel;

typedef struct {
    DiagSeverity severity;
    DiagCode code;

    String message;

    DiagLabel primary;

    Array/*DiagLabel*/ labels;

    Array/*String*/ notes;
    Array/*String*/ help;
} Diag;

typedef struct {
    Arena *arena;

    Array/*Diag*/ diags;
} Diags;

void diags_init(Diags *diags, Arena *arena);
void diags_clear(Diags *diags);

bool diags_has_errors(const Diags *diags);

typedef struct {
    Diags *diags;
    size_t index;
} DiagBuilder;

DiagBuilder diag_begin(Diags *diags, DiagSeverity severity, DiagCode code, Span primary, String message);

void diag_label(DiagBuilder *builder, Span span, String message);

void diag_note(DiagBuilder *builder, String message);

void diag_help(DiagBuilder *builder, String message);

void diag_finish(DiagBuilder *builder);

#endif