#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include "common.h"
#include "../lexer.h"
#include "../parser.h"

static bool path_equal(Path *a, Path *b) {
    if (a->parts.len != b->parts.len)
        return false;

    for (size_t i = 0; i < a->parts.len; i++) {
        AstName *aa = (AstName *)array_at(&a->parts, i);
        AstName *bb = (AstName *)array_at(&b->parts, i);

        if (!ast_name_equal(aa, bb))
            return false;
    }

    return true;
}

static ModuleEntry *module_index_lookup(ModuleIndex *index, Path path) {
    for (size_t i = 0; i < index->entries.len; i++) {
        ModuleEntry *entry = (ModuleEntry *)array_at(&index->entries, i);

        if (path_equal(&entry->path, &path))
            return entry;
    }

    return NULL;
}

static bool module_filename_equal(String a, String b) {
    return a.length == b.length && memcmp(a.data, b.data, a.length) == 0;
}

static void module_entry_add_filename(ModuleEntry *entry, String filename) {
    for (size_t i = 0; i < entry->filenames.len; i++) {
        String *existing = array_at(&entry->filenames, i);
        if (module_filename_equal(*existing, filename))
            return;
    }

    array_push(&entry->filenames, &filename);
}

static int module_filename_compare(const void *left, const void *right) {
    const String *a = left;
    const String *b = right;
    size_t length = a->length < b->length ? a->length : b->length;
    int result = memcmp(a->data, b->data, length);

    if (result != 0)
        return result;

    return (a->length > b->length) - (a->length < b->length);
}

static Symbol *sema_make_namespace_symbol(Sema *sema, Scope *scope, AstName name, Scope *target) {
    Symbol sym = {
        .kind = SYMBOL_NAMESPACE,
        .decl = NULL,
        .name = name,
        .type = NULL,
        .namespace_scope = target,
    };

    scope_insert(scope, &sym);

    return scope_lookup(scope, name);
}

static Scope *sema_namespace_child(Sema *sema, Scope *scope, AstName name) {
    Symbol *symbol = scope_lookup(scope, name);

    if (symbol != NULL) {
        if (symbol->kind != SYMBOL_NAMESPACE)
            return NULL;

        return symbol->namespace_scope;
    }

    Scope *child = arena_calloc(sema->arena, sizeof(Scope));
    *child = sema_make_scope(sema);

    sema_make_namespace_symbol(sema, scope, name, child);
    return child;
}

static bool sema_include_is_wildcard(AstIncludeDecl *include) {
    if (include->alias.parts.len != 1)
        return false;

    AstName *alias = array_at(&include->alias.parts, 0);
    return alias->kind == AST_NAME_IDENT && string_eq(alias->ident, STRING("_"));
}

static bool sema_symbol_is_method(Symbol *symbol) {
    return symbol->kind == SYMBOL_FN && symbol->decl != NULL &&
           symbol->decl->kind == AST_DECL_FN && symbol->decl->fn.receiver != NULL;
}

static bool sema_same_imported_symbol(Symbol *a, Symbol *b) {
    if (a->canonical != NULL && a->canonical == b->canonical)
        return true;

    if (a->decl != NULL && a->decl == b->decl)
        return true;

    return a->kind == SYMBOL_NAMESPACE && b->kind == SYMBOL_NAMESPACE &&
           a->namespace_scope == b->namespace_scope;
}

static bool sema_install_wildcard_include(Sema *sema, Scope *owner, AstIncludeDecl *include, ModuleEntry *entry) {
    Scope *exports = &entry->export_scope;

    for (size_t i = 0; i < exports->syms.len; i++) {
        Symbol *imported = array_at(&exports->syms, i);

        for (size_t j = 0; j < owner->syms.len; j++) {
            Symbol *existing = array_at(&owner->syms, j);

            if (sema_same_imported_symbol(existing, imported))
                continue;

            if (sema_symbol_is_method(existing) || sema_symbol_is_method(imported))
                continue;

            if (ast_name_equal(&existing->name, &imported->name)) {
                error_duplicate_symbol(sema->diags, imported->name.ident, include->span, existing->span);
                return false;
            }
        }
    }

    for (size_t i = 0; i < exports->syms.len; i++) {
        Symbol *imported = array_at(&exports->syms, i);
        bool already_imported = false;

        for (size_t j = 0; j < owner->syms.len; j++) {
            Symbol *existing = array_at(&owner->syms, j);

            if (sema_same_imported_symbol(existing, imported)) {
                already_imported = true;
                break;
            }
        }

        if (!already_imported)
            scope_insert(owner, imported);
    }

    return true;
}

static bool sema_install_include(Sema *sema, Scope *owner, AstIncludeDecl *include, ModuleEntry *entry) {
    if (sema_include_is_wildcard(include))
        return sema_install_wildcard_include(sema, owner, include, entry);

    Path *target = &include->path;

    if (include->alias.parts.len != 0)
        target = &include->alias;

    if (target->parts.len == 0)
        return false;

    Scope *scope = owner;

    for (size_t i = 0; i + 1 < target->parts.len; i++) {
        AstName *part = (AstName *)array_at(&target->parts, i);

        scope = sema_namespace_child(sema, scope, *part);
        if (scope == NULL)
            return false;
    }

    AstName *leaf = (AstName *)array_at(&target->parts, target->parts.len - 1);

    Symbol *existing = scope_lookup(scope, *leaf);
    if (existing != NULL) {
        if (existing->kind == SYMBOL_NAMESPACE &&
            existing->namespace_scope == &entry->export_scope)
            return true;

        return false;
    }

    sema_make_namespace_symbol(sema, scope, *leaf, &entry->export_scope);
    return true;
}

static bool module_read_source(Arena *arena, String filename, Source *source) {
    FILE *file = fopen(string_unmake(arena, filename), "rb");
    if (file == NULL)
        return false;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return false;
    }

    rewind(file);

    char *contents = arena_calloc(arena, (size_t)size + 1);

    size_t read = fread(contents, 1, (size_t)size, file);
    fclose(file);

    if (read != (size_t)size)
        return false;

    contents[size] = '\0';

    *source = (Source) {
        .contents = {
            .data = contents,
            .length = (size_t)size,
        },
        .path = filename,
    };

    source_build_lines(source, arena);
    return true;
}

static bool peek_module_decl(Arena *arena, String filename, Path *path) {
    Source source;
    if (!module_read_source(arena, filename, &source))
        return false;

    Diags diags = {
        .arena = arena,
    };

    array_init(&diags.diags, arena, sizeof(Diag));

    Lexer lexer = {
        .source = &source,
        .diags = &diags,
    };

    Token token = lexer_next(&lexer);
    if (token.type != TK_KW_MODULE)
        return false;

    Path result = {
        .parts = array_create(arena, sizeof(AstName)),
    };

    for (;;) {
        token = lexer_next(&lexer);

        if (token.type != TK_IDENT)
            return false;

        AstName name = {
            .kind = AST_NAME_IDENT,
            .ident = span_to_string(token.span),
        };

        array_push(&result.parts, &name);

        token = lexer_next(&lexer);

        if (token.type == TK_COLON_COLON)
            continue;

        if (token.type == TK_SEMICOLON)
            break;

        return false;
    }

    *path = result;
    return true;
}

static bool module_path_is_coda(String path) {
    static const char suffix[] = ".coda";

    if (path.length < sizeof(suffix) - 1)
        return false;

    return memcmp(path.data + path.length - (sizeof(suffix) - 1), suffix, sizeof(suffix) - 1) == 0;
}

static String module_join_path(Arena *arena, String dir, String name) {
    size_t slash = dir.length != 0 && dir.data[dir.length - 1] != '/';

    String path = {
        .length = dir.length + slash + name.length,
        .data = arena_calloc(arena, dir.length + slash + name.length + 1),
    };

    memcpy(path.data, dir.data, dir.length);

    if (slash)
        path.data[dir.length] = '/';

    memcpy(path.data + dir.length + slash, name.data, name.length);

    path.data[path.length] = '\0';
    return path;
}

static Scope module_make_scope(Arena *arena) {
    return (Scope){
        .syms = array_create(arena, sizeof(Symbol)),
        .defers = array_create(arena, sizeof(HirStmt *)),
        .loop = false,
    };
}

static void module_index_scan_dir(ModuleIndex *index, Arena *arena, String dir) {
    DIR *directory = opendir(dir.data);
    if (directory == NULL)
        return;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        String name = STRING(entry->d_name);
        String filename = module_join_path(arena, dir, name);

        struct stat st;
        if (stat(filename.data, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            module_index_scan_dir(index, arena, filename);
            continue;
        }

        if (!S_ISREG(st.st_mode) || !module_path_is_coda(name))
            continue;

        Path path;
        if (!peek_module_decl(arena, filename, &path))
            continue;

        ModuleEntry *existing = module_index_lookup(index, path);
        if (existing != NULL) {
            module_entry_add_filename(existing, filename);
            continue;
        }

        ModuleEntry module = {
            .path = path,
            .filenames = array_create(arena, sizeof(String)),
            .ast = NULL,
            .scope = module_make_scope(arena),
            .export_scope = module_make_scope(arena),
            .parsed = false,
            .included = false,
            .analysing = false,
            .analysed = false,
            .analysis_failed = false,
        };

        module_entry_add_filename(&module, filename);
        array_push(&index->entries, &module);
    }

    closedir(directory);
}

void module_index_scan(ModuleIndex *index, Arena *arena, Array(String) paths) {
    for (size_t i = 0; i < paths.len; i++) {
        String *path = (String *)array_at(&paths, i);
        module_index_scan_dir(index, arena, *path);
    }
}

static bool module_entry_parse(ModuleEntry *entry, Arena *arena, Diags *diags) {
    if (entry->parsed)
        return true;

    if (entry->filenames.len == 0)
        return false;

    qsort(entry->filenames.data, entry->filenames.len, sizeof(String), module_filename_compare);
    AstModule *module = NULL;

    for (size_t i = 0; i < entry->filenames.len; i++) {
        String filename = *(String *)array_at(&entry->filenames, i);
        Source *source = arena_calloc(arena, sizeof(Source));
        if (!module_read_source(arena, filename, source))
            return false;

        Lexer *lexer = arena_calloc(arena, sizeof(Lexer));
        *lexer = (Lexer) {
            .source = source,
            .diags = diags,
        };

        Parser *parser = arena_calloc(arena, sizeof(Parser));
        parser_init(parser, lexer, arena);

        AstModule *part = parser_parse_module(parser);
        if (part == NULL)
            return false;

        if (module == NULL) {
            module = part;
            continue;
        }

        for (size_t j = 0; j < part->decls.len; j++) {
            AstDecl *decl = *(AstDecl **)array_at(&part->decls, j);
            array_push(&module->decls, &decl);
        }
    }

    if (module == NULL)
        return false;

    entry->ast = module;
    entry->scope = module_make_scope(arena);
    entry->export_scope = module_make_scope(arena);
    entry->parsed = true;

    return true;
}

static bool sema_analyse_module_entry(Sema *sema, ModuleEntry *entry) {
    if (entry->analysed)
        return !entry->analysis_failed;

    if (entry->analysing)
        return false;

    size_t diagnostic_count = sema->diags->diags.len;
    AstModule *previous_module = sema->module;
    Scope previous_global_scope = sema->global_scope;
    Scope *previous_module_scope = sema->module_scope;
    Array previous_scopes = sema->scopes;
    HirFunction *previous_fn = sema->current_fn;

    entry->analysing = true;
    sema->module = entry->ast;
    sema->global_scope = entry->scope;
    sema->module_scope = &entry->scope;
    sema->scopes = array_create(sema->arena, sizeof(Scope));
    sema->current_fn = NULL;

    collect_decls(sema, entry->ast->decls);
    entry->scope = sema->global_scope;

    bool success = sema_resolve_includes(sema, entry->ast);
    entry->scope = sema->global_scope;
    if (success)
        sema_decls(sema, entry->ast->decls);

    entry->scope = sema->global_scope;
    entry->export_scope = module_make_scope(sema->arena);
    for (size_t i = 0; i < entry->scope.syms.len; i++) {
        Symbol *symbol = array_at(&entry->scope.syms, i);
        if (symbol->is_exported)
            scope_insert(&entry->export_scope, symbol);
    }
    entry->analysing = false;
    entry->analysed = true;
    entry->analysis_failed = !success || sema->diags->diags.len != diagnostic_count;

    sema->module = previous_module;
    sema->global_scope = previous_global_scope;
    sema->module_scope = previous_module_scope;
    sema->scopes = previous_scopes;
    sema->current_fn = previous_fn;

    return !entry->analysis_failed;
}

static bool sema_resolve_include(Sema *sema, Scope *owner, AstIncludeDecl *include) {
    ModuleEntry *entry = module_index_lookup(&sema->modules, include->path);

    if (entry == NULL) {
        error_module_not_found(sema->diags, include->path, include->span);
        return false;
    }

    if (!module_entry_parse(entry, sema->arena, sema->diags))
        return false;

    if (entry->analysing) {
        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_MODULE_CYCLE, include->span, STRING("cyclic module imports are not supported"));
        diag_note(&diagnostic, STRING("break the import cycle by moving shared declarations into another module"));
        diag_finish(&diagnostic);
        return false;
    }

    entry->included = true;
    if (!sema_analyse_module_entry(sema, entry))
        return false;

    return sema_install_include(sema, owner, include, entry);
}

static bool sema_type_symbol_matches_origin(Symbol *instance, Symbol *generic) {
    if (instance == NULL || generic == NULL ||
        instance->kind != SYMBOL_TYPE || generic->kind != SYMBOL_TYPE)
        return false;

    if (instance == generic ||
        symbol_canonical(instance) == symbol_canonical(generic))
        return true;

    if (!ast_name_equal(&instance->name, &generic->name))
        return false;

    if (instance->owner_module != NULL || generic->owner_module != NULL)
        return instance->owner_module != NULL &&
               instance->owner_module == generic->owner_module;

    return instance->owner_scope != NULL &&
           instance->owner_scope == generic->owner_scope;
}

Symbol *scope_lookup_fn_decl(Scope *scope, AstFnDecl *decl);;

static Symbol *sema_find_method_in_module(Sema *sema, AstModule *module, Scope *scope, HirType *receiver, AstName name, bool require_export) {
    AstModule *previous_module = sema->module;
    Scope previous_global_scope = sema->global_scope;
    Scope *previous_module_scope = sema->module_scope;
    Array previous_scopes = sema->scopes;

    sema->module = module;
    sema->global_scope = *scope;
    sema->module_scope = scope;
    sema->scopes = array_create(sema->arena, sizeof(Scope));

    Symbol *result = NULL;
    for (size_t i = 0; i < module->decls.len; i++) {
        AstDecl *decl = ((AstDecl **)module->decls.data)[i];
        if (decl == NULL || decl->kind != AST_DECL_FN)
            continue;

        AstFnDecl *candidate = &decl->fn;
        if (candidate->receiver == NULL || !ast_name_equal(&candidate->name, &name))
            continue;

        Symbol *symbol = scope_lookup_fn_decl(scope, candidate);
        if (symbol == NULL || symbol->kind != SYMBOL_FN || (require_export && !symbol->is_exported))
            continue;

        Symbol *generic_receiver = sema_generic_receiver_type(sema, candidate->receiver);
        if (generic_receiver != NULL) {
            Symbol *nominal = receiver != NULL ? receiver->nominal : NULL;
            if (sema_type_symbol_matches_origin(nominal, generic_receiver)) {
                result = symbol;
                break;
            }
            continue;
        }

        bool generic_pattern = false;
        if (sema_generic_method_receiver_matches(sema, candidate, receiver, &generic_pattern)) {
            result = symbol;
            break;
        }

        if (generic_pattern)
            continue;
        
        HirType *candidate_receiver = sema_type(sema, candidate->receiver);
        if (candidate_receiver == NULL || candidate_receiver->kind == HIR_TYPE_ERROR)
            continue;
        if (candidate_receiver->kind == HIR_TYPE_POINTER)
            candidate_receiver = candidate_receiver->pointer.pointee;
        if (candidate_receiver != NULL && type_equal(candidate_receiver, receiver)) {
            result = symbol;
            break;
        }
    }

    sema->module = previous_module;
    sema->global_scope = previous_global_scope;
    sema->module_scope = previous_module_scope;
    sema->scopes = previous_scopes;
    return result;
}

Symbol *sema_find_method(Sema *sema, HirType *receiver, AstName name) {
    if (sema->module != NULL) {
        Scope *scope = sema->module_scope != NULL ? sema->module_scope : &sema->global_scope;
        Symbol *symbol = sema_find_method_in_module(sema, sema->module, scope, receiver, name, false);
        if (symbol != NULL)
            return symbol;
    }

    for (size_t i = 0; i < sema->modules.entries.len; i++) {
        ModuleEntry *entry = array_at(&sema->modules.entries, i);
        if (!entry->included || !entry->analysed || entry->analysis_failed || entry->ast == NULL || entry->ast == sema->module)
            continue;

        Symbol *symbol = sema_find_method_in_module(sema, entry->ast, &entry->scope, receiver, name, true);
        if (symbol != NULL)
            return symbol;
    }

    if (receiver->super != NULL) {
        Symbol *inherited = sema_find_method(sema, receiver->super, name);
        if (inherited != NULL)
            return inherited;
    }

    return NULL;
}

bool sema_resolve_includes(Sema *sema, AstModule *module) {
    for (size_t i = 0; i < module->decls.len; i++) {
        AstDecl *decl = *(AstDecl **)array_at(&module->decls, i);

        if (decl->kind != AST_DECL_INCLUDE)
            continue;

        if (!sema_resolve_include(sema, &sema->global_scope, &decl->include))
            return false;
    }

    return true;
}
