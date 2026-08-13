#ifndef DIAG_H
#define DIAG_H

#include "source.h"
#include "array.h"

typedef enum {
    DIAG_ERROR
} DiagSeverity;

typedef uint32_t DiagCode;

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