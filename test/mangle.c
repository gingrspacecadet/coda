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

    AstModule io_module = {0};
    io_module.path.parts = array_create(arena, sizeof(AstName));
    AstName io_part = {.kind = AST_NAME_IDENT, .ident = STRING("std")};
    array_push(&io_module.path.parts, &io_part);
    io_part = (AstName){.kind = AST_NAME_IDENT, .ident = STRING("io")};
    array_push(&io_module.path.parts, &io_part);
    Symbol print_function = {
        .kind = SYMBOL_FN,
        .name = {.kind = AST_NAME_IDENT, .ident = STRING("print")},
        .owner_module = &io_module,
    };
    Array no_arguments = array_create(arena, sizeof(HirType *));
    String print_name = coda_mangle_symbol(&sema, &print_function, no_arguments, false);
    if (!string_eq(print_name, STRING("_C3std2io5print"))) {
        fprintf(stderr, "mangling test: simple qualified name changed unexpectedly\n");
        arena_destroy(arena);
        return false;
    }

    HirType int32_type = {.kind = HIR_TYPE_BUILTIN, .builtin = BUILTIN_INT32};
    HirType *argument_type = &int32_type;
    Array arguments = array_create(arena, sizeof(HirType *));
    array_push(&arguments, &argument_type);

    String mangled = coda_mangle_symbol(&sema, &function, arguments, true);
    if (!string_eq(mangled, STRING("_C3foo3bar8identity_G1T5int32"))) {
        fprintf(stderr, "mangling test: generic name is not compact and readable\n");
        arena_destroy(arena);
        return false;
    }
    CodaDemangledSymbol decoded;
    if (mangled.data == NULL || !coda_demangle_symbol(arena, mangled, &decoded)) {
        fprintf(stderr, "mangling test: failed to demangle generated symbol\n");
        arena_destroy(arena);
        return false;
    }

    String display = coda_demangled_symbol_name(arena, &decoded);
    bool passed = decoded.kind == 0 && decoded.signature.length == 0 && decoded.module_path.len == 2 &&
                  string_eq(((String *)decoded.module_path.data)[0], STRING("foo")) &&
                  string_eq(((String *)decoded.module_path.data)[1], STRING("bar")) &&
                  string_eq(decoded.name, STRING("identity")) && decoded.receiver.length == 0 &&
                  decoded.generic_arguments.len == 1 &&
                  string_eq(((String *)decoded.generic_arguments.data)[0], STRING("T:int32")) &&
                  string_eq(display, STRING("foo::bar::identity<int32>"));

    if (!passed)
        fprintf(stderr, "mangling test: decoded identity does not match source identity\n");

    HirType pointer_type = {.kind = HIR_TYPE_POINTER, .pointer = {.pointee = &int32_type}};
    HirType *pointer_argument = &pointer_type;
    Array pointer_arguments = array_create(arena, sizeof(HirType *));
    array_push(&pointer_arguments, &pointer_argument);
    String pointer_mangled = coda_mangle_symbol(&sema, &function, pointer_arguments, true);
    if (!string_eq(pointer_mangled, STRING("_C3foo3bar8identity_G1T8int32_2A"))) {
        fprintf(stderr, "mangling test: punctuation in type arguments was not made assembler-safe\n");
        passed = false;
    } else {
        CodaDemangledSymbol pointer_decoded;
        if (!coda_demangle_symbol(arena, pointer_mangled, &pointer_decoded) ||
            !string_eq(coda_demangled_symbol_name(arena, &pointer_decoded), STRING("foo::bar::identity<int32*>"))) {
            fprintf(stderr, "mangling test: escaped type argument did not round-trip\n");
            passed = false;
        }
    }

    Symbol ordinary = {
        .kind = SYMBOL_FN,
        .name = {.kind = AST_NAME_IDENT, .ident = STRING("work")},
        .owner_module = &module,
    };
    String ordinary_name = coda_mangle_symbol(&sema, &ordinary, no_arguments, false);
    if (!string_eq(ordinary_name, STRING("_C3foo3bar4work"))) {
        fprintf(stderr, "mangling test: ordinary name is not compact and readable\n");
        passed = false;
    }
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
    if (!string_eq(signature_mangled, STRING("_C3foo3bar8with_sig"))) {
        fprintf(stderr, "mangling test: ordinary function incorrectly encodes its signature\n");
        passed = false;
    }

    CodaDemangledSymbol method;
    String method_name = STRING("_C3foo3bar3baz_R21foo_3A_3Abar_3A_3ABox_G1T5int32");
    if (!coda_demangle_symbol(arena, method_name, &method) ||
        !string_eq(coda_demangled_symbol_name(arena, &method), STRING("foo::bar::Box.baz<int32>"))) {
        fprintf(stderr, "mangling test: method receiver was not reconstructed\n");
        passed = false;
    }

    CodaDemangledSymbol invalid;
    if (coda_demangle_symbol(arena, STRING("_C99x"), &invalid)) {
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
