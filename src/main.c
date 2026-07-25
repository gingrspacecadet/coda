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
    #include <process.h>
    #define lib_handle HMODULE
    #define load_lib(name) LoadLibraryA(name)
    #define load_sym(handle, sym) GetProcAddress(handle, sym)
    #define close_lib FreeLibrary
    #define HOST_OS "windows"
    #define SO_EXT ".dll"
    #define PATH_SEP "\\"
    #define ROOT "C:\\ProgramData\\coda"
    #define TEMP_EXE "coda_run_tmp.exe"
#elifdef __APPLE__
    #include <dlfcn.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #define lib_handle void*
    #define load_lib(name) dlopen(name, RTLD_LAZY)
    #define load_sym dlsym
    #define close_lib dlclose
    #define HOST_OS "linux"
    #define SO_EXT ".dylib"
    #define PATH_SEP "/"
    #define ROOT "/usr/share/coda"
    #define TEMP_EXE "./coda_run_tmp"    
#else
    #include <dlfcn.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #define lib_handle void*
    #define load_lib(name) dlopen(name, RTLD_LAZY)
    #define load_sym dlsym
    #define close_lib dlclose
    #define HOST_OS "linux"
    #define SO_EXT ".so"
    #define PATH_SEP "/"
    #define ROOT "/usr/share/coda"
    #define TEMP_EXE "./coda_run_tmp"
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
    int run_mode = 0;

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"target-os", required_argument, 0, 's'},
        {"target-arch", required_argument, 0, 'a'},
        {"output", required_argument, 0, 'o'},
        {"run", no_argument, 0, 'r'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    const char *optstring = "+hs:a:o:r";

    while ((opt = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [--target-os OS] [--target-arch ARCH] [--output PATH] [--run] <source.coda> [-- [args]]\n", argv[0]);
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
            case 'r':
                run_mode = 1;
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

    int forward_argc = 0;
    char **forward_argv = NULL;
    if (run_mode && (optind + 1 < argc) && strcmp(argv[optind + 1], "--") == 0) {
        forward_argc = argc - (optind + 2);
        if (forward_argc > 0) {
            forward_argv = &argv[optind + 2];
        }
    }

    const char *backend_path_override = getenv("CODA_BACKEND");
    char backend_path[512];
    snprintf(backend_path, sizeof(backend_path), "%s" PATH_SEP "%s" PATH_SEP "backend" SO_EXT, backend_path_override ? backend_path_override : ROOT, target_arch);

    const char *stdlib_dir_override = getenv("CODA_STDLIB");
    char stdlib_dir[512];
    char libcoda_file[512];
    snprintf(stdlib_dir, sizeof(stdlib_dir), "%s" PATH_SEP "%s" PATH_SEP "%s" PATH_SEP "lib", stdlib_dir_override ? stdlib_dir_override : ROOT, target_arch, target_os);
    snprintf(libcoda_file, sizeof(libcoda_file), "%s" PATH_SEP "%s" PATH_SEP "%s" PATH_SEP "lib" PATH_SEP "libcoda" SO_EXT, backend_path_override ? backend_path_override : ROOT, target_arch, target_os);

    Parser parser;
    Module *module = parse_file((char*)source_path, &parser);
    
    Analyser analyser = analyser_init(module, module->arena);
    
    scan_dir(&analyser, stdlib_dir);
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
    
    MirBuilder mirbuilder = {
        .arena = module->arena,
        .global_scope = analyser.global_scope,
        .strings = string_array_init(module->arena),
    };
    MirModule *mir = mir_lower_module(&mirbuilder, hir);
    for (size_t i = 0; i < mir->functions.len; i++) {
        opt_constant_folding(mir->functions.data[i]);
    }
    
    // hir_pretty_print(hir); mir_pretty_print(mir);

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

    if (run_mode) {
        char compile_cmd[1024];
#ifdef WIN32
        snprintf(compile_cmd, sizeof(compile_cmd), "gcc %s -o %s \"%s\"", 
                 output_file, TEMP_EXE, libcoda_file);
#else
        snprintf(compile_cmd, sizeof(compile_cmd), "gcc %s -o %s \"%s\" -Wl,-rpath,\"%s\"", 
                 output_file, TEMP_EXE, libcoda_file, stdlib_dir);
#endif

        if (system(compile_cmd) != 0) {
            fprintf(stderr, "\e[1;37m%s:\e[0m \e[1;31merror:\e[0m Failed to assemble and link generated assembly file\n", argv[0]);
            return 1;
        }

        char **exec_argv = malloc((forward_argc + 2) * sizeof(char*));
        exec_argv[0] = TEMP_EXE;
        for (int i = 0; i < forward_argc; i++) {
            exec_argv[i + 1] = forward_argv[i];
        }
        exec_argv[forward_argc + 1] = NULL;

        int run_res = 0;
#ifdef WIN32
        run_res = _spawnvp(_P_WAIT, TEMP_EXE, (const char *const *)exec_argv);
        _unlink(TEMP_EXE);
#else
        pid_t pid = fork();
        if (pid == 0) {
            execvp(TEMP_EXE, exec_argv);
            perror("execvp");
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            run_res = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        } else {
            perror("fork");
            run_res = 1;
        }
        remove(TEMP_EXE);
#endif
        free(exec_argv);
        return run_res;
    }

    arena_destroy(analyser.arena);

    return 0;
}