#include <assert.h>
#include "common.h"

static bool literal_fits(Sema *sema, HirLiteral *literal, HirType *type) {
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
        bool equal = type_equal(expr->type, type);

        if (equal)
            return expr;

        error_type_mismatch(sema->diags, type, expr->type, expr->span);
        return NULL;
    }

    if (expr->kind != HIR_EXPR_LITERAL) {
        //! TODO: expression has no type
        return NULL;
    }

    if (!literal_fits(sema, &expr->literal, type)) {
        error_type_mismatch(sema->diags, type, expr->type ? expr->type : builtin_type(sema, BUILTIN_UINT32), expr->span);
        return NULL;
    }

    expr->type = type;
    return expr;
}

static HirExpr *expr_coerce(Sema *sema, HirExpr *expr, HirType *expected) {
    if (expr == NULL)
        return NULL;

    if (expected == NULL)
        return expr;

    return sema_coerce(sema, expr, expected);
}

static bool binop_is_comparison(AstBinaryOp op) {
    switch (op) {
        case AST_BINARY_EQUAL:
        case AST_BINARY_NOT_EQUAL:
        case AST_BINARY_LT:
        case AST_BINARY_LTE:
        case AST_BINARY_GT:
        case AST_BINARY_GTE:
            return true;

        default:
            return false;
    }
}

static HirType *binop_type(Sema *sema, AstBinaryOp op, HirExpr *left, HirExpr *right, HirType *expected) {
    if (left->type != NULL && right->type != NULL) {
        if (binop_is_comparison(op) && is_pointer(left->type) && is_pointer(right->type)) {
            if (!type_equal(left->type->pointer.pointee, right->type->pointer.pointee))
                return NULL;

            return left->type;
        }

        if ((op == AST_BINARY_ADD || op == AST_BINARY_SUB) && is_pointer(left->type) && is_integer(right->type))
            return left->type;

        if (op == AST_BINARY_ADD && is_integer(left->type) && is_pointer(right->type))
            return right->type;

        if (op == AST_BINARY_SUB && is_pointer(left->type) && is_pointer(right->type)) {
            if (!type_equal(left->type->pointer.pointee, right->type->pointer.pointee))
                return NULL;

            return builtin_type(sema, BUILTIN_INT64);
        }

        if (!type_equal(left->type, right->type)) {
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

static HirType *binary_result_type(Sema *sema, AstBinaryOp op, HirType *left, HirType *right, HirType *operand) {
    if (op == AST_BINARY_SUB && is_pointer(left) && is_pointer(right))
        return builtin_type(sema, BUILTIN_INT64);

    switch (op) {
        case AST_BINARY_LT:
        case AST_BINARY_LTE:
        case AST_BINARY_GT:
        case AST_BINARY_GTE:
        case AST_BINARY_EQUAL:
        case AST_BINARY_NOT_EQUAL:
        case AST_BINARY_LOGICAL_AND:
        case AST_BINARY_LOGICAL_OR:
            return builtin_type(sema, BUILTIN_BOOL);

        default:
            return operand;
    }
}

static HirType *call_type(Sema *sema, HirExpr *callee) {
    if (callee->type == NULL)
        return NULL;

    if (callee->type->kind != HIR_TYPE_FUNCTION)
        return NULL;

    return callee->type;
}

static HirField *field_lookup(Array(HirField) fields, AstName name) {
    for (size_t i = 0; i < fields.len; i++) {
        HirField *field = ((HirField *)fields.data) + i;

        if (field->symbol == NULL)
            continue;

        if (ast_name_equal(&field->symbol->name, &name))
            return field;
    }

    return NULL;
}

static HirExpr *implicit_deref(Sema *sema, HirExpr *expr) {
    if (expr->type == NULL || expr->type->kind != HIR_TYPE_POINTER)
        return expr;

    HirExpr *deref = arena_calloc(sema->arena, sizeof(HirExpr));

    *deref = (HirExpr) {
        .span = expr->span,
        .kind = HIR_EXPR_UNARY,
        .type = type_with_mutability(sema, expr->type->pointer.pointee, expr->type->pointer.pointee->mutable),
        .unary = {
            .op = AST_UNARY_DEREF,
            .operand = expr,
        },
    };

    return deref;
}

static inline bool is_place(HirExpr *expr) {
    if (expr == NULL)
        return false;

    switch (expr->kind) {
        case HIR_EXPR_VALUE:
            return expr->value.symbol != NULL &&
                   (expr->value.symbol->kind == SYMBOL_LOCAL ||
                    expr->value.symbol->kind == SYMBOL_PARAMETER ||
                    expr->value.symbol->kind == SYMBOL_GLOBAL);

        case HIR_EXPR_FIELD:
        case HIR_EXPR_INDEX:
            return true;

        case HIR_EXPR_UNARY:
            return expr->unary.op == AST_UNARY_DEREF;

        default:
            return false;
    }
}

static HirType *unary_type(Sema *sema, AstUnaryOp op, HirExpr *operand) {
    if (operand == NULL || operand->type == NULL)
        return NULL;

    switch (op) {
        case AST_UNARY_POS:
        case AST_UNARY_NEG:
        case AST_UNARY_BIT_NOT:
            return operand->type;

        case AST_UNARY_NOT:
            if (operand->type->kind != HIR_TYPE_BUILTIN ||
                operand->type->builtin != BUILTIN_BOOL)
                return NULL;

            return operand->type;

        case AST_UNARY_DEREF:
            if (operand->type->kind != HIR_TYPE_POINTER)
                return NULL;

            return operand->type->pointer.pointee;

        case AST_UNARY_ADDRESS:
            if (!is_place(operand))
                return NULL;

            return pointer_type(sema, operand->type, false);
    }

    return NULL;
}

static HirField *init_field_lookup(HirType *type, AstName name) {
    if (type == NULL)
        return NULL;

    switch (type->kind) {
        case HIR_TYPE_STRUCT:
            return field_lookup(type->structure.fields, name);

        case HIR_TYPE_UNION:
            return field_lookup(type->union_.fields, name);

        case HIR_TYPE_SLICE:
            return field_lookup(type->slice.fields, name);

        default:
            return NULL;
    }
}

static String literal_compact(Sema *sema, String raw) {
    char *data = arena_calloc(sema->arena, raw.length + 1);
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

static uint64_t parse_integer(String raw) {
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

static uint32_t hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return (uint32_t)(c - '0');

    if (c >= 'a' && c <= 'f')
        return (uint32_t)(c - 'a' + 10);

    if (c >= 'A' && c <= 'F')
        return (uint32_t)(c - 'A' + 10);

    assert(!"invalid hexadecimal digit");
    return 0;
}

static uint32_t decode_escape(String raw, size_t *index) {
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

            uint32_t value = (hex_digit(raw.data[*index]) << 4) | hex_digit(raw.data[*index + 1]);
            *index += 2;
            return value;
    }

    assert(!"invalid escape sequence");
    return 0;
}

static uint32_t decode_utf8(String raw, size_t *index) {
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

static uint64_t decode_char(String raw) {
    size_t start = raw.length > 0 && raw.data[0] == '\'' ? 1 : 0;
    size_t end = raw.length > 1 && raw.data[raw.length - 1] == '\'' ? raw.length - 1 : raw.length;
    size_t index = start;

    assert(index < end);

    uint32_t value;

    if (raw.data[index] == '\\') {
        index++;
        value = decode_escape((String){
            .data = raw.data,
            .length = end
        }, &index);
    } else {
        String content = {
            .data = raw.data,
            .length = end
        };
        value = decode_utf8(content, &index);
    }

    assert(index == end);
    return value;
}

static String decode_string(Sema *sema, String raw) {
    size_t start = raw.length > 0 && raw.data[0] == '"' ? 1 : 0;
    size_t end = raw.length > 1 && raw.data[raw.length - 1] == '"' ? raw.length - 1 : raw.length;

    char *data = arena_calloc(sema->arena, end - start + 1);
    size_t out = 0;
    size_t index = start;

    while (index < end) {
        if (raw.data[index] == '\\') {
            index++;
            String content = {
                .data = raw.data,
                .length = end
            };

            uint32_t value = decode_escape(content, &index);
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

static HirLiteral literal(Sema *sema, AstLiteral literal) {
    switch (literal.kind) {
        case AST_LIT_INTEGER: {
            String raw = literal_compact(sema, literal.raw);

            return (HirLiteral){
                .kind = HIR_LITERAL_INTEGER,
                .integer = parse_integer(raw),
            };
        }

        case AST_LIT_FLOAT: {
            String raw = literal_compact(sema, literal.raw);

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
                .integer = decode_char(literal.raw),
            };

        case AST_LIT_STRING:
            return (HirLiteral){
                .kind = HIR_LITERAL_STRING,
                .string = decode_string(sema, literal.raw),
            };

        case AST_LIT_NULL:
            return (HirLiteral){
                .kind = HIR_LITERAL_NULL,
            };
    }

    assert(!"unhandled AST literal");
    return (HirLiteral){0};
}

static size_t align_up(size_t value, size_t align) {
    assert(align != 0);
    size_t remainder = value % align;
    return remainder == 0 ? value : value + align - remainder;
}

static HirExpr *sema_method_error(Sema *sema, Span span) {
    HirExpr *expr = arena_calloc(sema->arena, sizeof(*expr));
    *expr = (HirExpr){.span = span, .kind = HIR_EXPR_ERROR};
    return expr;
}

static HirExpr *sema_method_call(Sema *sema, AstExpr *ast, HirType *expected, bool *handled) {
    *handled = false;
    if (ast->call.callee == NULL || ast->call.callee->kind != AST_EXPR_MEMBER)
        return NULL;

    AstExpr *callee = ast->call.callee;
    if (sema->module == NULL)
        return NULL;

    HirExpr *receiver = sema_expr(sema, callee->member.object, NULL);
    if (receiver == NULL || receiver->kind == HIR_EXPR_ERROR) {
        *handled = true;
        return sema_method_error(sema, ast->span);
    }

    HirType *receiver_type = receiver->type;
    if (receiver_type == NULL) {
        *handled = true;
        return sema_method_error(sema, ast->span);
    }

    HirType *dispatch_type = receiver_type;
    if (dispatch_type->kind == HIR_TYPE_POINTER)
        dispatch_type = dispatch_type->pointer.pointee;
    if (dispatch_type == NULL)
        return NULL;

    Symbol *method = sema_find_method(sema, dispatch_type, callee->member.member);
    if (method == NULL)
        return NULL;

    *handled = true;
    HirType *function_type = sema_symbol_type(sema, method);
    if (function_type == NULL || function_type->kind != HIR_TYPE_FUNCTION || function_type->function.params.len == 0)
        return sema_method_error(sema, ast->span);

    if (ast->call.generic_args.len != 0) {
        DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_GENERIC_ARGUMENTS_ON_NON_GENERIC, ast->span, STRING("type arguments supplied to a non-generic method"));
        diag_finish(&diagnostic);
        return sema_method_error(sema, ast->span);
    }

    size_t expected_arguments = function_type->function.params.len - 1;
    if (expected_arguments != ast->call.args.len) {
        error_wrong_argument_count(sema->diags, expected_arguments, ast->call.args.len, ast->span);
        return sema_method_error(sema, ast->span);
    }

    if (method->decl->fn.comptime && !sema->comptime && !(sema->current_fn != NULL && sema->current_fn->is_comptime)) {
        error_cant_call_comptime(sema->diags, ast->span);
        return sema_method_error(sema, ast->span);
    }

    HirType *receiver_param = ((HirType **)function_type->function.params.data)[0];

    if (receiver_param->kind == HIR_TYPE_POINTER &&
        type_equal(receiver->type, receiver_param->pointer.pointee) &&
        is_place(receiver)) {
        HirExpr *address = arena_calloc(sema->arena, sizeof(*address));
        *address = (HirExpr){
            .span = receiver->span,
            .kind = HIR_EXPR_UNARY,
            .type = pointer_type(sema, receiver->type, false),
            .unary = {
                .op = AST_UNARY_ADDRESS,
                .operand = receiver,
            },
        };
        receiver = address;
    }

    receiver = sema_coerce(sema, receiver, receiver_param);
    if (receiver == NULL || receiver->kind == HIR_EXPR_ERROR)
        return sema_method_error(sema, ast->span);

    HirExpr *call = arena_calloc(sema->arena, sizeof(*call));
    *call = (HirExpr){
        .span = ast->span,
        .kind = HIR_EXPR_CALL,
        .type = function_type->function.ret,
        .call = {
            .function = method,
            .args = array_create(sema->arena, sizeof(HirExpr *)),
        },
    };
    array_push(&call->call.args, &receiver);

    for (size_t i = 0; i < ast->call.args.len; i++) {
        HirType *param_type = ((HirType **)function_type->function.params.data)[i + 1];
        HirExpr *value = sema_expr(sema, ((AstExpr **)ast->call.args.data)[i], param_type);
        if (value == NULL || value->kind == HIR_EXPR_ERROR)
            return sema_method_error(sema, ast->span);
        array_push(&call->call.args, &value);
    }

    if (expected != NULL) {
        HirExpr *coerced = sema_coerce(sema, call, expected);
        return coerced != NULL ? coerced : sema_method_error(sema, ast->span);
    }

    return call;
}

HirExpr *sema_expr(Sema *sema, AstExpr *ast, HirType *expected) {
    if (ast->comptime) {
        AstExpr copy = *ast;
        copy.comptime = false;

        bool previous = sema->comptime;
        sema->comptime = true;

        HirExpr *hir = sema_expr(sema, &copy, expected);

        sema->comptime = previous;

        if (hir == NULL || hir->kind == HIR_EXPR_ERROR)
            return hir;

        CompContext context = {
            .sema = sema,
            .frame = NULL,
        };

        HirExpr *result = comp_eval_expr(&context, hir);

        if (!comp_expr_is_evaluable(result)) {
            error_comptime_not_evaluable(sema->diags, ast->span);

            HirExpr *error = arena_calloc(sema->arena, sizeof(*error));
            error->span = ast->span;
            error->kind = HIR_EXPR_ERROR;
            return error;
        }

        if (expected != NULL && result->kind == HIR_EXPR_LITERAL) {
            if (!literal_fits(sema, &result->literal, expected)) {
                error_type_mismatch(sema->diags, expected, result->type, ast->span);

                HirExpr *error = arena_calloc(sema->arena, sizeof(*error));
                error->span = ast->span;
                error->kind = HIR_EXPR_ERROR;
                return error;
            }

            result->type = expected;
        }

        return result;
    }

    HirExpr *hir = arena_calloc(sema->arena, sizeof(HirExpr));

    hir->span = ast->span;
    hir->type = NULL;

    switch (ast->kind) {
        case AST_EXPR_LITERAL:
            hir->kind = HIR_EXPR_LITERAL;
            hir->literal = literal(sema, ast->literal);

            if (expected != NULL)
                return expr_coerce(sema, hir, expected);

            switch (hir->literal.kind) {
                case HIR_LITERAL_INTEGER:
                    hir->type = builtin_type(sema, BUILTIN_INT64);
                    break;

                default:
                    break;
            }

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
                return expr_coerce(sema, hir, expected);

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
                return expr_coerce(sema, hir, expected);

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

            HirType *type = unary_type(sema, hir->unary.op, operand);

            if (type == NULL) {
                error_invalid_unary_operation(sema->diags, unary_op_name(hir->unary.op), operand->type, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->unary.operand = operand;
            hir->type = type;

            if (expected != NULL)
                return expr_coerce(sema, hir, expected);

            return hir;
        }

        case AST_EXPR_BINARY: {
            hir->kind = HIR_EXPR_BINARY;
            hir->binary.op = ast->binary.op;

            HirExpr *left = sema_expr(sema, ast->binary.left, NULL);

            HirExpr *right = sema_expr(sema, ast->binary.right, NULL);

            HirType *operand_type = binop_type(sema, ast->binary.op, left, right, expected);

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
            hir->type = binary_result_type(sema, ast->binary.op, left->type, right->type, operand_type);

            if (hir->type == NULL) {
                error_invalid_binary_operation(sema->diags, binary_op_name(ast->binary.op), left->type, right->type, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            return hir;
        }

        case AST_EXPR_CALL: {
            bool method_handled = false;
            HirExpr *method_call = sema_method_call(sema, ast, expected, &method_handled);
            if (method_handled)
                return method_call;

            Symbol *generic = NULL;

            if (ast->call.callee != NULL && ast->call.callee->kind == AST_EXPR_IDENT)
                generic = sema_lookup(sema, ast->call.callee->ident);
            else if (ast->call.callee != NULL && ast->call.callee->kind == AST_EXPR_PATH)
                generic = sema_lookup_path(sema, ast->call.callee->path);

            if (generic != NULL && generic->kind == SYMBOL_FN && generic->decl != NULL &&
                generic->decl->kind == AST_DECL_FN && generic->decl->fn.generics.len != 0)
                return sema_generic_call(sema, ast, expected, generic);

            if (ast->call.generic_args.len != 0) {
                DiagBuilder diagnostic = diag_begin(sema->diags, DIAG_ERROR, E_GENERIC_ARGUMENTS_ON_NON_GENERIC, ast->span, STRING("type arguments supplied to a non-generic function"));
                diag_finish(&diagnostic);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->kind = HIR_EXPR_CALL;
            hir->call.function = NULL;
            hir->call.args = array_create(sema->arena, sizeof(HirExpr *));

            HirExpr *callee = sema_expr(sema, ast->call.callee, NULL);

            if (callee == NULL || callee->kind == HIR_EXPR_ERROR) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            HirType *function_type = call_type(sema, callee);

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

            if (callee->value.symbol->decl->fn.comptime && !sema->comptime && !(sema->current_fn != NULL && sema->current_fn->is_comptime)) {
                error_cant_call_comptime(sema->diags, callee->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

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
                hir = expr_coerce(sema, hir, expected);

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

            HirType *object_type = hir->index.object->type;

            if (object_type == NULL) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (object_type->kind == HIR_TYPE_POINTER) {
                HirType *pointee = object_type->pointer.pointee;

                if (pointee == NULL) {
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }

                if (pointee->kind != HIR_TYPE_ARRAY && pointee->kind != HIR_TYPE_SLICE) {
                    hir->type = pointee;

                    if (expected != NULL)
                        return expr_coerce(sema, hir, expected);

                    return hir;
                }
            }

            HirExpr *object = implicit_deref(sema, hir->index.object);
            hir->index.object = object;
            object_type = object->type;

            if (object_type == NULL) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            switch (object_type->kind) {
                case HIR_TYPE_ARRAY:
                    hir->type = type_with_mutability(sema, object_type->array.element, object_type->mutable);
                    HirExpr *index = hir->index.index;

                    if (index->kind == HIR_EXPR_LITERAL && index->literal.kind == HIR_LITERAL_INTEGER && index->literal.integer >= object_type->array.length) {
                        error_index_out_of_bounds(sema->diags, index->span, index->literal.integer, object_type->array.length);
                        hir->kind = HIR_EXPR_ERROR;
                        return hir;
                    }
                    break;

                case HIR_TYPE_SLICE:
                    hir->type = type_with_mutability(sema, object_type->slice.element, object_type->mutable);
                    break;

                default:
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
            }

            if (!is_integer(hir->index.index->type)) {
                error_expected_integer(sema->diags, hir->index.index->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (expected != NULL)
                return expr_coerce(sema, hir, expected);

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

                object = arena_calloc(sema->arena, sizeof(HirExpr));

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
                    field = field_lookup(object_type->structure.fields, ast->member.member);
                    break;

                case HIR_TYPE_UNION:
                    field = field_lookup(object_type->union_.fields, ast->member.member);
                    break;

                case HIR_TYPE_SLICE:
                    field = field_lookup(object_type->slice.fields, ast->member.member);
                    break;

                case HIR_TYPE_ARRAY:
                    if (ast->member.member.kind == AST_NAME_IDENT &&
                        string_eq(ast->member.member.ident, STRING("ptr"))) {
                        HirType *element_type = type_with_mutability(sema, object_type->array.element, object_type->mutable);
                        HirType *pointertype = pointer_type(sema, element_type, false);

                        HirExpr *index = arena_calloc(sema->arena, sizeof(*index));
                        *index = (HirExpr) {
                            .span = ast->span,
                            .kind = HIR_EXPR_LITERAL,
                            .type = builtin_type(sema, BUILTIN_UINT64),
                            .literal = {
                                .kind = HIR_LITERAL_INTEGER,
                                .integer = 0,
                            },
                        };

                        HirExpr *element = arena_calloc(sema->arena, sizeof(*element));
                        *element = (HirExpr) {
                            .span = ast->span,
                            .kind = HIR_EXPR_INDEX,
                            .type = element_type,
                            .index = {
                                .object = object,
                                .index = index,
                            },
                        };

                        hir->kind = HIR_EXPR_UNARY;
                        hir->type = pointertype;
                        hir->unary.op = AST_UNARY_ADDRESS;
                        hir->unary.operand = element;

                        return hir;
                    }

                    field = field_lookup(object_type->array.fields, ast->member.member);
                    break;

                default:
                    error_expected_member(sema->diags, ast->member.member.ident, object->type, ast->span);
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
            }
            
            if (field == NULL) {
                error_unknown_field(sema->diags, ast->member.member.ident, ast->span);
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->field.field = field;
            hir->type = type_with_mutability(sema, field->type, object->type->mutable);

            if (expected != NULL)
                return expr_coerce(sema, hir, expected);

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
                //! TODO: infer initialiser type
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (expected->kind != HIR_TYPE_STRUCT &&
                expected->kind != HIR_TYPE_UNION &&
                expected->kind != HIR_TYPE_ARRAY &&
                expected->kind != HIR_TYPE_SLICE) {
                //! TODO: expected aggregate initialiser
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (expected->kind == HIR_TYPE_ARRAY) {
                HirField *length_field = &((HirField *)expected->array.fields.data)[0];

                HirExpr *length = arena_calloc(sema->arena, sizeof(*length));
                *length = (HirExpr) {
                    .span = ast->span,
                    .kind = HIR_EXPR_LITERAL,
                    .type = length_field->type,
                    .literal = {
                        .kind = HIR_LITERAL_INTEGER,
                        .integer = expected->array.length,
                    },
                };

                HirInitField init_field = {
                    .field = length_field,
                    .offset = length_field->offset,
                    .value = length,
                };

                array_push(&hir->init.fields, &init_field);
            }

            size_t field_count = 0;

            switch (expected->kind) {
                case HIR_TYPE_STRUCT:
                    field_count = expected->structure.fields.len;
                    break;

                case HIR_TYPE_UNION:
                    field_count = expected->union_.fields.len;
                    break;

                case HIR_TYPE_ARRAY:
                    field_count = expected->array.length;
                    break;

                case HIR_TYPE_SLICE:
                    field_count = expected->slice.fields.len;
                    break;

                default:
                    break;
            }

            bool *initialized = arena_calloc(sema->arena, sizeof(bool) * field_count);
            size_t positional = 0;

            for (size_t i = 0; i < ast->init.fields.len; i++) {
                AstInitField *field = &((AstInitField *)ast->init.fields.data)[i];
                HirField *hir_field = NULL;
                size_t field_index = SIZE_MAX;
                size_t offset = 0;
                HirType *value_type = NULL;
                bool named = field->name.ident.length != 0;

                if (expected->kind == HIR_TYPE_ARRAY) {
                    if (named) {
                        //! TODO: invalid designated array initialiser
                        hir->kind = HIR_EXPR_ERROR;
                        return hir;
                    }

                    if (positional >= expected->array.length) {
                        //! TODO: too many initialiser values
                        hir->kind = HIR_EXPR_ERROR;
                        return hir;
                    }

                    field_index = positional++;
                    value_type = expected->array.element;
                    HirField *length_field = &((HirField *)expected->array.fields.data)[0];
                    size_t data_offset = align_up(length_field->offset + length_field->type->size, value_type->align);

                    offset = data_offset + field_index * value_type->size;
                } else {
                    if (named) {
                        hir_field = init_field_lookup(expected, field->name);

                        if (hir_field == NULL) {
                            //! TODO: unknown initialiser field diagnostic
                            hir->kind = HIR_EXPR_ERROR;
                            return hir;
                        }

                        if (expected->kind == HIR_TYPE_STRUCT)
                            field_index = (size_t)(hir_field - (HirField *)expected->structure.fields.data);
                        else if (expected->kind == HIR_TYPE_UNION)
                            field_index = (size_t)(hir_field - (HirField *)expected->union_.fields.data);
                        else
                            field_index = (size_t)(hir_field - (HirField *)expected->slice.fields.data);
                    } else {
                        while (positional < field_count && initialized[positional])
                            positional++;

                        if (positional >= field_count) {
                            //! TODO: too many initialiser values
                            hir->kind = HIR_EXPR_ERROR;
                            return hir;
                        }

                        field_index = positional++;

                        if (expected->kind == HIR_TYPE_STRUCT)
                            hir_field = &((HirField *)expected->structure.fields.data)[field_index];
                        else if (expected->kind == HIR_TYPE_UNION)
                            hir_field = &((HirField *)expected->union_.fields.data)[field_index];
                        else
                            hir_field = &((HirField *)expected->slice.fields.data)[field_index];
                    }

                    if (initialized[field_index]) {
                        //! TODO: duplicate initialiser diagnostic
                        hir->kind = HIR_EXPR_ERROR;
                        return hir;
                    }

                    initialized[field_index] = true;
                    value_type = hir_field->type;
                    offset = hir_field->offset;
                }

                HirExpr *value = sema_expr(sema, field->value, value_type);

                if (value == NULL || value->kind == HIR_EXPR_ERROR) {
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }

                HirInitField init_field = {
                    .field = hir_field,
                    .offset = offset,
                    .value = value,
                };

                array_push(&hir->init.fields, &init_field);
            }

            hir->type = expected;
            break;
        }
        
        case AST_EXPR_LAMBDA: {
            hir->kind = HIR_EXPR_LAMBDA;

            HirType *type = arena_calloc(sema->arena, sizeof(HirType));

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
                if (expected->kind != HIR_TYPE_FUNCTION || !type_equal(type, expected)) {
                    error_type_mismatch(sema->diags, expected, type, ast->span);
                    hir->kind = HIR_EXPR_ERROR;
                    return hir;
                }
            }

            HirFunction *fn = arena_calloc(sema->arena, sizeof(HirFunction));

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

                insert_parameter(sema, param, param_type);
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
            fprintf(stderr, "Internal compiler error at %s:%u", __FILE__, __LINE__);
            break;
    }

    return hir;
}
