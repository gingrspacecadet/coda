#include "diag.h"
#include <assert.h>

void diags_init(Diags *diags, Arena *arena) {
    assert(diags != NULL);
    assert(arena != NULL);

    diags->arena = arena;
    array_init(&diags->diags, arena, sizeof(Diag)); 
}

void diags_clear(Diags *diags) {
    assert(diags != NULL);

    array_clear(&diags->diags);
}

bool diags_has_errors(const Diags *diags) {
    assert(diags != NULL);

    Diag *d_array = (Diag *)diags->diags.data;
    
    for (size_t i = 0; i < diags->diags.len; i++) {
        if (d_array[i].severity == DIAG_ERROR) {
            return true;
        }
    }
    return false;
}

DiagBuilder diag_begin(Diags *diags, DiagSeverity severity, DiagCode code, Span primary_span, String message) {
    assert(diags != NULL);
    
    Diag d = {0};
    d.severity = severity;
    d.code = code;
    d.message = message;
    
    d.primary.span = primary_span;
    
    array_init(&d.labels, diags->arena, sizeof(DiagLabel));
    array_init(&d.notes, diags->arena, sizeof(String));
    array_init(&d.help, diags->arena, sizeof(String));
    
    array_push(&diags->diags, &d);
    
    DiagBuilder builder;
    builder.diags = diags;
    builder.index = diags->diags.len - 1;
    
    return builder;
}

void diag_label(DiagBuilder *builder, Span span, String message) {
    assert(builder != NULL);
    
    Diag *d = (Diag *)array_at(&builder->diags->diags, builder->index);
    
    DiagLabel label = { .span = span, .message = message };
    array_push(&d->labels, &label);
}

void diag_note(DiagBuilder *builder, String message) {
    assert(builder != NULL);

    Diag *d = (Diag *)array_at(&builder->diags->diags, builder->index);
    array_push(&d->notes, &message);
}

void diag_help(DiagBuilder *builder, String message) {
    assert(builder != NULL);

    Diag *d = (Diag *)array_at(&builder->diags->diags, builder->index);
    array_push(&d->help, &message);
}

void diag_finish(DiagBuilder *builder) {
    assert(builder != NULL);

    (void)builder;
}