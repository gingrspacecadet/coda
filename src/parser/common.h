#ifndef PARSER_COMMON_H
#define PARSER_COMMON_H

#include "../parser.h"

void error_expected_token(Diags *diags, TokenType expected, Span span);
void error_expected_identifier(Diags *diags, Span span);
void error_expected_type(Diags *diags, Span span);
void error_expected_module(Diags *diags, Span span);
void error_expected_expression(Diags *diags, Span span);
void error_expected_declaration(Diags *diags, Span span);
void error_expected_pattern(Diags *diags, Span span);
void error_unexpected_token(Diags *diags, TokenType token, Span span);

static inline bool number_is_float(String s) {
    for (size_t i = 0; i < s.length; i++) {
        if (s.data[i] == '.' || s.data[i] == 'e' || s.data[i] == 'E')
            return true;
    }

    return false;
}

typedef struct {
    size_t lexer_index;

    Token current;
    Token previous;

    size_t diagnostic_count;
} Checkpoint;

static inline Checkpoint checkpoint(Parser *p) {
    return (Checkpoint) {
        .lexer_index = p->lexer->index,

        .current = p->current,
        .previous = p->previous,

        .diagnostic_count = p->diags->diags.len,
    };
}

static inline void restore(Parser *p, Checkpoint cp) {
    p->lexer->index = cp.lexer_index;

    p->current = cp.current;
    p->previous = cp.previous;

    p->diags->diags.len = cp.diagnostic_count;
}

static inline void advance(Parser *p) {
    p->previous = p->current;
    p->current = lexer_next(p->lexer);
}

static inline bool at(Parser *p, TokenType type) {
    return p->current.type == type;
}

static inline bool match(Parser *p, TokenType type) {
    if (!at(p, type))
        return false;

    advance(p);
    return true;
}

static inline bool expect(Parser *p, TokenType type) {
    if (match(p, type))
        return true;

    error_expected_token(p->diags, type, p->current.span);
    return false;
}


Path parse_path(Parser *p);
AstAttribute parse_attribute(Parser *p);
Array parse_attributes(Parser *p);
AstName parse_name(Parser *p);

bool token_is_identifier(Parser *p, const char *text);

int binary_binding_power(TokenType type, bool *right_assoc, AstBinaryOp *op);
AstExpr *parse_literal(Parser *p);
AstExpr *parse_identifier_or_path(Parser *p);
bool token_to_unary(TokenType type, AstUnaryOp *op);
AstExpr *parse_expression_prefix(Parser *p);
AstExpr *parse_hash_expression(Parser *p);
AstExpr *parse_init_expression(Parser *p);
AstExpr *parse_lambda_expression(Parser *p);
bool try_parse_generic_arguments(Parser *p, Array *args);
AstExpr *parse_expression_postfix(Parser *p, AstExpr *left);
AstExpr *parse_expression_bp(Parser *p, int min_bp);
AstExpr *parse_expression(Parser *p);

void recover_statement(Parser *p);
AstStmt *stmt_new(Parser *p, AstStmtKind kind, Span span);
AstStmt *parse_return_stmt(Parser *p);
AstStmt *parse_expr_stmt(Parser *p);
AstStmt *parse_var_stmt(Parser *p);
bool try_parse_var_stmt(Parser *p, AstStmt **out);
AstStmt *parse_if_stmt(Parser *p);
AstStmt *parse_while_stmt(Parser *p);
AstStmt *parse_for_stmt(Parser *p);
AstStmt *parse_defer_stmt(Parser *p);
AstStmt *parse_break_stmt(Parser *p);
AstStmt *parse_continue_stmt(Parser *p);
AstStmt *parse_block(Parser *p);
AstPattern *parse_pattern(Parser *p);
AstMatchCase parse_match_case(Parser *p);
AstStmt *parse_match_stmt(Parser *p);
AstStmt *parse_statement(Parser *p);

void recover_declaration(Parser *p);
bool try_parse_fn_decl(Parser *p, AstFnDecl *fn);
AstParam parse_param(Parser *p);
void parse_fn_decl(Parser *p, AstFnDecl *fn, Array attrs);
void parse_var_decl(Parser *p, AstVarDecl *var);
void parse_type_decl(Parser *p, AstTypeDecl *decl);
void parse_include_decl(Parser *p, AstIncludeDecl *decl);
bool try_parse_var_decl(Parser *p, AstVarDecl *var);
void parse_constraint_decl(Parser *p, AstConstraintDecl *decl);
AstDecl *parse_decl(Parser *p);

AstConstraintItem parse_constraint_item(Parser *p);
void parse_constraint_items(Parser *p, Array *items );
AstConstraintDecl *parse_inline_constraint(Parser *p);
AstConstraintRef parse_constraint_ref(Parser *p);
AstGenericParam parse_generic_param(Parser *p);
Array parse_generic_params(Parser *p);
AstField parse_field(Parser *p);
AstType *parse_struct_type(Parser *p);
AstType *parse_union_type(Parser *p);
AstType *parse_enum_type(Parser *p);
AstType *parse_type_single(Parser *p);
AstType *parse_type(Parser *p);

void error_expected_token(Diags *diags, TokenType expected, Span span);
void error_expected_identifier(Diags *diags, Span span);
void error_expected_type(Diags *diags, Span span);
void error_expected_expression(Diags *diags, Span span);
void error_expected_declaration(Diags *diags, Span span);
void error_expected_pattern(Diags *diags, Span span);
void error_unexpected_token(Diags *diags, TokenType token, Span span);

#endif