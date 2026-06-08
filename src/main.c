#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
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

    ssize_t fsize = ftell(f);
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
    const char *backend_path = "./build/backends/x86_64.so";
    const char *source_path = NULL;
    const char *output_file = "a.s";

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"backend", required_argument, 0, 'b'},
        {"output", required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    const char *optstring = "hbo:";

    while ((opt = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [--backend PATH] [--source PATH]\n", argv[0]);
                return 0;
            case 'b':
                backend_path = optarg;
                break;
            case 'o':
                output_file = optarg;
                break;
            case '?':
            default:
                return 1;
        }
    }

    if (optind < argc) {
        source_path = argv[optind];
    }

    if (!source_path) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Missing source file\n", argv[0]);
        return 1;
    }

    Lexer lexer = {
        .arena = arena_create(),
        .source = setup_source((char*)source_path),
        .line = 1,
        .col = 1,
    };
    error_set_source(lexer.source);

    Parser parser = {
        .arena = lexer.arena,
        .lexer = &lexer,
        .current = lex_next_token(&lexer),
        .next = lex_next_token(&lexer)
    };

    Module *module = parse_module(&parser);
    ast_pass_monomorphise(module);

    Analyser analyser = analyser_init(module, lexer.arena);
    analyse(&analyser);

    HirModule *hir = hir_lower_module(&analyser, module);
    hir_pass_monomorphise(&analyser, hir);
    hir_pass_resolve_defers(&analyser, hir);
    // hir_pretty_print(hir);

    MirBuilder mirbuilder = {
        .arena = lexer.arena,
        .global_scope = analyser.global_scope,
    };
    MirModule *mir = mir_lower_module(&mirbuilder, hir);
    for (size_t i = 0; i < mir->functions.len; i++) {
        opt_constant_folding(mir->functions.data[i]);
    }

    // mir_pretty_print(mir);

    // backends are dls that we load at runtime
    // this allows for multiple backends
    // without rebuilding/downloading a new compiler
    lib_handle handle = load_lib((char*)backend_path);
    if (!handle) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to open backend %s\n", argv[0], backend_path);
        return 1;
    }
    typedef void (*backend_fn)(FILE *, MirBuilder *, MirModule *);
    backend_fn backend = (backend_fn)load_sym(handle, "backend");
    if (!backend) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to locate backend entry symbol from file %s\n", argv[0], backend_path);
        return 1;
    }

    FILE *output = fopen(output_file, "w");
    if (!output) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to open output file %s\n", argv[0], output_file);
        return 1;
    }
    backend(output, &mirbuilder, mir);
    close_lib(handle);
}