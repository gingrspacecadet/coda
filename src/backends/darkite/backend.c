#include "../../mir.h"
#include <stdio.h>
#include "lir.h"
#include "codegen.h"

// TODO: pass options like -Ox, etc
void backend(FILE *out, MirBuilder *ctx, MirModule *module) {
    string_const_array string_consts = string_const_array_init(ctx->arena);
    
    // First pass: collect all string constants and lower to LIR
    for (size_t i = 0; i < module->functions.len; i++) {
        LirFunction *lir = lir_lower_fn(ctx, module->functions.data[i]);
        collect_string_constants(lir, &string_consts);
    }
    
    // Emit .rodata section with string constants
    if (string_consts.len > 0) {
        fprintf(out, ".section .rodata\n");
        for (size_t i = 0; i < string_consts.len; i++) {
            fprintf(out, ".LC%u:\n", (unsigned int)i);
            fprintf(out, "    .string \"");
            for (size_t j = 0; j < string_consts.data[i].str.length; j++) {
                char c = string_consts.data[i].str.data[j];
                if (c == '\\') fprintf(out, "\\\\");
                else if (c == '"') fprintf(out, "\\\"");
                else if (c == '\n') fprintf(out, "\\n");
                else if (c == '\t') fprintf(out, "\\t");
                else fprintf(out, "%c", c);
            }
            fprintf(out, "\"\n");
        }
    }
    
    // Second pass: code generation
    for (size_t i = 0; i < module->functions.len; i++) {
        LirFunction *lir = lir_lower_fn(ctx, module->functions.data[i]);
        codegen(out, lir, &string_consts);
    }
}