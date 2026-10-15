#include <stdio.h>
#include <string.h>

#include "mangle.h"
#include "test.h"

bool test_mangling(void) {
    Arena *arena = arena_create();
    if (arena == NULL)
        return false;

    Diags diags = {
        .arena = arena,
        .diags = array_create(arena, sizeof(Diag)),
    };
    Sema sema = sema_create(arena, &diags);

    AstModule module = {0};
    module.path.parts = array_create(arena, sizeof(AstName));
    AstName part = {.kind = AST_NAME_IDENT, .ident = STRING("foo")};
    array_push(&module.path.parts, &part);
    part = (AstName){.kind = AST_NAME_IDENT, .ident = STRING("bar")};
    array_push(&module.path.parts, &part);

    Symbol function = {
        .kind = SYMBOL_FN,
        .name = {.kind = AST_NAME_IDENT, .ident = STRING("identity")},
        .owner_module = &module,
    };

    HirType int32_type = {.kind = HIR_TYPE_BUILTIN, .builtin = BUILTIN_INT32};
    HirType *argument_type = &int32_type;
    Array arguments = array_create(arena, sizeof(HirType *));
    array_push(&arguments, &argument_type);

    String mangled = coda_mangle_symbol(&sema, &function, arguments, true);
    CodaDemangledSymbol decoded;
    if (mangled.data == NULL || !coda_demangle_symbol(arena, mangled, &decoded)) {
        fprintf(stderr, "mangling test: failed to demangle generated symbol\n");
        arena_destroy(arena);
        return false;
    }

    String display = coda_demangled_symbol_name(arena, &decoded);
    bool passed = decoded.kind == 'F' && decoded.module_path.len == 2 &&
                  string_eq(((String *)decoded.module_path.data)[0], STRING("foo")) &&
                  string_eq(((String *)decoded.module_path.data)[1], STRING("bar")) &&
                  string_eq(decoded.name, STRING("identity")) && decoded.receiver.length == 0 &&
                  decoded.signature.length == 0 && decoded.generic_arguments.len == 1 &&
                  string_eq(((String *)decoded.generic_arguments.data)[0], STRING("T:int32")) &&
                  string_eq(display, STRING("foo::bar::identity<int32>"));

    if (!passed)
        fprintf(stderr, "mangling test: decoded identity does not match source identity\n");

    Symbol ordinary = {
        .kind = SYMBOL_FN,
        .name = {.kind = AST_NAME_IDENT, .ident = STRING("work")},
        .owner_module = &module,
    };
    Array no_arguments = array_create(arena, sizeof(HirType *));
    String ordinary_name = coda_mangle_symbol(&sema, &ordinary, no_arguments, false);
    CodaDemangledSymbol ordinary_decoded;
    if (!coda_demangle_symbol(arena, ordinary_name, &ordinary_decoded) ||
        !string_eq(coda_demangled_symbol_name(arena, &ordinary_decoded), STRING("foo::bar::work"))) {
        fprintf(stderr, "mangling test: cross-module function identity was not reconstructed\n");
        passed = false;
    }

    AstDecl signature_decl = {
        .kind = AST_DECL_FN,
        .attrs = array_create(arena, sizeof(AstAttribute)),
    };
    HirType function_type = {
        .kind = HIR_TYPE_FUNCTION,
        .function = {
            .ret = &int32_type,
            .params = array_create(arena, sizeof(HirType *)),
        },
    };
    array_push(&function_type.function.params, &argument_type);
    Symbol signature_function = {
        .kind = SYMBOL_FN,
        .decl = &signature_decl,
        .name = {.kind = AST_NAME_IDENT, .ident = STRING("with_sig")},
        .type = &function_type,
        .owner_module = &module,
    };
    String signature_mangled = coda_mangle_symbol(&sema, &signature_function, no_arguments, false);
    CodaDemangledSymbol signature_decoded;
    if (!coda_demangle_symbol(arena, signature_mangled, &signature_decoded) ||
        !string_eq(signature_decoded.signature, STRING("fn(int32)->int32"))) {
        fprintf(stderr, "mangling test: function signature was not reconstructed\n");
        passed = false;
    }

    CodaDemangledSymbol method;
    String method_name = STRING("__coda1_F_M2_3_666f6f3_626172_N3_62617a_R13_666f6f3a3a6261723a3a426f78_P0__G1_7_543a696e743332");
    if (!coda_demangle_symbol(arena, method_name, &method) ||
        !string_eq(coda_demangled_symbol_name(arena, &method), STRING("foo::bar::Box.baz<int32>"))) {
        fprintf(stderr, "mangling test: method receiver was not reconstructed\n");
        passed = false;
    }

    CodaDemangledSymbol invalid;
    if (coda_demangle_symbol(arena, STRING("__coda1_F_M99_"), &invalid)) {
        fprintf(stderr, "mangling test: malformed name was accepted\n");
        passed = false;
    }

    AstDecl extern_decl = {
        .attrs = array_create(arena, sizeof(AstAttribute)),
    };
    AstAttribute extern_attr = {.name = STRING("extern")};
    array_push(&extern_decl.attrs, &extern_attr);
    Symbol foreign_function = {
        .kind = SYMBOL_FN,
        .decl = &extern_decl,
        .name = {.kind = AST_NAME_IDENT, .ident = STRING("puts")},
        .owner_module = &module,
    };
    if (!string_eq(coda_mangle_symbol(&sema, &foreign_function, no_arguments, false), STRING("puts"))) {
        fprintf(stderr, "mangling test: @extern name was unexpectedly mangled\n");
        passed = false;
    }

    sema.root_module = &module;
    Symbol root_function = {
        .kind = SYMBOL_FN,
        .name = {.kind = AST_NAME_IDENT, .ident = STRING("main")},
        .owner_module = &module,
    };
    String root_name = coda_mangle_symbol(&sema, &root_function, no_arguments, false);
    if (!string_eq(root_name, STRING("main"))) {
        fprintf(stderr, "mangling test: root entry-point name was unexpectedly mangled\n");
        passed = false;
    }

    arena_destroy(arena);
    return passed;
}
