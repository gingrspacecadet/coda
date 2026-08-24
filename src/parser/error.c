#include <stdarg.h>
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
        string_make(format(diags->arena, "Expected token %.*s.", string_fmt(name)))
    );

    diag_finish(&b); 
}

void error_expected_identifier(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_IDENTIFIER,
        span,
        string_make("Expected an identifier.")
    );

    diag_finish(&b);
}

void error_expected_type(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_TYPE,
        span,
        string_make("Expected a type.")
    );

    diag_finish(&b);
}

void error_expected_module(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_DECLARATION,
        span,
        string_make("Expected a module.")
    );

    diag_finish(&b);
}

void error_expected_expression(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_EXPRESSION,
        span,
        string_make("Expected an expression.")
    );

    diag_finish(&b);
}

void error_expected_declaration(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_DECLARATION,
        span,
        string_make("Expected a declaration.")
    );

    diag_finish(&b);
}

void error_expected_pattern(Diags *diags, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_EXPECTED_PATTERN,
        span,
        string_make("Expected a pattern.")
    );

    diag_finish(&b);
}

void error_unexpected_token(Diags *diags, TokenType token, Span span) {
    DiagBuilder b = diag_begin(
        diags,
        DIAG_ERROR,
        E_UNEXPECTED_TOKEN,
        span,
        string_make(format(diags->arena, "Unexpected token %s", token_name(token)))
    );

    diag_finish(&b);
}

