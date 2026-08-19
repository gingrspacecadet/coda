#include "lexer.h"

static inline bool eof(Lexer *lx) {
    return lx->index >= lx->source->contents.length;
}

static inline char peek(Lexer *lx) {
    return eof(lx) ? '\0' : lx->source->contents.data[lx->index];
}

static inline char peek_ahead(Lexer *lx, size_t n) {
    size_t i = lx->index + n;
    return i >= lx->source->contents.length ? '\0' : lx->source->contents.data[i];
}

static inline char consume(Lexer *lx) {
    char c = peek(lx);
    if (!eof(lx)) lx->index++;
    return c;
}

static inline bool match(Lexer *lx, char expected) {
    if (peek(lx) == expected) {
        consume(lx);
        return true;
    }
    return false;
}

static inline bool is_ident_start(char c) {
    return  (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '_';
}

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static inline bool is_ident_continue(char c) {
    return is_ident_start(c) || is_digit(c);
}

static inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline Token token_make(Lexer *ctx, TokenType type, size_t offset, size_t length) {
    return (Token){
        .type = type,
        .span = (Span){
            .source = ctx->source,
            .offset = offset,
            .length = length,
        },
    };
}

static TokenType check_keyword(const char *text, size_t length) {
#define KW(name, type) \
    if (length == sizeof(name) - 1 && memcmp(text, name, sizeof(name) - 1) == 0) return type

    KW("module", TK_KW_MODULE);
    KW("include", TK_KW_INCLUDE);
    KW("fn", TK_KW_FN);
    KW("constraint", TK_KW_CONSTRAINT);
    KW("return", TK_KW_RETURN);
    KW("struct", TK_KW_STRUCT);
    KW("union", TK_KW_UNION);
    KW("defer", TK_KW_DEFER);
    KW("match", TK_KW_MATCH);
    KW("enum", TK_KW_ENUM);
    KW("type", TK_KW_TYPE);
    KW("mut", TK_KW_MUT);
    KW("if", TK_KW_IF);
    KW("else", TK_KW_ELSE);
    KW("for", TK_KW_FOR);
    KW("while", TK_KW_WHILE);
    KW("break", TK_KW_BREAK);
    KW("continue", TK_KW_CONTINUE);
    KW("true", TK_KW_TRUE);
    KW("false", TK_KW_FALSE);
    KW("null", TK_KW_NULL);

    return TK_IDENT;
}

Token lex_ident(Lexer *ctx, size_t start) {
    while (is_ident_continue(peek(ctx))) {
        consume(ctx);
    }

    size_t length = ctx->index - start;
    const char *text = &ctx->source->contents.data[start];
    TokenType type = check_keyword(text, length);

    return token_make(ctx, type, start, length);
}

static Token lex_number(Lexer *ctx, size_t start) {
    char first = ctx->source->contents.data[start];

    if (first == '0') {
        char next = peek(ctx);
        if (next == 'x' || next == 'X' || 
            next == 'b' || next == 'B' || 
            next == 'o' || next == 'O') {
            consume(ctx);
            
            while (is_digit(peek(ctx)) || 
                   (peek(ctx) >= 'a' && peek(ctx) <= 'f') ||
                   (peek(ctx) >= 'A' && peek(ctx) <= 'F') ||
                   peek(ctx) == '_') {
                consume(ctx);
            }
            return token_make(ctx, TK_NUMBER, start, ctx->index - start);
        }
    }

    while (is_digit(peek(ctx)) || peek(ctx) == '_') {
        consume(ctx);
    }

    if (peek(ctx) == '.') {
        consume(ctx);
        while (is_digit(peek(ctx)) || peek(ctx) == '_') {
            consume(ctx);
        }
    }

    if (peek(ctx) == 'e' || peek(ctx) == 'E') {
        consume(ctx);
        if (peek(ctx) == '+' || peek(ctx) == '-') {
            consume(ctx);
        }
        while (is_digit(peek(ctx)) || peek(ctx) == '_') {
            consume(ctx);
        }
    }

    return token_make(ctx, TK_NUMBER, start, ctx->index - start);
}

static Token lex_string(Lexer *ctx, size_t start) {
    while (!eof(ctx)) {
        char c = consume(ctx);
        
        if (c == '"') {
            break; // End of string
        }
        
        if (c == '\\' && !eof(ctx)) {
            consume(ctx); 
        }
    }
    
    return token_make(ctx, TK_STRING, start, ctx->index - start);
}

static Token lex_char(Lexer *ctx, size_t start) {
    while (!eof(ctx)) {
        char c = consume(ctx);
        
        if (c == '\'') {
            break;
        }
        
        if (c == '\\' && !eof(ctx)) {
            consume(ctx);
        }
    }
    
    return token_make(ctx, TK_CHAR, start, ctx->index - start);
}

Token lexer_next(Lexer *ctx) {
    while (is_space(peek(ctx))) {
        consume(ctx);
    }

    if (eof(ctx)) {
        return token_make(ctx, TK_EOF, ctx->index, 0);
    }

    size_t start = ctx->index;
    char c = consume(ctx);

    if (is_digit(c)) {
        return lex_number(ctx, start);
    }

    if (is_ident_start(c)) {
        return lex_ident(ctx, start);
    }

    switch (c) {
        case '(': return token_make(ctx, TK_LPAREN, start, 1);
        case ')': return token_make(ctx, TK_RPAREN, start, 1);
        case '{': return token_make(ctx, TK_LBRACE, start, 1);
        case '}': return token_make(ctx, TK_RBRACE, start, 1);
        case '[': return token_make(ctx, TK_LBRACK, start, 1);
        case ']': return token_make(ctx, TK_RBRACK, start, 1);
        case ';': return token_make(ctx, TK_SEMICOLON, start, 1);
        case '.': return token_make(ctx, TK_DOT, start, 1);
        case ',': return token_make(ctx, TK_COMMA, start, 1);
        case '#': return token_make(ctx, TK_POUND, start, 1);
        case '$': return token_make(ctx, TK_DOLLAR, start, 1);
        case '@': return token_make(ctx, TK_AT, start, 1);
        case '?': return token_make(ctx, TK_QUERY, start, 1);

        case '\'': return lex_char(ctx, start);
        case '\"': return lex_string(ctx, start);

        case ':':
            if (match(ctx, ':')) return token_make(ctx, TK_COLON_COLON, start, 2);
            return token_make(ctx, TK_COLON, start, 1);
        
        case '<':
            if (match(ctx, '=')) return token_make(ctx, TK_LT_EQ, start, 2);
            if (match(ctx, '<')) {
                if (match(ctx, '=')) return token_make(ctx, TK_SHL_EQ, start, 3);
                return token_make(ctx, TK_SHL, start, 2);
            }
            return token_make(ctx, TK_LT, start, 1);

        case '>':
            if (match(ctx, '=')) return token_make(ctx, TK_GT_EQ, start, 2);
            if (match(ctx, '>')) {
                if (match(ctx, '=')) return token_make(ctx, TK_SHR_EQ, start, 3);
                return token_make(ctx, TK_SHR, start, 2);
            }
            return token_make(ctx, TK_GT, start, 1);

        case '!':
            if (match(ctx, '=')) return token_make(ctx, TK_BANG_EQ, start, 2);
            if (match(ctx, '&')) {
                if (match(ctx, '=')) return token_make(ctx, TK_BANG_AMP_EQ, start, 3);
                return token_make(ctx, TK_BANG_AMP, start, 2);
            }
            if (match(ctx, '|')) {
                if (match(ctx, '=')) return token_make(ctx, TK_BANG_PIPE_EQ, start, 3);
                return token_make(ctx, TK_BANG_PIPE, start, 2);
            }
            if (match(ctx, '^')) {
                if (match(ctx, '=')) return token_make(ctx, TK_BANG_CARET_EQ, start, 3);
                return token_make(ctx, TK_BANG_CARET, start, 2);
            }
            return token_make(ctx, TK_BANG, start, 1);

        case '~':
            if (match(ctx, '=')) return token_make(ctx, TK_TILDE_EQ, start, 2);
            if (match(ctx, '&')) {
                if (match(ctx, '=')) return token_make(ctx, TK_TILDE_AMP_EQ, start, 3);
                return token_make(ctx, TK_TILDE_AMP, start, 2);
            }
            if (match(ctx, '|')) {
                if (match(ctx, '=')) return token_make(ctx, TK_TILDE_PIPE_EQ, start, 3);
                return token_make(ctx, TK_TILDE_PIPE, start, 2);
            }
            if (match(ctx, '^')) {
                if (match(ctx, '=')) return token_make(ctx, TK_TILDE_CARET_EQ, start, 3);
                return token_make(ctx, TK_TILDE_CARET, start, 2);
            }
            return token_make(ctx, TK_TILDE, start, 1);

        case '+':
            if (match(ctx, '=')) return token_make(ctx, TK_PLUS_EQ, start, 2);
            return token_make(ctx, TK_PLUS, start, 1);

        case '-':
            if (match(ctx, '=')) return token_make(ctx, TK_MINUS_EQ, start, 2);
            return token_make(ctx, TK_MINUS, start, 1);

        case '*':
            if (match(ctx, '=')) return token_make(ctx, TK_STAR_EQ, start, 2);
            return token_make(ctx, TK_STAR, start, 1);

        case '/':
            if (match(ctx, '=')) return token_make(ctx, TK_SLASH_EQ, start, 2);
            return token_make(ctx, TK_SLASH, start, 1);

        case '%':
            if (match(ctx, '=')) return token_make(ctx, TK_PERCENT_EQ, start, 2);
            return token_make(ctx, TK_PERCENT, start, 1);

        case '&':
            if (match(ctx, '&')) {
                if (match(ctx, '=')) return token_make(ctx, TK_AMP_AMP_EQ, start, 3);
                return token_make(ctx, TK_AMP_AMP, start, 2);
            }
            if (match(ctx, '=')) return token_make(ctx, TK_AMP_EQ, start, 2);
            return token_make(ctx, TK_AMP, start, 1);

        case '|':
            if (match(ctx, '|')) {
                if (match(ctx, '=')) return token_make(ctx, TK_PIPE_PIPE_EQ, start, 3);
                return token_make(ctx, TK_PIPE_PIPE, start, 2);
            }
            if (match(ctx, '=')) return token_make(ctx, TK_PIPE_EQ, start, 2);
            return token_make(ctx, TK_PIPE, start, 1);

        case '^':
            if (match(ctx, '=')) return token_make(ctx, TK_CARET_EQ, start, 2);
            return token_make(ctx, TK_CARET, start, 1);
    }

    DiagBuilder b = diag_begin(ctx->diags, DIAG_ERROR, 6767, (Span){.source = ctx->source, .offset = ctx->index}, string_make("Unexpected character"));
    diag_finish(&b);

    consume(ctx);
    return token_make(ctx, TK_ERROR, ctx->index - 1, 1);
}
