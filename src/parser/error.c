#include <stdarg.h>
#include "common.h"

String token_name(TokenType type) {
    switch (type) {
    case TK_LPAREN:      return STRING("'('");
    case TK_RPAREN:      return STRING("')'");
    case TK_LBRACK:      return STRING("'['");
    case TK_RBRACK:      return STRING("']'");
    case TK_LBRACE:      return STRING("'{'");
    case TK_RBRACE:      return STRING("'}'");
    case TK_SEMICOLON:   return STRING("';'");
    case TK_COMMA:       return STRING("','");
    case TK_GT:          return STRING("'>'");
    case TK_EQ:          return STRING("'='");
    case TK_DOT:         return STRING("'.'");
    case TK_COLON:       return STRING("':'");
    case TK_COLON_COLON: return STRING("'::'");

    case TK_KW_FN:       return STRING("'fn'");
    case TK_KW_TYPE:     return STRING("'type'");
    case TK_KW_INCLUDE:  return STRING("'include'");
    case TK_KW_CONSTRAINT:
        return STRING("'constraint'");

    default:
        return STRING("expected token");
    }
}

static char *format(Arena *arena, char *msg, ...) {
    char *buf;
    va_list args;
    va_start(args, msg);
    int n = vasprintf(&buf, msg, args);
    if (n == -1) return msg;
    
    char *nb = arena_alloc(arena, n);
    memcpy(nb, buf, n);
    free(buf);

    return nb;
}

void error_expected_token(Diags *diags, TokenType expected, Span span) {
    String name = token_name(expected);

    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_TOKEN,
        span,
        STRING(format(diags->arena, "Expected token %.*s.", string_fmt(name)))
    );

    diag_finish(&b); 
}

void error_expected_identifier(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_IDENTIFIER,
        span,
        STRING("Expected an identifier.")
    );

    diag_finish(&b);
}

void error_expected_type(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_TYPE,
        span,
        STRING("Expected a type.")
    );

    diag_finish(&b);
}

void error_expected_module(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_DECLARATION,
        span,
        STRING("Expected a module.")
    );

    diag_finish(&b);
}

void error_expected_expression(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_EXPRESSION,
        span,
        STRING("Expected an expression.")
    );

    diag_finish(&b);
}

void error_expected_declaration(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_DECLARATION,
        span,
        STRING("Expected a declaration.")
    );

    diag_finish(&b);
}

void error_expected_pattern(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_PATTERN,
        span,
        STRING("Expected a pattern.")
    );

    diag_finish(&b);
}

void error_unexpected_token(Diags *diags, TokenType token, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNEXPECTED_TOKEN,
        span,
        STRING(format(diags->arena, "Unexpected token %s", token_name(token)))
    );

    diag_finish(&b);
}

