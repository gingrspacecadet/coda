#include "common.h"

String token_name(TokenType type) {
    switch (type) {
    case TK_LPAREN:      return string_make("'('");
    case TK_RPAREN:      return string_make("')'");
    case TK_LBRACK:      return string_make("'['");
    case TK_RBRACK:      return string_make("']'");
    case TK_LBRACE:      return string_make("'{'");
    case TK_RBRACE:      return string_make("'}'");
    case TK_SEMICOLON:   return string_make("';'");
    case TK_COMMA:       return string_make("','");
    case TK_GT:          return string_make("'>'");
    case TK_EQ:          return string_make("'='");
    case TK_DOT:         return string_make("'.'");
    case TK_COLON:       return string_make("':'");
    case TK_COLON_COLON: return string_make("'::'");

    case TK_KW_FN:       return string_make("'fn'");
    case TK_KW_TYPE:     return string_make("'type'");
    case TK_KW_INCLUDE:  return string_make("'include'");
    case TK_KW_CONSTRAINT:
        return string_make("'constraint'");

    default:
        return string_make("expected token");
    }
}

void error_expected_token(Diags *diags, TokenType expected, Span span) {
    String name = token_name(expected);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_TOKEN,
        span,
        string_make("Expected token")
    );

    diag_label(&b, span, name);
    diag_finish(&b); 
    
    printf("error_expected_token %s\n", TokenTypeNames[expected]);
}

void error_expected_identifier(Diags *diags, Span span) {
    printf("error_expected_identifier\n");
}

void error_expected_type(Diags *diags, Span span) {
    printf("error_expected_type\n");
}

void error_expected_module(Diags *diags, Span span) {
    printf("error_expected_module\n");
}

void error_expected_expression(Diags *diags, Span span) {
    printf("error_expected_expression\n");
}

void error_expected_declaration(Diags *diags, Span span) {
    printf("error_expected_declaration\n");
}

void error_expected_pattern(Diags *diags, Span span) {
    printf("error_expected_pattern\n");
}

void error_unexpected_token(Diags *diags, Span span) {
    printf("error_unexpected_token\n");
}

