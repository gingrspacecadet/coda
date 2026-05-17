#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "arena.h"
#include "error.h"
#include "hir.h"
#include "mir.h"
#include "opt.h"

// platform-agnostic dl loading
#ifdef WIN32
    #include <windows.h>
    #define lib_handle HMODULE
    #define load_lib(name) LoadLibraryA(name)
    #define load_sym(handle, sym) GetProcAddress(handle, sym)
    #define close_lib FreeLibrary
#else
    #include <dlfcn.h>
    #define lib_handle void*
    #define load_lib(name) dlopen(name, RTLD_LAZY)
    #define load_sym dlsym
    #define close_lib dlclose
#endif

char *read_file(char *path) {
    FILE *f = fopen(path, "r");
    if (!f) goto err;

    if (fseek(f, 0, SEEK_END) != 0) goto err;

    size_t fsize = ftell(f);
    if (fsize < 0) goto err;
    if (fseek(f, 0, SEEK_SET) != 0) goto err;

    char *data = malloc(fsize + 1);
    if (!data) goto err;
    if (fread(data, fsize, 1, f) != 1) goto err;
    if (fclose(f) != 0) goto err;
    data[fsize] = 0;
    return data;

err:
    fprintf(stderr, "read_file failed\n");
    exit(1);
}

Source setup_source(char *path) {
    Source source;
    source.path = string_make(path);
    source.index = 0;
    source.contents = string_make(read_file(path));

    return source;
}

int main(int argc, char **argv) {
    Lexer lexer = {
        .arena = arena_create(),
        .source = setup_source("test/main.coda"),
        .line = 1,
        .col = 1,
    };
    error_set_source(lexer.source);

    token_array tokens = lex(&lexer);

    Parser parser = {
        .arena = lexer.arena,
        .index = 0,
        .tokens = tokens,
    };

    Module *module = parse_module(&parser);

    Analyser analyser = analyser_init(module, lexer.arena);
    analyse(&analyser);

    HirModule *hir = hir_lower_module(&analyser, module);

    MirBuilder mirbuilder = {
        .arena = lexer.arena,
        .global_scope = analyser.global_scope,
    };
    MirModule *mir = mir_lower_module(&mirbuilder, hir);
    for (size_t i = 0; i < mir->functions.len; i++) {
        opt_constant_folding(mir->functions.data[i]);
    }

    // hir_pretty_print(hir);
    // mir_pretty_print(mir);   // TODO: i needa add cli argssss faah

    // backends are dls that we load at runtime
    // this allows for multiple backends
    // without rebuilding/downloading a new compiler
    lib_handle handle = load_lib("./build/backends/x86_64.so");
    if (!handle) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to open backend\n", argv[0]);
        return 1;
    }
    void (*backend)(FILE *, MirBuilder *, MirModule *) = load_sym(handle, "backend");
    if (!backend) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to locate backend entry symbol\n", argv[0]);
        return 1;
    }
    backend(stdout, &mirbuilder, mir);
    close_lib(handle);
}