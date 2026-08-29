#include "../print.h"

static void indent(FILE *out, unsigned depth) {
    for (unsigned i = 0; i < depth; ++i)
        fputs("    ", out);
}

static void print_span(FILE *out, Span span) {
    fprintf(
        out,
        "[%zu:%zu+%zu]",
        source_line(span.source, span.offset),
        source_column(span.source, span.offset),
        span.length
    );
}

static void print_string(FILE *out, String s) {
    fprintf(
        out,
        "\"%.*s\"",
        (int)s.length,
        s.data
    );
}

static void print_path(FILE *out, const Path *path) {
    for (size_t i = 0; i < path->parts.len; ++i) {
        if (i != 0)
            fputs("::", out);

        String *part = array_at((Array *)&path->parts, i);
        fprintf(out, "%.*s", (int)part->length, part->data);
    }
}

static void print_name(FILE *out, const AstName *name) {
    switch (name->kind) {
    case AST_NAME_IDENT:
        print_string(out, name->ident);
        break;

    case AST_NAME_SPLICE:
        fputs("#(", out);
        /* Expression printer goes here. */
        fputs("...)", out);
        break;
    }
}

static const char *type_kind_name(AstTypeKind kind) {
    switch (kind) {
    case AST_TYPE_NAMED:  return "named";
    case AST_TYPE_POINTER: return "pointer";
    case AST_TYPE_ARRAY:  return "array";
    case AST_TYPE_FN:     return "fn";
    case AST_TYPE_SUM:    return "sum";
    case AST_TYPE_STRUCT: return "struct";
    case AST_TYPE_UNION:  return "union";
    case AST_TYPE_ENUM:   return "enum";
    case AST_TYPE_SPLICE: return "splice";
    case AST_TYPE_ERROR:  return "error";
    }

    return "?";
}

static const char *expr_kind_name(AstExprKind kind) {
    switch (kind) {
    case AST_EXPR_ERROR:      return "error";
    case AST_EXPR_LITERAL:    return "literal";
    case AST_EXPR_IDENT:      return "ident";
    case AST_EXPR_PATH:       return "path";
    case AST_EXPR_UNARY:      return "unary";
    case AST_EXPR_BINARY:     return "binary";
    case AST_EXPR_CALL:       return "call";
    case AST_EXPR_INDEX:      return "index";
    case AST_EXPR_MEMBER:     return "member";
    case AST_EXPR_CAST:       return "cast";
    case AST_EXPR_INTRINSIC:  return "intrinsic";
    case AST_EXPR_BUBBLE:     return "bubble";
    case AST_EXPR_INIT:       return "init";
    case AST_EXPR_LAMBDA:     return "lambda";
    case AST_EXPR_SPLICE:     return "splice";
    }

    return "?";
}

static const char *stmt_kind_name(AstStmtKind kind) {
    switch (kind) {
    case AST_STMT_ERROR:    return "error";
    case AST_STMT_VAR:      return "var";
    case AST_STMT_EXPR:     return "expr";
    case AST_STMT_BLOCK:    return "block";
    case AST_STMT_RETURN:   return "return";
    case AST_STMT_IF:       return "if";
    case AST_STMT_FOR:      return "for";
    case AST_STMT_WHILE:    return "while";
    case AST_STMT_MATCH:    return "match";
    case AST_STMT_BREAK:    return "break";
    case AST_STMT_CONTINUE: return "continue";
    case AST_STMT_DEFER:    return "defer";
    }

    return "?";
}

static const char *decl_kind_name(int kind) {
    switch (kind) {
    case AST_DECL_ERROR:       return "none";
    case AST_DECL_INCLUDE:    return "include";
    case AST_DECL_TYPE:       return "type";
    case AST_DECL_VAR:        return "var";
    case AST_DECL_FN:         return "fn";
    case AST_DECL_CONSTRAINT: return "constraint";
    }

    return "?";
}

static void print_literal(
    FILE *out,
    const AstLiteral *literal
) {
    switch (literal->kind) {
    case AST_LIT_INTEGER:
        fputs("integer ", out);
        break;
    case AST_LIT_FLOAT:
        fputs("float ", out);
        break;
    case AST_LIT_STRING:
        fputs("string ", out);
        break;
    case AST_LIT_CHAR:
        fputs("char ", out);
        break;
    case AST_LIT_BOOL:
        fputs("bool ", out);
        break;
    case AST_LIT_ERROR:
        fputs("none ", out);
        break;
    }

    print_string(out, literal->raw);
}

static const char *unary_op_name(AstUnaryOp op) {
    switch (op) {
    case AST_UNARY_POS:     return "+";
    case AST_UNARY_NEG:     return "-";
    case AST_UNARY_NOT:     return "!";
    case AST_UNARY_BIT_NOT: return "~";
    case AST_UNARY_DEREF:   return "*";
    case AST_UNARY_ADDRESS: return "&";
    }

    return "?";
}

static const char *binary_op_name(AstBinaryOp op) {
    switch (op) {
    case AST_BINARY_ADD:          return "+";
    case AST_BINARY_SUB:          return "-";
    case AST_BINARY_MUL:          return "*";
    case AST_BINARY_DIV:          return "/";
    case AST_BINARY_MOD:          return "%";
    case AST_BINARY_EQUAL:        return "==";
    case AST_BINARY_NOT_EQUAL:    return "!=";
    case AST_BINARY_LT:           return "<";
    case AST_BINARY_LTE:          return "<=";
    case AST_BINARY_GT:           return ">";
    case AST_BINARY_GTE:          return ">=";
    case AST_BINARY_LOGICAL_AND:  return "&&";
    case AST_BINARY_LOGICAL_OR:   return "||";
    case AST_BINARY_BIT_AND:      return "&";
    case AST_BINARY_BIT_OR:       return "|";
    case AST_BINARY_BIT_XOR:      return "^";
    case AST_BINARY_SHL:          return "<<";
    case AST_BINARY_SHR:          return ">>";
    case AST_BINARY_ASSIGN:       return "=";
    case AST_BINARY_ADD_ASSIGN:   return "+=";
    case AST_BINARY_SUB_ASSIGN:   return "-=";
    case AST_BINARY_MUL_ASSIGN:   return "*=";
    case AST_BINARY_DIV_ASSIGN:   return "/=";
    case AST_BINARY_MOD_ASSIGN:   return "%=";
    case AST_BINARY_BIT_AND_ASSIGN: return "&=";
    case AST_BINARY_BIT_OR_ASSIGN:  return "|=";
    case AST_BINARY_BIT_XOR_ASSIGN: return "^=";
    case AST_BINARY_SHL_ASSIGN:      return "<<=";
    case AST_BINARY_SHR_ASSIGN:      return ">>=";
    case AST_BINARY_NAND:            return "!&";
    case AST_BINARY_NAND_ASSIGN:     return "!&=";
    case AST_BINARY_NOR:             return "~|";
    case AST_BINARY_NOR_ASSIGN:      return "~|=";
    }

    return "?";
}

static void print_type(
    FILE *out,
    const AstType *type,
    unsigned depth
);

static void print_stmt(FILE *out, const AstStmt *stmt, unsigned depth);

static void print_expr(
    FILE *out,
    const AstExpr *expr,
    unsigned depth
) {
    indent(out, depth);

    fprintf(
        out,
        "expr %s",
        expr_kind_name(expr->kind)
    );

    if (expr->comptime)
        fputs(" comptime", out);

    fputs(" ", out);
    print_span(out, expr->span);
    fputc('\n', out);

    switch (expr->kind) {
    case AST_EXPR_LITERAL:
        indent(out, depth + 1);
        print_literal(out, &expr->lit.literal);
        fputc('\n', out);
        break;

    case AST_EXPR_IDENT:
        indent(out, depth + 1);
        print_name(out, &expr->ident.name);
        fputc('\n', out);
        break;

    case AST_EXPR_PATH:
        indent(out, depth + 1);
        print_path(out, &expr->path.path);
        fputc('\n', out);
        break;

    case AST_EXPR_UNARY:
        indent(out, depth + 1);
        fprintf(out, "op %s\n", unary_op_name(expr->unary.op));
        print_expr(out, expr->unary.operand, depth + 1);
        break;

    case AST_EXPR_BINARY:
        indent(out, depth + 1);
        fprintf(out, "op %s\n", binary_op_name(expr->binary.op));
        print_expr(out, expr->binary.left, depth + 1);
        print_expr(out, expr->binary.right, depth + 1);
        break;

    case AST_EXPR_CALL:
        print_expr(out, expr->call.callee, depth + 1);

        if (expr->call.generic_args.len != 0) {
            indent(out, depth + 1);
            fputs("generic-args\n", out);

            for (size_t i = 0;
                 i < expr->call.generic_args.len;
                 ++i) {
                AstType **arg = array_at(
                    (Array *)&expr->call.generic_args,
                    i
                );
                print_type(out, *arg, depth + 2);
            }
        }

        for (size_t i = 0;
             i < expr->call.args.len;
             ++i) {
            AstExpr **arg = array_at(
                (Array *)&expr->call.args,
                i
            );
            print_expr(out, *arg, depth + 1);
        }
        break;

    case AST_EXPR_INDEX:
        print_expr(out, expr->index.object, depth + 1);
        print_expr(out, expr->index.index, depth + 1);
        break;

    case AST_EXPR_MEMBER:
        print_expr(out, expr->member.object, depth + 1);

        indent(out, depth + 1);
        fputs("member ", out);
        print_name(out, &expr->member.member);
        fputc('\n', out);
        break;

    case AST_EXPR_CAST:
        print_type(out, expr->cast.type, depth + 1);
        print_expr(out, expr->cast.operand, depth + 1);
        break;

    case AST_EXPR_INTRINSIC:
        indent(out, depth + 1);
        fputs("name ", out);
        print_path(out, &expr->intrinsic.name);
        fputc('\n', out);

        for (size_t i = 0;
             i < expr->intrinsic.args.len;
             ++i) {
            AstExpr **arg = array_at(
                (Array *)&expr->intrinsic.args,
                i
            );
            print_expr(out, *arg, depth + 1);
        }
        break;

    case AST_EXPR_BUBBLE:
        print_expr(out, expr->bubble.operand, depth + 1);
        break;

    case AST_EXPR_INIT:
        for (size_t i = 0;
             i < expr->init.fields.len;
             ++i) {
            AstInitField *field = array_at(
                (Array *)&expr->init.fields,
                i
            );

            indent(out, depth + 1);
            fputs("field", out);

            if (field->name != NULL) {
                fputc(' ', out);
                print_name(out, field->name);
            }

            fputc('\n', out);
            print_expr(out, field->value, depth + 2);
        }
        break;

    case AST_EXPR_LAMBDA:
        print_type(out, expr->lambda.ret, depth + 1);

        for (size_t i = 0;
             i < expr->lambda.params.len;
             ++i) {
            AstParam *param = array_at(
                (Array *)&expr->lambda.params,
                i
            );

            indent(out, depth + 1);
            fputs("param ", out);
            print_name(out, &param->name);
            fputc('\n', out);

            print_type(out, param->type, depth + 2);
        }

        print_stmt(out, expr->lambda.body, depth + 1);
        break;

    case AST_EXPR_SPLICE:
        print_expr(out, expr->splice.expression, depth + 1);
        break;

    case AST_EXPR_ERROR:
        break;
    }
}

static void print_pattern(
    FILE *out,
    const AstPattern *pattern,
    unsigned depth
) {
    indent(out, depth);

    switch (pattern->kind) {
    case AST_PATTERN_ERROR:
        fputs("pattern error", out);
        break;

    case AST_PATTERN_WILDCARD:
        fputs("pattern wildcard", out);
        break;

    case AST_PATTERN_LITERAL:
        fputs("pattern literal ", out);
        print_literal(out, &pattern->literal);
        break;

    case AST_PATTERN_BINDING:
        fputs("pattern binding ", out);
        print_name(out, &pattern->binding);
        break;

    case AST_PATTERN_VARIANT:
        fputs("pattern variant ", out);
        print_name(out, &pattern->variant.name);
        fputc(' ', out);
        print_name(out, &pattern->variant.binding);
        break;

    case AST_PATTERN_EXPR:
        fputs("pattern expr", out);
        break;
    }

    fputc('\n', out);

    if (pattern->kind == AST_PATTERN_EXPR)
        print_expr(out, pattern->expr, depth + 1);
}

static void print_stmt(FILE *out, const AstStmt *stmt, unsigned depth) {
    if (stmt == NULL)
        return;

    indent(out, depth);

    fprintf(
        out,
        "stmt %s",
        stmt_kind_name(stmt->kind)
    );

    if (stmt->comptime)
        fputs(" comptime", out);

    fputs(" ", out);
    print_span(out, stmt->span);
    fputc('\n', out);

    switch (stmt->kind) {
    case AST_STMT_VAR:
        print_name(out, &stmt->var.var->name);
        fputc('\n', out);
        print_type(out, stmt->var.var->type, depth + 1);

        if (stmt->var.var->init != NULL)
            print_expr(out, stmt->var.var->init, depth + 1);
        break;

    case AST_STMT_EXPR:
        print_expr(out, stmt->expr.expr, depth + 1);
        break;

    case AST_STMT_BLOCK:
        for (size_t i = 0;
             i < stmt->block.stmts.len;
             ++i) {
            AstStmt **child = array_at(
                (Array *)&stmt->block.stmts,
                i
            );
            print_stmt(out, *child, depth + 1);
        }
        break;

    case AST_STMT_RETURN:
        if (stmt->_return.value != NULL)
            print_expr(out, stmt->_return.value, depth + 1);
        break;

    case AST_STMT_IF:
        print_expr(out, stmt->_if.cond, depth + 1);
        print_stmt(out, stmt->_if.then, depth + 1);
        print_stmt(out, stmt->_if._else, depth + 1);
        break;

    case AST_STMT_WHILE:
        print_expr(out, stmt->_while.cond, depth + 1);
        print_stmt(out, stmt->_while.body, depth + 1);
        break;

    case AST_STMT_FOR:
        print_stmt(out, stmt->_for.init, depth + 1);

        if (stmt->_for.cond != NULL)
            print_expr(out, stmt->_for.cond, depth + 1);

        if (stmt->_for.post != NULL)
            print_expr(out, stmt->_for.post, depth + 1);

        print_stmt(out, stmt->_for.body, depth + 1);
        break;

    case AST_STMT_DEFER:
        print_stmt(out, stmt->defer.deferred, depth + 1);
        break;

    case AST_STMT_MATCH:
        print_expr(out, stmt->match.expr, depth + 1);

        for (size_t i = 0;
             i < stmt->match.cases.len;
             ++i) {
            AstMatchCase *case_ = array_at(
                (Array *)&stmt->match.cases,
                i
            );

            print_pattern(out, case_->pattern, depth + 1);
            print_stmt(out, case_->body, depth + 1);
        }
        break;

    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
        if (stmt->_break.value != NULL)
            print_expr(out, stmt->_break.value, depth + 1);
        break;

    case AST_STMT_ERROR:
        break;
    }
}

static void print_field(
    FILE *out,
    const AstField *field,
    unsigned depth
) {
    indent(out, depth);
    fputs("field ", out);
    print_name(out, &field->name);
    fputc('\n', out);

    print_type(out, field->type, depth + 1);
}

static void print_type(
    FILE *out,
    const AstType *type,
    unsigned depth
) {
    indent(out, depth);
    fprintf(
        out,
        "type %s",
        type_kind_name(type->kind)
    );

    if (type->mutable)
        fputs(" mut", out);

    fputs(" ", out);
    print_span(out, type->span);
    fputc('\n', out);

    switch (type->kind) {
    case AST_TYPE_NAMED:
        indent(out, depth + 1);
        fputs("path ", out);
        print_path(out, &type->named.path);
        fputc('\n', out);

        for (size_t i = 0; i < type->named.args.len; ++i) {
            AstType **arg = array_at(
                (Array *)&type->named.args,
                i
            );
            print_type(out, *arg, depth + 1);
        }
        break;

    case AST_TYPE_POINTER:
        indent(out, depth + 1);
        fprintf(
            out,
            "optional: %s\n",
            type->pointer.optional ? "true" : "false"
        );
        print_type(out, type->pointer.pointee, depth + 1);
        break;

    case AST_TYPE_ARRAY:
        indent(out, depth + 1);
        fprintf(
            out,
            "sized: %s\n",
            type->array.sized ? "true" : "false"
        );

        if (type->array.length != NULL)
            print_expr(out, type->array.length, depth + 1);

        print_type(out, type->array.element, depth + 1);
        break;

    case AST_TYPE_FN:
        print_type(out, type->fn.ret, depth + 1);

        for (size_t i = 0; i < type->fn.params.len; ++i) {
            AstType **param = array_at(
                (Array *)&type->fn.params,
                i
            );
            print_type(out, *param, depth + 1);
        }
        break;

    case AST_TYPE_SUM:
        for (size_t i = 0; i < type->sum.members.len; ++i) {
            AstType **member = array_at(
                (Array *)&type->sum.members,
                i
            );
            print_type(out, *member, depth + 1);
        }
        break;

    case AST_TYPE_STRUCT:
        for (size_t i = 0; i < type->structure.fields.len; ++i) {
            AstField *field = array_at(
                (Array *)&type->structure.fields,
                i
            );
            print_field(out, field, depth + 1);
        }
        break;

    case AST_TYPE_UNION:
        for (size_t i = 0; i < type->union_.fields.len; ++i) {
            AstField *field = array_at(
                (Array *)&type->union_.fields,
                i
            );
            print_field(out, field, depth + 1);
        }
        break;

    case AST_TYPE_ENUM:
        if (type->enumeration.underlying != NULL)
            print_type(
                out,
                type->enumeration.underlying,
                depth + 1
            );

        for (size_t i = 0; i < type->enumeration.items.len; ++i) {
            AstEnumItem *item = array_at(
                (Array *)&type->enumeration.items,
                i
            );

            indent(out, depth + 1);
            fputs("enum-item ", out);
            print_name(out, &item->name);
            fputc('\n', out);

            if (item->value != NULL)
                print_expr(out, item->value, depth + 2);
        }
        break;

    case AST_TYPE_SPLICE:
        print_expr(out, type->splice.expr, depth + 1);
        break;

    case AST_TYPE_ERROR:
        break;
    }
}

static void print_attributes(
    FILE *out,
    const Array *attrs,
    unsigned depth
) {
    for (size_t i = 0; i < attrs->len; ++i) {
        AstAttribute *attr = array_at((Array *)attrs, i);

        indent(out, depth);
        fputs("attribute ", out);
        print_string(out, attr->name);
        fputc('\n', out);

        for (size_t j = 0; j < attr->args.len; ++j) {
            AstExpr **arg = array_at(
                &attr->args,
                j
            );
            print_expr(out, *arg, depth + 1);
        }
    }
}

static void print_param(
    FILE *out,
    const AstParam *param,
    unsigned depth
) {
    indent(out, depth);
    fputs("param ", out);
    print_name(out, &param->name);
    fputc('\n', out);

    print_type(out, param->type, depth + 1);
}

static void print_constraint_item(
    FILE *out,
    const AstConstraintItem *item,
    unsigned depth
) {
    indent(out, depth);

    switch (item->kind) {
    case AST_CONSTRAINT_METHOD:
        fputs("constraint method\n", out);

        if (item->method->receiver != NULL)
            print_type(
                out,
                item->method->receiver,
                depth + 1
            );

        print_name(out, &item->method->name);

        print_type(out, item->method->ret, depth + 1);

        for (size_t i = 0;
             i < item->method->params.len;
             ++i) {
            AstParam *param = array_at(
                (Array *)&item->method->params,
                i
            );
            print_param(out, param, depth + 1);
        }

        if (item->method->body != NULL)
            print_stmt(out, item->method->body, depth + 1);
        break;

    case AST_CONSTRAINT_FIELD:
        fputs("constraint field\n", out);
        print_field(out, &item->field, depth + 1);
        break;

    case AST_CONSTRAINT_EXPR:
        fputs("constraint expression\n", out);
        print_expr(out, item->expr, depth + 1);
        break;
    }
}

static void print_decl(FILE *out, const AstDecl *decl, unsigned depth) {
    indent(out, depth);

    fprintf(
        out,
        "decl %s ",
        decl_kind_name(decl->kind)
    );
    print_span(out, decl->span);
    fputc('\n', out);

    print_attributes(out, &decl->attrs, depth + 1);

    switch (decl->kind) {
    case AST_DECL_INCLUDE:
        indent(out, depth + 1);
        fputs("path ", out);
        print_path(out, &decl->include.path);
        if (decl->include.alias.parts.len > 0) {
            fputs(" alias ", out);
            print_path(out, &decl->include.alias);
        }
        fputc('\n', out);
        break;

    case AST_DECL_TYPE:
        indent(out, depth + 1);
        fputs("name ", out);
        print_name(out, &decl->type.name);
        fputc('\n', out);

        print_type(out, decl->type.type, depth + 1);
        break;

    case AST_DECL_VAR:
        indent(out, depth + 1);
        fputs("name ", out);
        print_name(out, &decl->var.name);
        fputc('\n', out);

        print_type(out, decl->var.type, depth + 1);

        if (decl->var.init != NULL)
            print_expr(out, decl->var.init, depth + 1);
        break;

    case AST_DECL_FN:
        indent(out, depth + 1);
        fputs("fn ", out);
        print_name(out, &decl->fn.name);
        fputc('\n', out);

        print_type(out, decl->fn.ret, depth + 1);

        if (decl->fn.receiver != NULL)
            print_type(
                out,
                decl->fn.receiver,
                depth + 1
            );

        for (size_t i = 0;
             i < decl->fn.params.len;
             ++i) {
            AstParam *param = array_at(
                (Array *)&decl->fn.params,
                i
            );
            print_param(out, param, depth + 1);
        }

        if (decl->fn.body != NULL)
            print_stmt(out, decl->fn.body, depth + 1);
        break;

    case AST_DECL_CONSTRAINT:
        indent(out, depth + 1);
        fputs("name ", out);
        print_name(out, &decl->constraint.name);
        fputc('\n', out);

        for (size_t i = 0;
             i < decl->constraint.items.len;
             ++i) {
            AstConstraintItem *item = array_at(
                (Array *)&decl->constraint.items,
                i
            );
            print_constraint_item(out, item, depth + 1);
        }
        break;

    case AST_DECL_ERROR:
        break;
    }
}

void print_ast_module(FILE *out, const AstModule *module) {
    fputs("module ", out);
    print_path(out, &module->path);

    fputs(" ", out);
    print_span(out, module->span);
    fputc('\n', out);

    for (size_t i = 0; i < module->decls.len; ++i) {
        AstDecl **decl = array_at(
            (Array *)&module->decls,
            i
        );
        print_decl(out, *decl, 1);
    }
}