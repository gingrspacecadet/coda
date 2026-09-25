#include "../print.h"
#include "../sema/common.h"

static void print_symbol(FILE *out, const Symbol *symbol);
static void indent(FILE *out, unsigned depth) {
    for (unsigned i = 0; i < depth; i++)
        fputs("    ", out);
}

static const char *type_kind_name(HirTypeKind kind) {
    switch (kind) {
    case HIR_TYPE_ERROR:   return "error";
    case HIR_TYPE_BUILTIN: return "builtin";
    case HIR_TYPE_NAMED:   return "named";
    case HIR_TYPE_POINTER: return "pointer";
    case HIR_TYPE_SLICE:   return "slice";
    case HIR_TYPE_ARRAY:   return "array";
    case HIR_TYPE_FUNCTION:return "fn";
    case HIR_TYPE_SUM:     return "sum";
    case HIR_TYPE_STRUCT:  return "struct";
    case HIR_TYPE_UNION:   return "union";
    case HIR_TYPE_ENUM:    return "enum";
    }

    return "?";
}

static const char *expr_kind_name(HirExprKind kind) {
    switch (kind) {
    case HIR_EXPR_ERROR:     return "error";
    case HIR_EXPR_LITERAL:   return "literal";
    case HIR_EXPR_VALUE:     return "value";
    case HIR_EXPR_UNARY:     return "unary";
    case HIR_EXPR_BINARY:    return "binary";
    case HIR_EXPR_CALL:      return "call";
    case HIR_EXPR_INDEX:     return "index";
    case HIR_EXPR_FIELD:     return "field";
    case HIR_EXPR_CAST:      return "cast";
    case HIR_EXPR_INIT:      return "init";
    case HIR_EXPR_LAMBDA:    return "lambda";
    }

    return "?";
}

static const char *stmt_kind_name(HirStmtKind kind) {
    switch (kind) {
    case HIR_STMT_ERROR:    return "error";
    case HIR_STMT_EXPR:     return "expr";
    case HIR_STMT_BLOCK:    return "block";
    case HIR_STMT_ASSIGN:   return "assign";
    case HIR_STMT_RETURN:   return "return";
    case HIR_STMT_IF:       return "if";
    case HIR_STMT_WHILE:    return "while";
    case HIR_STMT_BREAK:    return "break";
    case HIR_STMT_CONTINUE: return "continue";
    }

    return "?";
}

static const char *builtin_name(BuiltinType builtin) {
    switch (builtin) {
    case BUILTIN_UINT8:  return "uint8";
    case BUILTIN_UINT16: return "uint16";
    case BUILTIN_UINT32: return "uint32";
    case BUILTIN_UINT64: return "uint64";
    case BUILTIN_INT8:   return "int8";
    case BUILTIN_INT16:  return "int16";
    case BUILTIN_INT32:  return "int32";
    case BUILTIN_INT64:  return "int64";
    case BUILTIN_BOOL:   return "bool";
    case BUILTIN_NONE:   return "none";
    }

    return "?";
}

static void print_type(FILE *out, const HirType *type, unsigned depth) {
    indent(out, depth);

    if (type == NULL) {
        fputs("type <null>\n", out);
        return;
    }

    fprintf(out, "type %s", type_kind_name(type->kind));

    if (type->mutable)
        fputs(" mut", out);

    fputc('\n', out);

    switch (type->kind) {
    case HIR_TYPE_BUILTIN:
        indent(out, depth + 1);
        fprintf(out, "%s\n", builtin_name(type->builtin));
        break;

    case HIR_TYPE_NAMED:
        indent(out, depth + 1);
        fputs("symbol ", out);
        print_symbol(out, type->named.symbol);
        fputc('\n', out);
        break;

    case HIR_TYPE_POINTER:
        indent(out, depth + 1);
        fprintf(
            out,
            "optional %s\n",
            type->pointer.optional ? "true" : "false"
        );
        print_type(out, type->pointer.pointee, depth + 1);
        break;

    case HIR_TYPE_SLICE:
        print_type(out, type->slice.element, depth + 1);
        break;

    case HIR_TYPE_ARRAY:
        indent(out, depth + 1);
        fprintf(out, "length %zu\n", type->array.length);
        print_type(out, type->array.element, depth + 1);
        break;

    case HIR_TYPE_FUNCTION:
        indent(out, depth + 1);
        fputs("return\n", out);
        print_type(out, type->function.ret, depth + 2);

        for (size_t i = 0; i < type->function.params.len; i++) {
            HirType *param =
                ((HirType **)type->function.params.data)[i];

            indent(out, depth + 1);
            fprintf(out, "param %zu\n", i);
            print_type(out, param, depth + 2);
        }
        break;

    case HIR_TYPE_SUM:
        for (size_t i = 0; i < type->sum.members.len; i++) {
            HirType *member =
                ((HirType **)type->sum.members.data)[i];
            print_type(out, member, depth + 1);
        }
        break;

    case HIR_TYPE_STRUCT:
        for (size_t i = 0; i < type->structure.fields.len; i++) {
            HirField *field =
                ((HirField *)type->structure.fields.data) + i;

            indent(out, depth + 1);
            fputs("field ", out);
            print_symbol(out, field->symbol);
            fputc('\n', out);

            print_type(out, field->type, depth + 2);
        }
        break;

    case HIR_TYPE_UNION:
        for (size_t i = 0; i < type->union_.fields.len; i++) {
            HirField *field =
                ((HirField *)type->union_.fields.data) + i;

            indent(out, depth + 1);
            fputs("field ", out);
            print_symbol(out, field->symbol);
            fputc('\n', out);

            print_type(out, field->type, depth + 2);
        }
        break;

    case HIR_TYPE_ENUM:
        indent(out, depth + 1);
        fputs("underlying\n", out);
        print_type(out, type->_enum.underlying, depth + 2);

        for (size_t i = 0; i < type->_enum.items.len; i++) {
            HirEnumItem *item =
                ((HirEnumItem *)type->_enum.items.data) + i;

            indent(out, depth + 1);
            fprintf(out, "item ");
            print_symbol(out, item->symbol);
            fprintf(out, " = %zu\n", item->value);

            print_type(out, item->type, depth + 2);
        }
        break;

    case HIR_TYPE_ERROR:
        break;
    }
}

static void print_literal(FILE *out, const HirLiteral *literal) {
    switch (literal->kind) {
    case HIR_LITERAL_INTEGER:
        fputs("integer ", out);
        break;
    case HIR_LITERAL_FLOAT:
        fputs("float ", out);
        break;
    case HIR_LITERAL_STRING:
        fputs("string ", out);
        break;
    case HIR_LITERAL_BOOL:
        fputs("bool ", out);
        break;
    case HIR_LITERAL_ERROR:
        fputs("error ", out);
        break;
    }
}


static void print_expr(FILE *out, const HirExpr *expr, unsigned depth) {
    indent(out, depth);

    if (expr == NULL) {
        fputs("expr <null>\n", out);
        return;
    }

    fprintf(out, "expr %s\n", expr_kind_name(expr->kind));

    indent(out, depth + 1);
    fputs("type\n", out);
    print_type(out, expr->type, depth + 2);

    switch (expr->kind) {
    case HIR_EXPR_LITERAL:
        indent(out, depth + 1);
        fputs("literal ", out);
        print_literal(out, &expr->literal);
        fputc('\n', out);
        break;

    case HIR_EXPR_VALUE:
        indent(out, depth + 1);
        fputs("symbol ", out);
        print_symbol(out, expr->value.symbol);
        fputc('\n', out);
        break;

    case HIR_EXPR_UNARY:
        indent(out, depth + 1);
        fprintf(out, "op %.*s\n", string_fmt(unary_op_name(expr->unary.op)));
        print_expr(out, expr->unary.operand, depth + 1);
        break;

    case HIR_EXPR_BINARY:
        indent(out, depth + 1);
        fprintf(out, "op %.*s\n", string_fmt(binary_op_name(expr->binary.op)));
        print_expr(out, expr->binary.left, depth + 1);
        print_expr(out, expr->binary.right, depth + 1);
        break;

    case HIR_EXPR_CALL:
        indent(out, depth + 1);
        fputs("function ", out);
        print_symbol(out, expr->call.function);
        fputc('\n', out);

        for (size_t i = 0; i < expr->call.args.len; i++) {
            HirExpr *arg =
                ((HirExpr **)expr->call.args.data)[i];
            print_expr(out, arg, depth + 1);
        }
        break;

    case HIR_EXPR_INDEX:
        print_expr(out, expr->index.object, depth + 1);
        print_expr(out, expr->index.index, depth + 1);
        break;

    case HIR_EXPR_FIELD:
        print_expr(out, expr->field.object, depth + 1);

        indent(out, depth + 1);
        fputs("field ", out);
        print_symbol(out, expr->field.field->symbol);
        fputc('\n', out);
        break;

    case HIR_EXPR_CAST:
        indent(out, depth + 1);
        fputs("target\n", out);
        print_type(out, expr->cast.type, depth + 2);
        print_expr(out, expr->cast.operand, depth + 1);
        break;

    case HIR_EXPR_INIT:
        for (size_t i = 0; i < expr->init.fields.len; i++) {
            HirInitField *field =
                ((HirInitField *)expr->init.fields.data) + i;

            indent(out, depth + 1);
            fputs("field ", out);
            print_symbol(out, field->field->symbol);
            fputc('\n', out);

            print_expr(out, field->value, depth + 2);
        }
        break;

    case HIR_EXPR_LAMBDA:
        break;

    case HIR_EXPR_ERROR:
        break;
    }
}

static void print_stmt(FILE *out, const HirStmt *stmt, unsigned depth) {
    if (stmt == NULL)
        return;

    indent(out, depth);
    fprintf(out, "stmt %s\n", stmt_kind_name(stmt->kind));

    switch (stmt->kind) {
    case HIR_STMT_EXPR:
        print_expr(out, stmt->expr, depth + 1);
        break;

    case HIR_STMT_BLOCK:
        for (size_t i = 0; i < stmt->block.stmts.len; i++) {
            HirStmt *child =
                ((HirStmt **)stmt->block.stmts.data)[i];
            print_stmt(out, child, depth + 1);
        }
        break;

    case HIR_STMT_ASSIGN:
        print_expr(out, stmt->assign.target, depth + 1);
        print_expr(out, stmt->assign.value, depth + 1);
        break;

    case HIR_STMT_RETURN:
        if (stmt->_return.value != NULL)
            print_expr(out, stmt->_return.value, depth + 1);
        break;

    case HIR_STMT_IF:
        print_expr(out, stmt->_if.cond, depth + 1);
        print_stmt(out, stmt->_if.then, depth + 1);
        print_stmt(out, stmt->_if._else, depth + 1);
        break;

    case HIR_STMT_WHILE:
        print_expr(out, stmt->_while.cond, depth + 1);
        print_stmt(out, stmt->_while.body, depth + 1);
        break;

    case HIR_STMT_BREAK:
        indent(out, depth + 1);
        fprintf(out, "level %zu\n", stmt->_break.level);
        break;

    case HIR_STMT_CONTINUE:
        indent(out, depth + 1);
        fprintf(out, "level %zu\n", stmt->_continue.level);
        break;

    case HIR_STMT_ERROR:
        break;
    }
}

static void print_name(FILE *out, const AstName *name) {
    switch (name->kind) {
    case AST_NAME_IDENT:
        fprintf(out, "%.*s", string_fmt(name->ident));
        break;

    case AST_NAME_SPLICE:
        fputs("#(", out);
        /* Expression printer goes here. */
        fputs("...)", out);
        break;
    }
}

static void print_symbol(FILE *out, const Symbol *symbol) {
    if (symbol == NULL) {
        fputs("<null>", out);
        return;
    }

    print_name(out, &symbol->name);
}

void print_hir_module(FILE *out, const HirModule *module) {
    fputs("hir module\n", out);

    for (size_t i = 0; i < module->globals.len; i++) {
        HirGlobal *global = ((HirGlobal *)module->globals.data) + i;

        indent(out, 1);
        fputs("global ", out);
        print_symbol(out, global->symbol);
        fputc('\n', out);

        print_type(out, global->type, 2);
    }

    for (size_t i = 0; i < module->functions.len; i++) {
        HirFunction *fn = ((HirFunction *)module->functions.data) + i;

        indent(out, 1);
        fputs("function ", out);
        print_symbol(out, fn->symbol);
        fputc('\n', out);

        indent(out, 2);
        fputs("return\n", out);
        print_type(out, fn->return_type, 3);

        print_stmt(out, fn->body, 2);
    }
}