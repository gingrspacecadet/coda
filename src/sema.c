#include "sema.h"

//! TODO: hash table / binary lookup
//! TODO: report duplicated symbols
void scope_insert(Scope *scope, Symbol *sym) {
    array_push(&scope->syms, sym);
}

static inline bool ast_name_equal(AstName *a, AstName *b) {
    if (a->kind != AST_NAME_IDENT || b->kind != AST_NAME_IDENT)
        return false;

    return string_eq(a->ident, b->ident);
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

Symbol *sema_lookup_path(Sema *sema, Path path);

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
                };
                break;

            case AST_DECL_FN:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_FN,
                    .name = d->fn.name,
                    .decl = d,
                };
                break;

            case AST_DECL_VAR:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_GLOBAL,
                    .name = d->var.name,
                    .decl = d,
                };
                break;

            case AST_DECL_CONSTRAINT:
                sym = arena_alloc(sema->arena, sizeof(Symbol));
                *sym = (Symbol) {
                    .kind = SYMBOL_CONSTRAINT,
                    .name = d->constraint.name,
                    .decl = d,
                };
                break;

            case AST_DECL_INCLUDE:
            break;
            
            //! TODO: report internal error
            case AST_DECL_ERROR:
            default:
                break;
        }

        if (sym != NULL)
            scope_insert(&sema->global_scope, sym);
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
                HirType *ap =
                    ((HirType **)a->function.params.data)[i];
                HirType *bp =
                    ((HirType **)b->function.params.data)[i];

                if (!sema_type_equal(ap, bp))
                    return false;
            }

            return true;

        case HIR_TYPE_SUM:
            if (a->sum.members.len != b->sum.members.len)
                return false;

            for (size_t i = 0; i < a->sum.members.len; i++) {
                HirType *am =
                    ((HirType **)a->sum.members.data)[i];
                HirType *bm =
                    ((HirType **)b->sum.members.data)[i];

                if (!sema_type_equal(am, bm))
                    return false;
            }

            return true;

        case HIR_TYPE_STRUCT:
            if (a->structure.fields.len != b->structure.fields.len)
                return false;

            for (size_t i = 0; i < a->structure.fields.len; i++) {
                HirField *af =
                    ((HirField *)a->structure.fields.data) + i;
                HirField *bf =
                    ((HirField *)b->structure.fields.data) + i;

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
                HirField *af =
                    ((HirField *)a->union_.fields.data) + i;
                HirField *bf =
                    ((HirField *)b->union_.fields.data) + i;

                if (af->symbol != bf->symbol)
                    return false;

                if (!sema_type_equal(af->type, bf->type))
                    return false;
            }

            return true;

        case HIR_TYPE_ENUM:
            if (!sema_type_equal(a->enumeration.underlying,
                                  b->enumeration.underlying))
                return false;

            if (a->enumeration.items.len !=
                b->enumeration.items.len)
                return false;

            for (size_t i = 0; i < a->enumeration.items.len; i++) {
                HirEnumItem *ai =
                    ((HirEnumItem *)a->enumeration.items.data) + i;
                HirEnumItem *bi =
                    ((HirEnumItem *)b->enumeration.items.data) + i;

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
                //! TODO: unknown type diagnostic
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

            if (symbol->kind != SYMBOL_TYPE) {
                //! TODO: expected type diagnostic
                hir->kind = HIR_TYPE_ERROR;
                return hir;
            }

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

                HirField hir_field = {
                    .type = sema_type(sema, field->type),
                };

                array_push(&hir->structure.fields, &hir_field);
            }
            break;

        case AST_TYPE_UNION:
            hir->kind = HIR_TYPE_UNION;
            hir->union_.fields = array_create(sema->arena, sizeof(HirField));

            for (size_t i = 0; i < ast->union_.fields.len; i++) {
                AstField *field = ((AstField *)ast->union_.fields.data) + i;

                HirField hir_field = {
                    .type = sema_type(sema, field->type),
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

static bool sema_literal_fits(Sema *sema, AstLiteral *literal, HirType *type) {
    if (literal->kind == AST_LIT_NULL)
        return type->kind == HIR_TYPE_POINTER &&
               type->pointer.optional;

    if (type->kind != HIR_TYPE_BUILTIN)
        return false;

    switch (literal->kind) {
        case AST_LIT_INTEGER:
        case AST_LIT_CHAR:
            //! TODO: integer range check
            return true;

        case AST_LIT_FLOAT:
            //! TODO: float range / precision check
            return true;

        case AST_LIT_BOOL:
            return type->builtin == BUILTIN_BOOL;

        case AST_LIT_STRING:
            //! TODO: string literal conversion
            return false;

        case AST_LIT_NULL:
            return false;

        case AST_LIT_ERROR:
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

        //! TODO: implicit conversion
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

HirExpr *sema_expr(Sema *sema, AstExpr *ast, HirType *expected) {
    HirExpr *hir = arena_alloc(sema->arena, sizeof(HirExpr));

    hir->span = ast->span;
    hir->type = NULL;

    switch (ast->kind) {
        case AST_EXPR_LITERAL:
            hir->kind = HIR_EXPR_LITERAL;
            hir->literal = ast->lit.literal;

            if (expected != NULL)
                hir = sema_expr_coerce(sema, hir, expected);

            break;

        case AST_EXPR_IDENT: {
            Symbol *symbol = sema_lookup(sema, ast->ident.name);

            if (symbol == NULL) {
                //! TODO: unknown symbol diagnostic
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->kind = HIR_EXPR_VALUE;
            hir->value.symbol = symbol;
            hir->type = sema_symbol_type(sema, symbol);

            if (expected != NULL)
                hir = sema_expr_coerce(sema, hir, expected);

            break;
        }

        case AST_EXPR_PATH: {
            Symbol *symbol = sema_lookup_path(sema, ast->path.path);

            if (symbol == NULL) {
                //! TODO: unknown symbol diagnostic
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->kind = HIR_EXPR_VALUE;
            hir->value.symbol = symbol;
            hir->type = sema_symbol_type(sema, symbol);

            if (expected != NULL)
                hir = sema_expr_coerce(sema, hir, expected);

            break;
        }

        case AST_EXPR_UNARY: {
            hir->kind = HIR_EXPR_UNARY;
            hir->unary.op = ast->unary.op;

            HirExpr *operand = sema_expr(sema, ast->unary.operand, NULL);

            if (operand == NULL) {
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            //! TODO: infer unary operand type
            //! TODO: propagate expected type
            //! TODO: validate unary operator

            hir->unary.operand = operand;
            break;
        }

        case AST_EXPR_BINARY: {
            hir->kind = HIR_EXPR_BINARY;
            hir->binary.op = ast->binary.op;

            HirExpr *left = sema_expr(sema, ast->binary.left, NULL);

            HirExpr *right = sema_expr(sema, ast->binary.right, NULL);

            HirType *operand_type = sema_binary_operand_type(sema, ast->binary.op, left, right, expected);

            if (operand_type == NULL) {
                //! TODO: incompatible operand types
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (left->type == NULL)
                left = sema_coerce(sema, left, operand_type);

            if (right->type == NULL)
                right = sema_coerce(sema, right, operand_type);

            if (left == NULL || right == NULL) {
                //! TODO: operands cannot be coerced
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            hir->binary.left = left;
            hir->binary.right = right;
            hir->type = sema_binary_result_type(sema, ast->binary.op, operand_type);

            if (hir->type == NULL) {
                //! TODO: invalid binary operation
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
                //! TODO: expected function
                hir->kind = HIR_EXPR_ERROR;
                return hir;
            }

            if (function_type->function.params.len != ast->call.args.len) {
                //! TODO: wrong argument count
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

            HirType *object_type = hir->index.object->type;

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
                hir = sema_expr_coerce(sema, hir, expected);

            break;
        }

        case AST_EXPR_MEMBER:
            hir->kind = HIR_EXPR_FIELD;
            hir->field.object = sema_expr(sema, ast->member.object, NULL);
            hir->field.field = NULL;

            //! TODO: resolve member
            //! TODO: determine result type
            break;

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

        case AST_EXPR_INIT:
            hir->kind = HIR_EXPR_INIT;
            hir->init.fields = array_create(sema->arena, sizeof(HirInitField));

            for (size_t i = 0; i < ast->init.fields.len; i++) {
                AstInitField *field = ((AstInitField *)ast->init.fields.data) + i;

                HirInitField hir_field = {
                    .field = NULL,
                    .value = sema_expr(sema, field->value, NULL),
                };

                //! TODO: resolve field
                //! TODO: pass field type as expected type

                array_push(&hir->init.fields, &hir_field);
            }
            break;

        case AST_EXPR_LAMBDA:
            hir->kind = HIR_EXPR_LAMBDA;
            //! TODO: analyse lambda
            break;

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

HirStmt *sema_stmt(Sema *sema, AstStmt *ast);

void sema_fn_decl(Sema *sema, AstFnDecl *ast) {
    Symbol *symbol = sema_lookup(sema, ast->name);

    if (symbol == NULL) {
        //! TODO: internal compiler error
        return;
    }

    if (symbol->kind != SYMBOL_FN) {
        //! TODO: internal compiler error
        return;
    }

    HirType *type = arena_alloc(sema->arena, sizeof(HirType));

    type->kind = HIR_TYPE_FUNCTION;
    type->mutable = false;
    type->function.ret = sema_type(sema, ast->ret);
    type->function.params = array_create(sema->arena, sizeof(HirType *));

    for (size_t i = 0; i < ast->params.len; i++) {
        AstParam *param = ((AstParam *)ast->params.data) + i;
        HirType *param_type = sema_type(sema, param->type);
        array_push(&type->function.params, &param_type);
    }

    symbol->type = type;

    //! TODO: create parameter symbols and scope
    //! TODO: analyse body
    //! TODO: create HirFunction
}

void sema_type_decl(Sema *sema, AstTypeDecl *ast) {
    Symbol *symbol = sema_lookup(sema, ast->name);

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

void sema_var_decl(Sema *sema, AstVarDecl *ast) {
    Symbol *symbol = sema_lookup(sema, ast->name);

    if (symbol == NULL) {
        //! TODO: internal compiler error
        return;
    }

    if (symbol->kind != SYMBOL_GLOBAL) {
        //! TODO: internal compiler error
        return;
    }

    symbol->type = sema_type(sema, ast->type);

    HirExpr *init = NULL;

    if (ast->init != NULL)
        init = sema_expr(sema, ast->init, symbol->type);

    //! TODO: create HirGlobal
}

void sema_constraint_decl(Sema *sema, AstConstraintDecl *ast);
void sema_include_decl(Sema *sema, AstIncludeDecl *ast);

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

HirModule *sema_analyse(Sema *sema, AstModule *module) {
    sema->module = module;
    HirModule *hmod = arena_alloc(sema->arena, sizeof(HirModule));

    collect_decls(sema, module->decls);

    for (size_t i = 0; i < module->decls.len; i++) {
        AstDecl *d = ((AstDecl **)module->decls.data)[i];
        sema_decl(sema, d);
    }

    if (diags_has_errors(sema->diags))
        return NULL;

    return hmod;
}