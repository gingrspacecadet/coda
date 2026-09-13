#include <sys/stat.h>
#include <dirent.h>
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

    Scope *child = arena_alloc(sema->arena, sizeof(Scope));
    *child = sema_make_scope(sema);

    sema_make_namespace_symbol(sema, scope, name, child);
    return child;
}

static bool sema_install_include(Sema *sema, Scope *owner, AstIncludeDecl *include, ModuleEntry *entry) {
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
    if (existing != NULL)
        return false;

    sema_make_namespace_symbol(sema, scope, *leaf, &entry->scope);
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

    char *contents = arena_alloc(arena, (size_t)size + 1);

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
        .data = arena_alloc(arena, dir.length + slash + name.length + 1),
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

        ModuleEntry module = {
            .path = path,
            .filename = filename,
            .ast = NULL,
            .scope = module_make_scope(arena),
            .parsed = false,
        };

        if (module_index_lookup(index, module.path) != NULL) {
            // TODO: duplicate module diagnostic
            continue;
        }

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

    Source *source = arena_alloc(arena, sizeof(Source));

    if (!module_read_source(arena, entry->filename, source))
        return false;

    Lexer *lexer = arena_alloc(arena, sizeof(Lexer));
    *lexer = (Lexer) {
        .source = source,
        .diags = diags,
    };

    Parser *parser = arena_alloc(arena, sizeof(Parser));
    parser_init(parser, lexer, arena);

    AstModule *module = parser_parse_module(parser);
    if (module == NULL)
        return false;

    entry->ast = module;
    entry->scope = module_make_scope(arena);
    entry->parsed = true;

    return true;
}

static bool sema_resolve_include(Sema *sema, Scope *owner, AstIncludeDecl *include) {
    ModuleEntry *entry = module_index_lookup(&sema->modules, include->path);

    if (entry == NULL) {
        // TODO: diagnostic
        return false;
    }

    if (!module_entry_parse(entry, sema->arena, sema->diags))
        return false;

    return sema_install_include(sema, owner, include, entry);
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
