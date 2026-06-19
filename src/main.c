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

// platform-agnostic dl loading and path naming
#ifdef WIN32
    #include <windows.h>
    #define lib_handle HMODULE
    #define load_lib(name) LoadLibraryA(name)
    #define load_sym(handle, sym) GetProcAddress(handle, sym)
    #define close_lib FreeLibrary
    #define HOST_OS "windows"
    #define SO_EXT ".dll"
    #define PATH_SEP "\\"
    #define ROOT "C:\\ProgramData\\coda"
#else
    #include <dlfcn.h>
    #define lib_handle void*
    #define load_lib(name) dlopen(name, RTLD_LAZY)
    #define load_sym dlsym
    #define close_lib dlclose
    #define HOST_OS "linux"
    #define SO_EXT ".so"
    #define PATH_SEP "/"
    #define ROOT "/usr/share/coda"
#endif

#if defined(__x86_64__) || defined(_M_X64)
    #define HOST_ARCH "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define HOST_ARCH "aarch64"
#else
    #define HOST_ARCH "unknown"
#endif

int main(int argc, char **argv) {
    const char *target_os = HOST_OS;
    const char *target_arch = HOST_ARCH;
    const char *source_path = NULL;
    const char *output_file = "a.s";

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"target-os", required_argument, 0, 's'},
        {"target-arch", required_argument, 0, 'a'},
        {"output", required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    const char *optstring = "hs:a:o:";

    while ((opt = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [--target-os OS] [--target-arch ARCH] [--output PATH] <source.coda>\n", argv[0]);
                return 0;
            case 's':
                target_os = optarg;
                break;
            case 'a':
                target_arch = optarg;
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

    // construct backend shared library path dynamically
    // e.g., /usr/share/coda/x86_64/backend.so
    const char *backend_path_override = getenv("CODA_BACKEND");
    char backend_path[512];
    if (backend_path_override) {
        snprintf(backend_path, sizeof(backend_path), "%s", backend_path_override);
    } else {
        snprintf(backend_path, sizeof(backend_path), ROOT PATH_SEP "%s" PATH_SEP "backend" SO_EXT, target_arch);
    }

    // construct the target standard library directory path
    // e.g., /usr/share/coda/x86_64/linux/lib/
    const char *stdlib_path_override = getenv("CODA_STDLIB");
    char stdlib_path[512];
    if (stdlib_path_override) {
        snprintf(stdlib_path, sizeof(stdlib_path), "%s", stdlib_path_override);
    } else {
        snprintf(stdlib_path, sizeof(stdlib_path), ROOT PATH_SEP "%s" PATH_SEP "%s" PATH_SEP "lib" PATH_SEP, target_arch, target_os);
    }

    Parser parser;
    Module *module = parse_file((char*)source_path, &parser);
    
    Analyser analyser = analyser_init(module, module->arena);
    
    scan_dir(&analyser, stdlib_path);
    scan_dir(&analyser, ".");
    resolve_includes(&analyser, module);
    
    populate_module_namespaces(&analyser, module);
    for (size_t i = 0; i < analyser.module_map.len; i++) {
        if (analyser.module_map.data[i].is_parsed) {
            populate_module_namespaces(&analyser, analyser.module_map.data[i].ast);
        }
    }
    
    ast_pass_monomorphise(module);
    analyse(&analyser);

    HirModule *hir = hir_lower_module(&analyser, module);
    hir_pass_monomorphise(&analyser, hir);
    hir_pass_resolve_defers(&analyser, hir);
    hir_pretty_print(hir);

    MirBuilder mirbuilder = {
        .arena = module->arena,
        .global_scope = analyser.global_scope,
        .strings = string_array_init(),
    };
    MirModule *mir = mir_lower_module(&mirbuilder, hir);
    for (size_t i = 0; i < mir->functions.len; i++) {
        opt_constant_folding(mir->functions.data[i]);
    }
    mir_pretty_print(mir);

    // Load backend from structural runtime path
    lib_handle handle = load_lib((char*)backend_path);
    if (!handle) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to open target backend shared library: %s\n", argv[0], backend_path);
        return 1;
    }
    typedef void (*backend_fn)(FILE *, MirBuilder *, MirModule *);
    backend_fn backend = (backend_fn)load_sym(handle, "backend");
    if (!backend) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to locate backend entry symbol from file %s\n", argv[0], backend_path);
        close_lib(handle);
        return 1;
    }

    FILE *output = fopen(output_file, "w");
    if (!output) {
        fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to open output file %s\n", argv[0], output_file);
        close_lib(handle);
        return 1;
    }
    backend(output, &mirbuilder, mir);
    close_lib(handle);
    fclose(output);

    return 0;
}