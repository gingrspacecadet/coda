#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

static size_t sema_loop_scope(Sema *sema, size_t level) {
    for (size_t i = sema->scopes.len; i > 0; i--) {
        Scope *scope = (Scope *)array_at(&sema->scopes, i - 1);

        if (!scope->loop)
            continue;

        if (--level == 0)
            return i - 1;
    }

    return SIZE_MAX;
}

//! TODO: hash table / binary lookup
//! TODO: report duplicated symbols
void scope_insert(Scope *scope, Symbol *sym) {
    array_push(&scope->syms, sym);
}

Symbol *scope_lookup(Scope *scope, AstName name) {
    for (size_t i = scope->syms.len; i > 0; --i) {
        Symbol *sym = (Symbol *)array_at(&scope->syms, i - 1);

        if (ast_name_equal(&sym->name, &name))
            return sym;
    }

    return NULL;
}

Symbol *sema_lookup(Sema *sema, AstName name) {
    for (size_t i = sema->scopes.len; i > 0; --i) {
        Scope *scope = (Scope *)array_at(&sema->scopes, i - 1);
        Symbol *symbol = scope_lookup(scope, name);

        if (symbol != NULL)
            return symbol;
    }

    return scope_lookup(&sema->global_scope, name);
}

Symbol *sema_lookup_path(Sema *sema, Path path) {
    if (path.parts.len == 0)
        return NULL;

    AstName *part = (AstName *)array_at(&path.parts, 0);
    Symbol *symbol = sema_lookup(sema, *part);

    if (symbol == NULL)
        return NULL;

    for (size_t i = 1; i < path.parts.len; i++) {
        if (symbol->kind != SYMBOL_NAMESPACE)
            return NULL;

        part = (AstName *)array_at(&path.parts, i);
        symbol = scope_lookup(symbol->namespace_scope, *part);

        if (symbol == NULL)
            return NULL;
    }

    return symbol;
}

//! TODO: comptime stuff!
HirExpr *comp_eval_expr(Sema *sema, HirExpr *expr) {
    return expr;
}

void collect_decls(Sema *sema, Array(AstDecl *) decls) {
    for (size_t i = 0; i < decls.len; i++) {
        AstDecl *d = ((AstDecl **)decls.data)[i];
        Symbol *sym = NULL;

        switch (d->kind) {
            case AST_DECL_TYPE:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_TYPE,
                    .name = d->type.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_FN:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_FN,
                    .name = d->fn.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_VAR:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_GLOBAL,
                    .name = d->var.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_CONSTRAINT:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_CONSTRAINT,
                    .name = d->constraint.name,
                    .decl = d,
                    .span = d->span,
                };
                break;

            case AST_DECL_INCLUDE:
            break;
            
            //! TODO: report internal error
            case AST_DECL_ERROR:
            default:
                break;
        }

        if (sym != NULL) {
            Symbol *s = scope_lookup(&sema->global_scope, sym->name);
            if (s != NULL) {
                error_duplicate_symbol(sema->diags, sym->name.ident, d->span, s->span);
                continue;
            }

            scope_insert(&sema->global_scope, sym);
        }
    }
}

static bool sema_type_equal(HirType *a, HirType *b) {
    if (a == b)
        return true;

    if (a == NULL || b == NULL)
        return false;

    if (a->kind != b->kind)
        return false;

    if (a->mutable != b->mutable)
        return false;

    switch (a->kind) {
        case HIR_TYPE_ERROR:
            return true;

        case HIR_TYPE_BUILTIN:
            return a->builtin == b->builtin;

        case HIR_TYPE_NAMED:
            return a->named.symbol == b->named.symbol;

        case HIR_TYPE_POINTER:
            return a->pointer.optional == b->pointer.optional &&
                   sema_type_equal(a->pointer.pointee,
                                   b->pointer.pointee);

        case HIR_TYPE_SLICE:
            return sema_type_equal(a->slice.element,
                                   b->slice.element);

        case HIR_TYPE_ARRAY:
            return a->array.length == b->array.length &&
                   sema_type_equal(a->array.element,
                                   b->array.element);

        case HIR_TYPE_FUNCTION:
            if (!sema_type_equal(a->function.ret, b->function.ret))
                return false;

            if (a->function.params.len != b->function.params.len)
                return false;

            for (size_t i = 0; i < a->function.params.len; i++) {
                HirType *ap = ((HirType **)a->function.params.data)[i];
                HirType *bp = ((HirType **)b->function.params.data)[i];

                if (!sema_type_equal(ap, bp))
                    return false;
            }

            return true;

        case HIR_TYPE_SUM:
            if (a->sum.members.len != b->sum.members.len)
                return false;

            for (size_t i = 0; i < a->sum.members.len; i++) {
                HirType *am = ((HirType **)a->sum.members.data)[i];
                HirType *bm = ((HirType **)b->sum.members.data)[i];

                if (!sema_type_equal(am, bm))
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

                if (!sema_type_equal(af->type, bf->type))
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

                if (!sema_type_equal(af->type, bf->type))
                    return false;
            }

            return true;

        case HIR_TYPE_ENUM:
            if (!sema_type_equal(a->enumeration.underlying, b->enumeration.underlying))
                return false;

            if (a->enumeration.items.len != b->enumeration.items.len)
                return false;

            for (size_t i = 0; i < a->enumeration.items.len; i++) {
                HirEnumItem *ai = ((HirEnumItem *)a->enumeration.items.data) + i;
                HirEnumItem *bi = ((HirEnumItem *)b->enumeration.items.data) + i;

                if (ai->symbol != bi->symbol)
                    return false;

                if (ai->value != bi->value)
                    return false;
            }

            return true;
    }

    return false;
}

HirExpr *sema_expr(Sema *sema, AstExpr *ast, HirType *expected);
HirType *sema_type(Sema *sema, AstType *ast) {
    HirType *hir = arena_alloc(sema->arena, sizeof(HirType));

    hir->mutable = ast->mutable;

    switch (ast->kind) {
        //! TODO: builtin types!
        case AST_TYPE_NAMED: {
            Symbol *symbol = sema_lookup_path(sema, ast->named.path);

            if (symbol == NULL) {
                error_unknown_type(sema->diags, ast->named.path, ast->span);
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            if (symbol->kind != SYMBOL_TYPE) {
                AstName *name = (AstName *)array_at(&ast->named.path.parts, ast->named.path.parts.len - 1);
                error_expected_type_symbol(sema->diags, name->ident, ast->span);
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            if (symbol->type == NULL) {
                //! TODO: malformed type symbol
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            if (symbol->type->kind == HIR_TYPE_BUILTIN)
                return symbol->type;

            hir->kind = HIR_TYPE_NAMED;
            hir->named.symbol = symbol;
            break;
        }

        case AST_TYPE_POINTER:
            hir->kind = HIR_TYPE_POINTER;
            hir->pointer.pointee = sema_type(sema, ast->pointer.pointee);
            hir->pointer.optional = ast->pointer.optional;
            break;

        case AST_TYPE_ARRAY: {
            HirType *element = sema_type(sema, ast->array.element);

            if (!ast->array.sized) {
                hir->kind = HIR_TYPE_SLICE;
                hir->slice.element = element;
                break;
            }

            HirExpr *length = sema_expr(sema, ast->array.length, NULL);
            length = comp_eval_expr(sema, length);

            if (length->kind != HIR_EXPR_LITERAL) {
                //! TODO: expected comptime integer
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            hir->kind = HIR_TYPE_ARRAY;
            hir->array.element = element;
            //! TODO: turn literals into integer
            // hir->array.length = length->literal.integer;
            break;
        }

        case AST_TYPE_FN:
            hir->kind = HIR_TYPE_FUNCTION;
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

        case AST_TYPE_STRUCT:
            hir->kind = HIR_TYPE_STRUCT;
            hir->structure.fields = array_create(sema->arena, sizeof(HirField));

            for (size_t i = 0; i < ast->structure.fields.len; i++) {
                AstField *field = ((AstField *)ast->structure.fields.data) + i;
                Symbol *symbol = arena_alloc(sema->arena, sizeof(Symbol));

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

                array_push(&hir->structure.fields, &hir_field);
            }

            break;

        case AST_TYPE_UNION:
            hir->kind = HIR_TYPE_UNION;
            hir->union_.fields = array_create(sema->arena, sizeof(HirField));

            for (size_t i = 0; i < ast->union_.fields.len; i++) {
                AstField *field = ((AstField *)ast->union_.fields.data) + i;
                Symbol *symbol = arena_alloc(sema->arena, sizeof(Symbol));

                *symbol = (Symbol) {
                    .kind = SYMBOL_FIELD,
                    .name = field->name,
                    .decl = NULL,
                    .span = ast->span
                };

                symbol->type = sema_type(sema, field->type);

                HirField hir_field = {
                    .symbol = symbol,
                    .type = symbol->type,
                };

                array_push(&hir->union_.fields, &hir_field);
            }

            break;

        case AST_TYPE_ENUM:
            hir->kind = HIR_TYPE_ENUM;
            hir->enumeration.underlying =
                sema_type(sema, ast->enumeration.underlying);

            hir->enumeration.items =
                array_create(sema->arena, sizeof(HirEnumItem));

            for (size_t i = 0; i < ast->enumeration.items.len; i++) {
                AstEnumItem *item =
                    ((AstEnumItem *)ast->enumeration.items.data) + i;

                //! TODO: resolve enum item name/value
                HirEnumItem hir_item = {
                };

                array_push(&hir->enumeration.items, &hir_item);
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

static bool sema_literal_fits(Sema *sema, HirLiteral *literal, HirType *type) {
    if (literal->kind == HIR_LITERAL_NULL)
        return type->kind == HIR_TYPE_POINTER &&
               type->pointer.optional;

    if (type->kind != HIR_TYPE_BUILTIN)
        return false;

    switch (literal->kind) {
        case HIR_LITERAL_INTEGER:
            //! TODO: integer range check
            return true;

        case HIR_LITERAL_FLOAT:
            //! TODO: float range / precision check
            return true;

        case HIR_LITERAL_BOOL:
            return type->builtin == BUILTIN_BOOL;

        case HIR_LITERAL_STRING:
            //! TODO: string literal conversion
            return false;

        case HIR_LITERAL_NULL:
            return false;

        case HIR_LITERAL_ERROR:
            return false;
    }

    return false;
}

HirExpr *sema_coerce(Sema *sema, HirExpr *expr, HirType *type) {
    if (expr == NULL || type == NULL)
        return expr;

    if (expr->kind == HIR_EXPR_ERROR)
        return expr;

    if (expr->type != NULL) {
        if (sema_type_equal(expr->type, type))
            return expr;

        error_type_mismatch(sema->diags, type, expr->type, expr->span);

        return NULL;
    }

    if (expr->kind != HIR_EXPR_LITERAL) {
        //! TODO: expression has no type
        return NULL;
    }

    if (!sema_literal_fits(sema, &expr->literal, type)) {
        //! TODO: cannot coerce literal to type
        return NULL;
    }

    expr->type = type;
    return expr;
}

static HirExpr *sema_expr_coerce(Sema *sema, HirExpr *expr, HirType *expected) {
    if (expr == NULL)
        return NULL;

    if (expected == NULL)
        return expr;

    return sema_coerce(sema, expr, expected);
}

static HirType *sema_binary_operand_type(Sema *sema, AstBinaryOp op, HirExpr *left, HirExpr *right, HirType *expected) {
    if (left->type != NULL && right->type != NULL) {
        if (!sema_type_equal(left->type, right->type)) {
            //! TODO: implicit conversion
            return NULL;
        }

        return left->type;
    }

    if (left->type != NULL)
        return left->type;

    if (right->type != NULL)
        return right->type;

    return expected;
}

static HirType *sema_binary_result_type(Sema *sema, AstBinaryOp op, HirType *operand) {
    switch (op) {
        //! TODO: comparisons return bool
        //! TODO: logical operators return bool
        //! TODO: other operators

        default:
            return operand;
    }
}

static HirType *sema_call_type(Sema *sema, HirExpr *callee) {
    if (callee->type == NULL)
        return NULL;

    if (callee->type->kind != HIR_TYPE_FUNCTION)
        return NULL;

    return callee->type;
}

static HirField *sema_field_lookup(Array(HirField) fields, AstName name) {
    for (size_t i = 0; i < fields.len; i++) {
        HirField *field = ((HirField *)fields.data) + i;

        if (field->symbol == NULL)
            continue;

        if (ast_name_equal(&field->symbol->name, &name))
            return field;
    }

    return NULL;
}

static HirExpr *sema_implicit_deref(Sema *sema, HirExpr *expr) {
    if (expr->type == NULL || expr->type->kind != HIR_TYPE_POINTER)
        return expr;

    HirExpr *deref = arena_alloc(sema->arena, sizeof(HirExpr));

    *deref = (HirExpr) {
        .span = expr->span,
        .kind = HIR_EXPR_UNARY,
        .type = expr->type->pointer.pointee,
        .unary = {
            .op = AST_UNARY_DEREF,
            .operand = expr,
        },
    };

    return deref;
}

static HirType *sema_unary_type(Sema *sema, AstUnaryOp op, HirExpr *operand) {
    if (operand == NULL || operand->type == NULL)
        return NULL;

    switch (op) {
        case AST_UNARY_POS:
        case AST_UNARY_NEG:
        case AST_UNARY_BIT_NOT:
            return operand->type;

        case AST_UNARY_NOT:
            //! TODO: require bool
            return operand->type;

        case AST_UNARY_DEREF:
            if (operand->type->kind != HIR_TYPE_POINTER)
                return NULL;

            return operand->type->pointer.pointee;

        case AST_UNARY_ADDRESS:
            //! TODO: construct pointer type
            return NULL;
    }

    return NULL;
}

static HirField *sema_init_field_lookup(HirType *type, AstName name) {
    if (type == NULL)
        return NULL;

    switch (type->kind) {
        case HIR_TYPE_STRUCT:
            return sema_field_lookup(type->structure.fields, name);

        case HIR_TYPE_UNION:
            return sema_field_lookup(type->union_.fields, name);

        default:
            return NULL;
    }
}

static void sema_insert_parameter(Sema *sema, AstParam *param, HirType *type) {
    Symbol *symbol = arena_alloc(sema->arena, sizeof(Symbol));

    *symbol = (Symbol) {
        .kind = SYMBOL_PARAMETER,
        .name = param->name,
        .decl = NULL,
        .type = type,
        .span = param->span,
    };

    scope_insert((Scope *)array_at(&sema->scopes, sema->scopes.len - 1), symbol);
}

static String sema_literal_compact(Sema *sema, String raw) {
    char *data = arena_alloc(sema->arena, raw.length + 1);
    size_t len = 0;

    for (size_t i = 0; i < raw.length; i++) {
        if (raw.data[i] != '_')
            data[len++] = raw.data[i];
    }

    data[len] = '\0';

    return (String){
        .data = data,
        .length = len,
    };
}

static uint64_t sema_parse_integer(String raw) {
    size_t i = 0;
    int base = 10;

    if (raw.length >= 2 && raw.data[0] == '0') {
        if (raw.data[1] == 'x' || raw.data[1] == 'X') {
            base = 16;
            i = 2;
        } else if (raw.data[1] == 'b' || raw.data[1] == 'B') {
            base = 2;
            i = 2;
        } else if (raw.length > 1 && raw.data[1] >= '0' && raw.data[1] <= '7') {
            base = 8;
            i = 1;
        }
    }

    uint64_t value = 0;

    for (; i < raw.length; i++) {
        char c = raw.data[i];
        uint64_t digit;

        if (c >= '0' && c <= '9')
            digit = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (uint64_t)(c - 'A' + 10);
        else
            break;

        assert(digit < (uint64_t)base);
        value = value * (uint64_t)base + digit;
    }

    return value;
}

static uint32_t sema_hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return (uint32_t)(c - '0');

    if (c >= 'a' && c <= 'f')
        return (uint32_t)(c - 'a' + 10);

    if (c >= 'A' && c <= 'F')
        return (uint32_t)(c - 'A' + 10);

    assert(!"invalid hexadecimal digit");
    return 0;
}

static uint32_t sema_decode_escape(String raw, size_t *index) {
    assert(*index < raw.length);

    char c = raw.data[(*index)++];

    switch (c) {
        case '\\': return '\\';
        case '"': return '"';
        case '\'': return '\'';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '0': return '\0';

        case 'x':
            assert(*index + 2 <= raw.length);

            uint32_t value = (sema_hex_digit(raw.data[*index]) << 4) | sema_hex_digit(raw.data[*index + 1]);
            *index += 2;
            return value;
    }

    assert(!"invalid escape sequence");
    return 0;
}

static uint32_t sema_decode_utf8(String raw, size_t *index) {
    assert(*index < raw.length);

    uint8_t c0 = (uint8_t)raw.data[(*index)++];

    if (c0 < 0x80)
        return c0;

    if ((c0 & 0xe0) == 0xc0) {
        assert(*index < raw.length);
        uint8_t c1 = (uint8_t)raw.data[(*index)++];
        return ((uint32_t)(c0 & 0x1f) << 6) | (uint32_t)(c1 & 0x3f);
    }

    if ((c0 & 0xf0) == 0xe0) {
        assert(*index + 1 < raw.length);
        uint8_t c1 = (uint8_t)raw.data[(*index)++];
        uint8_t c2 = (uint8_t)raw.data[(*index)++];
        return ((uint32_t)(c0 & 0x0f) << 12) | ((uint32_t)(c1 & 0x3f) << 6) | (uint32_t)(c2 & 0x3f);
    }

    assert((c0 & 0xf8) == 0xf0);
    assert(*index + 2 < raw.length);

    uint8_t c1 = (uint8_t)raw.data[(*index)++];
    uint8_t c2 = (uint8_t)raw.data[(*index)++];
    uint8_t c3 = (uint8_t)raw.data[(*index)++];

    return ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(c1 & 0x3f) << 12) | ((uint32_t)(c2 & 0x3f) << 6) | (uint32_t)(c3 & 0x3f);
}

static uint64_t sema_decode_char(String raw) {
    size_t start = raw.length > 0 && raw.data[0] == '\'' ? 1 : 0;
    size_t end = raw.length > 1 && raw.data[raw.length - 1] == '\'' ? raw.length - 1 : raw.length;
    size_t index = start;

    assert(index < end);

    uint32_t value;

    if (raw.data[index] == '\\') {
        index++;
        value = sema_decode_escape((String){
            .data = raw.data,
            .length = end
        }, &index);
    } else {
        String content = {
            .data = raw.data,
            .length = end
        };
        value = sema_decode_utf8(content, &index);
    }

    assert(index == end);
    return value;
}

static String sema_decode_string(Sema *sema, String raw) {
    size_t start = raw.length > 0 && raw.data[0] == '"' ? 1 : 0;
    size_t end = raw.length > 1 && raw.data[raw.length - 1] == '"' ? raw.length - 1 : raw.length;

    char *data = arena_alloc(sema->arena, end - start + 1);
    size_t out = 0;
    size_t index = start;

    while (index < end) {
        if (raw.data[index] == '\\') {
            index++;
            String content = {
                .data = raw.data,
                .length = end
            };

            uint32_t value = sema_decode_escape(content, &index);
            assert(value <= 0xff);
            data[out++] = (char)value;
            continue;
        }

        data[out++] = raw.data[index++];
    }

    data[out] = '\0';

    return (String){
        .data = data,
        .length = out,
    };
}

static HirLiteral sema_literal(Sema *sema, AstLiteral literal) {
    switch (literal.kind) {
        case AST_LIT_INTEGER: {
            String raw = sema_literal_compact(sema, literal.raw);

            return (HirLiteral){
                .kind = HIR_LITERAL_INTEGER,
                .integer = sema_parse_integer(raw),
            };
        }

        case AST_LIT_FLOAT: {
            String raw = sema_literal_compact(sema, literal.raw);

            return (HirLiteral){
                .kind = HIR_LITERAL_FLOAT,
                .floating = strtod(raw.data, NULL),
            };
        }

        case AST_LIT_BOOL:
            return (HirLiteral){
                .kind = HIR_LITERAL_BOOL,
                .boolean = literal.raw.length == 4 && memcmp(literal.raw.data, "true", 4) == 0,
            };

        case AST_LIT_CHAR:
            return (HirLiteral){
                .kind = HIR_LITERAL_INTEGER,
                .integer = sema_decode_char(literal.raw),
            };

        case AST_LIT_STRING:
            return (HirLiteral){
                .kind = HIR_LITERAL_STRING,
                .string = sema_decode_string(sema, literal.raw),
            };

        case AST_LIT_NULL:
            return (HirLiteral){
                .kind = HIR_LITERAL_NULL,
            };
    }

    assert(!"unhandled AST literal");
    return (HirLiteral){0};
}

HirExpr *sema_expr(Sema *sema, AstExpr *ast, HirType *expected) {
    HirExpr *hir = arena_alloc(sema->arena, sizeof(HirExpr));

    hir->span = ast->span;
    hir->type = NULL;

    switch (ast->kind) {
        case AST_EXPR_LITERAL:
            hir->kind = HIR_EXPR_LITERAL;
            hir->literal = sema_literal(sema, ast->literal);
            if (expected != NULL)
                hir = sema_expr_coerce(sema, hir, expected);
            break;

        case AST_EXPR_IDENT: {
            Symbol *symbol = sema_lookup(sema, ast->ident);

            if (symbol == NULL) {
                error_unknown_name(sema->diags, ast->ident.ident, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (symbol->kind == SYMBOL_NAMESPACE) {
                error_namespace_value(sema->diags, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->kind = HIR_EXPR_VALUE;
            hir->value.symbol = symbol;
            hir->type = sema_symbol_type(sema, symbol);

            if (expected != NULL)
                return sema_expr_coerce(sema, hir, expected);

            return hir;
        }

        case AST_EXPR_PATH: {
            Symbol *symbol = sema_lookup_path(sema, ast->path);

            if (symbol == NULL) {
                error_unknown_path(sema->diags, ast->path, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (symbol->kind == SYMBOL_NAMESPACE) {
                error_namespace_value(sema->diags, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->kind = HIR_EXPR_VALUE;
            hir->value.symbol = symbol;
            hir->type = sema_symbol_type(sema, symbol);

            if (expected != NULL)
                return sema_expr_coerce(sema, hir, expected);

            return hir;
        }

        case AST_EXPR_UNARY: {
            hir->kind = HIR_EXPR_UNARY;
            hir->unary.op = ast->unary.op;

            HirExpr *operand = sema_expr(sema, ast->unary.operand, NULL);

            if (operand == NULL || operand->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            HirType *type = sema_unary_type(sema, hir->unary.op, operand);

            if (type == NULL) {
                error_invalid_unary_operation(sema->diags, unary_op_name(hir->unary.op), operand->type, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->unary.operand = operand;
            hir->type = type;

            if (expected != NULL)
                return sema_expr_coerce(sema, hir, expected);

            return hir;
        }

        case AST_EXPR_BINARY: {
            hir->kind = HIR_EXPR_BINARY;
            hir->binary.op = ast->binary.op;

            HirExpr *left = sema_expr(sema, ast->binary.left, NULL);

            HirExpr *right = sema_expr(sema, ast->binary.right, NULL);

            HirType *operand_type = sema_binary_operand_type(sema, ast->binary.op, left, right, expected);

            if (operand_type == NULL) {
                error_type_mismatch(sema->diags, expected, left->type != NULL ? left->type : right->type, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (left->type == NULL)
                left = sema_coerce(sema, left, operand_type);

            if (right->type == NULL)
                right = sema_coerce(sema, right, operand_type);

            if (left == NULL || right == NULL) {
                error_invalid_binary_operation(sema->diags, binary_op_name(ast->binary.op), left ? left->type : NULL, right ? right->type : NULL, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->binary.left = left;
            hir->binary.right = right;
            hir->type = sema_binary_result_type(sema, ast->binary.op, operand_type);

            if (hir->type == NULL) {
                error_invalid_binary_operation(sema->diags, binary_op_name(ast->binary.op), left->type, right->type, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            return hir;
        }

        case AST_EXPR_CALL: {
            hir->kind = HIR_EXPR_CALL;
            hir->call.function = NULL;
            hir->call.args = array_create(sema->arena, sizeof(HirExpr *));

            HirExpr *callee = sema_expr(sema, ast->call.callee, NULL);

            if (callee == NULL || callee->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            HirType *function_type = sema_call_type(sema, callee);

            if (function_type == NULL) {
                error_expected_function(sema->diags, callee->type, ast->call.callee->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (function_type->function.params.len != ast->call.args.len) {
                error_wrong_argument_count(sema->diags, function_type->function.params.len, ast->call.args.len, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (callee->kind != HIR_EXPR_VALUE || callee->value.symbol->kind != SYMBOL_FN) {
                //! TODO: function pointers / callable values
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->call.function = callee->value.symbol;

            for (size_t i = 0; i < ast->call.args.len; i++) {
                AstExpr *arg = ((AstExpr **)ast->call.args.data)[i];
                HirType *param_type = ((HirType **)function_type->function.params.data)[i];
                HirExpr *value = sema_expr(sema, arg, param_type);

                if (value == NULL || value->kind == HIR_EXPR_ERROR) {
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }

                array_push(&hir->call.args, &value);
            }

            hir->type = function_type->function.ret;

            if (expected != NULL)
                hir = sema_expr_coerce(sema, hir, expected);

            return hir;
        }

        case AST_EXPR_INDEX: {
            hir->kind = HIR_EXPR_INDEX;
            hir->index.object = sema_expr(sema, ast->index.object, NULL);
            hir->index.index = sema_expr(sema, ast->index.index, NULL);

            if (hir->index.object == NULL || hir->index.index == NULL) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            HirExpr *object = sema_implicit_deref(sema, hir->index.object);
            hir->index.object = object;

            HirType *object_type = object->type;

            if (object_type == NULL) {
                //! TODO: expected indexable type
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            switch (object_type->kind) {
                case HIR_TYPE_ARRAY:
                    hir->type = object_type->array.element;
                    break;

                case HIR_TYPE_SLICE:
                    hir->type = object_type->slice.element;
                    break;

                default:
                    //! TODO: expected array or slice
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
            }

            //! TODO: require integer index

            if (expected != NULL)
                return sema_expr_coerce(sema, hir, expected);

            return hir;
        }

        case AST_EXPR_MEMBER: {
            hir->kind = HIR_EXPR_FIELD;
            hir->field.object = sema_expr(sema, ast->member.object, NULL);
            hir->field.field = NULL;

            if (hir->field.object == NULL || hir->field.object->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            HirExpr *object = hir->field.object;

            if (object->type == NULL) {
                //! TODO: expected member-bearing type
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (object->type->kind == HIR_TYPE_POINTER) {
                if (object->type->pointer.pointee == NULL) {
                    //! TODO: malformed pointer type
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }

                object = arena_alloc(sema->arena, sizeof(HirExpr));

                *object = (HirExpr) {
                    .span = hir->field.object->span,
                    .kind = HIR_EXPR_UNARY,
                    .type = hir->field.object->type->pointer.pointee,
                    .unary = {
                        .op = AST_UNARY_DEREF,
                        .operand = hir->field.object,
                    },
                };

                hir->field.object = object;
            }

            HirType *object_type = object->type;
            HirField *field = NULL;

            switch (object_type->kind) {
                case HIR_TYPE_STRUCT:
                    field = sema_field_lookup(object_type->structure.fields, ast->member.member);
                    break;

                case HIR_TYPE_UNION:
                    field = sema_field_lookup(object_type->union_.fields, ast->member.member);
                    break;

                default:
                    //! TODO: expected struct or union
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
            }

            if (field == NULL) {
                error_unknown_field(sema->diags, ast->member.member.ident, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->field.field = field;
            hir->type = field->type;

            if (expected != NULL)
                return sema_expr_coerce(sema, hir, expected);

            return hir;
        }

        case AST_EXPR_CAST:
            hir->kind = HIR_EXPR_CAST;
            hir->cast.type = sema_type(sema, ast->cast.type);
            hir->cast.operand = sema_expr(sema, ast->cast.operand, hir->cast.type);
            hir->type = hir->cast.type;

            //! TODO: validate cast
            break;

        case AST_EXPR_INTRINSIC:
            //! TODO: resolve intrinsic
            hir->kind = HIR_EXPR_ERROR;
            break;

        case AST_EXPR_BUBBLE:
            hir->kind = HIR_EXPR_ERROR;
            //! TODO: lower bubble expression
            break;

        case AST_EXPR_INIT: {
            hir->kind = HIR_EXPR_INIT;
            hir->init.fields = array_create(sema->arena, sizeof(HirInitField));

            if (expected == NULL) {
                //! TODO: infer initializer type
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            switch (expected->kind) {
                case HIR_TYPE_STRUCT:
                case HIR_TYPE_UNION:
                    break;

                default:
                    //! TODO: expected struct or union
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
            }

            for (size_t i = 0; i < ast->init.fields.len; i++) {
                AstInitField *field = ((AstInitField *)ast->init.fields.data) + i;
                HirField *hir_field = sema_init_field_lookup(expected, field->name);

                if (hir_field == NULL) {
                    //! TODO: unknown field diagnostic
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }

                HirExpr *value = sema_expr(sema, field->value, hir_field->type);

                if (value == NULL || value->kind == HIR_EXPR_ERROR) {
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }

                HirInitField init_field = {
                    .field = hir_field,
                    .value = value,
                };

                array_push(&hir->init.fields, &init_field);
            }

            hir->type = expected;
            break;
        }

        case AST_EXPR_LAMBDA: {
            hir->kind = HIR_EXPR_LAMBDA;

            HirType *type = arena_alloc(sema->arena, sizeof(HirType));

            *type = (HirType) {
                .kind = HIR_TYPE_FUNCTION,
                .mutable = false,
                .function = {
                    .ret = sema_type(sema, ast->lambda.ret),
                    .params = array_create(sema->arena, sizeof(HirType *)),
                },
            };

            if (type->function.ret->kind == HIR_TYPE_ERROR) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            for (size_t i = 0; i < ast->lambda.params.len; i++) {
                AstParam *param = (AstParam *)array_at(&ast->lambda.params, i);

                HirType *param_type = sema_type(sema, param->type);

                if (param_type->kind == HIR_TYPE_ERROR) {
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }

                array_push(&type->function.params, &param_type);
            }

            if (expected != NULL) {
                if (expected->kind != HIR_TYPE_FUNCTION || !sema_type_equal(type, expected)) {
                    error_type_mismatch(sema->diags, expected, type, ast->span);
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }
            }

            HirFunction *fn = arena_alloc(sema->arena, sizeof(HirFunction));

            *fn = (HirFunction) {
                .symbol = NULL,
                .return_type = type->function.ret,
            };

            HirFunction *previous_fn = sema->current_fn;
            sema->current_fn = fn;

            sema_push_scope(sema);

            for (size_t i = 0; i < ast->lambda.params.len; i++) {
                AstParam *param = (AstParam *)array_at(&ast->lambda.params, i);
                HirType *param_type = *(HirType **)array_at(&type->function.params, i);

                sema_insert_parameter(sema, param, param_type);
            }

            fn->body = sema_stmt(sema, ast->lambda.body);

            sema_pop_scope(sema);
            sema->current_fn = previous_fn;

            hir->type = type;

            //! TODO: store fn in lambda HIR
            return hir;
        }

        case AST_EXPR_SPLICE:
            //! TODO: comptime
            hir->kind = HIR_EXPR_ERROR;
            break;

        default:
            hir->kind = HIR_EXPR_ERROR;
            //! TODO: internal compiler error
            break;
    }

    return hir;
}

static bool sema_local_decl(Sema *sema, AstVarDecl *ast, HirStmt **init_stmt) {
    Scope *scope = (Scope *)array_at(&sema->scopes, sema->scopes.len - 1);

    Symbol *s = scope_lookup(scope, ast->name);
    if (s != NULL) {
        error_duplicate_symbol(sema->diags, ast->name.ident, ast->span, s->span);
        return false;
    }

    Symbol *existing = sema_lookup(sema, ast->name);

    if (existing != NULL) {
        error_shadowing(sema->diags, ast->name.ident, ast->span, existing->span);
        return false;
    }

    HirType *type = sema_type(sema, ast->type);

    if (type == NULL || type->kind == HIR_TYPE_ERROR)
        return false;

    HirExpr *init = NULL;

    if (ast->init != NULL) {
        init = sema_expr(sema, ast->init, type);

        if (init == NULL || init->kind == HIR_EXPR_ERROR)
            return false;
    }

    Symbol *symbol = arena_alloc(sema->arena, sizeof(Symbol));
    *symbol = (Symbol) {
        .kind = SYMBOL_LOCAL,
        .decl = NULL,
        .type = type,
        .span = ast->span,
        .namespace_scope = NULL,
        .name = ast->name,
    };

    scope_insert(scope, symbol);

    HirLocal local = {
        .symbol = symbol,
        .type = type,
    };

    array_push(&sema->current_fn->locals, &local);

    *init_stmt = NULL;

    if (init != NULL) {
        HirExpr *target = arena_alloc(sema->arena, sizeof(HirExpr));
        *target = (HirExpr) {
            .span = ast->span,
            .kind = HIR_EXPR_VALUE,
            .type = type,
            .value = {
                .symbol = symbol,
            },
        };

        HirStmt *stmt = arena_alloc(sema->arena, sizeof(HirStmt));
        *stmt = (HirStmt) {
            .span = ast->span,
            .kind = HIR_STMT_ASSIGN,
            .assign = {
                .target = target,
                .value = init,
            },
        };

        *init_stmt = stmt;
    }

    return true;
}

static HirStmt *sema_hir_stmt(Sema *sema, HirStmtKind kind, Span span) {
    HirStmt *stmt = arena_alloc(sema->arena, sizeof(HirStmt));

    stmt->span = span;
    stmt->kind = kind;

    return stmt;
}

static HirExpr *sema_hir_bool(Sema *sema, bool value) {
    HirExpr *expr = arena_alloc(sema->arena, sizeof(HirExpr));

    expr->span = (Span) { 0 };
    expr->kind = HIR_EXPR_LITERAL;
    expr->type = NULL;
    expr->literal = (HirLiteral) {
        .kind = HIR_LITERAL_BOOL,
        .boolean = value,
    };

    return expr;
}

static HirStmt *sema_lower_defer_exit(Sema *sema, HirStmt *exit, size_t scope) {
    size_t count = 0;

    for (size_t i = sema->scopes.len; i > scope; i--) {
        Scope *current = (Scope *)array_at(&sema->scopes, i - 1);
        count += current->defers.len;
    }

    if (count == 0)
        return exit;

    HirStmt *block = sema_hir_stmt(sema, HIR_STMT_BLOCK, exit->span);
    block->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

    for (size_t i = sema->scopes.len; i > scope; i--) {
        Scope *current = (Scope *)array_at(&sema->scopes, i - 1);

        for (size_t j = current->defers.len; j > 0; j--) {
            HirStmt *deferred =
                ((HirStmt **)current->defers.data)[j - 1];

            array_push(&block->block.stmts, &deferred);
        }
    }

    array_push(&block->block.stmts, &exit);
    return block;
}

static bool hir_stmt_terminates(HirStmt *stmt) {
    if (stmt == NULL)
        return false;

    switch (stmt->kind) {
        case HIR_STMT_RETURN:
        case HIR_STMT_BREAK:
        case HIR_STMT_CONTINUE:
            return true;

        case HIR_STMT_BLOCK:
            if (stmt->block.stmts.len == 0)
                return false;

            return hir_stmt_terminates(((HirStmt **)stmt->block.stmts.data)[stmt->block.stmts.len - 1]);

        case HIR_STMT_IF:
            return stmt->_if._else != NULL && hir_stmt_terminates(stmt->_if.then) && hir_stmt_terminates(stmt->_if._else);

        default:
            return false;
    }
}

HirStmt *sema_stmt(Sema *sema, AstStmt *ast);
static bool sema_block(Sema *sema, AstStmt *ast, HirStmt *hir) {
    hir->kind = HIR_STMT_BLOCK;
    hir->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

    Scope *scope = sema_push_scope(sema);

    for (size_t i = 0; i < ast->block.stmts.len; i++) {
        AstStmt *stmt = ((AstStmt **)ast->block.stmts.data)[i];

        if (stmt->kind == AST_STMT_VAR) {
            HirStmt *init = NULL;

            if (!sema_local_decl(sema, stmt->var, &init)) {
                sema_pop_scope(sema);
                return false;
            }

            if (init != NULL)
                array_push(&hir->block.stmts, &init);

            continue;
        }

        if (stmt->kind == AST_STMT_DEFER) {
            HirStmt *deferred = sema_stmt(sema, stmt->defer.deferred);

            if (deferred == NULL || deferred->kind == HIR_STMT_ERROR) {
                sema_pop_scope(sema);
                return false;
            }

            array_push(&scope->defers, &deferred);
            continue;
        }

        HirStmt *value = sema_stmt(sema, stmt);

        if (value == NULL)
            continue;

        if (value->kind == HIR_STMT_ERROR) {
            sema_pop_scope(sema);
            return false;
        }

        array_push(&hir->block.stmts, &value);

        if (hir_stmt_terminates(value))
            break;
    }

    if (hir->block.stmts.len == 0 || !hir_stmt_terminates(((HirStmt **)hir->block.stmts.data)[hir->block.stmts.len - 1])) {
        for (size_t i = scope->defers.len; i > 0; i--) {
            HirStmt *deferred = ((HirStmt **)scope->defers.data)[i - 1];

            array_push(&hir->block.stmts, &deferred);
        }
    }

    sema_pop_scope(sema);
    return true;
}

static bool sema_expr_uint(HirExpr *expr, size_t *value) {
    if (expr == NULL || expr->kind != HIR_EXPR_LITERAL)
        return false;

    if (expr->literal.kind != HIR_LITERAL_INTEGER)
        return false;

    //! TODO: parse integer literal
    return false;
}

HirStmt *sema_stmt(Sema *sema, AstStmt *ast) {
    HirStmt *hir = arena_alloc(sema->arena, sizeof(HirStmt));

    hir->span = ast->span;

    switch (ast->kind) {
        case AST_STMT_VAR: {
            HirStmt *init = NULL;

            if (!sema_local_decl(sema, ast->var, &init)) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            return init;
        }

        case AST_STMT_EXPR:
            hir->kind = HIR_STMT_EXPR;
            hir->expr = sema_expr(sema, ast->expr, NULL);

            if (hir->expr == NULL || hir->expr->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            break;

        case AST_STMT_BLOCK:
            if (!sema_block(sema, ast, hir)) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            return hir;

        case AST_STMT_RETURN:
            hir->kind = HIR_STMT_RETURN;
            hir->_return.value = NULL;

            if (sema->current_fn == NULL) {
                error_invalid_return(sema->diags, NULL, NULL, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            HirType *return_type = sema->current_fn->return_type;
            bool returns_none =
                return_type != NULL &&
                return_type->kind == HIR_TYPE_BUILTIN &&
                return_type->builtin == BUILTIN_NONE;

            if (ast->_return.value == NULL) {
                if (!returns_none) {
                    error_missing_return_value(sema->diags, return_type, ast->span);
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }

                return hir;
            }

            HirExpr *value = sema_expr(sema, ast->_return.value, NULL);

            if (value == NULL || value->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            if (returns_none) {
                error_invalid_return(sema->diags, return_type, value->type, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            value = sema_coerce(sema, value, return_type);

            if (value == NULL || value->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->_return.value = value;
            return hir;

        case AST_STMT_IF: {
            hir->kind = HIR_STMT_IF;
            hir->_if.cond = sema_expr(sema, ast->_if.cond, NULL);

            if (hir->_if.cond == NULL || hir->_if.cond->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->_if.then = sema_stmt(sema, ast->_if.then);

            if (hir->_if.then == NULL || hir->_if.then->kind == HIR_STMT_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->_if._else = NULL;

            if (ast->_if._else != NULL) {
                hir->_if._else = sema_stmt(sema, ast->_if._else);

                if (hir->_if._else == NULL || hir->_if._else->kind == HIR_STMT_ERROR) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }
            }

            //! TODO: require bool condition

            return hir;
        }

        case AST_STMT_WHILE: {
            hir->kind = HIR_STMT_WHILE;
            hir->_while.cond = sema_expr(sema, ast->_while.cond, NULL);

            if (hir->_while.cond == NULL || hir->_while.cond->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            sema_push_scope(sema)->loop = true;

            hir->_while.body = sema_stmt(sema, ast->_while.body);

            sema_pop_scope(sema);

            if (hir->_while.body == NULL || hir->_while.body->kind == HIR_STMT_ERROR) {
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            //! TODO: require bool condition

            return hir;
        }

        case AST_STMT_BREAK: {
            size_t level = 1;

            if (ast->_break.value != NULL) {
                HirExpr *expr = sema_expr(sema, ast->_break.value, NULL);

                if (expr == NULL || expr->kind == HIR_EXPR_ERROR) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }

                expr = comp_eval_expr(sema, expr);

                //! TODO: require comptime integer
                //! TODO: extract integer

                if (expr->kind != HIR_EXPR_LITERAL) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }
            }

            size_t loop_scope = sema_loop_scope(sema, level);

            if (loop_scope == SIZE_MAX) {
                error_invalid_break(sema->diags, level, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->kind = HIR_STMT_BREAK;
            hir->_break.level = level;

            return sema_lower_defer_exit(sema, hir, loop_scope);
        }

        case AST_STMT_CONTINUE: {
            size_t level = 1;

            if (ast->_continue.value != NULL) {
                HirExpr *expr = sema_expr(sema, ast->_continue.value, NULL);

                if (expr == NULL || expr->kind == HIR_EXPR_ERROR) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }

                expr = comp_eval_expr(sema, expr);

                //! TODO: require comptime integer
                //! TODO: extract integer

                if (expr->kind != HIR_EXPR_LITERAL) {
                    hir->kind = HIR_STMT_ERROR;
                    return hir;
                }
            }

            size_t loop_scope = sema_loop_scope(sema, level);

            if (loop_scope == SIZE_MAX) {
                error_invalid_continue(sema->diags, level, ast->span);
                hir->kind = HIR_STMT_ERROR;
                return hir;
            }

            hir->kind = HIR_STMT_CONTINUE;
            hir->_continue.level = level;

            return sema_lower_defer_exit(sema, hir, loop_scope);
        }

        case AST_STMT_FOR: {
            HirStmt *block = sema_hir_stmt(sema, HIR_STMT_BLOCK, ast->span);
            block->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

            sema_push_scope(sema)->loop = true;

            if (ast->_for.init != NULL) {
                HirStmt *init = sema_stmt(sema, ast->_for.init);

                if (init != NULL) {
                    if (init->kind == HIR_STMT_ERROR) {
                        sema_pop_scope(sema);
                        return init;
                    }

                    array_push(&block->block.stmts, &init);
                }
            }

            HirStmt *loop = sema_hir_stmt(sema, HIR_STMT_WHILE, ast->span);

            if (ast->_for.cond != NULL)
                loop->_while.cond = sema_expr(sema, ast->_for.cond, NULL);
            else
                loop->_while.cond = sema_hir_bool(sema, true);

            if (loop->_while.cond == NULL ||
                loop->_while.cond->kind == HIR_EXPR_ERROR) {
                sema_pop_scope(sema);
                loop->kind = HIR_STMT_ERROR;
                return loop;
            }

            HirStmt *body = sema_stmt(sema, ast->_for.body);

            if (body == NULL || body->kind == HIR_STMT_ERROR) {
                sema_pop_scope(sema);
                loop->kind = HIR_STMT_ERROR;
                return loop;
            }

            HirStmt *loop_body = sema_hir_stmt(sema, HIR_STMT_BLOCK, ast->_for.body->span);
            loop_body->block.stmts = array_create(sema->arena, sizeof(HirStmt *));

            array_push(&loop_body->block.stmts, &body);

            if (ast->_for.post != NULL) {
                HirStmt *post = sema_hir_stmt(sema, HIR_STMT_EXPR, ast->_for.post->span);
                post->expr = sema_expr(sema, ast->_for.post, NULL);

                if (post->expr == NULL || post->expr->kind == HIR_EXPR_ERROR) {
                    sema_pop_scope(sema);
                    loop->kind = HIR_STMT_ERROR;
                    return loop;
                }

                array_push(&loop_body->block.stmts, &post);
            }

            loop->_while.body = loop_body;

            array_push(&block->block.stmts, &loop);

            sema_pop_scope(sema);

            return block;
        }

        case AST_STMT_MATCH:
            //! TODO: lower match
            hir->kind = HIR_STMT_ERROR;
            break;

        case AST_STMT_DEFER:
            //! TODO: lower defer
            hir->kind = HIR_STMT_ERROR;
            break;

        case AST_STMT_ERROR:
        default:
            hir->kind = HIR_STMT_ERROR;
            //! TODO: internal compiler error
            break;
    }

    return hir;
}

void sema_fn_decl(Sema *sema, AstFnDecl *ast) {
    Symbol *symbol = sema_lookup(sema, ast->name);

    if (symbol == NULL) {
        //! TODO: internal compiler error
        return;
    }

    symbol->span = ast->span;

    if (symbol->kind != SYMBOL_FN) {
        //! TODO: internal compiler error
        return;
    }

    if (symbol->type == NULL) {
        HirType *type = arena_alloc(sema->arena, sizeof(HirType));

        *type = (HirType) {
            .kind = HIR_TYPE_FUNCTION,
            .mutable = false,
            .function = {
                .ret = sema_type(sema, ast->ret),
                .params = array_create(sema->arena, sizeof(HirType *)),
            },
        };

        for (size_t i = 0; i < ast->params.len; i++) {
            AstParam *param = (AstParam *)array_at(&ast->params, i);
            HirType *param_type = sema_type(sema, param->type);

            array_push(&type->function.params, &param_type);
        }

        symbol->type = type;
    }

    if (ast->body == NULL)
        return;

    HirFunction *fn = arena_alloc(sema->arena, sizeof(HirFunction));

    *fn = (HirFunction) {
        .symbol = symbol,
        .return_type = symbol->type->function.ret,
        .locals = array_create(sema->arena, sizeof(HirLocal)),
    };

    sema_push_scope(sema);

    HirType *type = symbol->type;

    for (size_t i = 0; i < ast->params.len; i++) {
        AstParam *param = (AstParam *)array_at(&ast->params, i);
        HirType *param_type = *(HirType **)array_at(&type->function.params, i);

        sema_insert_parameter(sema, param, param_type);
    }

    HirFunction *previous_fn = sema->current_fn;
    sema->current_fn = fn;

    //! TODO: analyse attributes

    fn->body = sema_stmt(sema, ast->body);

    if (fn->body == NULL || fn->body->kind == HIR_STMT_ERROR) {
        sema->current_fn = previous_fn;
        sema_pop_scope(sema);
        return;
    }

    HirType *return_type = fn->return_type;
    bool returns_none =
        return_type != NULL &&
        return_type->kind == HIR_TYPE_BUILTIN &&
        return_type->builtin == BUILTIN_NONE;

    if (!returns_none && !hir_stmt_terminates(fn->body))
        error_missing_return_value(sema->diags, return_type, ast->body->span);

    sema->current_fn = previous_fn;
    sema_pop_scope(sema);

    array_push(&sema->hir_module->functions, fn);
}

void sema_type_decl(Sema *sema, AstTypeDecl *ast) {
    Symbol *symbol = sema_lookup(sema, ast->name);
    symbol->span = ast->span;

    if (symbol == NULL) {
        //! TODO: internal compiler error
        return;
    }

    if (symbol->kind != SYMBOL_TYPE) {
        //! TODO: internal compiler error
        return;
    }

    symbol->type = sema_type(sema, ast->type);
}

bool sema_var_decl(Sema *sema, AstVarDecl *ast) {
    Symbol *symbol = sema_lookup(sema, ast->name);

    if (symbol == NULL || symbol->kind != SYMBOL_GLOBAL)
        return false;

    HirType *type = sema_type(sema, ast->type);

    if (type == NULL || type->kind == HIR_TYPE_ERROR)
        return false;

    if (symbol->type == NULL || symbol->type->kind == HIR_TYPE_ERROR)
        return false;

    HirExpr *init = NULL;

    if (ast->init != NULL) {
        init = sema_expr(sema, ast->init, type);

        if (init == NULL || init->kind == HIR_EXPR_ERROR)
            return false;
    }

    symbol->type = type;

    HirGlobal global = {
        .symbol = symbol,
        .type = type,
        .init = init,
        .is_export = false,
    };

    array_push(&sema->hir_module->globals, &global);

    return true;
}

void sema_constraint_decl(Sema *sema, AstConstraintDecl *ast) {}
void sema_include_decl(Sema *sema, AstIncludeDecl *ast) {}

void sema_decl(Sema *sema, AstDecl *ast) {
    switch (ast->kind) {
        case AST_DECL_FN:
            sema_fn_decl(sema, &ast->fn);
            break;

        case AST_DECL_TYPE:
            sema_type_decl(sema, &ast->type);
            break;

        case AST_DECL_VAR:
            sema_var_decl(sema, &ast->var);
            break;

        case AST_DECL_CONSTRAINT:
            sema_constraint_decl(sema, &ast->constraint);
            break;

        case AST_DECL_INCLUDE:
            sema_include_decl(sema, &ast->include);
            break;

        //! TODO: internal compiler error

        case AST_DECL_ERROR:
        default:
            break;
    }
}

static void sema_insert_builtin_type(Sema *sema, String name, BuiltinType builtin) {
    Symbol *symbol = arena_alloc(sema->arena, sizeof(Symbol));
    HirType *type = arena_alloc(sema->arena, sizeof(HirType));

    *type = (HirType) {
        .kind = HIR_TYPE_BUILTIN,
        .mutable = false,
        .builtin = builtin,
    };

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
    sema_insert_builtin_type(sema, STRING("int8"), BUILTIN_INT8);
    sema_insert_builtin_type(sema, STRING("int16"), BUILTIN_INT16);
    sema_insert_builtin_type(sema, STRING("int32"), BUILTIN_INT32);
    sema_insert_builtin_type(sema, STRING("int64"), BUILTIN_INT64);

    sema_insert_builtin_type(sema, STRING("uint8"), BUILTIN_UINT8);
    sema_insert_builtin_type(sema, STRING("uint16"), BUILTIN_UINT16);
    sema_insert_builtin_type(sema, STRING("uint32"), BUILTIN_UINT32);
    sema_insert_builtin_type(sema, STRING("uint64"), BUILTIN_UINT64);

    sema_insert_builtin_type(sema, STRING("bool"), BUILTIN_BOOL);

    sema_insert_builtin_type(sema, STRING("none"), BUILTIN_NONE);
}