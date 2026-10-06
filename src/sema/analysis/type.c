#include <assert.h>
#include "common.h"

HirType *type_with_mutability(Sema *sema, HirType *type, bool mutable) {
    HirType *copy = arena_alloc(sema->arena, sizeof(HirType));
    *copy = *type;
    copy->mutable = mutable;
    return copy;
}

HirType *builtin_type(Sema *sema, BuiltinType builtin) {
    for (size_t i = 0; i < sema->global_scope.syms.len; i++) {
        Symbol *symbol = (Symbol *)array_at(&sema->global_scope.syms, i);

        if (symbol->kind == SYMBOL_TYPE &&
            symbol->type != NULL &&
            symbol->type->kind == HIR_TYPE_BUILTIN &&
            symbol->type->builtin == builtin)
            return symbol->type;
    }

    return NULL;
}

bool type_equal(HirType *a, HirType *b) {
    if (a == b)
        return true;

    if (a == NULL || b == NULL)
        return false;

    if (a->kind != b->kind)
        return false;
        
    if (a->nominal != NULL || b->nominal != NULL)
        return a->nominal == b->nominal;

    switch (a->kind) {
        case HIR_TYPE_ERROR:
            return true;

        case HIR_TYPE_BUILTIN:
            return a->builtin == b->builtin;

        case HIR_TYPE_POINTER:
            return a->pointer.optional == b->pointer.optional &&
                   type_equal(a->pointer.pointee,
                                   b->pointer.pointee);

        case HIR_TYPE_SLICE:
            return type_equal(a->slice.element,
                                   b->slice.element);

        case HIR_TYPE_ARRAY:
            return a->array.length == b->array.length &&
                   type_equal(a->array.element,
                                   b->array.element);

        case HIR_TYPE_FUNCTION:
            if (!type_equal(a->function.ret, b->function.ret))
                return false;

            if (a->function.params.len != b->function.params.len)
                return false;

            for (size_t i = 0; i < a->function.params.len; i++) {
                HirType *ap = ((HirType **)a->function.params.data)[i];
                HirType *bp = ((HirType **)b->function.params.data)[i];

                if (!type_equal(ap, bp))
                    return false;
            }

            return true;

        case HIR_TYPE_SUM:
            if (a->sum.members.len != b->sum.members.len)
                return false;

            for (size_t i = 0; i < a->sum.members.len; i++) {
                HirType *am = ((HirType **)a->sum.members.data)[i];
                HirType *bm = ((HirType **)b->sum.members.data)[i];

                if (!type_equal(am, bm))
                    return false;
            }

            return true;

        case HIR_TYPE_STRUCT:
            if (a->structure.fields.len != b->structure.fields.len)
                return false;

            for (size_t i = 0; i < a->structure.fields.len; i++) {
                HirField *af = ((HirField *)a->structure.fields.data) + i;
                HirField *bf = ((HirField *)b->structure.fields.data) + i;

                if (af->symbol != bf->symbol)
                    return false;

                if (!type_equal(af->type, bf->type))
                    return false;
            }

            return true;

        case HIR_TYPE_UNION:
            if (a->union_.fields.len != b->union_.fields.len)
                return false;

            for (size_t i = 0; i < a->union_.fields.len; i++) {
                HirField *af = ((HirField *)a->union_.fields.data) + i;
                HirField *bf = ((HirField *)b->union_.fields.data) + i;

                if (af->symbol != bf->symbol)
                    return false;

                if (!type_equal(af->type, bf->type))
                    return false;
            }

            return true;

        case HIR_TYPE_ENUM:
            if (!type_equal(a->_enum.underlying, b->_enum.underlying))
                return false;

            if (a->_enum.items.len != b->_enum.items.len)
                return false;

            for (size_t i = 0; i < a->_enum.items.len; i++) {
                HirEnumItem *ai = ((HirEnumItem *)a->_enum.items.data) + i;
                HirEnumItem *bi = ((HirEnumItem *)b->_enum.items.data) + i;

                if (ai->symbol != bi->symbol)
                    return false;

                if (ai->value != bi->value)
                    return false;
            }

            return true;
    }

    return false;
}

static size_t align_up(size_t value, size_t align) {
    assert(align != 0);
    size_t remainder = value % align;
    return remainder == 0 ? value : value + align - remainder;
}

HirType *sema_type(Sema *sema, AstType *ast) {
    HirType *hir = arena_alloc(sema->arena, sizeof(HirType));

    hir->mutable = ast->mutable;

    switch (ast->kind) {
        case AST_TYPE_NAMED: {
            Symbol *symbol = sema_lookup_path(sema, ast->named.path);

            if (symbol == NULL) {
                error_unknown_type(sema->diags, ast->named.path, ast->span);
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            if (symbol->kind != SYMBOL_TYPE) {
                AstName *name = array_at(&ast->named.path.parts, ast->named.path.parts.len - 1);
                error_expected_type_symbol(sema->diags, name->ident, ast->span);
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            if (symbol->type == NULL) {
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            *hir = *symbol->type;
            hir->mutable = ast->mutable;
            return hir;
        }

        case AST_TYPE_POINTER:
            hir->kind = HIR_TYPE_POINTER;
            hir->pointer.pointee = sema_type(sema, ast->pointer.pointee);
            hir->pointer.optional = ast->pointer.optional;
            hir->size = sema->pointer_size;
            hir->align = sema->pointer_size;
            break;

        case AST_TYPE_ARRAY: {
            HirType *element = sema_type(sema, ast->array.element);

            if (!ast->array.sized) {
                hir->kind = HIR_TYPE_SLICE;
                hir->slice.element = element;
                hir->size = sema->pointer_size * 2;
                hir->align = sema->pointer_size;
                break;
            }

            HirExpr *length = sema_expr(sema, ast->array.length, NULL);
            CompContext context = {
                .sema = sema,
                .frame = NULL
            };
            length = comp_eval_expr(&context, length);

            if (length->kind != HIR_EXPR_LITERAL) {
                //! TODO: expected comptime integer
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            hir->kind = HIR_TYPE_ARRAY;
            hir->array.element = element;
            hir->array.length = (size_t)length->literal.integer;
            assert(element->size == 0 || hir->array.length <= SIZE_MAX / element->size);
            hir->size = element->size * hir->array.length;
            hir->align = element->align;
            break;
        }

        case AST_TYPE_FN:
            hir->kind = HIR_TYPE_FUNCTION;
            hir->size = 0;
            hir->align = 0;
            hir->function.ret = sema_type(sema, ast->fn.ret);
            hir->function.params = array_create(sema->arena, sizeof(HirType *));

            for (size_t i = 0; i < ast->fn.params.len; i++) {
                AstType *param = ((AstType **)ast->fn.params.data)[i];

                HirType *type = sema_type(sema, param);
                array_push(&hir->function.params, &type);
            }
            break;

        case AST_TYPE_SUM:
            hir->kind = HIR_TYPE_SUM;
            hir->sum.members = array_create(sema->arena, sizeof(HirType *));

            for (size_t i = 0; i < ast->sum.members.len; i++) {
                AstType *member = ((AstType **)ast->sum.members.data)[i];

                HirType *type = sema_type(sema, member);
                array_push(&hir->sum.members, &type);
            }
            break;

        case AST_TYPE_STRUCT: {
            hir->kind = HIR_TYPE_STRUCT;
            hir->structure.fields = array_create(sema->arena, sizeof(HirField));

            size_t offset = 0;
            size_t align = 1;

            for (size_t i = 0; i < ast->structure.fields.len; i++) {
                AstField *field = array_at(&ast->structure.fields, i);
                Symbol *symbol = arena_alloc(sema->arena, sizeof(*symbol));
                *symbol = (Symbol) {
                    .kind = SYMBOL_FIELD,
                    .name = field->name,
                    .decl = NULL,
                    .span = ast->span,
                };
                symbol->type = sema_type(sema, field->type);

                HirField hir_field = {
                    .symbol = symbol,
                    .type = symbol->type,
                };

                offset = align_up(offset, hir_field.type->align);
                hir_field.offset = offset;
                offset += hir_field.type->size;

                if (hir_field.type->align > align)
                    align = hir_field.type->align;

                array_push(&hir->structure.fields, &hir_field);
            }

            hir->align = align;
            hir->size = align_up(offset, align);
            break;
        }

        case AST_TYPE_UNION: {
            hir->kind = HIR_TYPE_UNION;
            hir->union_.fields = array_create(sema->arena, sizeof(HirField));

            size_t size = 0;
            size_t align = 1;

            for (size_t i = 0; i < ast->union_.fields.len; i++) {
                AstField *field = array_at(&ast->union_.fields, i);
                Symbol *symbol = arena_alloc(sema->arena, sizeof(*symbol));
                *symbol = (Symbol) {
                    .kind = SYMBOL_FIELD,
                    .name = field->name,
                    .decl = NULL,
                    .span = ast->span,
                };
                symbol->type = sema_type(sema, field->type);

                HirField hir_field = {
                    .symbol = symbol,
                    .type = symbol->type,
                    .offset = 0,
                };

                if (hir_field.type->size > size)
                    size = hir_field.type->size;
                if (hir_field.type->align > align)
                    align = hir_field.type->align;

                array_push(&hir->union_.fields, &hir_field);
            }

            hir->align = align;
            hir->size = align_up(size, align);
            break;
        }

        case AST_TYPE_ENUM:
            hir->kind = HIR_TYPE_ENUM;
            hir->_enum.underlying = sema_type(sema, ast->_enum.underlying);
            hir->size = hir->_enum.underlying->size;
            hir->align = hir->_enum.underlying->align;

            hir->_enum.items = array_create(sema->arena, sizeof(HirEnumItem));

            for (size_t i = 0; i < ast->_enum.items.len; i++) {
                AstEnumItem *item = ((AstEnumItem *)ast->_enum.items.data) + i;

                //! TODO: resolve enum item name/value
                HirEnumItem hir_item = {
                };

                array_push(&hir->_enum.items, &hir_item);
            }
            break;

        // this can't be resolved until comptime eval
        case AST_TYPE_SPLICE:
            hir->kind = HIR_TYPE_ERROR;
            //! TODO: mark/defer comptime type resolution
            break;

        default:
            hir->kind = HIR_TYPE_ERROR;
            //! TODO: internal compiler error
            fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);
            break;
    }

    return hir;
}

HirType *sema_symbol_type(Sema *sema, Symbol *symbol) {
    if (symbol->type != NULL)
        return symbol->type;

    switch (symbol->kind) {
        case SYMBOL_GLOBAL:
            if (symbol->type == NULL)
                symbol->type = sema_type(sema, symbol->decl->var.type);

            return symbol->type;

        case SYMBOL_FN: {
            AstFnDecl *fn = &symbol->decl->fn;
            HirType *type = arena_alloc(sema->arena, sizeof(HirType));

            type->kind = HIR_TYPE_FUNCTION;
            type->mutable = false;
            type->function.ret = sema_type(sema, fn->ret);
            type->function.params = array_create(sema->arena, sizeof(HirType *));

            for (size_t i = 0; i < fn->params.len; i++) {
                AstParam *param = ((AstParam *)fn->params.data) + i;

                HirType *param_type = sema_type(sema, param->type);
                array_push(&type->function.params, &param_type);
            }

            symbol->type = type;
            return type;
        }

        case SYMBOL_TYPE:
        case SYMBOL_LOCAL:
        case SYMBOL_PARAMETER:
        case SYMBOL_FIELD:
        case SYMBOL_ENUM_ITEM:
        case SYMBOL_CONSTRAINT:
        case SYMBOL_ERROR:
            return NULL;
    }

    return NULL;
}

static void builtin_layout(BuiltinType builtin, size_t *size, size_t *align) {
    switch (builtin) {
        case BUILTIN_UINT8:
        case BUILTIN_INT8:
        case BUILTIN_BOOL:
            *size = 1;
            *align = 1;
            return;
        case BUILTIN_UINT16:
        case BUILTIN_INT16:
            *size = 2;
            *align = 2;
            return;
        case BUILTIN_UINT32:
        case BUILTIN_INT32:
            *size = 4;
            *align = 4;
            return;
        case BUILTIN_UINT64:
        case BUILTIN_INT64:
            *size = 8;
            *align = 8;
            return;
        case BUILTIN_NONE:
            *size = 0;
            *align = 1;
            return;
    }
    assert(!"unhandled builtin type");
}

static void insert_builtin_type(Sema *sema, String name, BuiltinType builtin) {
    Symbol *symbol = arena_alloc(sema->arena, sizeof(*symbol));
    HirType *type = arena_alloc(sema->arena, sizeof(*type));
    *type = (HirType) {
        .kind = HIR_TYPE_BUILTIN,
        .mutable = false,
        .builtin = builtin,
    };
    builtin_layout(builtin, &type->size, &type->align);
    *symbol = (Symbol) {
        .kind = SYMBOL_TYPE,
        .name = (AstName) {
            .kind = AST_NAME_IDENT,
            .ident = name,
        },
        .decl = NULL,
        .type = type,
    };
    scope_insert(&sema->global_scope, symbol);
}

void sema_insert_builtin_types(Sema *sema) {
    insert_builtin_type(sema, STRING("int8"), BUILTIN_INT8);
    insert_builtin_type(sema, STRING("int16"), BUILTIN_INT16);
    insert_builtin_type(sema, STRING("int32"), BUILTIN_INT32);
    insert_builtin_type(sema, STRING("int64"), BUILTIN_INT64);

    insert_builtin_type(sema, STRING("uint8"), BUILTIN_UINT8);
    insert_builtin_type(sema, STRING("uint16"), BUILTIN_UINT16);
    insert_builtin_type(sema, STRING("uint32"), BUILTIN_UINT32);
    insert_builtin_type(sema, STRING("uint64"), BUILTIN_UINT64);

    insert_builtin_type(sema, STRING("bool"), BUILTIN_BOOL);

    insert_builtin_type(sema, STRING("none"), BUILTIN_NONE);
}