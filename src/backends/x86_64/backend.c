#include "../../mir.h"
#include <stdio.h>
#include "lir.h"
#include "codegen.h"

// TODO: pass options like -Ox, etc
void backend(FILE *out, MirBuilder *ctx, MirModule *module) {
    for (size_t i = 0; i < module->functions.len; i++) {
        LirFunction *lir = lir_lower_fn(ctx, module->functions.data[i]);
        codegen(out, lir);
    }
}