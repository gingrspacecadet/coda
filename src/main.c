#include <time.h>
#include "print.h"
#include "coda.h"

static bool load_source(const char *path, Source *source, Arena *arena) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        perror(path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long size = ftell(file);

    if (size < 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char *contents = arena_calloc(arena, (size_t)size + 1);

    if (contents == NULL) {
        fclose(file);
        return false;
    }

    if (fread(contents, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        return false;
    }

    fclose(file);

    contents[size] = '\0';

    *source = (Source) {
        .path = (String) {
            .data = (char *)path,
            .length = strlen(path),
        },
        .contents = (String) {
            .data = contents,
            .length = (size_t)size,
        },
    };

    return true;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;

    Arena *arena = arena_create();
    Diags diags = {.arena = arena, .diags = array_create(arena, sizeof(Diag))};

    Source source;

    if (!load_source(argv[1], &source, arena)) {
        arena_destroy(arena);
        return 1;
    }

    const TargetInfo *target = target_native();

    if (target == NULL) {
        fprintf(stderr, "native target is unsupported\n");
        arena_destroy(arena);
        return 1;
    }

    FILE *output = fopen("a.s", "w");

    if (output == NULL) {
        perror("a.s");
        arena_destroy(arena);
        return 1;
    }

    CodaCompiler compiler;

    coda_compiler_init(&compiler, arena, &diags, target);
    compiler.output = output;

    if (!coda_compile(&compiler, &source, CODA_STAGE_CODEGEN)) {
        fclose(output);
        print_diags(&diags);
        arena_destroy(arena);
        return 1;
    }

    fclose(output);

    print_ast_module(stdout, compiler.compilation.ast);
    print_hir_module(stdout, compiler.compilation.hir);
    lir_print(stdout, compiler.compilation.lir);

    arena_destroy(arena);

    return 0;
}